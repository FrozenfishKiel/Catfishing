#include "Inventory/CatInventoryComponent.h"
#include "Growth/CatGrowthComponent.h"

#include "GameFramework/Pawn.h"
#include "Character/CatCharacter.h"
#include "Engine/World.h"
#include "Equipment/CatEquipmentItemDefinition.h"
#include "Fishing/CatFishingService.h"
#include "Inventory/CatFishOnlyInventoryComponent.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Inventory/CatInventoryAccessRules.h"
#include "Camp/CatCampSettings.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/CatInventorySettings.h"
#include "Logging/CatLog.h"
#include "Net/UnrealNetwork.h"
#include "UI/Inventory/CatInventoryModel.h"

DEFINE_LOG_CATEGORY_STATIC(LogCatInventory, Log, All);

namespace
{
	// 钓鱼主动道具闸门（钓鱼规则 §3.3）：从咬钩成立到本竿结局落定，这只猫不能再主动掏用道具。
	// 1. 闸门只按使用者本人当前主控竿的会话阶段判；同场其他玩家照常能用道具、能为同一个窝补料。
	// 2. 抄网是收鱼出口，单独放行；经这条路径的其余物品一律拒绝——吃鱼、放鱼护、恢复品、背包里换装都在内。
	// 3. 不在这条路径上的两件事各自有归属：窝料投放由 ChumPlacementService 用同一条闸门拦，
	//    切饵走装备选择 RPC 且设计明确「到点后再切饵无意义但无害」（§3.2），本来就不归这条闸门管。
	// 4. 备装时已穿戴的被动效果不经过使用路径，不受影响。
	// 5. FishingService 只在服务器 Game World 创建，客户端预检查不到会话时按放行处理，权威提交那一层仍会拒。
	bool IsBlockedByActiveFishingItemGate(const AController* RequestingController, const APawn* UserPawn,
		const UCatInventoryItemDefinition* Definition)
	{
		const AController* Controller = RequestingController ? RequestingController
			: (UserPawn ? UserPawn->GetController() : nullptr);
		const UWorld* World = UserPawn ? UserPawn->GetWorld() : (Controller ? Controller->GetWorld() : nullptr);
		UCatFishingService* Fishing = World ? World->GetSubsystem<UCatFishingService>() : nullptr;
		if (!Fishing || !Fishing->IsActiveItemUseBlockedForController(Controller))
		{
			return false;
		}
		const UCatEquipmentItemDefinition* Equipment = Cast<UCatEquipmentItemDefinition>(Definition);
		return Equipment == nullptr || !Equipment->CanServeScoopNet();
	}

}


// 操作提交先查同请求载荷的终态，再复核当前槽位身份、数量与定义声明；副作用前占住请求，避免回调重入重复执行。
// Statics 负责载体操作和出售接线；本组件统一保存菜单命令结果，世界操作成功后再广播，出售通知由商店事务负责。
// 拒绝也记录结果；同请求同载荷只回放，载荷改变则拒绝，避免旧请求作用于后来换入的实例。
FCatDomainCommandResult UCatInventoryComponent::ExecuteItemActionFromAuthority(const FCatInventoryItemUseContext& Context,
	const FGuid ItemInstanceId, const FGameplayTag Action, const int32 Quantity)
{
	FCatDomainCommandResult Result; Result.RequestId = Context.RequestId;
	if (!GetOwner() || !GetOwner()->HasAuthority() || Context.SourceInventory != this || !Context.RequestId.IsValid())
	{ Result.Error = ECatDomainCommandError::PermissionDenied; return Result; }
	const FString Key = MakeTerminalKey(TEXT("ItemAction"), Context.RequestId);
	const FString Payload = FString::Printf(TEXT("%s|%d|%s|%s|%d|%d|%d|%s|%s|%s"), *GetPathNameSafe(Context.RequestingController),
		Context.InventorySlotIndex, *ItemInstanceId.ToString(), *Action.ToString(), Quantity,
		Context.bContinuousInput ? 1 : 0, Context.Target.bHasViewRay,
		*Context.Target.ViewOrigin.ToString(), *Context.Target.ViewDirection.ToString(), *GetPathNameSafe(Context.Target.Actor));
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		if (TerminalPayloadByKey.FindRef(Key) == Payload)
		{
			// 缓存保存的是首次终态；重试返回前必须改写为只读重放，避免表现层把第二次回执误当成又一次提交。
			Result = *Cached;
			MarkCommandReplayed(Result);
			return Result;
		}
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	const FCatInventoryEntry* Current = GetInventoryEntryAtSlot(Context.InventorySlotIndex);
	const UCatInventoryItemDefinition* Definition = Current && Current->Instance ? Current->Instance->GetItemDefinition() : nullptr;
	const FCatInventoryActionDefinition* Declared = Definition ? Definition->InventoryActions.FindByPredicate(
		[&](const FCatInventoryActionDefinition& Row) { return Row.Action == Action; }) : nullptr;
	FText Reason;
	if (!Current || !Current->Instance || Current->Instance->GetItemInstanceId() != ItemInstanceId || !ItemInstanceId.IsValid()
		|| !Declared || Quantity <= 0 || Quantity > Current->StackCount
		|| (Declared->QuantityMode == ECatInventoryActionQuantityMode::Single && Quantity != 1))
		Result.Error = ECatDomainCommandError::InvalidPayload;
	else if (HasPreparedRemoval()) Result.Error = ECatDomainCommandError::InvalidPhase;
	else if (!Context.UserPawn || Context.UserPawn->GetWorld() != GetWorld()
		|| (GetOwner() != Context.UserPawn && !CatInventoryAccessRules::IsHostReachable(GetOwner(), Cast<ACatCharacter>(Context.UserPawn), GetDefault<UCatCampSettings>())))
		Result.Error = ECatDomainCommandError::PermissionDenied;
	else if (!Current->Instance->CanExecuteInventoryAction(Action, *Current, Context.UserPawn, Reason))
		Result.Error = ECatDomainCommandError::PermissionDenied;
	else
	{
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		TerminalPayloadByKey.Add(Key, Payload); TerminalCache.Add(Key, Result);
		Result = UCatInventoryStatics::ExecuteResolvedInventoryActionFromAuthority(Context, Action, Quantity);
		Result.RequestId = Context.RequestId;
	}
	TerminalPayloadByKey.Add(Key, Payload); TerminalCache.Add(Key, Result);
	if (Result.bCommitted && Action != CatInventoryActionTags::Sell) BroadcastInventoryChange(Context.InventorySlotIndex);
	const FString Event = FString::Printf(
		TEXT("Event=inventory_action_resolved World=%s NetMode=%d Authority=1 RequestId=%s Instance=%s Action=%s Quantity=%d Committed=%d Replay=%d Pending=%d Error=%s Reason=%s"),
		*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), *Context.RequestId.ToString(), *ItemInstanceId.ToString(),
		*Action.ToString(), Quantity, Result.bCommitted, Result.bTerminalReplay, Result.bPending, *UEnum::GetValueAsString(Result.Error), *Reason.ToString());
	if (CatIsAcceptedDomainCommandResult(Result))
	{
		UE_LOG(LogCatInventory, Log, TEXT("%s"), *Event);
	}
	else
	{
		UE_LOG(LogCatInventory, Warning, TEXT("%s"), *Event);
	}
	return Result;
}

// 空格 owner 构造流程：新空格立即知道自己属于哪个库存组件，复制回调和调试输出使用已绑定宿主。
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

// 格子反向判断流程：复用正向匹配逻辑，保证移动和清理分支没有额外空值规则。
bool FCatInventoryEntry::operator!=(const UCatInventoryItemInstance* InInstance) const
{
	return !(*this == InInstance);
}

// 列表 owner 构造流程：复制回调需要回到拥有组件广播变化，因此在组件构造时绑定一次。
FCatInventoryList::FCatInventoryList(UCatInventoryComponent* InOwnerComponent)
	: OwnerComponent(InOwnerComponent)
{
}

// 物品快照读取流程：只收集非空实例，调用方拿到的是返回数组而不是可写库存容器。
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

// 复制删除回调只清除即将移除格子的本地观察数量；引擎此时尚未删除数组元素，不能在这里通知 UI。
void FCatInventoryList::PreReplicatedRemove(const TArrayView<int32> RemovedIndices, const int32 FinalSize)
{
	for (const int32 Index : RemovedIndices)
	{
		if (Entries.IsValidIndex(Index))
		{
			Entries[Index].LastObservedCount = 0;
		}
	}

}

// 复制新增回调流程：补齐本地 owner 和运行宿主，记录本端实际收到的非空格；完整列表通知由批次结束回调发出。
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
		if (OwnerComponent != nullptr && Entry.StackCount > 0)
		{
			UE_LOG(LogCatInventory, Log,
				TEXT("Event=inventory_replication_observed Kind=Add World=%s NetMode=%d Owner=%s Slot=%d Instance=%s Count=%d"),
				*GetPathNameSafe(OwnerComponent->GetWorld()), static_cast<int32>(OwnerComponent->GetNetMode()),
				*GetNameSafe(OwnerComponent->GetOwner()), Index,
				Entry.Instance ? *Entry.Instance->GetItemInstanceId().ToString() : TEXT("Unmapped"), Entry.StackCount);
		}
	}

}

