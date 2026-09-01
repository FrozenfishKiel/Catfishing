#include "UI/Inventory/CatInventoryModel.h"

#include "Camp/CatCampInventoryActor.h"
#include "Character/CatCharacter.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Engine/World.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/PlayerController.h"
#include "Items/CatContainerReplicationComponent.h"
#include "Logging/CatLog.h"
#include "UI/CatUISettings.h"

namespace
{
	// 库存动作标签流程：只把动作枚举转成人类可读反馈，不参与服务器命令选择。
	FString GetInventoryActionDisplayText(const ECatInventoryAction Action)
	{
		switch (Action)
		{
		case ECatInventoryAction::ConsumeSelectedFish:
			return TEXT("吃鱼");
		case ECatInventoryAction::MoveObjectBetweenContainers:
			return TEXT("移动物体");
		case ECatInventoryAction::MoveInventoryItem:
			return TEXT("整理或转移库存");
		case ECatInventoryAction::SelectInventoryFishingItem:
			return TEXT("选择钓具");
		case ECatInventoryAction::WithdrawCampInventoryItem:
			return TEXT("取出公共物品");
		case ECatInventoryAction::SacrificeSelectedFish:
			return TEXT("献祭");
		case ECatInventoryAction::StoreSelectedFishInSharedTank:
			return TEXT("存入鱼缸");
		case ECatInventoryAction::None:
		default:
			return TEXT("无");
		}
	}

	// 领域错误标签流程：常见错误转中文，其他枚举保留原名方便按日志定位。
	FString GetInventoryDomainErrorDisplayText(const ECatDomainCommandError Error)
	{
		switch (Error)
		{
		case ECatDomainCommandError::None:
			return TEXT("成功");
		case ECatDomainCommandError::InvalidPayload:
			return TEXT("请求内容无效");
		case ECatDomainCommandError::DependencyUnavailable:
			return TEXT("当前缺少可用目标");
		case ECatDomainCommandError::NotFound:
			return TEXT("目标不存在");
		case ECatDomainCommandError::RevisionConflict:
			return TEXT("状态已变化，请再试一次");
		case ECatDomainCommandError::PermissionDenied:
			return TEXT("现在不能这样做");
		case ECatDomainCommandError::CommandsClosed:
			return TEXT("本阶段已关闭");
		case ECatDomainCommandError::InvalidPhase:
			return TEXT("当前动作还不能执行");
		case ECatDomainCommandError::CapacityExceeded:
			return TEXT("目标已满");
		case ECatDomainCommandError::PolicyUndecided:
			return TEXT("规则暂未开放");
		case ECatDomainCommandError::AlreadyResolved:
			return TEXT("请求已处理");
		default:
			return UEnum::GetValueAsString(Error);
		}
	}

	// 容器物体类别标签流程：只服务库存文本和调试反馈，不参与服务器策略选择。
	FString GetContainedObjectKindDisplayText(const ECatContainedObjectKind Kind)
	{
		switch (Kind)
		{
		case ECatContainedObjectKind::Fish:
			return TEXT("鱼");
		case ECatContainedObjectKind::Equipment:
			return TEXT("装备");
		case ECatContainedObjectKind::Consumable:
			return TEXT("耗材");
		case ECatContainedObjectKind::Unknown:
		default:
			return TEXT("物体");
		}
	}

	// 装备类别标签流程：只服务随身库存文本，不参与定义校验或钓鱼策略选择。
	FString GetEquipmentKindDisplayText(const ECatEquipmentKind Kind)
	{
		switch (Kind)
		{
		case ECatEquipmentKind::Rod:
			return TEXT("鱼竿");
		case ECatEquipmentKind::Bait:
			return TEXT("鱼饵");
		case ECatEquipmentKind::Float:
			return TEXT("鱼漂");
		case ECatEquipmentKind::ScoopNet:
			return TEXT("抄网");
		case ECatEquipmentKind::Chum:
			return TEXT("窝料");
		case ECatEquipmentKind::Herb:
			return TEXT("草药");
		case ECatEquipmentKind::Driftwood:
			return TEXT("浮木");
		case ECatEquipmentKind::Utility:
			return TEXT("道具");
		case ECatEquipmentKind::Unknown:
		default:
			return TEXT("物品");
		}
	}

	// 装备定义展示流程：未同步或未选择时给玩家可读占位，避免把 None 暴露成内部 ID。
	FString GetDefinitionDisplayText(const FName DefinitionId, const TCHAR* EmptyText)
	{
		return DefinitionId.IsNone() ? FString(EmptyText) : DefinitionId.ToString();
	}

	// 显示名解析流程：定义资产可以在蓝图里配置玩家可见名称；未配置时只回退到稳定 ID，保证 UI 不需要再查数据源。
	FText ResolveInventoryDisplayName(const FText& ConfiguredDisplayName, const FName DefinitionId)
	{
		return ConfiguredDisplayName.IsEmpty() ? FText::FromName(DefinitionId) : ConfiguredDisplayName;
	}

	// 数量角标文本流程：Model 统一决定角标内容，WBP 只绑定 QuantityText 和显隐状态。
	FText MakeInventoryQuantityText(const int32 Quantity, const bool bShowQuantity)
	{
		return bShowQuantity ? FText::FromString(FString::Printf(TEXT("x%d"), Quantity)) : FText::GetEmpty();
	}

	// 堆叠上限解析流程：每个定义可覆盖单格上限；数量型物品未显式配置时按项目默认上限兜底。
	int32 ResolveInventoryMaxStackSize(const UCatEquipmentDefinition* Definition)
	{
		if (!Definition)
		{
			return 1;
		}
		if (Definition->MaxStackSize > 0)
		{
			return FMath::Max(1, Definition->MaxStackSize);
		}
		if (!Definition->bRunConsumable)
		{
			return 1;
		}
		const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
		const int32 ConfiguredLimit = Settings ? Settings->InventoryQuantityStackCapacity : 0;
		return ConfiguredLimit > 0 ? ConfiguredLimit : MAX_int32;
	}

