#include "Framework/Game/CatfishingPlayerController.h"
#include "EngineUtils.h"
#include "Fishing/CatFishingSession.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Interaction/Carry/CatCarryableActor.h"
#include "Framework/Game/CatfishingGameState.h"
#include "Components/InputComponent.h"
#include "Logging/CatLogContext.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/Presentation/CatFishingCameraComponent.h"
#include "Character/CatCharacterMovementComponent.h"

#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "Camp/CatCampHubActor.h"
#include "FishContainers/CatFishGuardActor.h"
#include "ShopEconomy/CatFishBuyerActor.h"
#include "ShopEconomy/CatShopKioskActor.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "AbilitySystem/Config/CatAbilityInputConfig.h"
#include "AbilitySystem/Config/CatAbilitySettings.h"
#include "AbilitySystem/Effects/CatFishingScoopCooldownEffect.h"
#include "AbilitySystem/BodyAction/Camp/CatCampBodyActionCommandComponent.h"
#include "AbilitySystem/BodyAction/Social/CatSocialBodyActionCommandComponent.h"
#include "AbilitySystem/Input/CatAbilityInputBindingComponent.h"
#include "Logging/CatLog.h"
#include "Online/CatOnlineSubsystem.h"
#include "Social/CatRoomOwnerService.h"
#include "Condition/CatConditionComponent.h"
#include "Growth/CatGrowthComponent.h"
#include "Collection/CatRunImprintService.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Equipment/CatEquipmentComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Fishing/CatFishingService.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Interaction/CatInteractable.h"
#include "Interaction/CatInteractionTags.h"
#include "Interaction/CatInteractionTargetingComponent.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatBackPackComponent.h"
#include "Inventory/CatInventoryInputTags.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/CatInventoryStatics.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "Net/UnrealNetwork.h"
#include "Profile/CatProfileSubsystem.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "ShopEconomy/Trading/CatShopTradeController.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"

// 构造流程：创建 Controller 负责的输入与命令路由组件；不读取 Pawn、World 或玩家身份，避免类默认对象阶段产生运行时依赖。
ACatfishingPlayerController::ACatfishingPlayerController()
{
	FishingCommandComponent = CreateDefaultSubobject<UCatFishingCommandComponent>(TEXT("FishingCommandComponent"));
	InteractionTargetingComponent = CreateDefaultSubobject<UCatInteractionTargetingComponent>(TEXT("InteractionTargetingComponent"));
	AbilityInputBindingComponent = CreateDefaultSubobject<UCatAbilityInputBindingComponent>(TEXT("AbilityInputBindingComponent"));
	CampBodyActionCommandComponent = CreateDefaultSubobject<UCatCampBodyActionCommandComponent>(
		TEXT("CampBodyActionCommandComponent"));
	SocialBodyActionCommandComponent = CreateDefaultSubobject<UCatSocialBodyActionCommandComponent>(
		TEXT("SocialBodyActionCommandComponent"));
}

// 钓鱼命令组件读取流程：返回构造期创建的唯一组件给输入、蓝图或测试夹具；调用方仍只能通过组件提交钓鱼命令，不能把它当作 PlayerController 状态副本。
UCatFishingCommandComponent* ACatfishingPlayerController::GetFishingCommandComponent() const
{
	return FishingCommandComponent;
}

// 帧流程：先消费新快照并刷新遮罩，使本帧输入看到最新锁；父类仍负责正常 Controller、相机和网络生命周期。
void ACatfishingPlayerController::Tick(const float DeltaSeconds)
{
	if (HasAuthority())
	{
		if (auto* BackPack = GetControlledBackPack(); BackPack && BackPack->GetQuickbarHeldSlot().ItemInstanceId.IsValid())
		{
			auto* Fishing = GetWorld()->GetSubsystem<UCatFishingService>();
			auto* Rod = Fishing ? Fishing->FindRodOperatedBy(PlayerState) : nullptr;
			if (!Rod || Rod->GetPresentationState().ItemInstanceId != BackPack->GetQuickbarHeldSlot().ItemInstanceId)
				BackPack->ClearQuickbarHeldSlotFromAuthority();
			else BackPack->SetQuickbarHeldSlotInUseFromAuthority(Fishing->FindActiveSessionByRod(Rod) != nullptr);
		}
	}

	ReconcileDayTransition();
	Super::Tick(DeltaSeconds);
}

// 锁查询流程：旅行清理后不再接受旧 World 的快照；其他时候直接读取当前 GameState，不用动画超时推断权威解锁。
bool ACatfishingPlayerController::IsDayTransitionInputBlocked() const
{
	const UWorld* World = GetWorld();
	if (!World || DayTransitionTravelWorld.Get() == World) return false;
	const ACatfishingGameState* State = World->GetGameState<ACatfishingGameState>();
	return State && State->GetRunPublicState().DayTransition.bActive
		&& !State->GetRunPublicState().DayTransition.bFailed;
}

// 调和流程：
// 1. 旧旅行 World 不再处理；GameState 替换时清理旧绑定，再订阅新的公开快照，晚到依赖由下一帧接入。
// 2. 输入与服务器移动只跟随 active/failed；重复通知不叠锁，换 Pawn 时归还旧组件并接管新组件。
// 3. 仅 owning client 把服务器秒数和公开快照交给独立 UI；等待确认不会开启翻天锁，只有后端接受后 DayTransition 才接管输入。
void ACatfishingPlayerController::ReconcileDayTransition()
{
	UWorld* World = GetWorld();
	if (!World || DayTransitionTravelWorld.Get() == World) return;
	ACatfishingGameState* State = World->GetGameState<ACatfishingGameState>();
	if (!State)
	{
		ClearDayTransition();
		return;
	}
	if (DayTransitionGameState.Get() != State)
	{
		ClearDayTransition();
		DayTransitionGameState = State;
		DayTransitionStateHandle = State->OnRunPublicStateChanged.AddUObject(this, &ThisClass::ReconcileDayTransition);
	}
	SetDayTransitionLocked(State->GetRunPublicState().DayTransition.bActive
		&& !State->GetRunPublicState().DayTransition.bFailed);
	if (IsLocalController())
	{
		ULocalPlayer* LocalPlayer = GetLocalPlayer();
		UCatLocalPlayerUISubsystem* UI = LocalPlayer ? LocalPlayer->GetSubsystem<UCatLocalPlayerUISubsystem>() : nullptr;
		if (UI)
		{
			UI->RefreshDayTransition(this, State->GetRunPublicState().DayTransition, State->GetServerWorldTimeSeconds());
			UI->RefreshAltarConfirmation(this, State->GetRunPublicState().AltarConfirmation);
		}
	}
}

// 锁配对流程：
// 1. 服务器先归还已换走或已结束的移动组件；只有组件仍处于本功能写入的 None 模式才恢复原模式和自定义编号。
// 2. 新组件保存原模式后停止并禁用移动；客户端只用输入锁，绝不写 CharacterMovement 模式。
// 3. 首次加锁先申请一层移动/视角忽略计数，再取消钓鱼、Ability、抓握与物理身体的持续输入，并压入本地阻断组件。
// 4. 解锁只归还本层计数和本组件；状态未变时不重复申请，日志只记录边沿。
void ACatfishingPlayerController::SetDayTransitionLocked(const bool bLocked)
{
	ACharacter* ControlledCharacter = Cast<ACharacter>(GetPawn());
	UCharacterMovementComponent* Movement = HasAuthority() && bLocked && ControlledCharacter ? ControlledCharacter->GetCharacterMovement() : nullptr;
	if (DayTransitionMovement.Get() != Movement)
	{
		if (UCharacterMovementComponent* Previous = DayTransitionMovement.Get())
		{
			if (Previous->MovementMode == MOVE_None)
			{
				Previous->SetMovementMode(static_cast<EMovementMode>(DayTransitionSavedMovementMode), DayTransitionSavedCustomMode);
			}
		}
		DayTransitionMovement = Movement;
		if (Movement)
		{
			DayTransitionSavedMovementMode = Movement->MovementMode;
			DayTransitionSavedCustomMode = Movement->CustomMovementMode;
			Movement->StopMovementImmediately();
			Movement->DisableMovement();
		}
	}
	if (bDayTransitionLocked == bLocked) return;
	bDayTransitionLocked = bLocked;
	SetIgnoreMoveInput(bLocked);
	SetIgnoreLookInput(bLocked);
	if (bLocked)
	{
		// 翻天锁只阻止后续 Enhanced Input 回调不足以取消先前的物理按住意图；统一入口同时撤销本地和服务器的对应输入路由。
		ClearPhysicalControlInput(TEXT("DayTransitionLocked"));
		if (ControlledCharacter)
		{
			ControlledCharacter->StopJumping();
			ControlledCharacter->ConsumeMovementInputVector();
			// 本地先清惯性，移动模式仍只由服务器写入，避免复制到达前继续滑行。
			if (UCharacterMovementComponent* CharacterMovement = ControlledCharacter->GetCharacterMovement())
			{
				CharacterMovement->StopMovementImmediately();
			}
		}
		SetSprintRequested(false, false);
		RotationInput = FRotator::ZeroRotator;
		if (IsLocalController())
		{
			DayTransitionInputBlocker = NewObject<UEnhancedInputComponent>(this);
			DayTransitionInputBlocker->Priority = MAX_int32;
			DayTransitionInputBlocker->bBlockInput = true;
			DayTransitionInputBlocker->RegisterComponent();
			PushInputComponent(DayTransitionInputBlocker);
		}
	}
	else if (DayTransitionInputBlocker)
	{
		PopInputComponent(DayTransitionInputBlocker);
		DayTransitionInputBlocker->DestroyComponent();
		DayTransitionInputBlocker = nullptr;
	}
	const ACatfishingGameState* State = DayTransitionGameState.Get();
	const FCatRunDayTransition* Transition = State ? &State->GetRunPublicState().DayTransition : nullptr;
	UE_LOG(LogCatRun, Log,
		TEXT("Event=day_transition_operation_lock RequestId=%s Locked=%d Committed=%d Failed=%d TargetDay=%d World=%s NetMode=%d Authority=%d LocalRole=%d Controller=%s Pawn=%s"),
		Transition ? *Transition->RequestId.ToString(EGuidFormats::DigitsWithHyphens) : TEXT("None"), bLocked,
		Transition && Transition->bCommitted, Transition && Transition->bFailed, Transition ? Transition->TargetDayIndex : INDEX_NONE,
		*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), HasAuthority(), static_cast<int32>(GetLocalRole()),
		*GetNameSafe(this), *GetNameSafe(GetPawn()));
}

// 清理流程：先从原 GameState 移除确切订阅，再归还本功能锁和本地表现；远端服务器 Controller 不接触 LocalPlayer UI。
void ACatfishingPlayerController::ClearDayTransition()
{
	if (ACatfishingGameState* State = DayTransitionGameState.Get())
	{
		State->OnRunPublicStateChanged.Remove(DayTransitionStateHandle);
	}
	DayTransitionStateHandle.Reset();
	SetDayTransitionLocked(false);
	DayTransitionGameState.Reset();
	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (UCatLocalPlayerUISubsystem* UI = LocalPlayer->GetSubsystem<UCatLocalPlayerUISubsystem>())
		{
			UI->ClearDayTransition();
			UI->ClearAltarConfirmation();
		}
	}
}

// 旅行流程：先记住将离开的 World，清理翻天订阅和锁，再交给父类广播旅行；后续旧世界帧不能重新创建遮罩。
void ACatfishingPlayerController::PreClientTravel(const FString& PendingURL, const ETravelType TravelType, const bool bIsSeamlessTravel)
{
	DayTransitionTravelWorld = GetWorld();
	ClearSelectedItemUseInput(true);
	ClearDayTransition();
	Super::PreClientTravel(PendingURL, TravelType, bIsSeamlessTravel);
}

void ACatfishingPlayerController::OnPossess(APawn* InPawn)
{
	// 接管流程：父类先完成 Pawn 所有权切换，并经 SetPawn 统一刷新 Ability 输入路由；随后只清 Controller 自己的本地钓鱼命令和疾跑状态。
	Super::OnPossess(InPawn);
	if (FishingCommandComponent)
	{
		FishingCommandComponent->ResetTransientCommandState();
	}
	bSprintRequested = false;
	ApplySprintSpeed(InPawn, false);
	UE_LOG(LogCatfishing, Log, TEXT("Event=controller_possessed Controller=%s Pawn=%s"),
		*GetClass()->GetName(), InPawn ? *InPawn->GetClass()->GetName() : TEXT("None"));
}

// Pawn 复制刷新流程：父类复制收尾会经 SetPawn 切换 Ability 输入路由；这里只重置 Controller 本地输入和普通移动速度。
void ACatfishingPlayerController::OnRep_Pawn()
{
	Super::OnRep_Pawn();
	if (FishingCommandComponent)
	{
		FishingCommandComponent->ResetTransientCommandState();
	}
	bSprintRequested = false;
	ApplySprintSpeed(GetPawn(), false);
}

// Pawn 写入流程：换身体前先撤销旧身体的物理持续输入并归还翻天锁；父类写入后刷新 Ability 路由和本地 UI，再按当前服务器快照锁定新身体。
void ACatfishingPlayerController::SetPawn(APawn* InPawn)
{
	if (GetPawn() != InPawn)
	{
		ClearSelectedItemUseInput(true);
		ClearPhysicalControlInput(TEXT("PawnChanged"));
		// 物品栏焦点属于本地玩家当前身体；在新 UI 绑定前重置，背包自身不保存选择。
		SelectedQuickbarSlotIndex = 0;
		AuthorityQuickbarSlotIndex = 0;
		PendingQuickbarSelectionRequestId.Invalidate();
		QuickbarSelectionResults.Reset();
		SetDayTransitionLocked(false);
	}
	Super::SetPawn(InPawn);
	if (AbilityInputBindingComponent)
	{
		AbilityInputBindingComponent->RefreshForPawn(InPawn);
	}
	NotifyLocalPlayerUISubsystemPawnChanged();
	ReconcileDayTransition();
}

