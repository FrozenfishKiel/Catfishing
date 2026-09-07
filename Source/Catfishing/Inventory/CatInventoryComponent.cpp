#include "Inventory/CatInventoryComponent.h"

#include "GameFramework/Pawn.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/CatInventorySettings.h"
#include "Logging/CatLog.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogCatInventory, Log, All);

// 空格 owner 构造流程：新空格立即知道自己属于哪个库存组件，复制回调和调试输出不再反查宿主。
FCatInventoryEntry::FCatInventoryEntry(UCatInventoryComponent* InSlotOwnerComponent)
	: SlotOwnerComponent(InSlotOwnerComponent)
{
}

// 格子等价判断流程：物品实例身份是运行期唯一主键，堆叠数量变化不会让格子变成另一件物品。
bool FCatInventoryEntry::operator==(const FCatInventoryEntry& Other) const
{
	return Instance == Other.Instance;
}

// 格子实例判断流程：空格或空实例都不匹配，避免清理路径把空指针误判为同一件物品。
bool FCatInventoryEntry::operator==(const UCatInventoryItemInstance* InInstance) const
{
	return Instance != nullptr && InInstance != nullptr && Instance == InInstance;
}

// 格子反向判断流程：复用正向匹配逻辑，保证移动和清理分支没有第二套空值规则。
bool FCatInventoryEntry::operator!=(const UCatInventoryItemInstance* InInstance) const
{
	return !(*this == InInstance);
}

// 列表 owner 构造流程：复制回调需要回到拥有组件广播变化，因此在组件构造时绑定一次。
FCatInventoryList::FCatInventoryList(UCatInventoryComponent* InOwnerComponent)
	: OwnerComponent(InOwnerComponent)
{
}

// 物品快照读取流程：只收集非空实例，调用方拿到的是临时数组而不是可写库存容器。
TArray<UCatInventoryItemInstance*> FCatInventoryList::GetAllItems() const
{
	TArray<UCatInventoryItemInstance*> Results;
	Results.Reserve(Entries.Num());

	for (const FCatInventoryEntry& Entry : Entries)
	{
		if (Entry.Instance != nullptr)
		{
			Results.Add(Entry.Instance);
		}
	}

	return Results;
}

// 复制删除回调流程：更新观察数量后交给组件扩展点；Owner 缺失时只做本地数据修正。
void FCatInventoryList::PreReplicatedRemove(const TArrayView<int32> RemovedIndices, const int32 FinalSize)
{
	for (const int32 Index : RemovedIndices)
	{
		if (Entries.IsValidIndex(Index))
		{
			Entries[Index].LastObservedCount = 0;
		}
	}

	if (OwnerComponent != nullptr)
	{
		OwnerComponent->BroadcastInventoryRemoveOnClient(RemovedIndices, FinalSize);
		OwnerComponent->OnInventoryObservedChanged.Broadcast();
	}
}

// 复制新增回调流程：补齐本地 owner 和运行宿主，再统一广播变化，避免 UI 持有旧归属。
void FCatInventoryList::PostReplicatedAdd(const TArrayView<int32> AddedIndices, const int32 FinalSize)
{
	for (const int32 Index : AddedIndices)
	{
		if (!Entries.IsValidIndex(Index))
		{
			continue;
		}

		FCatInventoryEntry& Entry = Entries[Index];
		Entry.LastObservedCount = Entry.StackCount;
		Entry.SlotOwnerComponent = OwnerComponent;
		if (Entry.Instance != nullptr && OwnerComponent != nullptr)
		{
			Entry.Instance->SetRuntimeOwnerActor(OwnerComponent->GetOwner());
		}
	}

	if (OwnerComponent != nullptr)
	{
		OwnerComponent->BroadcastInventoryAddOnClient(AddedIndices, FinalSize, Entries);
		OwnerComponent->OnInventoryObservedChanged.Broadcast();
	}
}

// 复制变更回调流程：同步观察数量和本地 owner，然后让消费者重读完整库存。
void FCatInventoryList::PostReplicatedChange(const TArrayView<int32> ChangedIndices, const int32 FinalSize)
{
	for (const int32 Index : ChangedIndices)
	{
		if (!Entries.IsValidIndex(Index))
		{
			continue;
		}

		FCatInventoryEntry& Entry = Entries[Index];
		Entry.LastObservedCount = Entry.StackCount;
		Entry.SlotOwnerComponent = OwnerComponent;
		if (Entry.Instance != nullptr && OwnerComponent != nullptr)
		{
			Entry.Instance->SetRuntimeOwnerActor(OwnerComponent->GetOwner());
		}
	}

	if (OwnerComponent != nullptr)
	{
		OwnerComponent->BroadcastInventoryChangeOnClient(ChangedIndices, FinalSize);
		OwnerComponent->OnInventoryObservedChanged.Broadcast();
	}
}

// FastArray 序列化流程：把 Entries 的 delta 交给引擎通用实现，列表本身不增加第二份复制状态。
bool FCatInventoryList::NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
{
	return FFastArraySerializer::FastArrayDeltaSerialize<FCatInventoryEntry, FCatInventoryList>(
		Entries, DeltaParms, *this);
}

// 组件构造流程：启用复制、关闭 Tick，并使用 registered subobject list 管理物品实例生命周期。
UCatInventoryComponent::UCatInventoryComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, InventoryList(this)
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
	bReplicateUsingRegisteredSubObjectList = true;
}

// 初始化流程：确保 FastArray 能回到本组件，并按配置补齐空槽。
void UCatInventoryComponent::InitializeComponent()
{
	Super::InitializeComponent();
	InventoryList.OwnerComponent = this;
	InitializeOrRefreshInventorySlots();
}

// BeginPlay 流程：再执行一次轻量刷新，覆盖蓝图晚设默认值或动态创建组件后的槽位补齐。
void UCatInventoryComponent::BeginPlay()
{
	Super::BeginPlay();
	InventoryList.OwnerComponent = this;
	InitializeOrRefreshInventorySlots();
}

// 复制声明流程：复制库存列表和内容版本；实例对象通过 registered subobject list 单独登记，终态缓存只留在本机内存。
void UCatInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, InventoryList);
	DOREPLIFETIME(ThisClass, InventoryRevision);
}

// 复制就绪流程：服务器把当前所有非空实例登记为子对象，避免 FastArray 指针到客户端后无法解析。
void UCatInventoryComponent::ReadyForReplication()
{
	Super::ReadyForReplication();

	if (!IsUsingRegisteredSubObjectList())
	{
		return;
	}

	for (const FCatInventoryEntry& Entry : InventoryList.Entries)
	{
		if (Entry.Instance != nullptr && IsValid(Entry.Instance))
		{
			AddReplicatedSubObject(Entry.Instance);
		}
	}
}

// 子对象复制流程：registered subobject list 已经持有实例清单，保留父类实现作为唯一复制入口。
bool UCatInventoryComponent::ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch,
	FReplicationFlags* RepFlags)
{
	return Super::ReplicateSubobjects(Channel, Bunch, RepFlags);
}

// 接口实现流程：统一收货发现本组件后直接返回自身，不再创建额外库存宿主对象。
UCatInventoryComponent* UCatInventoryComponent::GetInventoryComponent()
{
	return this;
}

// 槽位刷新流程：只在 authority 或单机构造路径补齐空槽；新增格子会推进内容版本并广播，客户端只通过复制拿到服务器数组。
void UCatInventoryComponent::InitializeOrRefreshInventorySlots()
{
	AActor* OwningActor = GetOwner();
	if (OwningActor != nullptr && !OwningActor->HasAuthority())
	{
		return;
	}

	InventoryList.OwnerComponent = this;
	const int32 DesiredSlotCount = FMath::Max(0, NumSlots);
	bool bChanged = false;
	while (InventoryList.Entries.Num() < DesiredSlotCount)
	{
		InventoryList.Entries.Add(FCatInventoryEntry(this));
		bChanged = true;
	}

	for (FCatInventoryEntry& Entry : InventoryList.Entries)
	{
		Entry.SlotOwnerComponent = this;
	}

	if (bChanged)
	{
		InventoryList.MarkArrayDirty();
		AdvanceInventoryRevisionFromAuthority();
		BroadcastInventoryChange();
	}
}

// 实例创建流程：解析最终实例类、以库存拥有者作为 Outer 创建，再绑定定义和运行宿主。
UCatInventoryItemInstance* UCatInventoryComponent::CreateInventoryItemInstance(
	UCatInventoryItemDefinition* ItemDefinition,
	const TSubclassOf<UCatInventoryItemInstance> ItemInstanceClass)
{
	if (ItemDefinition == nullptr)
	{
		return nullptr;
	}

	AActor* OwningActor = GetOwner();
	if (OwningActor == nullptr)
	{
		return nullptr;
	}

	const TSubclassOf<UCatInventoryItemInstance> ResolvedItemClass =
		UCatInventoryItemDefinition::ResolveItemInstanceClass(ItemDefinition, ItemInstanceClass);
	if (ResolvedItemClass == nullptr)
	{
		return nullptr;
	}

	UCatInventoryItemInstance* NewInstance =
		NewObject<UCatInventoryItemInstance>(OwningActor, ResolvedItemClass);
	if (NewInstance == nullptr)
	{
		return nullptr;
	}

	NewInstance->SetItemDefinition(ItemDefinition);
	NewInstance->SetRuntimeOwnerActor(OwningActor);
	return NewInstance;
}

