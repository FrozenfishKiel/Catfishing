#include "UI/CatLakeReachPageController.h"

#include "Camp/CatCampHubActor.h"
#include "Character/CatCharacter.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EngineUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Logging/CatLog.h"
#include "Online/CatOnlineSubsystem.h"
#include "UI/CatLakeReachModel.h"
#include "UI/CatLakeReachWidget.h"
#include "UI/CatUISettings.h"

// 绑定流程：
// 1. 先解除旧页面，避免同一个 PageController 仍持有上一只 Controller 的输入。
// 2. 保存 LocalPlayer、Controller、Model 和 WBP View 的弱引用，并订阅 Model 完整快照、菜单意图和鱼护动作意图。
// 3. 尝试安装菜单输入 Context；缺 EnhancedInput 时只降级掉快捷键，不阻止 WBP 根和按钮意图工作。
// 4. 最后用 Model 当前 ViewState 渲染一次，保证进视口第一帧不是空画面。
bool UCatLakeReachPageController::Bind(
	ULocalPlayer* InLocalPlayer,
	APlayerController* InController,
	UCatLakeReachModel* InModel,
	UCatLakeReachWidget* InView)
{
	Unbind();
	if (!InLocalPlayer || !InController || !InModel || !InView)
	{
		return false;
	}

	BoundLocalPlayer = InLocalPlayer;
	BoundPlayerController = InController;
	BoundModel = InModel;
	BoundView = InView;
	ModelViewChangedHandle = InModel->OnViewStateChanged.AddUObject(this, &ThisClass::HandleModelViewStateChanged);
	ViewCloseHandle = InView->OnCloseRequested.AddUObject(this, &ThisClass::HandleViewCloseRequested);
	ViewLeaveHandle = InView->OnLeaveRequested.AddUObject(this, &ThisClass::HandleViewLeaveRequested);
	ViewFishGuardSelectionHandle = InView->OnFishGuardSelectionRequested.AddUObject(
		this, &ThisClass::HandleViewFishGuardSelectionRequested);
	ViewFishGuardActionHandle = InView->OnFishGuardActionRequested.AddUObject(
		this, &ThisClass::HandleViewFishGuardActionRequested);
	InstallMenuInput();
	HandleModelViewStateChanged();
	return true;
}

// 解绑流程：
// 1. 如果菜单仍打开，先恢复旧 Controller 的 GameOnly 与鼠标状态。
// 2. 移除 EnhancedInput Action/Context，再从同一 Model/View 移除菜单和鱼护意图委托。
// 3. 清弱引用和句柄，保证旧 WBP 按钮或旧 Model 更新不会影响新页面。
void UCatLakeReachPageController::Unbind()
{
	if (bLakeMenuOpen)
	{
		bLakeMenuOpen = false;
		ApplyLakeMenuInputMode(false);
	}
	RemoveMenuInput();
	if (UCatLakeReachModel* Model = BoundModel.Get())
	{
		Model->OnViewStateChanged.Remove(ModelViewChangedHandle);
		Model->SetMenuOpen(false);
	}
	if (UCatLakeReachWidget* View = BoundView.Get())
	{
		View->OnCloseRequested.Remove(ViewCloseHandle);
		View->OnLeaveRequested.Remove(ViewLeaveHandle);
		View->OnFishGuardSelectionRequested.Remove(ViewFishGuardSelectionHandle);
		View->OnFishGuardActionRequested.Remove(ViewFishGuardActionHandle);
	}
	ModelViewChangedHandle.Reset();
	ViewCloseHandle.Reset();
	ViewLeaveHandle.Reset();
	ViewFishGuardSelectionHandle.Reset();
	ViewFishGuardActionHandle.Reset();
	BoundLocalPlayer.Reset();
	BoundPlayerController.Reset();
	BoundModel.Reset();
	BoundView.Reset();
	bPreviousMouseCursorVisible = false;
}

