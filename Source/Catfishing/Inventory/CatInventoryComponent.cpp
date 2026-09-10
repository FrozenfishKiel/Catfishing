#include "Inventory/CatInventoryComponent.h"

#include "GameFramework/Pawn.h"
#include "Character/CatCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Condition/CatConditionComponent.h"
#include "Engine/World.h"
#include "Inventory/CatInventoryWorldItem.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/CatInventorySettings.h"
#include "Logging/CatLog.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsEngine/BodySetup.h"
#include "UI/Inventory/CatInventoryModel.h"

DEFINE_LOG_CATEGORY_STATIC(LogCatInventory, Log, All);

namespace
{
	// 落点求解流程：只用物理根的局部包围盒检查占用，不把准星交互球算作实体；放置依次搜索正前方与左右各30度内的地面。
	// 放置同时检查坡度、相对脚底高差、视线、物体占用和四角支撑，全部通过才返回最终 Actor 变换；全过程不移动 Actor。
	bool FindInventoryWorldTransform(ACatCharacter* Character, AActor* ItemActor, const ECatInventoryWorldAction Action,
		const UCatInventorySettings& Settings, FTransform& OutTransform)
	{
		UWorld* World = Character->GetWorld();
		const UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(ItemActor->GetRootComponent());
		if (!Body) return false;
		const FBox Bounds = Body->CalcBounds(FTransform::Identity).GetBox();
		if (!Bounds.IsValid) return false;
		const FVector Scale = ItemActor->GetActorScale3D();
		const FVector Extent = Bounds.GetExtent() * Scale.GetAbs();
		const FVector CenterOffset = Bounds.GetCenter() * Scale;
		const FVector Forward = Character->GetActorForwardVector().GetSafeNormal2D();
		const FVector Eye = Character->GetPawnViewLocation();
		if (!World || Extent.ContainsNaN() || Extent.GetMin() <= 0.0 || Forward.IsNearlyZero()) return false;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(CatInventoryWorldRelease), false, Character);
		Query.AddIgnoredActor(ItemActor);
		const FCollisionShape Shape = FCollisionShape::MakeBox(Extent);
		const double FeetZ = Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		if (Action == ECatInventoryWorldAction::Drop)
		{
			const FQuat Rotation = Forward.Rotation().Quaternion();
			const FVector Center = Eye + Forward * (Character->GetCapsuleComponent()->GetScaledCapsuleRadius() + Extent.GetMax() + 10.0);
			FHitResult Hit;
			if (World->SweepSingleByChannel(Hit, Eye, Center, Rotation, ECC_WorldDynamic, Shape, Query)
				|| World->OverlapBlockingTestByChannel(Center, Rotation, ECC_WorldDynamic, Shape, Query)) return false;
			OutTransform = FTransform(Rotation, Center - Rotation.RotateVector(CenterOffset), Scale);
			return true;
		}
		const double Height = Settings.PlacementHeightDifferenceCentimeters;
		const double MinimumNormalZ = FMath::Cos(FMath::DegreesToRadians(Settings.PlacementSlopeDegrees));
		for (const double Angle : {0.0, -15.0, 15.0, -30.0, 30.0})
		{
			const FVector Direction = Forward.RotateAngleAxis(Angle, FVector::UpVector);
			for (const double Fraction : {2.0 / 3.0, 0.5, 5.0 / 6.0, 1.0, 1.0 / 3.0})
			{
				FVector Candidate = Eye + Direction * Settings.PlacementRangeCentimeters * Fraction;
				Candidate.Z = FeetZ;
				FHitResult Ground;
				if (!World->LineTraceSingleByChannel(Ground, Candidate + FVector(0, 0, Height + 2.0),
					Candidate - FVector(0, 0, Height + 2.0), ECC_WorldDynamic, Query)
					|| FMath::Abs(Ground.ImpactPoint.Z - FeetZ) > Height || Ground.ImpactNormal.Z < MinimumNormalZ) continue;
				FHitResult Sight;
				if (World->LineTraceSingleByChannel(Sight, Eye, Ground.ImpactPoint, ECC_Visibility, Query)
					&& FVector::DistSquared(Sight.ImpactPoint, Ground.ImpactPoint) > FMath::Square(3.0)) continue;
				const FQuat Rotation = FRotationMatrix::MakeFromZX(Ground.ImpactNormal, Forward).ToQuat();
				const FVector Center = Ground.ImpactPoint + Ground.ImpactNormal * (Extent.Z + 1.0);
				if (World->OverlapBlockingTestByChannel(Center, Rotation, ECC_WorldDynamic, Shape, Query)) continue;
				bool bSupported = true;
				for (const FVector2D Corner : {FVector2D(-1, -1), FVector2D(-1, 1), FVector2D(1, -1), FVector2D(1, 1)})
				{
					const FVector Support = Ground.ImpactPoint + Rotation.RotateVector(FVector(Corner.X * Extent.X * 0.9, Corner.Y * Extent.Y * 0.9, 0));
					FHitResult Foot;
					if (!World->LineTraceSingleByChannel(Foot, Support + FVector(0, 0, Height + 2.0),
						Support - FVector(0, 0, Height + 2.0), ECC_WorldDynamic, Query)
						|| Foot.ImpactNormal.Z < MinimumNormalZ
						|| FMath::Abs(FVector::DotProduct(Foot.ImpactPoint - Ground.ImpactPoint, Ground.ImpactNormal)) > 2.0)
					{
						bSupported = false;
						break;
					}
				}
				if (bSupported)
				{
					OutTransform = FTransform(Rotation, Center - Rotation.RotateVector(CenterOffset), Scale);
					return true;
				}
			}
		}
		return false;
	}

	// 商店批量发货需要稳定载荷签名；这里拒绝混入实例项，避免批量购买把运行实例来源混进商店语义。
	// 1. 只接受定义发货项，实例发货仍走底层 ReceiveBatch。
	// 2. 按稳定定义 ID 合并重复行，并确认每行定义、数量、运行配置和实例类都能被正式库存创建。
	// 3. 生成只描述业务意图的签名；成功重放只校验原始载荷，不因当前格子后来变化而丢失首次回执。
	bool BuildDefinitionGrantBatchPayloadSignature(const FCatInventoryReceiveBatch& ReceiveBatch,
		const FString& IdempotencyPayloadContext, FString& OutPayloadSignature,
		FCatInventoryReceiveBatch& OutNormalizedBatch)
	{
		OutPayloadSignature.Reset();
		OutNormalizedBatch = FCatInventoryReceiveBatch();
		if (ReceiveBatch.DefinitionEntries.IsEmpty() || !ReceiveBatch.InstanceEntries.IsEmpty())
		{
			return false;
		}

		struct FNormalizedDefinitionGrantEntry
		{
			// 合并后的静态定义资产；同一个 DefinitionId 必须对应同一对象，否则重放签名无法代表唯一物品目录。
			UCatInventoryItemDefinition* Definition = nullptr;
			// 本批次要生成的运行实例类型；容量预演、正式物化和重放签名都读取它，保证同一订单不会换类。
			TSubclassOf<UCatInventoryItemInstance> InstanceClass;
			// 同一稳定定义合并后的发货数量；容量预演和正式写入按它计算，溢出时整批拒绝。
			int32 Count = 0;
		};

		TMap<FName, FNormalizedDefinitionGrantEntry> EntriesByDefinitionId;
		for (const FCatInventoryDefinitionEntry& Entry : ReceiveBatch.DefinitionEntries)
		{
			UCatInventoryItemDefinition* Definition = Entry.ItemDefinition;
			const FName DefinitionId = Definition != nullptr ? Definition->GetInventoryDefinitionId() : NAME_None;
			const TSubclassOf<UCatInventoryItemInstance> ResolvedClass =
				UCatInventoryItemDefinition::ResolveItemInstanceClass(Definition, Entry.ItemInstanceClass);
			if (Definition == nullptr || DefinitionId.IsNone() || Entry.Count <= 0
				|| !Definition->IsInventoryRuntimeDefinitionReady() || ResolvedClass == nullptr)
			{
				OutNormalizedBatch = FCatInventoryReceiveBatch();
				return false;
			}

			FNormalizedDefinitionGrantEntry& Normalized = EntriesByDefinitionId.FindOrAdd(DefinitionId);
			if (Normalized.Definition != nullptr
				&& (Normalized.Definition != Definition || Normalized.InstanceClass.Get() != ResolvedClass.Get()))
			{
				OutNormalizedBatch = FCatInventoryReceiveBatch();
				return false;
			}
			if (Entry.Count > MAX_int32 - Normalized.Count)
			{
				OutNormalizedBatch = FCatInventoryReceiveBatch();
				return false;
			}
			Normalized.Definition = Definition;
			Normalized.InstanceClass = ResolvedClass;
			Normalized.Count += Entry.Count;
		}

		TArray<FName> DefinitionIds;
		EntriesByDefinitionId.GetKeys(DefinitionIds);
		DefinitionIds.Sort([](const FName& Left, const FName& Right)
		{
			return Left.ToString() < Right.ToString();
		});
		if (DefinitionIds.IsEmpty())
		{
			return false;
		}

		TArray<FString> Parts;
		Parts.Reserve(DefinitionIds.Num());
		OutNormalizedBatch.DefinitionEntries.Reserve(DefinitionIds.Num());
		for (const FName& DefinitionId : DefinitionIds)
		{
			const FNormalizedDefinitionGrantEntry& Normalized = EntriesByDefinitionId.FindChecked(DefinitionId);
			FCatInventoryDefinitionEntry& OutEntry = OutNormalizedBatch.DefinitionEntries.AddDefaulted_GetRef();
			OutEntry.ItemDefinition = Normalized.Definition;
			OutEntry.ItemInstanceClass = Normalized.InstanceClass;
			OutEntry.Count = Normalized.Count;
			Parts.Add(FString::Printf(TEXT("%s:%d:%s"),
				*DefinitionId.ToString(), Normalized.Count, *GetPathNameSafe(Normalized.InstanceClass.Get())));
		}
		OutPayloadSignature = FString::Printf(TEXT("Context=%s|Entries=%s"),
			*IdempotencyPayloadContext, *FString::Join(Parts, TEXT(",")));
		return true;
	}
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