// 复制变更回调流程：同步观察数量和本地 owner，记录变化格子（包括清空）；批次结束后消费者才重读完整库存。
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
		if (OwnerComponent != nullptr)
		{
			UE_LOG(LogCatInventory, Log,
				TEXT("Event=inventory_replication_observed Kind=Change World=%s NetMode=%d Owner=%s Slot=%d Instance=%s Count=%d"),
				*GetPathNameSafe(OwnerComponent->GetWorld()), static_cast<int32>(OwnerComponent->GetNetMode()),
				*GetNameSafe(OwnerComponent->GetOwner()), Index,
				Entry.Instance ? *Entry.Instance->GetItemInstanceId().ToString() : TEXT("NoneOrUnmapped"), Entry.StackCount);
		}
	}

}

// 等引擎真正删去尾部格子、完成新增及延迟对象映射后，再让 Model 复制最终数组并通知 UI。
// PreReplicatedRemove 中广播会把尚未删除的旧格写进 Model，之后没有通知可以纠正缩容结果。
void FCatInventoryList::PostReplicatedReceive(const FFastArraySerializer::FPostReplicatedReceiveParameters& Parameters)
{
	if (OwnerComponent)
	{
		OwnerComponent->BroadcastInventoryChange();
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

// 界面首次请求时才创建这份库存的 Model，并填入当前列表；关闭再打开复用同一对象，其他库存各有自己的实例。
UCatInventoryModel* UCatInventoryComponent::GetInventoryModel()
{
	if (!InventoryModel)
	{
		InventoryModel = NewObject<UCatInventoryModel>(this);
		InventoryModel->SetInventoryList(InventoryList.Entries);
	}
	return InventoryModel;
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

// 复制声明流程：复制库存列表；实例对象通过 registered subobject list 单独登记，终态缓存只留在本机内存。
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

// 子对象复制流程：registered subobject list 已经持有实例清单，父类实现就是唯一复制入口。
bool UCatInventoryComponent::ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch,
	FReplicationFlags* RepFlags)
{
	return Super::ReplicateSubobjects(Channel, Bunch, RepFlags);
}

// 槽位刷新流程：批次准备中或非 authority 时不写入；无拥有者的构造期也可补齐空槽。
// 按 NumSlots 只补不裁，新增格子后标脏并按开关通知；关闭通知不会关闭复制，客户端仍通过复制接收数组。
void UCatInventoryComponent::InitializeOrRefreshInventorySlots(const bool bBroadcastChange)
{
	// 批次准备期间禁止重入写格；原实例和数量保持到统一提交或取消。
	if (HasPreparedRemoval()) return;
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

		if (bBroadcastChange) BroadcastInventoryChange();
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

// 复制登记更新流程：实例离开原库存时移除原登记，进入新库存且复制已就绪时登记到新库存。
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
// 2. 再逐格比较占用状态、实例身份和堆叠数量；空格只要求同为空，不比较运行 owner。
// 3. 函数不读取 Equipment 或玩法定义，保证接收规则只围绕正式库存自己的内容裁决。
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

// 定义收货先拒绝预留期间或非权威调用，再计算可接收份数并准备、提交实例；拒绝时保持输入数量，完整接收标记为假。
// 提交后写回余数与完整接收标记；有新增数量才按开关广播，并返回按格位顺序找到的首个接收实例。
UCatInventoryItemInstance* UCatInventoryComponent::AddEntry(UCatInventoryItemDefinition* ItemDefinition,
	int32& InOutCount, bool& bOutFullyAdded, const TSubclassOf<UCatInventoryItemInstance> ItemInstanceClass,
	const bool bBroadcastChange)
{
	bOutFullyAdded = false;
	FCatInventoryReceiveBatch Batch;
	Batch.DefinitionEntries.Add({InOutCount, ItemDefinition, ItemInstanceClass});
	TArray<FInventoryIntakeSlot> Slots;
	int32 Remaining = InOutCount;
	if (HasPreparedRemoval() || !GetOwner() || !GetOwner()->HasAuthority()
		|| !AllocateInventoryIntake(Batch, Slots, &Remaining) || !ApplyInventoryIntake(Slots)) return nullptr;
	InOutCount = Remaining;
	bOutFullyAdded = Remaining == 0;
	for (const FInventoryIntakeSlot& Slot : Slots)
	{
		if (Slot.AddedCount <= 0) continue;
		if (bBroadcastChange) BroadcastInventoryChange();
		return Slot.Instance;
	}
	return nullptr;
}

// 实例收货先核对预留状态与权威，再共用分配和提交；首个新增格复用来源实例，合并时按分配转移唯一载体。
// 拒绝时保持输入数量并报告未完整接收；提交后写回余数和完整接收标记，仅数量增加且通知开启时广播。
void UCatInventoryComponent::AddEntry(UCatInventoryItemInstance* ItemInstance, int32& InOutCount,
	bool& bOutFullyAdded, const bool bBroadcastChange)
{
	bOutFullyAdded = false;
	FCatInventoryReceiveBatch Batch;
	Batch.InstanceEntries.Add({InOutCount, ItemInstance});
	TArray<FInventoryIntakeSlot> Slots;
	int32 Remaining = InOutCount;
	if (HasPreparedRemoval() || !GetOwner() || !GetOwner()->HasAuthority()
		|| !AllocateInventoryIntake(Batch, Slots, &Remaining) || !ApplyInventoryIntake(Slots)) return;
	const bool bChanged = Remaining != InOutCount;
	InOutCount = Remaining;
	bOutFullyAdded = Remaining == 0;
	if (bChanged && bBroadcastChange) BroadcastInventoryChange();
}

// 可用槽查找流程：如果传入实例可堆叠则优先找同类未满格；否则返回第一处空格。
int32 UCatInventoryComponent::FindAvailableSlot(UCatInventoryItemInstance* ItemInstance, const int32 Count) const
{
	(void)Count;

	FCatInventoryEntry IncomingEntry;
	IncomingEntry.Instance = ItemInstance;
	IncomingEntry.StackCount = 1;

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
				&& Entry.StackCount < GetMaxStackCountForDefinition(*TargetDefinition)
				&& CanAcceptInventoryEntryAtSlot(IncomingEntry, SlotIndex))
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
			if (ItemInstance == nullptr || CanAcceptInventoryEntryAtSlot(IncomingEntry, SlotIndex))
			{
				return SlotIndex;
			}
		}
	}

	return INDEX_NONE;
}

// 条目移除流程：清空所有指向该实例的格子；确实移除后才解除复制登记并广播一次完整重读。
void UCatInventoryComponent::RemoveEntry(UCatInventoryItemInstance* ItemInstance)
{
	// 批次准备期间禁止重入写格；原实例和数量保持到统一提交或取消。
	if (HasPreparedRemoval()) return;
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

// 库存持久化导出流程：
// 1. 服务器复制可见 entry，空格保持空 entry，保证 Save 保留格位顺序。
// 2. 再把 held entry 归入第一个空格；实例仍是同一个 UObject，不生成投影或副本。
// 3. 只有“空实例且零数量”才是空格；其他实例/数量组合都是损坏 entry，绝不在导出时静默抹平。
// 4. 每个非空 entry 必须具备已就绪定义、有效实例 ID 和合法堆叠数量，重复实例直接拒绝整份导出。
bool UCatInventoryComponent::ExportInventorySlotsFromAuthority(TArray<FCatInventoryEntry>& OutSlots,
	const int32 MaximumSlotCount, FText& OutFailure) const
{
	OutSlots.Reset();
	OutFailure = FText::GetEmpty();
	if (GetOwner() != nullptr && !GetOwner()->HasAuthority())
	{
		OutFailure = FText::FromString(TEXT("正式库存只能在服务器导出。"));
		return false;
	}
	const int32 SlotLimit = FMath::Max(0, MaximumSlotCount);
	OutSlots.SetNum(SlotLimit);
	TSet<FGuid> SeenIds;
	const auto CopyEntry = [&SeenIds, &OutFailure](const FCatInventoryEntry& Entry, FCatInventoryEntry& OutEntry)
	{
		if (Entry.Instance == nullptr && Entry.StackCount == 0)
		{
			OutEntry = FCatInventoryEntry();
			return true;
		}
		if (Entry.Instance == nullptr || Entry.StackCount <= 0)
		{
			OutFailure = FText::FromString(TEXT("正式库存含有实例与数量不一致的损坏 entry。"));
			return false;
		}
		const FGuid InstanceId = Entry.Instance->GetItemInstanceId();
		const UCatInventoryItemDefinition* Definition = Entry.Instance->GetItemDefinition();
		if (!InstanceId.IsValid() || Definition == nullptr || !Definition->IsInventoryRuntimeDefinitionReady()
			|| Entry.StackCount > Definition->GetMaxStackCount() || SeenIds.Contains(InstanceId))
		{
			OutFailure = FText::FromString(TEXT("正式库存含有无效或重复物品实例。"));
			return false;
		}
		SeenIds.Add(InstanceId);
		OutEntry = Entry;
		return true;
	};
	for (int32 Index = 0; Index < InventoryList.Entries.Num(); ++Index)
	{
		const FCatInventoryEntry& Entry = InventoryList.Entries[Index];
		const bool bEmptyEntry = Entry.Instance == nullptr && Entry.StackCount == 0;
		if (Index >= SlotLimit)
		{
			if (bEmptyEntry)
			{
				continue;
			}
			OutSlots.Reset();
			OutFailure = FText::FromString(TEXT("正式库存物品超过当前容量。"));
			return false;
		}
		if (!CopyEntry(Entry, OutSlots[Index]))
		{
			OutSlots.Reset();
			return false;
		}
	}
	for (const TPair<FGuid, FCatInventoryEntry>& Pair : ActiveHeldItemEntries)
	{
		FCatInventoryEntry* EmptyEntry = OutSlots.FindByPredicate([](const FCatInventoryEntry& Entry)
		{
			return Entry.Instance == nullptr && Entry.StackCount == 0;
		});
		if (EmptyEntry == nullptr || !CopyEntry(Pair.Value, *EmptyEntry))
		{
			OutSlots.Reset();
			if (OutFailure.IsEmpty()) { OutFailure = FText::FromString(TEXT("正式库存活动物品收回后超过当前容量。")); }
			return false;
		}
	}
	return true;
}

// 库存持久化恢复流程：
// 1. Save 已按磁盘 DTO 创建并恢复实例；库存先拒绝损坏 entry、未就绪定义、非法数量和重复实例。
// 2. 目标容量可能尚未初始化出格子，因此只临时补齐内存中的空 entry 供槽位规则读取，不广播半份恢复状态。
// 3. 验证失败会移除临时空 entry；全部通过后才一次性替换正式格子，避免部分恢复覆盖现有库存。
bool UCatInventoryComponent::RestoreTeamStorageRoleFromAuthority(const ECatTeamStorageRole Role)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || uint8(Role) > uint8(ECatTeamStorageRole::SupplyStore)) return false;
	if (TeamStorageRole == Role) return true;
	for (const FCatInventoryEntry& Entry : InventoryList.Entries)
		if (Entry.Instance) return false;
	if (!ActiveHeldItemEntries.IsEmpty()) return false;
	TeamStorageRole = Role;
	return true;
}

