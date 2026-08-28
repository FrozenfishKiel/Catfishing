#include "UI/Inventory/CatInventoryPageController.h"

#include "Camp/CatCampInventoryActor.h"
#include "Character/CatCharacter.h"
#include "EnhancedInputComponent.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Items/CatContainerReplicationComponent.h"
#include "Logging/CatLog.h"
#include "UI/CatUISettings.h"
#include "UI/Inventory/CatCampInventoryWidget.h"
#include "UI/Inventory/CatInventoryModel.h"
#include "UI/Inventory/CatInventoryTypes.h"
#include "UI/Inventory/CatInventoryWidget.h"
#include "UI/InventorySlot/CatInventorySlotWidget.h"

namespace
{
	// 源格复核流程：Drop 时只在外部容器自己的 Slots 中按源容器、源槽位和物体身份复核，避免拿背包或营地格误当容器格。
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
		return State.ExternalContainerSlots.FindByPredicate([&DragSource](const FCatInventorySlotView& Slot)
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

	// 目标格复核流程：目标可以是空格；Drop 只在外部容器自己的 Slots 中找同一容器槽位，不能回退到其他库存。
	const FCatInventorySlotView* FindCurrentTargetSlot(const FCatInventoryViewState& State,
		const FCatInventorySlotView& DropTarget)
	{
		if (DropTarget.SlotSource != ECatInventorySlotSource::ContainerObject
			|| !DropTarget.ContainerId.IsValid() || DropTarget.ContainerSlotIndex == INDEX_NONE)
		{
			return nullptr;
		}
		return State.ExternalContainerSlots.FindByPredicate([&DropTarget](const FCatInventorySlotView& Slot)
		{
			return Slot.ContainerId == DropTarget.ContainerId
				&& Slot.SlotSource == ECatInventorySlotSource::ContainerObject
				&& Slot.ContainerKind == DropTarget.ContainerKind
				&& Slot.ContainerSlotIndex == DropTarget.ContainerSlotIndex;
		});
	}

	// 运行期库存来源判断流程：随身背包和营地公共仓库都使用 FCatRunInventorySlot，只是宿主不同；UI 复核应先按这一类库存处理。
	bool IsRunInventorySlotSource(const ECatInventorySlotSource SlotSource)
	{
		return SlotSource == ECatInventorySlotSource::InventoryObject
			|| SlotSource == ECatInventorySlotSource::CampInventoryObject;
	}

	// 运行期库存槽位读取流程：把不同宿主的槽位字段统一成可比较的数组下标；非运行期库存来源返回无效下标。
	int32 GetRunInventorySlotIndex(const FCatInventorySlotView& Slot)
	{
		if (Slot.SlotSource == ECatInventorySlotSource::InventoryObject)
		{
			return Slot.InventorySlotIndex;
		}
		if (Slot.SlotSource == ECatInventorySlotSource::CampInventoryObject)
		{
			return Slot.CampInventorySlotIndex;
		}
		return INDEX_NONE;
	}

	// 运行期库存版本读取流程：提交失败或服务器移动前需要把对应宿主的 Revision 带出去，避免用随身背包版本拒绝营地仓库动作。
	int64 GetRunInventoryRevision(const FCatInventoryViewState& State, const ECatInventorySlotSource SlotSource)
	{
		if (SlotSource == ECatInventorySlotSource::CampInventoryObject)
		{
			return State.CampInventoryRevision;
		}
		return State.Equipment.Revision;
	}

	// 同源库存整理判断流程：拖拽整理只改同一份数据源内部顺序；不同库存之间的存取由明确服务器命令修改各自数据源，再靠广播刷新两边 UI。
	bool IsSameRunInventory(const FCatInventorySlotView& SourceSlot, const FCatInventorySlotView& TargetSlot)
	{
		return SourceSlot.SlotSource == TargetSlot.SlotSource
			&& IsRunInventorySlotSource(SourceSlot.SlotSource)
			&& GetRunInventorySlotIndex(SourceSlot) != INDEX_NONE
			&& GetRunInventorySlotIndex(TargetSlot) != INDEX_NONE;
	}

	// 运行期库存格复核流程：按库存来源和宿主内槽位在最新 ViewState 中找同一格；格子内容是否仍可移动由调用方继续判断。
	const FCatInventorySlotView* FindCurrentRunInventorySlot(const FCatInventoryViewState& State,
		const FCatInventorySlotView& Candidate)
	{
		const int32 CandidateSlotIndex = GetRunInventorySlotIndex(Candidate);
		if (!IsRunInventorySlotSource(Candidate.SlotSource) || CandidateSlotIndex == INDEX_NONE)
		{
			return nullptr;
		}
		const TArray<FCatInventorySlotView>* Slots = Candidate.SlotSource == ECatInventorySlotSource::CampInventoryObject
			? &State.CampInventorySlots : &State.InventorySlots;
		return Slots->FindByPredicate([&Candidate, CandidateSlotIndex](const FCatInventorySlotView& Slot)
		{
			return Slot.SlotSource == Candidate.SlotSource
				&& GetRunInventorySlotIndex(Slot) == CandidateSlotIndex;
		});
	}

}

// 绑定流程：
// 1. 先解除当前页面，避免同一个 Controller 上留下重复输入绑定。
// 2. 校验 LocalPlayer，保存 Controller、Model 和默认 View；ViewState 广播由库存 WBP 自己订阅。
// 3. 切换到默认 View；每个库存 WBP 构建时都会自己从 LocalPlayer 解析 Model 并刷新。
// 4. 安装库存开关 Action；缺资产时只降级快捷键，不创建第二套 InputContext。
bool UCatInventoryPageController::Bind(ULocalPlayer* InLocalPlayer, APlayerController* InController,
	UCatInventoryModel* InModel, UCatInventoryWidget* InView)
{
	Unbind();
	if (!InLocalPlayer || !InController || !InModel || !InView)
	{
		return false;
	}
	BoundPlayerController = InController;
	BoundModel = InModel;
	DefaultInventoryView = InView;
	if (!SwitchInventoryView(InView))
	{
		Unbind();
		return false;
	}
	InstallInventoryInput();
	return true;
}

// 解绑流程：先关闭打开中的库存并恢复输入，再移除 Action、当前 View 和外部容器上下文；最后清弱引用和本地状态。
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
		Model->SetOpen(false);
		Model->ClearExternalContainerContexts();
		Model->ClearCampInventoryContext();
	}
	UnbindInventoryView();
	BoundPlayerController.Reset();
	BoundModel.Reset();
	BoundCampInventory.Reset();
	DefaultInventoryView.Reset();
	InteractionInventoryView = nullptr;
	ModalInputModeState = FCatUIModalInputModeState();
}