// 本地启动流程：父类完成 Actor 生命周期后，幂等安装本 Controller 的玩法输入层；最后消费翻天快照，缺失时由 Tick 补齐。
void ACatfishingPlayerController::BeginPlay()
{
	Super::BeginPlay();
	ApplyInputMappingContext();
	ReconcileDayTransition();
}

// 上鱼后的身体、移动与可见镜头跟随实际杆姿态；右键重设之后，转杆只消费新鼠标增量。
void ACatfishingPlayerController::UpdateRotation(const float DeltaTime)
{
	const FRotator LookDeltaDegrees = RotationInput;
	Super::UpdateRotation(DeltaTime);
	if (FishingCommandComponent)
	{
		// PostProcessInput 中的右键边沿先采基准，本帧尚未处理的鼠标量在这里且只累计一次。
		FishingCommandComponent->UpdateLocalRodAimInput(DeltaTime, LookDeltaDegrees);
	}
	RefreshPhysicalViewIntent();
}

void ACatfishingPlayerController::RefreshPhysicalViewIntent()
{
	if (const ACatCharacter* Cat = Cast<ACatCharacter>(GetPawn()))
	{
		if (UCatPhysicalBodyComponent* Body = Cat->GetPhysicalBodyComponent())
		{
			// 仍以实际杆姿态作为持竿镜头和移动基准；身体旋转只由物理电机写入。
			Body->SetViewIntent(UCatFishingCameraComponent::ResolveFacingRotation(this));
		}
	}
}

// 输入绑定流程：只接受项目配置的 EnhancedInputComponent；物理移动、视角、跳跃和疾跑绑定随当前 InputComponent 生命周期销毁，不由 Controller 手动解绑。
// 未接入的 Action 独立跳过，不阻塞其余输入；完成玩法输入绑定后通知 LocalPlayer UI 重新检查确认键，覆盖客户端 InputComponent 晚于 UI 子系统就绪的时序。
void ACatfishingPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	ApplyInputMappingContext();

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EnhancedInput)
	{
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=controller_input_binding_failed Controller=%s Error=EnhancedInputComponentUnavailable"),
			*GetClass()->GetName());
		return;
	}

	if (MoveAction)
	{
		EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ThisClass::Move);
		EnhancedInput->BindAction(MoveAction, ETriggerEvent::Completed, this, &ThisClass::StopMove);
		EnhancedInput->BindAction(MoveAction, ETriggerEvent::Canceled, this, &ThisClass::StopMove);
	}
	if (LookAction)
	{
		EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &ThisClass::Look);
	}
	if (JumpAction)
	{
		EnhancedInput->BindAction(JumpAction, ETriggerEvent::Started, this, &ThisClass::StartJump);
		EnhancedInput->BindAction(JumpAction, ETriggerEvent::Completed, this, &ThisClass::StopJump);
		EnhancedInput->BindAction(JumpAction, ETriggerEvent::Canceled, this, &ThisClass::StopJump);
	}
	if (SprintAction)
	{
		EnhancedInput->BindAction(SprintAction, ETriggerEvent::Started, this, &ThisClass::StartSprint);
		EnhancedInput->BindAction(SprintAction, ETriggerEvent::Completed, this, &ThisClass::StopSprint);
		EnhancedInput->BindAction(SprintAction, ETriggerEvent::Canceled, this, &ThisClass::StopSprint);
	}
	if (AltarConfirmationInputBoundComponent.Get() != InputComponent)
	{
		InputComponent->BindKey(EKeys::F8, IE_Pressed, this, &ThisClass::ConfirmAltarConfirmationFromInput);
		InputComponent->BindKey(EKeys::F9, IE_Pressed, this, &ThisClass::RevokeAltarConfirmationFromInput);
		AltarConfirmationInputBoundComponent = InputComponent;
	}

	const UCatAbilitySettings* AbilitySettings = GetDefault<UCatAbilitySettings>();
	const UCatAbilityInputConfig* AbilityInputConfig = AbilitySettings && AbilitySettings->IsFishingRuntimeReady()
		? AbilitySettings->AbilityInputConfig.LoadSynchronous() : nullptr;
	// Native 标签只在 InputComponent 对象变化时重新绑定；Ability 输入组件自己维护 ASC 路由，避免 SetupInputComponent 重入累积重复回调。
	if (AbilityInputConfig && NativeInputBoundComponent.Get() != EnhancedInput)
	{
		for (const FCatNativeInputAction& Entry : AbilityInputConfig->NativeInputActions)
		{
			EnhancedInput->BindAction(Entry.InputAction, ETriggerEvent::Started,
				this, &ThisClass::NativeInputTagPressed, Entry.InputTag);
			EnhancedInput->BindAction(Entry.InputAction, ETriggerEvent::Completed,
				this, &ThisClass::NativeInputTagReleased, Entry.InputTag);
			EnhancedInput->BindAction(Entry.InputAction, ETriggerEvent::Canceled,
				this, &ThisClass::NativeInputTagCanceled, Entry.InputTag);
		}
		NativeInputBoundComponent = EnhancedInput;
	}
	if (AbilityInputBindingComponent)
	{
		AbilityInputBindingComponent->BindAbilityActions(*EnhancedInput, AbilityInputConfig);
	}
	NotifyLocalPlayerUISubsystemPawnChanged();
}

// UI 通知流程：
// 1. 只允许 owning client 执行，服务器上的远端 Controller 不创建或刷新任何 LocalPlayer UI。
// 2. 从当前 Controller 持有的 LocalPlayer 取得 UI 子系统；没有 LocalPlayer 说明还处于服务器或非玩家上下文，直接跳过。
// 3. 子系统按当前 Controller/Pawn 重新对齐 HUD、库存和交互提示，并在输入链晚到时重装 UI Action。
void ACatfishingPlayerController::NotifyLocalPlayerUISubsystemPawnChanged()
{
	if (!IsLocalController())
	{
		return;
	}
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UCatLocalPlayerUISubsystem* UISubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<UCatLocalPlayerUISubsystem>() : nullptr;
	if (UISubsystem)
	{
		UISubsystem->RefreshPlayerLakeUIForController(this);
	}
}

// 输入后处理流程：先保留父类每帧输入收尾，再把本帧 Delta/GamePaused 交给当前 Pawn 的 ASC；没有有效 ASC 时保持静默，不缓存上一 Pawn。
void ACatfishingPlayerController::PostProcessInput(const float DeltaTime, const bool bGamePaused)
{
	Super::PostProcessInput(DeltaTime, bGamePaused);
	if (AbilityInputBindingComponent)
	{
		AbilityInputBindingComponent->ProcessAbilityInput(DeltaTime, bGamePaused);
	}
}

// Pawn 断开流程：先经统一入口撤销物理持续输入，再重置 ASC 路由和钓鱼临时状态、恢复普通疾跑速度并清本地疾跑意图；最后交还父类断开占有。
void ACatfishingPlayerController::OnUnPossess()
{
	ClearPhysicalControlInput(TEXT("UnPossessed"));
	if (AbilityInputBindingComponent)
	{
		AbilityInputBindingComponent->ResetAbilityInput();
	}
	if (FishingCommandComponent)
	{
		FishingCommandComponent->ResetTransientCommandState();
	}
	ApplySprintSpeed(GetPawn(), false);
	bSprintRequested = false;
	Super::OnUnPossess();
}

// 输入清理流程：先解绑翻天并归还专属锁，再撤销物理持续输入、Ability 路由和钓鱼临时状态，撤销自己的 Mapping Context，最后交还父类销毁。
void ACatfishingPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 父类结束流程可能再次写 Pawn；禁止该回调重新订阅即将销毁的 World。
	DayTransitionTravelWorld = GetWorld();
	ClearDayTransition();
	ClearPhysicalControlInput(TEXT("ControllerEndPlay"));
	if (AbilityInputBindingComponent)
	{
		AbilityInputBindingComponent->ResetAbilityInput();
	}
	NativeInputBoundComponent.Reset();
	AltarConfirmationInputBoundComponent.Reset();
	if (FishingCommandComponent)
	{
		FishingCommandComponent->ResetTransientCommandState();
	}
	ApplySprintSpeed(GetPawn(), false);
	bSprintRequested = false;
	RemoveInputMappingContext();
	Super::EndPlay(EndPlayReason);
}

// Mapping Context 安装流程：仅本地 Controller 从自身 LocalPlayer 取 Enhanced Input 子系统，重复调用保持幂等。
void ACatfishingPlayerController::ApplyInputMappingContext()
{
	if (!IsLocalController() || !DefaultMappingContext || AppliedMappingContext)
	{
		return;
	}

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!InputSubsystem)
	{
		return;
	}

	InputSubsystem->AddMappingContext(DefaultMappingContext, InputMappingPriority);
	AppliedInputSubsystem = InputSubsystem;
	AppliedMappingContext = DefaultMappingContext;
}

// Mapping Context 移除流程：使用安装时保存的同一子系统和资产成对清理，World teardown 下弱引用失效也安全。
void ACatfishingPlayerController::RemoveInputMappingContext()
{
	if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = AppliedInputSubsystem.Get();
		InputSubsystem && AppliedMappingContext)
	{
		InputSubsystem->RemoveMappingContext(AppliedMappingContext);
	}
	AppliedInputSubsystem.Reset();
	AppliedMappingContext = nullptr;
}

// 移动输入流程：翻天锁或引擎移动忽略时先清物理移动意图；其余以可见水平朝向转换前后左右输入，Pawn 缺失时不制造旁路状态。
void ACatfishingPlayerController::Move(const FInputActionValue& Value)
{
	if (IsDayTransitionInputBlocked() || UCatGE_FishingScoopCooldown::IsOperationBlocked(GetPawn()) || IsMoveInputIgnored())
	{
		StopMove();
		return;
	}
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return;
	}

	const FVector2D Movement = Value.Get<FVector2D>();
	const FRotator FacingRotation = UCatFishingCameraComponent::ResolveFacingRotation(this);
	const FRotator YawRotation(0.0, FacingRotation.Yaw, 0.0);
	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
	if (const ACatCharacter* Cat = Cast<ACatCharacter>(ControlledPawn))
	{
		if (UCatPhysicalBodyComponent* Body = Cat->GetPhysicalBodyComponent())
		{
			Body->SetMoveIntent((ForwardDirection * Movement.Y + RightDirection * Movement.X).GetClampedToMaxSize(1.0));
			return;
		}
	}
	ControlledPawn->AddMovementInput(ForwardDirection, Movement.Y);
	ControlledPawn->AddMovementInput(RightDirection, Movement.X);
}

// 移动停止流程：Completed、Canceled 或输入锁都把本地物理身体的自愿移动意图清零；已有速度、外力和抓握牵引保持由各自系统结算。
void ACatfishingPlayerController::StopMove()
{
	if (const ACatCharacter* Cat = Cast<ACatCharacter>(GetPawn()))
	{
		if (UCatPhysicalBodyComponent* Body = Cat->GetPhysicalBodyComponent()) Body->SetMoveIntent(FVector::ZeroVector);
	}
}

// 物理输入清理流程：先清钓鱼保持态与 Ability 路由，再清身体移动/抓握意图，最后关闭疾跑；各子系统仍保留自己的权限与复制收口。
void ACatfishingPlayerController::ClearPhysicalControlInput(const FName Reason)
{
	ClearSelectedItemUseInput(true);
	if (FishingCommandComponent && GetPawn()) FishingCommandComponent->ClearHeldInputForLifecycle(Reason);
	if (AbilityInputBindingComponent) AbilityInputBindingComponent->ReleaseAllInputRoutes(Reason);
	if (const ACatCharacter* Cat = Cast<ACatCharacter>(GetPawn()))
	{
		if (UCatPhysicalBodyComponent* Body = Cat->GetPhysicalBodyComponent()) Body->ClearControlIntent(Reason);
	}
	SetSprintRequested(false, true);
}

// 按键刷新流程：视口失焦或输入层切换时先撤销持续物理输入，再让父类丢弃引擎记录的按键状态，避免恢复焦点后重放旧意图。
void ACatfishingPlayerController::FlushPressedKeys()
{
	ClearSelectedItemUseInput(true);
	ClearPhysicalControlInput(TEXT("KeysFlushed"));
	Super::FlushPressedKeys();
}

// 视角输入流程：翻天锁或引擎视角忽略时不累积 RotationInput；其他时候把 Mapping Context 已处理过的二维意图写入视角。
void ACatfishingPlayerController::Look(const FInputActionValue& Value)
{
	if (IsDayTransitionInputBlocked() || UCatGE_FishingScoopCooldown::IsOperationBlocked(GetPawn()) || IsLookInputIgnored()) return;
	const FVector2D LookAxis = Value.Get<FVector2D>();
	AddYawInput(LookAxis.X);
	AddPitchInput(LookAxis.Y);
}