bool UCatInventoryComponent::RestoreInventorySlotsFromAuthority(const TArray<FCatInventoryEntry>& RestoredSlots,
	const int32 MinimumSlotCount, FText& OutFailure)
{
	// 批次准备期间禁止重入写格；原实例和数量保持到统一提交或取消。
	if (HasPreparedRemoval()) return false;
	OutFailure = FText::GetEmpty();
	if ((GetOwner() != nullptr && !GetOwner()->HasAuthority()) || RestoredSlots.Num() > FMath::Max(0, MinimumSlotCount)
		|| !ActiveHeldItemEntries.IsEmpty())
	{
		OutFailure = FText::FromString(TEXT("正式库存恢复上下文无效。"));
		return false;
	}
	const int32 RestoreSlotCount = FMath::Max(FMath::Max(0, MinimumSlotCount), RestoredSlots.Num());
	TSet<FGuid> SeenIds;
	for (const FCatInventoryEntry& Entry : RestoredSlots)
	{
		if (Entry.Instance == nullptr && Entry.StackCount == 0)
		{
			continue;
		}
		if (Entry.Instance == nullptr || Entry.StackCount <= 0)
		{
			OutFailure = FText::FromString(TEXT("正式库存恢复 entry 的实例与数量不一致。"));
			return false;
		}
		const FGuid InstanceId = Entry.Instance->GetItemInstanceId();
		const UCatInventoryItemDefinition* Definition = Entry.Instance->GetItemDefinition();
		if (!InstanceId.IsValid() || Definition == nullptr || !Definition->IsInventoryRuntimeDefinitionReady()
			|| Entry.StackCount > Definition->GetMaxStackCount()
			|| SeenIds.Contains(InstanceId))
		{
			OutFailure = FText::FromString(TEXT("正式库存恢复 entry 无效或重复。"));
			return false;
		}
		SeenIds.Add(InstanceId);
	}
	const int32 PreviousEntryCount = InventoryList.Entries.Num();
	while (InventoryList.Entries.Num() < RestoreSlotCount)
	{
		InventoryList.Entries.Add(FCatInventoryEntry(this));
	}
	const auto RemoveValidationEntries = [this, PreviousEntryCount]()
	{
		if (InventoryList.Entries.Num() > PreviousEntryCount)
		{
			InventoryList.Entries.SetNum(PreviousEntryCount);
		}
	};
	for (int32 SlotIndex = 0; SlotIndex < RestoredSlots.Num(); ++SlotIndex)
	{
		const FCatInventoryEntry& Entry = RestoredSlots[SlotIndex];
		if (Entry.Instance != nullptr && !CanAcceptInventoryEntryAtSlot(Entry, SlotIndex))
		{
			RemoveValidationEntries();
			OutFailure = FText::FromString(TEXT("正式库存恢复 entry 不符合目标容器槽位规则。"));
			return false;
		}
	}
	RemoveValidationEntries();
	if (!ReplaceInventoryEntriesFromAuthority(RestoredSlots, RestoreSlotCount))
	{
		OutFailure = FText::FromString(TEXT("正式库存恢复替换槽位失败。"));
		return false;
	}
	NumSlots = RestoreSlotCount;
	return true;
}

// 现有实例添加流程：只在服务器执行；成功时 AddEntry 负责写格并按需广播，返回值表示这次请求的全部数量是否都被接收。
bool UCatInventoryComponent::AddItemInstance(UCatInventoryItemInstance* ItemInstance, const int32 Count)
{
	int32 RemainingCount = Count;
	bool bAdded = false;
	AddEntry(ItemInstance, RemainingCount, bAdded);
	return bAdded && RemainingCount == 0;
}

// 按定义公开添加入口保持服务器事务语义；成功时 AddEntry 负责写格并按需广播，返回 false 时调用方应把整次发货当失败处理。
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

	TArray<FInventoryIntakeSlot> Slots;
	return AllocateInventoryIntake(ReceiveBatch, Slots);
}

// 批次收货先检查预留状态与权威，空批次直接成功；其他批次要求统一收货开启且每项都能完整分配。
// 实例全部准备好且原条目未被重入改变后才提交载体、条目、宿主和复制；拒绝不提交本批次，也不回滚其他调用的变更。
// 非空批次提交成功后按开关统一通知并记录结果；分配或准备失败只记录拒绝，不广播。
bool UCatInventoryComponent::TryAddInventoryBatch(const FCatInventoryReceiveBatch& ReceiveBatch,
	const bool bBroadcastChange)
{
	if (HasPreparedRemoval() || !GetOwner() || !GetOwner()->HasAuthority()) return false;
	if (ReceiveBatch.IsEmpty()) return true;
	TArray<FInventoryIntakeSlot> Slots;
	if (!CanReceiveUnifiedInventoryIntake() || !AllocateInventoryIntake(ReceiveBatch, Slots)
		|| !ApplyInventoryIntake(Slots))
	{
		UE_LOG(LogCatInventory, Warning, TEXT("Event=inventory_batch_rejected Owner=%s Definitions=%d Instances=%d"),
			*GetNameSafe(GetOwner()), ReceiveBatch.DefinitionEntries.Num(), ReceiveBatch.InstanceEntries.Num());
		return false;
	}
	if (bBroadcastChange) BroadcastInventoryChange();
	UE_LOG(LogCatInventory, Log, TEXT("Event=inventory_batch_accepted Owner=%s Definitions=%d Instances=%d Slots=%d"),
		*GetNameSafe(GetOwner()), ReceiveBatch.DefinitionEntries.Num(), ReceiveBatch.InstanceEntries.Num(), Slots.Num());
	return true;
}