// 复制登记更新流程：实例离开旧库存时移除旧登记，进入新库存且复制已就绪时登记到新库存。
void UCatInventoryComponent::UpdateReplicatedItemRegistration(UCatInventoryItemInstance* ItemInstance,
	UCatInventoryComponent* PreviousOwnerComponent, UCatInventoryComponent* NewOwnerComponent)
{
	if (ItemInstance == nullptr || PreviousOwnerComponent == NewOwnerComponent)
	{
		return;
	}

	if (PreviousOwnerComponent != nullptr && PreviousOwnerComponent->IsUsingRegisteredSubObjectList())
	{
		PreviousOwnerComponent->RemoveReplicatedSubObject(ItemInstance);
	}

	if (NewOwnerComponent != nullptr
		&& NewOwnerComponent->IsUsingRegisteredSubObjectList()
		&& NewOwnerComponent->IsReadyForReplication())
	{
		NewOwnerComponent->AddReplicatedSubObject(ItemInstance);
	}
}

// 运行宿主同步流程：库存组件拥有者就是实例当前运行归属，空实例不做任何写入。
void UCatInventoryComponent::SyncInventoryItemRuntimeOwner(UCatInventoryItemInstance* ItemInstance) const
{
	if (ItemInstance != nullptr)
	{
		ItemInstance->SetRuntimeOwnerActor(GetOwner());
	}
}

// 整表等价判断流程：
// 1. 先按最低容量计算替换后应有的槽位数量，数量不同就代表 UI 可见格位已经变化。
// 2. 再逐格比较占用状态、实例身份和堆叠数量；空格只要求同为空，不比较临时 owner。
// 3. 函数不读取 Equipment 或玩法定义，保证库存版本只围绕正式库存自己的内容裁决。
bool UCatInventoryComponent::AreInventoryEntriesEquivalent(const TArray<FCatInventoryEntry>& NewEntries,
	const int32 MinimumSlotCount) const
{
	const int32 DesiredSlotCount = FMath::Max(FMath::Max(0, MinimumSlotCount), NewEntries.Num());
	if (InventoryList.Entries.Num() != DesiredSlotCount)
	{
		return false;
	}

	for (int32 SlotIndex = 0; SlotIndex < DesiredSlotCount; ++SlotIndex)
	{
		const FCatInventoryEntry& ExistingEntry = InventoryList.Entries[SlotIndex];
		const FCatInventoryEntry* NewEntry = NewEntries.IsValidIndex(SlotIndex) ? &NewEntries[SlotIndex] : nullptr;
		const bool bExistingOccupied = ExistingEntry.Instance != nullptr && ExistingEntry.StackCount > 0;
		const bool bNewOccupied = NewEntry != nullptr && NewEntry->Instance != nullptr && NewEntry->StackCount > 0;
		if (bExistingOccupied != bNewOccupied)
		{
			return false;
		}
		if (!bExistingOccupied)
		{
			continue;
		}
		if (ExistingEntry.Instance != NewEntry->Instance || ExistingEntry.StackCount != NewEntry->StackCount)
		{
			return false;
		}
	}

	return true;
}

// 版本推进流程：
// 1. 客户端直接退出，避免本地预测或 UI 拖放伪造服务器库存版本。
// 2. 服务器把 0 起始版本推进到至少 1，后续每次真实内容提交单调递增。
// 3. 拥有 Actor 立即请求复制，让 InventoryRevision 和 FastArray 尽快到达观察端。
void UCatInventoryComponent::AdvanceInventoryRevisionFromAuthority()
{
	AActor* OwningActor = GetOwner();
	if (OwningActor != nullptr && !OwningActor->HasAuthority())
	{
		return;
	}

	InventoryRevision = FMath::Max<int64>(1, InventoryRevision + 1);
	if (OwningActor != nullptr)
	{
		OwningActor->ForceNetUpdate();
	}
}

// 库存版本复制流程：版本字段和 FastArray 没有固定先后顺序；统一广播让 UI 重新读完整状态并由 fallback 处理半包。
void UCatInventoryComponent::OnRep_InventoryRevision()
{
	BroadcastInventoryChange();
}

// 按定义入库流程：
// 1. 先拒绝无定义、无数量、非 authority 和运行定义未就绪的请求；失败不会改 Entries 或版本。
// 2. 再填充同类可堆叠格，并同步观察数量、槽位 owner 和实例运行宿主。
// 3. 剩余数量按空格创建新实例，登记复制子对象并把 InOutCount 扣到实际剩余数量。
// 4. 只要接收过至少一份物品就写 bOutFullyAdded；bAdvanceRevision 为 true 时把本次调用作为完整事务推进版本并广播。
UCatInventoryItemInstance* UCatInventoryComponent::AddEntry(
	UCatInventoryItemDefinition* ItemDefinition,
	int32& InOutCount,
	bool& bOutFullyAdded,
	const TSubclassOf<UCatInventoryItemInstance> ItemInstanceClass,
	const bool bAdvanceRevision)
{
	bOutFullyAdded = false;
	if (ItemDefinition == nullptr || InOutCount <= 0 || GetOwner() == nullptr || !GetOwner()->HasAuthority())
	{
		return nullptr;
	}

	if (!ItemDefinition->IsInventoryRuntimeDefinitionReady()
		|| UCatInventoryItemDefinition::ResolveItemInstanceClass(ItemDefinition, ItemInstanceClass) == nullptr)
	{
		return nullptr;
	}

	UCatInventoryItemInstance* FirstAcceptedInstance = nullptr;
	const int32 MaxStackCount = GetMaxStackCountForDefinition(*ItemDefinition);

	if (MaxStackCount > 1)
	{
		for (FCatInventoryEntry& Entry : InventoryList.Entries)
		{
			if (InOutCount <= 0)
			{
				break;
			}

			const UCatInventoryItemDefinition* ExistingDefinition =
				Entry.Instance != nullptr ? Entry.Instance->GetItemDefinition() : nullptr;
			if (ExistingDefinition == nullptr || !ExistingDefinition->CanStackWith(*ItemDefinition))
			{
				continue;
			}

			const int32 AvailableSpace = FMath::Max(0, MaxStackCount - Entry.StackCount);
			const int32 AddAmount = FMath::Min(InOutCount, AvailableSpace);
			if (AddAmount <= 0)
			{
				continue;
			}

			Entry.StackCount += AddAmount;
			Entry.LastObservedCount = Entry.StackCount;
			Entry.SlotOwnerComponent = this;
			SyncInventoryItemRuntimeOwner(Entry.Instance);
			InOutCount -= AddAmount;
			FirstAcceptedInstance = FirstAcceptedInstance != nullptr ? FirstAcceptedInstance : Entry.Instance.Get();
			InventoryList.MarkItemDirty(Entry);
		}
	}

	while (InOutCount > 0)
	{
		const int32 TargetIndex = FindAvailableSlot(nullptr, InOutCount);
		if (TargetIndex == INDEX_NONE)
		{
			break;
		}

		UCatInventoryItemInstance* NewInstance = CreateInventoryItemInstance(ItemDefinition, ItemInstanceClass);
		if (NewInstance == nullptr)
		{
			break;
		}

		FCatInventoryEntry& TargetEntry = InventoryList.Entries[TargetIndex];
		const int32 AddAmount = MaxStackCount > 1 ? FMath::Min(InOutCount, MaxStackCount) : 1;
		TargetEntry.Instance = NewInstance;
		TargetEntry.StackCount = AddAmount;
		TargetEntry.LastObservedCount = AddAmount;
		TargetEntry.SlotOwnerComponent = this;
		InOutCount -= AddAmount;
		FirstAcceptedInstance = FirstAcceptedInstance != nullptr ? FirstAcceptedInstance : NewInstance;

		if (IsUsingRegisteredSubObjectList() && IsReadyForReplication())
		{
			AddReplicatedSubObject(NewInstance);
		}

		InventoryList.MarkItemDirty(TargetEntry);
	}

	bOutFullyAdded = InOutCount == 0;
	if (FirstAcceptedInstance != nullptr)
	{
		if (bAdvanceRevision)
		{
			AdvanceInventoryRevisionFromAuthority();
			BroadcastInventoryChange();
		}
	}

	return FirstAcceptedInstance;
}

// 按实例入库流程：
// 1. 先拒绝空实例、无数量、非 authority 和缺定义的请求；失败不会占用或替换任何格子。
// 2. 堆叠物优先合并到同定义格，并把观察数量、槽位 owner 和运行宿主同步到正式库存事实。
// 3. 剩余数量先放入传入实例，再按同定义补建实例；每个新占用格都会登记复制子对象并扣减 InOutCount。
// 4. 接收过物品后更新 bOutFullyAdded；bAdvanceRevision 为 true 时本次调用自成事务，否则等待外层批次统一推进版本。
void UCatInventoryComponent::AddEntry(UCatInventoryItemInstance* ItemInstance, int32& InOutCount,
	bool& bOutFullyAdded, const bool bAdvanceRevision)
{
	bOutFullyAdded = false;
	if (ItemInstance == nullptr || InOutCount <= 0 || GetOwner() == nullptr || !GetOwner()->HasAuthority())
	{
		return;
	}

	const UCatInventoryItemDefinition* TargetDefinition = ItemInstance->GetItemDefinition();
	if (TargetDefinition == nullptr)
	{
		return;
	}

	UCatInventoryItemInstance* FirstAcceptedInstance = nullptr;
	const int32 MaxStackCount = GetMaxStackCountForDefinition(*TargetDefinition);

	if (MaxStackCount > 1)
	{
		for (FCatInventoryEntry& Entry : InventoryList.Entries)
		{
			if (InOutCount <= 0)
			{
				break;
			}

			const UCatInventoryItemDefinition* ExistingDefinition =
				Entry.Instance != nullptr ? Entry.Instance->GetItemDefinition() : nullptr;
			if (ExistingDefinition == nullptr || !ExistingDefinition->CanStackWith(*TargetDefinition))
			{
				continue;
			}

			const int32 AvailableSpace = FMath::Max(0, MaxStackCount - Entry.StackCount);
			const int32 AddAmount = FMath::Min(InOutCount, AvailableSpace);
			if (AddAmount <= 0)
			{
				continue;
			}

			Entry.StackCount += AddAmount;
			Entry.LastObservedCount = Entry.StackCount;
			Entry.SlotOwnerComponent = this;
			SyncInventoryItemRuntimeOwner(Entry.Instance);
			InOutCount -= AddAmount;
			FirstAcceptedInstance = FirstAcceptedInstance != nullptr ? FirstAcceptedInstance : Entry.Instance.Get();
			InventoryList.MarkItemDirty(Entry);
		}
	}

	bool bOriginalInstanceConsumed = false;
	while (InOutCount > 0)
	{
		const int32 TargetIndex = FindAvailableSlot(nullptr, InOutCount);
		if (TargetIndex == INDEX_NONE)
		{
			break;
		}

		UCatInventoryItemInstance* TargetInstance = nullptr;
		if (!bOriginalInstanceConsumed)
		{
			TargetInstance = ItemInstance;
			bOriginalInstanceConsumed = true;
		}
		else
		{
			TargetInstance = CreateInventoryItemInstance(
				ItemInstance->GetItemDefinition(),
				ItemInstance->GetClass());
		}

		if (TargetInstance == nullptr)
		{
			break;
		}

		FCatInventoryEntry& TargetEntry = InventoryList.Entries[TargetIndex];
		const int32 AddAmount = MaxStackCount > 1 ? FMath::Min(InOutCount, MaxStackCount) : 1;
		TargetEntry.Instance = TargetInstance;
		TargetEntry.StackCount = AddAmount;
		TargetEntry.LastObservedCount = AddAmount;
		TargetEntry.SlotOwnerComponent = this;
		TargetInstance->SetRuntimeOwnerActor(GetOwner());
		InOutCount -= AddAmount;
		FirstAcceptedInstance = FirstAcceptedInstance != nullptr ? FirstAcceptedInstance : TargetInstance;

		if (IsUsingRegisteredSubObjectList() && IsReadyForReplication())
		{
			AddReplicatedSubObject(TargetInstance);
		}

		InventoryList.MarkItemDirty(TargetEntry);
	}

	bOutFullyAdded = InOutCount == 0;
	if (FirstAcceptedInstance != nullptr)
	{
		if (bAdvanceRevision)
		{
			AdvanceInventoryRevisionFromAuthority();
			BroadcastInventoryChange();
		}
	}
}

