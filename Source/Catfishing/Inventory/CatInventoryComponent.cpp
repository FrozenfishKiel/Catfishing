#include "Inventory/CatInventoryComponent.h"

#include "GameFramework/Pawn.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Inventory/CatInventoryItemInstance.h"
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

// 复制声明流程：只复制库存列表；实例对象通过 registered subobject list 单独登记。
void UCatInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, InventoryList);
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

// 槽位刷新流程：只在 authority 或单机路径补齐空槽；客户端通过复制拿到服务器数组。
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
	}
}

// 实例创建流程：解析最终实例类、以库存拥有者作为 Outer 创建，再绑定定义和运行宿主。
UCatInventoryItemInstance* UCatInventoryComponent::CreateInventoryItemInstance(
	const TSubclassOf<UCatInventoryItemDefinition> ItemDefinitionClass,
	const TSubclassOf<UCatInventoryItemInstance> ItemInstanceClass)
{
	if (ItemDefinitionClass == nullptr)
	{
		return nullptr;
	}

	AActor* OwningActor = GetOwner();
	if (OwningActor == nullptr)
	{
		return nullptr;
	}

	const TSubclassOf<UCatInventoryItemInstance> ResolvedItemClass =
		UCatInventoryItemDefinition::ResolveItemInstanceClass(ItemDefinitionClass, ItemInstanceClass);
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

	NewInstance->SetItemDefinitionClass(ItemDefinitionClass);
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

// 按定义入库流程：先填充同类可堆叠格，再为剩余数量创建新实例和新堆栈。
UCatInventoryItemInstance* UCatInventoryComponent::AddEntry(
	const TSubclassOf<UCatInventoryItemDefinition> ItemDefinitionClass,
	int32& InOutCount,
	bool& bOutFullyAdded,
	const TSubclassOf<UCatInventoryItemInstance> ItemInstanceClass)
{
	bOutFullyAdded = false;
	if (ItemDefinitionClass == nullptr || InOutCount <= 0 || GetOwner() == nullptr || !GetOwner()->HasAuthority())
	{
		return nullptr;
	}

	const UCatInventoryItemDefinition* TargetDefinition =
		GetDefault<UCatInventoryItemDefinition>(ItemDefinitionClass);
	if (TargetDefinition == nullptr)
	{
		return nullptr;
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

	while (InOutCount > 0)
	{
		const int32 TargetIndex = FindAvailableSlot(nullptr, InOutCount);
		if (TargetIndex == INDEX_NONE)
		{
			break;
		}

		UCatInventoryItemInstance* NewInstance = CreateInventoryItemInstance(ItemDefinitionClass, ItemInstanceClass);
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
		BroadcastInventoryChange();
	}

	return FirstAcceptedInstance;
}

// 按实例入库流程：堆叠物优先合并到同定义格；非堆叠或剩余数量再保留原实例并补建同类实例。
void UCatInventoryComponent::AddEntry(UCatInventoryItemInstance* ItemInstance, int32& InOutCount,
	bool& bOutFullyAdded)
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
				ItemInstance->GetItemDefinitionClass(),
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
		BroadcastInventoryChange();
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

// 条目移除流程：清空所有指向该实例的格子，再解除复制登记并广播一次本地通知。
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

// 现有实例添加流程：只在服务器执行；返回值表示这次请求的全部数量是否都被接收。
bool UCatInventoryComponent::AddItemInstance(UCatInventoryItemInstance* ItemInstance, const int32 Count)
{
	int32 RemainingCount = Count;
	bool bAdded = false;
	AddEntry(ItemInstance, RemainingCount, bAdded);
	return bAdded && RemainingCount == 0;
}

// 按定义公开添加入口保持服务器事务语义；返回 false 时调用方应把整次发货当失败处理。
bool UCatInventoryComponent::AddItemDefinition(
	const TSubclassOf<UCatInventoryItemDefinition> ItemDefinitionClass,
	const int32 Count,
	const TSubclassOf<UCatInventoryItemInstance> ItemInstanceClass)
{
	int32 RemainingCount = Count;
	bool bAdded = false;
	AddEntry(ItemDefinitionClass, RemainingCount, bAdded, ItemInstanceClass);
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

// 批次写入流程：先完整预检，再逐项正式入库；正常情况下不会出现半批失败。
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

	for (const FCatInventoryDefinitionEntry& DefinitionEntry : ReceiveBatch.DefinitionEntries)
	{
		int32 RemainingCount = DefinitionEntry.Count;
		bool bAdded = false;
		AddEntry(DefinitionEntry.ItemDefinitionClass, RemainingCount, bAdded, DefinitionEntry.ItemInstanceClass);
		if (!bAdded || RemainingCount != 0)
		{
			UE_LOG(LogCatInventory, Error, TEXT("Event=inventory_batch_apply_failed Owner=%s Source=Definition Count=%d Remaining=%d"),
				*GetNameSafe(OwningActor), DefinitionEntry.Count, RemainingCount);
			return false;
		}
	}

	for (const FCatInventoryInstanceEntry& InstanceEntry : ReceiveBatch.InstanceEntries)
	{
		int32 RemainingCount = InstanceEntry.Count;
		bool bAdded = false;
		AddEntry(InstanceEntry.ItemInstance, RemainingCount, bAdded);
		if (!bAdded || RemainingCount != 0)
		{
			UE_LOG(LogCatInventory, Error, TEXT("Event=inventory_batch_apply_failed Owner=%s Source=Instance Count=%d Remaining=%d"),
				*GetNameSafe(OwningActor), InstanceEntry.Count, RemainingCount);
			return false;
		}
	}

	UE_LOG(LogCatInventory, Log, TEXT("Event=inventory_batch_accepted Owner=%s Definitions=%d Instances=%d Slots=%d"),
		*GetNameSafe(OwningActor), ReceiveBatch.DefinitionEntries.Num(), ReceiveBatch.InstanceEntries.Num(),
		InventoryList.Entries.Num());
	return true;
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

// 实例移除流程：委托条目移除入口处理清空、复制登记和变化广播，避免形成两套清理规则。
void UCatInventoryComponent::RemoveItemInstance(UCatInventoryItemInstance* ItemInstance)
{
	RemoveEntry(ItemInstance);
}

// 下标移除流程：清空指定槽位，并在实例不再被本库存引用时解除复制登记。
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

	BroadcastInventoryChange(TargetIndex);
}

// 扣量流程：服务器验证槽位和数量后扣减；清空格子时才解除实例复制登记。
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

// 使用预检流程：默认使用拥有者 Pawn，槽位、实例和定义都有效后才交给实例语义判断。
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

// 使用提交流程：客户端只发请求，服务器让实例裁决是否扣量，扣量仍由库存组件统一执行。
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

// authority 交换流程：同定义可堆叠时优先合并，否则交换两个格子的实例和数量。
bool UCatInventoryComponent::ExecuteExchangeRequestOnAuthority(UCatInventoryComponent* DraggedInventory,
	const int32 DraggedSlotIndex, UCatInventoryComponent* DropInventory, const int32 DropSlotIndex)
{
	if (!CanExecuteExchangeRequest(DraggedInventory, DraggedSlotIndex, DropInventory, DropSlotIndex))
	{
		return false;
	}

	AActor* DraggedOwner = DraggedInventory->GetOwner();
	AActor* DropOwner = DropInventory->GetOwner();
	if (DraggedOwner == nullptr
		|| DropOwner == nullptr
		|| !DraggedOwner->HasAuthority()
		|| !DropOwner->HasAuthority())
	{
		return false;
	}

	FCatInventoryEntry& DraggedEntry = DraggedInventory->InventoryList.Entries[DraggedSlotIndex];
	FCatInventoryEntry& DropEntry = DropInventory->InventoryList.Entries[DropSlotIndex];
	UCatInventoryItemInstance* DraggedInstanceBefore = DraggedEntry.Instance;
	UCatInventoryItemInstance* DropInstanceBefore = DropEntry.Instance;

	const UCatInventoryItemDefinition* DraggedDefinition =
		DraggedEntry.Instance != nullptr ? DraggedEntry.Instance->GetItemDefinition() : nullptr;
	const UCatInventoryItemDefinition* DropDefinition =
		DropEntry.Instance != nullptr ? DropEntry.Instance->GetItemDefinition() : nullptr;

	if (DropEntry.Instance != nullptr
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
			return false;
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
		DraggedInventory->BroadcastInventoryChange(DraggedSlotIndex);
		DropInventory->BroadcastInventoryChange(DropSlotIndex);
		return true;
	}

	if (DropEntry.Instance == nullptr || DropEntry.StackCount <= 0)
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
	DraggedInventory->BroadcastInventoryChange(DraggedSlotIndex);
	DropInventory->BroadcastInventoryChange(DropSlotIndex);
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
		if (DefinitionEntry.Count <= 0 || DefinitionEntry.ItemDefinitionClass == nullptr)
		{
			return false;
		}

		if (UCatInventoryItemDefinition::ResolveItemInstanceClass(
				DefinitionEntry.ItemDefinitionClass,
				DefinitionEntry.ItemInstanceClass) == nullptr)
		{
			return false;
		}

		const UCatInventoryItemDefinition* ItemDefinition =
			GetDefault<UCatInventoryItemDefinition>(DefinitionEntry.ItemDefinitionClass);
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