// 跳跃按下流程：翻天锁或引擎移动忽略时先停止已有跳跃保持态；其他时候只对当前 Character 生效，持竿时清保持态并拒绝起跳，普通 Pawn 不伪造实现。
void ACatfishingPlayerController::StartJump()
{
	if (IsDayTransitionInputBlocked() || UCatGE_FishingScoopCooldown::IsOperationBlocked(GetPawn()) || IsMoveInputIgnored())
	{
		StopJump();
		return;
	}
	if (ACharacter* ControlledCharacter = Cast<ACharacter>(GetPawn()))
	{
		if (const ACatFishingRodActor* Rod = UCatFishingCameraComponent::FindHeldRodOperatedBy(this);
			Rod && Rod->IsPrimaryOperator(PlayerState))
		{
			ControlledCharacter->StopJumping();
			return;
		}
		if (const ACatCharacter* Cat = Cast<ACatCharacter>(ControlledCharacter))
		{
			if (UCatPhysicalBodyComponent* Body = Cat->GetPhysicalBodyComponent())
			{
				Body->RequestJump();
				return;
			}
		}
		ControlledCharacter->Jump();
	}
}

// 跳跃释放流程：Completed 与 Canceled 共用同一收口，支持 Character 的可变跳跃时长。
void ACatfishingPlayerController::StopJump()
{
	if (ACharacter* ControlledCharacter = Cast<ACharacter>(GetPawn()))
	{
		ControlledCharacter->StopJumping();
	}
}

// 疾跑按下流程：先拒绝翻天操作，再本地应用并向 authority 发送布尔意图，客户端不能提交任意速度。
void ACatfishingPlayerController::StartSprint()
{
	if (IsDayTransitionInputBlocked()) return;
	SetSprintRequested(true, true);
}

// 疾跑释放流程：Completed/Canceled 幂等共用，窗口失焦或 Mapping Context 取消时也恢复普通速度。
void ACatfishingPlayerController::StopSprint()
{
	SetSprintRequested(false, true);
}

// 疾跑意图更新流程：重复事件仍会修正当前 Pawn 的速度，但只在状态实际变化时发送一次可靠 RPC。
void ACatfishingPlayerController::SetSprintRequested(const bool bNewSprintRequested, const bool bNotifyServer)
{
	const bool bStateChanged = bSprintRequested != bNewSprintRequested;
	bSprintRequested = bNewSprintRequested;
	ApplySprintSpeed(GetPawn(), bSprintRequested);

	if (bStateChanged && bNotifyServer && !HasAuthority() && !Cast<ACatCharacter>(GetPawn()))
	{
		ServerSetSprinting(bSprintRequested);
	}
}

// 移动速度应用流程：物理电机使用服务器配置的 cm/s；CMC 保留同值供正式动画/通用只读消费者。
float ACatfishingPlayerController::GetConfiguredMovementSpeed(const APawn* TargetPawn, const bool bSprinting) const
{
	const ACharacter* DefaultCharacter = TargetPawn ? Cast<ACharacter>(TargetPawn->GetClass()->GetDefaultObject()) : nullptr;
	const UCharacterMovementComponent* DefaultMovement = DefaultCharacter ? DefaultCharacter->GetCharacterMovement() : nullptr;
	// 普通速度只读当前猫类 CDO 的正式 CMC 配置，不能用已被疾跑临时覆盖的实例值当基准。
	const float WalkSpeed = DefaultMovement ? DefaultMovement->MaxWalkSpeed : 100.0f;
	return FMath::Max(0.0f, bSprinting ? SprintMaxSpeed : WalkSpeed);
}

void ACatfishingPlayerController::ApplySprintSpeed(APawn* TargetPawn, const bool bSprinting) const
{
	const float Speed = GetConfiguredMovementSpeed(TargetPawn, bSprinting);
	if (const ACatCharacter* Cat = Cast<ACatCharacter>(TargetPawn))
	{
		if (UCatPhysicalBodyComponent* Body = Cat->GetPhysicalBodyComponent()) Body->SetMovementSpeed(Speed);
	}
	ACharacter* ControlledCharacter = Cast<ACharacter>(TargetPawn);
	UCharacterMovementComponent* MovementComponent = ControlledCharacter
		? ControlledCharacter->GetCharacterMovement() : nullptr;
	if (!MovementComponent)
	{
		return;
	}

	MovementComponent->MaxWalkSpeed = Speed;
	if (auto* Movement = Cast<UCatCharacterMovementComponent>(MovementComponent)) Movement->SetSprintIntent(bSprinting);
}

// authority 疾跑流程：翻天期间拒绝迟到的开启意图但接受释放；最终速度继续读取服务器类默认值。
void ACatfishingPlayerController::ServerSetSprinting_Implementation(const bool bNewSprinting)
{
	if (bNewSprinting && IsDayTransitionInputBlocked()) return;
	SetSprintRequested(bNewSprinting, false);
}

// Controller 玩法 gate 流程：现取当前 World 的 authority GameMode 并委托唯一判断；不缓存 GameMode 或身份，旅行、Logout 与 teardown 后会立即 fail-closed。
bool ACatfishingPlayerController::CanForwardGameplayCommand() const
{
	const ACatfishingGameModeBase* GameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	return !UCatGE_FishingScoopCooldown::IsOperationBlocked(GetPawn()) && GameMode && GameMode->CanAcceptGameplayCommand(this);
}

// Controller 钓鱼 gate 流程：现取 authority GameMode 并使用 Fishing 专用白天规则；它只服务抛竿、鱼竿操作、协作、抢抄和玩家打窝，不影响 Social 或结算 RPC。
bool ACatfishingPlayerController::CanForwardFishingCommand() const
{
	const ACatfishingGameModeBase* GameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	return !UCatGE_FishingScoopCooldown::IsOperationBlocked(GetPawn()) && GameMode && GameMode->CanAcceptFishingCommand(this);
}

// 结算完成 RPC 流程：现取 authority GameMode/Imprint 服务并检查当前 Run 的计划终态与 Grant ACK；通过后才调用 Run 唯一写入口，不让客户端布尔值直接结束结算夜。
void ACatfishingPlayerController::ServerRequestSettlementCompletion_Implementation(const FGuid RequestId,
	const int64 ExpectedRevision)
{
	ACatfishingGameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	UCatRunImprintService* Imprint = GetWorld() ? GetWorld()->GetSubsystem<UCatRunImprintService>() : nullptr;
	FCatRunCommandResult Result;
	Result.RequestId = RequestId;
	if (GameMode && Imprint && Imprint->IsSettlementArchiveReady(GameMode->GetRunPublicState().Phase.RunId))
	{
		Result = GameMode->CompleteSettlementFromServerRequest(RequestId, ExpectedRevision);
	}
	else
	{
		Result.Error = ECatRunCommandError::TeardownFailed;
	}
	UE_LOG(LogCatRun, Log, TEXT("Event=settlement_completion_result RequestId=%s Committed=%s Error=%s Revision=%lld"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens), Result.bCommitted ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(Result.Error), Result.Revision);
}

// Profile Grant 客户端流程：从当前 LocalPlayer 现取唯一 Profile 子系统并执行两阶段 durable 应用；只有返回 AckAllowed 才调用服务器 ACK，保存失败保持待重投。
void ACatfishingPlayerController::ClientReceiveProfileGrant_Implementation(const FCatProfileGrant& Grant)
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UCatProfileSubsystem* Profile = LocalPlayer ? LocalPlayer->GetSubsystem<UCatProfileSubsystem>() : nullptr;
	const FCatProfileApplyResult Result = Profile ? Profile->ApplyGrant(Grant) : FCatProfileApplyResult();
	if (Result.bAckAllowed)
	{
		ServerAcknowledgeProfileGrant(Grant.GrantId);
		if (Grant.Kind == ECatProfileGrantKind::FishRecorded || Grant.Kind == ECatProfileGrantKind::FishSilhouette
			|| Grant.Kind == ECatProfileGrantKind::FishKnowledge)
		{
			// 服务端「这条鱼是不是首钓」只能读 PlayerState 上这份公开摘要，所以三类图鉴 Grant 落盘后都要回传，
			// 否则首钓判定会在本局内一直看到入局那一刻的旧事实（图鉴 §3.1.8:149 首钓新鱼种才抛印记）。
			TArray<FCatFishCollectionRecord> Records;
			if (Profile->GetFishCollectionSnapshot(Records))
			{
				ServerPublishPublicFishCollection(Records);
			}
		}
	}
}

// Profile ACK 服务器流程：先让 RunImprintService 以当前 Controller 核对并推进独立 DeliveryRecord；真实 ACK 成功或已重放后再通知 GameMode 复核 Host exit 统一等待。
void ACatfishingPlayerController::ServerAcknowledgeProfileGrant_Implementation(const FGuid GrantId)
{
	if (UCatRunImprintService* Service = GetWorld() ? GetWorld()->GetSubsystem<UCatRunImprintService>() : nullptr)
	{
		const FCatDomainCommandResult AckResult = Service->AcknowledgeGrant(this, GrantId);
		if (AckResult.bCommitted || AckResult.Error == ECatDomainCommandError::AlreadyResolved)
		{
			FCatProfileGrant AcknowledgedGrant;
			if (Service->TryGetAcknowledgedGrant(GrantId, AcknowledgedGrant)
				&& AcknowledgedGrant.Kind == ECatProfileGrantKind::Unlock)
			{
			}
			if (ACatfishingGameModeBase* GameMode = GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>())
			{
				GameMode->NotifyHostExitGrantAckProgress();
			}
		}
	}
}

// CapturePlan 客户端流程：把计划交给本 LocalPlayer Profile 的外部成像桥；桥或本地依赖拒绝时立即回报失败，使服务器把该计划收口为终态而不是永久重投阻塞结算。
void ACatfishingPlayerController::ClientReceiveImprintCapturePlan_Implementation(const FCatCapturePlan& Plan)
{
	bool bAcceptedByBridge = false;
	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (UCatProfileSubsystem* Profile = LocalPlayer->GetSubsystem<UCatProfileSubsystem>())
		{
			bAcceptedByBridge = Profile->ReceiveCapturePlan(Plan);
		}
	}
	if (!bAcceptedByBridge && Plan.CapturePlanId.IsValid())
	{
		ServerReportImprintCaptureResult(Plan.CapturePlanId, false, FGuid());
	}
}

// 成像结果服务器流程：只转交计划 ID、结果布尔和真实 ImprintId；服务端通过当前 PlayerState 校验接收者，客户端不能指定 Grant 内容。
void ACatfishingPlayerController::ServerReportImprintCaptureResult_Implementation(const FGuid CapturePlanId,
	const bool bSucceeded, const FGuid ImprintId)
{
	if (UCatRunImprintService* Service = GetWorld() ? GetWorld()->GetSubsystem<UCatRunImprintService>() : nullptr)
	{
		Service->ReportCaptureResult(this, CapturePlanId, bSucceeded, ImprintId);
	}
}

// 篝火回看 RPC 路由流程：只把营地回看意图投给 BodyAction Ability；没有正式 Ability 接管时回送依赖错误。
void ACatfishingPlayerController::ServerRequestCampfirePlayback_Implementation(ACatCampHubActor* Camp,
	const FGuid RequestId)
{
	if (!CampBodyActionCommandComponent || !CampBodyActionCommandComponent->SubmitCampfirePlayback(Camp, RequestId))
	{
		FCatDomainCommandResult Result;
		Result.RequestId = RequestId;
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		DeliverCampCommandResultToOwningClient(Result);
	}
}

// 公共领域结果客户端流程：
// 1. 可靠接收结果后按请求落盘并整体替换本机读模型，再广播给 UI；未开界面也保留接收证据，不触发新的领域命令。
// 2. 若连续左键 的 Begin 被服务器拒绝，本机立刻清掉同 RequestId 的等待记录；否则未松键时不能再次发起 Use。
// 3. 已接受或无关请求绝不触碰当前持续记录，迟到旧回执不能取消后来开始的同类物品。
void ACatfishingPlayerController::ClientReceiveCampCommandResult_Implementation(
	const FCatDomainCommandResult& Result)
{
	const FString Event = FString::Printf(
		TEXT("Event=inventory_command_received RequestId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Player=%s Committed=%d Replay=%d Error=%s FailureReason=%s"),
		*Result.RequestId.ToString(), *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole(), *GetName(),
		Result.bCommitted, Result.bTerminalReplay, *UEnum::GetValueAsString(Result.Error), *Result.FailureReason.ToString());
	if (CatIsAcceptedDomainCommandResult(Result)) { UE_LOG(LogCatfishing, Log, TEXT("%s"), *Event); }
	else { UE_LOG(LogCatfishing, Warning, TEXT("%s"), *Event); }
	if (ActiveSelectedItemUseRequestId == Result.RequestId && !CatIsAcceptedDomainCommandResult(Result))
	{
		if (ActiveSelectedItemUseInstance)
		{
			ActiveSelectedItemUseInstance->SetUseInputActiveLocally(this, false);
		}
		ActiveSelectedItemUseRequestId.Invalidate();
		ActiveSelectedItemUseItemId.Invalidate();
		ActiveSelectedItemUseSlotIndex = INDEX_NONE;
		ActiveSelectedItemUseBackPack = nullptr;
		ActiveSelectedItemUseInstance = nullptr;
	}
	LastCampCommandResult = Result;
	OnCampCommandResultReceived.Broadcast(Result);
}

// 公共领域结果读取流程：返回 owning client 最近收到的完整结果副本，供 UI 按 RequestId 关联反馈；服务器权限和领域真相不读取该缓存。
FCatDomainCommandResult ACatfishingPlayerController::GetLastCampCommandResult() const
{
	return LastCampCommandResult;
}

// BodyAction gate 读取流程：保留的 Camp/Social Ability 只需要知道服务器当前是否还接受玩法命令；Controller 继续隐藏 GameMode、Run 阶段和 teardown 细节。
bool ACatfishingPlayerController::CanSubmitBodyActionCommand() const
{
	return CanForwardGameplayCommand();
}

