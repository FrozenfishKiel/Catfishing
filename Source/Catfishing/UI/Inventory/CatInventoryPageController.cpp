#include "UI/Inventory/CatInventoryPageController.h"

#include "Character/CatCharacter.h"
#include "EnhancedInputComponent.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Items/CatContainerReplicationComponent.h"
#include "Logging/CatLog.h"
#include "UI/CatUISettings.h"
#include "UI/Inventory/CatInventoryModel.h"
#include "UI/Inventory/CatInventoryTypes.h"
#include "UI/Inventory/CatInventoryWidget.h"

namespace
{
	// 源格复核流程：Drop 时用源容器、源槽位和物体身份去最新 ViewState 复核，避免拖拽开始后的刷新把同一物体换到别的格子后仍被错误提交。
	const FCatInventorySlotView* FindCurrentSourceSlot(const FCatInventoryViewState& State,
		const FCatInventorySlotView& DragSource)
	{
		if (DragSource.SlotSource != ECatInventorySlotSource::ContainerObject
			|| !DragSource.ContainerId.IsValid() || DragSource.ContainerSlotIndex == INDEX_NONE
			|| DragSource.ObjectKind == ECatContainedObjectKind::Unknown
			|| !DragSource.ObjectInstanceId.IsValid())
		{
			return nullptr;
		}
		if (State.Slots.IsValidIndex(DragSource.SlotIndex))
		{
			const FCatInventorySlotView& IndexedSlot = State.Slots[DragSource.SlotIndex];
			if (IndexedSlot.ContainerId == DragSource.ContainerId
				&& IndexedSlot.SlotSource == ECatInventorySlotSource::ContainerObject
				&& IndexedSlot.ContainerKind == DragSource.ContainerKind
				&& IndexedSlot.ContainerSlotIndex == DragSource.ContainerSlotIndex
				&& IndexedSlot.bOccupied
				&& IndexedSlot.ObjectKind == DragSource.ObjectKind
				&& IndexedSlot.ObjectInstanceId == DragSource.ObjectInstanceId)
			{
				return &IndexedSlot;
			}
		}
		return State.Slots.FindByPredicate([&DragSource](const FCatInventorySlotView& Slot)
		{
			return Slot.ContainerId == DragSource.ContainerId
				&& Slot.SlotSource == ECatInventorySlotSource::ContainerObject
				&& Slot.ContainerKind == DragSource.ContainerKind
				&& Slot.ContainerSlotIndex == DragSource.ContainerSlotIndex
				&& Slot.bOccupied
				&& Slot.ObjectKind == DragSource.ObjectKind
				&& Slot.ObjectInstanceId == DragSource.ObjectInstanceId;
		});
	}

	// 目标格复核流程：目标可以是空格；Drop 必须保留目标容器内槽位，找不到同一槽位时不能回退到容器第一个格子。
	const FCatInventorySlotView* FindCurrentTargetSlot(const FCatInventoryViewState& State,
		const FCatInventorySlotView& DropTarget)
	{
		if (DropTarget.SlotSource != ECatInventorySlotSource::ContainerObject
			|| !DropTarget.ContainerId.IsValid() || DropTarget.ContainerSlotIndex == INDEX_NONE)
		{
			return nullptr;
		}
		if (State.Slots.IsValidIndex(DropTarget.SlotIndex))
		{
			const FCatInventorySlotView& IndexedSlot = State.Slots[DropTarget.SlotIndex];
			if (IndexedSlot.ContainerId == DropTarget.ContainerId
				&& IndexedSlot.SlotSource == ECatInventorySlotSource::ContainerObject
				&& IndexedSlot.ContainerKind == DropTarget.ContainerKind
				&& IndexedSlot.ContainerSlotIndex == DropTarget.ContainerSlotIndex)
			{
				return &IndexedSlot;
			}
		}
		return State.Slots.FindByPredicate([&DropTarget](const FCatInventorySlotView& Slot)
		{
			return Slot.ContainerId == DropTarget.ContainerId
				&& Slot.SlotSource == ECatInventorySlotSource::ContainerObject
				&& Slot.ContainerKind == DropTarget.ContainerKind
				&& Slot.ContainerSlotIndex == DropTarget.ContainerSlotIndex;
		});
	}

