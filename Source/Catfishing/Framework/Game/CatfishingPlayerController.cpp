#include "Framework/Game/CatfishingPlayerController.h"

#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "Character/CatCharacter.h"
#include "AbilitySystem/Config/CatAbilityInputConfig.h"
#include "AbilitySystem/Config/CatAbilitySettings.h"
#include "AbilitySystem/BodyAction/Camp/CatCampBodyActionCommandComponent.h"
#include "AbilitySystem/BodyAction/Social/CatSocialBodyActionCommandComponent.h"
#include "AbilitySystem/Input/CatAbilityInputBindingComponent.h"
#include "Logging/CatLog.h"
#include "Online/CatOnlineSubsystem.h"
#include "Condition/CatConditionComponent.h"
#include "Condition/CatHerbRecoveryCoordinator.h"
#include "Collection/CatRunImprintService.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Equipment/CatEquipmentCommandCoordinator.h"
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
#include "Items/CatContainerCommandCoordinator.h"
#include "Items/CatFishConsumptionCoordinator.h"
#include "Interaction/CatInteractable.h"
#include "Interaction/CatInteractionTags.h"
#include "Interaction/CatInteractionTargetingComponent.h"
#include "Inventory/CatInventoryCommandCoordinator.h"
#include "Net/UnrealNetwork.h"
#include "Profile/CatProfileSubsystem.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "Run/CatSacrificeCoordinator.h"
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
	// 接管流程：父类先完成 Pawn 所有权切换，并经 SetPawn 统一刷新 Ability 输入路由；随后只清 Controller 自己的临时钓鱼命令和疾跑状态。
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

// Pawn 复制刷新流程：父类复制收尾会经 SetPawn 切换 Ability 输入路由；这里只重置 Controller 本地临时输入和普通移动速度。
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

// 输入后处理流程：先保留父类每帧输入收尾，再把本帧 Delta/GamePaused 交给当前 Pawn 的 ASC；没有有效 ASC 时保持静默，不缓存旧 Pawn。
void ACatfishingPlayerController::PostProcessInput(const float DeltaTime, const bool bGamePaused)
{
	Super::PostProcessInput(DeltaTime, bGamePaused);
	if (AbilityInputBindingComponent)
	{
		AbilityInputBindingComponent->ProcessAbilityInput(DeltaTime, bGamePaused);
	}
}

// Pawn 断开流程：Controller 仍持有 Pawn 时先恢复普通速度并清意图，再交还父类断开占有。
void ACatfishingPlayerController::OnUnPossess()
{
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

// 输入清理流程：只撤销本 Controller 安装的 Context，再交还父类销毁；不干扰诊断或 UI 输入层。
void ACatfishingPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
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

// 移动输入流程：以控制器水平朝向为基准，Y 驱动前后、X 驱动左右；Pawn 缺失时不制造旁路移动状态。
void ACatfishingPlayerController::Move(const FInputActionValue& Value)
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return;
	}

	const FVector2D Movement = Value.Get<FVector2D>();
	const FRotator YawRotation(0.0, GetControlRotation().Yaw, 0.0);
	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
	ControlledPawn->AddMovementInput(ForwardDirection, Movement.Y);
	ControlledPawn->AddMovementInput(RightDirection, Movement.X);
}

// 视角输入流程：输入资产只提供二维意图，轴反转、缩放和死区由 Mapping Context 的 Modifier 决定。
void ACatfishingPlayerController::Look(const FInputActionValue& Value)
{
	const FVector2D LookAxis = Value.Get<FVector2D>();
	AddYawInput(LookAxis.X);
	AddPitchInput(LookAxis.Y);
}

