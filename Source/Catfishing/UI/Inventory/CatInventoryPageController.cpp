#include "UI/Inventory/CatInventoryPageController.h"

#include "Camp/CatCampHubActor.h"
#include "Character/CatCharacter.h"
#include "EnhancedInputComponent.h"
#include "EngineUtils.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Logging/CatLog.h"
#include "UI/CatUISettings.h"
#include "UI/Inventory/CatInventoryModel.h"
#include "UI/Inventory/CatInventoryWidget.h"

// 绑定流程：
// 1. 先解除旧页面，避免同一个 Controller 上留下旧输入绑定。
// 2. 保存 LocalPlayer、Controller、Model 和 View，并订阅 Model、关闭、格子和动作意图。
// 3. 安装背包开关 Action；缺资产时只降级快捷键，不创建第二套 InputContext。
// 4. 渲染当前 Model 状态，保证首次打开不是空白。
bool UCatInventoryPageController::Bind(ULocalPlayer* InLocalPlayer, APlayerController* InController,
	UCatInventoryModel* InModel, UCatInventoryWidget* InView)
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
	ViewSlotSelectionHandle = InView->OnSlotSelectionRequested.AddUObject(
		this, &ThisClass::HandleViewSlotSelectionRequested);
	ViewSlotPointerHandle = InView->OnSlotPointerRequested.AddUObject(
		this, &ThisClass::HandleViewSlotPointerRequested);
	ViewActionHandle = InView->OnInventoryActionRequested.AddUObject(this, &ThisClass::HandleViewActionRequested);
	InstallInventoryInput();
	HandleModelViewStateChanged();
	return true;
}

// 解绑流程：先关闭打开中的背包并恢复输入，再移除 Action 和所有委托；最后清弱引用和本地状态。
void UCatInventoryPageController::Unbind()
{
	if (bInventoryOpen)
	{
		bInventoryOpen = false;
		ApplyInventoryInputMode(false);
	}
	RemoveInventoryInput();
	if (UCatInventoryModel* Model = BoundModel.Get())
	{
		Model->OnViewStateChanged.Remove(ModelViewChangedHandle);
		Model->SetOpen(false);
	}
	if (UCatInventoryWidget* View = BoundView.Get())
	{
		View->OnCloseRequested.Remove(ViewCloseHandle);
		View->OnSlotSelectionRequested.Remove(ViewSlotSelectionHandle);
		View->OnSlotPointerRequested.Remove(ViewSlotPointerHandle);
		View->OnInventoryActionRequested.Remove(ViewActionHandle);
		View->RemoveFromParent();
	}
	ModelViewChangedHandle.Reset();
	ViewCloseHandle.Reset();
	ViewSlotSelectionHandle.Reset();
	ViewSlotPointerHandle.Reset();
	ViewActionHandle.Reset();
	BoundLocalPlayer.Reset();
	BoundPlayerController.Reset();
	BoundModel.Reset();
	BoundView.Reset();
	bPreviousMouseCursorVisible = false;
}

// 切换流程：打开前保存鼠标状态；随后应用输入模式、调整视口挂载和把打开状态投影给 Model。
void UCatInventoryPageController::ToggleInventory()
{
	APlayerController* Controller = BoundPlayerController.Get();
	UCatInventoryModel* Model = BoundModel.Get();
	UCatInventoryWidget* View = BoundView.Get();
	if (!Controller || !Model || !View)
	{
		return;
	}
	const bool bOpen = !bInventoryOpen;
	if (bOpen)
	{
		bPreviousMouseCursorVisible = Controller->bShowMouseCursor;
		if (!View->IsInViewport())
		{
			View->AddToViewport(10);
		}
	}
	bInventoryOpen = bOpen;
	ApplyInventoryInputMode(bInventoryOpen);
	Model->SetOpen(bInventoryOpen);
	if (!bInventoryOpen)
	{
		View->RemoveFromParent();
	}
}

// 状态查询流程：返回 PageController 的唯一打开状态，不读取 Widget 可见性或鼠标状态拼第二份真相。
bool UCatInventoryPageController::IsInventoryOpen() const
{
	return bInventoryOpen;
}