	// 数量表现写入流程：后端格子只给 DefinitionId 和 Quantity，所有 UI 可读的堆叠状态都在这里生成。
	void ApplyInventoryQuantityPresentation(FCatInventorySlotView& Slot, const int32 Quantity, const int32 MaxStackSize)
	{
		Slot.Quantity = FMath::Max(0, Quantity);
		Slot.MaxStackSize = FMath::Max(1, MaxStackSize);
		Slot.bStackable = Slot.MaxStackSize > 1;
		Slot.bShowQuantity = Slot.Quantity > 1;
		Slot.QuantityText = MakeInventoryQuantityText(Slot.Quantity, Slot.bShowQuantity);
	}

	// 当前选择文案流程：只读 Equipment 快照中的钓鱼选择；文案刻意叫“选择”而不是“装备槽”，避免 UI 继续暗示独立装备栏。
	FText MakeSelectionText(const FCatEquipmentLoadoutSnapshot& Equipment, const bool bEquipmentAvailable)
	{
		if (!bEquipmentAvailable)
		{
			return FText::FromString(TEXT("当前选择：等待同步"));
		}
		const FString RodState = Equipment.bRodBroken ? TEXT("，已断") : TEXT("");
		return FText::FromString(FString::Printf(TEXT("当前选择：鱼竿 %s（耐久 %.0f%s） | 鱼饵 %s | 鱼漂 %s"),
			*GetDefinitionDisplayText(Equipment.RodDefinitionId, TEXT("未选择")),
			Equipment.RodDurability,
			*RodState,
			*GetDefinitionDisplayText(Equipment.BaitDefinitionId, TEXT("未选择")),
			*GetDefinitionDisplayText(Equipment.FloatDefinitionId, TEXT("未选择"))));
	}

	// 随身库存格数流程：配置容量决定玩家看到的固定空格；复制快照数组更长时以快照为准，UI 不截断已经同步下来的真实槽位。
	int32 GetInventorySlotCountForView(const FCatEquipmentLoadoutSnapshot& Equipment)
	{
		const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
		const int32 ConfiguredSlotCount = Settings ? FMath::Max(0, Settings->InventorySlotCapacity) : 0;
		return FMath::Max(ConfiguredSlotCount, Equipment.InventorySlots.Num());
	}

	// 随身库存占用计数流程：只数格子数组里的有效格；数量堆叠不会被误当成多个可拖拽格子。
	int32 CountOccupiedInventorySlots(const FCatEquipmentLoadoutSnapshot& Equipment)
	{
		int32 Count = 0;
		for (const FCatRunInventorySlot& Slot : Equipment.InventorySlots)
		{
			if (!Slot.DefinitionId.IsNone() && Slot.Quantity > 0)
			{
				++Count;
			}
		}
		return Count;
	}

	// 格子数组占用计数流程：公共仓库和随身库存共享带实例身份的运行槽位结构，摘要只需要数有效格，不解释物品权限。
	int32 CountOccupiedRunInventorySlots(const TArray<FCatRunInventorySlot>& InventorySlots)
	{
		int32 Count = 0;
		for (const FCatRunInventorySlot& Slot : InventorySlots)
		{
			if (!Slot.DefinitionId.IsNone() && Slot.Quantity > 0)
			{
				++Count;
			}
		}
		return Count;
	}

	// 随身库存种类计数流程：用定义 ID 去重，只服务摘要文案，不参与任何库存容量或交易判断。
	int32 CountActiveInventoryItemKinds(const FCatEquipmentLoadoutSnapshot& Equipment)
	{
		TSet<FName> DefinitionIds;
		for (const FCatRunInventorySlot& Slot : Equipment.InventorySlots)
		{
			if (!Slot.DefinitionId.IsNone() && Slot.Quantity > 0)
			{
				DefinitionIds.Add(Slot.DefinitionId);
			}
		}
		return DefinitionIds.Num();
	}

	// 随身库存文案流程：只给固定格数组做概要，具体内容留给 Slots，避免把库存重新退化成“物品 x 数量”的列表。
	FText MakeInventoryItemsText(const FCatEquipmentLoadoutSnapshot& Equipment, const bool bEquipmentAvailable)
	{
		if (!bEquipmentAvailable)
		{
			return FText::FromString(TEXT("随身库存：等待同步"));
		}
		const int32 SlotCount = GetInventorySlotCountForView(Equipment);
		if (SlotCount <= 0)
		{
			return FText::FromString(TEXT("随身库存：暂无可用格子"));
		}
		return FText::FromString(FString::Printf(TEXT("随身库存：%d/%d 格有物品，%d 种；具体内容见格子"),
			CountOccupiedInventorySlots(Equipment),
			SlotCount,
			CountActiveInventoryItemKinds(Equipment)));
	}

}

// 绑定流程：
// 1. 先解绑上一来源，避免换 Pawn 后同一个 Model 同时订阅上一装备或外部容器。
// 2. 校验 LocalPlayer、Controller 与 Character；鱼护箱子只通过交互外部容器上下文进入页面。
// 3. 订阅随身库存快照变化和 PlayerController 的吃鱼、容器移动、钓具选择、献祭结果。
// 4. 发布首份 ViewState，让库存第一次打开时已有随身物品、当前外部容器以及可选营地仓库数据。
bool UCatInventoryModel::Bind(ULocalPlayer* InLocalPlayer, APlayerController* InController,
	ACatCharacter* InCharacter)
{
	Unbind();
	if (!InLocalPlayer || !InController || !InCharacter || InController->GetPawn() != InCharacter)
	{
		return false;
	}

	BoundLocalPlayer = InLocalPlayer;
	BoundPlayerController = InController;
	if (UCatEquipmentComponent* Equipment = InCharacter->GetEquipmentComponent())
	{
		BoundEquipment = Equipment;
		EquipmentChangedHandle = Equipment->OnSnapshotChanged.AddUObject(this, &ThisClass::HandleEquipmentSnapshotChanged);
	}
	if (ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(InController))
	{
		CampCommandResultHandle = CatController->OnCampCommandResultReceived.AddUObject(
			this, &ThisClass::HandleCampCommandResult);
		SacrificeResultHandle = CatController->OnSacrificeResultReceived.AddUObject(
			this, &ThisClass::HandleSacrificeResult);
		FishConsumeResultHandle = CatController->OnFishConsumeResultReceived.AddUObject(
			this, &ThisClass::HandleFishConsumeResult);
	}
	Refresh();
	return true;
}