// 槽位刷新流程：只在 authority 或单机构造路径补齐空槽；新增格子会标脏并广播，客户端只通过复制拿到服务器数组。
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

// 按定义入库流程：
// 1. 先拒绝无定义、无数量、非 authority 和运行定义未就绪的请求；失败不会改 Entries 或广播变化。
// 2. 再填充同类可堆叠格，并同步观察数量、槽位 owner 和实例运行宿主。
// 3. 剩余数量按空格创建新实例，登记复制子对象并把 InOutCount 扣到实际剩余数量。
// 4. 只要接收过至少一份物品就写 bOutFullyAdded；bBroadcastChange 为 true 时把本次调用作为完整事务广播。
UCatInventoryItemInstance* UCatInventoryComponent::AddEntry(
	UCatInventoryItemDefinition* ItemDefinition,
	int32& InOutCount,
	bool& bOutFullyAdded,
	const TSubclassOf<UCatInventoryItemInstance> ItemInstanceClass,
	const bool bBroadcastChange)
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
		for (int32 SlotIndex = 0; SlotIndex < InventoryList.Entries.Num(); ++SlotIndex)
		{
			if (InOutCount <= 0)
			{
				break;
			}
			FCatInventoryEntry& Entry = InventoryList.Entries[SlotIndex];
			const UCatInventoryItemDefinition* ExistingDefinition =
				Entry.Instance != nullptr ? Entry.Instance->GetItemDefinition() : nullptr;
			if (ExistingDefinition == nullptr || !ExistingDefinition->CanStackWith(*ItemDefinition))
			{
				continue;
			}
			if (!CanAcceptInventoryDefinitionAtSlot(*ItemDefinition, SlotIndex))
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
		UCatInventoryItemInstance* NewInstance = CreateInventoryItemInstance(ItemDefinition, ItemInstanceClass);
		if (NewInstance == nullptr)
		{
			break;
		}

		const int32 TargetIndex = FindAvailableSlot(NewInstance, InOutCount);
		if (TargetIndex == INDEX_NONE)
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
		if (bBroadcastChange)
		{

			BroadcastInventoryChange();
		}
	}

	return FirstAcceptedInstance;
}

// 按实例入库流程：
// 1. 先拒绝空实例、无数量、非 authority 和缺定义的请求；失败不会占用或替换任何格子。
// 2. 堆叠物优先合并到同定义格，并把观察数量、槽位 owner 和运行宿主同步到正式库存事实。
// 3. 剩余数量先放入传入实例，再按同定义补建实例；每个新占用格都会登记复制子对象并扣减 InOutCount。
// 4. 接收过物品后更新 bOutFullyAdded；bBroadcastChange 为 true 时本次调用自成事务广播，否则等待外层批次统一通知。
void UCatInventoryComponent::AddEntry(UCatInventoryItemInstance* ItemInstance, int32& InOutCount,
	bool& bOutFullyAdded, const bool bBroadcastChange)
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
		for (int32 SlotIndex = 0; SlotIndex < InventoryList.Entries.Num(); ++SlotIndex)
		{
			if (InOutCount <= 0)
			{
				break;
			}
			FCatInventoryEntry& Entry = InventoryList.Entries[SlotIndex];
			const UCatInventoryItemDefinition* ExistingDefinition =
				Entry.Instance != nullptr ? Entry.Instance->GetItemDefinition() : nullptr;
			if (ExistingDefinition == nullptr || !ExistingDefinition->CanStackWith(*TargetDefinition))
			{
				continue;
			}
			FCatInventoryEntry IncomingEntry(this);
			IncomingEntry.Instance = ItemInstance;
			IncomingEntry.StackCount = 1;
			if (!CanAcceptInventoryEntryAtSlot(IncomingEntry, SlotIndex))
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

		const int32 TargetIndex = FindAvailableSlot(TargetInstance, InOutCount);
		if (TargetIndex == INDEX_NONE)
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
		if (bBroadcastChange)
		{

			BroadcastInventoryChange();
		}
	}
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
bool UCatInventoryComponent::RestoreInventorySlotsFromAuthority(const TArray<FCatInventoryEntry>& RestoredSlots,
	const int32 MinimumSlotCount, FText& OutFailure)
{
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

	TArray<FSimulatedInventorySlot> SimulatedSlots;
	return SimulateAddInventoryBatch(ReceiveBatch, SimulatedSlots);
}

// 批次写入流程：
// 1. 先要求 authority 和非空批次，再用容量预演保证正常路径不会半批失败。
// 2. 逐项正式入库阶段暂不广播，只记录是否已经发生任何 Entries 变更。
// 3. 预检后仍失败时恢复原有条目及传入实例的运行宿主；失败批次不留下部分物品，调用方可以保留世界物或重试。
// 4. 全批成功且确有变更时只广播一次，避免一批收货拆成多次 UI 刷新。
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

	const TArray<FCatInventoryEntry> SavedEntries = InventoryList.Entries;
	TMap<UCatInventoryItemInstance*, AActor*> PreviousRuntimeOwners;
	for (const FCatInventoryInstanceEntry& Entry : ReceiveBatch.InstanceEntries)
	{
		if (Entry.ItemInstance != nullptr)
		{
			PreviousRuntimeOwners.FindOrAdd(Entry.ItemInstance, Entry.ItemInstance->GetRuntimeOwnerActor());
		}
	}
	// 先恢复传入实例宿主，再替换条目并广播，保证回调不会观察到已退回物品仍归本库存的中间状态。
	const auto RollbackBatch = [this, &SavedEntries, &PreviousRuntimeOwners]()
	{
		for (const TPair<UCatInventoryItemInstance*, AActor*>& Pair : PreviousRuntimeOwners)
		{
			Pair.Key->SetRuntimeOwnerActor(Pair.Value);
		}
		ReplaceInventoryEntriesFromAuthority(SavedEntries, SavedEntries.Num());
	};
	bool bAnyMutation = false;
	for (const FCatInventoryDefinitionEntry& DefinitionEntry : ReceiveBatch.DefinitionEntries)
	{
		int32 RemainingCount = DefinitionEntry.Count;
		bool bAdded = false;
		AddEntry(DefinitionEntry.ItemDefinition, RemainingCount, bAdded, DefinitionEntry.ItemInstanceClass, false);
		bAnyMutation = bAnyMutation || RemainingCount != DefinitionEntry.Count;
		if (!bAdded || RemainingCount != 0)
		{
			RollbackBatch();
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
			RollbackBatch();
			UE_LOG(LogCatInventory, Error, TEXT("Event=inventory_batch_apply_failed Owner=%s Source=Instance Count=%d Remaining=%d"),
				*GetNameSafe(OwningActor), InstanceEntry.Count, RemainingCount);
			return false;
		}
	}

	if (bAnyMutation)
	{

		BroadcastInventoryChange();
	}
	UE_LOG(LogCatInventory, Log, TEXT("Event=inventory_batch_accepted Owner=%s Definitions=%d Instances=%d Slots=%d"),
		*GetNameSafe(OwningActor), ReceiveBatch.DefinitionEntries.Num(), ReceiveBatch.InstanceEntries.Num(),
		InventoryList.Entries.Num());
	return true;
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