// 切换流程：普通按键打开前切回默认库存 WBP 并清空外部容器上下文，避免从鱼护箱子关闭后沿用箱子布局。
void UCatInventoryPageController::ToggleInventory()
{
	UCatInventoryModel* Model = BoundModel.Get();
	if (!bInventoryOpen && Model)
	{
		if (SwitchInventoryView(DefaultInventoryView.Get()))
		{
			InteractionInventoryView = nullptr;
		}
		Model->ClearExternalContainerContexts();
		Model->ClearCampInventoryContext();
		BoundCampInventory.Reset();
	}
	SetInventoryOpen(!bInventoryOpen);
}

// 交互打开流程：先切回默认库存 WBP 并丢弃当前营地 Actor；Model 接收外部容器时会清掉营地仓库上下文，只有需要独立页面的世界对象才走 ViewClass 版本。
void UCatInventoryPageController::OpenInventoryWithExternalContainerContexts(
	const TArray<UCatContainerReplicationComponent*>& ExternalContainers)
{
	if (SwitchInventoryView(DefaultInventoryView.Get()))
	{
		InteractionInventoryView = nullptr;
	}
	BoundCampInventory.Reset();
	if (UCatInventoryModel* Model = BoundModel.Get())
	{
		Model->SetExternalContainerContexts(ExternalContainers);
	}
	SetInventoryOpen(true);
}