// 解绑流程：按保存的句柄解除外部容器、随身库存和 Controller 结果订阅，再清空 pending、结果和 ViewState，避免上一角色事实跨 Pawn 泄漏。
void UCatInventoryModel::Unbind()
{
	ClearExternalContainerBindings();
	ClearCampInventoryBinding();
	if (UCatEquipmentComponent* Equipment = BoundEquipment.Get())
	{
		Equipment->OnSnapshotChanged.Remove(EquipmentChangedHandle);
	}
	if (ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(BoundPlayerController.Get()))
	{
		CatController->OnCampCommandResultReceived.Remove(CampCommandResultHandle);
		CatController->OnSacrificeResultReceived.Remove(SacrificeResultHandle);
		CatController->OnFishConsumeResultReceived.Remove(FishConsumeResultHandle);
	}
	EquipmentChangedHandle.Reset();
	CampCommandResultHandle.Reset();
	SacrificeResultHandle.Reset();
	FishConsumeResultHandle.Reset();
	BoundLocalPlayer.Reset();
	BoundPlayerController.Reset();
	BoundEquipment.Reset();
	bOpen = false;
	PendingAction = ECatInventoryAction::None;
	PendingRequestId.Invalidate();
	bActionPending = false;
	LastAction = ECatInventoryAction::None;
	LastCommandResult = FCatDomainCommandResult();
	bHasCommandResult = false;
	LastConsumeResult = FCatFishConsumeResult();
	LastSacrificeResult = FCatSacrificeResult();
	ViewState = FCatInventoryViewState();
}

// 绑定状态查询流程：同时要求本地玩家和 Controller 有效；各复制读源按弱引用独立失效，Refresh 会降级为空快照。
bool UCatInventoryModel::IsBound() const
{
	return BoundLocalPlayer.IsValid()
		&& BoundPlayerController.IsValid();
}

// 打开状态流程：PageController 是唯一写者；状态变化后重建 ViewState，让 WBP 可见性和键盘关闭条件同步。
void UCatInventoryModel::SetOpen(const bool bInOpen)
{
	if (bOpen == bInOpen)
	{
		return;
	}
	bOpen = bInOpen;
	Refresh();
}

// 外部容器上下文流程：
// 1. 先解除上一外部容器订阅，保证一次打开只显示本次交互对象贡献的容器集合。
// 2. 再解除营地公共仓库上下文，避免鱼容器页面和公共仓库页面同时残留。
// 3. 跳过空组件和重复组件，避免同一容器在 WrapBox 中出现两份。
// 4. 为每个外部容器订阅复制变化；任意容器刷新都会让库存重建外部容器自己的 Slots。
void UCatInventoryModel::SetExternalContainerContexts(
	const TArray<UCatContainerReplicationComponent*>& InExternalContainers)
{
	ClearExternalContainerBindings();
	ClearCampInventoryBinding();
	TSet<const UCatContainerReplicationComponent*> SeenComponents;
	for (UCatContainerReplicationComponent* Container : InExternalContainers)
	{
		if (!Container || SeenComponents.Contains(Container))
		{
			continue;
		}
		SeenComponents.Add(Container);
		FExternalContainerBinding& Binding = BoundExternalContainers.AddDefaulted_GetRef();
		Binding.Component = Container;
		Binding.SnapshotChangedHandle = Container->OnSnapshotChanged.AddUObject(
			this, &ThisClass::HandleExternalContainerSnapshotChanged);
	}
	Refresh();
}

// 外部容器清理流程：普通库存打开时显式清空上下文；如果当前已经没有外部绑定，只刷新一次以清掉上一 UI 投影。
void UCatInventoryModel::ClearExternalContainerContexts()
{
	ClearExternalContainerBindings();
	Refresh();
}

// 营地公共仓库上下文流程：
// 1. 先解除鱼容器和上一公共仓库订阅，让本次页面只展示一个明确的世界库存上下文。
// 2. 绑定有效公共仓库并订阅它的快照变化；空仓库也允许打开，因为容量由 Actor 配置决定。
// 3. 最后刷新完整投影；随身背包和公共仓库仍是两份独立 Slots，只是监听者会同时收到刷新。
void UCatInventoryModel::SetCampInventoryContext(ACatCampInventoryActor* InCampInventory)
{
	ClearExternalContainerBindings();
	ClearCampInventoryBinding();
	if (InCampInventory)
	{
		BoundCampInventory = InCampInventory;
		CampInventoryChangedHandle = InCampInventory->OnSnapshotChanged.AddUObject(
			this, &ThisClass::HandleCampInventorySnapshotChanged);
	}
	Refresh();
}

// 营地公共仓库清理流程：关闭或切换上下文时解绑公共仓库事件并刷新一次，确保上一公共格不会留在默认库存页里。
void UCatInventoryModel::ClearCampInventoryContext()
{
	ClearCampInventoryBinding();
	Refresh();
}