// BodyAction 回执投递流程：保留的 Camp/Social Ability 已经拿到领域服务终态；Controller 只复用公共领域结果的 owning-client 网络适配。
void ACatfishingPlayerController::DeliverBodyActionCommandResultToOwningClient(
	const FCatDomainCommandResult& Result)
{
	DeliverCampCommandResultToOwningClient(Result);
}

// 通用库存移动 RPC 路由流程：Controller 先执行玩法命令 gate 与本人身体复核，再把宿主和槽位交给 Actor 级库存入口；
// 正式移动由 Source InventoryComponent 裁决。这条路径同时就是「拿鱼」机制本身——把别人地面鱼护里的鱼拿走走的就是它，
// 机制不问动机也不问归属（2026-09-11 裁决②）。三条客观规则各有执行位置，这里不复制第二份：
//   够得着     ＝ CatInventoryAccessRules::IsHostReachable 的距离复核（经 CatInventoryStatics 的端点解析）；
//   鱼护在地面 ＝ 同一处对 ACatFishGuardActor::IsGrounded 的拒绝，鱼护一旦被叼走或收进包就读写不了里面的鱼；
//   一嘴一条   ＝ ACatCharacter::TryClaimMouthCarriedActorFromAuthority 的单占用，叼第二条时认领失败。
// 本入口自己只补两道它原先缺、而同文件其它库存 RPC 都有的复核：角色必须属于当前 World，且倒地的猫不能拿。
void ACatfishingPlayerController::ServerMoveInventoryItemBetweenHosts_Implementation(
	const FGuid RequestId, AActor* SourceInventoryHost, const int32 SourceSlotIndex,
	AActor* TargetInventoryHost, const int32 TargetSlotIndex)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	const UCatConditionComponent* Conditions = ControlledCharacter ? ControlledCharacter->GetConditionComponent() : nullptr;
	if (!CanForwardGameplayCommand())
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=move_inventory_item_between_hosts_rejected Reason=CommandsClosedOrInactive Request=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	}
	else if (!ControlledCharacter || ControlledCharacter->GetWorld() != GetWorld())
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=move_inventory_item_between_hosts_rejected Reason=MissingCharacter Request=%s Character=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(ControlledCharacter));
	}
	else if (!Conditions)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=move_inventory_item_between_hosts_rejected Reason=MissingConditions Request=%s Character=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(ControlledCharacter));
	}
	// 倒地只挡「涉及他人容器」的转移，不挡整理自己的背包。
	// 裁决②（2026-09-11）管的是拿鱼——从别人的鱼护或共用大缸取鱼，客观规则是够得着、
	// 鱼护在地面、一嘴一条；整理自己背包不在这条规则的射程内。倒地本身的设计口径只有
	// 「来源唯一＝吃重毒鱼、队友搬运或休息自愈、献祭到场判定豁免」（猫咪与状态.md），
	// 从没写过倒地等于冻结；代码里那张「倒地＝全面交互禁用」清单是工程自造、尚未入册
	// （见 Docs/gap-analysis/2026-09-11/回填清单.md §五），不该顺着扩宽。
	else if (Conditions->GetSnapshot().bDowned
		&& (SourceInventoryHost != ControlledCharacter || TargetInventoryHost != ControlledCharacter))
	{
		Result.Error = ECatDomainCommandError::PermissionDenied;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=move_inventory_item_between_hosts_rejected Reason=DownedForeignHost Request=%s Character=%s Source=%s Target=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(ControlledCharacter),
			*GetNameSafe(SourceInventoryHost), *GetNameSafe(TargetInventoryHost));
	}
	else
	{
		Result = UCatInventoryStatics::MoveItemBetweenInventoryHostsFromAuthority(ControlledCharacter, RequestId,
			SourceInventoryHost, SourceSlotIndex, TargetInventoryHost, TargetSlotIndex);
	}
	DeliverCampCommandResultToOwningClient(Result);
}

// 装备配置 RPC 执行流程：
// 1. 先在服务器侧读取当前 Pawn 和 EquipmentComponent，确保后续裁决只基于正式角色状态。
// 2. 再依次处理玩法命令 gate、RequestId 和依赖缺失分支，失败时只写结构化错误和诊断日志。
// 3. 校验通过后把装备定义、实例和 Revision 原样提交给 EquipmentComponent 裁决。
// 4. 最后统一记录结果并回送 owning client，让 UI 只消费服务器确认后的终态。
void ACatfishingPlayerController::ServerConfigureEquipment_Implementation(const FGuid RequestId,
	const int64 ExpectedRevision, const FName RodDefinitionId, const FName BaitDefinitionId,
	const FName FloatDefinitionId, const FName ScoopNetDefinitionId, const FGuid RodItemInstanceId,
	const FGuid BaitItemInstanceId, const FGuid FloatItemInstanceId, const FGuid ScoopNetItemInstanceId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	UCatEquipmentComponent* Equipment = ControlledCharacter ? ControlledCharacter->GetEquipmentComponent() : nullptr;
	if (!CanForwardGameplayCommand())
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=configure_equipment_rejected Reason=CommandsClosedOrInactive Request=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	}
	else if (!RequestId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=configure_equipment_rejected Reason=InvalidRequest Request=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	}
	else if (!ControlledCharacter || ControlledCharacter->GetWorld() != GetWorld() || !Equipment)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=configure_equipment_rejected Reason=NoEquipmentComponent Request=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	}
	else
	{
		Result = Equipment->ConfigureLoadoutFromAuthority(RequestId, ExpectedRevision,
			RodDefinitionId, BaitDefinitionId, FloatDefinitionId, ScoopNetDefinitionId, NAME_None,
			RodItemInstanceId, BaitItemInstanceId, FloatItemInstanceId, ScoopNetItemInstanceId);
	}
	UE_LOG(LogCatfishing, Log,
		TEXT("Event=configure_equipment Committed=%s Error=%s Revision=%lld Rod=%s RodItem=%s Bait=%s BaitItem=%s Float=%s FloatItem=%s Net=%s NetItem=%s"),
		Result.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Error),
		Result.Revision, *RodDefinitionId.ToString(), *RodItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*BaitDefinitionId.ToString(), *BaitItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*FloatDefinitionId.ToString(), *FloatItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*ScoopNetDefinitionId.ToString(), *ScoopNetItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens));
	DeliverCampCommandResultToOwningClient(Result);
}

// 背包读取流程：只从当前 Character 的构造期个人库存取得 BackPack；鱼护、鱼缸、商店和营地容器虽然也实现库存接口，却不能成为快捷栏来源。
UCatBackPackComponent* ACatfishingPlayerController::GetControlledBackPack() const
{
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	return ControlledCharacter ? Cast<UCatBackPackComponent>(ControlledCharacter->GetInventoryComponent()) : nullptr;
}

// 选格携带观察到的实例身份；服务器负责收回旧竿、装备新竿，并回执最终焦点。
bool ACatfishingPlayerController::RequestSelectQuickbarSlotFromInput(const int32 RequestedSlotIndex)
{
	if (!IsLocalController() || IsDayTransitionInputBlocked() || IsMoveInputIgnored() || IsQuickbarSelectionLocked()) return false;
	UCatBackPackComponent* BackPack = GetControlledBackPack();
	if (!BackPack || !BackPack->IsValidInventorySlotIndex(RequestedSlotIndex)) return false;
	const auto* Entry = BackPack->GetInventoryEntryAtSlot(RequestedSlotIndex);
	const auto& Held = BackPack->GetQuickbarHeldSlot();
	const FGuid ExpectedItemId = Held.SlotIndex == RequestedSlotIndex && Held.ItemInstanceId.IsValid()
		? Held.ItemInstanceId : Entry && Entry->Instance ? Entry->Instance->GetItemInstanceId() : FGuid();
	if (FishingCommandComponent) FishingCommandComponent->ClearHeldInputForLifecycle(TEXT("QuickbarSelection"));
	SelectedQuickbarSlotIndex = RequestedSlotIndex;
	OnQuickbarSelectionChanged.Broadcast(SelectedQuickbarSlotIndex);
	PendingQuickbarSelectionRequestId = FGuid::NewGuid();
	ServerSelectQuickbarSlot(PendingQuickbarSelectionRequestId, RequestedSlotIndex, ExpectedItemId);
	return true;
}

bool ACatfishingPlayerController::IsQuickbarSelectionLocked() const
{
	if (!GetWorld() || ActiveSelectedItemUseRequestId.IsValid()) return true;
	if (const auto* BackPack = GetControlledBackPack(); !HasAuthority() && BackPack && BackPack->GetQuickbarHeldSlot().bInUse) return true;
	for (TActorIterator<ACatFishingSession> It(GetWorld()); It; ++It)
	{
		const auto& State = It->GetSnapshot();
		if (!It->IsTerminal() && State.FisherPlayerState == PlayerState && State.RodActor
			&& State.RodActor->IsPrimaryOperator(PlayerState)) return true;
	}
	return false;
}

void ACatfishingPlayerController::ServerSelectQuickbarSlot_Implementation(const FGuid RequestId,
	const int32 SlotIndex, const FGuid ExpectedItemId)
{
	if (const auto* Cached = QuickbarSelectionResults.Find(RequestId))
	{
		ClientReceiveQuickbarSelection(RequestId, Cached->Key, Cached->Value);
		return;
	}
	UCatBackPackComponent* BackPack = GetControlledBackPack();
	UCatFishingService* Fishing = GetWorld()->GetSubsystem<UCatFishingService>();
	const auto* Entry = BackPack ? BackPack->GetInventoryEntryAtSlot(SlotIndex) : nullptr;
	const auto Held = BackPack ? BackPack->GetQuickbarHeldSlot() : FCatQuickbarHeldSlot{};
	const FGuid ActualId = Held.ItemInstanceId.IsValid() && Held.SlotIndex == SlotIndex
		? Held.ItemInstanceId : Entry && Entry->Instance ? Entry->Instance->GetItemInstanceId() : FGuid();
	bool bCommitted = false;
	const TCHAR* Reason = TEXT("InvalidSelection");
	if (RequestId.IsValid() && BackPack && Fishing && BackPack->IsValidInventorySlotIndex(SlotIndex)
		&& ActualId == ExpectedItemId && CanForwardGameplayCommand() && !IsQuickbarSelectionLocked())
	{
		if (FishingCommandComponent) FishingCommandComponent->ClearHeldInputForLifecycle(TEXT("QuickbarSelection"));
		ACatFishingRodActor* OldRod = Fishing->FindRodOperatedBy(PlayerState);
		const FGuid OldId = OldRod ? OldRod->GetPresentationState().ItemInstanceId : FGuid();
		const bool bSameHeld = OldRod && OldId == ExpectedItemId;
		bool bReleased = true;
		if (OldRod && !bSameHeld)
		{
			FCatLeaveRodCommand Leave;
			Leave.Context.RequestId = FGuid::NewGuid();
			Leave.Context.RodActorId = OldRod->GetPresentationState().RodActorId;
			Leave.Context.ExpectedRodActorRevision = OldRod->GetPresentationState().RodActorRevision;
			bReleased = Fishing->LeaveRod(this, Leave).bCommitted;
			if (bReleased && Held.ItemInstanceId == OldId)
			{
				FCatPackRodCommand Pack;
				Pack.Context.RequestId = FGuid::NewGuid();
				Pack.Context.RodActorId = OldRod->GetPresentationState().RodActorId;
				Pack.Context.ExpectedRodActorRevision = OldRod->GetPresentationState().RodActorRevision;
				bReleased = Fishing->PackRod(this, Pack).bCommitted;
			}
			if (bReleased) BackPack->ClearQuickbarHeldSlotFromAuthority();
		}
		if (bReleased)
		{
			Entry = BackPack->GetInventoryEntryAtSlot(SlotIndex);
			const auto* Definition = Entry && Entry->Instance ? Cast<UCatEquipmentDefinition>(Entry->Instance->GetItemDefinition()) : nullptr;
			if (!bSameHeld && ExpectedItemId.IsValid() && Definition && Definition->CanServeFishingRod())
			{
				FCatInventoryItemUseContext Context;
				Context.RequestId = RequestId; Context.RequestingController = this; Context.UserPawn = GetPawn();
				Context.SourceInventory = BackPack; Context.InventorySlotIndex = SlotIndex;
				bCommitted = Entry->Instance->GetItemInstanceId() == ExpectedItemId
					&& BackPack->ReserveQuickbarHeldSlotFromAuthority(SlotIndex, ExpectedItemId)
					&& BackPack->ExecuteItemActionFromAuthority(Context, ExpectedItemId, CatInventoryActionTags::Use, 1).bCommitted;
				if (!bCommitted) BackPack->ClearQuickbarHeldSlotFromAuthority();
			}
			else bCommitted = bSameHeld || (Entry && Entry->Instance ? Entry->Instance->GetItemInstanceId() : FGuid()) == ExpectedItemId;
		}
		if (!bCommitted && IsValid(OldRod) && OldRod->GetPresentationState().bDeployed && !Fishing->FindRodOperatedBy(PlayerState))
		{
			FCatOperateRodCommand Restore;
			Restore.Context.RequestId = FGuid::NewGuid(); Restore.Context.RodActorId = OldRod->GetPresentationState().RodActorId;
			Restore.Context.ExpectedRodActorRevision = OldRod->GetPresentationState().RodActorRevision;
			const auto Restored = Fishing->OperateRod(this, Restore);
			if (FishingCommandComponent) FishingCommandComponent->DeliverResultFromAuthority(Restored);
		}
		// 新竿拒绝时恢复原手持实例；已入库的旧竿重新使用同一身份。
		if (!bCommitted && OldId.IsValid() && Held.ItemInstanceId == OldId)
		{
			const auto* Previous = BackPack->GetInventoryEntryAtSlot(Held.SlotIndex);
			if (Previous && Previous->Instance && Previous->Instance->GetItemInstanceId() == OldId)
			{
				FCatInventoryItemUseContext Restore;
				Restore.RequestId = FGuid::NewGuid(); Restore.RequestingController = this; Restore.UserPawn = GetPawn();
				Restore.SourceInventory = BackPack; Restore.InventorySlotIndex = Held.SlotIndex;
				const bool bRestored = BackPack->ReserveQuickbarHeldSlotFromAuthority(Held.SlotIndex, OldId)
					&& BackPack->ExecuteItemActionFromAuthority(Restore, OldId, CatInventoryActionTags::Use, 1).bCommitted;
				if (!bRestored) BackPack->ClearQuickbarHeldSlotFromAuthority();
				UE_LOG(LogCatfishing, Warning, TEXT("Event=quickbar_selection_restore RequestId=%s ItemId=%s Restored=%d %s"),
					*RequestId.ToString(), *OldId.ToString(), bRestored, *CatLogContext::BuildControllerFields(this));
			}
		}
		Reason = bCommitted ? TEXT("Selected") : TEXT("RodTransitionRejected");
	}
	else if (IsQuickbarSelectionLocked()) Reason = TEXT("ItemInUse");
	if (bCommitted) AuthorityQuickbarSlotIndex = SlotIndex;
	if (QuickbarSelectionResults.Num() >= 128) QuickbarSelectionResults.Empty();
	QuickbarSelectionResults.Add(RequestId, {AuthorityQuickbarSlotIndex, bCommitted});
	UE_CLOG(!bCommitted, LogCatfishing, Warning, TEXT("Event=quickbar_selection_rejected RequestId=%s Slot=%d Reason=%s %s"),
		*RequestId.ToString(), SlotIndex, Reason, *CatLogContext::BuildControllerFields(this));
	UE_LOG(LogCatfishing, Log, TEXT("Event=quickbar_selection_result RequestId=%s Slot=%d ItemId=%s Committed=%d Reason=%s %s"),
		*RequestId.ToString(), SlotIndex, *ExpectedItemId.ToString(), bCommitted, Reason, *CatLogContext::BuildControllerFields(this));
	ClientReceiveQuickbarSelection(RequestId, AuthorityQuickbarSlotIndex, bCommitted);
}