// 外部刷新流程：只要求 Model 重读鱼护快照；PageController 不缓存任何后端事实。
void UCatInventoryPageController::RefreshModel()
{
	if (UCatInventoryModel* Model = BoundModel.Get())
	{
		Model->Refresh();
	}
}

// 渲染转交流程：Model 已聚合完整背包投影；PageController 只转交给 WBP。
void UCatInventoryPageController::HandleModelViewStateChanged()
{
	UCatInventoryModel* Model = BoundModel.Get();
	UCatInventoryWidget* View = BoundView.Get();
	if (Model && View)
	{
		View->RenderInventory(Model->GetViewState());
	}
}

// 关闭意图流程：只有背包打开时才切换关闭，迟到关闭点击不会反向打开。
void UCatInventoryPageController::HandleViewCloseRequested()
{
	if (bInventoryOpen)
	{
		ToggleInventory();
	}
}

// 格子选择流程：PageController 不保存选择状态，只把下标交给 Model 基于最新快照裁剪。
void UCatInventoryPageController::HandleViewSlotSelectionRequested(const int32 SlotIndex)
{
	if (UCatInventoryModel* Model = BoundModel.Get())
	{
		Model->SelectSlot(SlotIndex);
	}
}

// 格子上下文流程：右键和拖拽先同步选择；当前版本不在 PageController 中把右键直接转成吃鱼，避免误删实物鱼。
void UCatInventoryPageController::HandleViewSlotPointerRequested(const int32 SlotIndex,
	const ECatInventorySlotPointerAction PointerAction)
{
	(void)PointerAction;
	HandleViewSlotSelectionRequested(SlotIndex);
}