// 可用槽查找流程：如果传入实例可堆叠则优先找同类未满格；否则返回第一处空格。
int32 UCatInventoryComponent::FindAvailableSlot(UCatInventoryItemInstance* ItemInstance, const int32 Count) const
{
	(void)Count;

	const UCatInventoryItemDefinition* TargetDefinition =
		ItemInstance != nullptr ? ItemInstance->GetItemDefinition() : nullptr;
	if (TargetDefinition != nullptr && GetMaxStackCountForDefinition(*TargetDefinition) > 1)
	{
		for (int32 SlotIndex = 0; SlotIndex < InventoryList.Entries.Num(); ++SlotIndex)
		{
			const FCatInventoryEntry& Entry = InventoryList.Entries[SlotIndex];
			const UCatInventoryItemDefinition* ExistingDefinition =
				Entry.Instance != nullptr ? Entry.Instance->GetItemDefinition() : nullptr;
			if (ExistingDefinition != nullptr
				&& ExistingDefinition->CanStackWith(*TargetDefinition)
				&& Entry.StackCount < GetMaxStackCountForDefinition(*TargetDefinition))
			{
				return SlotIndex;
			}
		}
	}

	for (int32 SlotIndex = 0; SlotIndex < InventoryList.Entries.Num(); ++SlotIndex)
	{
		if (InventoryList.Entries[SlotIndex].Instance == nullptr
			|| InventoryList.Entries[SlotIndex].StackCount <= 0)
		{
			return SlotIndex;
		}
	}

	return INDEX_NONE;
}

// 条目移除流程：清空所有指向该实例的格子；确实移除后才解除复制登记、推进内容版本并广播一次完整重读。
void UCatInventoryComponent::RemoveEntry(UCatInventoryItemInstance* ItemInstance)
{
	if (ItemInstance == nullptr || GetOwner() == nullptr || !GetOwner()->HasAuthority())
	{
		return;
	}

	bool bRemoved = false;
	for (FCatInventoryEntry& Entry : InventoryList.Entries)
	{
		if (Entry.Instance == ItemInstance)
		{
			Entry = FCatInventoryEntry(this);
			bRemoved = true;
			InventoryList.MarkItemDirty(Entry);
		}
	}

	if (bRemoved)
	{
		if (!IsItemInstanceReferencedByOtherSlots(ItemInstance, INDEX_NONE)
			&& IsUsingRegisteredSubObjectList())
		{
			RemoveReplicatedSubObject(ItemInstance);
		}
		AdvanceInventoryRevisionFromAuthority();
		BroadcastInventoryChange();
	}
}

// 实例数组读取流程：委托 FastArray 列表生成快照，避免外部直接访问 Entries。
TArray<UCatInventoryItemInstance*> UCatInventoryComponent::GetAllItems() const
{
	return InventoryList.GetAllItems();
}

// 格子快照读取流程：返回 Entries 的副本，UI 和调试层不能绕开事务写库存。
TArray<FCatInventoryEntry> UCatInventoryComponent::GetInventoryEntries() const
{
	return InventoryList.Entries;
}

// 库存版本读取流程：只返回服务器提交过的内容版本，调用方不能用它推断钓鱼选择或外部容器状态。
int64 UCatInventoryComponent::GetInventoryRevision() const
{
	return InventoryRevision;
}

// 现有实例添加流程：只在服务器执行；成功时 AddEntry 推进内容版本，返回值表示这次请求的全部数量是否都被接收。
bool UCatInventoryComponent::AddItemInstance(UCatInventoryItemInstance* ItemInstance, const int32 Count)
{
	int32 RemainingCount = Count;
	bool bAdded = false;
	AddEntry(ItemInstance, RemainingCount, bAdded);
	return bAdded && RemainingCount == 0;
}

// 按定义公开添加入口保持服务器事务语义；成功时 AddEntry 推进内容版本，返回 false 时调用方应把整次发货当失败处理。
bool UCatInventoryComponent::AddItemDefinition(
	UCatInventoryItemDefinition* ItemDefinition,
	const int32 Count,
	const TSubclassOf<UCatInventoryItemInstance> ItemInstanceClass)
{
	int32 RemainingCount = Count;
	bool bAdded = false;
	AddEntry(ItemDefinition, RemainingCount, bAdded, ItemInstanceClass);
	return bAdded && RemainingCount == 0;
}

// 批次预检流程：统一收货关闭时拒绝 Actor 级入口；容量模拟成功才认为整批可接收。
bool UCatInventoryComponent::CanFullyAcceptInventoryBatch(const FCatInventoryReceiveBatch& ReceiveBatch) const
{
	if (ReceiveBatch.IsEmpty())
	{
		return true;
	}

	if (!CanReceiveUnifiedInventoryIntake())
	{
		return false;
	}

	TArray<FSimulatedInventorySlot> SimulatedSlots;
	return SimulateAddInventoryBatch(ReceiveBatch, SimulatedSlots);
}

// 批次写入流程：
// 1. 先要求 authority 和非空批次，再用容量预演保证正常路径不会半批失败。
// 2. 逐项正式入库时暂不推进版本，记录是否已经发生任何 Entries 变更。
// 3. 如果预检后仍遇到创建或登记失败，已经写入的部分会先推进版本并广播，让读者不继续拿旧版本观察新内容。
// 4. 全批成功且确有变更时只推进一次版本并广播，避免一批收货拆成多次 UI 刷新。
bool UCatInventoryComponent::TryAddInventoryBatch(const FCatInventoryReceiveBatch& ReceiveBatch)
{
	AActor* OwningActor = GetOwner();
	if (OwningActor == nullptr || !OwningActor->HasAuthority())
	{
		return false;
	}

	if (ReceiveBatch.IsEmpty())
	{
		return true;
	}

	if (!CanFullyAcceptInventoryBatch(ReceiveBatch))
	{
		UE_LOG(LogCatInventory, Warning, TEXT("Event=inventory_batch_rejected Owner=%s Definitions=%d Instances=%d"),
			*GetNameSafe(OwningActor), ReceiveBatch.DefinitionEntries.Num(), ReceiveBatch.InstanceEntries.Num());
		return false;
	}

	bool bAnyMutation = false;
	for (const FCatInventoryDefinitionEntry& DefinitionEntry : ReceiveBatch.DefinitionEntries)
	{
		int32 RemainingCount = DefinitionEntry.Count;
		bool bAdded = false;
		AddEntry(DefinitionEntry.ItemDefinition, RemainingCount, bAdded, DefinitionEntry.ItemInstanceClass, false);
		bAnyMutation = bAnyMutation || RemainingCount != DefinitionEntry.Count;
		if (!bAdded || RemainingCount != 0)
		{
			if (bAnyMutation)
			{
				AdvanceInventoryRevisionFromAuthority();
				BroadcastInventoryChange();
			}
			UE_LOG(LogCatInventory, Error, TEXT("Event=inventory_batch_apply_failed Owner=%s Source=Definition Count=%d Remaining=%d"),
				*GetNameSafe(OwningActor), DefinitionEntry.Count, RemainingCount);
			return false;
		}
	}

	for (const FCatInventoryInstanceEntry& InstanceEntry : ReceiveBatch.InstanceEntries)
	{
		int32 RemainingCount = InstanceEntry.Count;
		bool bAdded = false;
		AddEntry(InstanceEntry.ItemInstance, RemainingCount, bAdded, false);
		bAnyMutation = bAnyMutation || RemainingCount != InstanceEntry.Count;
		if (!bAdded || RemainingCount != 0)
		{
			if (bAnyMutation)
			{
				AdvanceInventoryRevisionFromAuthority();
				BroadcastInventoryChange();
			}
			UE_LOG(LogCatInventory, Error, TEXT("Event=inventory_batch_apply_failed Owner=%s Source=Instance Count=%d Remaining=%d"),
				*GetNameSafe(OwningActor), InstanceEntry.Count, RemainingCount);
			return false;
		}
	}

	if (bAnyMutation)
	{
		AdvanceInventoryRevisionFromAuthority();
		BroadcastInventoryChange();
	}
	UE_LOG(LogCatInventory, Log, TEXT("Event=inventory_batch_accepted Owner=%s Definitions=%d Instances=%d Slots=%d"),
		*GetNameSafe(OwningActor), ReceiveBatch.DefinitionEntries.Num(), ReceiveBatch.InstanceEntries.Num(),
		InventoryList.Entries.Num());
	return true;
}