// 稳定 ID 发货提交入口流程：先从正式库存目录解析定义资产，再进入共用发货事务；幂等和写入不在公开入口重复实现。
FCatDomainCommandResult UCatInventoryComponent::GrantInventoryDefinitionFromAuthority(
	const FGuid RequestId, const FName DefinitionId, const int32 Count)
{
	const UCatInventorySettings* InventorySettings = GetDefault<UCatInventorySettings>();
	UCatInventoryItemDefinition* ItemDefinition =
		InventorySettings != nullptr ? InventorySettings->FindRuntimeDefinition(DefinitionId) : nullptr;
	return GrantInventoryDefinitionFromAuthorityInternal(
		RequestId, DefinitionId, ItemDefinition, Count);
}

// 已解析定义发货提交流程：调用方只把业务定义交给库存，实际幂等、容量和写入仍落在统一库存命令上。
FCatDomainCommandResult UCatInventoryComponent::GrantResolvedInventoryDefinitionFromAuthority(
	const FGuid RequestId, UCatInventoryItemDefinition* ItemDefinition, const int32 Count)
{
	const FName DefinitionId = ItemDefinition != nullptr ? ItemDefinition->GetInventoryDefinitionId() : NAME_None;
	return GrantInventoryDefinitionFromAuthorityInternal(
		RequestId, DefinitionId, ItemDefinition, Count);
}

// 商店扣款前必须先得到库存侧的只读接收结论；这里把定义批次规整为稳定签名，避免重试被 UI 行顺序影响。
// 1. 坏定义或实例项直接拒绝，保证预检只回答定义发货这一个语义。
// 2. 已有同 RequestId 成功终态时只校验签名并放行重放，避免购物车重试在扣款后被当前格子变化挡住。
// 3. 首次预检要求服务器正式库存仍可接收，再用同一个容量预演判断整批是否能完整放入。
ECatDomainCommandError UCatInventoryComponent::ValidateInventoryDefinitionBatchGrantFromAuthority(
	const FGuid RequestId, const FString& IdempotencyPayloadContext,
	const FCatInventoryReceiveBatch& ReceiveBatch) const
{
	FString PayloadSignature;
	FCatInventoryReceiveBatch NormalizedBatch;
	if (!BuildDefinitionGrantBatchPayloadSignature(ReceiveBatch, IdempotencyPayloadContext,
		PayloadSignature, NormalizedBatch))
	{
		return ECatDomainCommandError::InvalidPayload;
	}

	const FString Key = MakeTerminalKey(TEXT("GrantInventoryDefinitionBatch"), RequestId);
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		const FString* CachedPayload = TerminalPayloadByKey.Find(Key);
		if (!CachedPayload || *CachedPayload != PayloadSignature)
		{
			return ECatDomainCommandError::InvalidPayload;
		}
		return Cached->Error == ECatDomainCommandError::None
			? ECatDomainCommandError::None : Cached->Error;
	}

	const AActor* OwningActor = GetOwner();
	if (!RequestId.IsValid() || OwningActor == nullptr || !OwningActor->HasAuthority())
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	return CanFullyAcceptInventoryBatch(NormalizedBatch)
		? ECatDomainCommandError::None : ECatDomainCommandError::CapacityExceeded;
}

// 购物车交付只由正式库存容量和成功终态缓存裁决整批定义发货，营地 Actor 不保存第二份入库状态。
// 1. 先用载荷签名处理成功重放；同 RequestId 换身份上下文、定义或数量会被拒绝。
// 2. 首次提交必须在服务器正式库存可接收时执行，并复用整批容量预演确认定义批次完整可接收。
// 3. 写入前先把定义物化为实例批次，避免写入阶段再创建实例导致商店交付出现半批事实。
// 4. 最后只调用正式库存批次入口写入并通知；成功终态进入缓存，失败不封死同一订单再次补交付的机会。
FCatDomainCommandResult UCatInventoryComponent::GrantInventoryDefinitionBatchFromAuthority(
	const FGuid RequestId, const FString& IdempotencyPayloadContext,
	const FCatInventoryReceiveBatch& ReceiveBatch)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;

	FString PayloadSignature;
	FCatInventoryReceiveBatch NormalizedBatch;
	if (!BuildDefinitionGrantBatchPayloadSignature(ReceiveBatch, IdempotencyPayloadContext,
		PayloadSignature, NormalizedBatch))
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}

	const FString Key = MakeTerminalKey(TEXT("GrantInventoryDefinitionBatch"), RequestId);
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
	if (!RequestId.IsValid() || OwningActor == nullptr || !OwningActor->HasAuthority())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (!CanFullyAcceptInventoryBatch(NormalizedBatch))
	{
		Result.Error = ECatDomainCommandError::CapacityExceeded;
	}
	else
	{
		FCatInventoryReceiveBatch MaterializedBatch;
		for (const FCatInventoryDefinitionEntry& DefinitionEntry : NormalizedBatch.DefinitionEntries)
		{
			const int32 MaxStackCount = FMath::Max(1, GetMaxStackCountForDefinition(*DefinitionEntry.ItemDefinition));
			int32 RemainingCount = DefinitionEntry.Count;
			while (RemainingCount > 0)
			{
				const int32 EntryCount = MaxStackCount > 1 ? FMath::Min(RemainingCount, MaxStackCount) : 1;
				UCatInventoryItemInstance* Instance = CreateInventoryItemInstance(
					DefinitionEntry.ItemDefinition, DefinitionEntry.ItemInstanceClass);
				if (Instance == nullptr)
				{
					Result.Error = ECatDomainCommandError::DependencyUnavailable;

					UE_LOG(LogCatInventory, Error,
						TEXT("Event=inventory_definition_batch_materialize_failed Owner=%s Request=%s Definition=%s Remaining=%d"),
						*GetNameSafe(OwningActor), *RequestId.ToString(EGuidFormats::DigitsWithHyphens),
						*DefinitionEntry.ItemDefinition->GetInventoryDefinitionId().ToString(), RemainingCount);
					return Result;
				}

				FCatInventoryInstanceEntry& InstanceEntry =
					MaterializedBatch.InstanceEntries.AddDefaulted_GetRef();
				InstanceEntry.ItemInstance = Instance;
				InstanceEntry.Count = EntryCount;
				RemainingCount -= EntryCount;
			}
		}

		if (!CanFullyAcceptInventoryBatch(MaterializedBatch))
		{
			Result.Error = ECatDomainCommandError::CapacityExceeded;
		}
		else if (TryAddInventoryBatch(MaterializedBatch))
		{
			Result.bCommitted = true;
			Result.Error = ECatDomainCommandError::None;
		}
		else
		{
			Result.Error = ECatDomainCommandError::DependencyUnavailable;
		}
	}

	if (Result.bCommitted && Result.Error == ECatDomainCommandError::None)
	{
		TerminalCache.Add(Key, Result);
		TerminalPayloadByKey.Add(Key, PayloadSignature);
	}
	UE_LOG(LogCatInventory, Log,
		TEXT("Event=inventory_definition_batch_grant Owner=%s Request=%s Committed=%s Error=%s Definitions=%d"),
		*GetNameSafe(OwningActor), *RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		Result.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Error),
		NormalizedBatch.DefinitionEntries.Num());
	return Result;
}