// 菜单切换流程：打开前保存鼠标状态；随后写 PageController 唯一菜单状态、应用输入模式，并把状态投影给 Model 触发 View 刷新。
void UCatLakeReachPageController::ToggleLakeMenu()
{
	APlayerController* Controller = BoundPlayerController.Get();
	UCatLakeReachModel* Model = BoundModel.Get();
	UCatLakeReachWidget* View = BoundView.Get();
	if (!Controller || !Model || !View)
	{
		return;
	}
	const bool bOpen = !bLakeMenuOpen;
	if (bOpen)
	{
		bPreviousMouseCursorVisible = Controller->bShowMouseCursor;
	}
	bLakeMenuOpen = bOpen;
	ApplyLakeMenuInputMode(bLakeMenuOpen);
	Model->SetMenuOpen(bLakeMenuOpen);
}

// 状态查询流程：直接返回 PageController 维护的唯一布尔值；不读取 Widget 可见性、焦点或鼠标状态拼第二份状态。
bool UCatLakeReachPageController::IsLakeMenuOpen() const
{
	return bLakeMenuOpen;
}

// 外部刷新流程：Online 或其他 PageController 外事实变化时只要求 Model 重读 Query；本对象不缓存那些事实。
void UCatLakeReachPageController::RefreshModel()
{
	if (UCatLakeReachModel* Model = BoundModel.Get())
	{
		Model->Refresh();
	}
}

// 渲染转交流程：Model 已经聚合完整 ViewState；PageController 只把当前快照交给 WBP View，不修改 DTO。
void UCatLakeReachPageController::HandleModelViewStateChanged()
{
	UCatLakeReachModel* Model = BoundModel.Get();
	UCatLakeReachWidget* View = BoundView.Get();
	if (!Model || !View)
	{
		return;
	}
	View->Render(Model->GetViewState());
}

// 关闭意图流程：只有菜单仍打开时才切换关闭；迟到按钮或销毁期广播不会把已关闭菜单重新打开。
void UCatLakeReachPageController::HandleViewCloseRequested()
{
	if (bLakeMenuOpen)
	{
		ToggleLakeMenu();
	}
}

// Lake 离局流程：复用统一 Online Leave 请求；请求后刷新 Model，让同步拒绝或 pending 状态能反映到按钮可用性。
void UCatLakeReachPageController::HandleViewLeaveRequested()
{
	ULocalPlayer* LocalPlayer = BoundLocalPlayer.Get();
	const UWorld* World = LocalPlayer ? LocalPlayer->GetWorld() : nullptr;
	UCatOnlineSubsystem* Online = LocalPlayer && LocalPlayer->GetGameInstance()
		? LocalPlayer->GetGameInstance()->GetSubsystem<UCatOnlineSubsystem>() : nullptr;
	if (!Online)
	{
		return;
	}

	const FCatOnlineResult Result = Online->RequestLeave();
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_reach_leave_requested RequestId=%s World=%s Result=%s Error=%s"),
		*Result.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		World ? *World->GetName() : TEXT("None"),
		Result.bAccepted ? TEXT("accepted") : TEXT("rejected"),
		*UEnum::GetValueAsString(Result.Error));
	RefreshModel();
}

// 鱼护选择意图流程：PageController 不缓存下标，只把偏移交给 Model，让 Model 基于最新快照裁剪并发布。
void UCatLakeReachPageController::HandleViewFishGuardSelectionRequested(const int32 Offset)
{
	if (UCatLakeReachModel* Model = BoundModel.Get())
	{
		Model->SelectFishGuardEntryByOffset(Offset);
	}
}