// 背包动作流程：
// 1. 从 Model 当前 ViewState 读取选中鱼、容器 ID 和 Revision，拒绝空选择。
// 2. 生成 RequestId 并先写 pending，使同步 authority 回包也能匹配。
// 3. 按动作类型调用 PlayerController 正式服务器入口，绝不让 Widget 直接访问 Items 或 Run。
// 4. 本地无法解析 Controller、Character 或营地时只发布结构化拒绝。
void UCatInventoryPageController::HandleViewActionRequested(const ECatInventoryAction Action)
{
	UCatInventoryModel* Model = BoundModel.Get();
	ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(BoundPlayerController.Get());
	if (!Model || !CatController || Action == ECatInventoryAction::None)
	{
		return;
	}
	const FCatInventoryViewState& State = Model->GetViewState();
	const FGuid RequestId = FGuid::NewGuid();
	const bool bHasSelectedFish = State.bHasSelectedFish
		&& State.SelectedFish.FishInstanceId.IsValid()
		&& State.PersonalFishGuard.ContainerId.IsValid();
	if (!bHasSelectedFish)
	{
		Model->MarkActionRejected(Action, RequestId, ECatDomainCommandError::InvalidPayload,
			State.PersonalFishGuard.Revision);
		return;
	}

	switch (Action)
	{
	case ECatInventoryAction::ConsumeSelectedFish:
		{
			ACatCharacter* Character = Cast<ACatCharacter>(CatController->GetPawn());
			if (!Character)
			{
				Model->MarkActionRejected(Action, RequestId, ECatDomainCommandError::DependencyUnavailable,
					State.PersonalFishGuard.Revision);
				return;
			}
			FCatFishConsumeCommand Command;
			Command.Context.RequestId = RequestId;
			Command.Context.ExpectedRevision = State.PersonalFishGuard.Revision;
			Command.FishInstanceId = State.SelectedFish.FishInstanceId;
			Command.SourceContainerId = State.PersonalFishGuard.ContainerId;
			Model->MarkActionSubmitted(Action, RequestId);
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
	case ECatInventoryAction::TransferSelectedFishToTank:
		{
			ACatCampHubActor* Camp = ResolveCampHubForInventoryAction();
			FCatContainerSnapshot TankSnapshot;
			if (!Camp || !Camp->TryGetSharedFishTankSnapshot(TankSnapshot))
			{
				Model->MarkActionRejected(Action, RequestId, ECatDomainCommandError::DependencyUnavailable,
					State.PersonalFishGuard.Revision);
				return;
			}
			Model->MarkActionSubmitted(Action, RequestId);
			if (CatController->HasAuthority())
			{
				CatController->ServerTransferFishToTank_Implementation(Camp, RequestId, State.SelectedFish.FishInstanceId,
					State.PersonalFishGuard.Revision, TankSnapshot.Revision);
			}
			else
			{
				CatController->ServerTransferFishToTank(Camp, RequestId, State.SelectedFish.FishInstanceId,
					State.PersonalFishGuard.Revision, TankSnapshot.Revision);
			}
			return;
		}
	case ECatInventoryAction::SacrificeSelectedFish:
		{
			FCatSacrificeCommand Command;
			Command.Context.RequestId = RequestId;
			Command.Context.ExpectedRevision = State.PersonalFishGuard.Revision;
			Command.FishInstanceId = State.SelectedFish.FishInstanceId;
			Command.ContainerId = State.PersonalFishGuard.ContainerId;
			Model->MarkActionSubmitted(Action, RequestId);
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
	case ECatInventoryAction::None:
	default:
		return;
	}
}

// 营地定位流程：优先选择当前 Pawn 最近的固定营地；无 Pawn 时返回 World 中第一座营地，服务器范围校验仍是最终裁决。
ACatCampHubActor* UCatInventoryPageController::ResolveCampHubForInventoryAction() const
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

// 输入安装流程：
// 1. 要求 Controller 当前 InputComponent 是 EnhancedInputComponent。
// 2. 从 UI Settings 加载已有 InputContext 中的背包 Action，并把 IMC 加载作为资产接线校验。
// 3. 只 BindAction，不 AddMappingContext、不 MapKey，避免运行时代码写死按键或生成第二套 IMC。
void UCatInventoryPageController::InstallInventoryInput()
{
	RemoveInventoryInput();
	APlayerController* Controller = BoundPlayerController.Get();
	UEnhancedInputComponent* Input = Controller ? Cast<UEnhancedInputComponent>(Controller->InputComponent) : nullptr;
	const UCatUISettings* Settings = GetDefault<UCatUISettings>();
	UInputAction* ToggleAction = Settings ? Settings->LoadInventoryToggleAction() : nullptr;
	const UInputMappingContext* MappingContext = Settings ? Settings->LoadGameplayInputMappingContext() : nullptr;
	if (!Input || !Settings || !ToggleAction || !MappingContext)
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_inventory_input_unavailable Controller=%s Action=%s Context=%s"),
			*GetNameSafe(Controller),
			Settings ? *Settings->LakeMenuToggleAction.ToSoftObjectPath().ToString() : TEXT("None"),
			Settings ? *Settings->LakeMenuInputMappingContext.ToSoftObjectPath().ToString() : TEXT("None"));
		return;
	}
	AppliedInventoryToggleAction = ToggleAction;
	InventoryInputBindingHandle = Input->BindAction(
		AppliedInventoryToggleAction, ETriggerEvent::Started, this, &ThisClass::ToggleInventory).GetHandle();
	BoundInventoryInputComponent = Input;
}

// 输入移除流程：从安装时记录的 EnhancedInputComponent 删除精确绑定；InputContext 属于基础输入层，本页不安装也不移除。
void UCatInventoryPageController::RemoveInventoryInput()
{
	if (UEnhancedInputComponent* Input = BoundInventoryInputComponent.Get();
		Input && InventoryInputBindingHandle != 0)
	{
		Input->RemoveBindingByHandle(InventoryInputBindingHandle);
	}
	BoundInventoryInputComponent.Reset();
	InventoryInputBindingHandle = 0;
	AppliedInventoryToggleAction = nullptr;
}

// 输入模式流程：打开时把背包 WBP 设为 UIOnly 焦点并显示鼠标；关闭时恢复 GameOnly 和打开前鼠标可见性。
void UCatInventoryPageController::ApplyInventoryInputMode(const bool bOpen)
{
	APlayerController* Controller = BoundPlayerController.Get();
	if (!Controller)
	{
		return;
	}
	if (bOpen)
	{
		if (UCatInventoryWidget* View = BoundView.Get())
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