// 稳定定义发货提交共用流程：
// 1. 先按稳定 ID、数量和来源上下文建立幂等签名，所有来源共享同一条重放规则。
// 2. 首次请求必须在 authority 且定义运行配置完整，再按当前容量和堆叠规则裁决是否可写入。
// 3. 通过后只调用库存批次收货；堆叠、实例创建和变化广播都集中在本组件。
// 4. 所有首次终态都会缓存并写入诊断日志，便于商店、奖励或 Equipment 入口跨端追查。
FCatDomainCommandResult UCatInventoryComponent::GrantInventoryDefinitionFromAuthorityInternal(
	const FGuid RequestId, const FName DefinitionId,
	UCatInventoryItemDefinition* ItemDefinition, const int32 Count)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString Key = MakeTerminalKey(TEXT("GrantInventoryDefinition"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("Definition=%s|Count=%d"),
		*DefinitionId.ToString(), Count);
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
		|| DefinitionId.IsNone() || Count <= 0 || ItemDefinition == nullptr
		|| !ItemDefinition->IsInventoryRuntimeDefinitionReady()
		|| ItemDefinition->GetInventoryDefinitionId() != DefinitionId)
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

	TerminalCache.Add(Key, Result);
	TerminalPayloadByKey.Add(Key, PayloadSignature);
	UE_LOG(LogCatInventory, Log,
		TEXT("Event=inventory_definition_grant Owner=%s Request=%s Committed=%s Error=%s Definition=%s Count=%d"),
		*GetNameSafe(OwningActor), *RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		Result.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Error),
		*DefinitionId.ToString(), Count);
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
// 2. 先比较格位、实例和数量；内容没变时只补齐本地 owner，不广播库存变化。
// 3. 内容变化时移除原实例复制登记，按目标数量调整数组并在原格上写入内容；保留已有格子的复制身份，避免恢复/回滚让客户端格序漂移。
// 4. 每格（包括空格）标记内容变化，有效实例补齐运行宿主和复制登记，最后广播完整变化。
bool UCatInventoryComponent::ReplaceInventoryEntriesFromAuthority(
	const TArray<FCatInventoryEntry>& NewEntries, const int32 MinimumSlotCount, const bool bBroadcastChange)
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

// 活动区持有流程：
// 1. 先要求服务器、合法槽位和有效实例 ID，避免客户端或空格制造活动记录。
// 2. 再确认这是不可堆叠的完整实例，数量型或可堆叠物继续走 Consume 或普通收货批次，避免归还时被合并到别的栈里。
// 3. 同一实例不能已经被本库存借出，防止一次部署命令重复移出同一件物品。
// 4. 成功后复用正式移出入口清空可见槽位，并把完整 entry 放入活动区维持同一实例身份。
// 5. 活动 entry 只服务部署事务和磨损写回；原槽位保持空闲，收回必须走统一入包。
bool UCatInventoryComponent::HoldInventoryEntryAtSlotFromAuthority(
	const int32 SlotIndex, FCatInventoryEntry& OutHeldEntry)
{
	OutHeldEntry = FCatInventoryEntry(this);
	AActor* OwningActor = GetOwner();
	if (OwningActor == nullptr || !OwningActor->HasAuthority() || !IsValidInventorySlotIndex(SlotIndex))
	{
		return false;
	}

	const FCatInventoryEntry& SourceEntry = InventoryList.Entries[SlotIndex];
	const FGuid ItemInstanceId = SourceEntry.Instance != nullptr ? SourceEntry.Instance->GetItemInstanceId() : FGuid();
	const UCatInventoryItemDefinition* SourceDefinition =
		SourceEntry.Instance != nullptr ? SourceEntry.Instance->GetItemDefinition() : nullptr;
	if (!ItemInstanceId.IsValid() || SourceEntry.StackCount != 1 || SourceDefinition == nullptr
		|| GetMaxStackCountForDefinition(*SourceDefinition) > 1 || ActiveHeldItemEntries.Contains(ItemInstanceId))
	{
		return false;
	}

	FCatInventoryEntry RemovedEntry;
	if (!RemoveInventoryEntryAtSlotFromAuthority(SlotIndex, RemovedEntry))
	{
		return false;
	}

	RemovedEntry.SlotOwnerComponent = this;
	RemovedEntry.LastObservedCount = RemovedEntry.StackCount;
	SyncInventoryItemRuntimeOwner(RemovedEntry.Instance);

	FCatInventoryEntry& HeldEntry = ActiveHeldItemEntries.Add(ItemInstanceId);
	HeldEntry = RemovedEntry;
	OutHeldEntry = HeldEntry;
	UE_LOG(LogCatInventory, Log,
		TEXT("Event=inventory_hold_item Owner=%s Instance=%s SourceSlot=%d Count=%d"),
		*GetNameSafe(OwningActor), *ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		SlotIndex, RemovedEntry.StackCount);
	return true;
}

// 按实例借出流程：
// 1. 先确认调用来自 authority，再按实例身份重读当前正式背包里的可见条目。
// 2. 再由库存自己按实例 ID 找可见槽位，调用方不用知道这个实例当前落在哪个格子。
// 3. 找到槽位后复用 held 规则检查不可堆叠、未借出和复制登记，成功才把可见格移入活动区。
// 4. 本函数不缓存终态，因为放竿等外层流程可能在 Actor 生成失败后回滚借出；RequestId 只用于把日志和外层命令串起来。
// 5. 成功或失败都返回结构化结果，让上层刷新而不是继续猜测槽位。
FCatDomainCommandResult UCatInventoryComponent::HoldInventoryItemInstanceFromAuthority(
	const FGuid RequestId, const FGuid ItemInstanceId,
	FCatInventoryEntry& OutHeldEntry)
{
	OutHeldEntry = FCatInventoryEntry(this);
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;


	AActor* OwningActor = GetOwner();
	if (OwningActor == nullptr || !OwningActor->HasAuthority() || !RequestId.IsValid()
		|| !ItemInstanceId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (ActiveHeldItemEntries.Contains(ItemInstanceId))
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
	}
	else
	{
		const int32 SlotIndex = FindInventorySlotIndexFromInstanceId(ItemInstanceId);
		if (SlotIndex == INDEX_NONE)
		{
			Result.Error = ECatDomainCommandError::NotFound;
		}
		else if (HoldInventoryEntryAtSlotFromAuthority(SlotIndex, OutHeldEntry))
		{
			Result.bCommitted = true;
			Result.Error = ECatDomainCommandError::None;
		}
		else
		{
			Result.Error = ECatDomainCommandError::InvalidPhase;
		}
	}

	UE_LOG(LogCatInventory, Log,
		TEXT("Event=inventory_hold_item_instance Owner=%s Request=%s Item=%s Committed=%s Error=%s"),
		*GetNameSafe(OwningActor), *RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		*ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), Result.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Error));
	return Result;
}