// 预留批次归还流程：
// 1. 普通容量能接收时复用标准批次入口，保持商店、拾取和返还的堆叠规则一致。
// 2. 普通容量放不下时只按调用方给出的返还格预算追加空槽，再重新预检整批，避免把“满包返还”扩散到 Equipment 或 Fishing。
// 3. 追加后如果正式写入仍失败，回滚到追加前 entries；成功时由标准批次入口推进版本和广播。
bool UCatInventoryComponent::TryReturnReservedInventoryBatchFromAuthority(
	const FCatInventoryReceiveBatch& ReceiveBatch, const int32 OverflowSlotCount)
{
	AActor* OwningActor = GetOwner();
	if (OwningActor == nullptr || !OwningActor->HasAuthority())
	{
		return false;
	}

	if (ReceiveBatch.IsEmpty())
	{
		return true;
	}

	if (CanFullyAcceptInventoryBatch(ReceiveBatch))
	{
		return TryAddInventoryBatch(ReceiveBatch);
	}

	const int32 SafeOverflowSlotCount = FMath::Max(0, OverflowSlotCount);
	if (SafeOverflowSlotCount <= 0)
	{
		return false;
	}

	const TArray<FCatInventoryEntry> SavedEntries = InventoryList.Entries;
	const int32 OriginalSlotCount = InventoryList.Entries.Num();
	for (int32 AddedSlotIndex = 0; AddedSlotIndex < SafeOverflowSlotCount; ++AddedSlotIndex)
	{
		InventoryList.Entries.Add(FCatInventoryEntry(this));
	}
	InventoryList.MarkArrayDirty();

	if (!CanFullyAcceptInventoryBatch(ReceiveBatch))
	{
		InventoryList.Entries = SavedEntries;
		InventoryList.MarkArrayDirty();
		return false;
	}

	if (TryAddInventoryBatch(ReceiveBatch))
	{
		UE_LOG(LogCatInventory, Log,
			TEXT("Event=inventory_reserved_return_overflow Owner=%s OriginalSlots=%d NewSlots=%d Definitions=%d Instances=%d"),
			*GetNameSafe(OwningActor), OriginalSlotCount, InventoryList.Entries.Num(),
			ReceiveBatch.DefinitionEntries.Num(), ReceiveBatch.InstanceEntries.Num());
		return true;
	}

	ReplaceInventoryEntriesFromAuthority(SavedEntries, OriginalSlotCount);
	return false;
}

// 稳定 ID 发货预检入口流程：先从正式库存目录解析定义资产，再进入共用预检；目录缺失时让内部流程统一返回无效载荷。
ECatDomainCommandError UCatInventoryComponent::ValidateInventoryDefinitionGrantFromAuthority(
	const FGuid RequestId, const FName DefinitionId, const int32 Count) const
{
	const UCatInventorySettings* InventorySettings = GetDefault<UCatInventorySettings>();
	UCatInventoryItemDefinition* ItemDefinition =
		InventorySettings != nullptr ? InventorySettings->FindRuntimeDefinition(DefinitionId) : nullptr;
	return ValidateInventoryDefinitionGrantFromAuthorityInternal(RequestId, DefinitionId, ItemDefinition, Count);
}

// 已解析定义发货预检流程：调用方已经完成业务目录解析时，库存仍按定义自己的稳定 ID 建立同一份载荷口径。
ECatDomainCommandError UCatInventoryComponent::ValidateResolvedInventoryDefinitionGrantFromAuthority(
	const FGuid RequestId, UCatInventoryItemDefinition* ItemDefinition, const int32 Count) const
{
	const FName DefinitionId = ItemDefinition != nullptr ? ItemDefinition->GetInventoryDefinitionId() : NAME_None;
	return ValidateInventoryDefinitionGrantFromAuthorityInternal(RequestId, DefinitionId, ItemDefinition, Count);
}

// 稳定定义发货预检共用流程：
// 1. 先处理已缓存载荷，允许同一请求在提交前后重复询问，但不允许换定义或数量。
// 2. 首次预检必须确认 authority、稳定 ID、定义运行配置和数量都有效，避免来源系统传入半配置资产。
// 3. 最后只用正式库存批次预演容量，调用方不能在自己系统里复制堆叠和格子规则。
ECatDomainCommandError UCatInventoryComponent::ValidateInventoryDefinitionGrantFromAuthorityInternal(
	const FGuid RequestId, const FName DefinitionId, UCatInventoryItemDefinition* ItemDefinition, const int32 Count) const
{
	const FString Key = MakeTerminalKey(TEXT("GrantInventoryDefinition"), RequestId);
	const FString PayloadPrefix = FString::Printf(TEXT("Definition=%s|Count=%d|"),
		*DefinitionId.ToString(), Count);
	if (const FString* CachedPayload = TerminalPayloadByKey.Find(Key))
	{
		return CachedPayload->StartsWith(PayloadPrefix)
			? ECatDomainCommandError::None
			: ECatDomainCommandError::InvalidPayload;
	}

	const AActor* OwningActor = GetOwner();
	if (!RequestId.IsValid() || OwningActor == nullptr || !OwningActor->HasAuthority()
		|| DefinitionId.IsNone() || Count <= 0 || ItemDefinition == nullptr
		|| !ItemDefinition->IsInventoryRuntimeDefinitionReady()
		|| ItemDefinition->GetInventoryDefinitionId() != DefinitionId)
	{
		return ECatDomainCommandError::InvalidPayload;
	}

	FCatInventoryReceiveBatch ReceiveBatch;
	FCatInventoryDefinitionEntry& DefinitionEntry = ReceiveBatch.DefinitionEntries.AddDefaulted_GetRef();
	DefinitionEntry.ItemDefinition = ItemDefinition;
	DefinitionEntry.Count = Count;
	return CanFullyAcceptInventoryBatch(ReceiveBatch)
		? ECatDomainCommandError::None
		: ECatDomainCommandError::CapacityExceeded;
}

// 稳定 ID 发货提交入口流程：先从正式库存目录解析定义资产，再进入共用发货事务；版本、幂等和写入不在公开入口重复实现。
FCatDomainCommandResult UCatInventoryComponent::GrantInventoryDefinitionFromAuthority(
	const FGuid RequestId, const int64 ExpectedRevision, const FName DefinitionId, const int32 Count)
{
	const UCatInventorySettings* InventorySettings = GetDefault<UCatInventorySettings>();
	UCatInventoryItemDefinition* ItemDefinition =
		InventorySettings != nullptr ? InventorySettings->FindRuntimeDefinition(DefinitionId) : nullptr;
	return GrantInventoryDefinitionFromAuthorityInternal(
		RequestId, ExpectedRevision, DefinitionId, ItemDefinition, Count);
}

// 已解析定义发货提交流程：旧适配层只把业务定义交给库存，实际幂等、容量和写入仍落在统一库存命令上。
FCatDomainCommandResult UCatInventoryComponent::GrantResolvedInventoryDefinitionFromAuthority(
	const FGuid RequestId, const int64 ExpectedRevision, UCatInventoryItemDefinition* ItemDefinition, const int32 Count)
{
	const FName DefinitionId = ItemDefinition != nullptr ? ItemDefinition->GetInventoryDefinitionId() : NAME_None;
	return GrantInventoryDefinitionFromAuthorityInternal(
		RequestId, ExpectedRevision, DefinitionId, ItemDefinition, Count);
}

// 稳定定义发货提交共用流程：
// 1. 先按稳定 ID、数量和正式库存 Revision 建立幂等签名，所有来源共享同一条重放规则。
// 2. 首次请求必须在 authority 且定义运行配置完整，当前 InventoryRevision 也必须等于调用方观察值。
// 3. 通过后只调用库存批次收货；堆叠、实例创建、版本推进和广播都不再散落到外层系统。
// 4. 所有首次终态都会缓存并写入诊断日志，便于商店、奖励或旧 Equipment 入口跨端追查。
FCatDomainCommandResult UCatInventoryComponent::GrantInventoryDefinitionFromAuthorityInternal(
	const FGuid RequestId, const int64 ExpectedRevision, const FName DefinitionId,
	UCatInventoryItemDefinition* ItemDefinition, const int32 Count)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString Key = MakeTerminalKey(TEXT("GrantInventoryDefinition"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("Definition=%s|Count=%d|ExpectedRevision=%lld"),
		*DefinitionId.ToString(), Count, ExpectedRevision);
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		const FString* CachedPayload = TerminalPayloadByKey.Find(Key);
		if (!CachedPayload || *CachedPayload != PayloadSignature)
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
			Result.Revision = InventoryRevision;
			return Result;
		}
		Result = *Cached;
		MarkCommandReplayed(Result);
		return Result;
	}

	const AActor* OwningActor = GetOwner();
	if (!RequestId.IsValid() || OwningActor == nullptr || !OwningActor->HasAuthority()
		|| DefinitionId.IsNone() || Count <= 0 || ItemDefinition == nullptr
		|| !ItemDefinition->IsInventoryRuntimeDefinitionReady()
		|| ItemDefinition->GetInventoryDefinitionId() != DefinitionId)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (InventoryRevision != ExpectedRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
	}
	else
	{
		FCatInventoryReceiveBatch ReceiveBatch;
		FCatInventoryDefinitionEntry& DefinitionEntry = ReceiveBatch.DefinitionEntries.AddDefaulted_GetRef();
		DefinitionEntry.ItemDefinition = ItemDefinition;
		DefinitionEntry.Count = Count;
		if (!CanFullyAcceptInventoryBatch(ReceiveBatch))
		{
			Result.Error = ECatDomainCommandError::CapacityExceeded;
		}
		else if (TryAddInventoryBatch(ReceiveBatch))
		{
			Result.bCommitted = true;
			Result.Error = ECatDomainCommandError::None;
		}
		else
		{
			Result.Error = ECatDomainCommandError::DependencyUnavailable;
		}
	}

	Result.Revision = InventoryRevision;
	TerminalCache.Add(Key, Result);
	TerminalPayloadByKey.Add(Key, PayloadSignature);
	UE_LOG(LogCatInventory, Log,
		TEXT("Event=inventory_definition_grant Owner=%s Request=%s Committed=%s Error=%s Revision=%lld Definition=%s Count=%d"),
		*GetNameSafe(OwningActor), *RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		Result.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Error),
		Result.Revision, *DefinitionId.ToString(), Count);
	return Result;
}

