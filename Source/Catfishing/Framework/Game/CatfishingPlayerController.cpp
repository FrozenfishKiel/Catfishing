#include "Framework/Game/CatfishingPlayerController.h"
#include "Logging/CatLogContext.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/Presentation/CatFishingCameraComponent.h"

#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "Camp/CatCampHubActor.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
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
#include "Net/UnrealNetwork.h"
#include "Profile/CatProfileSubsystem.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "ShopEconomy/Trading/CatShopTradeController.h"
#include "Social/CatSocialService.h"
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

// Pawn 写入流程：先保留 PlayerController 引擎内部的 SetPawn 行为，再按最终 Pawn 刷新 Ability 输入路由，最后让 owning client 的 LocalPlayer UI 消费当前身体。
void ACatfishingPlayerController::SetPawn(APawn* InPawn)
{
	if (GetPawn() != InPawn) ClearPhysicalControlInput(TEXT("PawnChanged"));
	Super::SetPawn(InPawn);
	if (AbilityInputBindingComponent)
	{
		AbilityInputBindingComponent->RefreshForPawn(InPawn);
	}
	NotifyLocalPlayerUISubsystemPawnChanged();
}

// 本地启动流程：父类完成 Actor 生命周期后，幂等安装本 Controller 的玩法输入层；
// 如果本机 durable Profile 已可读，再把装备解锁摘要投影给服务器 PlayerState，缺失时保持服务器 fail-closed。
void ACatfishingPlayerController::BeginPlay()
{
	Super::BeginPlay();
	ApplyInputMappingContext();
	PublishProfileEquipmentUnlocksIfAvailable();
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

// 输入清理流程：先恢复持竿转向和本 Controller 持有的输入/钓鱼临时状态，再成对撤销 Mapping Context；最后交还父类销毁，不清理其他本地输入层。
void ACatfishingPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearPhysicalControlInput(TEXT("ControllerEndPlay"));
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


// 移动输入流程：以当前可见水平朝向为基准，Y 驱动前后、X 驱动左右；Pawn 缺失时不制造旁路移动状态。
void ACatfishingPlayerController::Move(const FInputActionValue& Value)
{
	if (IsMoveInputIgnored()) { StopMove(); return; }
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

void ACatfishingPlayerController::StopMove()
{
	if (const ACatCharacter* Cat = Cast<ACatCharacter>(GetPawn()))
	{
		if (UCatPhysicalBodyComponent* Body = Cat->GetPhysicalBodyComponent()) Body->SetMoveIntent(FVector::ZeroVector);
	}
}

void ACatfishingPlayerController::ClearPhysicalControlInput(const FName Reason)
{
	if (FishingCommandComponent && GetPawn()) FishingCommandComponent->ClearHeldInputForLifecycle(Reason);
	if (AbilityInputBindingComponent) AbilityInputBindingComponent->ReleaseAllInputRoutes(Reason);
	if (const ACatCharacter* Cat = Cast<ACatCharacter>(GetPawn()))
	{
		if (UCatPhysicalBodyComponent* Body = Cat->GetPhysicalBodyComponent()) Body->ClearControlIntent(Reason);
	}
	SetSprintRequested(false, true);
}

void ACatfishingPlayerController::FlushPressedKeys()
{
	ClearPhysicalControlInput(TEXT("KeysFlushed"));
	Super::FlushPressedKeys();
}

// 视角输入流程：输入资产只提供二维意图，轴反转、缩放和死区由 Mapping Context 的 Modifier 决定。
void ACatfishingPlayerController::Look(const FInputActionValue& Value)
{
	const FVector2D LookAxis = Value.Get<FVector2D>();
	AddYawInput(LookAxis.X);
	AddPitchInput(LookAxis.Y);
}

// 跳跃按下流程：只对当前已占有的 Character 生效；持竿操作时清掉跳跃保持态并拒绝起跳，普通 Pawn 不伪造跳跃实现。
void ACatfishingPlayerController::StartJump()
{
	if (IsMoveInputIgnored()) return;
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

// 疾跑按下流程：本地立即应用以保持操控响应，同时仅向 authority 发送布尔意图，客户端不能提交任意速度。
void ACatfishingPlayerController::StartSprint()
{
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

// 移动速度应用流程：物理电机使用服务器配置的 cm/s；CMC 保留同值供正式动画/通用只读消费者。
void ACatfishingPlayerController::ApplySprintSpeed(APawn* TargetPawn, const bool bSprinting) const
{
	const ACharacter* DefaultCharacter = TargetPawn ? Cast<ACharacter>(TargetPawn->GetClass()->GetDefaultObject()) : nullptr;
	const UCharacterMovementComponent* DefaultMovement = DefaultCharacter ? DefaultCharacter->GetCharacterMovement() : nullptr;
	// 普通速度只读当前猫类 CDO 的正式 CMC 配置，不能用已被疾跑临时覆盖的实例值当基准。
	const float WalkSpeed = DefaultMovement ? DefaultMovement->MaxWalkSpeed : 100.0f;
	const float Speed = FMath::Max(0.0f, bSprinting ? SprintMaxSpeed : WalkSpeed);
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
}

// 持竿面对模式应用流程：
// 1. 首次接管或持竿 Pawn 切换时，先恢复上一对象，再保存新 Character/Movement 的普通转向配置。
// 2. 持竿期间每帧清掉跳跃保持态，避免进入持竿前的按键让角色在搏斗或操作杆时起跳。
// 3. 最后让身体只跟随当前钓鱼方向 yaw；控制器仍保留鼠标施力意图，第一人称镜头可继续读实际鱼竿姿态。


// 持竿面对模式恢复流程：
// 1. 只在本 Controller 曾经接管过转向配置时写回，避免普通状态每帧碰 CharacterMovement。
// 2. 弱引用仍有效时恢复进入前的三个转向开关；对象已销毁时只清缓存，让新 Pawn 使用自己的默认值。
// 3. 恢复同时清掉跳跃保持态，避免离竿同帧把持竿期间的输入带回普通移动。


// authority 疾跑流程：客户端只能选择开关，服务器使用自身类默认速度重新应用并参与权威移动校验。
void ACatfishingPlayerController::ServerSetSprinting_Implementation(const bool bNewSprinting)
{
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

// 公共领域结果客户端流程：可靠接收 Camp、容器移动和库存物品使用等结果并整体替换本机读模型；随后广播本机通知供 UI Model 刷新，不解释错误，也不触发新的领域命令。
void ACatfishingPlayerController::ClientReceiveCampCommandResult_Implementation(
	const FCatDomainCommandResult& Result)
{
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
	if (!CanForwardGameplayCommand() || !RequestId.IsValid() || !IsValid(Target)
		|| Target->GetWorld() != GetWorld()
		|| !Target->GetClass()->ImplementsInterface(UCatInteractable::StaticClass())
		|| !ICatInteractable::Execute_CanInteract(Target, this))
	{
		return;
	}
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

void ACatfishingPlayerController::ServerSellFish_Implementation(const FGuid FishItemInstanceId,
	AActor* SourceInventoryHost, const int32 SourceInventorySlotIndex, const FGuid RequestId,
	const int64 ExpectedWalletRevision)
{
	// 售鱼 RPC 流程：Controller 只取当前 World 的商店交易控制器并转交库存宿主和槽位。
	// 依赖缺失时这里静默返回，保持 RPC 缺依赖不改状态；真正的鱼移除、估价、入公款和账本幂等都在 ShopTradeController 中完成。
	UCatShopTradeController* Controller =
		GetWorld() ? GetWorld()->GetSubsystem<UCatShopTradeController>() : nullptr;
	if (!Controller)
	{
		return;
	}
	Controller->SubmitFishSaleFromPlayer(this, FishItemInstanceId, SourceInventoryHost,
		SourceInventorySlotIndex, RequestId, ExpectedWalletRevision);
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
// 1. 只处理项目约定的交互标签，其他 Native 标签保持无副作用返回。
// 2. IA_Interact 只进入 PlayerController 持有的唯一 TargetingComponent；提示 UI 只展示当前目标。
// 3. TargetingComponent 对当前 Actor 调用 ICatInteractable，商店、营地公共仓库、鱼护、鱼缸和死鱼各自在 Actor 实现中处理。
void ACatfishingPlayerController::NativeInputTagPressed(const FGameplayTag InputTag)
{
	if (!InputTag.MatchesTagExact(CatInteractionTags::Input_Interact))
	{
		return;
	}
	if (InteractionTargetingComponent)
	{
		InteractionTargetingComponent->TryInteract();
	}
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