// 提交标记流程：记录动作和 RequestId 只服务防重复与回包匹配；这里不刷新 ViewState，避免真实库存未变化前重建同屏库存格。
void UCatInventoryModel::MarkActionSubmitted(const ECatInventoryAction Action, const FGuid RequestId)
{
	if (Action == ECatInventoryAction::None || !RequestId.IsValid())
	{
		return;
	}
	PendingAction = Action;
	PendingRequestId = RequestId;
	bActionPending = true;
	LastAction = ECatInventoryAction::None;
	LastCommandResult = FCatDomainCommandResult();
	bHasCommandResult = false;
	LastConsumeResult = FCatFishConsumeResult();
	LastSacrificeResult = FCatSacrificeResult();
}

// 本地拒绝流程：PageController 无法构造正式服务器命令时只解除 pending 并落日志；无效点击或拖放不是库存数据变化。
void UCatInventoryModel::MarkActionRejected(const ECatInventoryAction Action, const FGuid RequestId,
	const ECatDomainCommandError Error, const int64 Revision)
{
	if (Action == ECatInventoryAction::None || !RequestId.IsValid())
	{
		return;
	}
	UE_LOG(LogCatUI, Warning,
		TEXT("Event=ui_inventory_action_rejected_without_refresh Action=%s Request=%s Error=%s Revision=%lld"),
		*UEnum::GetValueAsString(Action),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		*UEnum::GetValueAsString(Error),
		Revision);
	PendingAction = ECatInventoryAction::None;
	PendingRequestId.Invalidate();
	bActionPending = false;
	LastAction = ECatInventoryAction::None;
	LastCommandResult = FCatDomainCommandResult();
	bHasCommandResult = false;
	LastConsumeResult = FCatFishConsumeResult();
	LastSacrificeResult = FCatSacrificeResult();
}

// pending 查询流程：PageController 在提交新命令前读取这份轻量状态；它不改变 ViewState，也不会触发任何 WBP 刷新。
bool UCatInventoryModel::IsActionPending() const
{
	return bActionPending;
}