// 指定 ViewClass 打开流程：
// 1. 从当前 Controller 创建交互对象指定的库存页，并加载同一套库存格 WBP，保证临时页面仍走统一格子和拖拽链路。
// 2. PageController 持有这张临时页；LocalPlayer 不再为鱼护、营地仓库或未来箱子增加专用成员。
// 3. Controller、页面类或格子类缺失时直接返回 false，不创建原生替身或退回默认库存页。
// 4. 新页面创建成功后先清掉上一张交互页，再接管新页，避免失败路径继续显示上一个箱子。
// 5. 复用已创建 View 的打开流程写入外部容器上下文并打开库存；失败时释放临时页。
bool UCatInventoryPageController::OpenInventoryWithExternalContainerContextsUsingViewClass(
	const TArray<UCatContainerReplicationComponent*>& ExternalContainers,
	const TSubclassOf<UCatInventoryWidget> InventoryViewClass)
{
	APlayerController* Controller = BoundPlayerController.Get();
	const UCatUISettings* Settings = GetDefault<UCatUISettings>();
	const TSubclassOf<UCatInventorySlotWidget> InventorySlotViewClass =
		Settings ? Settings->LoadInventorySlotWidgetClass() : nullptr;
	if (!Controller || !InventoryViewClass || !InventorySlotViewClass)
	{
		ClearInteractionInventoryOpenFailure();
		UE_LOG(LogCatUI, Warning,
			TEXT("Event=ui_external_inventory_view_class_unavailable Controller=%s ViewClass=%s Slot=%s"),
			*GetNameSafe(Controller),
			*GetNameSafe(InventoryViewClass.Get()),
			*GetNameSafe(InventorySlotViewClass.Get()));
		return false;
	}

	UCatInventoryWidget* NewInteractionInventoryView = CreateWidget<UCatInventoryWidget>(Controller, InventoryViewClass);
	if (!NewInteractionInventoryView)
	{
		ClearInteractionInventoryOpenFailure();
		UE_LOG(LogCatUI, Warning,
			TEXT("Event=ui_external_inventory_view_create_failed Controller=%s ViewClass=%s"),
			*GetNameSafe(Controller),
			*GetNameSafe(InventoryViewClass.Get()));
		return false;
	}

	ClearInteractionInventoryOpenFailure();
	InteractionInventoryView = NewInteractionInventoryView;
	InteractionInventoryView->SetInventorySlotWidgetClass(InventorySlotViewClass);
	if (!OpenInventoryWithExternalContainerContextsUsingView(ExternalContainers, InteractionInventoryView))
	{
		InteractionInventoryView = nullptr;
		return false;
	}
	return true;
}

// 指定 View 打开流程：
// 1. 要求调用方提供交互对象指定的 WBP；缺少 View 时返回 false，让鱼护箱子这类入口能明确 fail-closed。
// 2. 先切换到指定 WBP；具体布局不由 PageController 参与。
// 3. 再把外部容器读源交给同一个 Model；这一步会清掉营地仓库上下文，切换 WBP 不会生成第二套库存状态。
// 4. 最后打开库存并返回真实打开结果，调用者可据此 fail-closed。
bool UCatInventoryPageController::OpenInventoryWithExternalContainerContextsUsingView(
	const TArray<UCatContainerReplicationComponent*>& ExternalContainers, UCatInventoryWidget* PreferredView)
{
	if (!PreferredView || !SwitchInventoryView(PreferredView))
	{
		return false;
	}
	BoundCampInventory.Reset();
	if (UCatInventoryModel* Model = BoundModel.Get())
	{
		Model->SetExternalContainerContexts(ExternalContainers);
	}
	SetInventoryOpen(true);
	return bInventoryOpen;
}