void ACatfishingPlayerController::ClientReceiveQuickbarSelection_Implementation(const FGuid RequestId,
	const int32 SlotIndex, const bool bCommitted)
{
	if (RequestId != PendingQuickbarSelectionRequestId) return;
	PendingQuickbarSelectionRequestId.Invalidate();
	SelectedQuickbarSlotIndex = SlotIndex;
	OnQuickbarSelectionChanged.Broadcast(SlotIndex);
	UE_LOG(LogCatfishing, Log, TEXT("Event=quickbar_selection_received RequestId=%s Slot=%d Committed=%d %s"),
		*RequestId.ToString(), SlotIndex, bCommitted, *CatLogContext::BuildControllerFields(this));
}

void ACatfishingPlayerController::SelectAcquiredRodSlotFromAuthority(const FGuid RequestId)
{
	const auto* BackPack = GetControlledBackPack();
	if (!HasAuthority() || !BackPack || !BackPack->GetQuickbarHeldSlot().ItemInstanceId.IsValid()) return;
	AuthorityQuickbarSlotIndex = BackPack->GetQuickbarHeldSlot().SlotIndex;
	ClientReceiveAcquiredRodSlot(RequestId, AuthorityQuickbarSlotIndex);
}

void ACatfishingPlayerController::ClientReceiveAcquiredRodSlot_Implementation(const FGuid RequestId, const int32 SlotIndex)
{
	// 不清空用户随后发起的选格请求；同一 Controller 上可靠回执的顺序会让较新的选格继续生效。
	SelectedQuickbarSlotIndex = SlotIndex;
	OnQuickbarSelectionChanged.Broadcast(SlotIndex);
	UE_LOG(LogCatfishing, Log, TEXT("Event=quickbar_rod_acquired_selection_received RequestId=%s Slot=%d %s"),
		*RequestId.ToString(), SlotIndex, *CatLogContext::BuildControllerFields(this));
}

bool ACatfishingPlayerController::IsQuickbarRodSelected() const
{
	const auto* BackPack = GetControlledBackPack();
	if (!BackPack) return false;
	const int32 Slot = GetSelectedQuickbarSlotIndex();
	if (BackPack->GetQuickbarHeldSlot().ItemInstanceId.IsValid() && BackPack->GetQuickbarHeldSlot().SlotIndex == Slot) return true;
	const auto* Entry = BackPack->GetInventoryEntryAtSlot(Slot);
	const auto* Definition = Entry && Entry->Instance ? Cast<UCatEquipmentDefinition>(Entry->Instance->GetItemDefinition()) : nullptr;
	return Definition && Definition->CanServeFishingRod();
}

void ACatfishingPlayerController::PackHeldRodFromInput()
{
	if (!IsLocalController() || IsDayTransitionInputBlocked() || IsMoveInputIgnored()) return;
	if (FishingCommandComponent) FishingCommandComponent->ClearHeldInputForLifecycle(TEXT("PackRod"));
	const auto* Rod = UCatFishingCameraComponent::FindHeldRodOperatedBy(this);
	if (Rod) ServerPackHeldRod(FGuid::NewGuid(), Rod->GetPresentationState().RodActorId);
}
void ACatfishingPlayerController::ServerPackHeldRod_Implementation(const FGuid RequestId, const FGuid RodId)
{
	if (!RequestId.IsValid() || !CanForwardFishingCommand()) return;
	auto* Fishing = GetWorld()->GetSubsystem<UCatFishingService>();
	auto* Rod = Fishing ? Fishing->FindRodOperatedBy(PlayerState) : nullptr;
	if (!Rod || Rod->GetPresentationState().RodActorId != RodId)
	{
		UE_LOG(LogCatfishing, Warning, TEXT("Event=quickbar_rod_request_rejected RequestId=%s RodActorId=%s Reason=HeldRodChanged %s"),
			*RequestId.ToString(), *RodId.ToString(), *CatLogContext::BuildControllerFields(this));
		return;
	}
	auto* BackPack = GetControlledBackPack();
	auto* SourceEquipment = Fishing->ResolveRodEquipmentFromAuthority(Rod);
	auto* SourceInventory = SourceEquipment ? SourceEquipment->GetOwner()->FindComponentByClass<UCatInventoryComponent>() : nullptr;
	const auto* Held = SourceInventory ? SourceInventory->FindHeldInventoryEntryFromAuthority(Rod->GetPresentationState().ItemInstanceId) : nullptr;
	FCatInventoryReceiveBatch ReturnBatch;
	if (Held)
	{
		auto& Entry = ReturnBatch.InstanceEntries.AddDefaulted_GetRef();
		Entry.ItemInstance = Held->Instance; Entry.Count = Held->StackCount;
	}
	if (!Held || !BackPack || !BackPack->CanFullyAcceptInventoryBatch(ReturnBatch))
	{
		FCatFishingCommandResult Rejected;
		Rejected.RequestId = RequestId; Rejected.RodActorId = RodId; Rejected.CommandType = ECatFishingCommandType::PackRod;
		Rejected.Error = ECatFishingCommandError::DependencyUnavailable;
		if (FishingCommandComponent) FishingCommandComponent->DeliverResultFromAuthority(Rejected);
		UE_LOG(LogCatfishing, Warning, TEXT("Event=quickbar_rod_pack_rejected RequestId=%s RodActorId=%s Reason=InventoryCannotReceive %s"),
			*RequestId.ToString(), *RodId.ToString(), *CatLogContext::BuildControllerFields(this));
		return;
	}
	if (FishingCommandComponent) FishingCommandComponent->ClearHeldInputForLifecycle(TEXT("PackRod"));
	if (auto* Session = Fishing->FindActiveSessionByRod(Rod))
	{
		FCatFishingSessionCommandContext Context;
		Context.RequestId = FGuid::NewGuid(); Context.FishingSessionId = Session->GetSnapshot().FishingSessionId;
		Context.CastAttemptId = Session->GetSnapshot().CastAttemptId; Context.ExpectedRevision = Session->GetSnapshot().Revision;
		const auto CutResult = Session->CutLineFromAuthority(this, Context);
		if (FishingCommandComponent) FishingCommandComponent->DeliverResultFromAuthority(CutResult);
		if (!CutResult.bCommitted) return;
	}
	FCatLeaveRodCommand Leave;
	Leave.Context.RequestId = FGuid::NewGuid();
	Leave.Context.RodActorId = RodId;
	Leave.Context.ExpectedRodActorRevision = Rod->GetPresentationState().RodActorRevision;
	bool bCommitted = Fishing->LeaveRod(this, Leave).bCommitted;
	if (bCommitted)
	{
		FCatPackRodCommand Pack;
		Pack.Context.RequestId = RequestId; Pack.Context.RodActorId = RodId;
		Pack.Context.ExpectedRodActorRevision = Rod->GetPresentationState().RodActorRevision;
		const auto PackResult = Fishing->PackRod(this, Pack);
		if (FishingCommandComponent) FishingCommandComponent->DeliverResultFromAuthority(PackResult);
		bCommitted = PackResult.bCommitted;
	}
	if (bCommitted) BackPack->ClearQuickbarHeldSlotFromAuthority();
	else if (!Rod->GetPresentationState().bBroken && !Fishing->FindRodOperatedBy(PlayerState))
	{
		FCatOperateRodCommand Restore;
		Restore.Context.RequestId = FGuid::NewGuid(); Restore.Context.RodActorId = RodId;
		Restore.Context.ExpectedRodActorRevision = Rod->GetPresentationState().RodActorRevision;
		const auto Restored = Fishing->OperateRod(this, Restore);
		if (FishingCommandComponent) FishingCommandComponent->DeliverResultFromAuthority(Restored);
	}
	// 收回不改变当前选中格的含义：归还的仍是该格鱼竿时，沿唯一选格入口重新装备原实例。
	// 不从客户端旧库存发二次请求，避免归还复制尚未抵达时携带空 ItemId；新 RodActorId 也隔离旧 X 重放。
	if (bCommitted)
	{
		const FGuid ReturnedItemId = Rod->GetPresentationState().ItemInstanceId;
		const int32 ReturnedSlot = BackPack->FindInventorySlotIndexFromInstanceId(ReturnedItemId);
		if (ReturnedSlot != INDEX_NONE && ReturnedSlot == AuthorityQuickbarSlotIndex)
		{
			const FGuid EquipRequestId = FGuid::NewGuid();
			ServerSelectQuickbarSlot_Implementation(EquipRequestId, ReturnedSlot, ReturnedItemId);
			const auto* EquippedRod = Fishing->FindRodOperatedBy(PlayerState);
			const bool bEquipped = EquippedRod && EquippedRod->GetPresentationState().ItemInstanceId == ReturnedItemId;
			UE_LOG(LogCatfishing, Log, TEXT("Event=quickbar_rod_pack_selection_restored RequestId=%s EquipRequestId=%s ItemId=%s Slot=%d Equipped=%d %s"),
				*RequestId.ToString(), *EquipRequestId.ToString(), *ReturnedItemId.ToString(), ReturnedSlot, bEquipped,
				*CatLogContext::BuildControllerFields(this));
		}
	}
	UE_LOG(LogCatfishing, Log, TEXT("Event=quickbar_rod_pack_result RequestId=%s RodActorId=%s Committed=%d %s"),
		*RequestId.ToString(), *RodId.ToString(), bCommitted, *CatLogContext::BuildControllerFields(this));
}

void ACatfishingPlayerController::ParkHeldRodFromInput()
{
	if (!IsLocalController() || IsDayTransitionInputBlocked() || IsMoveInputIgnored()) return;
	if (FishingCommandComponent) FishingCommandComponent->ClearHeldInputForLifecycle(TEXT("ParkRod"));
	const auto* Rod = UCatFishingCameraComponent::FindHeldRodOperatedBy(this);
	if (Rod) ServerParkHeldRod(FGuid::NewGuid(), Rod->GetPresentationState().RodActorId);
}
void ACatfishingPlayerController::ServerParkHeldRod_Implementation(const FGuid RequestId, const FGuid RodId)
{
	if (!RequestId.IsValid() || !CanForwardFishingCommand()) return;
	auto* Fishing = GetWorld()->GetSubsystem<UCatFishingService>();
	auto* Rod = Fishing ? Fishing->FindRodOperatedBy(PlayerState) : nullptr;
	if (!Rod || Rod->GetPresentationState().RodActorId != RodId)
	{
		UE_LOG(LogCatfishing, Warning, TEXT("Event=quickbar_rod_request_rejected RequestId=%s RodActorId=%s Reason=HeldRodChanged %s"),
			*RequestId.ToString(), *RodId.ToString(), *CatLogContext::BuildControllerFields(this));
		return;
	}
	if (FishingCommandComponent) FishingCommandComponent->ClearHeldInputForLifecycle(TEXT("ParkRod"));
	FCatLeaveRodCommand Leave;
	Leave.Context.RequestId = RequestId;
	Leave.Context.RodActorId = Rod->GetPresentationState().RodActorId;
	Leave.Context.ExpectedRodActorRevision = Rod->GetPresentationState().RodActorRevision;
	const auto Result = Fishing->LeaveRod(this, Leave);
	if (FishingCommandComponent) FishingCommandComponent->DeliverResultFromAuthority(Result);
	if (Result.bCommitted) if (auto* BackPack = GetControlledBackPack()) BackPack->ClearQuickbarHeldSlotFromAuthority();
	UE_LOG(LogCatfishing, Log, TEXT("Event=quickbar_rod_park_result RequestId=%s RodActorId=%s Committed=%d %s"),
		*RequestId.ToString(), *Leave.Context.RodActorId.ToString(), Result.bCommitted, *CatLogContext::BuildControllerFields(this));
}