// 跳跃按下流程：只对当前已占有的 Character 生效，普通 Pawn 不伪造跳跃实现。
void ACatfishingPlayerController::StartJump()
{
	if (ACharacter* ControlledCharacter = Cast<ACharacter>(GetPawn()))
	{
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

// Controller 钓鱼 gate 流程：现取 authority GameMode 并使用 Fishing 专用白天规则；它只服务抛竿、鱼竿操作、协作、抢抄和玩家打窝，不影响 Social、翻天 ready 或结算 RPC。
bool ACatfishingPlayerController::CanForwardFishingCommand() const
{
	const ACatfishingGameModeBase* GameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	return GameMode && GameMode->CanAcceptFishingCommand(this);
}

// 额度 RPC 流程：先过统一玩法 gate，再组装客户端意图；随后由 GameMode 重建身份并完成 Revision/幂等裁决，结果只写结构化日志。
void ACatfishingPlayerController::ServerSubmitQuotaContribution_Implementation(const FGuid RequestId,
	const int64 ExpectedRevision, const int32 Contribution)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	FCatQuotaContributionCommand Command;
	Command.Context.RequestId = RequestId;
	Command.Context.ExpectedRevision = ExpectedRevision;
	Command.Contribution = Contribution;
	ACatfishingGameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	const FCatRunCommandResult Result = GameMode
		? GameMode->SubmitQuotaContribution(this, Command)
		: FCatRunCommandResult();
	UE_LOG(LogCatRun, Log, TEXT("Event=quota_command_result RequestId=%s Committed=%s Error=%s Revision=%lld"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens), Result.bCommitted ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(Result.Error), Result.Revision);
}

// Ready RPC 流程：先过统一玩法 gate，再转发 RequestId/ExpectedRevision/意图布尔值；GameMode 决定资格、个人复制值和全员 StateTree 事件。
void ACatfishingPlayerController::ServerSetNextDayReady_Implementation(const FGuid RequestId,
	const int64 ExpectedRevision, const bool bReady)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	FCatNextDayReadyCommand Command;
	Command.Context.RequestId = RequestId;
	Command.Context.ExpectedRevision = ExpectedRevision;
	Command.bReady = bReady;
	ACatfishingGameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	const FCatRunCommandResult Result = GameMode
		? GameMode->SubmitNextDayReady(this, Command)
		: FCatRunCommandResult();
	UE_LOG(LogCatRun, Log, TEXT("Event=ready_command_result RequestId=%s Committed=%s Error=%s Revision=%lld"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens), Result.bCommitted ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(Result.Error), Result.Revision);
}