// 按实例 Use 事务流程：
// 1. 先按 RequestId 和调用方提供的载荷上下文处理终态重放；缓存命中直接返回首次终态，避免已扣量或已借出后被空格挡住。
// 2. 首次提交先执行调用方的外部前置校验，再用当前可见槽位和物品实例裁决这次 Use 是否仍然成立。
// 3. 数量消耗物由库存扣指定数量；部署型物品由库存把同一实例移入 held 活动区并记录活动 Use。
// 4. 库存 mutation 完成后再交给调用方刷新读模型或创建下一阶段读模型；如果调用方拒绝，库存在写缓存前回滚刚才的扣量或借出。
// 5. 所有缓存终态都只保存本次库存结果；调用方额外状态只能放在自己的结果载荷里，不能改写库存提交结论。
FCatInventoryItemUseResult UCatInventoryComponent::UseItemInstanceFromAuthority(
	const FGuid RequestId, const FGuid ItemInstanceId,
	const int32 Quantity, const FString& IdempotencyPayloadContext,
	TFunctionRef<ECatDomainCommandError(FCatInventoryItemUseResult&)> ValidateBeforeMutation,
	TFunctionRef<bool(FCatInventoryItemUseResult&)> FinalizeCommittedUse)
{
	FCatInventoryItemUseResult Result;
	Result.RequestId = RequestId;

	AActor* OwningActor = GetOwner();
	if (OwningActor == nullptr || !OwningActor->HasAuthority() || !RequestId.IsValid()
		|| !ItemInstanceId.IsValid() || Quantity <= 0)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}

	const FString Key = MakeTerminalKey(TEXT("UseInventoryItem"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("ItemInstance=%s|Quantity=%d|Context=%s"),
		*ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), Quantity, *IdempotencyPayloadContext);
	if (const FCatInventoryItemUseResult* Cached = InventoryItemUseTerminalCache.Find(Key))
	{
		const FString* CachedPayload = TerminalPayloadByKey.Find(Key);
		if (!CachedPayload || *CachedPayload != PayloadSignature)
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
			return Result;
		}
		Result = *Cached;
		MarkInventoryItemUseReplayed(Result);
		return Result;
	}

	const auto Finish = [this, &Key, &PayloadSignature](FCatInventoryItemUseResult Completed)
	{

		InventoryItemUseTerminalCache.Add(Key, Completed);
		TerminalPayloadByKey.Add(Key, PayloadSignature);
		return Completed;
	};

	const ECatDomainCommandError ExternalPrecheck = ValidateBeforeMutation(Result);
	if (ExternalPrecheck != ECatDomainCommandError::None)
	{
		Result.Error = ExternalPrecheck;
		return Finish(Result);
	}
	if (const FCatInventoryEntry* ExistingHeldEntry = ActiveHeldItemEntries.Find(ItemInstanceId);
		ExistingHeldEntry != nullptr)
	{
		Result.Item = *ExistingHeldEntry;
		Result.Error = ECatDomainCommandError::InvalidPhase;
		return Finish(Result);
	}

	const int32 FormalSlotIndex = FindInventorySlotIndexFromInstanceId(ItemInstanceId);
	FCatInventoryEntry* FormalEntry = InventoryList.Entries.IsValidIndex(FormalSlotIndex)
		? &InventoryList.Entries[FormalSlotIndex] : nullptr;
	UCatInventoryItemInstance* Instance = FormalEntry != nullptr ? FormalEntry->Instance.Get() : nullptr;
	if (FormalEntry == nullptr || Instance == nullptr || FormalEntry->StackCount <= 0)
	{
		Result.Error = ECatDomainCommandError::NotFound;
		return Finish(Result);
	}
	if (Instance->GetItemDefinition() == nullptr)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Finish(Result);
	}

	const FCatInventoryEntry SourceItem = *FormalEntry;
	Result.Item = SourceItem;
	const ECatDomainCommandError InstanceUseError = Instance->Use(SourceItem, Quantity);
	if (InstanceUseError != ECatDomainCommandError::None)
	{
		Result.Error = InstanceUseError;
		return Finish(Result);
	}
	if (Instance->ConsumesInventoryQuantityOnUse())
	{
		const TArray<FCatInventoryEntry> SavedEntries = GetInventoryEntries();
		if (!ConsumeItemAtSlot(FormalSlotIndex, Quantity))
		{

			Result.Error = ECatDomainCommandError::DependencyUnavailable;
			return Finish(Result);
		}

		Result.Item = SourceItem;
		Result.Item.StackCount = Quantity;

		Result.bCommitted = true;
		Result.Error = ECatDomainCommandError::None;
		if (!FinalizeCommittedUse(Result))
		{
			ReplaceInventoryEntriesFromAuthority(SavedEntries, GetInventorySlotCount());

			Result.bCommitted = false;
			Result.Error = ECatDomainCommandError::DependencyUnavailable;
			return Finish(Result);
		}
		return Finish(Result);
	}
	if (!Instance->KeepsInventoryInstanceWhileUsed())
	{
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		return Finish(Result);
	}

	const TArray<FCatInventoryEntry> SavedEntries = GetInventoryEntries();
	FCatInventoryEntry HeldEntry;
	const FCatDomainCommandResult HoldResult =
		HoldInventoryItemInstanceFromAuthority(RequestId, Instance->GetItemInstanceId(), HeldEntry);
	if (!HoldResult.bCommitted || HoldResult.Error != ECatDomainCommandError::None)
	{

		Result.Error = HoldResult.Error;
		return Finish(Result);
	}

	Result.Item = SourceItem;

	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	if (!FinalizeCommittedUse(Result))
	{
		FCatInventoryEntry ReturnedEntry;
		const FCatDomainCommandResult ReturnResult = ReturnHeldInventoryItemInstanceFromAuthority(
			RequestId, Instance->GetItemInstanceId(), GetInventorySlotCount(), ReturnedEntry);
		if (!ReturnResult.bCommitted || ReturnResult.Error != ECatDomainCommandError::None)
		{
			RetireHeldInventoryEntryFromAuthority(Instance->GetItemInstanceId());
			ReplaceInventoryEntriesFromAuthority(SavedEntries, GetInventorySlotCount());
		}

		Result.bCommitted = false;
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	return Finish(Result);
}

// Use 终态查询流程：
// 1. 只用 RequestId、实例、数量和上层载荷上下文命中正式库存缓存，不触碰当前槽位或 held entry。
// 2. 载荷漂移返回 true+InvalidPayload，提醒调用方同一个 RequestId 已经被不同意图占用。
// 3. 命中正常终态时改写成可诊断重放，外层只按首次提交结果决定是否继续补自己的领域提交。
bool UCatInventoryComponent::TryReplayItemUseTerminalFromAuthority(const FGuid RequestId,
	const FGuid ItemInstanceId, const int32 Quantity, const FString& IdempotencyPayloadContext,
	FCatInventoryItemUseResult& OutResult) const
{
	OutResult = FCatInventoryItemUseResult();
	OutResult.RequestId = RequestId;

	if (!RequestId.IsValid() || !ItemInstanceId.IsValid() || Quantity <= 0)
	{
		return false;
	}

	const FString Key = MakeTerminalKey(TEXT("UseInventoryItem"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("ItemInstance=%s|Quantity=%d|Context=%s"),
		*ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), Quantity, *IdempotencyPayloadContext);
	const FCatInventoryItemUseResult* Cached = InventoryItemUseTerminalCache.Find(Key);
	if (Cached == nullptr)
	{
		return false;
	}
	const FString* CachedPayload = TerminalPayloadByKey.Find(Key);
	if (!CachedPayload || *CachedPayload != PayloadSignature)
	{
		OutResult.Error = ECatDomainCommandError::InvalidPayload;
		return true;
	}
	OutResult = *Cached;
	MarkInventoryItemUseReplayed(OutResult);
	return true;
}

// 按实例归还流程：
// 1. 先校验 authority、RequestId 和实例身份，避免外层收杆或 Use 回滚在错误宿主、空载荷上改动活动区状态。
// 2. 再由库存自己读取 held entry；缺失返回 NotFound，空实例、可堆叠物、数量异常或可见库存重复实例返回 InvalidPhase。
// 3. 预检通过后复用低层归还流程；低层入口负责补足最低槽数、容量预演、复制登记、变化广播和活动记录删除。
// 4. 本函数不缓存终态，因为外层 Equipment::Use/UnUse 仍可能在读模型刷新失败后把刚归还的实例重新借回活动区。
// 5. 成功或失败都输出包含 RequestId 的诊断日志，方便串联外层部署/收口请求。
FCatDomainCommandResult UCatInventoryComponent::ReturnHeldInventoryItemInstanceFromAuthority(
	const FGuid RequestId, const FGuid ItemInstanceId, const int32 MinimumSlotCount,
	FCatInventoryEntry& OutReturnedEntry)
{
	OutReturnedEntry = FCatInventoryEntry(this);
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;


	AActor* OwningActor = GetOwner();
	if (OwningActor == nullptr || !OwningActor->HasAuthority() || !RequestId.IsValid()
		|| !ItemInstanceId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else
	{
		const FCatInventoryEntry* HeldEntry = ActiveHeldItemEntries.Find(ItemInstanceId);
		if (HeldEntry == nullptr)
		{
			Result.Error = ECatDomainCommandError::NotFound;
		}
		else if (HeldEntry->Instance == nullptr || HeldEntry->StackCount <= 0)
		{
			Result.Error = ECatDomainCommandError::InvalidPhase;
		}
		else
		{
			const UCatInventoryItemDefinition* HeldDefinition = HeldEntry->Instance->GetItemDefinition();
			if (HeldDefinition == nullptr || HeldEntry->StackCount != 1
				|| GetMaxStackCountForDefinition(*HeldDefinition) > 1
				|| FindInventorySlotIndexFromInstance(HeldEntry->Instance) != INDEX_NONE)
			{
				Result.Error = ECatDomainCommandError::InvalidPhase;
			}
			else if (ReturnHeldInventoryEntryFromAuthority(
				ItemInstanceId, MinimumSlotCount, OutReturnedEntry))
			{
				Result.bCommitted = true;
				Result.Error = ECatDomainCommandError::None;
			}
			else
			{
				Result.Error = ECatDomainCommandError::CapacityExceeded;
			}
		}
	}

	UE_LOG(LogCatInventory, Log,
		TEXT("Event=inventory_return_held_item_instance Owner=%s Request=%s Item=%s MinimumSlotCount=%d Committed=%s Error=%s"),
		*GetNameSafe(OwningActor), *RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		*ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), MinimumSlotCount,
		Result.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Error));
	return Result;
}

