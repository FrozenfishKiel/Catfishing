#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingGameState.h"
#include "Components/InputComponent.h"

#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "Camp/CatCampHubActor.h"
#include "FishContainers/CatFishGuardActor.h"
#include "ShopEconomy/CatFishBuyerActor.h"
#include "Character/CatCharacter.h"
#include "AbilitySystem/Config/CatAbilityInputConfig.h"
#include "AbilitySystem/Config/CatAbilitySettings.h"
#include "AbilitySystem/BodyAction/Camp/CatCampBodyActionCommandComponent.h"
#include "AbilitySystem/BodyAction/Social/CatSocialBodyActionCommandComponent.h"
#include "AbilitySystem/Input/CatAbilityInputBindingComponent.h"
#include "Logging/CatLog.h"
#include "Online/CatOnlineSubsystem.h"
#include "Condition/CatConditionComponent.h"
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
#include "InputMappingContext.h"
#include "Interaction/CatInteractable.h"
#include "Interaction/CatInteractionTags.h"
#include "Interaction/CatInteractionTargetingComponent.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/CatInventoryStatics.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "Net/UnrealNetwork.h"
#include "Profile/CatProfileSubsystem.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "ShopEconomy/Trading/CatShopTradeController.h"
#include "Social/CatSocialService.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Fishing/Presentation/CatFishingCameraComponent.h"

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
// 3. 仅 owning client 把服务器秒数和快照交给独立 UI；这里不提交供品、不推进 Run、不开始新天倒计时。
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
		}
	}
}

// 锁配对流程：
// 1. 服务器先归还已换走或已结束的移动组件；只有组件仍处于本功能写入的 None 模式才恢复原模式和自定义编号。
// 2. 新组件保存原模式后停止并禁用移动；客户端只用输入锁，绝不写 CharacterMovement 模式。
// 3. 首次加锁申请一层移动/视角忽略计数，清跳跃和疾跑，并为本地输入压入专属阻断组件。
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
		Transition ? *Transition->RequestId.ToString() : TEXT("None"), bLocked,
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
		}
	}
}