// 结算完成 RPC 流程：现取 authority GameMode/Imprint 服务并检查当前 Run 的计划终态与 Grant ACK；通过后才调用 Run 唯一协调入口，不让客户端布尔值直接结束结算夜。
void ACatfishingPlayerController::ServerRequestSettlementCompletion_Implementation(const FGuid RequestId,
	const int64 ExpectedRevision)
{
	ACatfishingGameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	UCatRunImprintService* Imprint = GetWorld() ? GetWorld()->GetSubsystem<UCatRunImprintService>() : nullptr;
	FCatRunCommandResult Result;
	Result.RequestId = RequestId;
	if (GameMode && Imprint && Imprint->IsSettlementArchiveReady(GameMode->GetRunPublicState().Phase.RunId))
	{
		Result = GameMode->CompleteSettlementFromCoordinator(RequestId, ExpectedRevision);
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

// 装备解锁摘要 RPC 流程：服务器只把 owning client 提交的 durable Profile 摘要写到当前 PlayerState；非法摘要保留旧授权，不回写 Profile 或生成 Grant。
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

// 搏斗协作 RPC 流程：先过钓鱼白天 gate，再转交会话键、幂等键与 ExpectedRevision；Session 继续验证 Giant 与 HookedFight。
void ACatfishingPlayerController::ServerAssistFishingSession_Implementation(const FGuid FishingSessionId,
	const FGuid RequestId, const int64 ExpectedRevision)
{
	if (!CanForwardFishingCommand())
	{
		return;
	}
	if (FishingCommandComponent)
	{
		FishingCommandComponent->ForwardLegacyAssist(FishingSessionId, RequestId, ExpectedRevision);
	}
}

// 抄网 RPC 流程：先过钓鱼白天 gate，再把客户端意图交给命令组件；后续由命令组件和 Session 裁决范围，并在成功时完成抄网结果。
void ACatfishingPlayerController::ServerRequestScoop_Implementation(const FGuid FishingSessionId,
	FCatScoopCommand Command)
{
	if (!CanForwardFishingCommand())
	{
		return;
	}
	if (FishingCommandComponent)
	{
		FishingCommandComponent->ForwardLegacyScoop(FishingSessionId, Command);
	}
}

// 献祭 RPC 路由流程：服务器 RPC 只做网络入口和回执转交；协议顺序、鱼预留和 Run apply 仍由 SacrificeCoordinator 独占，不进入 BodyAction。
void ACatfishingPlayerController::ServerRequestSacrifice_Implementation(FCatSacrificeCommand Command)
{
	FCatSacrificeResult Result;
	Result.RequestId = Command.Context.RequestId;
	if (!CanForwardGameplayCommand())
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (UCatSacrificeCoordinator* Coordinator = GetWorld()
		? GetWorld()->GetSubsystem<UCatSacrificeCoordinator>() : nullptr)
	{
		Command.Context.StableNetId.Reset();
		Result = Coordinator->RequestSacrifice(this, Command);
	}
	else
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	DeliverSacrificeResultToOwningClient(Result);
}

// 献祭结果客户端流程：可靠接收服务器协调器的完整阶段结果并整体替换本机读模型；随后广播本机通知供 UI Model 刷新，不参与任何服务器恢复或写入。
void ACatfishingPlayerController::ClientReceiveSacrificeResult_Implementation(const FCatSacrificeResult& Result)
{
	LastSacrificeResult = Result;
	OnSacrificeResultReceived.Broadcast(Result);
}

// 献祭结果读取流程：返回 owning client 最近收到的完整副本；调用方只能展示 RequestId、阶段与 Revision，不能据此直接操作 Items 或 Run。
FCatSacrificeResult ACatfishingPlayerController::GetLastSacrificeResult() const
{
	return LastSacrificeResult;
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

// 普通容器库存拖拽 RPC 流程：owning client 只把请求送到自己的 PlayerController；服务器直接进入 Items 提交，地面鱼护箱子按外部箱子处理，不依赖 Actor Owner，也不投 BodyAction/Social。
void ACatfishingPlayerController::ServerTransferObjectBetweenContainers_Implementation(const FGuid RequestId,
	const ECatContainedObjectKind ObjectKind, const FGuid ObjectInstanceId, const FGuid SourceContainerId,
	const ECatContainerKind SourceContainerKind,
	const int32 SourceContainerSlotIndex, const int64 ExpectedSourceRevision, const FGuid TargetContainerId,
	const ECatContainerKind TargetContainerKind, const int32 TargetContainerSlotIndex,
	const int64 ExpectedTargetRevision)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	if (UCatContainerCommandCoordinator* Coordinator = GetWorld()
		? GetWorld()->GetSubsystem<UCatContainerCommandCoordinator>() : nullptr)
	{
		Result = Coordinator->TransferReachableObject(this, ControlledCharacter, RequestId, ObjectKind,
			ObjectInstanceId, SourceContainerId, SourceContainerKind, SourceContainerSlotIndex,
			ExpectedSourceRevision, TargetContainerId, TargetContainerKind, TargetContainerSlotIndex,
			ExpectedTargetRevision);
	}
	else
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	DeliverCampCommandResultToOwningClient(Result);
}

// 一键存入共享鱼缸 RPC 流程：owning client 只提交鱼护源格和鱼实例；目标鱼缸不接受客户端指定，统一交给服务器从固定营地解析。
void ACatfishingPlayerController::ServerStoreFishInSharedTank_Implementation(const FGuid RequestId,
	const FGuid FishInstanceId, const FGuid SourceContainerId, const int32 SourceContainerSlotIndex,
	const int64 ExpectedSourceRevision)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	if (UCatContainerCommandCoordinator* Coordinator = GetWorld()
		? GetWorld()->GetSubsystem<UCatContainerCommandCoordinator>() : nullptr)
	{
		Result = Coordinator->StoreFishInReachableSharedTank(this, ControlledCharacter, RequestId,
			FishInstanceId, SourceContainerId, SourceContainerSlotIndex, ExpectedSourceRevision);
	}
	else
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	DeliverCampCommandResultToOwningClient(Result);
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

// 公共领域结果客户端流程：可靠接收 Camp、容器移动和钓具选择等结果并整体替换本机读模型；随后广播本机通知供 UI Model 刷新，不解释错误、不重算 Revision，也不触发新的领域命令。
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

// 公共仓库 Actor 取用 RPC 路由流程：Controller 只把网络参数和当前 Pawn 交给 Items 协调器，仓库触达、双方 Revision 和容量由 Items/Camp 自己裁决。
void ACatfishingPlayerController::ServerWithdrawCampInventoryItemAtActor_Implementation(
	ACatCampInventoryActor* CampInventory, const FGuid RequestId, const int64 ExpectedCampInventoryRevision,
	const int32 SourceSlotIndex, const int32 Quantity, const int64 ExpectedInventoryRevision)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	if (UCatContainerCommandCoordinator* Coordinator = GetWorld()
		? GetWorld()->GetSubsystem<UCatContainerCommandCoordinator>() : nullptr)
	{
		Result = Coordinator->WithdrawCampInventoryItem(this, ControlledCharacter, CampInventory, RequestId,
			ExpectedCampInventoryRevision, SourceSlotIndex, Quantity, ExpectedInventoryRevision);
	}
	else
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	DeliverCampCommandResultToOwningClient(Result);
}

// 公共仓库 Actor 整理 RPC 路由流程：Controller 不解释营地仓库格子，只把源/目标槽位交给 Items 协调器并回送终态。
void ACatfishingPlayerController::ServerMoveCampInventorySlotAtActor_Implementation(
	ACatCampInventoryActor* CampInventory, const FGuid RequestId, const int64 ExpectedCampInventoryRevision,
	const int32 SourceSlotIndex, const int32 TargetSlotIndex)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	if (UCatContainerCommandCoordinator* Coordinator = GetWorld()
		? GetWorld()->GetSubsystem<UCatContainerCommandCoordinator>() : nullptr)
	{
		Result = Coordinator->MoveCampInventorySlot(this, ControlledCharacter, CampInventory, RequestId,
			ExpectedCampInventoryRevision, SourceSlotIndex, TargetSlotIndex);
	}
	else
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	DeliverCampCommandResultToOwningClient(Result);
}