// 已解析定义发货预检流程：
// 1. 先处理已缓存载荷，允许同一请求在提交前后重复询问，但不允许换定义或数量。
// 2. 首次预检必须确认 authority、稳定 ID、定义运行配置和数量都有效，避免来源系统传入半配置资产。
// 3. 最后只用正式库存批次预演容量，调用方不能在自己系统里复制堆叠和格子规则。
ECatDomainCommandError UCatInventoryComponent::ValidateResolvedInventoryDefinitionGrantFromAuthority(
	const FGuid RequestId, UCatInventoryItemDefinition* ItemDefinition, const int32 Count) const
{
	const int32 ItemId = ItemDefinition ? ItemDefinition->GetItemId() : 0;
	const FString Key = MakeTerminalKey(TEXT("GrantInventoryDefinition"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("Definition=%s|Count=%d"),
		*FString::FromInt(ItemId), Count);
	if (const FString* CachedPayload = TerminalPayloadByKey.Find(Key))
	{
		return *CachedPayload == PayloadSignature
			? ECatDomainCommandError::None
			: ECatDomainCommandError::InvalidPayload;
	}

	const AActor* OwningActor = GetOwner();
	if (!RequestId.IsValid() || OwningActor == nullptr || !OwningActor->HasAuthority()
		|| (ItemId == 0) || Count <= 0 || ItemDefinition == nullptr
		|| !ItemDefinition->IsInventoryRuntimeDefinitionReady())
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

// 已解析定义发货提交流程：
// 1. 以请求 ID 查本组件缓存，并用定义的稳定 ID 与数量核对载荷；同载荷只返回首次结果，换载荷拒绝。
// 2. 首次请求必须在 authority 且定义运行配置完整，再按当前容量和堆叠规则裁决是否可写入。
// 3. 容量通过后静默整批收货；容量不足或实例准备失败分别返回拒绝，不发布库存变化。
// 4. 先记录首次结果和载荷，再对成功提交广播，保证观察者重放不会再次发货；最后写入诊断日志。
FCatDomainCommandResult UCatInventoryComponent::GrantResolvedInventoryDefinitionFromAuthority(
	const FGuid RequestId, UCatInventoryItemDefinition* ItemDefinition, const int32 Count)
{
	const int32 ItemId = ItemDefinition ? ItemDefinition->GetItemId() : 0;
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString Key = MakeTerminalKey(TEXT("GrantInventoryDefinition"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("Definition=%s|Count=%d"),
		*FString::FromInt(ItemId), Count);
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		const FString* CachedPayload = TerminalPayloadByKey.Find(Key);
		if (!CachedPayload || *CachedPayload != PayloadSignature)
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;

			return Result;
		}
		Result = *Cached;
		MarkCommandReplayed(Result);
		return Result;
	}

	const AActor* OwningActor = GetOwner();
	if (!RequestId.IsValid() || OwningActor == nullptr || !OwningActor->HasAuthority()
		|| (ItemId == 0) || Count <= 0 || ItemDefinition == nullptr
		|| !ItemDefinition->IsInventoryRuntimeDefinitionReady())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
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
		else if (TryAddInventoryBatch(ReceiveBatch, false))
		{
			Result.bCommitted = true;
			Result.Error = ECatDomainCommandError::None;
		}
		else
		{
			Result.Error = ECatDomainCommandError::DependencyUnavailable;
		}
	}

	TerminalCache.Add(Key, Result);
	TerminalPayloadByKey.Add(Key, PayloadSignature);
	if (Result.bCommitted) BroadcastInventoryChange();
	UE_LOG(LogCatInventory, Log,
		TEXT("Event=inventory_definition_grant Owner=%s Request=%s Committed=%s Error=%s Definition=%s Count=%d"),
		*GetNameSafe(OwningActor), *RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		Result.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Error),
		*FString::FromInt(ItemId), Count);
	return Result;
}

// 统一收货开关读取流程：只返回配置事实，不检查容量或 authority。
bool UCatInventoryComponent::CanReceiveUnifiedInventoryIntake() const
{
	return bAllowUnifiedInventoryIntake;
}

// 收货优先级读取流程：数值越高越靠前，保持正式统一收货排序稳定。
int32 UCatInventoryComponent::GetUnifiedInventoryIntakePriority() const
{
	return UnifiedInventoryIntakePriority;
}

// 槽位容量刷新流程：批次准备中或非 authority 时拒绝；服务器或无拥有者的构造期将配置限制为非负值，再补齐空槽。
// 刷新不裁掉已有格子，通知开关交给补槽流程；未新增格子时不广播，复制标脏不受开关影响。
void UCatInventoryComponent::SetInventorySlotCountFromAuthority(const int32 NewSlotCount, const bool bBroadcastChange)
{
	// 批次准备期间禁止重入写格；原实例和数量保持到统一提交或取消。
	if (HasPreparedRemoval()) return;
	AActor* OwningActor = GetOwner();
	if (OwningActor != nullptr && !OwningActor->HasAuthority())
	{
		return;
	}

	NumSlots = FMath::Max(0, NewSlotCount);
	InitializeOrRefreshInventorySlots(bBroadcastChange);
}