// 旅行流程：先记住将离开的 World，清理翻天订阅和锁，再交给父类广播旅行；后续旧世界帧不能重新创建遮罩。
void ACatfishingPlayerController::PreClientTravel(const FString& PendingURL, const ETravelType TravelType, const bool bIsSeamlessTravel)
{
	DayTransitionTravelWorld = GetWorld();
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

// Pawn 写入流程：换身体前归还旧身体的翻天锁；父类写入后刷新 Ability 路由和本地 UI，再按当前服务器快照锁定新身体。
void ACatfishingPlayerController::SetPawn(APawn* InPawn)
{
	if (GetPawn() != InPawn) SetDayTransitionLocked(false);
	Super::SetPawn(InPawn);
	if (AbilityInputBindingComponent)
	{
		AbilityInputBindingComponent->RefreshForPawn(InPawn);
	}
	NotifyLocalPlayerUISubsystemPawnChanged();
	ReconcileDayTransition();
}

// 本地启动流程：父类完成 Actor 生命周期后，幂等安装本 Controller 的玩法输入层；
// 如果本机 durable Profile 已可读，再把装备解锁摘要投影给服务器；最后消费翻天快照，缺失时由 Tick 补齐。
void ACatfishingPlayerController::BeginPlay()
{
	Super::BeginPlay();
	ApplyInputMappingContext();
	PublishProfileEquipmentUnlocksIfAvailable();
	ReconcileDayTransition();
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

// Pawn 断开流程：先恢复持竿期间接管过的转向配置，再清 ASC 输入、钓鱼本地状态和疾跑意图；最后恢复普通速度并交还父类断开占有。
void ACatfishingPlayerController::OnUnPossess()
{
	RestoreHeldRodFacingMode();
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

// 输入清理流程：先解绑翻天并归还专属锁，再恢复持竿转向和输入/钓鱼状态，撤销自己的 Mapping Context，最后交还父类销毁。
void ACatfishingPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 父类结束流程可能再次写 Pawn；禁止该回调重新订阅即将销毁的 World。
	DayTransitionTravelWorld = GetWorld();
	ClearDayTransition();
	RestoreHeldRodFacingMode();
	if (AbilityInputBindingComponent)
	{
		AbilityInputBindingComponent->ResetAbilityInput();
	}
	NativeInputBoundComponent.Reset();
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

// Profile 解锁发布流程：只在 owning client 有 LocalPlayer Profile 时复制 UnlockIds 并走服务器 RPC；Profile 不可用时保持服务器 fail-closed 授权。
void ACatfishingPlayerController::PublishProfileEquipmentUnlocksIfAvailable()
{
	if (!IsLocalController())
	{
		return;
	}
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UCatProfileSubsystem* Profile = LocalPlayer ? LocalPlayer->GetSubsystem<UCatProfileSubsystem>() : nullptr;
	TArray<FName> UnlockIds;
	if (Profile && Profile->GetEquipmentUnlockSnapshot(UnlockIds))
	{
		ServerPublishEquipmentUnlocks(UnlockIds);
	}
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

// 持竿旋转同步流程：
// 1. 先让 PlayerController 消化本帧视角输入，保留普通相机、网络和输入收尾行为。
// 2. 再从 Fishing/Rod 的现有查询入口判断当前 Pawn 是否正在操作世界鱼竿；没有持竿就恢复进入前的移动配置。
// 3. 持竿时不新增玩法状态，只接管 Character/Movement 的转向开关，并把身体 yaw 写到可见钓鱼方向。
void ACatfishingPlayerController::UpdateRotation(const float DeltaTime)
{
	Super::UpdateRotation(DeltaTime);

	ACatCharacter* ControlledCat = Cast<ACatCharacter>(GetPawn());
	UCharacterMovementComponent* Movement = ControlledCat ? ControlledCat->GetCharacterMovement() : nullptr;
	if (!ControlledCat || !Movement || !UCatFishingCameraComponent::FindHeldRodOperatedBy(this))
	{
		RestoreHeldRodFacingMode();
		return;
	}

	ApplyHeldRodFacingMode(*ControlledCat, *Movement, UCatFishingCameraComponent::ResolveFacingRotation(this));
}

// 移动输入流程：先拒绝翻天操作；其余以可见水平朝向转换前后左右输入，Pawn 缺失时不制造旁路状态。
void ACatfishingPlayerController::Move(const FInputActionValue& Value)
{
	if (IsDayTransitionInputBlocked()) return;
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
	ControlledPawn->AddMovementInput(ForwardDirection, Movement.Y);
	ControlledPawn->AddMovementInput(RightDirection, Movement.X);
}

// 视角输入流程：先拒绝翻天操作，再将经过 Mapping Context 反转、缩放和死区处理的二维意图写入视角。
void ACatfishingPlayerController::Look(const FInputActionValue& Value)
{
	if (IsDayTransitionInputBlocked()) return;
	const FVector2D LookAxis = Value.Get<FVector2D>();
	AddYawInput(LookAxis.X);
	AddPitchInput(LookAxis.Y);
}

// 跳跃按下流程：先拒绝翻天操作；只对当前 Character 生效，持竿时清保持态并拒绝起跳，普通 Pawn 不伪造实现。
void ACatfishingPlayerController::StartJump()
{
	if (IsDayTransitionInputBlocked()) return;
	if (ACharacter* ControlledCharacter = Cast<ACharacter>(GetPawn()))
	{
		if (UCatFishingCameraComponent::FindHeldRodOperatedBy(this))
		{
			ControlledCharacter->StopJumping();
			return;
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

	if (bStateChanged && bNotifyServer && !HasAuthority())
	{
		ServerSetSprinting(bSprintRequested);
	}
}

// 移动速度应用流程：只修改当前 CharacterMovement 的 MaxWalkSpeed；实际速度仍由移动组件加速度、制动和网络移动决定。
void ACatfishingPlayerController::ApplySprintSpeed(APawn* TargetPawn, const bool bSprinting) const
{
	ACharacter* ControlledCharacter = Cast<ACharacter>(TargetPawn);
	UCharacterMovementComponent* MovementComponent = ControlledCharacter
		? ControlledCharacter->GetCharacterMovement() : nullptr;
	if (!MovementComponent)
	{
		return;
	}

	MovementComponent->MaxWalkSpeed = FMath::Max(0.0f, bSprinting ? SprintMaxSpeed : WalkMaxSpeed);
}

// 持竿面对模式应用流程：
// 1. 首次接管或持竿 Pawn 切换时，先恢复上一对象，再保存新 Character/Movement 的普通转向配置。
// 2. 持竿期间每帧清掉跳跃保持态，避免进入持竿前的按键让角色在搏斗或操作杆时起跳。
// 3. 最后让身体只跟随当前钓鱼方向 yaw；控制器仍保留鼠标施力意图，第一人称镜头可继续读实际鱼竿姿态。
void ACatfishingPlayerController::ApplyHeldRodFacingMode(ACatCharacter& ControlledCat,
	UCharacterMovementComponent& Movement, const FRotator& FacingRotation)
{
	if (!bHeldRodFacingModeActive || HeldRodFacingCharacter.Get() != &ControlledCat
		|| HeldRodFacingMovement.Get() != &Movement)
	{
		RestoreHeldRodFacingMode();
		bSavedHeldRodUseControllerRotationYaw = ControlledCat.bUseControllerRotationYaw;
		bSavedHeldRodOrientRotationToMovement = Movement.bOrientRotationToMovement;
		bSavedHeldRodUseControllerDesiredRotation = Movement.bUseControllerDesiredRotation;
		HeldRodFacingCharacter = &ControlledCat;
		HeldRodFacingMovement = &Movement;
		bHeldRodFacingModeActive = true;
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_holder_facing_mode_changed State=Applied Controller=%s Pawn=%s SavedUseControllerYaw=%s SavedOrientToMovement=%s SavedDesiredRotation=%s"),
			*GetNameSafe(this), *GetNameSafe(&ControlledCat),
			bSavedHeldRodUseControllerRotationYaw ? TEXT("true") : TEXT("false"),
			bSavedHeldRodOrientRotationToMovement ? TEXT("true") : TEXT("false"),
			bSavedHeldRodUseControllerDesiredRotation ? TEXT("true") : TEXT("false"));
	}

	ControlledCat.StopJumping();
	ControlledCat.bPressedJump = false;
	ControlledCat.bUseControllerRotationYaw = true;
	Movement.bOrientRotationToMovement = false;
	Movement.bUseControllerDesiredRotation = false;
	ControlledCat.SetActorRotation(FRotator(0.0, FacingRotation.Yaw, 0.0));
}

// 持竿面对模式恢复流程：
// 1. 只在本 Controller 曾经接管过转向配置时写回，避免普通状态每帧碰 CharacterMovement。
// 2. 弱引用仍有效时恢复进入前的三个转向开关；对象已销毁时只清缓存，让新 Pawn 使用自己的默认值。
// 3. 恢复同时清掉跳跃保持态，避免离竿同帧把持竿期间的输入带回普通移动。
void ACatfishingPlayerController::RestoreHeldRodFacingMode()
{
	if (!bHeldRodFacingModeActive)
	{
		return;
	}

	if (ACatCharacter* ControlledCat = HeldRodFacingCharacter.Get())
	{
		ControlledCat->bUseControllerRotationYaw = bSavedHeldRodUseControllerRotationYaw;
		ControlledCat->StopJumping();
		ControlledCat->bPressedJump = false;
	}
	if (UCharacterMovementComponent* Movement = HeldRodFacingMovement.Get())
	{
		Movement->bOrientRotationToMovement = bSavedHeldRodOrientRotationToMovement;
		Movement->bUseControllerDesiredRotation = bSavedHeldRodUseControllerDesiredRotation;
	}
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_holder_facing_mode_changed State=Restored Controller=%s Pawn=%s RestoredUseControllerYaw=%s RestoredOrientToMovement=%s RestoredDesiredRotation=%s"),
		*GetNameSafe(this), *GetNameSafe(HeldRodFacingCharacter.Get()),
		bSavedHeldRodUseControllerRotationYaw ? TEXT("true") : TEXT("false"),
		bSavedHeldRodOrientRotationToMovement ? TEXT("true") : TEXT("false"),
		bSavedHeldRodUseControllerDesiredRotation ? TEXT("true") : TEXT("false"));
	bHeldRodFacingModeActive = false;
	HeldRodFacingCharacter.Reset();
	HeldRodFacingMovement.Reset();
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
	return GameMode && GameMode->CanAcceptGameplayCommand(this);
}

// Controller 钓鱼 gate 流程：现取 authority GameMode 并使用 Fishing 专用白天规则；它只服务抛竿、鱼竿操作、协作、抢抄和玩家打窝，不影响 Social 或结算 RPC。
bool ACatfishingPlayerController::CanForwardFishingCommand() const
{
	const ACatfishingGameModeBase* GameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	return GameMode && GameMode->CanAcceptFishingCommand(this);
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
		if (Grant.Kind == ECatProfileGrantKind::Unlock)
		{
			PublishProfileEquipmentUnlocksIfAvailable();
		}
		if (Grant.Kind == ECatProfileGrantKind::FishRecorded || Grant.Kind == ECatProfileGrantKind::FishSilhouette)
		{
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
				if (ACatfishingPlayerState* CatPlayerState = GetPlayerState<ACatfishingPlayerState>())
				{
					CatPlayerState->AuthorizeEquipmentUnlockFromProfileGrant(AcknowledgedGrant);
				}
			}
			if (ACatfishingGameModeBase* GameMode = GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>())
			{
				GameMode->NotifyHostExitGrantAckProgress();
			}
		}
	}
}

// 装备解锁摘要 RPC 流程：服务器只把 owning client 提交的 durable Profile 摘要写到当前 PlayerState；非法摘要保留原授权，不回写 Profile 或生成 Grant。
void ACatfishingPlayerController::ServerPublishEquipmentUnlocks_Implementation(const TArray<FName>& UnlockIds)
{
	if (ACatfishingPlayerState* CatPlayerState = GetPlayerState<ACatfishingPlayerState>())
	{
		CatPlayerState->SetAuthorizedEquipmentUnlocksFromAuthority(UnlockIds);
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

// 营地休息 RPC 路由流程：只把固定营地和 RequestId 投给 BodyAction Ability；没有正式 Ability 接管时回送依赖错误。
void ACatfishingPlayerController::ServerRequestCampRest_Implementation(ACatCampHubActor* Camp, const FGuid RequestId)
{
	if (!CampBodyActionCommandComponent || !CampBodyActionCommandComponent->SubmitCampRest(Camp, RequestId))
	{
		FCatDomainCommandResult Result;
		Result.RequestId = RequestId;
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		DeliverCampCommandResultToOwningClient(Result);
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

// 搬运救援 RPC 路由流程：只把目标和营地投给 BodyAction Ability；没有正式 Ability 接管时回送依赖错误。
void ACatfishingPlayerController::ServerRescueCharacterToCamp_Implementation(ACatCampHubActor* Camp,
	ACatCharacter* TargetCharacter, const FGuid RequestId)
{
	if (!CampBodyActionCommandComponent
		|| !CampBodyActionCommandComponent->SubmitRescueCharacterToCamp(Camp, TargetCharacter, RequestId))
	{
		FCatDomainCommandResult Result;
		Result.RequestId = RequestId;
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		DeliverCampCommandResultToOwningClient(Result);
	}
}

// 公共领域结果客户端流程：可靠接收结果后按请求落盘并整体替换本机读模型，再广播给 UI；未开界面也保留接收证据，不触发新的领域命令。
void ACatfishingPlayerController::ClientReceiveCampCommandResult_Implementation(
	const FCatDomainCommandResult& Result)
{
	const FString Event = FString::Printf(
		TEXT("Event=inventory_command_received RequestId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Player=%s Committed=%d Replay=%d Error=%s"),
		*Result.RequestId.ToString(), *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole(), *GetName(),
		Result.bCommitted, Result.bTerminalReplay, *UEnum::GetValueAsString(Result.Error));
	if (CatIsAcceptedDomainCommandResult(Result)) { UE_LOG(LogCatfishing, Log, TEXT("%s"), *Event); }
	else { UE_LOG(LogCatfishing, Warning, TEXT("%s"), *Event); }
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

// 通用库存移动 RPC 路由流程：Controller 先执行玩法命令 gate，再把宿主和槽位交给 Actor 级库存入口；正式移动由 Source InventoryComponent 裁决。
void ACatfishingPlayerController::ServerMoveInventoryItemBetweenHosts_Implementation(
	const FGuid RequestId, AActor* SourceInventoryHost, const int32 SourceSlotIndex,
	AActor* TargetInventoryHost, const int32 TargetSlotIndex)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	if (!CanForwardGameplayCommand())
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=move_inventory_item_between_hosts_rejected Reason=CommandsClosedOrInactive Request=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
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

// 随身库存物品使用 RPC 路由流程：
// 1. Controller 先执行玩法命令 gate、RequestId、Pawn 和库存组件校验，失败只回结构化错误。
// 2. 通过后构造正式库存 Use 上下文；物品定义、数量扣减和钓具读模型刷新都留在 InventoryComponent/ItemInstance 内部。
// 3. 最后统一回送 owning client，UI 只显示服务器确认后的终态。
void ACatfishingPlayerController::ServerUseInventoryItem_Implementation(
	const FGuid RequestId, const int32 InventorySlotIndex)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	UCatInventoryComponent* Inventory = ControlledCharacter ? ControlledCharacter->GetInventoryComponent() : nullptr;
	if (!CanForwardGameplayCommand())
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=use_inventory_item_rejected Reason=CommandsClosedOrInactive Request=%s Slot=%d"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens), InventorySlotIndex);
	}
	else if (!RequestId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=use_inventory_item_rejected Reason=InvalidRequest Request=%s Slot=%d"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens), InventorySlotIndex);
	}
	else if (!ControlledCharacter || ControlledCharacter->GetWorld() != GetWorld() || !Inventory)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=use_inventory_item_rejected Reason=MissingInventory Request=%s Slot=%d Character=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens), InventorySlotIndex,
			*GetNameSafe(ControlledCharacter));
	}
	else
	{
		FCatInventoryItemUseContext UseContext;
		UseContext.RequestId = RequestId;
		UseContext.RequestingController = this;
		UseContext.UserPawn = ControlledCharacter;
		UseContext.SourceInventory = Inventory;
		UseContext.InventorySlotIndex = InventorySlotIndex;
		Result = Inventory->UseItemAtSlotFromAuthority(UseContext);
	}
	UE_LOG(LogCatfishing, Log,
		TEXT("Event=use_inventory_item Committed=%s Error=%s Slot=%d"),
		Result.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Error),
		InventorySlotIndex);
	DeliverCampCommandResultToOwningClient(Result);
}

// 指定宿主库存物品使用 RPC 流程：
// 1. Controller 先执行玩法命令 gate、RequestId 和 Pawn 校验，失败只回结构化错误。
// 2. 通过后把来源 Actor 和槽位交给 InventoryStatics，服务器重新解析可触达正式 InventoryComponent。
// 3. 物品实际效果、数量扣减和失败回滚都由 InventoryComponent/ItemInstance 完成，Controller 不再拆鱼容器或物品类别。
void ACatfishingPlayerController::ServerUseInventoryItemFromHost_Implementation(
	const FGuid RequestId, AActor* SourceInventoryHost, const int32 InventorySlotIndex)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	if (!CanForwardGameplayCommand())
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=use_inventory_item_from_host_rejected Reason=CommandsClosedOrInactive Request=%s Slot=%d Host=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens), InventorySlotIndex,
			*GetNameSafe(SourceInventoryHost));
	}
	else if (!RequestId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=use_inventory_item_from_host_rejected Reason=InvalidRequest Request=%s Slot=%d Host=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens), InventorySlotIndex,
			*GetNameSafe(SourceInventoryHost));
	}
	else if (!ControlledCharacter || ControlledCharacter->GetWorld() != GetWorld())
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=use_inventory_item_from_host_rejected Reason=MissingCharacter Request=%s Slot=%d Character=%s Host=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens), InventorySlotIndex,
			*GetNameSafe(ControlledCharacter), *GetNameSafe(SourceInventoryHost));
	}
	else
	{
		Result = UCatInventoryStatics::UseItemFromInventoryHostFromAuthority(ControlledCharacter, RequestId,
			SourceInventoryHost, InventorySlotIndex);
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
		&& ICatInteractable::Execute_CanInteract(Target, this);
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
	const TArray<FCatShopCartLineCommand>& Lines, const FGuid RequestId, const int64 ExpectedWalletRevision)
{
	FCatDomainCommandResult DeliveryResult;
	DeliveryResult.RequestId = RequestId;
	if (UCatShopTradeController* Controller = GetWorld()
		? GetWorld()->GetSubsystem<UCatShopTradeController>() : nullptr)
	{
		DeliveryResult = Controller->SubmitCartFromKiosk(this, ShopKiosk, Lines, RequestId,
			ExpectedWalletRevision).Delivery;
	}
	else
	{
		DeliveryResult.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	DeliveryResult.RequestId = RequestId;
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

// 落地路由流程：先记录来源和意图，拒绝关闭阶段与错误载荷，其余交库存校验提交；结果按原请求落盘并回送，客户端不决定落点。
void ACatfishingPlayerController::ServerReleaseInventoryItemToWorld_Implementation(const FGuid RequestId,
	AActor* SourceHost, const int32 Slot, const FGuid ItemInstanceId, const int32 Quantity,
	const ECatInventoryWorldAction Action)
{
	UE_LOG(LogCatfishing, Log,
		TEXT("Event=inventory_world_requested RequestId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Player=%s Source=%s Slot=%d ItemInstanceId=%s Quantity=%d Action=%d"),
		*RequestId.ToString(), *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole(), *GetName(),
		*GetNameSafe(SourceHost), Slot, *ItemInstanceId.ToString(), Quantity, static_cast<int32>(Action));
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (!CanForwardGameplayCommand()) Result.Error = ECatDomainCommandError::CommandsClosed;
	else if (!RequestId.IsValid() || !ItemInstanceId.IsValid() || Quantity <= 0
		|| (Action != ECatInventoryWorldAction::Drop && Action != ECatInventoryWorldAction::Place))
		Result.Error = ECatDomainCommandError::InvalidPayload;
	else if (ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn()))
		Result = UCatInventoryStatics::ReleaseItemToWorldFromAuthority(ControlledCharacter, RequestId,
			SourceHost, Slot, ItemInstanceId, Quantity, Action);
	else Result.Error = ECatDomainCommandError::DependencyUnavailable;
	const FString Event = FString::Printf(
		TEXT("Event=inventory_world_result RequestId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Player=%s Source=%s Action=%d Committed=%d Replay=%d Error=%s"),
		*RequestId.ToString(), *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole(), *GetName(),
		*GetNameSafe(SourceHost), static_cast<int32>(Action), Result.bCommitted, Result.bTerminalReplay, *UEnum::GetValueAsString(Result.Error));
	if (CatIsAcceptedDomainCommandResult(Result)) { UE_LOG(LogCatfishing, Log, TEXT("%s"), *Event); }
	else { UE_LOG(LogCatfishing, Warning, TEXT("%s"), *Event); }
	DeliverCampCommandResultToOwningClient(Result);
}

// 快捷丢弃流程：服务器检查玩法门和身体，再读取当前携带对象；空嘴直接返回。
// 单鱼解除原Actor的携带，鱼护沿原库存Drop释放；不接收客户端目标、不保存快捷请求，也不改背包选中格。
void ACatfishingPlayerController::ServerDropCarriedItem_Implementation()
{
	ACatCharacter* CatCharacter = Cast<ACatCharacter>(GetPawn());
	if (!CanForwardGameplayCommand() || !CatCharacter || !CatCharacter->GetConditionComponent()
		|| CatCharacter->GetConditionComponent()->GetSnapshot().bDowned) return;
	ACatFishPickupActor* Fish = ACatFishPickupActor::FindCarriedFish(CatCharacter);
	ACatFishGuardActor* Guard = ACatFishGuardActor::FindCarriedGuard(CatCharacter);
	if (!IsValid(Fish) && !IsValid(Guard)) return;
	bool bDropped = false;
	if (IsValid(Fish))
	{
		bDropped = Fish->DropFromAuthority(this);
	}
	else if (UCatInventoryComponent* Inventory = CatCharacter->GetInventoryComponent())
	{
		for (const FCatInventoryEntry& Entry : Inventory->GetInventoryEntries())
		{
			if (Entry.Instance && Entry.StackCount == 1 && Entry.Instance->GetWorldActor() == Guard)
			{
				// 这里只满足原库存接口的事务参数，不为快捷入口另建请求状态。
				bDropped = UCatInventoryStatics::ReleaseItemToWorldFromAuthority(CatCharacter, FGuid::NewGuid(), CatCharacter,
					Inventory->FindInventorySlotIndexFromInstance(Entry.Instance), Entry.Instance->GetItemInstanceId(),
					1, ECatInventoryWorldAction::Drop).bCommitted;
				break;
			}
		}
	}
	const FString Event = FString::Printf(
		TEXT("Event=mouth_drop_result World=%s NetMode=%d Authority=%d LocalRole=%d Player=%s ItemActor=%s Dropped=%d"),
		*GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole(), *GetName(),
		*GetNameSafe(Fish ? static_cast<AActor*>(Fish) : static_cast<AActor*>(Guard)), bDropped);
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

// 草药 RPC 路由流程：Controller 只定位目标 Character 的 ConditionComponent 并转交请求；正式扣草药、刷新装备读模型和恢复身体都由 ConditionComponent 按服务器事实提交。
void ACatfishingPlayerController::ServerUseHerbOnCharacter_Implementation(ACatCharacter* TargetCharacter,
	const FGuid RequestId, const FGuid HerbItemInstanceId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	UCatConditionComponent* TargetConditions = TargetCharacter && TargetCharacter->GetWorld() == GetWorld()
		? TargetCharacter->GetConditionComponent() : nullptr;
	if (TargetConditions)
	{
		Result = TargetConditions->UseHerbOnCharacterFromAuthority(this, RequestId, HerbItemInstanceId);
	}
	else
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
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

// 偷鱼开始 RPC 路由流程：偷鱼是 Social 持有同一鱼物品实例的短协议，不进入 BodyAction；Controller 只清客户端身份并转交 Social。
void ACatfishingPlayerController::ServerBeginTheft_Implementation(FCatTheftCommand Command)
{
	FCatTheftResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	if (!CanForwardGameplayCommand())
	{
		Result.Command.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (UCatSocialService* Social = GetWorld() ? GetWorld()->GetSubsystem<UCatSocialService>() : nullptr)
	{
		Command.Context.StableNetId.Reset();
		Result = Social->BeginTheft(this, Command);
	}
	else
	{
		Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	ClientReceiveTheftResult(Result);
}

// 偷鱼结果客户端流程：可靠接收服务器完整终态并整体替换本机读模型；ProtocolId 和身体终态由此到达 UI，客户端不能据缓存修改权威事实。
void ACatfishingPlayerController::ClientReceiveTheftResult_Implementation(const FCatTheftResult& Result)
{
	LastTheftResult = Result;
}

// 偷鱼结果读取流程：返回最近一次 Begin/Catch/到期消费的本机副本供界面取得 ProtocolId、阶段和身体终态；服务器授权仍重读当前事实。
FCatTheftResult ACatfishingPlayerController::GetLastTheftResult() const
{
	return LastTheftResult;
}

// 偷鱼追回 RPC 路由流程：追回只按服务器 ProtocolId 进入 Social，不走 BodyAction，也不接受客户端重建 escrow。
void ACatfishingPlayerController::ServerCatchTheft_Implementation(const FGuid TheftProtocolId)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	if (UCatSocialService* Social = GetWorld() ? GetWorld()->GetSubsystem<UCatSocialService>() : nullptr)
	{
		ClientReceiveTheftResult(Social->CatchTheft(this, TheftProtocolId));
	}
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
// 1. 先拒绝翻天操作；快捷丢弃还检查本地输入锁，菜单打开时不丢物，空嘴也不发请求。
// 2. 丢弃先查当前嘴部，取消尚未完成的交互长按，再通知服务器读取携带对象；Started 绑定避免按住重复触发。
// 3. 其余仅处理 IA_Interact，由唯一 TargetingComponent 把交互交给准星 Actor；不认识的标签无副作用返回。
void ACatfishingPlayerController::NativeInputTagPressed(const FGameplayTag InputTag)
{
	if (IsDayTransitionInputBlocked()) return;
	if (InputTag.MatchesTagExact(CatInteractionTags::Input_DropCarriedItem))
	{
		if (!IsLocalController() || IsMoveInputIgnored()) return;
		ACatCharacter* CatCharacter = Cast<ACatCharacter>(GetPawn());
		if (!CatCharacter) return;
		AActor* Item = ACatFishPickupActor::FindCarriedFish(CatCharacter);
		if (!Item) Item = ACatFishGuardActor::FindCarriedGuard(CatCharacter);
		if (!Item) return;
		if (InteractionTargetingComponent) InteractionTargetingComponent->EndInteractionInput(true);
		UE_LOG(LogCatfishing, Log,
			TEXT("Event=mouth_drop_submitted World=%s NetMode=%d Authority=%d LocalRole=%d Player=%s ItemActor=%s"),
			*GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole(), *GetName(), *GetNameSafe(Item));
		ServerDropCarriedItem();
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

// 松开流程：只处理交互标签；翻天锁已接管时取消候选，其他时候由目标组件决定是否属于短按。
void ACatfishingPlayerController::NativeInputTagReleased(const FGameplayTag InputTag)
{
	if (InputTag.MatchesTagExact(CatInteractionTags::Input_Interact) && InteractionTargetingComponent)
		InteractionTargetingComponent->EndInteractionInput(IsDayTransitionInputBlocked());
}

// 取消流程：输入层失效只释放计时器与候选，绝不提交短按或拾取命令。
void ACatfishingPlayerController::NativeInputTagCanceled(const FGameplayTag InputTag)
{
	if (InputTag.MatchesTagExact(CatInteractionTags::Input_Interact) && InteractionTargetingComponent)
		InteractionTargetingComponent->EndInteractionInput(true);
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