	// 随身库存格复核流程：按数组下标在最新 ViewState 中找同一格；格子内容是否仍可移动由调用方继续判断。
	const FCatInventorySlotView* FindCurrentInventorySlot(const FCatInventoryViewState& State,
		const FCatInventorySlotView& Candidate)
	{
		if (Candidate.SlotSource != ECatInventorySlotSource::InventoryObject
			|| Candidate.InventorySlotIndex == INDEX_NONE)
		{
			return nullptr;
		}
		if (State.Slots.IsValidIndex(Candidate.SlotIndex))
		{
			const FCatInventorySlotView& IndexedSlot = State.Slots[Candidate.SlotIndex];
			if (IndexedSlot.SlotSource == ECatInventorySlotSource::InventoryObject
				&& IndexedSlot.InventorySlotIndex == Candidate.InventorySlotIndex)
			{
				return &IndexedSlot;
			}
		}
		return State.Slots.FindByPredicate([&Candidate](const FCatInventorySlotView& Slot)
		{
			return Slot.SlotSource == ECatInventorySlotSource::InventoryObject
				&& Slot.InventorySlotIndex == Candidate.InventorySlotIndex;
		});
	}
}

// 绑定流程：
// 1. 先解除旧页面，避免同一个 Controller 上留下旧输入绑定。
// 2. 保存 LocalPlayer、Controller、Model 和 View，并订阅 Model、关闭、格子和动作意图。
// 3. 安装库存开关 Action；缺资产时只降级快捷键，不创建第二套 InputContext。
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
	ViewSlotDropHandle = InView->OnSlotDropRequested.AddUObject(
		this, &ThisClass::HandleViewSlotDropRequested);
	ViewActionHandle = InView->OnInventoryActionRequested.AddUObject(this, &ThisClass::HandleViewActionRequested);
	InstallInventoryInput();
	HandleModelViewStateChanged();
	return true;
}

// 解绑流程：先关闭打开中的库存并恢复输入，再移除 Action、外部容器上下文和所有委托；最后清弱引用和本地状态。
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
		Model->ClearExternalContainerContexts();
	}
	if (UCatInventoryWidget* View = BoundView.Get())
	{
		View->OnCloseRequested.Remove(ViewCloseHandle);
		View->OnSlotSelectionRequested.Remove(ViewSlotSelectionHandle);
		View->OnSlotPointerRequested.Remove(ViewSlotPointerHandle);
		View->OnSlotDropRequested.Remove(ViewSlotDropHandle);
		View->OnInventoryActionRequested.Remove(ViewActionHandle);
		View->RemoveFromParent();
	}
	ModelViewChangedHandle.Reset();
	ViewCloseHandle.Reset();
	ViewSlotSelectionHandle.Reset();
	ViewSlotPointerHandle.Reset();
	ViewSlotDropHandle.Reset();
	ViewActionHandle.Reset();
	BoundLocalPlayer.Reset();
	BoundPlayerController.Reset();
	BoundModel.Reset();
	BoundView.Reset();
	ModalInputModeState = FCatUIModalInputModeState();
}

// 切换流程：普通按键打开前清空外部容器上下文；随后把真实打开/关闭生命周期交给统一 SetInventoryOpen。
void UCatInventoryPageController::ToggleInventory()
{
	UCatInventoryModel* Model = BoundModel.Get();
	if (!bInventoryOpen && Model)
	{
		Model->ClearExternalContainerContexts();
	}
	SetInventoryOpen(!bInventoryOpen);
}