// 鱼护动作意图流程：
// 1. 从 Model 当前 ViewState 读取选中鱼、鱼护 ID、鱼护 Revision 和 Run Revision，拒绝空选择或无效载荷。
// 2. 生成新的 RequestId 并先写入 pending，让同步回包也能被 Model 匹配。
// 3. 按动作类型调用对应 PlayerController 服务器入口：authority 场景直进实现，客户端场景发 RPC。
// 4. 本地无法解析营地或 Controller 时只发布结构化拒绝，不绕过 PlayerController 直接调用 Items/Camp/Coordinator。
void UCatLakeReachPageController::HandleViewFishGuardActionRequested(const ECatUIReachFishGuardAction Action)
{
	UCatLakeReachModel* Model = BoundModel.Get();
	ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(BoundPlayerController.Get());
	if (!Model || !CatController || Action == ECatUIReachFishGuardAction::None)
	{
		return;
	}

	const FCatUIReachViewState& State = Model->GetViewState();
	const FGuid RequestId = FGuid::NewGuid();
	const bool bHasSelectedFish = State.bHasSelectedFishGuardFish
		&& State.SelectedFishGuardFish.FishInstanceId.IsValid()
		&& State.PersonalFishGuard.ContainerId.IsValid();
	if (!bHasSelectedFish)
	{
		Model->MarkFishGuardActionRejected(Action, RequestId, ECatDomainCommandError::InvalidPayload,
			State.PersonalFishGuard.Revision);
		return;
	}

	switch (Action)
	{
	case ECatUIReachFishGuardAction::ConsumeSelectedFish:
		{
			ACatCharacter* Character = Cast<ACatCharacter>(CatController->GetPawn());
			if (!Character)
			{
				Model->MarkFishGuardActionRejected(Action, RequestId, ECatDomainCommandError::DependencyUnavailable,
					State.PersonalFishGuard.Revision);
				return;
			}
			FCatFishConsumeCommand Command;
			Command.Context.RequestId = RequestId;
			Command.Context.ExpectedRevision = State.PersonalFishGuard.Revision;
			Command.FishInstanceId = State.SelectedFishGuardFish.FishInstanceId;
			Command.SourceContainerId = State.PersonalFishGuard.ContainerId;
			Model->MarkFishGuardActionSubmitted(Action, RequestId);
			if (CatController->HasAuthority())
			{
				CatController->ServerConsumeFish_Implementation(Character, Command);
			}
			else
			{
				CatController->ServerConsumeFish(Character, Command);
			}
			return;
		}
	case ECatUIReachFishGuardAction::TransferSelectedFishToTank:
		{
			ACatCampHubActor* Camp = ResolveCampHubForFishGuardAction();
			FCatContainerSnapshot SharedTankSnapshot;
			if (!Camp || !Camp->TryGetSharedFishTankSnapshot(SharedTankSnapshot))
			{
				Model->MarkFishGuardActionRejected(Action, RequestId, ECatDomainCommandError::DependencyUnavailable,
					State.PersonalFishGuard.Revision);
				return;
			}
			Model->MarkFishGuardActionSubmitted(Action, RequestId);
			if (CatController->HasAuthority())
			{
				CatController->ServerTransferFishToTank_Implementation(Camp, RequestId,
					State.SelectedFishGuardFish.FishInstanceId, State.PersonalFishGuard.Revision,
					SharedTankSnapshot.Revision);
			}
			else
			{
				CatController->ServerTransferFishToTank(Camp, RequestId, State.SelectedFishGuardFish.FishInstanceId,
					State.PersonalFishGuard.Revision, SharedTankSnapshot.Revision);
			}
			return;
		}
	case ECatUIReachFishGuardAction::SacrificeSelectedFish:
		{
			FCatSacrificeCommand Command;
			Command.Context.RequestId = RequestId;
			Command.Context.ExpectedRevision = State.PersonalFishGuard.Revision;
			Command.FishInstanceId = State.SelectedFishGuardFish.FishInstanceId;
			Command.ContainerId = State.PersonalFishGuard.ContainerId;
			Command.ExpectedRunRevision = State.Run.Revision;
			Model->MarkFishGuardActionSubmitted(Action, RequestId);
			if (CatController->HasAuthority())
			{
				CatController->ServerRequestSacrifice_Implementation(Command);
			}
			else
			{
				CatController->ServerRequestSacrifice(Command);
			}
			return;
		}
	case ECatUIReachFishGuardAction::None:
	default:
		return;
	}
}

// 营地定位流程：优先选择当前 Pawn 最近的固定营地；没有 Pawn 时返回 World 中第一座营地，由服务器范围校验继续兜底。
ACatCampHubActor* UCatLakeReachPageController::ResolveCampHubForFishGuardAction() const
{
	const APlayerController* Controller = BoundPlayerController.Get();
	UWorld* World = Controller ? Controller->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}

	const APawn* Pawn = Controller->GetPawn();
	const FVector SourceLocation = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
	ACatCampHubActor* BestCamp = nullptr;
	double BestDistanceSq = TNumericLimits<double>::Max();
	for (TActorIterator<ACatCampHubActor> It(World); It; ++It)
	{
		ACatCampHubActor* Candidate = *It;
		if (!Candidate)
		{
			continue;
		}
		if (!Pawn)
		{
			return Candidate;
		}
		const double DistanceSq = FVector::DistSquared(SourceLocation, Candidate->GetActorLocation());
		if (DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			BestCamp = Candidate;
		}
	}
	return BestCamp;
}