// 按实例 UnUse 事务流程：
// 1. 先按 RequestId 和载荷上下文处理终态重放，重复收口不会再次把同一 held entry 放回可见库存。
// 2. 首次提交必须找到库存唯一的 held entry；找不到说明外层玩法和库存事实已经分叉。
// 3. 归还前从 held entry 当前实例重新转换运行库存格，因此鱼竿磨损等状态以实例本体为准。
// 4. 正式归还成功后活动 entry 已被移除，再让调用方刷新读模型；调用方拒绝时把实例重新借回 held entry 或恢复保存状态。
// 5. 写入终态缓存前一定已经完成归还或回滚，避免同一请求重试读到半成功结果。
FCatInventoryItemUseResult UCatInventoryComponent::UnUseItemInstanceFromAuthority(
	const FGuid RequestId, const FGuid ItemInstanceId, const int32 MinimumSlotCount,
	const FString& IdempotencyPayloadContext,
	TFunctionRef<bool(FCatInventoryItemUseResult&)> FinalizeCommittedUnUse)
{
	FCatInventoryItemUseResult Result;
	Result.RequestId = RequestId;

	AActor* OwningActor = GetOwner();
	if (OwningActor == nullptr || !OwningActor->HasAuthority() || !RequestId.IsValid()
		|| !ItemInstanceId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}

	const FString Key = MakeTerminalKey(TEXT("UnUseInventoryItem"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("ItemInstance=%s|Context=%s"),
		*ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), *IdempotencyPayloadContext);
	if (const FCatInventoryItemUseResult* Cached = InventoryItemUseTerminalCache.Find(Key))
	{
		const FString* CachedPayload = TerminalPayloadByKey.Find(Key);
		if (!CachedPayload || *CachedPayload != PayloadSignature)
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
			return Result;
		}
		Result = *Cached;
		MarkInventoryItemUseReplayed(Result);
		return Result;
	}

	const auto Finish = [this, &Key, &PayloadSignature](FCatInventoryItemUseResult Completed)
	{

		InventoryItemUseTerminalCache.Add(Key, Completed);
		TerminalPayloadByKey.Add(Key, PayloadSignature);
		return Completed;
	};

	const FCatInventoryEntry* HeldEntry = ActiveHeldItemEntries.Find(ItemInstanceId);
	if (HeldEntry == nullptr)
	{
		Result.Error = ECatDomainCommandError::NotFound;
		return Finish(Result);
	}
	UCatInventoryItemInstance* HeldInstance = HeldEntry != nullptr ? HeldEntry->Instance.Get() : nullptr;
	if (HeldEntry == nullptr || HeldInstance == nullptr
		|| FindInventorySlotIndexFromInstance(HeldInstance) != INDEX_NONE)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	if (HeldInstance->GetItemDefinition() == nullptr)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Finish(Result);
	}

	const FCatInventoryEntry RestoredItem = *HeldEntry;
	const ECatDomainCommandError InstanceUnUseError = HeldInstance->UnUse(RestoredItem);
	if (InstanceUnUseError != ECatDomainCommandError::None)
	{
		Result.Item = RestoredItem;
		Result.Error = InstanceUnUseError;
		return Finish(Result);
	}

	const FCatInventoryEntry SavedHeldEntry = *HeldEntry;
	const TArray<FCatInventoryEntry> SavedEntries = GetInventoryEntries();
	FCatInventoryEntry ReturnedEntry;
	const FCatDomainCommandResult ReturnResult = ReturnHeldInventoryItemInstanceFromAuthority(
		RequestId, HeldInstance->GetItemInstanceId(), MinimumSlotCount, ReturnedEntry);
	if (!ReturnResult.bCommitted || ReturnResult.Error != ECatDomainCommandError::None)
	{

		Result.Item = RestoredItem;
		Result.Error = ReturnResult.Error;
		return Finish(Result);
	}

	Result.Item = RestoredItem;

	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	if (!FinalizeCommittedUnUse(Result))
	{
		const int32 RestoredSlotIndex = FindInventorySlotIndexFromInstance(HeldInstance);
		FCatInventoryEntry ReheldEntry;
		const bool bReheld = RestoredSlotIndex != INDEX_NONE
			&& HoldInventoryEntryAtSlotFromAuthority(RestoredSlotIndex, ReheldEntry);
		if (!bReheld)
		{
			ReplaceInventoryEntriesFromAuthority(SavedEntries, MinimumSlotCount);
			if (!RestoreHeldInventoryEntryForRollbackFromAuthority(SavedHeldEntry))
			{
				UE_LOG(LogCatInventory, Error,
					TEXT("Event=inventory_unuse_rollback_hold_restore_failed Owner=%s Instance=%s"),
					*GetNameSafe(OwningActor),
					*HeldInstance->GetItemInstanceId().ToString(EGuidFormats::DigitsWithHyphens));
			}
		}

		Result.bCommitted = false;
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	return Finish(Result);
}

// 活动区持有归还流程：
// 1. 先按实例 ID 找到活动区记录，并拒绝已经重新出现在可见库存里的异常状态。
// 2. 活动记录必须仍是不可堆叠的单实例；这让收回结果一定是同一 UObject 重新入包，而不是按定义生成另一件。
// 3. 归还不记原槽、不补隐藏容量，直接构造实例批次交给当前库存组件入包，防止 Actor 路由把它交给另一份库存。
// 4. 只有当前库存入包成功后才删除活动记录；失败时实例仍留在活动区，调用方可以把世界鱼竿恢复为可见并允许重试。
bool UCatInventoryComponent::ReturnHeldInventoryEntryFromAuthority(
	const FGuid ItemInstanceId, const int32 MinimumSlotCount, FCatInventoryEntry& OutReturnedEntry)
{
	OutReturnedEntry = FCatInventoryEntry(this);
	AActor* OwningActor = GetOwner();
	if (OwningActor == nullptr || !OwningActor->HasAuthority() || !ItemInstanceId.IsValid())
	{
		return false;
	}

	FCatInventoryEntry* HeldEntry = ActiveHeldItemEntries.Find(ItemInstanceId);
	if (HeldEntry == nullptr || HeldEntry->Instance == nullptr || HeldEntry->StackCount <= 0)
	{
		return false;
	}
	const UCatInventoryItemDefinition* HeldDefinition = HeldEntry->Instance->GetItemDefinition();
	if (HeldDefinition == nullptr || HeldEntry->StackCount != 1
		|| GetMaxStackCountForDefinition(*HeldDefinition) > 1)
	{
		return false;
	}
	if (FindInventorySlotIndexFromInstance(HeldEntry->Instance) != INDEX_NONE)
	{
		return false;
	}

	SetInventorySlotCountFromAuthority(MinimumSlotCount);
	FCatInventoryReceiveBatch ReceiveBatch;
	FCatInventoryInstanceEntry& InstanceEntry = ReceiveBatch.InstanceEntries.AddDefaulted_GetRef();
	InstanceEntry.ItemInstance = HeldEntry->Instance;
	InstanceEntry.Count = HeldEntry->StackCount;
	if (!TryAddInventoryBatch(ReceiveBatch))
	{
		UE_LOG(LogCatInventory, Warning,
			TEXT("Event=inventory_return_held_current_inventory_rejected Owner=%s Instance=%s MinimumSlotCount=%d Count=%d"),
			*GetNameSafe(OwningActor), *ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
			MinimumSlotCount, HeldEntry->StackCount);
		return false;
	}

	OutReturnedEntry = *HeldEntry;
	ActiveHeldItemEntries.Remove(ItemInstanceId);
	UE_LOG(LogCatInventory, Log,
		TEXT("Event=inventory_return_held_item Owner=%s Instance=%s Source=CurrentInventory Count=%d"),
		*GetNameSafe(OwningActor), *ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		OutReturnedEntry.StackCount);
	return true;
}