// 交互打开流程：先把交互对象贡献的外部容器读源交给 Model，再保证库存处于打开态；已打开时只刷新，不重置输入锁。
void UCatInventoryPageController::OpenInventoryWithExternalContainerContexts(
	const TArray<UCatContainerReplicationComponent*>& ExternalContainers)
{
	if (UCatInventoryModel* Model = BoundModel.Get())
	{
		Model->SetExternalContainerContexts(ExternalContainers);
	}
	SetInventoryOpen(true);
}

// 状态查询流程：返回 PageController 的唯一打开状态，不读取 Widget 可见性或鼠标状态拼第二份真相。
bool UCatInventoryPageController::IsInventoryOpen() const
{
	return bInventoryOpen;
}

// 外部刷新流程：只要求 Model 重读本人随身库存、当前选择和已绑定容器快照；PageController 不缓存任何后端事实。
void UCatInventoryPageController::RefreshModel()
{
	if (UCatInventoryModel* Model = BoundModel.Get())
	{
		Model->Refresh();
	}
}

// 输入刷新流程：Controller 通知输入链重新就绪时重跑同一套安装逻辑；安装函数会先移除旧绑定，因此重复调用不会叠加快捷键。
void UCatInventoryPageController::RefreshInputBinding()
{
	InstallInventoryInput();
}

// 渲染转交流程：Model 已聚合完整库存投影；PageController 只转交给 WBP。
void UCatInventoryPageController::HandleModelViewStateChanged()
{
	UCatInventoryModel* Model = BoundModel.Get();
	UCatInventoryWidget* View = BoundView.Get();
	if (Model && View)
	{
		View->RenderInventory(Model->GetViewState());
	}
}

// 关闭意图流程：只有库存打开时才切换关闭，迟到关闭点击不会反向打开。
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

// 格子上下文流程：
// 1. 任何右键或拖拽入口都先同步 Model 选择，让 View 的选中框和说明文本跟随最新格子。
// 2. 只有右键、当前没有 pending、且目标仍是有效随身库存物品时，才继续构造钓具选择命令。
// 3. 根据格子的装备类别只替换当前组合中的一项；鱼竿、鱼饵、鱼漂三项不完整时本地拒绝，避免提交半套钓鱼选择。
// 4. 写 pending 之后再调用 PlayerController RPC；服务器会重读目录、解锁、Revision 和库存持有量，UI 不直接改选择。
void UCatInventoryPageController::HandleViewSlotPointerRequested(const int32 SlotIndex,
	const ECatInventorySlotPointerAction PointerAction)
{
	HandleViewSlotSelectionRequested(SlotIndex);
	if (PointerAction != ECatInventorySlotPointerAction::Context)
	{
		return;
	}
	UCatInventoryModel* Model = BoundModel.Get();
	ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(BoundPlayerController.Get());
	if (!Model || !CatController)
	{
		return;
	}
	const FCatInventoryViewState& State = Model->GetViewState();
	if (State.bActionPending || !State.Slots.IsValidIndex(SlotIndex))
	{
		return;
	}
	const FCatInventorySlotView& Slot = State.Slots[SlotIndex];
	if (Slot.SlotSource != ECatInventorySlotSource::InventoryObject || !Slot.bOccupied
		|| Slot.EquipmentDefinitionId.IsNone())
	{
		return;
	}
	FName RodDefinitionId = State.Equipment.RodDefinitionId;
	FName BaitDefinitionId = State.Equipment.BaitDefinitionId;
	FName FloatDefinitionId = State.Equipment.FloatDefinitionId;
	FName ScoopNetDefinitionId = State.Equipment.ScoopNetDefinitionId;
	const int64 ExpectedEquipmentRevision = State.Equipment.Revision;
	switch (Slot.EquipmentKind)
	{
	case ECatEquipmentKind::Rod:
		RodDefinitionId = Slot.EquipmentDefinitionId;
		break;
	case ECatEquipmentKind::Bait:
		BaitDefinitionId = Slot.EquipmentDefinitionId;
		break;
	case ECatEquipmentKind::Float:
		FloatDefinitionId = Slot.EquipmentDefinitionId;
		break;
	case ECatEquipmentKind::ScoopNet:
		ScoopNetDefinitionId = Slot.EquipmentDefinitionId;
		break;
	default:
		return;
	}
	const FGuid RequestId = FGuid::NewGuid();
	if (RodDefinitionId.IsNone() || BaitDefinitionId.IsNone() || FloatDefinitionId.IsNone())
	{
		Model->MarkActionRejected(ECatInventoryAction::SelectInventoryFishingItem, RequestId,
			ECatDomainCommandError::InvalidPayload, State.Equipment.Revision);
		return;
	}
	Model->MarkActionSubmitted(ECatInventoryAction::SelectInventoryFishingItem, RequestId);
	if (CatController->HasAuthority())
	{
		CatController->ServerConfigureEquipment_Implementation(RequestId, ExpectedEquipmentRevision,
			RodDefinitionId, BaitDefinitionId, FloatDefinitionId, ScoopNetDefinitionId);
	}
	else
	{
		CatController->ServerConfigureEquipment(RequestId, ExpectedEquipmentRevision,
			RodDefinitionId, BaitDefinitionId, FloatDefinitionId, ScoopNetDefinitionId);
	}
}