// 营地公共仓库打开流程：
// 1. 要求调用方传入本次准星命中的公共仓库 Actor 和它自己的独立 WBP 类；无效对象直接失败，不退回默认库存页。
// 2. 用当前 Controller 创建营地仓库独立页面，并加载同一套格子 WBP，保证显示独立但格子事件仍回到统一库存链路。
// 3. 新页面创建成功后先清掉上一张交互临时页，再接管新页，避免失败路径继续显示上一个箱子或上一张页面。
// 4. 把公共仓库上下文交给 Model 后打开页面；右键取用或背包/营地拖放时再把同一个 Actor 交给服务器复核距离和版本。
bool UCatInventoryPageController::OpenCampInventory(ACatCampInventoryActor* CampInventory,
	const TSubclassOf<UCatCampInventoryWidget> InventoryViewClass)
{
	APlayerController* Controller = BoundPlayerController.Get();
	UCatInventoryModel* Model = BoundModel.Get();
	const UCatUISettings* Settings = GetDefault<UCatUISettings>();
	const TSubclassOf<UCatInventorySlotWidget> InventorySlotViewClass =
		Settings ? Settings->LoadInventorySlotWidgetClass() : nullptr;
	if (!Controller || !CampInventory || !Model || !InventoryViewClass || !InventorySlotViewClass)
	{
		ClearInteractionInventoryOpenFailure();
		UE_LOG(LogCatUI, Warning,
			TEXT("Event=ui_camp_inventory_view_class_unavailable Controller=%s Inventory=%s ViewClass=%s Slot=%s"),
			*GetNameSafe(Controller),
			*GetNameSafe(CampInventory),
			*GetNameSafe(InventoryViewClass.Get()),
			*GetNameSafe(InventorySlotViewClass.Get()));
		return false;
	}

	UCatInventoryWidget* NewCampInventoryView = CreateWidget<UCatCampInventoryWidget>(Controller, InventoryViewClass);
	if (!NewCampInventoryView)
	{
		ClearInteractionInventoryOpenFailure();
		UE_LOG(LogCatUI, Warning,
			TEXT("Event=ui_camp_inventory_view_create_failed Controller=%s Inventory=%s ViewClass=%s"),
			*GetNameSafe(Controller),
			*GetNameSafe(CampInventory),
			*GetNameSafe(InventoryViewClass.Get()));
		return false;
	}

	ClearInteractionInventoryOpenFailure();
	InteractionInventoryView = NewCampInventoryView;
	InteractionInventoryView->SetInventorySlotWidgetClass(InventorySlotViewClass);
	if (!SwitchInventoryView(InteractionInventoryView))
	{
		InteractionInventoryView = nullptr;
		return false;
	}
	BoundCampInventory = CampInventory;
	Model->SetCampInventoryContext(CampInventory);
	SetInventoryOpen(true);
	return bInventoryOpen;
}