// 菜单输入安装流程：先要求 Controller 使用 EnhancedInputComponent、LocalPlayer 子系统和有效配置键；然后创建瞬时 Action/Context、映射一个键并保存唯一绑定句柄。
void UCatLakeReachPageController::InstallMenuInput()
{
	RemoveMenuInput();
	APlayerController* Controller = BoundPlayerController.Get();
	UEnhancedInputComponent* Input = Controller ? Cast<UEnhancedInputComponent>(Controller->InputComponent) : nullptr;
	ULocalPlayer* LocalPlayer = BoundLocalPlayer.Get();
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	const UCatUISettings* Settings = GetDefault<UCatUISettings>();
	const FKey MenuKey = Settings ? FKey(Settings->LakeMenuToggleKeyName) : FKey();
	if (!Input || !InputSubsystem || !Settings || !MenuKey.IsValid())
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_reach_menu_input_unavailable Controller=%s Key=%s"),
			*GetNameSafe(Controller), Settings ? *Settings->LakeMenuToggleKeyName.ToString() : TEXT("None"));
		return;
	}

	LakeMenuToggleAction = NewObject<UInputAction>(this);
	LakeMenuMappingContext = NewObject<UInputMappingContext>(this);
	if (!LakeMenuToggleAction || !LakeMenuMappingContext)
	{
		LakeMenuToggleAction = nullptr;
		LakeMenuMappingContext = nullptr;
		return;
	}
	LakeMenuToggleAction->ValueType = EInputActionValueType::Boolean;
	LakeMenuMappingContext->MapKey(LakeMenuToggleAction, MenuKey);
	InputSubsystem->AddMappingContext(LakeMenuMappingContext, FMath::Max(0, Settings->LakeMenuInputPriority));
	bLakeMenuMappingInstalled = true;
	LakeMenuInputBindingHandle = Input->BindAction(
		LakeMenuToggleAction, ETriggerEvent::Started, this, &ThisClass::ToggleLakeMenu).GetHandle();
	BoundMenuInputComponent = Input;
}

// 菜单输入移除流程：先从安装时保存的 EnhancedInputComponent 删除精确绑定，再从同一 LocalPlayer 移除已安装 Context；最后清弱引用、句柄和瞬时对象。
void UCatLakeReachPageController::RemoveMenuInput()
{
	if (UEnhancedInputComponent* Input = BoundMenuInputComponent.Get();
		Input && LakeMenuInputBindingHandle != 0)
	{
		Input->RemoveBindingByHandle(LakeMenuInputBindingHandle);
	}
	if (bLakeMenuMappingInstalled && LakeMenuMappingContext)
	{
		if (ULocalPlayer* LocalPlayer = BoundLocalPlayer.Get())
		{
			if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
				LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				InputSubsystem->RemoveMappingContext(LakeMenuMappingContext);
			}
		}
	}
	BoundMenuInputComponent.Reset();
	LakeMenuInputBindingHandle = 0;
	bLakeMenuMappingInstalled = false;
	LakeMenuToggleAction = nullptr;
	LakeMenuMappingContext = nullptr;
}

// InputMode 流程：打开时把 WBP View 设为 UIOnly 焦点并显示鼠标；关闭时恢复 GameOnly 和打开前鼠标可见性。
void UCatLakeReachPageController::ApplyLakeMenuInputMode(const bool bOpen)
{
	APlayerController* Controller = BoundPlayerController.Get();
	if (!Controller)
	{
		return;
	}
	if (bOpen)
	{
		if (UCatLakeReachWidget* View = BoundView.Get())
		{
			FInputModeUIOnly InputMode;
			InputMode.SetWidgetToFocus(View->TakeWidget());
			InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			Controller->SetInputMode(InputMode);
			Controller->bShowMouseCursor = true;
			View->SetKeyboardFocus();
		}
		return;
	}
	Controller->SetInputMode(FInputModeGameOnly());
	Controller->bShowMouseCursor = bPreviousMouseCursorVisible;
}