// 活动区持有回滚恢复流程：
// 1. 只接受服务器传回的有效不可堆叠单实例 entry，避免把数量栈或空记录塞进活动区。
// 2. 如果同一实例已经出现在可见库存，说明调用方尚未撤销归还结果，活动区不能再接管它。
// 3. 如果活动区已有同 ID 记录，必须确认它指向同一个 UObject；否则返回失败暴露身份冲突。
// 4. 成功时只重建活动记录，不恢复任何原槽位占用语义；下一次收回仍按统一入包重新裁决容量。
// 5. 这一步不广播库存变化，因为它只恢复外层失败前的活动区保管状态。
bool UCatInventoryComponent::RestoreHeldInventoryEntryForRollbackFromAuthority(const FCatInventoryEntry& HeldEntry)
{
	AActor* OwningActor = GetOwner();
	if (OwningActor == nullptr || !OwningActor->HasAuthority()
		|| HeldEntry.Instance == nullptr || HeldEntry.StackCount != 1)
	{
		return false;
	}

	const FGuid ItemInstanceId = HeldEntry.Instance->GetItemInstanceId();
	const UCatInventoryItemDefinition* HeldDefinition = HeldEntry.Instance->GetItemDefinition();
	if (!ItemInstanceId.IsValid() || HeldDefinition == nullptr || GetMaxStackCountForDefinition(*HeldDefinition) > 1
		|| FindInventorySlotIndexFromInstance(HeldEntry.Instance) != INDEX_NONE)
	{
		return false;
	}

	if (const FCatInventoryEntry* ExistingHeldEntry = ActiveHeldItemEntries.Find(ItemInstanceId);
		ExistingHeldEntry != nullptr && ExistingHeldEntry->Instance != HeldEntry.Instance)
	{
		return false;
	}

	FCatInventoryEntry& RestoredHeldEntry = ActiveHeldItemEntries.FindOrAdd(ItemInstanceId);
	RestoredHeldEntry = HeldEntry;
	RestoredHeldEntry.SlotOwnerComponent = this;
	RestoredHeldEntry.LastObservedCount = RestoredHeldEntry.StackCount;
	SyncInventoryItemRuntimeOwner(RestoredHeldEntry.Instance);
	UE_LOG(LogCatInventory, Warning,
		TEXT("Event=inventory_restore_held_item_for_rollback Owner=%s Instance=%s Count=%d"),
		*GetNameSafe(OwningActor), *ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		RestoredHeldEntry.StackCount);
	return true;
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

// 活动 entry 查找流程：只在服务器侧按稳定实例 ID 返回本库存活动区里的同一份 entry，不触发归还或复制变化。
FCatInventoryEntry* UCatInventoryComponent::FindHeldInventoryEntryFromAuthority(const FGuid ItemInstanceId)
{
	if (GetOwner() == nullptr || !GetOwner()->HasAuthority() || !ItemInstanceId.IsValid())
	{
		return nullptr;
	}

	return ActiveHeldItemEntries.Find(ItemInstanceId);
}

// 活动 entry 只读查找流程：和可写入口保持同一 authority 与实例 ID 规则，查询本身不会补槽、归还或退役物品。
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

// 扣量流程：服务器验证槽位和数量后扣减；清空格子时才解除实例复制登记，任一成功扣减都会广播变化。
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

// 物品落地流程：
// 1. 先重放同请求终态，再复核当前实例、数量、身体和配置，防止数量面板打开后误操作已换入的物品。
// 2. 有既有载体的鱼护复用原 Actor；普通物品延迟生成配置 Actor 并复制实例状态，失败销毁新载体但不扣来源。
// 3. 落点通过后才提交库存扣量；扣量的同步广播前记录重入拒绝，完成后用最终结果覆盖该请求缓存。
// 4. 最后同步实例归属、解除附着并设置物理模式，丢弃只施加一次初速度，放置不调用任何装备使用逻辑；失败清理新载体后按请求记录拒绝原因。
FCatDomainCommandResult UCatInventoryComponent::ReleaseItemToWorldFromAuthority(ACatCharacter* Character,
	const FGuid RequestId, const int32 SlotIndex, const FGuid ItemInstanceId, const int32 Quantity, const ECatInventoryWorldAction Action)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString Key = MakeTerminalKey(TEXT("WorldRelease"), RequestId);
	const FString Payload = FString::Printf(TEXT("%s|%d|%s|%d|%d"), *GetPathNameSafe(Character), SlotIndex, *ItemInstanceId.ToString(), Quantity, static_cast<int32>(Action));
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		if (TerminalPayloadByKey.FindRef(Key) == Payload) return *Cached;
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	AActor* WorldActor = nullptr;
	bool bNewActor = false;
	const auto Finish = [&](const ECatDomainCommandError Error)
	{
		Result.Error = Error;
		Result.bCommitted = Error == ECatDomainCommandError::None;
		if (!Result.bCommitted && bNewActor && IsValid(WorldActor)) WorldActor->Destroy();
		TerminalPayloadByKey.Add(Key, Payload);
		TerminalCache.Add(Key, Result);
		const FString Event = FString::Printf(TEXT("Event=inventory_world_release RequestId=%s ItemInstanceId=%s Quantity=%d Action=%d Actor=%s Error=%s World=%s NetMode=%d Authority=%d LocalRole=%d"),
			*RequestId.ToString(), *ItemInstanceId.ToString(), Quantity, static_cast<int32>(Action), *GetNameSafe(WorldActor),
			*UEnum::GetValueAsString(Error), *GetNameSafe(GetWorld()), GetOwner() ? GetOwner()->GetNetMode() : -1,
			GetOwner() && GetOwner()->HasAuthority(), GetOwner() ? static_cast<int32>(GetOwner()->GetLocalRole()) : -1);
		if (Result.bCommitted) { UE_LOG(LogCatInventory, Log, TEXT("%s"), *Event); }
		else { UE_LOG(LogCatInventory, Warning, TEXT("%s"), *Event); }
		return Result;
	};
	const FCatInventoryEntry* Entry = GetInventoryEntryAtSlot(SlotIndex);
	const UCatInventorySettings* Settings = GetDefault<UCatInventorySettings>();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Character || Character->GetWorld() != GetWorld()
		|| !RequestId.IsValid() || !ItemInstanceId.IsValid() || Quantity <= 0
		|| (Action != ECatInventoryWorldAction::Drop && Action != ECatInventoryWorldAction::Place)
		|| !Entry || !Entry->Instance || Entry->Instance->GetItemInstanceId() != ItemInstanceId || Entry->StackCount < Quantity)
		return Finish(ECatDomainCommandError::InvalidPayload);
	if (!Character->GetConditionComponent() || Character->GetConditionComponent()->GetSnapshot().bDowned)
		return Finish(ECatDomainCommandError::PermissionDenied);
	if (!Settings || !FMath::IsFinite(Settings->PlacementRangeCentimeters) || Settings->PlacementRangeCentimeters <= 0
		|| !FMath::IsFinite(Settings->PlacementHeightDifferenceCentimeters) || Settings->PlacementHeightDifferenceCentimeters < 0
		|| !FMath::IsFinite(Settings->PlacementSlopeDegrees) || Settings->PlacementSlopeDegrees < 0 || Settings->PlacementSlopeDegrees >= 90
		|| !FMath::IsFinite(Settings->DropForwardSpeed) || Settings->DropForwardSpeed < 0
		|| !FMath::IsFinite(Settings->DropUpwardSpeed) || Settings->DropUpwardSpeed < 0)
		return Finish(ECatDomainCommandError::InvalidPayload);
	UCatInventoryItemInstance* SourceItem = Entry->Instance;
	UCatInventoryItemDefinition* Definition = SourceItem->GetItemDefinition();
	if (!Definition || !Definition->IsInventoryRuntimeDefinitionReady()) return Finish(ECatDomainCommandError::InvalidPayload);
	WorldActor = SourceItem->GetWorldActor();
	UCatInventoryItemInstance* ReleasedItem = SourceItem;
	if (!WorldActor)
	{
		UClass* ActorClass = Definition->WorldActorClass.LoadSynchronous();
		if (!ActorClass || !ActorClass->ImplementsInterface(UCatInventoryWorldItem::StaticClass())) return Finish(ECatDomainCommandError::DependencyUnavailable);
		WorldActor = GetWorld()->SpawnActorDeferred<AActor>(ActorClass, Character->GetActorTransform(), nullptr, Character,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		bNewActor = true;
		if (!WorldActor) return Finish(ECatDomainCommandError::DependencyUnavailable);
		WorldActor->SetActorHiddenInGame(true);
		WorldActor->SetActorEnableCollision(false);
		ReleasedItem = DuplicateObject<UCatInventoryItemInstance>(SourceItem, WorldActor);
		if (Quantity < Entry->StackCount) ReleasedItem->SetItemInstanceIdFromAuthority(FGuid::NewGuid());
		ICatInventoryWorldItem* Receiver = Cast<ICatInventoryWorldItem>(WorldActor);
		if (!Receiver || !Receiver->InitializeFromInventoryFromAuthority(ReleasedItem, Quantity)) return Finish(ECatDomainCommandError::InvalidPayload);
		WorldActor->FinishSpawning(Character->GetActorTransform());
	}
	else if (WorldActor->GetWorld() != GetWorld() || Quantity != Entry->StackCount)
	{
		return Finish(ECatDomainCommandError::InvalidPayload);
	}
	UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(WorldActor->GetRootComponent());
	FTransform Transform;
	// 先检查真实刚体形状和可移动性；不能以设置了SimulatePhysics布尔值就假定物体确实能够运动。
	if (!Body || Body->Mobility != EComponentMobility::Movable || !Body->GetBodySetup()
		|| Body->GetBodySetup()->AggGeom.GetElementCount() == 0
		|| Body->GetBodySetup()->CollisionTraceFlag == CTF_UseComplexAsSimple
		|| !FindInventoryWorldTransform(Character, WorldActor, Action, *Settings, Transform))
		return Finish(ECatDomainCommandError::PermissionDenied);
	Result.Error = ECatDomainCommandError::AlreadyResolved;
	TerminalPayloadByKey.Add(Key, Payload);
	TerminalCache.Add(Key, Result);
	if (!ConsumeItemAtSlot(SlotIndex, Quantity)) return Finish(ECatDomainCommandError::InvalidPayload);
	ReleasedItem->SetRuntimeOwnerActor(WorldActor);
	Body->SetSimulatePhysics(false);
	WorldActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	WorldActor->SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	WorldActor->SetOwner(nullptr);
	WorldActor->SetInstigator(nullptr);
	WorldActor->SetActorHiddenInGame(false);
	WorldActor->SetActorEnableCollision(true);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Body->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	if (Action == ECatInventoryWorldAction::Drop)
	{
		Body->SetSimulatePhysics(true);
		Body->SetPhysicsLinearVelocity(Character->GetActorForwardVector().GetSafeNormal2D() * Settings->DropForwardSpeed + FVector(0, 0, Settings->DropUpwardSpeed));
	}
	WorldActor->ForceNetUpdate();
	return Finish(ECatDomainCommandError::None);
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

// 可见库存数量汇总流程：
// 1. 空定义 ID 直接返回 0，避免调用方把缺配置当成“任意物品都有数量”。
// 2. 只遍历 InventoryList 的正式可见槽位，要求实例有效、数量为正且定义 ID 精确匹配。
// 3. held 活动区和 Fishing 会话冻结不从这里叠加，因为它们已经离开玩家当前可整理、可选择的背包格。
int32 UCatInventoryComponent::CountVisibleInventoryQuantityByDefinitionId(const FName DefinitionId) const
{
	if (DefinitionId.IsNone())
	{
		return 0;
	}

	int32 Quantity = 0;
	for (const FCatInventoryEntry& Entry : InventoryList.Entries)
	{
		if (Entry.Instance != nullptr
			&& Entry.StackCount > 0
			&& Entry.Instance->GetItemDefinitionId() == DefinitionId)
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

// 结构化使用提交流程：
// 1. 先确认当前组件仍是服务器正式库存，并且 RequestId、来源组件和槽位参数有效。
// 2. 再按服务器当前槽位重读 entry、实例和定义；空格或坏实例按正式库存错误返回。
// 3. 通过后把当前 entry 交给物品实例提交具体领域效果；库存组件不认识装备、GAS、草药或窝料的内部规则。
// 4. 下游结果决定提交状态和错误；库存只在 RequestId 缺失时补回本次请求 ID，便于日志串联。
// 5. 最后记录当前读取到的定义、实例和数量，供失败或成功回包诊断。
FCatDomainCommandResult UCatInventoryComponent::UseItemAtSlotFromAuthority(
	const FCatInventoryItemUseContext& UseContext)
{
	FCatDomainCommandResult Result;
	Result.RequestId = UseContext.RequestId;

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
		TEXT("Event=inventory_use_item Owner=%s Request=%s Slot=%d Definition=%s Item=%s Stack=%d Committed=%s Error=%s"),
		*GetNameSafe(GetOwner()),
		*UseContext.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		UseContext.InventorySlotIndex,
		*DefinitionId.ToString(),
		*ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		StackCount,
		Result.bCommitted ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(Result.Error));
	return Result;
}

// authority 库存移动流程：
// 1. 先用 Source 组件自己的终态缓存处理同一 RequestId 重放；载荷变化会被拒绝而不读取当前格子。
// 2. 再复核 Source/Target 的 authority、当前槽位和目标组件；服务器按此刻条目执行，不接受客户端提供内容快照。
// 3. 通过后复用内部交换规则完成合并、搬空格或互换，并由实际变更决定是否通知双方库存。
// 4. 最后统一广播变化并缓存首次结果；调用方只拿结构化终态刷新 UI 或同步钓具读模型。
FCatDomainCommandResult UCatInventoryComponent::MoveItemToInventoryFromAuthority(const FGuid RequestId,
	const int32 SourceSlotIndex,
	UCatInventoryComponent* TargetInventory, const int32 TargetSlotIndex, const FString& IdempotencyPayloadContext)
{
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
		if (Mutation.bChanged)
		{
			BroadcastInventoryChange(SourceSlotIndex);
			if (TargetInventory != this)
			{
				TargetInventory->BroadcastInventoryChange(TargetSlotIndex);
			}
		}
	}

	TerminalCache.Add(Key, Result);
	TerminalPayloadByKey.Add(Key, PayloadSignature);
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

// 容量预演单项流程：先合并同类未满格，再占用空格；每个目标槽都复用定义接收规则，避免预演放行正式入库会拒绝的物品。
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
		for (int32 SlotIndex = 0; SlotIndex < SimulatedSlots.Num(); ++SlotIndex)
		{
			if (InOutRemainingCount <= 0)
			{
				break;
			}

			FSimulatedInventorySlot& SimulatedSlot = SimulatedSlots[SlotIndex];
			if (SimulatedSlot.ItemDefinition == nullptr
				|| !SimulatedSlot.ItemDefinition->CanStackWith(ItemDefinition))
			{
				continue;
			}
			if (!CanAcceptInventoryDefinitionAtSlot(ItemDefinition, SlotIndex))
			{
				continue;
			}

			const int32 AvailableSpace = FMath::Max(0, MaxStackCount - SimulatedSlot.StackCount);
			const int32 AddAmount = FMath::Min(InOutRemainingCount, AvailableSpace);
			SimulatedSlot.StackCount += AddAmount;
			InOutRemainingCount -= AddAmount;
		}
	}

	for (int32 SlotIndex = 0; SlotIndex < SimulatedSlots.Num(); ++SlotIndex)
	{
		if (InOutRemainingCount <= 0)
		{
			break;
		}

		FSimulatedInventorySlot& SimulatedSlot = SimulatedSlots[SlotIndex];
		if (SimulatedSlot.ItemDefinition != nullptr)
		{
			continue;
		}
		if (!CanAcceptInventoryDefinitionAtSlot(ItemDefinition, SlotIndex))
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