// 背包存入公共仓库 RPC 路由流程：Controller 只保留网络入口，跨随身库存和公共仓库的原子事务由 Items 协调器持有。
void ACatfishingPlayerController::ServerDepositInventoryItemToCampAtActor_Implementation(
	ACatCampInventoryActor* CampInventory, const FGuid RequestId, const int64 ExpectedCampInventoryRevision,
	const int32 TargetCampSlotIndex, const int64 ExpectedInventoryRevision, const int32 SourceEquipmentSlotIndex)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	if (UCatContainerCommandCoordinator* Coordinator = GetWorld()
		? GetWorld()->GetSubsystem<UCatContainerCommandCoordinator>() : nullptr)
	{
		Result = Coordinator->DepositEquipmentSlotToCampInventory(this, ControlledCharacter, CampInventory,
			RequestId, ExpectedCampInventoryRevision, TargetCampSlotIndex, ExpectedInventoryRevision,
			SourceEquipmentSlotIndex);
	}
	else
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	DeliverCampCommandResultToOwningClient(Result);
}

// 公共仓库拖入背包 RPC 路由流程：Controller 不读取公共仓库或装备细节，只把候选槽位交给 Items 协调器。
void ACatfishingPlayerController::ServerWithdrawCampInventoryItemToSlotAtActor_Implementation(
	ACatCampInventoryActor* CampInventory, const FGuid RequestId, const int64 ExpectedCampInventoryRevision,
	const int32 SourceCampSlotIndex, const int64 ExpectedInventoryRevision, const int32 TargetEquipmentSlotIndex)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	if (UCatContainerCommandCoordinator* Coordinator = GetWorld()
		? GetWorld()->GetSubsystem<UCatContainerCommandCoordinator>() : nullptr)
	{
		Result = Coordinator->WithdrawCampInventoryItemToEquipmentSlot(this, ControlledCharacter, CampInventory,
			RequestId, ExpectedCampInventoryRevision, SourceCampSlotIndex, ExpectedInventoryRevision,
			TargetEquipmentSlotIndex);
	}
	else
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	DeliverCampCommandResultToOwningClient(Result);
}