// 刷新流程：
// 1. 从 Equipment 读取随身库存格数组和当前钓鱼选择，写入 InventorySlots 这一份独立数据源投影。
// 2. 从当前外部容器复制组件读取容量、Revision 和容器物体投影，写入 ExternalContainerSlots。
// 3. 从当前营地公共仓库读取装备/耗材格，写入 CampInventorySlots；它和随身背包不会共享显示数组。
// 4. 写入打开态、pending、结果和摘要文本；格子选择不属于共享数据源，留给各个 WBP 自己叠加。
// 5. 从 UI Settings 解析既有 InputContext 的库存开关键名，最后广播完整投影，所有监听 WBP 都无条件重读自己那份。
void UCatInventoryModel::Refresh()
{
	FCatInventoryViewState NewState;
	if (const UCatEquipmentComponent* Equipment = BoundEquipment.Get())
	{
		NewState.Equipment = Equipment->GetSnapshot();
		NewState.bEquipmentAvailable = true;
	}

	auto AddContainerToView = [this, &NewState](const FCatContainerSnapshot& Snapshot,
		const FText& DisplayName)
	{
		if (!Snapshot.ContainerId.IsValid())
		{
			return;
		}
		const int32 ObjectCount = CatItems::GetContainedObjectCount(Snapshot);
		const int32 SparseSlotCount = FMath::Max(Snapshot.Fish.Num(), Snapshot.Objects.Num());
		const int32 DeclaredSlotCount = Snapshot.Capacity > 0 ? Snapshot.Capacity : SparseSlotCount;
		const int32 EffectiveSlotCount = FMath::Max(FMath::Max(DeclaredSlotCount, ObjectCount), 1);
		FCatInventoryContainerView ContainerView;
		ContainerView.Snapshot = Snapshot;
		ContainerView.DisplayName = DisplayName;
		ContainerView.FirstSlotIndex = NewState.ExternalContainerSlots.Num();
		ContainerView.SlotCount = EffectiveSlotCount;
		ContainerView.bInteractionFishContainer = Snapshot.Kind == ECatContainerKind::FishGuard
			|| Snapshot.Kind == ECatContainerKind::SharedFishTank;
		NewState.Containers.Add(ContainerView);
		const FString ContainerDisplayName = DisplayName.ToString();
		for (int32 ContainerSlotIndex = 0; ContainerSlotIndex < EffectiveSlotCount; ++ContainerSlotIndex)
		{
			NewState.ExternalContainerSlots.Add(MakeSlotView(Snapshot, ContainerSlotIndex,
				NewState.ExternalContainerSlots.Num(), *ContainerDisplayName));
		}
	};

	if (NewState.bEquipmentAvailable)
	{
		NewState.InventorySlotCount = GetInventorySlotCountForView(NewState.Equipment);
		const FCatRunInventorySlot EmptyInventorySlot;
		for (int32 InventorySlotIndex = 0; InventorySlotIndex < NewState.InventorySlotCount; ++InventorySlotIndex)
		{
			const FCatRunInventorySlot& InventorySlot = NewState.Equipment.InventorySlots.IsValidIndex(InventorySlotIndex)
				? NewState.Equipment.InventorySlots[InventorySlotIndex] : EmptyInventorySlot;
			NewState.InventorySlots.Add(MakeInventorySlotView(InventorySlot, InventorySlotIndex));
		}
	}
	int32 ExternalContainerIndex = 0;
	for (const FExternalContainerBinding& Binding : BoundExternalContainers)
	{
		const UCatContainerReplicationComponent* Component = Binding.Component.Get();
		if (!Component)
		{
			continue;
		}
		const FCatContainerSnapshot Snapshot = Component->GetSnapshot();
		AddContainerToView(Snapshot, MakeContainerDisplayName(Snapshot, ExternalContainerIndex));
		++ExternalContainerIndex;
	}
	int32 CampOccupiedSlotsForSummary = 0;
	if (const ACatCampInventoryActor* CampInventory = BoundCampInventory.Get())
	{
		const FCatCampInventorySnapshot& CampSnapshot = CampInventory->GetSnapshot();
		CampOccupiedSlotsForSummary = CountOccupiedRunInventorySlots(CampSnapshot.InventorySlots);
		const int32 CampSlotCount = FMath::Max(
			CampInventory->GetInventorySlotCapacityForView(), CampSnapshot.InventorySlots.Num());
		const FCatRunInventorySlot EmptyCampSlot;
		NewState.bHasCampInventory = true;
		NewState.CampInventorySlotCount = CampSlotCount;
		NewState.CampInventoryRevision = CampSnapshot.Revision;
		for (int32 CampSlotIndex = 0; CampSlotIndex < CampSlotCount; ++CampSlotIndex)
		{
			const FCatRunInventorySlot& CampSlot = CampSnapshot.InventorySlots.IsValidIndex(CampSlotIndex)
				? CampSnapshot.InventorySlots[CampSlotIndex] : EmptyCampSlot;
			NewState.CampInventorySlots.Add(MakeCampInventorySlotView(CampSlot, CampSlotIndex,
				CampSnapshot.Revision));
		}
	}
	NewState.bHasExternalContainers = !NewState.Containers.IsEmpty();
	NewState.bOpen = bOpen;
	NewState.bActionPending = bActionPending;
	NewState.PendingAction = PendingAction;
	NewState.PendingRequestId = PendingRequestId;
	NewState.LastAction = LastAction;
	NewState.LastCommandResult = LastCommandResult;
	NewState.bHasCommandResult = bHasCommandResult;
	NewState.LastConsumeResult = LastConsumeResult;
	NewState.LastSacrificeResult = LastSacrificeResult;
	NewState.bCanSubmitAction = false;
	NewState.bCanStoreSelectedFishInSharedTank = false;
	if (const UCatUISettings* Settings = GetDefault<UCatUISettings>())
	{
		NewState.ToggleKeyName = Settings->ResolveInventoryToggleKeyName();
	}
	NewState.EquipmentText = MakeSelectionText(NewState.Equipment, NewState.bEquipmentAvailable);
	NewState.InventoryItemsText = MakeInventoryItemsText(NewState.Equipment, NewState.bEquipmentAvailable);
	TArray<FString> ContainerSummaries;
	ContainerSummaries.Reserve(NewState.Containers.Num());
	for (const FCatInventoryContainerView& ContainerView : NewState.Containers)
	{
		ContainerSummaries.Add(FString::Printf(TEXT("%s %d/%d"),
			*ContainerView.DisplayName.ToString(),
			CatItems::GetContainedObjectCount(ContainerView.Snapshot),
			ContainerView.SlotCount));
	}
	const FString ContainerSummary = ContainerSummaries.IsEmpty()
		? FString(TEXT("容器 0/0")) : FString::Join(ContainerSummaries, TEXT(" | "));
	const int32 InventorySlotCount = NewState.InventorySlotCount;
	const FString CampSummary = NewState.bHasCampInventory
		? FString::Printf(TEXT("营地库存 %d/%d"), CampOccupiedSlotsForSummary, NewState.CampInventorySlotCount)
		: FString(TEXT("营地库存 未打开"));
	NewState.SummaryText = FText::FromString(FString::Printf(TEXT("库存：库存格 %d/%d，%d 种 | 世界容器：%s | %s"),
		CountOccupiedInventorySlots(NewState.Equipment),
		InventorySlotCount,
		CountActiveInventoryItemKinds(NewState.Equipment),
		*ContainerSummary,
		*CampSummary));
	if (NewState.bHasCampInventory)
	{
		NewState.SelectedFishText = FText::FromString(TEXT("库存操作：点击格子可查看；营地物品可右键取用，也可在背包和营地格之间拖放。"));
	}
	else
	{
		NewState.SelectedFishText = NewState.bHasExternalContainers
			? FText::FromString(TEXT("库存操作：点击外部容器格可查看；选中鱼后可执行鱼动作，拖拽容器格可整理或跨容器移动。"))
			: FText::FromString(TEXT("库存操作：点击格子可查看；拖拽库存格整理或在背包和营地之间转移，点击鱼护格后可执行鱼动作。"));
	}
	if (bActionPending)
	{
		NewState.ResultText = FText::FromString(FString::Printf(TEXT("库存操作：%s，等待服务器确认"),
			*GetInventoryActionDisplayText(PendingAction)));
	}
	else if (bHasCommandResult)
	{
		NewState.ResultText = FText::FromString(FString::Printf(TEXT("库存操作：%s，%s"),
			*GetInventoryActionDisplayText(LastAction),
			*GetInventoryDomainErrorDisplayText(LastCommandResult.Error)));
	}
	else
	{
		NewState.ResultText = FText::FromString(TEXT("库存操作：暂无"));
	}
	ViewState = MoveTemp(NewState);
	OnViewStateChanged.Broadcast();
}

// ViewState 读取流程：返回最近发布的只读投影；调用方不能通过它拿到任何后端对象引用。
const FCatInventoryViewState& UCatInventoryModel::GetViewState() const
{
	return ViewState;
}

// Equipment 变化流程：装备、耗材、耐久都以完整快照为准；真实数据变化会关闭 pending 并触发重读，库存不缓存增量。
void UCatInventoryModel::HandleEquipmentSnapshotChanged()
{
	ClearPendingAfterObservedSourceChange();
	Refresh();
}

// 外部容器变化流程：外部容器只通知“公开快照变了”；真实数据变化会关闭 pending，Model 再统一重读所有容器。
void UCatInventoryModel::HandleExternalContainerSnapshotChanged()
{
	ClearPendingAfterObservedSourceChange();
	Refresh();
}

// 营地公共仓库变化流程：公共仓库只通知快照变化；真实数据变化会关闭 pending，Model 再统一重读随身库存和公共格。
void UCatInventoryModel::HandleCampInventorySnapshotChanged()
{
	ClearPendingAfterObservedSourceChange();
	Refresh();
}