// 整表替换流程：
// 1. 只允许 authority 或尚未绑定 Actor 的构造/恢复路径写入，客户端不能用它覆盖复制事实。
// 2. 先比较格位、实例和数量；内容没变时只补齐本地 owner，不广播库存变化。
// 3. 内容变化时移除原实例复制登记，按目标数量调整数组并在原格上写入内容；保留已有格子的复制身份，避免恢复/回滚让客户端格序漂移。
// 4. 每格（包括空格）标记内容变化，有效实例补齐运行宿主和复制登记，最后广播完整变化。
bool UCatInventoryComponent::ReplaceInventoryEntriesFromAuthority(
	const TArray<FCatInventoryEntry>& NewEntries, const int32 MinimumSlotCount, const bool bBroadcastChange)
{
	// 批次准备期间禁止重入写格；原实例和数量保持到统一提交或取消。
	if (HasPreparedRemoval()) return false;
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
	InventoryList.Entries.SetNum(DesiredSlotCount);
	for (int32 SlotIndex = 0; SlotIndex < DesiredSlotCount; ++SlotIndex)
	{
		FCatInventoryEntry& TargetEntry = InventoryList.Entries[SlotIndex];
		TargetEntry = FCatInventoryEntry(this);
		TargetEntry.LastObservedCount = 0;
		InventoryList.MarkItemDirty(TargetEntry);
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

	// 跨库存与经济的一次提交先完成两端事实，再由调用方通知观察者；其他调用仍保持原有立即广播。
	if (bBroadcastChange) BroadcastInventoryChange();
	return true;
}

// 实例移除流程：委托条目移除入口处理清空、复制登记和变化广播，避免形成两套清理规则。
void UCatInventoryComponent::RemoveItemInstance(UCatInventoryItemInstance* ItemInstance)
{
	RemoveEntry(ItemInstance);
}

// 下标移除流程：
// 1. 公开清空入口表达“清空这个位置”的语义，合法空槽也会发布一次清空提交，避免外部调用观察不到确认。
// 2. 非空实例离开最后一个格子时解除复制登记，防止客户端继续收到已经不归库存持有的子对象。
// 3. 最后按槽位广播变化；需要拿走实例身份的部署/转移流程改用带返回值的 entry 移出入口。
void UCatInventoryComponent::RemoveItemInstanceFromIndex(const int32 TargetIndex)
{
	// 批次准备期间禁止重入写格；原实例和数量保持到统一提交或取消。
	if (HasPreparedRemoval()) return;
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

// 槽位 entry 移出流程：
// 1. 只允许服务器移出非空正式格，失败时输出保持空 entry，避免调用方误拿失效实例。
// 2. 成功时先复制被移出的实例和数量，再清空正式槽位并维护复制子对象登记。
// 3. 最后广播具体槽位变化，让部署或转移流程从库存事实源拿到同一份实例。
bool UCatInventoryComponent::RemoveInventoryEntryAtSlotFromAuthority(
	const int32 TargetIndex, FCatInventoryEntry& OutRemovedEntry)
{
	// 批次准备期间禁止重入写格；原实例和数量保持到统一提交或取消。
	if (HasPreparedRemoval()) return false;
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

	BroadcastInventoryChange(TargetIndex);
	return true;
}

// 先按实例找原格，拒绝无权威、无效请求、离库预留、已借出或非独立单件；校验失败保持可见库存和保管记录不变。
// 静默扣除原格后建立同一实例的活动保管记录并填写结果，最后通知；观察者看到的格子与保管记录已完成交接。
// RequestId 关联部署日志而不缓存终态，外层创建鱼竿失败时仍可归还后重新借出同一件物品。
FCatDomainCommandResult UCatInventoryComponent::HoldInventoryItemInstanceFromAuthority(
	const FGuid RequestId, const FGuid ItemInstanceId, FCatInventoryEntry& OutHeldEntry)
{
	OutHeldEntry = FCatInventoryEntry(this);
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const int32 SlotIndex = FindInventorySlotIndexFromInstanceId(ItemInstanceId);
	const FCatInventoryEntry* Entry = GetInventoryEntryAtSlot(SlotIndex);
	const UCatInventoryItemDefinition* Definition = Entry && Entry->Instance ? Entry->Instance->GetItemDefinition() : nullptr;
	if (!GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid() || !ItemInstanceId.IsValid())
		Result.Error = ECatDomainCommandError::InvalidPayload;
	else if (HasPreparedRemoval() || ActiveHeldItemEntries.Contains(ItemInstanceId))
		Result.Error = ECatDomainCommandError::InvalidPhase;
	else if (!Entry) Result.Error = ECatDomainCommandError::NotFound;
	else if (!Definition || Entry->StackCount != 1 || GetMaxStackCountForDefinition(*Definition) > 1)
		Result.Error = ECatDomainCommandError::InvalidPhase;
	else
	{
		const FCatInventoryEntry Held = *Entry;
		if (ConsumeItemAtSlot(SlotIndex, 1, false))
		{
			ActiveHeldItemEntries.Add(ItemInstanceId, Held);
			OutHeldEntry = Held;
			Result.bCommitted = true;
			Result.Error = ECatDomainCommandError::None;
			BroadcastInventoryChange(SlotIndex);
		}
		else Result.Error = ECatDomainCommandError::InvalidPhase;
	}
	UE_LOG(LogCatInventory, Log, TEXT("Event=inventory_hold_item_instance Owner=%s Request=%s Item=%s Committed=%d Error=%s"),
		*GetNameSafe(GetOwner()), *RequestId.ToString(), *ItemInstanceId.ToString(), Result.bCommitted, *UEnum::GetValueAsString(Result.Error));
	return Result;
}

// 先读取保管记录并校验权威、请求、独立单件和可见库存无重复实例；离库预留或非法状态直接拒绝。
// 静默设置最低槽数并把原实例整件入库，成功后删除活动记录、填写输出再通知；入库失败保留活动记录，但不撤销已补齐的空槽。
// 本入口不缓存请求终态，外层仍可在部署回退时重新借出；跨玩家接管与保存继续读取同一张保管表。
FCatDomainCommandResult UCatInventoryComponent::ReturnHeldInventoryItemInstanceFromAuthority(
	const FGuid RequestId, const FGuid ItemInstanceId, const int32 MinimumSlotCount, FCatInventoryEntry& OutReturnedEntry)
{
	OutReturnedEntry = FCatInventoryEntry(this);
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FCatInventoryEntry* Held = ActiveHeldItemEntries.Find(ItemInstanceId);
	const UCatInventoryItemDefinition* Definition = Held && Held->Instance ? Held->Instance->GetItemDefinition() : nullptr;
	if (!GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid() || !ItemInstanceId.IsValid())
		Result.Error = ECatDomainCommandError::InvalidPayload;
	else if (!Held) Result.Error = ECatDomainCommandError::NotFound;
	else if (HasPreparedRemoval() || !Definition || Held->StackCount != 1 || GetMaxStackCountForDefinition(*Definition) > 1
		|| FindInventorySlotIndexFromInstance(Held->Instance) != INDEX_NONE)
		Result.Error = ECatDomainCommandError::InvalidPhase;
	else
	{
		const FCatInventoryEntry Returning = *Held;
		SetInventorySlotCountFromAuthority(MinimumSlotCount, false);
		FCatInventoryReceiveBatch Batch;
		Batch.InstanceEntries.Add({1, Returning.Instance});
		if (TryAddInventoryBatch(Batch, false))
		{
			OutReturnedEntry = Returning;
			ActiveHeldItemEntries.Remove(ItemInstanceId);
			Result.bCommitted = true;
			Result.Error = ECatDomainCommandError::None;
			BroadcastInventoryChange();
		}
		else Result.Error = ECatDomainCommandError::CapacityExceeded;
	}
	UE_LOG(LogCatInventory, Log, TEXT("Event=inventory_return_held_item_instance Owner=%s Request=%s Item=%s MinimumSlotCount=%d Committed=%d Error=%s"),
		*GetNameSafe(GetOwner()), *RequestId.ToString(), *ItemInstanceId.ToString(), MinimumSlotCount, Result.bCommitted, *UEnum::GetValueAsString(Result.Error));
	return Result;
}

// 活动区持有退役流程：
// 1. 只允许服务器按实例 ID 移除活动区记录，客户端不能让部署物品从库存生命周期里消失。
// 2. 退役表示存档或外部权威已经接管这件物品，因此不重新放回可见格，也不广播库存内容变化。
// 3. 找不到 held entry 时返回 false，让调用方暴露库存事实和玩法记录已经分叉。
bool UCatInventoryComponent::RetireHeldInventoryEntryFromAuthority(const FGuid ItemInstanceId)
{
	AActor* OwningActor = GetOwner();
	if (OwningActor == nullptr || !OwningActor->HasAuthority() || !ItemInstanceId.IsValid())
	{
		return false;
	}

	const int32 RemovedCount = ActiveHeldItemEntries.Remove(ItemInstanceId);
	if (RemovedCount <= 0)
	{
		return false;
	}

	UE_LOG(LogCatInventory, Log, TEXT("Event=inventory_retire_held_item Owner=%s Instance=%s"),
		*GetNameSafe(OwningActor), *ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens));
	return true;
}

// 活动 entry 只读查找流程：拒绝非 authority 或无效实例 ID，随后返回保管表中的记录；查询不补槽、不归还也不退役物品。
const FCatInventoryEntry* UCatInventoryComponent::FindHeldInventoryEntryFromAuthority(
	const FGuid ItemInstanceId) const
{
	if (GetOwner() == nullptr || !GetOwner()->HasAuthority() || !ItemInstanceId.IsValid())
	{
		return nullptr;
	}

	return ActiveHeldItemEntries.Find(ItemInstanceId);
}

// 活动 entry 快照追加流程：
// 1. 只在服务器侧读取活动区，客户端没有 authority 时拿不到活动区保管状态，避免 UI 把它当可见背包。
// 2. 逐条复核 key、实例 ID、数量和不可堆叠约束，坏记录不会进入保存候选载荷。
// 3. 输出是 entry 副本，调用方可以转换或校验，但不能通过返回数组修改库存活动区。
void UCatInventoryComponent::AppendHeldInventoryEntriesFromAuthority(TArray<FCatInventoryEntry>& OutEntries) const
{
	if (GetOwner() == nullptr || !GetOwner()->HasAuthority())
	{
		return;
	}

	for (const TPair<FGuid, FCatInventoryEntry>& Pair : ActiveHeldItemEntries)
	{
		const FCatInventoryEntry& Entry = Pair.Value;
		const UCatInventoryItemDefinition* HeldDefinition =
			Entry.Instance != nullptr ? Entry.Instance->GetItemDefinition() : nullptr;
		const FGuid EntryInstanceId = Entry.Instance != nullptr ? Entry.Instance->GetItemInstanceId() : FGuid();
		if (!Pair.Key.IsValid() || EntryInstanceId != Pair.Key || Entry.StackCount != 1
			|| HeldDefinition == nullptr || GetMaxStackCountForDefinition(*HeldDefinition) > 1)
		{
			continue;
		}
		OutEntries.Add(Entry);
	}
}

// 活动 entry 存在性查询流程：
// 1. 只把当前服务器库存活动区视为正式部署占用，Equipment 读模型不能覆盖这份事实。
// 2. 查询同样复核不可堆叠单实例约束，坏记录不会让恢复或失败预算永久卡死。
// 3. 命中任意有效 held entry 就返回 true，调用方据此延后会改写库存或物品状态的事务。
bool UCatInventoryComponent::HasActiveHeldInventoryEntriesFromAuthority() const
{
	if (GetOwner() == nullptr || !GetOwner()->HasAuthority())
	{
		return false;
	}

	for (const TPair<FGuid, FCatInventoryEntry>& Pair : ActiveHeldItemEntries)
	{
		const FCatInventoryEntry& Entry = Pair.Value;
		const UCatInventoryItemDefinition* HeldDefinition =
			Entry.Instance != nullptr ? Entry.Instance->GetItemDefinition() : nullptr;
		const FGuid EntryInstanceId = Entry.Instance != nullptr ? Entry.Instance->GetItemInstanceId() : FGuid();
		if (Pair.Key.IsValid() && EntryInstanceId == Pair.Key && Entry.StackCount == 1
			&& HeldDefinition != nullptr && GetMaxStackCountForDefinition(*HeldDefinition) <= 1)
		{
			return true;
		}
	}
	return false;
}

// 离库准备流程：
// 1. 先拒绝非 authority、无效请求或已有预留，保证一份库存同一时间只有一个同步事务。
// 2. 再逐个按实例 GUID 反查当前槽位，要求它仍是非空完整条目，并拒绝重复槽位。
// 3. 校验全部通过后只保存槽位和请求号，不改条目、运行宿主、复制登记或观察者通知。
// 4. 预留只跨同一游戏线程调用栈，外层结算拒绝时 Finish 释放锁即可恢复原状。
bool UCatInventoryComponent::PrepareRemovalFromAuthority(const FGuid RequestId, const TArray<FGuid>& ItemIds)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid() || HasPreparedRemoval()) return false;
	TArray<int32> Slots;
	for (const FGuid& Id : ItemIds)
	{
		const int32 Slot = FindInventorySlotIndexFromInstanceId(Id);
		const FCatInventoryEntry* Entry = GetInventoryEntryAtSlot(Slot);
		if (!Id.IsValid() || !Entry || !Entry->Instance || Entry->StackCount <= 0 || Slots.Contains(Slot)) return false;
		Slots.Add(Slot);
	}
	PreparedRemovalSlots = MoveTemp(Slots);
	PreparedRemovalRequest = RequestId;
	return true;
}

// 同请求完成预留：
// 1. 只接受与准备阶段相同的 RequestId，避免其它事务释放或提交本批槽位。
// 2. 提交时清空已经独占的原槽并在实例不再被其它槽引用时解除子对象复制；取消时保持条目原样。
// 3. 随后清掉请求号和槽位锁，并按调用方指定时机广播；外层可在所有库存与世界实物完成后统一通知观察者。
void UCatInventoryComponent::FinishRemovalFromAuthority(const FGuid RequestId, const bool bCommit, const bool bBroadcast)
{
	if (!RequestId.IsValid() || PreparedRemovalRequest != RequestId) return;
	if (bCommit)
	{
		for (const int32 Slot : PreparedRemovalSlots)
		{
			UCatInventoryItemInstance* Item = InventoryList.Entries[Slot].Instance;
			InventoryList.Entries[Slot] = FCatInventoryEntry(this);
			InventoryList.MarkItemDirty(InventoryList.Entries[Slot]);
			if (Item && IsUsingRegisteredSubObjectList() && !IsItemInstanceReferencedByOtherSlots(Item, Slot)) RemoveReplicatedSubObject(Item);
		}
	}
	PreparedRemovalSlots.Reset();
	PreparedRemovalRequest.Invalidate();
	UE_LOG(LogCatInventory, Log, TEXT("Event=inventory_removal_finished RequestId=%s Owner=%s Committed=%d World=%s NetMode=%d"),
		*RequestId.ToString(), *GetNameSafe(GetOwner()), bCommit, *GetNameSafe(GetWorld()), GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone);
	if (bCommit && bBroadcast) BroadcastInventoryChange();
}