// 当前选择 RPC 路由流程：Controller 只转交玩家意图，装备定义、实例、解锁和 Revision 由 Equipment 协调器与组件裁决。
void ACatfishingPlayerController::ServerConfigureEquipment_Implementation(const FGuid RequestId,
	const int64 ExpectedRevision, const FName RodDefinitionId, const FName BaitDefinitionId,
	const FName FloatDefinitionId, const FName ScoopNetDefinitionId, const FGuid RodItemInstanceId,
	const FGuid BaitItemInstanceId, const FGuid FloatItemInstanceId, const FGuid ScoopNetItemInstanceId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	if (UCatEquipmentCommandCoordinator* Coordinator = GetWorld()
		? GetWorld()->GetSubsystem<UCatEquipmentCommandCoordinator>() : nullptr)
	{
		Result = Coordinator->ConfigureLoadout(this, ControlledCharacter, RequestId, ExpectedRevision,
			RodDefinitionId, BaitDefinitionId, FloatDefinitionId, ScoopNetDefinitionId, RodItemInstanceId,
			BaitItemInstanceId, FloatItemInstanceId, ScoopNetItemInstanceId);
	}
	else
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	DeliverCampCommandResultToOwningClient(Result);
}

// 随身库存钓具选择 RPC 路由流程：Controller 只转交槽位意图和两份版本；库存协调器重读正式 InventoryComponent 后再交给 Equipment 更新选择。
void ACatfishingPlayerController::ServerSelectInventoryFishingItem_Implementation(
	const FGuid RequestId, const int64 ExpectedInventoryRevision,
	const int64 ExpectedEquipmentRevision, const int32 InventorySlotIndex)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	if (UCatInventoryCommandCoordinator* Coordinator = GetWorld()
		? GetWorld()->GetSubsystem<UCatInventoryCommandCoordinator>() : nullptr)
	{
		Result = Coordinator->SelectFishingItemFromInventorySlot(this, ControlledCharacter, RequestId,
			ExpectedInventoryRevision, ExpectedEquipmentRevision, InventorySlotIndex);
	}
	else
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	DeliverCampCommandResultToOwningClient(Result);
}

// 随身库存整理 RPC 路由流程：Controller 只提交源/目标槽位，正式库存移动由 Inventory 协调器转给 InventoryComponent 裁决。
void ACatfishingPlayerController::ServerMoveInventorySlot_Implementation(const FGuid RequestId,
	const int64 ExpectedRevision, const int32 SourceSlotIndex, const int32 TargetSlotIndex)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	if (UCatInventoryCommandCoordinator* Coordinator = GetWorld()
		? GetWorld()->GetSubsystem<UCatInventoryCommandCoordinator>() : nullptr)
	{
		Result = Coordinator->MoveInventorySlot(this, ControlledCharacter, RequestId, ExpectedRevision,
			SourceSlotIndex, TargetSlotIndex);
	}
	else
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
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

void ACatfishingPlayerController::ServerSellFish_Implementation(const FGuid FishInstanceId,
	const FGuid ContainerId, const int64 ExpectedContainerRevision, const FGuid RequestId,
	const int64 ExpectedWalletRevision)
{
	// 售鱼 RPC 流程：Controller 只取当前 World 的商店交易控制器并转交鱼、容器和版本前提。
	// 依赖缺失时这里静默返回，和旧入口保持一致；真正的鱼移除、估价、入公款和账本幂等都在 ShopTradeController 中完成。
	UCatShopTradeController* Controller =
		GetWorld() ? GetWorld()->GetSubsystem<UCatShopTradeController>() : nullptr;
	if (!Controller)
	{
		return;
	}
	Controller->SubmitFishSaleFromPlayer(this, FishInstanceId, ContainerId, ExpectedContainerRevision,
		RequestId, ExpectedWalletRevision);
}

// 修竿 RPC 路由流程：修竿是 Equipment 事务，Controller 只把营地候选和当前 Pawn 转交给 Equipment 协调器。
void ACatfishingPlayerController::ServerRepairRodAtCamp_Implementation(ACatCampHubActor* Camp,
	const FGuid RequestId, const int64 ExpectedEquipmentRevision)
{
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	if (UCatEquipmentCommandCoordinator* Coordinator = GetWorld()
		? GetWorld()->GetSubsystem<UCatEquipmentCommandCoordinator>() : nullptr)
	{
		Coordinator->RepairRodAtCamp(this, ControlledCharacter, Camp, RequestId, ExpectedEquipmentRevision);
	}
}