// 公共领域结果流程：非成功终态只解锁本地 pending；成功整理、取用、装备选择或存入鱼缸必须等随身/营地/外部容器快照变化来刷新和解锁。
void UCatInventoryModel::HandleCampCommandResult(const FCatDomainCommandResult& Result)
{
	if (!IsPendingResult(ECatInventoryAction::MoveObjectBetweenContainers, Result.RequestId)
		&& !IsPendingResult(ECatInventoryAction::MoveInventoryItem, Result.RequestId)
		&& !IsPendingResult(ECatInventoryAction::SelectInventoryFishingItem, Result.RequestId)
		&& !IsPendingResult(ECatInventoryAction::WithdrawCampInventoryItem, Result.RequestId)
		&& !IsPendingResult(ECatInventoryAction::StoreSelectedFishInSharedTank, Result.RequestId))
	{
		return;
	}
	const ECatInventoryAction MatchedAction = PendingAction;
	ClearPendingAfterTerminalResultWithoutRefresh(MatchedAction, Result.RequestId, Result.Error);
}

// 献祭结果流程：Items commit 前的非成功终态只解锁本地 pending；鱼已移除或协议完成时等容器快照变化来解锁。
void UCatInventoryModel::HandleSacrificeResult(const FCatSacrificeResult& Result)
{
	if (!IsPendingResult(ECatInventoryAction::SacrificeSelectedFish, Result.RequestId))
	{
		return;
	}
	const bool bItemsMayHaveChanged = Result.bCompleted
		|| Result.Stage == ECatSacrificeStage::ItemsCommitted
		|| Result.Stage == ECatSacrificeStage::RunApplied
		|| Result.Stage == ECatSacrificeStage::Completed;
	if (bItemsMayHaveChanged)
	{
		return;
	}
	ClearPendingAfterTerminalResultWithoutRefresh(ECatInventoryAction::SacrificeSelectedFish,
		Result.RequestId, Result.Error);
}

// 吃鱼结果流程：非成功终态只解锁本地 pending；成功吃鱼的移除事实由 Items 容器快照通知。
void UCatInventoryModel::HandleFishConsumeResult(const FCatFishConsumeResult& Result)
{
	if (!IsPendingResult(ECatInventoryAction::ConsumeSelectedFish, Result.Command.RequestId))
	{
		return;
	}
	ClearPendingAfterTerminalResultWithoutRefresh(ECatInventoryAction::ConsumeSelectedFish,
		Result.Command.RequestId, Result.Command.Error);
}

// pending 匹配流程：动作、pending 标记和 RequestId 同时一致才消费结果；其他 UI 或已过期请求的回包直接忽略。
bool UCatInventoryModel::IsPendingResult(const ECatInventoryAction Action, const FGuid RequestId) const
{
	return bActionPending
		&& PendingAction == Action
		&& RequestId.IsValid()
		&& PendingRequestId == RequestId;
}

// 数据变化后 pending 清理流程：提交只是本地等待状态；一旦任一真实库存读源变化，说明 UI 可以靠快照刷新，不再等结果回包触发第二次重画。
void UCatInventoryModel::ClearPendingAfterObservedSourceChange()
{
	if (!bActionPending)
	{
		return;
	}
	PendingAction = ECatInventoryAction::None;
	PendingRequestId.Invalidate();
	bActionPending = false;
}

// 无快照终态 pending 清理流程：None 表示本次成功可能随后产生真实快照，继续等待；AlreadyResolved 和拒绝都不会再广播库存读源，只解除等待。
void UCatInventoryModel::ClearPendingAfterTerminalResultWithoutRefresh(const ECatInventoryAction Action,
	const FGuid RequestId, const ECatDomainCommandError Error)
{
	if (!IsPendingResult(Action, RequestId)
		|| Error == ECatDomainCommandError::None)
	{
		return;
	}
	if (Error == ECatDomainCommandError::AlreadyResolved)
	{
		UE_LOG(LogCatUI, Log,
			TEXT("Event=ui_inventory_action_result_already_resolved_without_refresh Action=%s Request=%s Error=%s"),
			*UEnum::GetValueAsString(Action),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			*UEnum::GetValueAsString(Error));
	}
	else
	{
		UE_LOG(LogCatUI, Warning,
			TEXT("Event=ui_inventory_action_result_rejected_without_refresh Action=%s Request=%s Error=%s"),
			*UEnum::GetValueAsString(Action),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			*UEnum::GetValueAsString(Error));
	}
	PendingAction = ECatInventoryAction::None;
	PendingRequestId.Invalidate();
	bActionPending = false;
}