// 物品栏焦点读取流程：不存在库存格时无选择，容量尚在复制或缩小时返回有效第一格；库存内容变化不改变玩家选中的槽位。
int32 ACatfishingPlayerController::GetSelectedQuickbarSlotIndex() const
{
	const UCatBackPackComponent* BackPack = IsLocalController() ? GetControlledBackPack() : nullptr;
	if (!BackPack || BackPack->GetInventorySlotCount() <= 0) return INDEX_NONE;
	return BackPack->IsValidInventorySlotIndex(SelectedQuickbarSlotIndex) ? SelectedQuickbarSlotIndex : 0;
}

// 滚轮物品栏选择流程：只读背包实际格数逐格循环，包含空格；最终复用物品栏选择入口的本地输入校验，背包窗口不参与。
bool ACatfishingPlayerController::RequestCycleQuickbarSlotFromInput(const int32 Direction)
{
	if (Direction == 0) return false;
	const UCatBackPackComponent* BackPack = GetControlledBackPack();
	const int32 SlotCount = BackPack ? BackPack->GetInventorySlotCount() : 0;
	if (SlotCount <= 0) return false;
	const int32 CurrentSlot = FMath::Max(0, GetSelectedQuickbarSlotIndex());
	const int32 Step = Direction < 0 ? -1 : 1;
	return RequestSelectQuickbarSlotFromInput((CurrentSlot + Step + SlotCount) % SlotCount);
}

// 使用输入预检流程：本机只确认没有输入锁、当前选中槽位仍存在且已经解析到实例；可用性和真正效果仍由服务器按本次槽位和实例重新裁决。
bool ACatfishingPlayerController::CanUseSelectedBackpackItemFromInput() const
{
	if (!IsLocalController() || IsDayTransitionInputBlocked() || IsMoveInputIgnored())
	{
		return false;
	}
	UCatBackPackComponent* BackPack = GetControlledBackPack();
	const int32 SelectedSlotIndex = GetSelectedQuickbarSlotIndex();
	return BackPack && BackPack->CanUseItemAtSlot(SelectedSlotIndex, GetPawn());
}

// 选中物品左键按下流程：
// 1. 先用 Controller 的独立物品栏焦点读取背包中的槽位和实例身份；未解析、空格或本地预检失败不发网络请求。
// 2. 为本次 Use 创建稳定 RequestId，并把槽位与观察到的实例 ID 原样交给服务器；服务器不会读取客户端的“已选中”声明。
// 3. 只有声明持续输入的实例才在本机冻结这组身份，松开和取消因此不会改用之后新选中的物品。
void ACatfishingPlayerController::BeginSelectedItemUseFromInput()
{
	if (ActiveSelectedItemUseRequestId.IsValid() || IsQuickbarRodSelected() || !CanUseSelectedBackpackItemFromInput())
	{
		return;
	}
	UCatBackPackComponent* BackPack = GetControlledBackPack();
	const int32 SelectedSlotIndex = GetSelectedQuickbarSlotIndex();
	const FCatInventoryEntry* Entry = BackPack ? BackPack->GetInventoryEntryAtSlot(SelectedSlotIndex) : nullptr;
	UCatInventoryItemInstance* Instance = Entry ? Entry->Instance.Get() : nullptr;
	if (!Instance || !Instance->GetItemInstanceId().IsValid())
	{
		return;
	}
	const FGuid RequestId = FGuid::NewGuid();
	if (Instance->UsesContinuousInput())
	{
		ActiveSelectedItemUseRequestId = RequestId;
		ActiveSelectedItemUseItemId = Instance->GetItemInstanceId();
		ActiveSelectedItemUseSlotIndex = SelectedSlotIndex;
		ActiveSelectedItemUseBackPack = BackPack;
		ActiveSelectedItemUseInstance = Instance;
		Instance->SetUseInputActiveLocally(this, true);
	}
	UE_LOG(LogCatfishing, Log,
		TEXT("Event=selected_inventory_use_submitted RequestId=%s Slot=%d ItemId=%s Continuous=%d World=%s NetMode=%d Authority=%d LocalRole=%d Controller=%s"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens), SelectedSlotIndex,
		*Instance->GetItemInstanceId().ToString(EGuidFormats::DigitsWithHyphens), Instance->UsesContinuousInput(),
		*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), HasAuthority(), static_cast<int32>(GetLocalRole()), *GetNameSafe(this));
	ServerUseSelectedBackpackItem(RequestId, SelectedSlotIndex, Instance->GetItemInstanceId(), Instance->CaptureUseTarget(this));
}

// 选中物品左键松开流程：持续 Use 已在按下时冻结了 RequestId、实例和槽位；本地清记录后发送同一组值，普通瞬时物品不会生成结束请求。
void ACatfishingPlayerController::EndSelectedItemUseFromInput(const bool bCancelled)
{
	ClearSelectedItemUseInput(bCancelled);
}

// 持续使用清理流程：
// 1. 没有活动请求时直接返回，普通物品的 左键 Completed/Canceled 不会制造额外服务器动作。
// 2. 有请求时先保存 Begin 固定身份，再在 authority 直接执行或客户端发 RPC，保证 Pawn 切换和旅行也能取消旧效果。
// 3. 客户端发送后清空自己的记录；authority 交给 End RPC 在身份匹配后清理，避免 listen host 先丢失原实例。
void ACatfishingPlayerController::ClearSelectedItemUseInput(const bool bCancelled)
{
	if (!ActiveSelectedItemUseRequestId.IsValid())
	{
		return;
	}
	const FGuid RequestId = ActiveSelectedItemUseRequestId;
	const FGuid ItemInstanceId = ActiveSelectedItemUseItemId;
	if (ActiveSelectedItemUseInstance)
	{
		ActiveSelectedItemUseInstance->SetUseInputActiveLocally(this, false);
	}
	if (HasAuthority())
	{
		ServerEndSelectedBackpackItem_Implementation(RequestId, ItemInstanceId, bCancelled);
		return;
	}
	else if (IsLocalController())
	{
		ServerEndSelectedBackpackItem(RequestId, ItemInstanceId, bCancelled);
	}
	ActiveSelectedItemUseRequestId.Invalidate();
	ActiveSelectedItemUseItemId.Invalidate();
	ActiveSelectedItemUseSlotIndex = INDEX_NONE;
	ActiveSelectedItemUseBackPack = nullptr;
	ActiveSelectedItemUseInstance = nullptr;
}

// 快捷栏 Use RPC 流程：
// 1. 服务器只从当前 Controller 的 Character 解析个人 BackPack，并重新校验 RequestId、槽位和实例身份。
// 2. 再构造同一份库存使用上下文并执行既有 Inventory.Action.Use；客户端本地选中状态不会参与权限判断。
// 3. 连续实例仅在 Begin 成功后保存固定身份；所有结果写入统一领域回执和 Development 日志。
void ACatfishingPlayerController::ServerUseSelectedBackpackItem_Implementation(const FGuid RequestId,
	const int32 ExpectedSelectedSlot, const FGuid ItemInstanceId, FCatInventoryUseTarget Target)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	UCatBackPackComponent* BackPack = GetControlledBackPack();
	const FCatInventoryEntry* Entry = BackPack ? BackPack->GetInventoryEntryAtSlot(ExpectedSelectedSlot) : nullptr;
	UCatInventoryItemInstance* Instance = Entry ? Entry->Instance.Get() : nullptr;
	if (!CanForwardGameplayCommand())
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (!RequestId.IsValid() || !ItemInstanceId.IsValid() || !BackPack || !Entry || !Instance
		|| Entry->StackCount <= 0 || Instance->GetItemInstanceId() != ItemInstanceId)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (ActiveSelectedItemUseRequestId.IsValid() && ActiveSelectedItemUseRequestId != RequestId)
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
	}
	else
	{
		FCatInventoryItemUseContext Context;
		Context.RequestId = RequestId;
		Context.RequestingController = this;
		Context.UserPawn = GetPawn();
		Context.SourceInventory = BackPack;
		Context.InventorySlotIndex = ExpectedSelectedSlot;
		Context.bContinuousInput = Instance->UsesContinuousInput();
		Context.Target = Target;
		Context.OnCompleted = [WeakThis = TWeakObjectPtr<ThisClass>(this)](const FCatDomainCommandResult& Final)
		{
			if (ThisClass* Controller = WeakThis.Get()) Controller->DeliverCampCommandResultToOwningClient(Final);
		};
		Result = BackPack->ExecuteItemActionFromAuthority(Context, ItemInstanceId, CatInventoryActionTags::Use, 1);
		if (Result.bCommitted && !Result.bTerminalReplay && Instance->UsesContinuousInput())
		{
			ActiveSelectedItemUseRequestId = RequestId;
			ActiveSelectedItemUseItemId = ItemInstanceId;
			ActiveSelectedItemUseSlotIndex = ExpectedSelectedSlot;
			ActiveSelectedItemUseBackPack = BackPack;
			ActiveSelectedItemUseInstance = Instance;
		}
	}
	if (Result.bPending) return; // 最终回执由库存完成口发送，排队不是失败。
	const FString UseEvent = FString::Printf(
		TEXT("Event=selected_inventory_use_result RequestId=%s Slot=%d ItemId=%s Committed=%d Error=%s World=%s NetMode=%d Authority=%d LocalRole=%d Controller=%s"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens), ExpectedSelectedSlot,
		*ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), Result.bCommitted, *UEnum::GetValueAsString(Result.Error),
		*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), HasAuthority(), static_cast<int32>(GetLocalRole()), *GetNameSafe(this));
	if (Result.bCommitted)
	{
		UE_LOG(LogCatfishing, Log, TEXT("%s"), *UseEvent);
	}
	else
	{
		UE_LOG(LogCatfishing, Warning, TEXT("%s"), *UseEvent);
	}
	DeliverCampCommandResultToOwningClient(Result);
}

// 持续 Use 结束 RPC 流程：
// 1. 只接受服务器 Begin 成功后保存的 RequestId、实例和背包；当前本地选中格不参与匹配。
// 2. 直接使用 Begin 保留的原实例和背包构造上下文；槽位换物或物品暂时离开可见格仍取消原持续效果，绝不改用新实例。
// 3. 只有完整身份匹配的本轮结束才清服务器活动记录；迟到旧 End 被拒绝但不能取消较新的 Use。
void ACatfishingPlayerController::ServerEndSelectedBackpackItem_Implementation(const FGuid RequestId,
	const FGuid ItemInstanceId, const bool bCancelled)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	UCatBackPackComponent* BackPack = ActiveSelectedItemUseBackPack;
	UCatInventoryItemInstance* Instance = ActiveSelectedItemUseInstance;
	const bool bMatchesActiveUse = RequestId.IsValid() && RequestId == ActiveSelectedItemUseRequestId
		&& ItemInstanceId == ActiveSelectedItemUseItemId;
	if (!bMatchesActiveUse
		|| !BackPack || !Instance
		|| Instance->GetItemInstanceId() != ItemInstanceId)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else
	{
		FCatInventoryItemUseContext Context;
		Context.RequestId = RequestId;
		Context.RequestingController = this;
		Context.UserPawn = GetPawn();
		Context.SourceInventory = BackPack;
		Context.InventorySlotIndex = ActiveSelectedItemUseSlotIndex;
		Context.bContinuousInput = true;
		Result = Instance->EndUseFromInventorySlotFromAuthority(Context, bCancelled);
	}
	if (bMatchesActiveUse)
	{
		ActiveSelectedItemUseRequestId.Invalidate();
		ActiveSelectedItemUseItemId.Invalidate();
		ActiveSelectedItemUseSlotIndex = INDEX_NONE;
		ActiveSelectedItemUseBackPack = nullptr;
		ActiveSelectedItemUseInstance = nullptr;
	}
	const FString EndEvent = FString::Printf(
		TEXT("Event=selected_inventory_use_end_result RequestId=%s ItemId=%s Cancelled=%d Committed=%d Error=%s World=%s NetMode=%d Authority=%d LocalRole=%d Controller=%s"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens), *ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		bCancelled, Result.bCommitted, *UEnum::GetValueAsString(Result.Error), *GetNameSafe(GetWorld()),
		static_cast<int32>(GetNetMode()), HasAuthority(), static_cast<int32>(GetLocalRole()), *GetNameSafe(this));
	if (Result.bCommitted)
	{
		UE_LOG(LogCatfishing, Log, TEXT("%s"), *EndEvent);
	}
	else
	{
		UE_LOG(LogCatfishing, Warning, TEXT("%s"), *EndEvent);
	}
	DeliverCampCommandResultToOwningClient(Result);
}