// 交互库存打开失败清理流程：
// 1. 如果当前页面就是上一次按需创建的交互库存页，先按正常关闭流程释放输入模式和 Model 打开态。
// 2. 再移出临时页并释放强引用，避免失败交互继续显示上一个箱子。
// 3. 清空外部容器和营地公共仓库上下文，并丢掉当前仓库 Actor 弱引用。
// 4. 普通 Tab 库存页不是临时页时只丢弃交互上下文，不强行关闭玩家随身库存页面。
void UCatInventoryPageController::ClearInteractionInventoryOpenFailure()
{
	UCatInventoryWidget* CurrentInteractionView = InteractionInventoryView.Get();
	if (CurrentInteractionView && BoundView.Get() == CurrentInteractionView)
	{
		if (bInventoryOpen)
		{
			SetInventoryOpen(false);
		}
		UnbindInventoryView();
	}
	InteractionInventoryView = nullptr;
	BoundCampInventory.Reset();
	if (UCatInventoryModel* Model = BoundModel.Get())
	{
		Model->ClearExternalContainerContexts();
		Model->ClearCampInventoryContext();
	}
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

// 输入刷新流程：Controller 通知输入链重新就绪时重跑同一套安装逻辑；安装函数会先移除已有绑定，因此重复调用不会叠加快捷键。
void UCatInventoryPageController::RefreshInputBinding()
{
	InstallInventoryInput();
}

// 关闭意图流程：库存 WBP 只表达玩家要关闭；只有库存打开时才切换关闭，迟到关闭点击不会反向打开。
void UCatInventoryPageController::RequestCloseInventoryFromWidget()
{
	if (bInventoryOpen)
	{
		ToggleInventory();
	}
}

// 格子选择流程：PageController 不保存选择状态，只把 SlotView 的来源身份交给 Model 基于对应数据源复核。
void UCatInventoryPageController::RequestSelectInventorySlotFromWidget(const FCatInventorySlotView& Slot)
{
	if (UCatInventoryModel* Model = BoundModel.Get())
	{
		Model->SelectSlot(Slot);
	}
}

// 格子上下文流程：
// 1. 右键入口先同步 Model 选择，让 View 的选中框和说明文本跟随最新格子。
// 2. 如果目标是营地公共仓库格，先按最新 ViewState 复核公共槽位，再提交“取到随身库存”服务器请求；这里不直接改公共仓库格。
// 3. 如果目标是随身库存格，当前没有 pending 且物品有效时，才继续构造钓具选择命令。
// 4. 根据随身格装备类别只替换当前组合中的一项；鱼竿、鱼饵、鱼漂三项不完整时本地拒绝，避免提交半套钓鱼选择。
// 5. 写 pending 之后再调用 PlayerController RPC；服务器会重读目录、解锁、当前快照和库存持有量，UI 只发命令，不直接改选择或公共仓库。
void UCatInventoryPageController::RequestInventorySlotContextFromWidget(const FCatInventorySlotView& Slot)
{
	RequestSelectInventorySlotFromWidget(Slot);
	UCatInventoryModel* Model = BoundModel.Get();
	ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(BoundPlayerController.Get());
	if (!Model || !CatController)
	{
		return;
	}
	const FCatInventoryViewState& State = Model->GetViewState();
	if (State.bActionPending)
	{
		return;
	}
	if (Slot.SlotSource == ECatInventorySlotSource::CampInventoryObject)
	{
		const FGuid RequestId = FGuid::NewGuid();
		ACatCampInventoryActor* CampInventory = BoundCampInventory.Get();
		const FCatInventorySlotView* CurrentSlot = FindCurrentRunInventorySlot(State, Slot);
		if (!CampInventory || !CurrentSlot || !CurrentSlot->bOccupied
			|| CurrentSlot->CampInventorySlotIndex == INDEX_NONE || CurrentSlot->Quantity <= 0
			|| !State.bEquipmentAvailable)
		{
			Model->MarkActionRejected(ECatInventoryAction::WithdrawCampInventoryItem, RequestId,
				ECatDomainCommandError::DependencyUnavailable, State.CampInventoryRevision);
			return;
		}
		const int64 SubmittedCampRevision = CurrentSlot->CampInventoryRevision;
		const int32 SubmittedCampSlotIndex = CurrentSlot->CampInventorySlotIndex;
		const int32 SubmittedQuantity = CurrentSlot->Quantity;
		const int64 SubmittedEquipmentRevision = State.Equipment.Revision;
		Model->MarkActionSubmitted(ECatInventoryAction::WithdrawCampInventoryItem, RequestId);
		if (CatController->HasAuthority())
		{
			CatController->ServerWithdrawCampInventoryItemAtActor_Implementation(CampInventory, RequestId,
				SubmittedCampRevision, SubmittedCampSlotIndex, SubmittedQuantity, SubmittedEquipmentRevision);
		}
		else
		{
			CatController->ServerWithdrawCampInventoryItemAtActor(CampInventory, RequestId,
				SubmittedCampRevision, SubmittedCampSlotIndex, SubmittedQuantity, SubmittedEquipmentRevision);
		}
		return;
	}
	if (Slot.SlotSource != ECatInventorySlotSource::InventoryObject)
	{
		return;
	}
	const FCatInventorySlotView* CurrentSlot = FindCurrentRunInventorySlot(State, Slot);
	if (!CurrentSlot || !CurrentSlot->bOccupied || CurrentSlot->EquipmentDefinitionId.IsNone())
	{
		return;
	}
	FName RodDefinitionId = State.Equipment.RodDefinitionId;
	FName BaitDefinitionId = State.Equipment.BaitDefinitionId;
	FName FloatDefinitionId = State.Equipment.FloatDefinitionId;
	FName ScoopNetDefinitionId = State.Equipment.ScoopNetDefinitionId;
	const int64 ExpectedEquipmentRevision = State.Equipment.Revision;
	switch (CurrentSlot->EquipmentKind)
	{
	case ECatEquipmentKind::Rod:
		RodDefinitionId = CurrentSlot->EquipmentDefinitionId;
		break;
	case ECatEquipmentKind::Bait:
		BaitDefinitionId = CurrentSlot->EquipmentDefinitionId;
		break;
	case ECatEquipmentKind::Float:
		FloatDefinitionId = CurrentSlot->EquipmentDefinitionId;
		break;
	case ECatEquipmentKind::ScoopNet:
		ScoopNetDefinitionId = CurrentSlot->EquipmentDefinitionId;
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
// 1. 从最新 ViewState 复核源物体和目标格；已有请求等待回包时只忽略新的 Drop，避免覆盖上一次 pending。
// 2. 运行期库存格同源时整理本数据源，跨背包和营地时提交一条同时改双方数据源的服务器事务。
// 3. 鱼容器格之间走 Items 容器移动；运行期库存和 Items 容器混拖直接拒绝，避免把两套领域写口塞进一次 Drop。
// 4. 同格 Drop 视为无操作直接返回；同容器不同格继续提交服务器整理，不能再当 InvalidPayload 拒绝。
// 5. 在写 pending 前复制完整 RPC 载荷；随身库存、营地仓库、跨源转移和容器移动分别提交各自的并发前提。
void UCatInventoryPageController::RequestInventorySlotDropFromWidget(const FCatInventorySlotView& SourceSlot,
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
	if (IsRunInventorySlotSource(SourceSlot.SlotSource) || IsRunInventorySlotSource(TargetSlot.SlotSource))
	{
		const bool bSourceIsRunInventory = IsRunInventorySlotSource(SourceSlot.SlotSource);
		const bool bTargetIsRunInventory = IsRunInventorySlotSource(TargetSlot.SlotSource);
		const ECatInventorySlotSource RejectSource = IsRunInventorySlotSource(SourceSlot.SlotSource)
			? SourceSlot.SlotSource : TargetSlot.SlotSource;
		const int64 RejectRevision = GetRunInventoryRevision(State, RejectSource);
		if (!bSourceIsRunInventory || !bTargetIsRunInventory)
		{
			Model->MarkActionRejected(ECatInventoryAction::MoveInventoryItem, RequestId,
				ECatDomainCommandError::InvalidPayload, RejectRevision);
			return;
		}
		const FCatInventorySlotView* CurrentSource = FindCurrentRunInventorySlot(State, SourceSlot);
		const FCatInventorySlotView* CurrentTarget = FindCurrentRunInventorySlot(State, TargetSlot);
		if (!CurrentSource || !CurrentTarget || !CurrentSource->bOccupied
			|| GetRunInventorySlotIndex(*CurrentSource) == INDEX_NONE
			|| GetRunInventorySlotIndex(*CurrentTarget) == INDEX_NONE
			|| CurrentSource->EquipmentDefinitionId != SourceSlot.EquipmentDefinitionId
			|| CurrentSource->Quantity != SourceSlot.Quantity)
		{
			Model->MarkActionRejected(ECatInventoryAction::MoveInventoryItem, RequestId,
				ECatDomainCommandError::InvalidPayload, RejectRevision);
			return;
		}
		const int32 SubmittedSourceSlotIndex = GetRunInventorySlotIndex(*CurrentSource);
		const int32 SubmittedTargetSlotIndex = GetRunInventorySlotIndex(*CurrentTarget);
		const bool bSameRunInventory = IsSameRunInventory(*CurrentSource, *CurrentTarget);
		if (bSameRunInventory && SubmittedSourceSlotIndex == SubmittedTargetSlotIndex)
		{
			return;
		}
		const bool bCrossEquipmentToCamp =
			CurrentSource->SlotSource == ECatInventorySlotSource::InventoryObject
			&& CurrentTarget->SlotSource == ECatInventorySlotSource::CampInventoryObject;
		const bool bCrossCampToEquipment =
			CurrentSource->SlotSource == ECatInventorySlotSource::CampInventoryObject
			&& CurrentTarget->SlotSource == ECatInventorySlotSource::InventoryObject;
		ACatCampInventoryActor* CampInventory = BoundCampInventory.Get();
		if (bSameRunInventory && CurrentSource->SlotSource == ECatInventorySlotSource::CampInventoryObject
			&& !CampInventory)
		{
			Model->MarkActionRejected(ECatInventoryAction::MoveInventoryItem, RequestId,
				ECatDomainCommandError::DependencyUnavailable, RejectRevision);
			return;
		}
		if (!bSameRunInventory && (!CampInventory || (!bCrossEquipmentToCamp && !bCrossCampToEquipment)))
		{
			Model->MarkActionRejected(ECatInventoryAction::MoveInventoryItem, RequestId,
				ECatDomainCommandError::DependencyUnavailable, RejectRevision);
			return;
		}
		Model->MarkActionSubmitted(ECatInventoryAction::MoveInventoryItem, RequestId);
		if (bSameRunInventory && CurrentSource->SlotSource == ECatInventorySlotSource::CampInventoryObject)
		{
			const int64 SubmittedCampRevision = CurrentSource->CampInventoryRevision;
			if (CatController->HasAuthority())
			{
				CatController->ServerMoveCampInventorySlotAtActor_Implementation(CampInventory, RequestId,
					SubmittedCampRevision, SubmittedSourceSlotIndex, SubmittedTargetSlotIndex);
			}
			else
			{
				CatController->ServerMoveCampInventorySlotAtActor(CampInventory, RequestId,
					SubmittedCampRevision, SubmittedSourceSlotIndex, SubmittedTargetSlotIndex);
			}
		}
		else if (bSameRunInventory)
		{
			const int64 SubmittedEquipmentRevision = State.Equipment.Revision;
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
		}
		else if (bCrossEquipmentToCamp)
		{
			const int64 SubmittedCampRevision = CurrentTarget->CampInventoryRevision;
			const int64 SubmittedEquipmentRevision = State.Equipment.Revision;
			if (CatController->HasAuthority())
			{
				CatController->ServerDepositInventoryItemToCampAtActor_Implementation(CampInventory, RequestId,
					SubmittedCampRevision, SubmittedTargetSlotIndex,
					SubmittedEquipmentRevision, SubmittedSourceSlotIndex);
			}
			else
			{
				CatController->ServerDepositInventoryItemToCampAtActor(CampInventory, RequestId,
					SubmittedCampRevision, SubmittedTargetSlotIndex,
					SubmittedEquipmentRevision, SubmittedSourceSlotIndex);
			}
		}
		else
		{
			const int64 SubmittedCampRevision = CurrentSource->CampInventoryRevision;
			const int64 SubmittedEquipmentRevision = State.Equipment.Revision;
			if (CatController->HasAuthority())
			{
				CatController->ServerWithdrawCampInventoryItemToSlotAtActor_Implementation(CampInventory, RequestId,
					SubmittedCampRevision, SubmittedSourceSlotIndex,
					SubmittedEquipmentRevision, SubmittedTargetSlotIndex);
			}
			else
			{
				CatController->ServerWithdrawCampInventoryItemToSlotAtActor(CampInventory, RequestId,
					SubmittedCampRevision, SubmittedSourceSlotIndex,
					SubmittedEquipmentRevision, SubmittedTargetSlotIndex);
			}
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
void UCatInventoryPageController::RequestInventoryActionFromWidget(const ECatInventoryAction Action)
{
	UCatInventoryModel* Model = BoundModel.Get();
	ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(BoundPlayerController.Get());
	if (!Model || !CatController || Action == ECatInventoryAction::None)
	{
		return;
	}
	const FCatInventoryViewState& State = Model->GetViewState();
	const FGuid RequestId = FGuid::NewGuid();
	const FCatInventorySlotView* SelectedSlot = State.bHasSelectedSlot ? &State.SelectedSlot : nullptr;
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

// 打开状态流程：打开时先入视口再申请模态输入，关闭时先释放输入再移出视口；临时外部页面关闭后同步释放强引用，不把专用页面留给下一次默认库存打开。
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
		Model->ClearCampInventoryContext();
		BoundCampInventory.Reset();
		if (InteractionInventoryView.Get() == View)
		{
			InteractionInventoryView = nullptr;
		}
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

// View 切换流程：
// 1. 空 View 直接失败；同一个 View 重复传入时无需重新接线。
// 2. 当前 View 只移出视口；Model、输入和打开状态都保留在 PageController。
// 3. 新 View 保存为当前根页面；如果库存已经打开，再把它加入视口并刷新焦点。
bool UCatInventoryPageController::SwitchInventoryView(UCatInventoryWidget* NewView)
{
	if (!NewView)
	{
		return false;
	}
	if (BoundView.Get() == NewView)
	{
		return true;
	}
	UnbindInventoryView();
	BoundView = NewView;
	if (bInventoryOpen)
	{
		if (!NewView->IsInViewport())
		{
			NewView->AddToViewport(10);
		}
		ApplyInventoryInputMode(true);
	}
	return true;
}

// View 解绑流程：只移出当前根页面；页面自己的 Model 监听由 Widget 构建和销毁生命周期配对处理。
void UCatInventoryPageController::UnbindInventoryView()
{
	if (UCatInventoryWidget* View = BoundView.Get())
	{
		View->RemoveFromParent();
	}
	BoundView.Reset();
}