// 容器格投影流程：
// 1. 从 Items Snapshot 读取容器身份和格位内容；空格只生成占位表现，不制造对象事实。
// 2. 鱼对象通过鱼定义资产投影名称、说明和缩略图，缺失定义时回退到 FishDefinitionId，保证 WBP 不再自己查鱼表。
// 3. 非鱼对象沿用容器对象里的定义 ID 和数量生成展示字段；这些字段只服务 UI，不改变 Items 容器移动授权。
FCatInventorySlotView UCatInventoryModel::MakeSlotView(const FCatContainerSnapshot& Snapshot,
	const int32 ContainerSlotIndex, const int32 ExternalSlotIndex, const TCHAR* ContainerDisplayName) const
{
	FCatInventorySlotView Slot;
	Slot.SlotIndex = ExternalSlotIndex;
	Slot.SlotSource = ECatInventorySlotSource::ContainerObject;
	Slot.ContainerKind = Snapshot.Kind;
	Slot.ContainerId = Snapshot.ContainerId;
	Slot.ContainerRevision = Snapshot.Revision;
	Slot.ContainerSlotIndex = ContainerSlotIndex;
	ApplyInventoryQuantityPresentation(Slot, 0, 1);
	Slot.bOccupied = CatItems::TryGetContainedObjectAt(Snapshot, ContainerSlotIndex, Slot.Object);
	if (Slot.bOccupied)
	{
		Slot.ObjectKind = Slot.Object.ObjectKind;
		Slot.ObjectInstanceId = Slot.Object.ObjectInstanceId;
		Slot.bCanDrag = Slot.ObjectKind != ECatContainedObjectKind::Unknown && Slot.ObjectInstanceId.IsValid();
		if (Slot.ObjectKind == ECatContainedObjectKind::Fish)
		{
			Slot.Fish = Slot.Object.Fish;
			Slot.DefinitionId = Slot.Fish.FishDefinitionId;
			ApplyInventoryQuantityPresentation(Slot, 1, 1);
			if (const UCatFishCatalogSettings* FishCatalog = GetDefault<UCatFishCatalogSettings>())
			{
				if (const UCatFishDefinition* FishDefinition = FishCatalog->FindRuntimeDefinition(Slot.Fish.FishDefinitionId))
				{
					Slot.DisplayName = ResolveInventoryDisplayName(FishDefinition->DisplayName, Slot.Fish.FishDefinitionId);
					Slot.Description = FishDefinition->Description;
					Slot.Thumbnail = FishDefinition->Thumbnail;
				}
			}
			if (Slot.DisplayName.IsEmpty())
			{
				Slot.DisplayName = FText::FromName(Slot.Fish.FishDefinitionId);
			}
			Slot.DisplayText = FText::FromString(FString::Printf(TEXT("%s\n第 %d 格\n%s\n%.2f kg"),
				ContainerDisplayName,
				ContainerSlotIndex + 1,
				*Slot.DisplayName.ToString(),
				Slot.Fish.WeightKilograms));
		}
		else
		{
			Slot.DefinitionId = Slot.Object.DefinitionId;
			ApplyInventoryQuantityPresentation(Slot, FMath::Max(1, Slot.Object.StackQuantity),
				FMath::Max(1, Slot.Object.StackQuantity));
			Slot.DisplayName = FText::FromName(Slot.Object.DefinitionId);
			Slot.DisplayText = FText::FromString(FString::Printf(TEXT("%s\n第 %d 格\n%s\n%s x%d"),
				ContainerDisplayName,
				ContainerSlotIndex + 1,
				*GetContainedObjectKindDisplayText(Slot.ObjectKind),
				*Slot.DisplayName.ToString(),
				Slot.Quantity));
		}
	}
	else
	{
		Slot.DisplayText = FText::FromString(FString::Printf(TEXT("%s\n第 %d 格\n空"),
			ContainerDisplayName,
			ContainerSlotIndex + 1));
	}
	return Slot;
}

// 随身库存格投影流程：
// 1. 先把本库存内局部下标、库存数组下标、来源、定义 ID 和实例 ID 写入只读 SlotView；空定义或非正数量只生成占位文本。
// 2. 对有效格读取 Equipment 定义，定义缺失时仍展示物品 ID，但类别降级为 Unknown，避免 UI 因数据缺口空白。
// 3. 名称、说明、缩略图和堆叠表现都由定义资产投影出来；WBP 不再绕过 Model 自己查数据源。
// 4. 把当前格的定义、数量和类别同步到通用 Object 字段，让既有 WBP 能复用同一套显示绑定。
FCatInventorySlotView UCatInventoryModel::MakeInventorySlotView(
	const FCatRunInventorySlot& InventorySlot, const int32 InventorySlotIndex) const
{
	FCatInventorySlotView Slot;
	Slot.SlotIndex = InventorySlotIndex;
	Slot.SlotSource = ECatInventorySlotSource::InventoryObject;
	Slot.InventorySlotIndex = InventorySlotIndex;
	Slot.EquipmentDefinitionId = InventorySlot.DefinitionId;
	Slot.DefinitionId = InventorySlot.DefinitionId;
	Slot.InventoryItemInstanceId = InventorySlot.ItemInstanceId;
	ApplyInventoryQuantityPresentation(Slot, 0, 1);
	Slot.bOccupied = !InventorySlot.DefinitionId.IsNone() && InventorySlot.Quantity > 0;
	if (Slot.bOccupied)
	{
		const UCatEquipmentDefinition* Definition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(
			InventorySlot.DefinitionId);
		Slot.EquipmentKind = Definition ? Definition->Kind : ECatEquipmentKind::Unknown;
		const bool bEquipmentType = Slot.EquipmentKind == ECatEquipmentKind::Rod
			|| Slot.EquipmentKind == ECatEquipmentKind::Float
			|| Slot.EquipmentKind == ECatEquipmentKind::ScoopNet;
		Slot.ObjectKind = bEquipmentType ? ECatContainedObjectKind::Equipment : ECatContainedObjectKind::Consumable;
		Slot.Object.ObjectKind = Slot.ObjectKind;
		Slot.Object.DefinitionId = InventorySlot.DefinitionId;
		Slot.Object.StackQuantity = InventorySlot.Quantity;
		ApplyInventoryQuantityPresentation(Slot, InventorySlot.Quantity, ResolveInventoryMaxStackSize(Definition));
		if (Definition)
		{
			Slot.DisplayName = ResolveInventoryDisplayName(Definition->DisplayName, InventorySlot.DefinitionId);
			Slot.Description = Definition->Description;
			Slot.Thumbnail = Definition->Thumbnail;
		}
		if (Slot.DisplayName.IsEmpty())
		{
			Slot.DisplayName = FText::FromName(InventorySlot.DefinitionId);
		}
		Slot.bCanDrag = true;
		const FString QuantityText = Slot.bShowQuantity
			? FString::Printf(TEXT(" %s"), *Slot.QuantityText.ToString()) : FString();
		Slot.DisplayText = FText::FromString(FString::Printf(TEXT("随身库存\n第 %d 格\n%s\n%s%s"),
			InventorySlotIndex + 1,
			*GetEquipmentKindDisplayText(Slot.EquipmentKind),
			*Slot.DisplayName.ToString(),
			*QuantityText));
	}
	else
	{
		Slot.ObjectKind = ECatContainedObjectKind::Unknown;
		Slot.DisplayText = FText::FromString(FString::Printf(TEXT("随身库存\n第 %d 格\n空"),
			InventorySlotIndex + 1));
	}
	return Slot;
}