// 统一库存操作 RPC 路由流程：
// 1. 先记录客户端提交的宿主、槽位、实例、动作和数量，供房主端与客户端日志按 RequestId 对照。
// 2. 命令门关闭时只产生拒绝回执；开启时由 Statics 重新解析当前 Pawn 可访问的正式库存，绝不信任客户端定义或效果。
// 3. 组件随后复核实例身份、数量、定义声明和实时可用性，并把动作交给实例虚函数提交。
// 4. 最后按成功或拒绝的诊断等级落盘并可靠回送 owning client；重放只回显首次终态，不重复执行副作用。
void ACatfishingPlayerController::ServerExecuteInventoryAction_Implementation(const FGuid RequestId,
	AActor* SourceInventoryHost, const int32 SourceSlotIndex, const FGuid ItemInstanceId,
	const FGameplayTag Action, const int32 Quantity, FCatInventoryUseTarget Target)
{
	FCatDomainCommandResult Result; Result.RequestId = RequestId;
	UE_LOG(LogCatfishing, Log, TEXT("Event=inventory_action_received World=%s NetMode=%d Authority=%d Player=%s RequestId=%s Host=%s Slot=%d Instance=%s Action=%s Quantity=%d"),
		*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), HasAuthority(), *GetName(), *RequestId.ToString(),
		*GetNameSafe(SourceInventoryHost), SourceSlotIndex, *ItemInstanceId.ToString(), *Action.ToString(), Quantity);
	if (!CanForwardGameplayCommand()) Result.Error = ECatDomainCommandError::CommandsClosed;
	else Result = UCatInventoryStatics::ExecuteInventoryActionFromAuthority(Cast<ACatCharacter>(GetPawn()),
		RequestId, SourceInventoryHost, SourceSlotIndex, ItemInstanceId, Action, Quantity, Target,
		[WeakThis = TWeakObjectPtr<ThisClass>(this)](const FCatDomainCommandResult& Final)
		{
			if (ThisClass* Controller = WeakThis.Get()) Controller->DeliverCampCommandResultToOwningClient(Final);
		});
	if (Result.bPending) return;
	const FString Event = FString::Printf(
		TEXT("Event=inventory_action_result World=%s NetMode=%d Authority=%d LocalRole=%d Player=%s RequestId=%s Host=%s Slot=%d Instance=%s Action=%s Quantity=%d Committed=%d Replay=%d Error=%s"),
		*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), HasAuthority(), static_cast<int32>(GetLocalRole()), *GetName(),
		*RequestId.ToString(), *GetNameSafe(SourceInventoryHost), SourceSlotIndex, *ItemInstanceId.ToString(), *Action.ToString(),
		Quantity, Result.bCommitted, Result.bTerminalReplay, *UEnum::GetValueAsString(Result.Error));
	if (CatIsAcceptedDomainCommandResult(Result))
	{
		UE_LOG(LogCatfishing, Log, TEXT("%s"), *Event);
	}
	else
	{
		UE_LOG(LogCatfishing, Warning, TEXT("%s"), *Event);
	}
	DeliverCampCommandResultToOwningClient(Result);
}

// 交互 RPC 流程：先过玩法 gate、RequestId、World 和接口校验，再让目标 Actor 按自己的 Interact 实现处理；失败分支保持无副作用返回。
void ACatfishingPlayerController::ServerRequestInteraction_Implementation(AActor* Target, const FGuid RequestId)
{
	// 服务器先记录命令门与目标校验，再执行同一接口；拒绝也落盘，避免客户端只看到按键没有结果。
	const bool bGameplayOpen = CanForwardGameplayCommand();
	const bool bValidTarget = IsValid(Target) && Target->GetWorld() == GetWorld()
		&& Target->GetClass()->ImplementsInterface(UCatInteractable::StaticClass());
	const bool bAccepted = bGameplayOpen && RequestId.IsValid() && bValidTarget
		&& (Cast<ACatFishPickupActor>(Target) || ICatInteractable::Execute_CanInteract(Target, this));
	if (!bAccepted)
	{
		UE_LOG(LogCatfishing, Warning, TEXT("Event=interaction_request_rejected World=%s NetMode=%d Authority=%d LocalRole=%d Player=%s Target=%s RequestId=%s GameplayOpen=%d ValidTarget=%d ValidRequest=%d"),
			*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), HasAuthority(), static_cast<int32>(GetLocalRole()), *GetName(),
			*GetNameSafe(Target), *RequestId.ToString(), bGameplayOpen, bValidTarget, RequestId.IsValid());
		return;
	}
	UE_LOG(LogCatfishing, Log, TEXT("Event=interaction_request_accepted World=%s NetMode=%d Authority=%d LocalRole=%d Player=%s Target=%s RequestId=%s"),
		*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), HasAuthority(), static_cast<int32>(GetLocalRole()), *GetName(), *GetNameSafe(Target), *RequestId.ToString());
	ICatInteractable::Execute_Interact(Target, this, RequestId);
}

// 摊位购物车支付 RPC 流程：服务器只接受来源摊位引用和 EntryId/次数意图，不接受客户端提交的价格、库存或收货仓库。
void ACatfishingPlayerController::ServerSubmitShopCartAtKiosk_Implementation(ACatShopKioskActor* ShopKiosk,
	const TArray<FCatShopCartLineCommand>& Lines, const FGuid RequestId)
{
	UE_LOG(LogCatfishing, Log,
		TEXT("Event=shop_cart_requested RequestId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Player=%s Shop=%s Lines=%d"),
		*RequestId.ToString(), *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole(),
		*GetName(), *GetNameSafe(ShopKiosk), Lines.Num());
	FCatDomainCommandResult DeliveryResult;
	DeliveryResult.RequestId = RequestId;
	if (UCatShopTradeController* Controller = GetWorld()
		? GetWorld()->GetSubsystem<UCatShopTradeController>() : nullptr)
	{
		DeliveryResult = Controller->SubmitCartFromKiosk(this, ShopKiosk, Lines, RequestId).Delivery;
	}
	else
	{
		DeliveryResult.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	DeliveryResult.RequestId = RequestId;
	const FString CartEvent = FString::Printf(
		TEXT("Event=shop_cart_result RequestId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Player=%s Shop=%s Committed=%d Replay=%d Error=%s FailureReason=%s"),
		*RequestId.ToString(), *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole(),
		*GetName(), *GetNameSafe(ShopKiosk), DeliveryResult.bCommitted, DeliveryResult.bTerminalReplay,
		*UEnum::GetValueAsString(DeliveryResult.Error), *DeliveryResult.FailureReason.ToString());
	if (CatIsAcceptedDomainCommandResult(DeliveryResult)) { UE_LOG(LogCatfishing, Log, TEXT("%s"), *CartEvent); }
	else { UE_LOG(LogCatfishing, Warning, TEXT("%s"), *CartEvent); }
	DeliverCampCommandResultToOwningClient(DeliveryResult);
}

// 售鱼路由流程：记录买家与鱼身份请求后转交协调器重读来源和距离，再按原请求记录并回送结果；失败和重放不丢失跨端关联。
void ACatfishingPlayerController::ServerSellFishBatch_Implementation(const FGuid RequestId,
	ACatFishBuyerActor* Buyer, ACatFishGuardActor* Guard, const TArray<FGuid>& FishInstanceIds)
{
	UE_LOG(LogCatfishing, Log,
		TEXT("Event=fish_sale_requested RequestId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Player=%s Buyer=%s Source=%s FishCount=%d"),
		*RequestId.ToString(), *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole(), *GetName(),
		*GetNameSafe(Buyer), *GetNameSafe(Guard), FishInstanceIds.Num());
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (UCatShopTradeController* Trading = GetWorld() ? GetWorld()->GetSubsystem<UCatShopTradeController>() : nullptr)
	{
		Result = Trading->SubmitFishSaleFromPlayer(this, Buyer, Guard, FishInstanceIds, RequestId).Delivery;
	}
	else Result.Error = ECatDomainCommandError::DependencyUnavailable;
	const FString Event = FString::Printf(
		TEXT("Event=fish_sale_result RequestId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Player=%s Buyer=%s Source=%s Committed=%d Replay=%d Error=%s"),
		*RequestId.ToString(), *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole(), *GetName(),
		*GetNameSafe(Buyer), *GetNameSafe(Guard), Result.bCommitted, Result.bTerminalReplay, *UEnum::GetValueAsString(Result.Error));
	if (CatIsAcceptedDomainCommandResult(Result)) { UE_LOG(LogCatfishing, Log, TEXT("%s"), *Event); }
	else { UE_LOG(LogCatfishing, Warning, TEXT("%s"), *Event); }
	DeliverCampCommandResultToOwningClient(Result);
}

// 丢弃请求流程：
// 1. 先用 RequestId 防重入，重复请求只记录拒绝，不再次释放。
// 2. 再核对玩法命令门、当前 Pawn、客户端声明的原 Actor 和携带代次，确保 Q 的目标仍是按下时那次嘴叼。
// 3. 只有当前本人嘴叼物且代次匹配才把同一 RequestId 交给公共释放；空嘴、旧目标、旧携带和玩法门关闭都不以当前新目标替代请求里的对象。
void ACatfishingPlayerController::ServerDropCarriedItem_Implementation(FGuid RequestId, AActor* ExpectedItem, uint32 ExpectedCarryRevision)
{
	ACatCharacter* CatCharacter = Cast<ACatCharacter>(GetPawn());
	ACatCarryableActor* Item = Cast<ACatCarryableActor>(ExpectedItem);
	bool bDropped = false;
	const bool bFresh = RequestId.IsValid() && !ProcessedDropRequests.Contains(RequestId);
	if (bFresh)
	{
		ProcessedDropRequests.Add(RequestId);
		if (CanForwardGameplayCommand() && CatCharacter && IsValid(Item)
			&& CatCharacter->GetMouthCarriedActor() == Item && Item->GetCarryRevision() == ExpectedCarryRevision)
		{
			bDropped = Item->DropFromAuthority(this, RequestId);
		}
	}
	const FString Event = FString::Printf(TEXT("Event=mouth_drop_result RequestId=%s ItemActor=%s CarryRevision=%u Fresh=%d Dropped=%d Player=%s World=%s NetMode=%d Authority=%d LocalRole=%d"),
		*RequestId.ToString(), *GetNameSafe(ExpectedItem), ExpectedCarryRevision, bFresh, bDropped, *GetName(), *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole());
	if (bDropped) { UE_LOG(LogCatfishing, Log, TEXT("%s"), *Event); }
	else { UE_LOG(LogCatfishing, Warning, TEXT("%s"), *Event); }
}

// 鱼护拾取路由：记录请求后确认命令窗口与同世界对象，再让鱼护裁决所有权和容量；按原请求记录及回送结果，不创建第二份携带状态。
void ACatfishingPlayerController::ServerPickUpFishGuard_Implementation(ACatFishGuardActor* Guard, const FGuid RequestId)
{
	UE_LOG(LogCatfishing, Log,
		TEXT("Event=fish_guard_pickup_requested RequestId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Player=%s Guard=%s"),
		*RequestId.ToString(), *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole(), *GetName(), *GetNameSafe(Guard));
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (!CanForwardGameplayCommand()) Result.Error = ECatDomainCommandError::CommandsClosed;
	else if (!RequestId.IsValid() || !IsValid(Guard) || Guard->GetWorld() != GetWorld())
		Result.Error = ECatDomainCommandError::InvalidPayload;
	else Result.Error = Guard->PickUpFromAuthority(this, RequestId)
		? ECatDomainCommandError::None : ECatDomainCommandError::InvalidPayload;
	Result.bCommitted = Result.Error == ECatDomainCommandError::None;
	const FString Event = FString::Printf(
		TEXT("Event=fish_guard_pickup_result RequestId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Player=%s Guard=%s Committed=%d Error=%s"),
		*RequestId.ToString(), *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole(), *GetName(),
		*GetNameSafe(Guard), Result.bCommitted, *UEnum::GetValueAsString(Result.Error));
	if (Result.bCommitted) { UE_LOG(LogCatfishing, Log, TEXT("%s"), *Event); }
	else { UE_LOG(LogCatfishing, Warning, TEXT("%s"), *Event); }
	DeliverCampCommandResultToOwningClient(Result);
}

// 三选一 RPC 路由流程：Controller 只转交选择意图；池、出现次序、上限与叠加全部由 Growth 组件按服务器配表裁决。
// 面板不冻结世界、个人选择不阻挡他人，所以这里不设任何全局门，只走普通玩法命令窗口。
void ACatfishingPlayerController::ServerChooseGrowthOption_Implementation(const FGuid RequestId,
	const ECatGrowthOptionId OptionId, const int32 OfferSerial)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	UCatGrowthComponent* Growth = ControlledCharacter ? ControlledCharacter->GetGrowthComponent() : nullptr;
	if (!CanForwardGameplayCommand())
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (!Growth)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else
	{
		Result = Growth->ChooseOfferedOptionFromAuthority(this, RequestId, OptionId, OfferSerial);
	}
	DeliverCampCommandResultToOwningClient(Result);
}

// 公共领域回执投递流程：单机或 listen server 本地玩家没有远端连接可回送时，直接复用 Client 实现刷新本机缓存；远端玩家保持可靠 RPC 语义。
void ACatfishingPlayerController::DeliverCampCommandResultToOwningClient(const FCatDomainCommandResult& Result)
{
	if (HasAuthority() && IsLocalController())
	{
		ClientReceiveCampCommandResult_Implementation(Result);
		return;
	}
	ClientReceiveCampCommandResult(Result);
}

// 手动求助 RPC 路由流程：只把 RequestId 和求助类型投给 Social BodyAction；Ability 未接管时回送依赖错误，不发布信号。
void ACatfishingPlayerController::ServerRequestManualHelp_Implementation(const FGuid RequestId,
	const ECatHelpSignalKind Kind)
{
	if (!SocialBodyActionCommandComponent || !SocialBodyActionCommandComponent->SubmitManualHelp(RequestId, Kind))
	{
		FCatDomainCommandResult Result;
		Result.RequestId = RequestId;
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		DeliverCampCommandResultToOwningClient(Result);
	}
}