// 能力成本流程：按请求与实例去重，先占住处理状态再扣真实数量；缓存终态后按开关发布，延迟通知不延迟真实扣量。
FCatDomainCommandResult UCatInventoryComponent::ConsumeAbilityItemFromAuthority(FGuid RequestId, FGuid ItemId, int32 Quantity, bool bPublishChange)
{
	FCatDomainCommandResult Result; Result.RequestId = RequestId;
	if (!GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid() || !ItemId.IsValid() || Quantity < 0)
	{ Result.Error = ECatDomainCommandError::InvalidPayload; return Result; }
	const FString Key = MakeTerminalKey(TEXT("AbilityCost"), RequestId);
	const FString Payload = FString::Printf(TEXT("%s|%d"), *ItemId.ToString(), Quantity);
	if (const auto* Cached = TerminalCache.Find(Key))
	{
		if (TerminalPayloadByKey.FindRef(Key) != Payload) { Result.Error = ECatDomainCommandError::InvalidPayload; return Result; }
		Result = *Cached; MarkCommandReplayed(Result); return Result;
	}
	Result.Error = ECatDomainCommandError::AlreadyResolved;
	TerminalCache.Add(Key, Result); TerminalPayloadByKey.Add(Key, Payload);
	const int32 Slot = FindInventorySlotIndexFromInstanceId(ItemId);
	// 零数量仍需记录来源与请求终态，永久道具的相同输入重放不能再次产生效果。
	Result.bCommitted = Quantity == 0 ? Slot != INDEX_NONE : ConsumeItemAtSlot(Slot, Quantity, false);
	Result.Error = Result.bCommitted ? ECatDomainCommandError::None : ECatDomainCommandError::InvalidPayload;
	TerminalCache.Add(Key, Result);
	if (Result.bCommitted && Quantity > 0 && bPublishChange) BroadcastInventoryChange(Slot);
	return Result;
}

// 扣量流程：先拒绝批次准备中、无权威、非法格位、非正数或数量不足的请求，失败不改变库存。
// 扣量后同步观察数量并标脏；归零则清格，仅在无其他格引用且启用注册子对象复制时注销实例。
// 最后按开关发布该格变化；静默扣量仍保留复制标记，由业务调用方在关联资源提交完整后通知观察者。
bool UCatInventoryComponent::ConsumeItemAtSlot(const int32 SlotIndex, const int32 ConsumeCount, const bool bBroadcastChange)
{
	// 批次准备期间禁止重入写格；原实例和数量保持到统一提交或取消。
	if (HasPreparedRemoval()) return false;
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

	if (bBroadcastChange) BroadcastInventoryChange(SlotIndex);
	return true;
}

// 世界动作枚举沿用调用方契约，只转换成同一菜单命令；来源校验、去重与通知由 ExecuteItemAction 统一负责。
FCatDomainCommandResult UCatInventoryComponent::ReleaseItemToWorldFromAuthority(ACatCharacter* Character,
	const FGuid RequestId, const int32 SlotIndex, const FGuid ItemInstanceId, const int32 Quantity, const ECatInventoryWorldAction Action)
{
	FCatInventoryItemUseContext Context;
	Context.RequestId = RequestId;
	Context.UserPawn = Character;
	Context.RequestingController = Character ? Character->GetController() : nullptr;
	Context.SourceInventory = this;
	Context.InventorySlotIndex = SlotIndex;
	const FGameplayTag Tag = Action == ECatInventoryWorldAction::Drop ? CatInventoryActionTags::Drop
		: Action == ECatInventoryWorldAction::Place ? CatInventoryActionTags::Place
		: Action == ECatInventoryWorldAction::Carry ? CatInventoryActionTags::Carry : FGameplayTag();
	return ExecuteItemActionFromAuthority(Context, ItemInstanceId, Tag, Quantity);
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
// 2. 查询只信任已绑定实例和正堆叠数量，因为空格、坏复制或读模型都不能证明玩家拥有材料。
// 3. 返回第一格保持“先找到先消费”的项目口径；真正扣量仍由 ConsumeItemAtSlot 重新校验。
int32 UCatInventoryComponent::FindFirstInventorySlotIndexByItemId(const int32  ItemId) const
{
	if ((ItemId == 0))
	{
		return INDEX_NONE;
	}

	for (int32 SlotIndex = 0; SlotIndex < InventoryList.Entries.Num(); ++SlotIndex)
	{
		const FCatInventoryEntry& Entry = InventoryList.Entries[SlotIndex];
		if (Entry.Instance != nullptr
			&& Entry.StackCount > 0
			&& Entry.Instance->GetItemId() == ItemId)
		{
			return SlotIndex;
		}
	}

	return INDEX_NONE;
}

// 可见库存数量汇总流程：
// 1. 空定义 ID 直接返回 0，避免调用方把缺配置当成“任意物品都有数量”。
// 2. 只遍历 InventoryList 的正式可见槽位，要求实例有效、数量为正且定义 ID 精确匹配。
// 3. held 活动区和 Fishing 会话冻结不从这里叠加，因为它们已经离开玩家当前可整理、可选择的背包格。
int32 UCatInventoryComponent::CountVisibleInventoryQuantityByItemId(const int32  ItemId) const
{
	if ((ItemId == 0))
	{
		return 0;
	}

	int32 Quantity = 0;
	for (const FCatInventoryEntry& Entry : InventoryList.Entries)
	{
		if (Entry.Instance != nullptr
			&& Entry.StackCount > 0
			&& Entry.Instance->GetItemId() == ItemId)
		{
			Quantity += Entry.StackCount;
		}
	}
	return Quantity;
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

// 使用预检流程：默认使用拥有者 Pawn，槽位、实例和定义都有效后才过钓鱼道具闸门，最后交给实例自己的真实 Use 语义判断。
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

	if (IsBlockedByActiveFishingItemGate(nullptr, UserPawn, Entry.Instance->GetItemDefinition()))
	{
		return false;
	}

	return Entry.Instance->CanUseFromInventory(Entry, UserPawn);
}

// authority 库存移动流程：
// 1. 先用 Source 组件自己的终态缓存处理同一 RequestId 重放；载荷变化会被拒绝而不读取当前格子。
// 2. 再复核 Source/Target 的 authority、当前槽位和目标组件；服务器按此刻条目执行，不接受客户端提供内容快照。
// 3. 通过后复用内部交换规则完成合并、搬空格或互换，并由实际变更决定是否通知双方库存。
// 4. 先缓存首次结果和载荷，再向发生变化的来源与目标广播；通知中的同请求重放只读取缓存，不重复交换。
FCatDomainCommandResult UCatInventoryComponent::MoveItemToInventoryFromAuthority(const FGuid RequestId,
	const int32 SourceSlotIndex,
	UCatInventoryComponent* TargetInventory, const int32 TargetSlotIndex, const FString& IdempotencyPayloadContext)
{
	// 本库存的整批离库已占用，禁止在物品副作用之前开启另一事务。
	if (HasPreparedRemoval()) { FCatDomainCommandResult Rejected; Rejected.RequestId = RequestId; Rejected.Error = ECatDomainCommandError::InvalidPhase; return Rejected; }
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;

	const FString PayloadSignature = FString::Printf(
		TEXT("Context=%s|SourceInventory=%s|SourceSlot=%d|TargetInventory=%s|TargetSlot=%d"),
		*IdempotencyPayloadContext, *GetPathNameSafe(this), SourceSlotIndex,
		*GetPathNameSafe(TargetInventory), TargetSlotIndex);
	const FString Key = MakeTerminalKey(TEXT("MoveInventoryItem"), RequestId);
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		const FString* CachedPayload = TerminalPayloadByKey.Find(Key);
		if (!CachedPayload || *CachedPayload != PayloadSignature)
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
			return Result;
		}
		Result = *Cached;
		MarkCommandReplayed(Result);
		return Result;
	}

	AActor* SourceOwner = GetOwner();
	AActor* TargetOwner = TargetInventory != nullptr ? TargetInventory->GetOwner() : nullptr;
	if (!RequestId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (SourceOwner == nullptr || TargetOwner == nullptr
		|| !SourceOwner->HasAuthority() || !TargetOwner->HasAuthority())
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else if (TargetInventory == nullptr)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else
	{
		const FInventoryExchangeMutation Mutation = ExecuteExchangeRequestOnAuthorityInternal(
			this, SourceSlotIndex, TargetInventory, TargetSlotIndex);
		Result.bCommitted = Mutation.bChanged;
		Result.Error = Mutation.Error;

	}

	TerminalCache.Add(Key, Result);
	TerminalPayloadByKey.Add(Key, PayloadSignature);
	// 先缓存终态，观察者即使在通知中重放交换，也不能把刚换好的两个物品再换回去。
	if (Result.bCommitted)
	{
		BroadcastInventoryChange(SourceSlotIndex);
		if (TargetInventory != this) TargetInventory->BroadcastInventoryChange(TargetSlotIndex);
	}
	UE_LOG(LogCatInventory, Log,
		TEXT("Event=inventory_move_item World=%s NetMode=%d Authority=%s Owner=%s Request=%s SourceSlot=%d TargetOwner=%s TargetSlot=%d Committed=%s Error=%s"),
		*GetPathNameSafe(GetWorld()), static_cast<int32>(GetNetMode()),
		SourceOwner && SourceOwner->HasAuthority() ? TEXT("true") : TEXT("false"),
		*GetNameSafe(SourceOwner),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		SourceSlotIndex,
		*GetNameSafe(TargetOwner),
		TargetSlotIndex,
		Result.bCommitted ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(Result.Error));
	return Result;
}