// 正式库存整理流程：
// 1. 先按 RequestId、库存 Revision、源/目标槽位生成幂等签名；重放只返回首次终态，不再次移动格子。
// 2. 首次请求必须在 authority 上执行，并且客户端看到的库存版本要等于当前正式库存版本。
// 3. 真正移动、合并或交换交给库存内部交换规则；成功后只推进正式库存版本并广播一次完整重读。
// 4. 失败时返回当前库存版本，让 UI 丢弃旧视图后按复制事实重读。
FCatDomainCommandResult UCatInventoryComponent::MoveInventorySlotFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const int32 SourceSlotIndex, const int32 TargetSlotIndex)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString Key = MakeTerminalKey(TEXT("MoveInventorySlot"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("ExpectedRevision=%lld|Source=%d|Target=%d"),
		ExpectedRevision, SourceSlotIndex, TargetSlotIndex);
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		const FString* CachedPayload = TerminalPayloadByKey.Find(Key);
		if (!CachedPayload || *CachedPayload != PayloadSignature)
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
			Result.Revision = InventoryRevision;
			return Result;
		}
		Result = *Cached;
		MarkCommandReplayed(Result);
		return Result;
	}

	AActor* OwningActor = GetOwner();
	if (OwningActor == nullptr || !OwningActor->HasAuthority() || !RequestId.IsValid()
		|| SourceSlotIndex < 0 || TargetSlotIndex < 0 || SourceSlotIndex == TargetSlotIndex)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (InventoryRevision != ExpectedRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
	}
	else
	{
		const FInventoryExchangeMutation MoveResult =
			ExecuteExchangeRequestOnAuthorityInternal(this, SourceSlotIndex, this, TargetSlotIndex);
		Result.bCommitted = MoveResult.bChanged;
		Result.Error = MoveResult.Error;
		if (MoveResult.bChanged)
		{
			AdvanceInventoryRevisionFromAuthority();
			BroadcastInventoryChange();
		}
	}

	Result.Revision = InventoryRevision;
	TerminalCache.Add(Key, Result);
	TerminalPayloadByKey.Add(Key, PayloadSignature);
	UE_LOG(LogCatInventory, Log,
		TEXT("Event=inventory_move_slot Owner=%s Request=%s Committed=%s Error=%s Revision=%lld Source=%d Target=%d"),
		*GetNameSafe(OwningActor), *RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		Result.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Error),
		Result.Revision, SourceSlotIndex, TargetSlotIndex);
	return Result;
}

// 统一收货开关读取流程：只返回配置事实，不检查容量或 authority。
bool UCatInventoryComponent::CanReceiveUnifiedInventoryIntake() const
{
	return bAllowUnifiedInventoryIntake;
}

// 收货优先级读取流程：数值越高越靠前，保持与 Aegis 统一收货排序一致。
int32 UCatInventoryComponent::GetUnifiedInventoryIntakePriority() const
{
	return UnifiedInventoryIntakePriority;
}

// 槽位容量刷新流程：只允许服务器或尚未拥有 Actor 的构造期路径写配置值；刷新时不会裁掉已有格子。
void UCatInventoryComponent::SetInventorySlotCountFromAuthority(const int32 NewSlotCount)
{
	AActor* OwningActor = GetOwner();
	if (OwningActor != nullptr && !OwningActor->HasAuthority())
	{
		return;
	}

	NumSlots = FMath::Max(0, NewSlotCount);
	InitializeOrRefreshInventorySlots();
}

// 整表替换流程：
// 1. 只允许 authority 或尚未绑定 Actor 的构造/恢复路径写入，客户端不能用它覆盖复制事实。
// 2. 先比较格位、实例和数量；内容没变时只补齐本地 owner，不推进库存版本。
// 3. 内容变化时移除旧实例复制登记，再按传入槽位顺序重建 Entries，并补足最低格子数量。
// 4. 每个有效实例都会刷新格子 owner、运行宿主和复制登记，最后推进一次库存版本并广播完整变化。
bool UCatInventoryComponent::ReplaceInventoryEntriesFromAuthority(
	const TArray<FCatInventoryEntry>& NewEntries, const int32 MinimumSlotCount)
{
	AActor* OwningActor = GetOwner();
	if (OwningActor != nullptr && !OwningActor->HasAuthority())
	{
		return false;
	}

	if (AreInventoryEntriesEquivalent(NewEntries, MinimumSlotCount))
	{
		for (FCatInventoryEntry& Entry : InventoryList.Entries)
		{
			Entry.SlotOwnerComponent = this;
			SyncInventoryItemRuntimeOwner(Entry.Instance);
		}
		return true;
	}

	if (IsUsingRegisteredSubObjectList())
	{
		TSet<UCatInventoryItemInstance*> RemovedInstances;
		for (const FCatInventoryEntry& ExistingEntry : InventoryList.Entries)
		{
			if (ExistingEntry.Instance != nullptr && !RemovedInstances.Contains(ExistingEntry.Instance))
			{
				RemoveReplicatedSubObject(ExistingEntry.Instance);
				RemovedInstances.Add(ExistingEntry.Instance);
			}
		}
	}

	const int32 DesiredSlotCount = FMath::Max(FMath::Max(0, MinimumSlotCount), NewEntries.Num());
	InventoryList.Entries.Reset(DesiredSlotCount);
	for (int32 SlotIndex = 0; SlotIndex < DesiredSlotCount; ++SlotIndex)
	{
		FCatInventoryEntry& TargetEntry = InventoryList.Entries.AddDefaulted_GetRef();
		TargetEntry = FCatInventoryEntry(this);
		if (!NewEntries.IsValidIndex(SlotIndex)
			|| NewEntries[SlotIndex].Instance == nullptr
			|| NewEntries[SlotIndex].StackCount <= 0)
		{
			continue;
		}

		TargetEntry.Instance = NewEntries[SlotIndex].Instance;
		TargetEntry.StackCount = NewEntries[SlotIndex].StackCount;
		TargetEntry.LastObservedCount = TargetEntry.StackCount;
		TargetEntry.SlotOwnerComponent = this;
		SyncInventoryItemRuntimeOwner(TargetEntry.Instance);
		if (IsUsingRegisteredSubObjectList() && IsReadyForReplication())
		{
			AddReplicatedSubObject(TargetEntry.Instance);
		}
	}

	InventoryList.MarkArrayDirty();
	AdvanceInventoryRevisionFromAuthority();
	BroadcastInventoryChange();
	return true;
}

// 实例移除流程：委托条目移除入口处理清空、复制登记和变化广播，避免形成两套清理规则。
void UCatInventoryComponent::RemoveItemInstance(UCatInventoryItemInstance* ItemInstance)
{
	RemoveEntry(ItemInstance);
}

// 下标移除流程：
// 1. 保留旧公开入口的“清空这个位置”语义，合法空槽也会发布一次清空提交，避免外部兼容调用观察不到确认。
// 2. 非空实例离开最后一个格子时解除复制登记，防止客户端继续收到已经不归库存持有的子对象。
// 3. 最后推进内容版本并按槽位广播；需要拿走实例身份的部署/转移流程改用带返回值的 entry 移出入口。
void UCatInventoryComponent::RemoveItemInstanceFromIndex(const int32 TargetIndex)
{
	if (GetOwner() == nullptr || !GetOwner()->HasAuthority() || !IsValidInventorySlotIndex(TargetIndex))
	{
		return;
	}

	UCatInventoryItemInstance* RemovedInstance = InventoryList.Entries[TargetIndex].Instance;
	InventoryList.Entries[TargetIndex] = FCatInventoryEntry(this);
	InventoryList.MarkItemDirty(InventoryList.Entries[TargetIndex]);

	if (RemovedInstance != nullptr
		&& !IsItemInstanceReferencedByOtherSlots(RemovedInstance, TargetIndex)
		&& IsUsingRegisteredSubObjectList())
	{
		RemoveReplicatedSubObject(RemovedInstance);
	}

	AdvanceInventoryRevisionFromAuthority();
	BroadcastInventoryChange(TargetIndex);
}