// Drop 提交流程：
// 1. 从最新 ViewState 复核源物体和目标格；已有请求等待回包时只忽略新的 Drop，避免覆盖旧 pending。
// 2. 随身库存格之间走 Equipment 数组整理，并要求拖拽开始时的 Revision 和源格内容仍匹配；鱼容器格之间走 Items 容器移动，混合来源直接拒绝。
// 3. 同格 Drop 视为无操作直接返回；同容器不同格继续提交服务器整理，不能再当 InvalidPayload 拒绝。
// 4. 在写 pending 前复制完整 RPC 载荷；库存整理提交拖拽开始那版 Revision，容器移动提交各自容器 Revision。
void UCatInventoryPageController::HandleViewSlotDropRequested(const FCatInventorySlotView& SourceSlot,
	const FCatInventorySlotView& TargetSlot)
{
	UCatInventoryModel* Model = BoundModel.Get();
	ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(BoundPlayerController.Get());
	if (!Model || !CatController)
	{
		return;
	}
	const FCatInventoryViewState& State = Model->GetViewState();
	const FGuid RequestId = FGuid::NewGuid();
	if (State.bActionPending)
	{
		return;
	}
	if (SourceSlot.SlotSource == ECatInventorySlotSource::InventoryObject
		|| TargetSlot.SlotSource == ECatInventorySlotSource::InventoryObject)
	{
		const FCatInventorySlotView* CurrentSource = FindCurrentInventorySlot(State, SourceSlot);
		const FCatInventorySlotView* CurrentTarget = FindCurrentInventorySlot(State, TargetSlot);
		if (!CurrentSource || !CurrentTarget || !CurrentSource->bOccupied
			|| CurrentSource->InventorySlotIndex == INDEX_NONE || CurrentTarget->InventorySlotIndex == INDEX_NONE
			|| SourceSlot.InventoryRevision != State.Equipment.Revision
			|| CurrentSource->EquipmentDefinitionId != SourceSlot.EquipmentDefinitionId
			|| CurrentSource->Object.StackQuantity != SourceSlot.Object.StackQuantity)
		{
			Model->MarkActionRejected(ECatInventoryAction::MoveInventoryItem, RequestId,
				SourceSlot.InventoryRevision != State.Equipment.Revision
					? ECatDomainCommandError::RevisionConflict : ECatDomainCommandError::InvalidPayload,
				State.Equipment.Revision);
			return;
		}
		if (CurrentSource->InventorySlotIndex == CurrentTarget->InventorySlotIndex)
		{
			return;
		}
		const int32 SubmittedSourceSlotIndex = CurrentSource->InventorySlotIndex;
		const int32 SubmittedTargetSlotIndex = CurrentTarget->InventorySlotIndex;
		const int64 SubmittedEquipmentRevision = SourceSlot.InventoryRevision;
		Model->MarkActionSubmitted(ECatInventoryAction::MoveInventoryItem, RequestId);
		if (CatController->HasAuthority())
		{
			CatController->ServerMoveInventorySlot_Implementation(RequestId, SubmittedEquipmentRevision,
				SubmittedSourceSlotIndex, SubmittedTargetSlotIndex);
		}
		else
		{
			CatController->ServerMoveInventorySlot(RequestId, SubmittedEquipmentRevision,
				SubmittedSourceSlotIndex, SubmittedTargetSlotIndex);
		}
		return;
	}
	const FCatInventorySlotView* CurrentSource = FindCurrentSourceSlot(State, SourceSlot);
	const FCatInventorySlotView* CurrentTarget = FindCurrentTargetSlot(State, TargetSlot);
	const int64 RejectRevision = CurrentSource ? CurrentSource->ContainerRevision : 0;
	if (!CurrentSource || !CurrentTarget || !CurrentSource->bOccupied
		|| CurrentSource->ObjectKind == ECatContainedObjectKind::Unknown
		|| !CurrentSource->ObjectInstanceId.IsValid()
		|| !CurrentSource->ContainerId.IsValid() || !CurrentTarget->ContainerId.IsValid()
		|| CurrentSource->ContainerSlotIndex == INDEX_NONE || CurrentTarget->ContainerSlotIndex == INDEX_NONE)
	{
		Model->MarkActionRejected(ECatInventoryAction::MoveObjectBetweenContainers, RequestId,
			ECatDomainCommandError::InvalidPayload, RejectRevision);
		return;
	}
	if (CurrentSource->ContainerId == CurrentTarget->ContainerId
		&& CurrentSource->ContainerSlotIndex == CurrentTarget->ContainerSlotIndex)
	{
		return;
	}

	const ECatContainedObjectKind SubmittedObjectKind = CurrentSource->ObjectKind;
	const FGuid SubmittedObjectInstanceId = CurrentSource->ObjectInstanceId;
	const FGuid SubmittedSourceContainerId = CurrentSource->ContainerId;
	const ECatContainerKind SubmittedSourceContainerKind = CurrentSource->ContainerKind;
	const int32 SubmittedSourceContainerSlotIndex = CurrentSource->ContainerSlotIndex;
	const int64 SubmittedSourceRevision = CurrentSource->ContainerRevision;
	const FGuid SubmittedTargetContainerId = CurrentTarget->ContainerId;
	const ECatContainerKind SubmittedTargetContainerKind = CurrentTarget->ContainerKind;
	const int32 SubmittedTargetContainerSlotIndex = CurrentTarget->ContainerSlotIndex;
	const int64 SubmittedTargetRevision = CurrentTarget->ContainerRevision;
	Model->MarkActionSubmitted(ECatInventoryAction::MoveObjectBetweenContainers, RequestId);
	if (CatController->HasAuthority())
	{
		CatController->ServerTransferObjectBetweenContainers_Implementation(RequestId,
			SubmittedObjectKind,
			SubmittedObjectInstanceId,
			SubmittedSourceContainerId,
			SubmittedSourceContainerKind,
			SubmittedSourceContainerSlotIndex,
			SubmittedSourceRevision,
			SubmittedTargetContainerId,
			SubmittedTargetContainerKind,
			SubmittedTargetContainerSlotIndex,
			SubmittedTargetRevision);
	}
	else
	{
		CatController->ServerTransferObjectBetweenContainers(RequestId,
			SubmittedObjectKind,
			SubmittedObjectInstanceId,
			SubmittedSourceContainerId,
			SubmittedSourceContainerKind,
			SubmittedSourceContainerSlotIndex,
			SubmittedSourceRevision,
			SubmittedTargetContainerId,
			SubmittedTargetContainerKind,
			SubmittedTargetContainerSlotIndex,
			SubmittedTargetRevision);
	}
}