// 内部交换流程：
// 1. 先做详细校验并返回结构化错误；源格为空是 NotFound，目标同堆叠已满是 AlreadyResolved。
// 2. 再按正式库存定义的堆叠规则决定合并、搬空格或交换，期间维护实例 owner、观察数量和复制登记。
// 3. 函数只标记 FastArray 变脏，不广播；外层命令统一通知，避免 UI 在半次交换中重读。
UCatInventoryComponent::FInventoryExchangeMutation UCatInventoryComponent::ExecuteExchangeRequestOnAuthorityInternal(
	UCatInventoryComponent* DraggedInventory, const int32 DraggedSlotIndex,
	UCatInventoryComponent* DropInventory, const int32 DropSlotIndex)
{
	FInventoryExchangeMutation Result;
	// 交换直接写两端槽位，因此任一端预留中都必须在取可写引用之前拒绝。
	if ((DraggedInventory && DraggedInventory->HasPreparedRemoval()) || (DropInventory && DropInventory->HasPreparedRemoval()))
	{ Result.Error = ECatDomainCommandError::InvalidPhase; return Result; }

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
	if (DraggedEntry.Instance == nullptr || DraggedEntry.StackCount <= 0)
	{
		Result.Error = ECatDomainCommandError::NotFound;
		return Result;
	}

	FCatInventoryEntry& DropEntry = DropInventory->InventoryList.Entries[DropSlotIndex];
	if (DraggedOwner->GetWorld() != DropOwner->GetWorld())
	{
		Result.Error = ECatDomainCommandError::PermissionDenied;
		UE_LOG(LogCatInventory, Warning, TEXT("Event=inventory_exchange_rejected Reason=WorldMismatch World=%s NetMode=%d Authority=1 LocalRole=%d Source=%s Target=%s"),
			*GetNameSafe(DraggedOwner->GetWorld()), DraggedOwner->GetNetMode(), DraggedOwner->GetLocalRole(), *GetNameSafe(DraggedOwner), *GetNameSafe(DropOwner));
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

	// 随身携带总量只在跨库存移动时问：同一份库存内部整理不改变这只猫身上带了多少
	//（与 09-11 裁决③同源口径——挡的是涉及别处容器的转移，不挡整理自己的背包）。
	const bool bCrossInventoryExchange = DraggedInventory != DropInventory;
	const auto ExceedsCarryAllowance = [](const UCatInventoryComponent* Target,
		const UCatInventoryItemDefinition* Definition, const int32 IncomingCount)
	{
		return Target != nullptr && Definition != nullptr && IncomingCount > 0
			&& Target->GetRemainingCarryAllowanceForDefinition(*Definition) < IncomingCount;
	};

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
		if (bCrossInventoryExchange && ExceedsCarryAllowance(DropInventory, DraggedDefinition, MovedCount))
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
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

	if (bCrossInventoryExchange
		&& (ExceedsCarryAllowance(DropInventory, DraggedDefinition, DraggedEntry.StackCount)
			|| (bDropOccupied && ExceedsCarryAllowance(DraggedInventory, DropDefinition, DropEntry.StackCount))))
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}

	if (!bDropOccupied)
	{
		DropEntry = DraggedEntry;
		DraggedEntry = FCatInventoryEntry(DraggedInventory);
	}
	else
	{
		// UE 的通用 Swap 可整块交换结构体，连 FastArray 身份一起搬走；逐次赋值仅交换内容，保留两端槽位身份。
		const FCatInventoryEntry PreviousDraggedEntry = DraggedEntry;
		DraggedEntry = DropEntry;
		DropEntry = PreviousDraggedEntry;
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
// 1. 先复用内部交换规则拿到结构化变更结果；没有真实变更时直接返回 false，不广播库存变化。
// 2. 有变更时源库存一定广播；跨库存交换时目标库存也广播自己的变化。
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

	DraggedInventory->BroadcastInventoryChange(DraggedSlotIndex);
	if (DropInventory != DraggedInventory)
	{
		DropInventory->BroadcastInventoryChange(DropSlotIndex);
	}
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

// 本地变化广播流程：先把所属库存写入它自己的 Model 并通知 UI，再广播给玩法观察者；未打开过 UI 时无需创建 Model。
void UCatInventoryComponent::BroadcastInventoryChange(const int32 ChangedIndex)
{
	(void)ChangedIndex;
	if (InventoryModel)
	{
		InventoryModel->SetInventoryList(InventoryList.Entries);
	}
	OnInventoryObservedChanged.Broadcast();
}

// 槽位接收默认规则：通用库存只校验目标下标有效；装备栏或专用容器可以在子类按标签继续收窄。
bool UCatInventoryComponent::CanAcceptInventoryEntryAtSlot(const FCatInventoryEntry& IncomingEntry,
	const int32 TargetSlotIndex) const
{
	(void)IncomingEntry;
	return IsValidInventorySlotIndex(TargetSlotIndex);
}

// 定义接收默认规则：容量预演没有运行实例，只能问定义是否能进目标格；通用库存仍只裁决数组边界。
bool UCatInventoryComponent::CanAcceptInventoryDefinitionAtSlot(const UCatInventoryItemDefinition& IncomingDefinition,
	const int32 TargetSlotIndex) const
{
	(void)IncomingDefinition;
	return IsValidInventorySlotIndex(TargetSlotIndex);
}

// 从现有格建立临时分配，按输入顺序优先堆叠再占空格；每项读分配后的总量，因此批次内部共享携带额度。
// 实例接收规则保留手持原格限制；仅分配载体转移意图，不提前修改来源实例。
// 定义或数量非法即拒绝；先处理全部定义项，再处理实例项。仅单项调用提供余数指针时允许留下未接收数量，整批必须全部放下。
bool UCatInventoryComponent::AllocateInventoryIntake(const FCatInventoryReceiveBatch& Batch,
	TArray<FInventoryIntakeSlot>& Slots, int32* OutRemaining) const
{
	Slots.Reset(InventoryList.Entries.Num());
	for (const FCatInventoryEntry& Entry : InventoryList.Entries)
	{
		FInventoryIntakeSlot& Slot = Slots.AddDefaulted_GetRef();
		Slot.Instance = Entry.Instance;
		Slot.ItemDefinition = Entry.Instance ? Entry.Instance->GetItemDefinition() : nullptr;
		Slot.StackCount = Entry.StackCount;
	}
	const auto Allocate = [&](UCatInventoryItemDefinition* Definition, UCatInventoryItemInstance* Instance,
		TSubclassOf<UCatInventoryItemInstance> InstanceClass, const int32 Count)
	{
		if (!Definition || Count <= 0 || !Definition->IsInventoryRuntimeDefinitionReady()
			|| !UCatInventoryItemDefinition::ResolveItemInstanceClass(Definition, InstanceClass)) return false;
		int32 Remaining = Count;
		int32 Allowance = Count;
		const ECatInventoryCarryCategory Category = UCatInventorySettings::ResolveCarryCategory(*Definition);
		if (EnforcesCarryLimits() && Category != ECatInventoryCarryCategory::None)
		{
			int64 Total = 0;
			for (const FInventoryIntakeSlot& Slot : Slots)
				if (Slot.ItemDefinition && UCatInventorySettings::ResolveCarryCategory(*Slot.ItemDefinition) == Category)
					Total += Slot.StackCount;
			Allowance = static_cast<int32>(FMath::Clamp<int64>(int64(GetEffectiveCarryLimit(Category)) - Total, 0, Count));
		}
		FCatInventoryEntry Incoming;
		Incoming.Instance = Instance;
		Incoming.StackCount = Count;
		bool bOriginalPlaced = false;
		bool bActorAssigned = Slots.ContainsByPredicate([&](const FInventoryIntakeSlot& Slot) { return Instance && Slot.WorldActorSource == Instance; });
		const int32 MaxStack = GetMaxStackCountForDefinition(*Definition);
		for (int32 Pass = 0; Pass < 2 && Allowance > 0; ++Pass)
		{
			for (int32 Index = 0; Index < Slots.Num() && Allowance > 0; ++Index)
			{
				FInventoryIntakeSlot& Slot = Slots[Index];
				const bool bEmpty = Slot.ItemDefinition == nullptr;
				if (Pass == 0 ? (bEmpty || MaxStack <= 1 || !Slot.ItemDefinition->CanStackWith(*Definition)) : !bEmpty) continue;
				if (Instance ? !CanAcceptInventoryEntryAtSlot(Incoming, Index) : !CanAcceptInventoryDefinitionAtSlot(*Definition, Index)) continue;
				const int32 Added = FMath::Min(Allowance, FMath::Max(0, MaxStack - Slot.StackCount));
				if (Added <= 0) continue;
				if (bEmpty)
				{
					Slot.ItemDefinition = Definition;
					Slot.InstanceClass = InstanceClass;
					Slot.Instance = !bOriginalPlaced ? Instance : nullptr;
					bOriginalPlaced = Instance != nullptr;
				}
				else if (Instance && Instance->GetWorldActor() && !bActorAssigned && !Slot.WorldActorSource
					&& (!Slot.Instance || !Slot.Instance->GetWorldActor()))
				{
					Slot.WorldActorSource = Instance;
					bActorAssigned = true;
				}
				Slot.StackCount += Added;
				Slot.AddedCount += Added;
				Remaining -= Added;
				Allowance -= Added;
			}
		}
		if (OutRemaining) *OutRemaining = Remaining;
		return OutRemaining || Remaining == 0;
	};
	for (const FCatInventoryDefinitionEntry& Entry : Batch.DefinitionEntries)
		if (!Allocate(Entry.ItemDefinition, nullptr, Entry.ItemInstanceClass, Entry.Count)) return false;
	for (const FCatInventoryInstanceEntry& Entry : Batch.InstanceEntries)
		if (!Entry.ItemInstance || !Allocate(Entry.ItemInstance->GetItemDefinition(), Entry.ItemInstance, Entry.ItemInstance->GetClass(), Entry.Count)) return false;
	return true;
}

// 先保存条目用于核对实例初始化期间的重入，再为新增格创建所需实例；任一创建失败即拒绝本次提交。
// 创建后检查预留状态、格数、实例指针和数量；任一不符就保留当前库存，不用旧快照覆盖重入调用的结果。
// 核对通过后迁移分配中的载体，再写新增格的实例、数量、宿主和复制登记并标脏；全程不广播，由外层在提交后通知。
bool UCatInventoryComponent::ApplyInventoryIntake(TArray<FInventoryIntakeSlot>& Slots)
{
	const TArray<FCatInventoryEntry> Before = InventoryList.Entries;
	for (FInventoryIntakeSlot& Slot : Slots)
	{
		if (Slot.AddedCount > 0 && !Slot.Instance)
		{
			Slot.Instance = CreateInventoryItemInstance(Slot.ItemDefinition, Slot.InstanceClass);
			if (!Slot.Instance) return false;
		}
	}
	if (HasPreparedRemoval() || Before.Num() != InventoryList.Entries.Num()) return false;
	for (int32 Index = 0; Index < Before.Num(); ++Index)
		if (Before[Index].Instance != InventoryList.Entries[Index].Instance
			|| Before[Index].StackCount != InventoryList.Entries[Index].StackCount) return false;
	for (FInventoryIntakeSlot& Slot : Slots)
	{
		if (Slot.WorldActorSource)
		{
			Slot.Instance->SetWorldActor(Slot.WorldActorSource->GetWorldActor());
			Slot.WorldActorSource->SetWorldActor(nullptr);
		}
	}
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		const FInventoryIntakeSlot& Slot = Slots[Index];
		if (Slot.AddedCount <= 0) continue;
		FCatInventoryEntry& Entry = InventoryList.Entries[Index];
		Entry.Instance = Slot.Instance;
		Entry.StackCount = Entry.LastObservedCount = Slot.StackCount;
		Entry.SlotOwnerComponent = this;
		SyncInventoryItemRuntimeOwner(Entry.Instance);
		if (Before[Index].Instance != Entry.Instance && IsUsingRegisteredSubObjectList() && IsReadyForReplication())
			AddReplicatedSubObject(Entry.Instance);
		InventoryList.MarkItemDirty(Entry);
	}
	return true;
}

// 堆叠上限读取流程：委托定义集中收束非法配置，保证预演和正式写入使用同一规则。
int32 UCatInventoryComponent::GetMaxStackCountForDefinition(
	const UCatInventoryItemDefinition& ItemDefinition) const
{
	return ItemDefinition.GetMaxStackCount();
}

// 同类总量统计流程：逐格判分类再累加数量；held entry 不计——借出去的竿漂不是数量型消耗品，不属于任何携带分类。
int32 UCatInventoryComponent::CountVisibleQuantityForCarryCategory(const ECatInventoryCarryCategory Category) const
{
	if (Category == ECatInventoryCarryCategory::None)
	{
		return 0;
	}
	int32 Total = 0;
	for (const FCatInventoryEntry& Entry : InventoryList.Entries)
	{
		const UCatInventoryItemDefinition* Definition =
			Entry.Instance != nullptr ? Entry.Instance->GetItemDefinition() : nullptr;
		if (Definition == nullptr || Entry.StackCount <= 0
			|| UCatInventorySettings::ResolveCarryCategory(*Definition) != Category)
		{
			continue;
		}
		Total += Entry.StackCount;
	}
	return Total;
}

// 携带余量读取流程：
// 1. 不受约束的库存（营地公库、鱼护、商店货架、鱼缸）一律返回 MAX_int32，「携带上限」只管猫身上那一份。
// 2. 未配置上限的分类同样返回 MAX_int32 —— 漏配的后果是「这条规则还没生效」，不是把饵挡在背包外。
// 3. 有效上限减去已带份数即余量；负值夹到 0。
int32 UCatInventoryComponent::GetRemainingCarryAllowanceForDefinition(
	const UCatInventoryItemDefinition& ItemDefinition) const
{
	if (!EnforcesCarryLimits())
	{
		return MAX_int32;
	}
	const ECatInventoryCarryCategory Category = UCatInventorySettings::ResolveCarryCategory(ItemDefinition);
	const int32 Limit = GetEffectiveCarryLimit(Category);
	if (Category == ECatInventoryCarryCategory::None || Limit == MAX_int32)
	{
		return MAX_int32;
	}
	return FMath::Max(0, Limit - CountVisibleQuantityForCarryCategory(Category));
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

bool UCatInventoryComponent::MoveHeldInventoryEntriesToCustodianFromAuthority(
	UCatInventoryComponent* Target, const TArray<FGuid>& ItemInstanceIds)
{
	if (!IsValid(Target) || Target == this || !GetOwner() || !GetOwner()->HasAuthority()
		|| !Target->GetOwner() || !Target->GetOwner()->HasAuthority() || GetWorld() != Target->GetWorld()) return false;
	TSet<FGuid> Seen;
	for (const FGuid ItemId : ItemInstanceIds)
	{
		const FCatInventoryEntry* Entry = FindHeldInventoryEntryFromAuthority(ItemId);
		if (Seen.Contains(ItemId) || !Entry || !Entry->Instance || Entry->StackCount != 1
			|| Target->ActiveHeldItemEntries.Contains(ItemId)
			|| Target->FindInventorySlotIndexFromInstanceId(ItemId) != INDEX_NONE) return false;
		Seen.Add(ItemId);
	}
	for (const FGuid ItemId : ItemInstanceIds)
	{
		FCatInventoryEntry Record = MoveTemp(ActiveHeldItemEntries.FindChecked(ItemId));
		ActiveHeldItemEntries.Remove(ItemId);
		Record.SlotOwnerComponent = Target;
		UpdateReplicatedItemRegistration(Record.Instance, this, Target);
		Target->SyncInventoryItemRuntimeOwner(Record.Instance);
		Target->ActiveHeldItemEntries.Add(ItemId, MoveTemp(Record));
	}
	if (!ItemInstanceIds.IsEmpty())
	{
		UE_LOG(LogCatInventory, Log, TEXT("Event=inventory_held_resources_transferred Source=%s Target=%s Count=%d World=%s NetMode=%d Authority=true LocalRole=%d"),
			*GetNameSafe(GetOwner()), *GetNameSafe(Target->GetOwner()), ItemInstanceIds.Num(), *GetNameSafe(GetWorld()), static_cast<int32>(GetWorld()->GetNetMode()), static_cast<int32>(GetOwner()->GetLocalRole()));
	}
	return true;
}

// 携带上限查询：先读类别基础限制；不执行限制、无类别或基础无限时直接返回无限，否则叠加拥有者成长容量并限制在整数上限内。
int32 UCatInventoryComponent::GetEffectiveCarryLimit(const ECatInventoryCarryCategory Category) const
{
	const int32 Base = GetDefault<UCatInventorySettings>()->GetCarryLimitForCategory(Category);
	if (!EnforcesCarryLimits() || Category == ECatInventoryCarryCategory::None || Base == MAX_int32) return MAX_int32;
	const auto* Growth = GetOwner() ? GetOwner()->FindComponentByClass<UCatGrowthComponent>() : nullptr;
	const double Bonus = Growth ? Growth->GetTotalMagnitude(ECatGrowthOptionId::SupplyCapacity) : 0.0;
	return static_cast<int32>(FMath::Min(double(MAX_int32), double(Base) + Bonus));
}