// 槽位 entry 移出流程：
// 1. 只允许服务器移出非空正式格，失败时输出保持空 entry，避免调用方误拿旧实例。
// 2. 成功时先复制被移出的实例和数量，再清空正式槽位并维护复制子对象登记。
// 3. 最后推进库存版本并广播具体槽位，让部署或转移流程从库存事实源拿到同一份实例。
bool UCatInventoryComponent::RemoveInventoryEntryAtSlotFromAuthority(
	const int32 TargetIndex, FCatInventoryEntry& OutRemovedEntry)
{
	OutRemovedEntry = FCatInventoryEntry(this);
	if (GetOwner() == nullptr || !GetOwner()->HasAuthority() || !IsValidInventorySlotIndex(TargetIndex))
	{
		return false;
	}

	if (InventoryList.Entries[TargetIndex].Instance == nullptr
		|| InventoryList.Entries[TargetIndex].StackCount <= 0)
	{
		return false;
	}

	OutRemovedEntry = InventoryList.Entries[TargetIndex];
	UCatInventoryItemInstance* RemovedInstance = OutRemovedEntry.Instance;
	InventoryList.Entries[TargetIndex] = FCatInventoryEntry(this);
	InventoryList.MarkItemDirty(InventoryList.Entries[TargetIndex]);

	if (RemovedInstance != nullptr
		&& !IsItemInstanceReferencedByOtherSlots(RemovedInstance, TargetIndex)
		&& IsUsingRegisteredSubObjectList())
	{
		RemoveReplicatedSubObject(RemovedInstance);
	}

	AdvanceInventoryRevisionFromAuthority();
	BroadcastInventoryChange(TargetIndex);
	return true;
}

// 扣量流程：服务器验证槽位和数量后扣减；清空格子时才解除实例复制登记，任一成功扣减都会推进内容版本并广播。
bool UCatInventoryComponent::ConsumeItemAtSlot(const int32 SlotIndex, const int32 ConsumeCount)
{
	if (GetOwner() == nullptr
		|| !GetOwner()->HasAuthority()
		|| ConsumeCount <= 0
		|| !IsValidInventorySlotIndex(SlotIndex))
	{
		return false;
	}

	FCatInventoryEntry& Entry = InventoryList.Entries[SlotIndex];
	if (Entry.Instance == nullptr || Entry.StackCount < ConsumeCount)
	{
		return false;
	}

	Entry.StackCount -= ConsumeCount;
	Entry.LastObservedCount = Entry.StackCount;
	if (Entry.StackCount > 0)
	{
		InventoryList.MarkItemDirty(Entry);
	}
	else
	{
		UCatInventoryItemInstance* ConsumedInstance = Entry.Instance;
		Entry = FCatInventoryEntry(this);
		InventoryList.MarkItemDirty(Entry);

		if (ConsumedInstance != nullptr
			&& !IsItemInstanceReferencedByOtherSlots(ConsumedInstance, SlotIndex)
			&& IsUsingRegisteredSubObjectList())
		{
			RemoveReplicatedSubObject(ConsumedInstance);
		}
	}

	AdvanceInventoryRevisionFromAuthority();
	BroadcastInventoryChange(SlotIndex);
	return true;
}

// 槽位合法性判断流程：只检查数组边界，不要求槽位非空。
bool UCatInventoryComponent::IsValidInventorySlotIndex(const int32 SlotIndex) const
{
	return InventoryList.Entries.IsValidIndex(SlotIndex);
}

// 槽位占用判断流程：必须同时有实例和正数量，避免坏复制状态被当成可用物品。
bool UCatInventoryComponent::HasItemAtSlot(const int32 SlotIndex) const
{
	return IsValidInventorySlotIndex(SlotIndex)
		&& InventoryList.Entries[SlotIndex].Instance != nullptr
		&& InventoryList.Entries[SlotIndex].StackCount > 0;
}

// 实例下标查找流程：按运行实例指针匹配，找不到时明确返回 INDEX_NONE。
int32 UCatInventoryComponent::FindInventorySlotIndexFromInstance(const UCatInventoryItemInstance* ItemInstance) const
{
	if (ItemInstance == nullptr)
	{
		return INDEX_NONE;
	}

	for (int32 SlotIndex = 0; SlotIndex < InventoryList.Entries.Num(); ++SlotIndex)
	{
		if (InventoryList.Entries[SlotIndex].Instance == ItemInstance)
		{
			return SlotIndex;
		}
	}

	return INDEX_NONE;
}

// 实例 ID 下标查找流程：
// 1. 先拒绝无效 ID，避免把默认空 ID 当作可匹配物品。
// 2. 再逐格读取正式实例身份；空槽不会命中，堆叠数量变化也不会改变实例 ID 查询结果。
// 3. 找不到时返回 INDEX_NONE，让上层按正式库存事实处理 NotFound。
int32 UCatInventoryComponent::FindInventorySlotIndexFromInstanceId(const FGuid ItemInstanceId) const
{
	if (!ItemInstanceId.IsValid())
	{
		return INDEX_NONE;
	}

	for (int32 SlotIndex = 0; SlotIndex < InventoryList.Entries.Num(); ++SlotIndex)
	{
		const UCatInventoryItemInstance* Instance = InventoryList.Entries[SlotIndex].Instance;
		if (Instance != nullptr && Instance->GetItemInstanceId() == ItemInstanceId)
		{
			return SlotIndex;
		}
	}

	return INDEX_NONE;
}

// 正式库存材料定位规则：
// 1. 空定义 ID 代表调用方缺配置，直接失败，避免把任意物品误当成可消费材料。
// 2. 查询只信任已绑定实例和正堆叠数量，因为空格、坏复制或旧投影都不能证明玩家拥有材料。
// 3. 返回第一格是为了保持旧“先找到先消费”的口径；真正扣量仍由 ConsumeItemAtSlot 重新校验。
int32 UCatInventoryComponent::FindFirstInventorySlotIndexByDefinitionId(const FName DefinitionId) const
{
	if (DefinitionId.IsNone())
	{
		return INDEX_NONE;
	}

	for (int32 SlotIndex = 0; SlotIndex < InventoryList.Entries.Num(); ++SlotIndex)
	{
		const FCatInventoryEntry& Entry = InventoryList.Entries[SlotIndex];
		if (Entry.Instance != nullptr
			&& Entry.StackCount > 0
			&& Entry.Instance->GetItemDefinitionId() == DefinitionId)
		{
			return SlotIndex;
		}
	}

	return INDEX_NONE;
}

// 槽位数量读取流程：返回当前数组长度，可能大于配置 NumSlots，因为运行期不会截断已有物品。
int32 UCatInventoryComponent::GetInventorySlotCount() const
{
	return InventoryList.Entries.Num();
}

// 槽位读取流程：越界返回空；有效时返回内部只读指针，调用方不得长期缓存。
const FCatInventoryEntry* UCatInventoryComponent::GetInventoryEntryAtSlot(const int32 SlotIndex) const
{
	if (!IsValidInventorySlotIndex(SlotIndex))
	{
		return nullptr;
	}

	return &InventoryList.Entries[SlotIndex];
}

// 使用预检流程：默认使用拥有者 Pawn，槽位、实例和定义都有效后才交给实例自己的真实 Use 语义判断。
bool UCatInventoryComponent::CanUseItemAtSlot(const int32 SlotIndex, APawn* UserPawn) const
{
	if (GetOwner() == nullptr)
	{
		return false;
	}

	if (UserPawn == nullptr)
	{
		UserPawn = Cast<APawn>(GetOwner());
	}

	if (!IsValidInventorySlotIndex(SlotIndex) || UserPawn == nullptr)
	{
		return false;
	}

	const FCatInventoryEntry& Entry = InventoryList.Entries[SlotIndex];
	if (Entry.Instance == nullptr || Entry.StackCount <= 0 || Entry.Instance->GetItemDefinition() == nullptr)
	{
		return false;
	}

	return Entry.Instance->CanUseFromInventory(Entry, UserPawn);
}

// 使用提交流程：客户端只发请求，服务器让具名实例先完成真实 Use 裁决，随后由库存组件按实例返回值决定是否扣数量。
bool UCatInventoryComponent::TryUseItemAtSlot(const int32 SlotIndex, APawn* UserPawn)
{
	if (GetOwner() == nullptr)
	{
		return false;
	}

	if (UserPawn == nullptr)
	{
		UserPawn = Cast<APawn>(GetOwner());
	}

	if (!GetOwner()->HasAuthority())
	{
		ServerTryUseItemAtSlot(SlotIndex, UserPawn);
		return true;
	}

	if (!IsValidInventorySlotIndex(SlotIndex) || UserPawn == nullptr)
	{
		return false;
	}

	FCatInventoryEntry& Entry = InventoryList.Entries[SlotIndex];
	if (Entry.Instance == nullptr || Entry.StackCount <= 0 || Entry.Instance->GetItemDefinition() == nullptr)
	{
		return false;
	}

	int32 ConsumeCount = 0;
	if (!Entry.Instance->TryUseFromInventory(Entry, UserPawn, ConsumeCount))
	{
		return false;
	}

	if (ConsumeCount > 0 && !ConsumeItemAtSlot(SlotIndex, ConsumeCount))
	{
		return false;
	}

	return true;
}