// 恶作剧 RPC 路由流程：只把目标 PlayerState、RequestId 和交互位置投给 Social BodyAction；Ability 未接管时回送依赖错误，不进入 Social。
void ACatfishingPlayerController::ServerRequestMischief_Implementation(APlayerState* TargetPlayerState,
	const FGuid RequestId, const FVector InteractionLocation)
{
	if (!SocialBodyActionCommandComponent
		|| !SocialBodyActionCommandComponent->SubmitMischief(TargetPlayerState, RequestId, InteractionLocation))
	{
		FCatDomainCommandResult Result;
		Result.RequestId = RequestId;
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		DeliverCampCommandResultToOwningClient(Result);
	}
}

// 放牌 RPC 路由流程：只把 RequestId 和期望位置投给 Social BodyAction；Ability 未接管时回送依赖错误，不生成保护牌。
void ACatfishingPlayerController::ServerPlaceProtectionSign_Implementation(const FGuid RequestId,
	const FVector SignLocation)
{
	if (!SocialBodyActionCommandComponent
		|| !SocialBodyActionCommandComponent->SubmitPlaceProtectionSign(RequestId, SignLocation))
	{
		FCatDomainCommandResult Result;
		Result.RequestId = RequestId;
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		DeliverCampCommandResultToOwningClient(Result);
	}
}

// Native 输入分流流程：
// 1. 先拒绝翻天操作；键盘/滚轮选择标签还要服从现有模态输入锁，而背包页面按钮直接调用选择入口时仍可更新本地焦点。
// 2. G Started 发起当前槽位/实例的统一 Use；Q 只在未被模态输入锁拦截且本地确有嘴叼物时提交目标 Actor 与携带代次。
// 3. 提交 Q 前结束可能正在蓄力的交互输入，避免长按交互和丢弃同时占用同一玩家意图。
// 4. 其余 IA_Interact 继续由唯一 TargetingComponent 处理；不认识的标签无副作用返回。
void ACatfishingPlayerController::NativeInputTagPressed(const FGameplayTag InputTag)
{
	if (UCatGE_FishingScoopCooldown::IsOperationBlocked(GetPawn())) return;
	if (IsDayTransitionInputBlocked()) return;
	const bool bIsInventorySelectionInput = InputTag.MatchesTagExact(CatInventoryInputTags::Input_SelectSlot1)
		|| InputTag.MatchesTagExact(CatInventoryInputTags::Input_SelectSlot2)
		|| InputTag.MatchesTagExact(CatInventoryInputTags::Input_SelectSlot3)
		|| InputTag.MatchesTagExact(CatInventoryInputTags::Input_SelectSlot4)
		|| InputTag.MatchesTagExact(CatInventoryInputTags::Input_SelectPreviousSlot)
		|| InputTag.MatchesTagExact(CatInventoryInputTags::Input_SelectNextSlot);
	if (bIsInventorySelectionInput && IsMoveInputIgnored()) return;
	if (InputTag.MatchesTagExact(CatInventoryInputTags::Input_SelectSlot1))
	{
		RequestSelectQuickbarSlotFromInput(0);
		return;
	}
	if (InputTag.MatchesTagExact(CatInventoryInputTags::Input_SelectSlot2))
	{
		RequestSelectQuickbarSlotFromInput(1);
		return;
	}
	if (InputTag.MatchesTagExact(CatInventoryInputTags::Input_SelectSlot3))
	{
		RequestSelectQuickbarSlotFromInput(2);
		return;
	}
	if (InputTag.MatchesTagExact(CatInventoryInputTags::Input_SelectSlot4))
	{
		RequestSelectQuickbarSlotFromInput(3);
		return;
	}
	if (InputTag.MatchesTagExact(CatInventoryInputTags::Input_SelectPreviousSlot))
	{
		RequestCycleQuickbarSlotFromInput(-1);
		return;
	}
	if (InputTag.MatchesTagExact(CatInventoryInputTags::Input_SelectNextSlot))
	{
		RequestCycleQuickbarSlotFromInput(1);
		return;
	}
	if (InputTag == CatInventoryInputTags::Input_ParkRod) { ParkHeldRodFromInput(); return; }
	if (InputTag == CatInventoryInputTags::Input_PackRod) { PackHeldRodFromInput(); return; }
	if (InputTag.MatchesTagExact(CatInventoryInputTags::Input_UseSelectedItem))
	{
		BeginSelectedItemUseFromInput();
		return;
	}
	if (InputTag.MatchesTagExact(CatInteractionTags::Input_DropCarriedItem))
	{
		if (!IsLocalController() || IsMoveInputIgnored()) return;
		ACatCharacter* CatCharacter = Cast<ACatCharacter>(GetPawn());
		if (!CatCharacter) return;
		ACatCarryableActor* Item = Cast<ACatCarryableActor>(CatCharacter->GetMouthCarriedActor());
		if (!Item) return;
		const FGuid RequestId = FGuid::NewGuid();
		if (InteractionTargetingComponent) InteractionTargetingComponent->EndInteractionInput(true);
		UE_LOG(LogCatfishing, Log,
			TEXT("Event=mouth_drop_submitted RequestId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Player=%s ItemActor=%s"),
			*RequestId.ToString(), *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole(), *GetName(), *GetNameSafe(Item));
		ServerDropCarriedItem(RequestId, Item, Item->GetCarryRevision());
		return;
	}
	if (!InputTag.MatchesTagExact(CatInteractionTags::Input_Interact))
	{
		return;
	}
	if (InteractionTargetingComponent)
	{
		InteractionTargetingComponent->BeginInteractionInput();
	}
}

// 松开流程：左键只结束 Begin 固定的持续实例；交互标签仍由目标组件决定短按，翻天锁已接管时取消候选。
void ACatfishingPlayerController::NativeInputTagReleased(const FGameplayTag InputTag)
{
	if (InputTag.MatchesTagExact(CatInventoryInputTags::Input_UseSelectedItem))
	{
		EndSelectedItemUseFromInput(false);
		return;
	}
	if (InputTag.MatchesTagExact(CatInteractionTags::Input_Interact) && InteractionTargetingComponent)
		InteractionTargetingComponent->EndInteractionInput(IsDayTransitionInputBlocked());
}

// 取消流程：左键取消会终止同一次持续 Use；交互输入只释放计时器与候选，绝不提交短按或拾取命令。
void ACatfishingPlayerController::NativeInputTagCanceled(const FGameplayTag InputTag)
{
	if (InputTag.MatchesTagExact(CatInventoryInputTags::Input_UseSelectedItem))
	{
		EndSelectedItemUseFromInput(true);
		return;
	}
	if (InputTag.MatchesTagExact(CatInteractionTags::Input_Interact) && InteractionTargetingComponent)
		InteractionTargetingComponent->EndInteractionInput(true);
}

// 祭坛确认输入流程：
// 1. 只允许 owning client 从当前 GameState 快照读取等待中的有效请求，F8/F9 以目标布尔值表达意图而不计算人数。
// 2. 正式翻天锁已建立时拒绝发送；发起者没有再次确认操作，消费 F8 而不发 RPC，F9 仍交服务器裁决取消整轮。
// 3. 找到请求后记录关联键并可靠发送；服务器仍以 RPC 所属 Controller 和 GameMode 的公开名单完成最终裁决。
bool ACatfishingPlayerController::TrySetAltarConfirmationFromKey(const FKey& Key)
{
	const bool bConfirmed = Key == EKeys::F8;
	if (!bConfirmed && Key != EKeys::F9)
	{
		return false;
	}
	const ACatfishingGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
	const FCatAltarConfirmationSnapshot* Confirmation = GameState ? &GameState->GetRunPublicState().AltarConfirmation : nullptr;
	if (!IsLocalController() || IsDayTransitionInputBlocked() || !Confirmation
		|| Confirmation->State != ECatAltarConfirmationState::Waiting || !Confirmation->RequestId.IsValid())
	{
		return false;
	}
	if (bConfirmed && Confirmation->Initiator == PlayerState.Get())
	{
		return true;
	}
	UE_LOG(LogCatRun, Log,
		TEXT("Event=altar_confirmation_input_submitted RequestId=%s Confirmed=%d World=%s NetMode=%d Authority=%d LocalRole=%d Controller=%s"),
		*Confirmation->RequestId.ToString(EGuidFormats::DigitsWithHyphens), bConfirmed, *GetNameSafe(GetWorld()),
		static_cast<int32>(GetNetMode()), HasAuthority(), static_cast<int32>(GetLocalRole()), *GetNameSafe(this));
	ServerSetAltarConfirmation(Confirmation->RequestId, bConfirmed);
	return true;
}

// F8 输入流程：游戏视口没有 UMG 焦点时，直接把按键送入统一的确认入口；是否存在有效请求完全由该入口重读公开快照决定。
void ACatfishingPlayerController::ConfirmAltarConfirmationFromInput()
{
	TrySetAltarConfirmationFromKey(EKeys::F8);
}

// F9 输入流程：没有 UMG 焦点时送入同一入口；服务器识别发起者后取消整轮，其他人只撤回本人，迟到重复输入被等待态和请求 ID 拒绝。
void ACatfishingPlayerController::RevokeAltarConfirmationFromInput()
{
	TrySetAltarConfirmationFromKey(EKeys::F9);
}

// RPC 接收流程：服务器只接受 Controller 自己提交的目标确认状态，并把身份解析、请求时限、名单资格与最终结算全部委托给 GameMode。
void ACatfishingPlayerController::ServerSetAltarConfirmation_Implementation(const FGuid RequestId, const bool bConfirmed)
{
	ACatfishingGameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	if (!GameMode || !RequestId.IsValid())
	{
		UE_LOG(LogCatRun, Warning,
			TEXT("Event=altar_confirmation_intent_rejected RequestId=%s Confirmed=%d Reason=%s World=%s Controller=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens), bConfirmed,
			GameMode ? TEXT("InvalidRequestId") : TEXT("GameModeUnavailable"), *GetNameSafe(GetWorld()), *GetNameSafe(this));
		return;
	}
	UE_LOG(LogCatRun, Log,
		TEXT("Event=altar_confirmation_intent_received RequestId=%s Confirmed=%d World=%s NetMode=%d Controller=%s"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens), bConfirmed, *GetNameSafe(GetWorld()),
		static_cast<int32>(GetNetMode()), *GetNameSafe(this));
	GameMode->SetAltarConfirmation(this, RequestId, bConfirmed);
}

// 踢人 RPC 路由流程：只把目标 PlayerState 与 RequestId 交给 authority 的 RoomOwnerService；
// 本 RPC 不判断谁是房主、不碰会话、不碰存档——那三件分别在 RoomOwnerService、Online 与 Profile 手里。
void ACatfishingPlayerController::ServerKickPlayer_Implementation(APlayerState* TargetPlayerState,
	const FGuid RequestId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	Result.Error = ECatDomainCommandError::DependencyUnavailable;
	if (UCatRoomOwnerService* RoomOwner = GetWorld() ? GetWorld()->GetSubsystem<UCatRoomOwnerService>() : nullptr)
	{
		Result = RoomOwner->RequestKickPlayer(this, TargetPlayerState, RequestId);
	}
	DeliverCampCommandResultToOwningClient(Result);
}

// 主动离局 RPC 流程：只把当前 Controller 交给 authority GameMode；标记不销毁 Session、不旅行，并由随后 Logout 精确消费。
void ACatfishingPlayerController::ServerMarkVoluntaryLeave_Implementation()
{
	if (ACatfishingGameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr)
	{
		GameMode->MarkVoluntaryLeave(this);
	}
}

// Host exit 客户端流程：从本地 GameInstance 取得唯一 Online 子系统并提交服务器关联 RequestId；子系统只在本地 DestroySession 成功后回 ACK，失败会让 Host 继续等待真实回执。
void ACatfishingPlayerController::ClientPrepareForHostExit_Implementation(const FGuid RequestId)
{
	UGameInstance* GameInstance = GetGameInstance();
	if (UCatOnlineSubsystem* Online = GameInstance ? GameInstance->GetSubsystem<UCatOnlineSubsystem>() : nullptr)
	{
		Online->RequestRemoteHostExit(RequestId);
	}
}

// Host exit ACK 服务器流程：只把当前 Controller 与关联键交给 authority GameMode；RPC 自身不销毁 Session、不旅行或更改 Run。
void ACatfishingPlayerController::ServerAcknowledgeHostExit_Implementation(const FGuid RequestId)
{
	if (ACatfishingGameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr)
	{
		GameMode->AcknowledgeHostExitClient(this, RequestId);
	}
}

// 公开图鉴刷新客户端流程：从当前 LocalPlayer durable Profile 只读取 FishCollection；读取成功才提交服务器，绝不附带相册或 Journal。
void ACatfishingPlayerController::ClientRefreshPublicFishCollection_Implementation()
{
	TArray<FCatFishCollectionRecord> Records;
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UCatProfileSubsystem* Profile = LocalPlayer ? LocalPlayer->GetSubsystem<UCatProfileSubsystem>() : nullptr;
	if (Profile && Profile->GetFishCollectionSnapshot(Records))
	{
		ServerPublishPublicFishCollection(Records);
	}
}

// 公开图鉴发布服务器流程：只允许当前 Controller 自己的项目 PlayerState 接收，并让 PlayerState 完整校验后整体复制。
void ACatfishingPlayerController::ServerPublishPublicFishCollection_Implementation(const TArray<FCatFishCollectionRecord>& Records)
{
	if (ACatfishingPlayerState* CatPlayerState = GetPlayerState<ACatfishingPlayerState>())
	{
		CatPlayerState->SetPublicFishCollectionFromAuthority(Records);
	}
}