// 鱼动作按钮流程：
// 1. 本入口只处理吃鱼和献祭这类按钮动作；库存整理走 Drop，钓具选择走格子右键上下文。
// 2. 从 Model 当前 ViewState 读取选中鱼、容器 ID 和 Revision，拒绝空选择或无效鱼护上下文。
// 3. 生成 RequestId 并先写 pending，使同步 authority 回包也能匹配。
// 4. 按动作类型调用 PlayerController 正式服务器入口，绝不让 Widget 直接访问 Items 或 Run。
// 5. Model 或 Controller 已失效时直接丢弃迟到意图；需要 Character 的吃鱼分支无法解析 Pawn 时发布结构化拒绝。
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
	const FCatInventorySlotView* SelectedSlot = State.Slots.IsValidIndex(State.SelectedSlotIndex)
		? &State.Slots[State.SelectedSlotIndex] : nullptr;
	const bool bHasSelectedFish = State.bSelectedFishInFishGuard
		&& State.SelectedFish.FishInstanceId.IsValid() && SelectedSlot
		&& SelectedSlot->ContainerKind == ECatContainerKind::FishGuard
		&& SelectedSlot->ContainerId.IsValid();
	if (!bHasSelectedFish)
	{
		Model->MarkActionRejected(Action, RequestId, ECatDomainCommandError::InvalidPayload,
			SelectedSlot ? SelectedSlot->ContainerRevision : 0);
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
					SelectedSlot->ContainerRevision);
				return;
			}
			FCatFishConsumeCommand Command;
			Command.Context.RequestId = RequestId;
			Command.Context.ExpectedRevision = SelectedSlot->ContainerRevision;
			Command.FishInstanceId = State.SelectedFish.FishInstanceId;
			Command.SourceContainerId = SelectedSlot->ContainerId;
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
	case ECatInventoryAction::SacrificeSelectedFish:
		{
			FCatSacrificeCommand Command;
			Command.Context.RequestId = RequestId;
			Command.Context.ExpectedRevision = SelectedSlot->ContainerRevision;
			Command.FishInstanceId = State.SelectedFish.FishInstanceId;
			Command.ContainerId = SelectedSlot->ContainerId;
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

// 打开状态流程：打开时先入视口再申请模态输入，关闭时先释放输入再移出视口；重复写同一状态保持幂等。
void UCatInventoryPageController::SetInventoryOpen(const bool bOpen)
{
	APlayerController* Controller = BoundPlayerController.Get();
	UCatInventoryModel* Model = BoundModel.Get();
	UCatInventoryWidget* View = BoundView.Get();
	if (!Controller || !Model || !View || bInventoryOpen == bOpen)
	{
		return;
	}
	if (bOpen && !View->IsInViewport())
	{
		View->AddToViewport(10);
	}
	bInventoryOpen = bOpen;
	ApplyInventoryInputMode(bInventoryOpen);
	Model->SetOpen(bInventoryOpen);
	if (!bInventoryOpen)
	{
		View->RemoveFromParent();
		Model->ClearExternalContainerContexts();
	}
}

// 输入安装流程：
// 1. 要求 Controller 当前 InputComponent 是 EnhancedInputComponent。
// 2. 从 UI Settings 加载已有 InputContext 中的库存 Action，并把 IMC 加载作为资产接线校验。
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
			Settings ? *Settings->InventoryToggleAction.ToSoftObjectPath().ToString() : TEXT("None"),
			Settings ? *Settings->GameplayInputMappingContext.ToSoftObjectPath().ToString() : TEXT("None"));
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

// 输入模式流程：打开时交给通用模态 UI 工具设置焦点、鼠标和移动锁；关闭时只释放本库存页面申请过的那一层锁。
void UCatInventoryPageController::ApplyInventoryInputMode(const bool bOpen)
{
	APlayerController* Controller = BoundPlayerController.Get();
	if (bOpen)
	{
		CatUIModalInputMode::Open(Controller, BoundView.Get(), ModalInputModeState);
		return;
	}
	CatUIModalInputMode::Close(Controller, ModalInputModeState);
}