// 结构化使用提交流程：
// 1. 先确认当前组件仍是服务器正式库存，并且 RequestId、来源组件和槽位参数有效。
// 2. 再用 ExpectedInventoryRevision 拦截陈旧 UI 请求；冲突时只返回当前库存版本，不触碰物品实例。
// 3. 通过后重读槽位、实例和定义资产，空格或坏实例按正式库存错误返回。
// 4. 最后调用物品实例自己的结构化 Use；库存组件不认识装备、GAS、草药或窝料的具体效果。
FCatDomainCommandResult UCatInventoryComponent::UseItemAtSlotFromAuthority(
	const FCatInventoryItemUseContext& UseContext)
{
	FCatDomainCommandResult Result;
	Result.RequestId = UseContext.RequestId;
	Result.Revision = InventoryRevision;

	const AActor* OwningActor = GetOwner();
	FName DefinitionId = NAME_None;
	FGuid ItemInstanceId;
	int32 StackCount = 0;
	if (OwningActor == nullptr || !OwningActor->HasAuthority())
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else if (!UseContext.RequestId.IsValid()
		|| (UseContext.SourceInventory != nullptr && UseContext.SourceInventory != this)
		|| UseContext.InventorySlotIndex == INDEX_NONE)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (UseContext.ExpectedInventoryRevision != InventoryRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
	}
	else
	{
		FCatInventoryEntry* Entry = InventoryList.Entries.IsValidIndex(UseContext.InventorySlotIndex)
			? &InventoryList.Entries[UseContext.InventorySlotIndex] : nullptr;
		UCatInventoryItemInstance* Instance = Entry != nullptr ? Entry->Instance.Get() : nullptr;
		UCatInventoryItemDefinition* Definition = Instance != nullptr ? Instance->GetItemDefinition() : nullptr;
		if (Entry == nullptr || Instance == nullptr || Entry->StackCount <= 0
			|| !Instance->GetItemInstanceId().IsValid())
		{
			Result.Error = ECatDomainCommandError::NotFound;
		}
		else if (Definition == nullptr || Definition->GetInventoryDefinitionId().IsNone())
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
		}
		else
		{
			DefinitionId = Definition->GetInventoryDefinitionId();
			ItemInstanceId = Instance->GetItemInstanceId();
			StackCount = Entry->StackCount;
			Result = Instance->UseFromInventorySlotFromAuthority(*Entry, UseContext);
			if (!Result.RequestId.IsValid())
			{
				Result.RequestId = UseContext.RequestId;
			}
		}
	}

	UE_LOG(LogCatInventory, Log,
		TEXT("Event=inventory_use_item Owner=%s Request=%s Slot=%d Definition=%s Item=%s Stack=%d ExpectedInventoryRevision=%lld InventoryRevision=%lld Committed=%s Error=%s ResultRevision=%lld"),
		*GetNameSafe(GetOwner()),
		*UseContext.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		UseContext.InventorySlotIndex,
		*DefinitionId.ToString(),
		*ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		StackCount,
		UseContext.ExpectedInventoryRevision,
		InventoryRevision,
		Result.bCommitted ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(Result.Error),
		Result.Revision);
	return Result;
}

// 使用 RPC 流程：服务器收到客户端请求后重新走完整 authority 校验，不信任客户端预检结果。
void UCatInventoryComponent::ServerTryUseItemAtSlot_Implementation(const int32 SlotIndex, APawn* UserPawn)
{
	TryUseItemAtSlot(SlotIndex, UserPawn);
}

// 交换 RPC 流程：服务器收到拖放请求后只调用 authority 交换入口，所有规则集中在那里执行。
void UCatInventoryComponent::ServerExchangeInventorySlot_Implementation(UCatInventoryComponent* DropInventory,
	const int32 DraggedSlotIndex, const int32 DropSlotIndex)
{
	ExecuteExchangeRequestOnAuthority(this, DraggedSlotIndex, DropInventory, DropSlotIndex);
}

// 交换预检流程：先验证双方组件和槽位，再让目标与源组件分别确认能接收换入条目。
bool UCatInventoryComponent::CanExecuteExchangeRequest(UCatInventoryComponent* DraggedInventory,
	const int32 DraggedSlotIndex, UCatInventoryComponent* DropInventory, const int32 DropSlotIndex)
{
	if (DraggedInventory == nullptr || DropInventory == nullptr)
	{
		return false;
	}

	if (DraggedInventory == DropInventory && DraggedSlotIndex == DropSlotIndex)
	{
		return false;
	}

	if (!DraggedInventory->IsValidInventorySlotIndex(DraggedSlotIndex)
		|| !DropInventory->IsValidInventorySlotIndex(DropSlotIndex))
	{
		return false;
	}

	const FCatInventoryEntry& DraggedEntry = DraggedInventory->InventoryList.Entries[DraggedSlotIndex];
	const FCatInventoryEntry& DropEntry = DropInventory->InventoryList.Entries[DropSlotIndex];
	if (DraggedEntry.Instance == nullptr || DraggedEntry.StackCount <= 0)
	{
		return false;
	}

	if (!DropInventory->CanAcceptInventoryEntryAtSlot(DraggedEntry, DropSlotIndex))
	{
		return false;
	}

	if (DropEntry.Instance != nullptr
		&& !DraggedInventory->CanAcceptInventoryEntryAtSlot(DropEntry, DraggedSlotIndex))
	{
		return false;
	}

	return true;
}

// 内部交换流程：
// 1. 先做详细校验并返回结构化错误；源格为空是 NotFound，目标同堆叠已满是 AlreadyResolved。
// 2. 再按正式库存定义的堆叠规则决定合并、搬空格或交换，期间维护实例 owner、观察数量和复制登记。
// 3. 函数只标记 FastArray 变脏，不推进 Revision、不广播；外层命令先写版本再通知，避免 UI 读到旧版本新内容。
UCatInventoryComponent::FInventoryExchangeMutation UCatInventoryComponent::ExecuteExchangeRequestOnAuthorityInternal(
	UCatInventoryComponent* DraggedInventory, const int32 DraggedSlotIndex,
	UCatInventoryComponent* DropInventory, const int32 DropSlotIndex)
{
	FInventoryExchangeMutation Result;
	if (DraggedInventory == nullptr || DropInventory == nullptr
		|| (DraggedInventory == DropInventory && DraggedSlotIndex == DropSlotIndex))
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}

	if (!DraggedInventory->IsValidInventorySlotIndex(DraggedSlotIndex)
		|| !DropInventory->IsValidInventorySlotIndex(DropSlotIndex))
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}

	AActor* DraggedOwner = DraggedInventory->GetOwner();
	AActor* DropOwner = DropInventory->GetOwner();
	if (DraggedOwner == nullptr || DropOwner == nullptr
		|| !DraggedOwner->HasAuthority() || !DropOwner->HasAuthority())
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}

	FCatInventoryEntry& DraggedEntry = DraggedInventory->InventoryList.Entries[DraggedSlotIndex];
	FCatInventoryEntry& DropEntry = DropInventory->InventoryList.Entries[DropSlotIndex];
	if (DraggedEntry.Instance == nullptr || DraggedEntry.StackCount <= 0)
	{
		Result.Error = ECatDomainCommandError::NotFound;
		return Result;
	}

	if (!DropInventory->CanAcceptInventoryEntryAtSlot(DraggedEntry, DropSlotIndex))
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}

	const bool bDropOccupied = DropEntry.Instance != nullptr && DropEntry.StackCount > 0;
	if (bDropOccupied && !DraggedInventory->CanAcceptInventoryEntryAtSlot(DropEntry, DraggedSlotIndex))
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}

	UCatInventoryItemInstance* DraggedInstanceBefore = DraggedEntry.Instance;
	UCatInventoryItemInstance* DropInstanceBefore = DropEntry.Instance;
	const UCatInventoryItemDefinition* DraggedDefinition =
		DraggedEntry.Instance != nullptr ? DraggedEntry.Instance->GetItemDefinition() : nullptr;
	const UCatInventoryItemDefinition* DropDefinition =
		DropEntry.Instance != nullptr ? DropEntry.Instance->GetItemDefinition() : nullptr;

	if (bDropOccupied
		&& DraggedDefinition != nullptr
		&& DropDefinition != nullptr
		&& DropDefinition->CanStackWith(*DraggedDefinition)
		&& DropInventory->GetMaxStackCountForDefinition(*DropDefinition) > 1)
	{
		const int32 MaxStackCount = DropInventory->GetMaxStackCountForDefinition(*DropDefinition);
		const int32 AvailableSpace = FMath::Max(0, MaxStackCount - DropEntry.StackCount);
		const int32 MovedCount = FMath::Min(AvailableSpace, DraggedEntry.StackCount);
		if (MovedCount <= 0)
		{
			Result.Error = ECatDomainCommandError::AlreadyResolved;
			return Result;
		}

		DropEntry.StackCount += MovedCount;
		DraggedEntry.StackCount -= MovedCount;
		DropEntry.LastObservedCount = DropEntry.StackCount;
		DraggedEntry.LastObservedCount = DraggedEntry.StackCount;
		if (DraggedEntry.StackCount <= 0)
		{
			DraggedEntry = FCatInventoryEntry(DraggedInventory);
			if (DraggedInstanceBefore != nullptr
				&& !DraggedInventory->IsItemInstanceReferencedByOtherSlots(DraggedInstanceBefore, DraggedSlotIndex)
				&& DraggedInventory->IsUsingRegisteredSubObjectList())
			{
				DraggedInventory->RemoveReplicatedSubObject(DraggedInstanceBefore);
			}
		}

		DraggedInventory->InventoryList.MarkItemDirty(DraggedEntry);
		DropInventory->InventoryList.MarkItemDirty(DropEntry);
		Result.bChanged = true;
		Result.Error = ECatDomainCommandError::None;
		return Result;
	}

	if (!bDropOccupied)
	{
		DropEntry = DraggedEntry;
		DraggedEntry = FCatInventoryEntry(DraggedInventory);
	}
	else
	{
		Swap(DraggedEntry, DropEntry);
	}

	DraggedEntry.SlotOwnerComponent = DraggedInventory;
	DropEntry.SlotOwnerComponent = DropInventory;
	DraggedEntry.LastObservedCount = DraggedEntry.StackCount;
	DropEntry.LastObservedCount = DropEntry.StackCount;
	DraggedInventory->SyncInventoryItemRuntimeOwner(DraggedEntry.Instance);
	DropInventory->SyncInventoryItemRuntimeOwner(DropEntry.Instance);

	DraggedInventory->UpdateReplicatedItemRegistration(DraggedInstanceBefore, DraggedInventory,
		DropEntry.Instance == DraggedInstanceBefore ? DropInventory : DraggedInventory);
	if (DropInstanceBefore != nullptr && DropInstanceBefore != DraggedInstanceBefore)
	{
		DropInventory->UpdateReplicatedItemRegistration(DropInstanceBefore, DropInventory,
			DraggedEntry.Instance == DropInstanceBefore ? DraggedInventory : DropInventory);
	}

	DraggedInventory->InventoryList.MarkItemDirty(DraggedEntry);
	DropInventory->InventoryList.MarkItemDirty(DropEntry);
	Result.bChanged = true;
	Result.Error = ECatDomainCommandError::None;
	return Result;
}