// 营地公共仓库格投影流程：
// 1. 写入公共仓库槽位下标、快照版本、本仓库局部下标和实例 ID；这些字段只用于展示和取用请求复核。
// 2. 有效格复用装备定义解析名称、类别、说明、缩略图和堆叠角标，保持公共仓库与随身库存表现一致。
// 3. 公共仓库格开放同仓库整理和背包/营地跨源拖放；它不进入 Items 容器移动，所有写入仍走服务器命令。
FCatInventorySlotView UCatInventoryModel::MakeCampInventorySlotView(const FCatRunInventorySlot& InventorySlot,
	const int32 CampSlotIndex, const int64 CampRevision) const
{
	FCatInventorySlotView Slot;
	Slot.SlotIndex = CampSlotIndex;
	Slot.SlotSource = ECatInventorySlotSource::CampInventoryObject;
	Slot.CampInventorySlotIndex = CampSlotIndex;
	Slot.CampInventoryRevision = CampRevision;
	Slot.EquipmentDefinitionId = InventorySlot.DefinitionId;
	Slot.DefinitionId = InventorySlot.DefinitionId;
	Slot.InventoryItemInstanceId = InventorySlot.ItemInstanceId;
	ApplyInventoryQuantityPresentation(Slot, 0, 1);
	Slot.bOccupied = !InventorySlot.DefinitionId.IsNone() && InventorySlot.Quantity > 0;
	if (Slot.bOccupied)
	{
		const UCatEquipmentDefinition* Definition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(
			InventorySlot.DefinitionId);
		Slot.EquipmentKind = Definition ? Definition->Kind : ECatEquipmentKind::Unknown;
		const bool bEquipmentType = Slot.EquipmentKind == ECatEquipmentKind::Rod
			|| Slot.EquipmentKind == ECatEquipmentKind::Float
			|| Slot.EquipmentKind == ECatEquipmentKind::ScoopNet;
		Slot.ObjectKind = bEquipmentType ? ECatContainedObjectKind::Equipment : ECatContainedObjectKind::Consumable;
		Slot.Object.ObjectKind = Slot.ObjectKind;
		Slot.Object.DefinitionId = InventorySlot.DefinitionId;
		Slot.Object.StackQuantity = InventorySlot.Quantity;
		ApplyInventoryQuantityPresentation(Slot, InventorySlot.Quantity, ResolveInventoryMaxStackSize(Definition));
		if (Definition)
		{
			Slot.DisplayName = ResolveInventoryDisplayName(Definition->DisplayName, InventorySlot.DefinitionId);
			Slot.Description = Definition->Description;
			Slot.Thumbnail = Definition->Thumbnail;
		}
		if (Slot.DisplayName.IsEmpty())
		{
			Slot.DisplayName = FText::FromName(InventorySlot.DefinitionId);
		}
		Slot.bCanDrag = true;
		const FString QuantityText = Slot.bShowQuantity
			? FString::Printf(TEXT(" %s"), *Slot.QuantityText.ToString()) : FString();
		Slot.DisplayText = FText::FromString(FString::Printf(TEXT("营地库存\n第 %d 格\n%s\n%s%s"),
			CampSlotIndex + 1,
			*GetEquipmentKindDisplayText(Slot.EquipmentKind),
			*Slot.DisplayName.ToString(),
			*QuantityText));
	}
	else
	{
		Slot.ObjectKind = ECatContainedObjectKind::Unknown;
		Slot.DisplayText = FText::FromString(FString::Printf(TEXT("营地库存\n第 %d 格\n空"),
			CampSlotIndex + 1));
	}
	return Slot;
}

// 容器名称流程：正式鱼容器显示玩家能理解的设施名；未知外部容器才按顺序给稳定 fallback。
FText UCatInventoryModel::MakeContainerDisplayName(const FCatContainerSnapshot& Snapshot,
	const int32 ExternalContainerIndex)
{
	if (Snapshot.Kind == ECatContainerKind::FishGuard)
	{
		return FText::FromString(TEXT("鱼护"));
	}
	if (Snapshot.Kind == ECatContainerKind::SharedFishTank)
	{
		return FText::FromString(TEXT("共享鱼缸"));
	}
	return FText::FromString(FString::Printf(TEXT("外部容器 %d"), ExternalContainerIndex + 1));
}

// 外部绑定清理流程：逐个从原复制组件移除本 Model 的委托句柄，再清空数组；组件失效时只丢本地记录，不延长 Actor 生命周期。
void UCatInventoryModel::ClearExternalContainerBindings()
{
	for (FExternalContainerBinding& Binding : BoundExternalContainers)
	{
		if (UCatContainerReplicationComponent* Component = Binding.Component.Get())
		{
			Component->OnSnapshotChanged.Remove(Binding.SnapshotChangedHandle);
		}
		Binding.SnapshotChangedHandle.Reset();
	}
	BoundExternalContainers.Reset();
}

// 营地公共仓库绑定清理流程：从原 Actor 移除快照变化句柄，再清空弱引用；Actor 失效时只丢本地记录，不创建替代仓库。
void UCatInventoryModel::ClearCampInventoryBinding()
{
	if (ACatCampInventoryActor* CampInventory = BoundCampInventory.Get())
	{
		CampInventory->OnSnapshotChanged.Remove(CampInventoryChangedHandle);
	}
	CampInventoryChangedHandle.Reset();
	BoundCampInventory.Reset();
}