// 草药 RPC 路由流程：草药救援是库存 + Condition 事务，不进入 BodyAction；Controller 不碰 Equipment，完整扣草药和恢复顺序由 Condition 协调器处理。
void ACatfishingPlayerController::ServerUseHerbOnCharacter_Implementation(ACatCharacter* TargetCharacter,
	const FGuid RequestId, const int64 ExpectedInventoryRevision, const FGuid HerbItemInstanceId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (UCatHerbRecoveryCoordinator* Coordinator = GetWorld()
		? GetWorld()->GetSubsystem<UCatHerbRecoveryCoordinator>() : nullptr)
	{
		Result = Coordinator->UseHerbOnCharacter(this, TargetCharacter, RequestId, ExpectedInventoryRevision,
			HerbItemInstanceId);
	}
	else
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	DeliverCampCommandResultToOwningClient(Result);
}

// 直接吃鱼 RPC 路由流程：吃鱼是 Items + Condition 事务，不进入 BodyAction；Controller 只调用协调器并回送结构化终态。
void ACatfishingPlayerController::ServerConsumeFish_Implementation(ACatCharacter* EatingCharacter,
	FCatFishConsumeCommand Command)
{
	FCatFishConsumeResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	if (UCatFishConsumptionCoordinator* Coordinator = GetWorld()
		? GetWorld()->GetSubsystem<UCatFishConsumptionCoordinator>() : nullptr)
	{
		Result = Coordinator->ConsumeReachableFish(this, EatingCharacter, Command);
	}
	else
	{
		Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	DeliverFishConsumeResultToOwningClient(Result);
}

// 献祭回执投递流程：单机或 listen server 本地玩家没有远端连接可回送时，直接复用 Client 实现刷新本机缓存；远端玩家保持可靠 RPC 语义。
void ACatfishingPlayerController::DeliverSacrificeResultToOwningClient(const FCatSacrificeResult& Result)
{
	if (HasAuthority() && IsLocalController())
	{
		ClientReceiveSacrificeResult_Implementation(Result);
		return;
	}
	ClientReceiveSacrificeResult(Result);
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

// 直接吃鱼回执投递流程：单机或 listen server 本地玩家没有远端连接可回送时，直接复用 Client 实现刷新本机缓存；远端玩家保持可靠 RPC 语义。
void ACatfishingPlayerController::DeliverFishConsumeResultToOwningClient(const FCatFishConsumeResult& Result)
{
	if (HasAuthority() && IsLocalController())
	{
		ClientReceiveFishConsumeResult_Implementation(Result);
		return;
	}
	ClientReceiveFishConsumeResult(Result);
}

// 直接吃鱼结果客户端流程：可靠接收 Items 消费鱼和身体提交的完整结果并整体替换本机读模型；随后广播本机通知供 UI Model 刷新，不应用效果。
void ACatfishingPlayerController::ClientReceiveFishConsumeResult_Implementation(const FCatFishConsumeResult& Result)
{
	LastFishConsumeResult = Result;
	OnFishConsumeResultReceived.Broadcast(Result);
}

// 直接吃鱼结果读取流程：返回 owning client 最近收到的完整副本；调用方只能展示 RequestId、错误、容器 Revision 和身体提交终态。
FCatFishConsumeResult ACatfishingPlayerController::GetLastFishConsumeResult() const
{
	return LastFishConsumeResult;
}

// 偷鱼开始 RPC 路由流程：偷鱼是 Social + Items escrow 协议，不进入 BodyAction；Controller 只清客户端身份并转交 Social。
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
// 2. IA_Interact 只进入 PlayerController 持有的唯一 TargetingComponent；提示 UI 不再绑定第二次 E。
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

// Host exit 客户端流程：从本地 GameInstance 取得唯一 Online 子系统并提交服务器关联 RequestId；子系统只在本地 DestroySession 成功后回 ACK，失败由 Host 有界超时收口。
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