// authority 交换流程：
// 1. 先复用内部交换规则拿到结构化变更结果；没有真实变更时直接返回 false，不推进任何库存版本。
// 2. 有变更时源库存一定推进版本；跨库存交换时目标库存也推进自己的版本。
// 3. 同一库存内部整理只广播一次，跨库存才分别广播源格和目标格，避免同一 UI 收到重复重读信号。
bool UCatInventoryComponent::ExecuteExchangeRequestOnAuthority(UCatInventoryComponent* DraggedInventory,
	const int32 DraggedSlotIndex, UCatInventoryComponent* DropInventory, const int32 DropSlotIndex)
{
	const FInventoryExchangeMutation Result =
		ExecuteExchangeRequestOnAuthorityInternal(DraggedInventory, DraggedSlotIndex, DropInventory, DropSlotIndex);
	if (!Result.bChanged)
	{
		return false;
	}

	DraggedInventory->AdvanceInventoryRevisionFromAuthority();
	if (DropInventory != DraggedInventory)
	{
		DropInventory->AdvanceInventoryRevisionFromAuthority();
	}
	DraggedInventory->BroadcastInventoryChange(DraggedSlotIndex);
	if (DropInventory != DraggedInventory)
	{
		DropInventory->BroadcastInventoryChange(DropSlotIndex);
	}
	return true;
}

// 交换发起流程：本地没有 authority 时走源库存 RPC；服务器路径直接执行一次最终校验和写入。
bool UCatInventoryComponent::ExecuteExchangeRequest(UCatInventoryComponent* DraggedInventory,
	const int32 DraggedSlotIndex, UCatInventoryComponent* DropInventory, const int32 DropSlotIndex)
{
	if (!CanExecuteExchangeRequest(DraggedInventory, DraggedSlotIndex, DropInventory, DropSlotIndex))
	{
		return false;
	}

	if (DraggedInventory->GetOwner() != nullptr && DraggedInventory->GetOwner()->HasAuthority())
	{
		return ExecuteExchangeRequestOnAuthority(DraggedInventory, DraggedSlotIndex, DropInventory, DropSlotIndex);
	}

	DraggedInventory->ServerExchangeInventorySlot(DropInventory, DraggedSlotIndex, DropSlotIndex);
	return true;
}

// 幂等键流程：正式库存只按本组件生命周期去重；跨玩家、跨局或存档恢复不复用这份内存缓存。
FString UCatInventoryComponent::MakeTerminalKey(const TCHAR* Operation, const FGuid RequestId)
{
	return FString::Printf(TEXT("%s|%s"), Operation, *RequestId.ToString(EGuidFormats::DigitsWithHyphens));
}

// 实例查找流程：返回匹配实例的格子副本，未命中时返回默认空格。
FCatInventoryEntry UCatInventoryComponent::FindInventoryEntryFromInstance(
	UCatInventoryItemInstance* ItemInstance) const
{
	for (const FCatInventoryEntry& Entry : InventoryList.Entries)
	{
		if (Entry.Instance == ItemInstance)
		{
			return Entry;
		}
	}

	return FCatInventoryEntry();
}

// 客户端删除扩展点：默认不做额外表现，统一变化通知已经足够让消费者重读。
void UCatInventoryComponent::BroadcastInventoryRemoveOnClient(const TArrayView<int32> RemovedIndices,
	const int32 FinalSize)
{
	(void)RemovedIndices;
	(void)FinalSize;
}

// 客户端新增扩展点：默认不做额外表现，后续 UI 可在子类里播放获取提示。
void UCatInventoryComponent::BroadcastInventoryAddOnClient(const TArrayView<int32> AddedIndices,
	const int32 FinalSize, const TArray<FCatInventoryEntry>& TargetList)
{
	(void)AddedIndices;
	(void)FinalSize;
	(void)TargetList;
}

// 客户端变更扩展点：默认不做额外表现，统一变化通知已经覆盖刷新需求。
void UCatInventoryComponent::BroadcastInventoryChangeOnClient(const TArrayView<int32> ChangedIndices,
	const int32 FinalSize)
{
	(void)ChangedIndices;
	(void)FinalSize;
}

// 本地变化广播流程：ChangedIndex 只作为扩展信息保留，当前统一通知让消费者重新读取完整库存。
void UCatInventoryComponent::BroadcastInventoryChange(const int32 ChangedIndex)
{
	(void)ChangedIndex;
	OnInventoryObservedChanged.Broadcast();
}

// 槽位接收默认规则：通用库存只校验目标下标有效；装备栏或专用容器可以在子类按标签限制。
bool UCatInventoryComponent::CanAcceptInventoryEntryAtSlot(const FCatInventoryEntry& IncomingEntry,
	const int32 TargetSlotIndex) const
{
	(void)IncomingEntry;
	return IsValidInventorySlotIndex(TargetSlotIndex);
}

// 容量预演单项流程：先合并同类未满格，再占用空格；整个过程只改模拟数组和剩余数量。
bool UCatInventoryComponent::SimulateAddItemDefinition(TArray<FSimulatedInventorySlot>& SimulatedSlots,
	const UCatInventoryItemDefinition& ItemDefinition, int32& InOutRemainingCount) const
{
	if (InOutRemainingCount <= 0)
	{
		return true;
	}

	const int32 MaxStackCount = GetMaxStackCountForDefinition(ItemDefinition);
	if (MaxStackCount > 1)
	{
		for (FSimulatedInventorySlot& SimulatedSlot : SimulatedSlots)
		{
			if (InOutRemainingCount <= 0)
			{
				break;
			}

			if (SimulatedSlot.ItemDefinition == nullptr
				|| !SimulatedSlot.ItemDefinition->CanStackWith(ItemDefinition))
			{
				continue;
			}

			const int32 AvailableSpace = FMath::Max(0, MaxStackCount - SimulatedSlot.StackCount);
			const int32 AddAmount = FMath::Min(InOutRemainingCount, AvailableSpace);
			SimulatedSlot.StackCount += AddAmount;
			InOutRemainingCount -= AddAmount;
		}
	}

	for (FSimulatedInventorySlot& SimulatedSlot : SimulatedSlots)
	{
		if (InOutRemainingCount <= 0)
		{
			break;
		}

		if (SimulatedSlot.ItemDefinition != nullptr)
		{
			continue;
		}

		const int32 AddAmount = MaxStackCount > 1 ? FMath::Min(InOutRemainingCount, MaxStackCount) : 1;
		SimulatedSlot.ItemDefinition = &ItemDefinition;
		SimulatedSlot.StackCount = AddAmount;
		InOutRemainingCount -= AddAmount;
	}

	return InOutRemainingCount <= 0;
}

// 批次预演流程：从当前正式库存复制轻量槽位，再按定义项和实例项逐个模拟接收。
bool UCatInventoryComponent::SimulateAddInventoryBatch(const FCatInventoryReceiveBatch& ReceiveBatch,
	TArray<FSimulatedInventorySlot>& SimulatedSlots) const
{
	SimulatedSlots.Reset();
	SimulatedSlots.Reserve(InventoryList.Entries.Num());

	for (const FCatInventoryEntry& Entry : InventoryList.Entries)
	{
		FSimulatedInventorySlot& SimulatedSlot = SimulatedSlots.AddDefaulted_GetRef();
		SimulatedSlot.ItemDefinition = Entry.Instance != nullptr ? Entry.Instance->GetItemDefinition() : nullptr;
		SimulatedSlot.StackCount = Entry.StackCount;
	}

	for (const FCatInventoryDefinitionEntry& DefinitionEntry : ReceiveBatch.DefinitionEntries)
	{
		if (DefinitionEntry.Count <= 0 || DefinitionEntry.ItemDefinition == nullptr)
		{
			return false;
		}

		if (UCatInventoryItemDefinition::ResolveItemInstanceClass(
				DefinitionEntry.ItemDefinition,
				DefinitionEntry.ItemInstanceClass) == nullptr)
		{
			return false;
		}

		const UCatInventoryItemDefinition* ItemDefinition = DefinitionEntry.ItemDefinition;
		int32 RemainingCount = DefinitionEntry.Count;
		if (ItemDefinition == nullptr
			|| !SimulateAddItemDefinition(SimulatedSlots, *ItemDefinition, RemainingCount))
		{
			return false;
		}
	}

	for (const FCatInventoryInstanceEntry& InstanceEntry : ReceiveBatch.InstanceEntries)
	{
		if (InstanceEntry.Count <= 0 || InstanceEntry.ItemInstance == nullptr)
		{
			return false;
		}

		const UCatInventoryItemDefinition* ItemDefinition = InstanceEntry.ItemInstance->GetItemDefinition();
		int32 RemainingCount = InstanceEntry.Count;
		if (ItemDefinition == nullptr
			|| !SimulateAddItemDefinition(SimulatedSlots, *ItemDefinition, RemainingCount))
		{
			return false;
		}
	}

	return true;
}

// 堆叠上限读取流程：委托定义集中收束非法配置，保证预演和正式写入使用同一规则。
int32 UCatInventoryComponent::GetMaxStackCountForDefinition(
	const UCatInventoryItemDefinition& ItemDefinition) const
{
	return ItemDefinition.GetMaxStackCount();
}

// 实例引用检查流程：清理指定槽位时跳过该槽，确认同一实例没有被其他格继续持有。
bool UCatInventoryComponent::IsItemInstanceReferencedByOtherSlots(
	const UCatInventoryItemInstance* ItemInstance, const int32 IgnoredSlotIndex) const
{
	if (ItemInstance == nullptr)
	{
		return false;
	}

	for (int32 SlotIndex = 0; SlotIndex < InventoryList.Entries.Num(); ++SlotIndex)
	{
		if (SlotIndex == IgnoredSlotIndex)
		{
			continue;
		}

		if (InventoryList.Entries[SlotIndex].Instance == ItemInstance)
		{
			return true;
		}
	}

	return false;
}
