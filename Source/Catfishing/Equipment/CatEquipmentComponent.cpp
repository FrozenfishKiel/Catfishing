#include "Equipment/CatEquipmentComponent.h"

#include "Equipment/CatEquipmentInventoryItemInstance.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/Inventory/CatInventoryTransferService.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Equipment/CatRunInventorySlotOperations.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryMutationScope.h"
#include "Misc/ScopeExit.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/CatInventorySettings.h"
#include "Inventory/CatInventoryStatics.h"
#include "Logging/CatLog.h"
#include "Logging/CatLogContext.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogCatEquipment, Log, All);

namespace
{
	// 玩家随身容量迁移流程：InventorySettings 是正式来源；旧 EquipmentSettings 只有被测试或诊断改成非项目默认值时才临时覆盖。
	int32 ResolvePlayerInventorySlotCapacity()
	{
		const UCatInventorySettings* InventorySettings = GetDefault<UCatInventorySettings>();
		const int32 InventorySlotCapacity =
			InventorySettings != nullptr ? InventorySettings->GetPlayerInventorySlotCapacity() : 0;
		const UCatEquipmentSettings* EquipmentSettings = GetDefault<UCatEquipmentSettings>();
		const int32 LegacySlotCapacity =
			EquipmentSettings != nullptr ? FMath::Max(0, EquipmentSettings->InventorySlotCapacity)
			: UCatInventorySettings::ProjectDefaultPlayerInventorySlotCapacity;
		return LegacySlotCapacity != UCatInventorySettings::ProjectDefaultPlayerInventorySlotCapacity
			? LegacySlotCapacity : InventorySlotCapacity;
	}

	// 旧随身库存投影等价判断流程：按 UI 和存档会读取的全部字段比较；只有投影格的可见载荷差异才需要推进兼容 Snapshot 版本。
	bool AreLegacyInventorySlotArraysEquivalent(const TArray<FCatRunInventorySlot>& Left,
		const TArray<FCatRunInventorySlot>& Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}

		for (int32 SlotIndex = 0; SlotIndex < Left.Num(); ++SlotIndex)
		{
			const FCatRunInventorySlot& LeftSlot = Left[SlotIndex];
			const FCatRunInventorySlot& RightSlot = Right[SlotIndex];
			if (LeftSlot.DefinitionId != RightSlot.DefinitionId
				|| LeftSlot.ItemInstanceId != RightSlot.ItemInstanceId
				|| LeftSlot.Quantity != RightSlot.Quantity
				|| !FMath::IsNearlyEqual(LeftSlot.RodDurability, RightSlot.RodDurability)
				|| LeftSlot.bRodBroken != RightSlot.bRodBroken)
			{
				return false;
			}
		}

		return true;
	}

	// 库存目录装备解析流程：正式发货先从 Inventory Catalog 认定物品，再把仍需要钓鱼语义的定义窄化为 EquipmentDefinition；目录缺失或类型不匹配返回空，由调用方决定拒绝或只记录投影同步失败。
	UCatEquipmentDefinition* FindEquipmentDefinitionFromInventoryCatalog(const FName DefinitionId)
	{
		const UCatInventorySettings* InventorySettings = GetDefault<UCatInventorySettings>();
		UCatInventoryItemDefinition* ItemDefinition =
			InventorySettings != nullptr ? InventorySettings->FindRuntimeDefinition(DefinitionId) : nullptr;
		return Cast<UCatEquipmentDefinition>(ItemDefinition);
	}
}

// 构造流程：开启组件复制并关闭 Tick；Snapshot 初始 Revision=0 表示还没有随身库存提交或钓鱼选择。
UCatEquipmentComponent::UCatEquipmentComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
}

// 复制声明流程：保留父类字段并注册唯一 Snapshot；终态缓存和定义对象不复制。
void UCatEquipmentComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, Snapshot);
}

// Snapshot 读取流程：返回服务器钓鱼选择和旧库存投影；正式物品实例由 Owner 的 InventoryComponent 持有。
const FCatEquipmentLoadoutSnapshot& UCatEquipmentComponent::GetSnapshot() const
{
	return Snapshot;
}

// 开局装备配置流程：
// 1. 先要求组件仍有 authority Owner 且装备设置存在；任一依赖缺失时不写入库存，避免生命周期早期或客户端伪造开局提交。
// 2. 再检查显式开关和当前鱼竿选择，已关闭或玩家/Profile 已有选择时保持现状，不覆盖既有装备事实。
// 3. 选择为空时通过正式 Configure 入口提交配置的鱼竿、鱼饵、鱼漂与抄网；该入口继续负责解锁、库存持有量和选择 Revision 裁决。
// 4. 只有配置真正提交且窝料定义、数量均有效时，才按正式 InventoryRevision 发货并同步旧投影；没有正式库存时拒绝补给。
// 5. 正式库存发货后的投影同步失败只记诊断日志，不回滚正式库存发货，避免 starter 兜底重新制造第二笔库存。
void UCatEquipmentComponent::ApplyConfiguredStarterLoadoutFromAuthority()
{
	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || !Settings)
	{
		return;
	}
	if (!Settings->bAutoConfigureStarterLoadout)
	{
		return;
	}
	if (!Snapshot.RodDefinitionId.IsNone())
	{
		return;
	}
	const FCatDomainCommandResult Configure = ConfigureLoadoutFromAuthority(FGuid::NewGuid(), Snapshot.Revision,
		Settings->StarterRodDefinitionId, Settings->StarterBaitDefinitionId, Settings->StarterFloatDefinitionId,
		Settings->StarterScoopNetDefinitionId);
	UE_LOG(LogCatEquipment, Log, TEXT("Event=starter_loadout_configure Committed=%s Error=%s Revision=%lld"),
		Configure.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Configure.Error), Configure.Revision);
	if (!Configure.bCommitted || Settings->StarterChumDefinitionId.IsNone() || Settings->StarterChumQuantity <= 0)
	{
		return;
	}
	const FGuid GrantRequestId = FGuid::NewGuid();
	FCatDomainCommandResult Grant;
	Grant.RequestId = GrantRequestId;
	if (UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent())
	{
		OwnerInventory->SetInventorySlotCountFromAuthority(GetConfiguredInventorySlotCapacity());
		Grant = OwnerInventory->GrantInventoryDefinitionFromAuthority(GrantRequestId,
			OwnerInventory->GetInventoryRevision(), Settings->StarterChumDefinitionId, Settings->StarterChumQuantity);
		if (Grant.bCommitted)
		{
			const UCatEquipmentDefinition* GrantedDefinition =
				FindEquipmentDefinitionFromInventoryCatalog(Settings->StarterChumDefinitionId);
			if (!RefreshInventoryProjectionFromInventoryComponentFromAuthority(
				GrantedDefinition, Settings->StarterChumDefinitionId))
			{
				UE_LOG(LogCatEquipment, Warning,
					TEXT("Event=starter_chum_projection_sync_failed RequestId=%s Definition=%s InventoryRevision=%lld SnapshotRevision=%lld"),
					*GrantRequestId.ToString(EGuidFormats::DigitsWithHyphens),
					*Settings->StarterChumDefinitionId.ToString(), Grant.Revision, Snapshot.Revision);
			}
		}
	}
	else
	{
		Grant.Error = ECatDomainCommandError::DependencyUnavailable;
		Grant.Revision = Snapshot.Revision;
	}
	UE_LOG(LogCatCharacter, Log, TEXT("Event=starter_chum_grant Committed=%s Error=%s Revision=%lld Definition=%s Quantity=%d"),
		Grant.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Grant.Error), Grant.Revision,
		*Settings->StarterChumDefinitionId.ToString(), Settings->StarterChumQuantity);
}

// 持久化导出流程：
// 1. 先拒绝尚未结算的 Fishing 预留，避免把仍在会话中的饵料伪装成已提交库存。
// 2. 正式 Character 从 InventoryComponent 重建保存载荷；没有正式库存组件的兼容宿主只沿用 Snapshot 里的旧投影。
// 3. 正式库存存在时从 held-entry 活动区读取部署实例并合并成收回姿态；兼容宿主只把 Equipment 玩法镜像作为导出载荷补齐。
// 4. 合并后的候选载荷还要完整校验，Save 只接收不会重复实物、不会超容量且选择引用一致的快照。
bool UCatEquipmentComponent::ExportSnapshotFromAuthority(FCatEquipmentLoadoutSnapshot& OutSnapshot, FText& OutFailure) const
{
	OutSnapshot = FCatEquipmentLoadoutSnapshot();
	if (!GetOwner() || !GetOwner()->HasAuthority() || HasActiveFishingUse())
	{
		OutFailure = FText::FromString(TEXT("玩家库存仍有未结算 Fishing 预留，等待领域收口后才能保存。"));
		return false;
	}
	FCatEquipmentLoadoutSnapshot Candidate = Snapshot;
	UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent();
	if (OwnerInventory != nullptr && !BuildSnapshotInventorySlotsFromOwnerInventoryComponent(Candidate.InventorySlots))
	{
		OutFailure = FText::FromString(TEXT("正式随身库存无法转换为可保存载荷。"));
		return false;
	}
	const auto AppendDeployedItem = [this, &Candidate, &OutFailure](const FCatRunInventorySlot& DeployedItem)
	{
		if (!CatRunInventorySlotOperations::IsInventorySlotOccupied(DeployedItem))
		{
			OutFailure = FText::FromString(TEXT("部署物品载荷无效，不能保存。"));
			return false;
		}
		if (Candidate.InventorySlots.ContainsByPredicate([&DeployedItem](const FCatRunInventorySlot& Slot)
			{ return Slot.ItemInstanceId == DeployedItem.ItemInstanceId; }))
		{
			OutFailure = FText::FromString(TEXT("部署实例同时存在于背包，不能保存重复实物。"));
			return false;
		}
		FCatRunInventorySlot* Empty = Candidate.InventorySlots.FindByPredicate([](const FCatRunInventorySlot& Slot)
			{ return !CatRunInventorySlotOperations::IsInventorySlotOccupied(Slot); });
		if (!Empty && Candidate.InventorySlots.Num() >= GetConfiguredInventorySlotCapacity())
		{
			OutFailure = FText::FromString(TEXT("部署物品收回后的库存超过当前容量，必须先腾出背包空间。"));
			return false;
		}
		(Empty ? *Empty : Candidate.InventorySlots.AddDefaulted_GetRef()) = DeployedItem;
		return true;
	};
	if (OwnerInventory != nullptr)
	{
		TArray<FCatInventoryEntry> HeldEntries;
		OwnerInventory->AppendHeldInventoryEntriesFromAuthority(HeldEntries);
		for (const FCatInventoryEntry& HeldEntry : HeldEntries)
		{
			FCatRunInventorySlot DeployedItem;
			if (!BuildLegacyRunInventorySlotFromFormalEntry(HeldEntry, DeployedItem))
			{
				OutFailure = FText::FromString(TEXT("正式部署物品无法转换为可保存载荷。"));
				return false;
			}
			if (!AppendDeployedItem(DeployedItem))
			{
				return false;
			}
		}
	}
	else
	{
		for (const TPair<FGuid, FCatInventoryItemUseRecord>& Pair : InventoryItemUseRecords)
		{
			if (!Pair.Value.bReleased && !AppendDeployedItem(Pair.Value.Item))
			{
				return false;
			}
		}
	}
	const auto ClearUnavailableSelection = [&Candidate](FName& DefinitionId, FGuid& ItemId)
	{
		if (!Candidate.InventorySlots.ContainsByPredicate([ItemId](const FCatRunInventorySlot& Slot)
			{ return ItemId.IsValid() && Slot.ItemInstanceId == ItemId && Slot.Quantity > 0; }))
		{
			DefinitionId = NAME_None;
			ItemId.Invalidate();
		}
	};
	ClearUnavailableSelection(Candidate.RodDefinitionId, Candidate.RodItemInstanceId);
	ClearUnavailableSelection(Candidate.BaitDefinitionId, Candidate.BaitItemInstanceId);
	ClearUnavailableSelection(Candidate.FloatDefinitionId, Candidate.FloatItemInstanceId);
	ClearUnavailableSelection(Candidate.ScoopNetDefinitionId, Candidate.ScoopNetItemInstanceId);
	if (Candidate.RodDefinitionId.IsNone())
	{
		Candidate.RodDurability = 0.0;
		Candidate.bRodBroken = false;
	}
	if (!ValidatePersistentSnapshotPayload(Candidate, OutFailure))
	{
		return false;
	}
	OutSnapshot = MoveTemp(Candidate);
	return true;
}

// 退出部署收口流程：Save 已接管本组件仍持有的实例，先核查全部 Use/held，再退役对应场景竿与记录。
// 已托管的竿没有本组件的 Use/held，不参与本次退役；活动会话锁未解除或 Destroy 失败时明确拒绝。
bool UCatEquipmentComponent::RetireDeploymentAfterPersistentCapture(APlayerState& PlayerState)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return false;
	UCatInventoryComponent* Inventory = ResolveOwnerInventoryComponent();
	if (!Inventory) return false;
	TArray<FGuid> ItemIds;
	TArray<TWeakObjectPtr<ACatFishingRodActor>> Rods;
	// 只退役本次保存载荷仍持有的实例；已托管给队友继续使用的竿不再属于这些记录。
	for (const auto& Pair : InventoryItemUseRecords)
	{
		if (Pair.Value.bReleased) continue;
		if (Pair.Value.BoundFishingSessionId.IsValid() || !Inventory->FindHeldInventoryEntryFromAuthority(Pair.Key))
			return false;
		ItemIds.Add(Pair.Key);
	}
	for (TActorIterator<ACatFishingRodActor> It(GetWorld()); It; ++It)
	{
		if (ItemIds.Contains(It->GetPresentationState().ItemInstanceId)) Rods.Add(*It);
	}
	for (const TWeakObjectPtr<ACatFishingRodActor>& Rod : Rods)
	{
		if (Rod.IsValid() && !Rod->Destroy()) return false;
	}
	for (const FGuid ItemId : ItemIds)
	{
		if (!Inventory->RetireHeldInventoryEntryFromAuthority(ItemId)) return false;
		if (FCatInventoryItemUseRecord* Record = InventoryItemUseRecords.Find(ItemId)) Record->bReleased = true;
	}
	UE_LOG(LogCatEquipment, Log, TEXT("Event=persistence_departure_deployment_retired Owner=%s PlayerId=%d Items=%d Rods=%d World=%s NetMode=%d Authority=true LocalRole=%d"),
		*GetNameSafe(GetOwner()), PlayerState.GetPlayerId(), ItemIds.Num(), Rods.Num(), *GetNameSafe(GetWorld()),
		static_cast<int32>(GetWorld()->GetNetMode()), static_cast<int32>(GetOwner()->GetLocalRole()));
	return true;
}

// 随身库存恢复预检流程：
// 1. 先确认调用点仍是 authority，且没有 Fishing 或部署物品正在借走库存实例。
// 2. 再逐格校验容量、定义、数量、实例唯一性和鱼竿耐久，空格不能夹带旧实例残留。
// 3. 最后校验所有选择都精确指向同一快照中的具体实例；本方法只读，供 Save 组合跨领域原子预检。
bool UCatEquipmentComponent::CanRestoreSnapshotFromAuthority(const FCatEquipmentLoadoutSnapshot& RestoredSnapshot,
	FText& OutFailure) const
{
	OutFailure = FText::GetEmpty();
	if (!GetOwner() || !GetOwner()->HasAuthority() || HasActiveFishingUse() || HasActiveInventoryItemUse()
		|| RestoredSnapshot.InventorySlots.Num() > GetConfiguredInventorySlotCapacity())
	{
		OutFailure = FText::FromString(TEXT("随身库存恢复上下文不可用、存在活动使用记录或超过容量。"));
		return false;
	}
	return ValidatePersistentSnapshotPayload(RestoredSnapshot, OutFailure);
}

// 库存载荷校验流程：先核对现行容量和目录，再逐格验证实物与耐久，最后检查所有装备选择引用；没有 authority 或会话副作用，供导出与恢复共同使用。
bool UCatEquipmentComponent::ValidatePersistentSnapshotPayload(const FCatEquipmentLoadoutSnapshot& RestoredSnapshot,
	FText& OutFailure) const
{
	OutFailure = FText::GetEmpty();
	if (RestoredSnapshot.InventorySlots.Num() > GetConfiguredInventorySlotCapacity())
	{
		OutFailure = FText::FromString(TEXT("玩家持久化库存超过当前配置容量。"));
		return false;
	}
	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	if (!Settings)
	{
		OutFailure = FText::FromString(TEXT("装备运行目录不可用。"));
		return false;
	}
	TSet<FGuid> SeenInstanceIds;
	for (const FCatRunInventorySlot& Slot : RestoredSnapshot.InventorySlots)
	{
		const bool bOccupied = CatRunInventorySlotOperations::IsInventorySlotOccupied(Slot);
		if (!bOccupied)
		{
			if (!Slot.DefinitionId.IsNone() || Slot.ItemInstanceId.IsValid() || Slot.Quantity != 0
				|| Slot.RodDurability != 0.0 || Slot.bRodBroken)
			{
				OutFailure = FText::FromString(TEXT("随身库存空格携带了残留运行状态。"));
				return false;
			}
			continue;
		}
		const UCatEquipmentDefinition* Definition = Settings->FindRuntimeDefinition(Slot.DefinitionId);
		if (!Slot.ItemInstanceId.IsValid() || SeenInstanceIds.Contains(Slot.ItemInstanceId) || !Definition
			|| !Definition->IsRuntimeDefinitionReady() || Slot.Quantity <= 0
			|| Slot.Quantity > GetInventoryStackLimit(*Definition))
		{
			OutFailure = FText::FromString(TEXT("随身库存含有无效定义、数量或重复实例。"));
			return false;
		}
		if (Definition->Kind == ECatEquipmentKind::Rod)
		{
			if (!FMath::IsFinite(Slot.RodDurability) || Slot.RodDurability < 0.0
				|| Slot.RodDurability > Definition->MaximumRodDurability
				|| (Slot.bRodBroken && Slot.RodDurability != 0.0))
			{
				OutFailure = FText::FromString(TEXT("鱼竿耐久与定义约束不一致。"));
				return false;
			}
		}
		else if (Slot.RodDurability != 0.0 || Slot.bRodBroken)
		{
			OutFailure = FText::FromString(TEXT("非鱼竿库存格包含鱼竿状态。"));
			return false;
		}
		SeenInstanceIds.Add(Slot.ItemInstanceId);
	}
	const auto HasSelectedInstance = [&RestoredSnapshot, Settings](const FName DefinitionId,
		const FGuid InstanceId, const ECatEquipmentKind ExpectedKind)
	{
		if (DefinitionId.IsNone())
		{
			return !InstanceId.IsValid();
		}
		return InstanceId.IsValid() && RestoredSnapshot.InventorySlots.ContainsByPredicate(
			[DefinitionId, InstanceId, ExpectedKind, Settings](const FCatRunInventorySlot& Slot)
			{
				const UCatEquipmentDefinition* Definition = Settings->FindRuntimeDefinition(Slot.DefinitionId);
				return Slot.DefinitionId == DefinitionId && Slot.ItemInstanceId == InstanceId && Definition
					&& Definition->Kind == ExpectedKind;
			});
	};
	if (!HasSelectedInstance(RestoredSnapshot.RodDefinitionId, RestoredSnapshot.RodItemInstanceId, ECatEquipmentKind::Rod)
		|| !HasSelectedInstance(RestoredSnapshot.BaitDefinitionId, RestoredSnapshot.BaitItemInstanceId, ECatEquipmentKind::Bait)
		|| !HasSelectedInstance(RestoredSnapshot.FloatDefinitionId, RestoredSnapshot.FloatItemInstanceId, ECatEquipmentKind::Float)
		|| !HasSelectedInstance(RestoredSnapshot.ScoopNetDefinitionId, RestoredSnapshot.ScoopNetItemInstanceId,
			ECatEquipmentKind::ScoopNet))
	{
		OutFailure = FText::FromString(TEXT("装备选择没有指向同一份库存中的正确实例。"));
		return false;
	}
	const FCatRunInventorySlot* SelectedRod = RestoredSnapshot.InventorySlots.FindByPredicate(
		[&RestoredSnapshot](const FCatRunInventorySlot& Slot)
		{
			return Slot.ItemInstanceId == RestoredSnapshot.RodItemInstanceId;
		});
	if (!RestoredSnapshot.RodDefinitionId.IsNone()
		&& (!SelectedRod || RestoredSnapshot.RodDurability != SelectedRod->RodDurability
			|| RestoredSnapshot.bRodBroken != SelectedRod->bRodBroken))
	{
		OutFailure = FText::FromString(TEXT("鱼竿选择状态与库存实例不一致。"));
		return false;
	}
	if (RestoredSnapshot.RodDefinitionId.IsNone()
		&& (RestoredSnapshot.RodItemInstanceId.IsValid() || RestoredSnapshot.RodDurability != 0.0
			|| RestoredSnapshot.bRodBroken))
	{
		OutFailure = FText::FromString(TEXT("空鱼竿选择含有实例或耐久状态。"));
		return false;
	}
	return true;
}

// 随身库存恢复提交流程：
// 1. 先重复完整预检，并在正式角色上把保存载荷导入 InventoryComponent，失败时保持现有背包和选择不变。
// 2. 成功后清掉只属于旧 Character 生命周期的请求缓存、Fishing reservation 和 Equipment 玩法镜像；正式 held entry 在预检阶段被禁止存在。
// 3. 最后替换兼容快照并发布旧读模型；发布只通知选择和投影变化，不再反向改写正式背包。
bool UCatEquipmentComponent::RestoreSnapshotFromAuthority(const FCatEquipmentLoadoutSnapshot& RestoredSnapshot)
{
	FText Failure;
	if (!CanRestoreSnapshotFromAuthority(RestoredSnapshot, Failure))
	{
		return false;
	}
	if (!ImportSnapshotInventoryToOwnerInventoryComponent(RestoredSnapshot))
	{
		return false;
	}
	Snapshot = RestoredSnapshot;
	Snapshot.Revision = FMath::Max<int64>(1, Snapshot.Revision + 1);
	TerminalCache.Reset();
	TerminalPayloadByKey.Reset();
	FishingUseRecords.Reset();
	InventoryItemUseRecords.Reset();
	InventoryItemUseTerminalCache.Reset();
	PublishSnapshot();
	return true;
}

// 当前钓鱼选择配置流程：
// 1. 先用 RequestId 返回既有终态；部署竿可沿用，也可切换到另一根库存竿，已运行的会话仍绑定原实例。
// 2. 每次提交都必须通过服务器目录、authority、Revision、定义类别、消耗属性和 Profile 解锁证明。
// 3. 正式角色从 InventoryComponent 解析钓具实例；没有正式库存的兼容宿主只读当前尚未收口的同一条 Use 记录。
// 4. 同一套定义和实例选择直接返回 AlreadyResolved；不同选择会切换当前钓鱼选择，并从鱼竿实例读取耐久。
// 5. 成功时只写钓鱼选择、实例身份和当前鱼竿运行态，并发布给迁移期旧消费者的投影快照。
FCatDomainCommandResult UCatEquipmentComponent::ConfigureLoadoutFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const FName RodDefinitionId, const FName BaitDefinitionId,
	const FName FloatDefinitionId, const FName ScoopNetDefinitionId, const FName RodSkinDefinitionId,
	const FGuid RodItemInstanceId, const FGuid BaitItemInstanceId, const FGuid FloatItemInstanceId,
	const FGuid ScoopNetItemInstanceId)
{
	// 钓具配置提交流程：
	// 1. RequestId 命中终态缓存时直接返回旧结果，避免重放请求重新选择或推进 Revision。
	// 2. 再校验服务器权威、定义类型、解锁权限和版本；任一失败只写错误码，不改变当前选择和库存投影。
	// 3. 当前选中鱼竿正在部署时，正式库存存在就从 held-entry 确认同一实例，兼容宿主只读取 Equipment 的 Use 镜像。
	// 4. 所有候选装备都先解析成正式库存或兼容投影里的占用槽位，再写 Snapshot 里的实例身份。
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString Key = MakeTerminalKey(TEXT("ConfigureLoadout"), RequestId);
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		Result = *Cached;
		MarkCommandReplayed(Result);
		return Result;
	}
	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	UCatEquipmentDefinition* Rod = Settings->FindRuntimeDefinition(RodDefinitionId);
	UCatEquipmentDefinition* Bait = Settings->FindRuntimeDefinition(BaitDefinitionId);
	UCatEquipmentDefinition* Float = Settings->FindRuntimeDefinition(FloatDefinitionId);
	UCatEquipmentDefinition* Scoop = ScoopNetDefinitionId.IsNone() ? nullptr : Settings->FindRuntimeDefinition(ScoopNetDefinitionId);
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const ACatfishingPlayerState* PlayerState = OwnerPawn ? OwnerPawn->GetPlayerState<ACatfishingPlayerState>() : nullptr;
	if (Settings->ProfileLoadoutTrustPolicy != ECatDomainPolicy::Enabled)
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
	}
	else if (!GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid() || !Rod || !Bait || !Float
		|| (!ScoopNetDefinitionId.IsNone() && !Scoop))
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else if (Snapshot.Revision != ExpectedRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
	}
	else if (Rod->Kind != ECatEquipmentKind::Rod || Bait->Kind != ECatEquipmentKind::Bait
		|| Float->Kind != ECatEquipmentKind::Float || (Scoop && Scoop->Kind != ECatEquipmentKind::ScoopNet)
		|| Rod->bRunConsumable || !Bait->bRunConsumable || Float->bRunConsumable
		|| (Scoop && Scoop->bRunConsumable))
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (!PlayerState || !PlayerState->HasServerAuthorizedEquipmentUnlock(Rod->RequiredUnlockId)
		|| !PlayerState->HasServerAuthorizedEquipmentUnlock(Bait->RequiredUnlockId)
		|| !PlayerState->HasServerAuthorizedEquipmentUnlock(Float->RequiredUnlockId)
		|| (Scoop && !PlayerState->HasServerAuthorizedEquipmentUnlock(Scoop->RequiredUnlockId)))
	{
		Result.Error = ECatDomainCommandError::PermissionDenied;
	}
	else
	{
		FCatRunInventorySlot ActiveSelectedRodSlot;
		bool bSelectedRodIsInUse = false;
		if (Snapshot.RodItemInstanceId.IsValid())
		{
			if (ResolveOwnerInventoryComponent() != nullptr)
			{
				bSelectedRodIsInUse = TryBuildHeldInventoryUseSlot(
					Snapshot.RodItemInstanceId, ActiveSelectedRodSlot)
					&& ActiveSelectedRodSlot.DefinitionId == Snapshot.RodDefinitionId
					&& ActiveSelectedRodSlot.ItemInstanceId == Snapshot.RodItemInstanceId;
			}
			else if (const FCatInventoryItemUseRecord* ActiveSelectedRod =
				FindInventoryItemUseRecord(Snapshot.RodItemInstanceId))
			{
				if (!ActiveSelectedRod->bReleased
					&& ActiveSelectedRod->Item.DefinitionId == Snapshot.RodDefinitionId
					&& ActiveSelectedRod->Item.ItemInstanceId == Snapshot.RodItemInstanceId)
				{
					ActiveSelectedRodSlot = ActiveSelectedRod->Item;
					bSelectedRodIsInUse = true;
				}
			}
		}
		const bool bRequestsActiveSelectedRod = bSelectedRodIsInUse
			&& RodDefinitionId == ActiveSelectedRodSlot.DefinitionId
			&& (!RodItemInstanceId.IsValid() || RodItemInstanceId == ActiveSelectedRodSlot.ItemInstanceId);
		// 允许选择另一根库存竿；原会话仍绑定原部署实例。
		{
			const auto ResolveSelectedRodSlot =
				[this, ActiveSelectedRodSlot, bRequestsActiveSelectedRod](
					const FName DefinitionId, const FGuid ItemInstanceId,
					FCatRunInventorySlot& OutSlot) -> bool
				{
					if (bRequestsActiveSelectedRod)
					{
						OutSlot = ActiveSelectedRodSlot;
						return OutSlot.DefinitionId == DefinitionId
							&& (!ItemInstanceId.IsValid() || OutSlot.ItemInstanceId == ItemInstanceId);
					}
					return TryResolveSelectionInventorySlot(DefinitionId, ItemInstanceId, OutSlot);
				};
			FCatRunInventorySlot RodSlotBeforeNormalize;
			FCatRunInventorySlot BaitSlotBeforeNormalize;
			FCatRunInventorySlot FloatSlotBeforeNormalize;
			FCatRunInventorySlot ScoopSlotBeforeNormalize;
			const bool bHasRodSlotBeforeNormalize =
				ResolveSelectedRodSlot(RodDefinitionId, RodItemInstanceId, RodSlotBeforeNormalize);
			const bool bHasBaitSlotBeforeNormalize =
				TryResolveSelectionInventorySlot(BaitDefinitionId, BaitItemInstanceId, BaitSlotBeforeNormalize);
			const bool bHasFloatSlotBeforeNormalize =
				TryResolveSelectionInventorySlot(FloatDefinitionId, FloatItemInstanceId, FloatSlotBeforeNormalize);
			const bool bHasScoopSlotBeforeNormalize = ScoopNetDefinitionId.IsNone()
				|| TryResolveSelectionInventorySlot(ScoopNetDefinitionId, ScoopNetItemInstanceId,
					ScoopSlotBeforeNormalize);
			if (!bHasRodSlotBeforeNormalize || !bHasBaitSlotBeforeNormalize || !bHasFloatSlotBeforeNormalize
				|| !bHasScoopSlotBeforeNormalize)
			{
				Result.Error = ECatDomainCommandError::NotFound;
			}
			else
			{
				const bool bNormalized = NormalizeInventorySlots();
				FCatRunInventorySlot RodSlot;
				FCatRunInventorySlot BaitSlot;
				FCatRunInventorySlot FloatSlot;
				FCatRunInventorySlot ScoopSlot;
				const bool bHasRodSlot = ResolveSelectedRodSlot(RodDefinitionId, RodItemInstanceId, RodSlot);
				const bool bHasBaitSlot = TryResolveSelectionInventorySlot(BaitDefinitionId, BaitItemInstanceId,
					BaitSlot);
				const bool bHasFloatSlot = TryResolveSelectionInventorySlot(FloatDefinitionId, FloatItemInstanceId,
					FloatSlot);
				const bool bHasScoopSlot = ScoopNetDefinitionId.IsNone()
					|| TryResolveSelectionInventorySlot(ScoopNetDefinitionId, ScoopNetItemInstanceId, ScoopSlot);
				if (!bHasRodSlot || !bHasBaitSlot || !bHasFloatSlot || !bHasScoopSlot)
				{
					Result.Error = ECatDomainCommandError::NotFound;
				}
				else
				{
					const FGuid NewScoopItemInstanceId = ScoopNetDefinitionId.IsNone()
						? FGuid() : ScoopSlot.ItemInstanceId;
					const bool bSameLoadout = Snapshot.RodDefinitionId == RodDefinitionId
						&& Snapshot.RodItemInstanceId == RodSlot.ItemInstanceId
						&& Snapshot.BaitDefinitionId == BaitDefinitionId
						&& Snapshot.BaitItemInstanceId == BaitSlot.ItemInstanceId
						&& Snapshot.FloatDefinitionId == FloatDefinitionId
						&& Snapshot.FloatItemInstanceId == FloatSlot.ItemInstanceId
						&& Snapshot.ScoopNetDefinitionId == ScoopNetDefinitionId
						&& Snapshot.ScoopNetItemInstanceId == NewScoopItemInstanceId
						&& Snapshot.RodSkinDefinitionId == RodSkinDefinitionId
						&& Snapshot.RodDurability == RodSlot.RodDurability
						&& Snapshot.bRodBroken == RodSlot.bRodBroken;
					if (bSameLoadout && !bNormalized)
					{
						Result.Error = ECatDomainCommandError::AlreadyResolved;
						Result.Revision = Snapshot.Revision;
						TerminalCache.Add(Key, Result);
						return Result;
					}
					Snapshot.RodDefinitionId = RodDefinitionId;
					Snapshot.RodItemInstanceId = RodSlot.ItemInstanceId;
					Snapshot.BaitDefinitionId = BaitDefinitionId;
					Snapshot.BaitItemInstanceId = BaitSlot.ItemInstanceId;
					Snapshot.FloatDefinitionId = FloatDefinitionId;
					Snapshot.FloatItemInstanceId = FloatSlot.ItemInstanceId;
					Snapshot.ScoopNetDefinitionId = ScoopNetDefinitionId;
					Snapshot.ScoopNetItemInstanceId = NewScoopItemInstanceId;
					Snapshot.RodSkinDefinitionId = RodSkinDefinitionId;
					Snapshot.RodDurability = RodSlot.RodDurability;
					Snapshot.bRodBroken = RodSlot.bRodBroken;
					++Snapshot.Revision;
					PublishSnapshot();
					Result.bCommitted = true;
					Result.Error = ECatDomainCommandError::None;
				}
			}
		}
	}
	Result.Revision = Snapshot.Revision;
	TerminalCache.Add(Key, Result);
	return Result;
}

// 数量型库存授予预检流程：
// 1. 先从目录读取正式定义，并确认 RequestId、authority、定义类型和授予数量都成立；失败时不读取或补写库存格。
// 2. 已经缓存过同 RequestId 的授予结果时放行重放，让商店重试能拿回原回执而不是被当前容量误拦。
// 3. Owner 必须提供正式 InventoryComponent；Equipment 不再用旧投影数组回答库存容量，避免商店扣款前看错事实源。
// 4. 容量和堆叠预检统一交给 InventoryComponent；这里不扩容数组，保证 Validate 纯只读。
ECatDomainCommandError UCatEquipmentComponent::ValidateInventoryQuantityGrant(const FGuid RequestId,
	const FName DefinitionId, const int32 Quantity) const
{
	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	UCatEquipmentDefinition* Definition = Settings->FindRuntimeDefinition(DefinitionId);
	if (!RequestId.IsValid() || !GetOwner() || !GetOwner()->HasAuthority() || !Definition
		|| !Definition->bRunConsumable || Quantity <= 0)
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	const FString Key = MakeTerminalKey(TEXT("GrantInventoryQuantity"), RequestId);
	if (TerminalCache.Contains(Key))
	{
		return ECatDomainCommandError::None;
	}
	const UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent();
	if (OwnerInventory == nullptr)
	{
		return ECatDomainCommandError::DependencyUnavailable;
	}
	return OwnerInventory->ValidateResolvedInventoryDefinitionGrantFromAuthority(
		RequestId, Definition, Quantity);
}

// 数量型库存物品入库流程：
// 1. 先拒绝无效 RequestId，并用 RequestId、定义和数量签名保护终态重放；载荷漂移直接拒绝且不改库存。
// 2. 正式库存组件存在时先按配置补齐槽位，再把已解析定义交给 InventoryComponent 的统一发货事务。
// 3. 正式库存写入成功后只从 InventoryComponent 重建 Equipment 旧投影，并按新增定义修正钓鱼选择。
// 4. 投影同步失败只记录诊断，不回滚正式库存事实；后续刷新仍以 InventoryComponent 为准。
// 5. 没有正式库存组件时返回依赖错误；Equipment 不再作为随身库存的备用写入口。
FCatDomainCommandResult UCatEquipmentComponent::GrantInventoryQuantityFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const FName DefinitionId, const int32 Quantity)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (!RequestId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		Result.Revision = Snapshot.Revision;
		return Result;
	}
	const FString Key = MakeTerminalKey(TEXT("GrantInventoryQuantity"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("ExpectedRevision=%lld|Definition=%s|Quantity=%d"),
		ExpectedRevision, *DefinitionId.ToString(), Quantity);
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		const FString* CachedPayload = TerminalPayloadByKey.Find(Key);
		if (!CachedPayload || *CachedPayload != PayloadSignature)
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
			Result.Revision = Snapshot.Revision;
			return Result;
		}
		Result = *Cached;
		MarkCommandReplayed(Result);
		return Result;
	}
	UCatEquipmentDefinition* Definition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(DefinitionId);
	UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Definition || !Definition->bRunConsumable
		|| Quantity <= 0)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (Snapshot.Revision != ExpectedRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
	}
	else if (OwnerInventory == nullptr)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else
	{
		OwnerInventory->SetInventorySlotCountFromAuthority(GetConfiguredInventorySlotCapacity());
		const FCatDomainCommandResult InventoryGrant =
			OwnerInventory->GrantResolvedInventoryDefinitionFromAuthority(
				RequestId, OwnerInventory->GetInventoryRevision(), Definition, Quantity);
		Result.bCommitted = InventoryGrant.bCommitted;
		Result.Error = InventoryGrant.Error;
		if (InventoryGrant.bCommitted
			&& !RefreshInventoryProjectionFromInventoryComponentFromAuthority(Definition, DefinitionId))
		{
			UE_LOG(LogCatEquipment, Warning,
				TEXT("Event=equipment_inventory_projection_sync_failed Operation=GrantInventoryQuantity Owner=%s Request=%s Definition=%s InventoryRevision=%lld SnapshotRevision=%lld"),
				*GetNameSafe(GetOwner()), *RequestId.ToString(EGuidFormats::DigitsWithHyphens),
				*DefinitionId.ToString(), InventoryGrant.Revision, Snapshot.Revision);
		}
	}
	Result.Revision = Snapshot.Revision;
	TerminalCache.Add(Key, Result);
	TerminalPayloadByKey.Add(Key, PayloadSignature);
	return Result;
}

// 商店非数量物品入库预检流程：
// 1. 先按 RequestId 和定义 ID 查询既有终态载荷，合法重放放行，载荷漂移拒绝。
// 2. 再确认当前组件属于 authority 角色，并读取正式定义和单实例容量。
// 3. 正式库存组件是唯一容量裁决者；缺少它时直接返回依赖错误，不再回看 Equipment 旧投影。
// 4. 非数量物品仍按单件载荷交给 InventoryComponent 预演，避免商店和背包各维护一套容量规则。
ECatDomainCommandError UCatEquipmentComponent::ValidateEquipmentGrantFromAuthority(const FGuid RequestId,
	const FName DefinitionId) const
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid())
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	const FString Key = MakeTerminalKey(TEXT("GrantEquipment"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("Definition=%s"), *DefinitionId.ToString());
	if (const FString* CachedPayload = TerminalPayloadByKey.Find(Key))
	{
		return *CachedPayload == PayloadSignature ? ECatDomainCommandError::None
			: ECatDomainCommandError::InvalidPayload;
	}
	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	UCatEquipmentDefinition* Definition = Settings->FindRuntimeDefinition(DefinitionId);
	if (!Definition || Definition->bRunConsumable)
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	const UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent();
	if (OwnerInventory == nullptr)
	{
		return ECatDomainCommandError::DependencyUnavailable;
	}
	return OwnerInventory->ValidateResolvedInventoryDefinitionGrantFromAuthority(
		RequestId, Definition, 1);
}

// 商店非数量物品入库流程：
// 1. 先用 RequestId 和定义 ID 找终态缓存；合法重放只返回首次结果，不重复增加库存数量或推进 Revision。
// 2. 正式库存组件存在时先按配置补齐槽位，再把已解析定义交给 InventoryComponent 的统一发货事务。
// 3. 正式库存写入成功后刷新 Equipment 旧投影和钓鱼选择；投影失败只记诊断，库存事实不再反向回滚。
// 4. 没有正式库存组件时返回依赖错误；Equipment 不再维护第二套随身库存写入逻辑。
FCatDomainCommandResult UCatEquipmentComponent::GrantEquipmentFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const FName DefinitionId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (!RequestId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		Result.Revision = Snapshot.Revision;
		return Result;
	}
	const FString Key = MakeTerminalKey(TEXT("GrantEquipment"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("Definition=%s"), *DefinitionId.ToString());
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		const FString* CachedPayload = TerminalPayloadByKey.Find(Key);
		if (!CachedPayload || *CachedPayload != PayloadSignature)
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
			Result.Revision = Snapshot.Revision;
			return Result;
		}
		Result = *Cached;
		MarkCommandReplayed(Result);
		return Result;
	}

	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	UCatEquipmentDefinition* Definition = Settings->FindRuntimeDefinition(DefinitionId);
	UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Definition || Definition->bRunConsumable)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (Snapshot.Revision != ExpectedRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
	}
	else if (OwnerInventory == nullptr)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else
	{
		OwnerInventory->SetInventorySlotCountFromAuthority(GetConfiguredInventorySlotCapacity());
		const FCatDomainCommandResult InventoryGrant =
			OwnerInventory->GrantResolvedInventoryDefinitionFromAuthority(
				RequestId, OwnerInventory->GetInventoryRevision(), Definition, 1);
		Result.bCommitted = InventoryGrant.bCommitted;
		Result.Error = InventoryGrant.Error;
		if (InventoryGrant.bCommitted
			&& !RefreshInventoryProjectionFromInventoryComponentFromAuthority(Definition, DefinitionId))
		{
			UE_LOG(LogCatEquipment, Warning,
				TEXT("Event=equipment_inventory_projection_sync_failed Operation=GrantEquipment Owner=%s Request=%s Definition=%s InventoryRevision=%lld SnapshotRevision=%lld"),
				*GetNameSafe(GetOwner()), *RequestId.ToString(EGuidFormats::DigitsWithHyphens),
				*DefinitionId.ToString(), InventoryGrant.Revision, Snapshot.Revision);
		}
	}
	Result.Revision = Snapshot.Revision;
	TerminalCache.Add(Key, Result);
	TerminalPayloadByKey.Add(Key, PayloadSignature);
	return Result;
}

// 临时抄网补给流程：
// 1. 先确认玩家 Pawn、authority、配置开关和本 Character 生命周期去重标记，避免客户端或重复占有自动刷物品。
// 2. 再记录一次诊断请求并通过库存目录校验配置定义确实是抄网，配置错误只写日志，不改随身库存。
// 3. 如果正式库存已有任一完整抄网，就只修正缺失选择并发布必要快照，不因容量临时变小移除玩家已有物品。
// 4. 没有抄网时必须至少给基础竿、漂、饵和抄网留下四格；容量不足只记录拒绝，等容量恢复后允许再次尝试。
// 5. 最后只让 InventoryComponent 按正式库存 Revision 发货；没有正式库存时拒绝，不再写 Equipment 旧数组。
// 6. 正式库存发货成功后刷新旧投影并写本 Character 生命周期去重标记，投影同步失败只记日志，不回滚正式库存发货。
void UCatEquipmentComponent::GrantStarterScoopNetIfConfigured()
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	if (!Pawn || !Pawn->HasAuthority() || !Controller || !Controller->IsPlayerController()
		|| !Settings->bAutoGrantStarterScoopNet || bStarterScoopNetGrantHandled)
	{
		return;
	}

	const FGuid RequestId = FGuid::NewGuid();
	const FName DefinitionId = Settings->StarterScoopNetDefinitionId;
	// PossessedBy 期间 Controller->GetPawn() 尚可能指向旧身体；实际写入对象必须直接记录组件 Owner。
	const FString Context = FString::Printf(TEXT("World=%s Owner=%s LocalRole=%d Authority=true %s"),
		*GetPathNameSafe(GetWorld()), *GetNameSafe(Pawn), static_cast<int32>(Pawn->GetLocalRole()),
		*CatLogContext::BuildControllerFields(Controller));
	UE_LOG(LogCatEquipment, Log,
		TEXT("Event=equipment_starter_scoop_requested RequestId=%s Definition=%s Revision=%lld %s"),
		*RequestId.ToString(), *DefinitionId.ToString(), Snapshot.Revision, *Context);
	UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent();
	const UCatEquipmentDefinition* Definition = FindEquipmentDefinitionFromInventoryCatalog(DefinitionId);
	if (!Definition || Definition->Kind != ECatEquipmentKind::ScoopNet)
	{
		UE_LOG(LogCatEquipment, Warning,
			TEXT("Event=equipment_starter_scoop_rejected RequestId=%s Definition=%s Error=InvalidScoopDefinition Revision=%lld %s"),
			*RequestId.ToString(), *DefinitionId.ToString(), Snapshot.Revision, *Context);
		return;
	}
	if (OwnerInventory == nullptr)
	{
		UE_LOG(LogCatEquipment, Warning,
			TEXT("Event=equipment_starter_scoop_rejected RequestId=%s Definition=%s Error=%s Revision=%lld %s"),
			*RequestId.ToString(), *DefinitionId.ToString(),
			*UEnum::GetValueAsString(ECatDomainCommandError::DependencyUnavailable),
			Snapshot.Revision, *Context);
		return;
	}

	// 正式库存已经有抄网时只刷新旧投影和选择；不依赖旧 Snapshot 才能知道玩家是否已经持有这件物品。
	const int32 ExistingFormalSlotIndex = OwnerInventory->FindFirstInventorySlotIndexByDefinitionId(DefinitionId);
	const FCatInventoryEntry* ExistingFormalEntry =
		OwnerInventory->GetInventoryEntryAtSlot(ExistingFormalSlotIndex);
	if (ExistingFormalEntry != nullptr && ExistingFormalEntry->StackCount > 0)
	{
		const bool bProjectionSynced =
			RefreshInventoryProjectionFromInventoryComponentFromAuthority(Definition, DefinitionId);
		bStarterScoopNetGrantHandled = true;
		UE_LOG(LogCatEquipment, Log,
			TEXT("Event=equipment_starter_scoop_completed RequestId=%s Result=AlreadyOwnedFormal Definition=%s Slot=%d ProjectionSynced=%s ScoopNetItemInstanceId=%s Revision=%lld %s"),
			*RequestId.ToString(), *DefinitionId.ToString(), ExistingFormalSlotIndex,
			bProjectionSynced ? TEXT("true") : TEXT("false"),
			*Snapshot.ScoopNetItemInstanceId.ToString(), Snapshot.Revision, *Context);
		return;
	}

	constexpr int32 MinimumStarterScoopCapacity = 4;
	if (GetConfiguredInventorySlotCapacity() < MinimumStarterScoopCapacity)
	{
		UE_LOG(LogCatEquipment, Warning,
			TEXT("Event=equipment_starter_scoop_rejected RequestId=%s Definition=%s Error=%s RequiredCapacity=%d ConfiguredCapacity=%d Revision=%lld %s"),
			*RequestId.ToString(), *DefinitionId.ToString(),
			*UEnum::GetValueAsString(ECatDomainCommandError::CapacityExceeded),
			MinimumStarterScoopCapacity, GetConfiguredInventorySlotCapacity(), Snapshot.Revision, *Context);
		return;
	}

	FCatDomainCommandResult Grant;
	bool bProjectionSynced = true;
	OwnerInventory->SetInventorySlotCountFromAuthority(GetConfiguredInventorySlotCapacity());
	Grant = OwnerInventory->GrantInventoryDefinitionFromAuthority(
		RequestId, OwnerInventory->GetInventoryRevision(), DefinitionId, 1);
	if (Grant.bCommitted)
	{
		bProjectionSynced =
			RefreshInventoryProjectionFromInventoryComponentFromAuthority(Definition, DefinitionId);
		if (!bProjectionSynced)
		{
			UE_LOG(LogCatEquipment, Warning,
				TEXT("Event=equipment_starter_scoop_projection_sync_failed RequestId=%s Definition=%s InventoryRevision=%lld SnapshotRevision=%lld %s"),
				*RequestId.ToString(), *DefinitionId.ToString(), Grant.Revision, Snapshot.Revision, *Context);
		}
	}
	bStarterScoopNetGrantHandled = Grant.bCommitted;
	if (Grant.bCommitted)
	{
		UE_LOG(LogCatEquipment, Log,
			TEXT("Event=equipment_starter_scoop_completed RequestId=%s Result=Granted Definition=%s ScoopNetItemInstanceId=%s Quantity=1 ProjectionSynced=%s Revision=%lld %s"),
			*RequestId.ToString(), *DefinitionId.ToString(), *Snapshot.ScoopNetItemInstanceId.ToString(),
			bProjectionSynced ? TEXT("true") : TEXT("false"), Snapshot.Revision, *Context);
	}
	else
	{
		UE_LOG(LogCatEquipment, Warning,
			TEXT("Event=equipment_starter_scoop_rejected RequestId=%s Definition=%s Error=%s Revision=%lld %s"),
			*RequestId.ToString(), *DefinitionId.ToString(), *UEnum::GetValueAsString(Grant.Error),
			Snapshot.Revision, *Context);
	}
}

FCatInventoryItemUseResult UCatEquipmentComponent::Use(const FGuid RequestId, const int64 ExpectedRevision,
	const FGuid ItemInstanceId, const int32 Quantity, const int64 ExpectedInventoryRevision)
{
	FCatInventoryMutationScope InventoryMutation(ResolveOwnerInventoryComponent());
	const bool bWasDeferring = bDeferringSnapshotPublication;
	bDeferringSnapshotPublication = true;
	ON_SCOPE_EXIT
	{
		bDeferringSnapshotPublication = bWasDeferring;
		if (!bWasDeferring && bSnapshotPublicationPending)
		{
			bSnapshotPublicationPending = false;
			PublishSnapshot();
		}
	};
	// 物品使用流程：
	// 1. 先校验 authority、RequestId 和数量，再用实例载荷签名处理幂等重放，避免数量消耗品重复扣量。
	// 2. 正式库存存在时按实例 ID 回到 InventoryComponent 槽位，旧槽位只作为定义裁决的只读投影；外部提供的非 0 库存版本会继续传给正式库存入口复核。
	// 3. 部署型物品通过 InventoryComponent 的按实例 held 命令借出可见格，正式 UObject 仍由库存活动区保管；数量消耗物暂时由正式库存扣指定份数。
	// 4. 正式路径刷新旧投影失败时先通过 InventoryComponent 结构化归还入口把刚借出的实例放回可见库存，归还失败才退役活动记录并恢复 entries 与旧 Snapshot。
	// 5. ExpectedInventoryRevision 为 0 只表示旧调用方没有外部库存观察点；此时用服务器当前库存版本进入正式库存入口，不跳过库存事实层。
	// 6. 没有正式库存组件时返回依赖错误；Equipment 不再移出或扣除旧 Snapshot 数组里的物品。
	FCatInventoryItemUseResult Result;
	Result.RequestId = RequestId;
	Result.EquipmentRevision = Snapshot.Revision;
	if (!GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid() || !ItemInstanceId.IsValid()
		|| Quantity <= 0)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	const FString Key = MakeTerminalKey(TEXT("UseInventoryItem"), RequestId);
	const FString PayloadSignature = FString::Printf(
		TEXT("ExpectedRevision=%lld|ExpectedInventoryRevision=%lld|ItemInstance=%s|Quantity=%d"),
		ExpectedRevision, ExpectedInventoryRevision,
		*ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), Quantity);
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
		if (Completed.InventoryRevision == 0)
		{
			if (const UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent())
			{
				Completed.InventoryRevision = OwnerInventory->GetInventoryRevision();
			}
		}
		InventoryItemUseTerminalCache.Add(Key, Completed);
		TerminalPayloadByKey.Add(Key, PayloadSignature);
		return Completed;
	};
	if (Snapshot.Revision != ExpectedRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
		return Finish(Result);
	}
	if (const FCatInventoryItemUseRecord* ExistingRecord = FindInventoryItemUseRecord(ItemInstanceId);
		ExistingRecord && !ExistingRecord->bReleased)
	{
		Result.Item = ExistingRecord->Item;
		Result.Error = ECatDomainCommandError::InvalidPhase;
		return Finish(Result);
	}
	UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent();
	if (OwnerInventory == nullptr)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	Result.InventoryRevision = OwnerInventory->GetInventoryRevision();
	const int64 EffectiveExpectedInventoryRevision = ExpectedInventoryRevision != 0
		? ExpectedInventoryRevision : Result.InventoryRevision;
	if (Result.InventoryRevision != EffectiveExpectedInventoryRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
		return Finish(Result);
	}
	// 正式角色的 Use 以 InventoryComponent 为事实源；Equipment 在这里临时投影旧载荷，是为了继续复用现有定义裁决。
	const int32 FormalSlotIndex = OwnerInventory->FindInventorySlotIndexFromInstanceId(ItemInstanceId);
	const FCatInventoryEntry* FormalEntry = OwnerInventory->GetInventoryEntryAtSlot(FormalSlotIndex);
	FCatRunInventorySlot SourceItem;
	if (FormalEntry == nullptr || !BuildLegacyRunInventorySlotFromFormalEntry(*FormalEntry, SourceItem))
	{
		Result.Error = ECatDomainCommandError::NotFound;
		return Finish(Result);
	}

	const UCatEquipmentDefinition* Definition =
		Cast<UCatEquipmentDefinition>(FormalEntry->Instance->GetItemDefinition());
	if (Definition == nullptr)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Finish(Result);
	}

	Result.Item = SourceItem;
	const ECatDomainCommandError DefinitionUseError = Definition->Use(SourceItem, Quantity);
	if (DefinitionUseError != ECatDomainCommandError::None)
	{
		Result.Error = DefinitionUseError;
		return Finish(Result);
	}
	if (Definition->ConsumesInventoryQuantityOnUse())
	{
		const TArray<FCatInventoryEntry> SavedEntries = OwnerInventory->GetInventoryEntries();
		const FCatEquipmentLoadoutSnapshot SavedSnapshot = Snapshot;
		if (!OwnerInventory->ConsumeItemAtSlot(FormalSlotIndex, Quantity)
			|| !RefreshInventoryProjectionFromInventoryComponentFromAuthority())
		{
			OwnerInventory->ReplaceInventoryEntriesFromAuthority(
				SavedEntries, GetConfiguredInventorySlotCapacity());
			Snapshot = SavedSnapshot;
			Result.InventoryRevision = OwnerInventory->GetInventoryRevision();
			Result.Error = ECatDomainCommandError::DependencyUnavailable;
			return Finish(Result);
		}

		Result.Item = SourceItem;
		Result.Item.Quantity = Quantity;
		Result.InventoryRevision = OwnerInventory->GetInventoryRevision();
		Result.EquipmentRevision = Snapshot.Revision;
		Result.bCommitted = true;
		Result.Error = ECatDomainCommandError::None;
		return Finish(Result);
	}
	if (!Definition->KeepsInventoryInstanceWhileUsed())
	{
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		return Finish(Result);
	}

	const TArray<FCatInventoryEntry> SavedEntries = OwnerInventory->GetInventoryEntries();
	const FCatEquipmentLoadoutSnapshot SavedSnapshot = Snapshot;
	FCatInventoryEntry HeldEntry;
	const FCatDomainCommandResult HoldResult = OwnerInventory->HoldInventoryItemInstanceFromAuthority(
		RequestId, EffectiveExpectedInventoryRevision, SourceItem.ItemInstanceId, HeldEntry);
	if (!HoldResult.bCommitted || HoldResult.Error != ECatDomainCommandError::None)
	{
		Result.InventoryRevision = HoldResult.Revision;
		Result.Error = HoldResult.Error;
		return Finish(Result);
	}

	FCatInventoryItemUseRecord Record;
	Record.ItemInstanceId = SourceItem.ItemInstanceId;
	Record.Item = SourceItem;
	InventoryItemUseRecords.Add(SourceItem.ItemInstanceId, Record);
	if (!RefreshInventoryProjectionFromInventoryComponentFromAuthority())
	{
		InventoryItemUseRecords.Remove(SourceItem.ItemInstanceId);
		FCatInventoryEntry ReturnedEntry;
		const FCatDomainCommandResult ReturnResult = OwnerInventory->ReturnHeldInventoryItemInstanceFromAuthority(
			RequestId, SourceItem.ItemInstanceId, GetConfiguredInventorySlotCapacity(), 0, ReturnedEntry);
		if (!ReturnResult.bCommitted || ReturnResult.Error != ECatDomainCommandError::None)
		{
			OwnerInventory->RetireHeldInventoryEntryFromAuthority(SourceItem.ItemInstanceId);
			OwnerInventory->ReplaceInventoryEntriesFromAuthority(
				SavedEntries, GetConfiguredInventorySlotCapacity());
		}
		Snapshot = SavedSnapshot;
		Result.InventoryRevision = OwnerInventory->GetInventoryRevision();
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	if (Definition->Kind == ECatEquipmentKind::Rod)
	{
		// 部署鱼竿已离开正式库存格，但仍是钓鱼选择指向的活动实例；耐久用小容差过滤浮点微差，避免同一次 Use 后多发布一版旧快照。
		const FName PreviousRodDefinitionId = Snapshot.RodDefinitionId;
		const FGuid PreviousRodItemInstanceId = Snapshot.RodItemInstanceId;
		const double PreviousRodDurability = Snapshot.RodDurability;
		const bool bPreviousRodBroken = Snapshot.bRodBroken;
		Snapshot.RodDefinitionId = SourceItem.DefinitionId;
		Snapshot.RodItemInstanceId = SourceItem.ItemInstanceId;
		Snapshot.RodDurability = SourceItem.RodDurability;
		Snapshot.bRodBroken = SourceItem.bRodBroken;
		const bool bRodSelectionChanged = PreviousRodDefinitionId != Snapshot.RodDefinitionId
			|| PreviousRodItemInstanceId != Snapshot.RodItemInstanceId
			|| !FMath::IsNearlyEqual(PreviousRodDurability, Snapshot.RodDurability, KINDA_SMALL_NUMBER)
			|| bPreviousRodBroken != Snapshot.bRodBroken;
		if (bRodSelectionChanged)
		{
			++Snapshot.Revision;
			PublishSnapshot();
		}
	}
	InventoryItemUseRecords.FindChecked(SourceItem.ItemInstanceId).UseRevision = Snapshot.Revision;
	Result.Item = SourceItem;
	Result.InventoryRevision = OwnerInventory->GetInventoryRevision();
	Result.EquipmentRevision = Snapshot.Revision;
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	return Finish(Result);
}

bool UCatEquipmentComponent::TryReplayInventoryItemUseTerminal(const FGuid RequestId, const int64 ExpectedRevision,
	const FGuid ItemInstanceId, const int32 Quantity, FCatInventoryItemUseResult& OutResult,
	const int64 ExpectedInventoryRevision) const
{
	// 库存 Use 重放查询流程：
	// 1. 先复原 Use 使用的终态键和载荷签名，ExpectedInventoryRevision 也参与签名，避免同一 RequestId 被换成另一份库存观察点。
	// 2. 查询不读取当前库存格或定义，避免成功扣除后的空格阻断二段提交。
	// 3. 没有缓存返回 false，调用方继续执行首次提交 preflight；载荷漂移返回 true+InvalidPayload，阻止同 RequestId 改目标。
	// 4. 命中缓存时返回 MarkInventoryItemUseReplayed 后的结果，让协调器按首次成功或失败决定是否补放后续领域提交。
	OutResult = FCatInventoryItemUseResult();
	OutResult.RequestId = RequestId;
	OutResult.EquipmentRevision = Snapshot.Revision;
	if (const UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent())
	{
		OutResult.InventoryRevision = OwnerInventory->GetInventoryRevision();
	}
	if (!RequestId.IsValid() || !ItemInstanceId.IsValid() || Quantity <= 0)
	{
		return false;
	}
	const FString Key = MakeTerminalKey(TEXT("UseInventoryItem"), RequestId);
	const FString PayloadSignature = FString::Printf(
		TEXT("ExpectedRevision=%lld|ExpectedInventoryRevision=%lld|ItemInstance=%s|Quantity=%d"),
		ExpectedRevision, ExpectedInventoryRevision,
		*ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), Quantity);
	const FCatInventoryItemUseResult* Cached = InventoryItemUseTerminalCache.Find(Key);
	if (!Cached)
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

FCatInventoryItemUseResult UCatEquipmentComponent::UnUse(const FGuid RequestId, const FGuid ItemInstanceId)
{
	// 旧签名只指定实例；-1 保留在通道的原始请求签名中，首次执行由服务器解析当前版本。
	// 转移通道先缓存终态再发布，重放和发布回调重入不会再次把实例放回库存。
	FCatInventoryItemUseResult Result;
	Result.RequestId = RequestId;
	Result.EquipmentRevision = Snapshot.Revision;
	if (!GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid() || !ItemInstanceId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	UCatInventoryTransferService* Transfers = GetWorld() ? GetWorld()->GetSubsystem<UCatInventoryTransferService>() : nullptr;
	if (!Transfers)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	FCatInventoryTransferRequest Request;
	Request.RequestId = RequestId;
	Request.Initiator = GetOwner();
	Request.Source.Host = this;
	Request.Source.Channel = TEXT("ActiveUse");
	Request.Source.EntryId = ItemInstanceId;
	Request.Target.Host = this;
	Request.Target.Channel = TEXT("Stored");
	Request.ExpectedSourceRevision = -1;
	Request.ExpectedTargetRevision = -1;
	Request.SourceSlotIndex = 0;
	Request.Quantity = 1;
	Request.ExpectedSourceItemId = ItemInstanceId;
	const FCatInventoryTransferResult Transfer = Transfers->TransferFromAuthority(Request);
	Result.Error = Transfer.Error;
	Result.bCommitted = Transfer.bCommitted;
	Result.EquipmentRevision = Transfer.TargetRevision;
	if (const UCatInventoryComponent* Inventory = ResolveOwnerInventoryComponent())
		Result.InventoryRevision = Inventory->GetInventoryRevision();
	Result.Item = Transfer.Item;
	return Result;
}

FCatFishingUseReservationResult UCatEquipmentComponent::BeginFishingUse(const FGuid FishingSessionId,
	const FGuid RodItemInstanceId, const FGuid BaitItemInstanceId, const FGuid FloatItemInstanceId,
	const FName RodDefinitionId, const FName BaitDefinitionId, const FName FloatDefinitionId,
	const int64 ExpectedRevision, UCatEquipmentComponent* RodEquipment,
	const int64 ExpectedRodEquipmentRevision)
{
	FCatInventoryMutationScope InventoryMutation(ResolveOwnerInventoryComponent());
	// 本组件协调用饵，RodEquipment 持有真实竿实例。两边全部预检，再单次扣饵并锁竿，最后才广播。
	if (!RodEquipment) RodEquipment = this;
	const auto Reject = [&](const ECatDomainCommandError Error, const TCHAR* Reason)
	{
		UE_LOG(LogCatEquipment, Warning,
			TEXT("Event=equipment_rod_session_rejected SessionId=%s RodItemInstanceId=%s Reason=%s Error=%s Revision=%lld ExpectedRevision=%lld RodEquipmentRevision=%lld ExpectedRodEquipmentRevision=%lld World=%s NetMode=%d Authority=%s LocalRole=%d Owner=%s RodOwner=%s"),
			*FishingSessionId.ToString(), *RodItemInstanceId.ToString(), Reason, *UEnum::GetValueAsString(Error),
			Snapshot.Revision, ExpectedRevision, IsValid(RodEquipment) ? RodEquipment->Snapshot.Revision : -1,
			ExpectedRodEquipmentRevision, *GetNameSafe(GetWorld()),
			static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone),
			GetOwner() && GetOwner()->HasAuthority() ? TEXT("true") : TEXT("false"),
			GetOwner() ? static_cast<int32>(GetOwner()->GetLocalRole()) : 0, *GetNameSafe(GetOwner()),
			*GetNameSafe(IsValid(RodEquipment) ? RodEquipment->GetOwner() : nullptr));
		return MakeFishingUseReservationResult(FishingSessionId, Error, false);
	};
	if (!GetOwner() || !GetOwner()->HasAuthority() || bEndingPlay || !IsValid(RodEquipment)
		|| !RodEquipment->GetOwner() || !RodEquipment->GetOwner()->HasAuthority()
		|| RodEquipment->bEndingPlay || !GetWorld() || RodEquipment->GetWorld() != GetWorld())
	{
		return Reject(ECatDomainCommandError::DependencyUnavailable, TEXT("AuthorityOrRodOwnerUnavailable"));
	}
	if (const FCatFishingUseRecord* ExistingRecord = FindFishingUseRecord(FishingSessionId))
	{
		if (ExistingRecord->RodEquipment != RodEquipment || ExistingRecord->RodItemInstanceId != RodItemInstanceId
			|| ExistingRecord->RodDefinitionId != RodDefinitionId || ExistingRecord->BaitItemInstanceId != BaitItemInstanceId
			|| ExistingRecord->FloatItemInstanceId != FloatItemInstanceId || ExistingRecord->BaitDefinitionId != BaitDefinitionId
			|| ExistingRecord->FloatDefinitionId != FloatDefinitionId)
		{
			return Reject(ECatDomainCommandError::InvalidPayload, TEXT("SessionPayloadConflict"));
		}
		const bool bReserved = ExistingRecord->bBaitQuantityReserved && !ExistingRecord->bBaitCommitted
			&& !ExistingRecord->bReleased;
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::AlreadyResolved, bReserved,
			ExistingRecord);
	}
	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	UCatEquipmentDefinition* Rod = Settings->FindRuntimeDefinition(RodDefinitionId);
	UCatEquipmentDefinition* Bait = Settings->FindRuntimeDefinition(BaitDefinitionId);
	UCatEquipmentDefinition* Float = Settings->FindRuntimeDefinition(FloatDefinitionId);
	if (!FishingSessionId.IsValid() || !RodItemInstanceId.IsValid() || !BaitItemInstanceId.IsValid()
		|| !FloatItemInstanceId.IsValid() || RodDefinitionId.IsNone()
		|| BaitDefinitionId.IsNone() || FloatDefinitionId.IsNone() || !Rod || !Bait || !Float
		|| Rod->Kind != ECatEquipmentKind::Rod || Bait->Kind != ECatEquipmentKind::Bait
		|| Float->Kind != ECatEquipmentKind::Float || Rod->bRunConsumable || !Bait->bRunConsumable
		|| Float->bRunConsumable || !Rod->KeepsInventoryInstanceWhileUsed()
		|| ExpectedRevision < 0 || ExpectedRodEquipmentRevision < -1)
	{
		return Reject(ECatDomainCommandError::InvalidPayload, TEXT("InvalidPayload"));
	}
	if (Snapshot.Revision != ExpectedRevision || (ExpectedRodEquipmentRevision >= 0
		&& RodEquipment->Snapshot.Revision != ExpectedRodEquipmentRevision))
	{
		return Reject(ECatDomainCommandError::RevisionConflict, TEXT("RevisionConflict"));
	}
	if (Snapshot.BaitDefinitionId != BaitDefinitionId || Snapshot.BaitItemInstanceId != BaitItemInstanceId
		|| Snapshot.FloatDefinitionId != FloatDefinitionId || Snapshot.FloatItemInstanceId != FloatItemInstanceId)
	{
		return Reject(ECatDomainCommandError::InvalidPayload, TEXT("BaitOrFloatSelectionMismatch"));
	}
	FCatInventoryItemUseRecord* RodUseRecord = RodEquipment->FindInventoryItemUseRecord(RodItemInstanceId);
	FCatRunInventorySlot FormalRodSlot, FormalBaitSlot, FormalFloatSlot;
	if (!RodEquipment->TryBuildHeldInventoryUseSlot(RodItemInstanceId, FormalRodSlot))
		return Reject(ECatDomainCommandError::NotFound, TEXT("FormalHeldRodUnavailable"));
	if (RodUseRecord) RodUseRecord->Item = FormalRodSlot;
	const FCatRunInventorySlot* BaitSlot = TryResolveSelectionInventorySlot(BaitDefinitionId, BaitItemInstanceId, FormalBaitSlot) ? &FormalBaitSlot : nullptr;
	const FCatRunInventorySlot* FloatSlot = TryResolveSelectionInventorySlot(FloatDefinitionId, FloatItemInstanceId, FormalFloatSlot) ? &FormalFloatSlot : nullptr;
	if (!RodUseRecord || RodUseRecord->bReleased || RodUseRecord->ItemInstanceId != RodItemInstanceId
		|| RodUseRecord->Item.ItemInstanceId != RodItemInstanceId || RodUseRecord->Item.DefinitionId != RodDefinitionId
		|| RodUseRecord->Item.Quantity != 1 || RodEquipment->FindInventorySlotByInstanceId(RodItemInstanceId)
		|| !BaitSlot || BaitSlot->DefinitionId != BaitDefinitionId
		|| !FloatSlot || FloatSlot->DefinitionId != FloatDefinitionId)
	{
		return Reject(ECatDomainCommandError::NotFound, TEXT("ItemInstanceUnavailable"));
	}
	if (FloatSlot->Quantity <= 0 || BaitSlot->Quantity <= 0)
	{
		return Reject(ECatDomainCommandError::CapacityExceeded, TEXT("BaitOrFloatUnavailable"));
	}
	if (RodUseRecord->Item.bRodBroken || !FMath::IsFinite(RodUseRecord->Item.RodDurability)
		|| RodUseRecord->Item.RodDurability <= 0.0)
	{
		return Reject(ECatDomainCommandError::InvalidPhase, TEXT("RodBrokenOrInvalidDurability"));
	}
	if (RodUseRecord->BoundFishingSessionId.IsValid())
	{
		return Reject(ECatDomainCommandError::InvalidPhase, TEXT("RodAlreadyBound"));
	}
	FCatRunInventorySlot ReservedBaitItem;
	if (!RemoveInventoryItemQuantityFromInstance(BaitItemInstanceId, 1, ReservedBaitItem))
	{
		return Reject(ECatDomainCommandError::CapacityExceeded, TEXT("BaitReservationFailed"));
	}

	FCatFishingUseRecord Record;
	Record.RodEquipment = RodEquipment;
	Record.SessionId = FishingSessionId;
	Record.RodItemInstanceId = RodItemInstanceId;
	Record.RodDefinitionId = RodDefinitionId;
	Record.BaitItemInstanceId = BaitItemInstanceId;
	Record.FloatItemInstanceId = FloatItemInstanceId;
	Record.BaitDefinitionId = BaitDefinitionId;
	Record.FloatDefinitionId = FloatDefinitionId;
	Record.ReservedBaitDefinitionId = ReservedBaitItem.DefinitionId;
	Record.bBaitQuantityReserved = true;
	RodUseRecord->BoundFishingSessionId = FishingSessionId;
	RodUseRecord->FishingUseCoordinator = this;
	FishingUseRecords.Add(FishingSessionId, Record);
	++Snapshot.Revision;
	if (RodEquipment != this) ++RodEquipment->Snapshot.Revision;
	// 发布回调可以部署/预留另一根竿，扩容两份 TMap；回执与日志必须在广播前冻结。
	const FCatFishingUseReservationResult Result = MakeFishingUseReservationResult(
		FishingSessionId, ECatDomainCommandError::None, true);
	UE_LOG(LogCatEquipment, Log,
		TEXT("Event=equipment_rod_session_bound SessionId=%s RodItemInstanceId=%s Definition=%s Durability=%.3f Revision=%lld World=%s NetMode=%d Authority=true Owner=%s BaitDefinition=%s ReservedBaitItemInstanceId=%s BaitQuantityRemaining=%d SelectedBaitItemInstanceId=%s LocalRole=%d RodOwner=%s RodEquipmentRevision=%lld"),
		*FishingSessionId.ToString(), *RodItemInstanceId.ToString(), *RodDefinitionId.ToString(),
		RodUseRecord->Item.RodDurability, Snapshot.Revision, *GetNameSafe(GetWorld()),
		static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone), *GetNameSafe(GetOwner()),
		*BaitDefinitionId.ToString(), *BaitItemInstanceId.ToString(), GetInventoryItemQuantity(BaitDefinitionId),
		*Snapshot.BaitItemInstanceId.ToString(), GetOwner() ? static_cast<int32>(GetOwner()->GetLocalRole()) : 0,
		*GetNameSafe(RodEquipment->GetOwner()), RodEquipment->Snapshot.Revision);
	const TWeakObjectPtr<UCatEquipmentComponent> RodEquipmentToPublish = RodEquipment;
	PublishSnapshot();
	if (RodEquipmentToPublish.IsValid() && RodEquipmentToPublish.Get() != this) RodEquipmentToPublish->PublishSnapshot();
	return Result;
}

FCatFishingUseOperationResult UCatEquipmentComponent::CommitFishingBaitDeferred(const FGuid FishingSessionId)
{
	// 确认消耗鱼饵的流程：
	// 1. 先找到 Begin 阶段留下的记录；没有记录说明 Fishing 从未拿到装备使用权。
	// 2. 已释放或已提交的记录只返回终态，不允许重复处理同一份暂存饵。
	// 3. 只有该 Session 自己仍处于活动预留态才能提交，旧会话 tombstone 不会补消耗。
	// 4. Begin 已经把饵从库存移入记录并发布快照；这里只清掉暂存副本并标记已消耗。
	FCatFishingUseRecord* Record = FindFishingUseRecord(FishingSessionId);
	UCatEquipmentComponent* RodEquipment = Record ? Record->RodEquipment.Get() : nullptr;
	const auto Reject = [&](const ECatDomainCommandError Error, const TCHAR* Reason)
	{
		UE_LOG(LogCatEquipment, Warning,
			TEXT("Event=equipment_fishing_bait_commit_rejected SessionId=%s RodItemInstanceId=%s Reason=%s Error=%s Revision=%lld RodEquipmentRevision=%lld World=%s NetMode=%d Authority=%s LocalRole=%d Owner=%s RodOwner=%s"),
			*FishingSessionId.ToString(), Record ? *Record->RodItemInstanceId.ToString() : TEXT("None"),
			Reason, *UEnum::GetValueAsString(Error), Snapshot.Revision,
			RodEquipment ? RodEquipment->Snapshot.Revision : -1, *GetNameSafe(GetWorld()),
			static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone),
			GetOwner() && GetOwner()->HasAuthority() ? TEXT("true") : TEXT("false"),
			GetOwner() ? static_cast<int32>(GetOwner()->GetLocalRole()) : 0,
			*GetNameSafe(GetOwner()), *GetNameSafe(RodEquipment ? RodEquipment->GetOwner() : nullptr));
		return MakeFishingUseOperationResult(FishingSessionId, Error, false, Record);
	};
	if (!GetOwner() || !GetOwner()->HasAuthority() || bEndingPlay)
	{
		return Reject(ECatDomainCommandError::DependencyUnavailable, TEXT("NotAuthorityOrEndingPlay"));
	}
	if (!Record)
	{
		return Reject(ECatDomainCommandError::NotFound, TEXT("SessionMissing"));
	}
	if (Record->bReleased || Record->bBaitCommitted)
	{
		return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::AlreadyResolved, false, Record);
	}
	if (!IsFishingUseActive(FishingSessionId) || !FindFishingRodInstance(*Record))
	{
		return Reject(ECatDomainCommandError::InvalidPhase, TEXT("SessionOrRodLockUnavailable"));
	}
	if (Record->bBaitQuantityReserved)
	{
		if (Record->ReservedBaitDefinitionId.IsNone())
		{
			return Reject(ECatDomainCommandError::InvalidPhase, TEXT("ReservedBaitUnavailable"));
		}
		Record->ReservedBaitDefinitionId = NAME_None;
		Record->bBaitQuantityReserved = false;
	}
	Record->bBaitCommitted = true;
	UE_LOG(LogCatEquipment, Log,
		TEXT("Event=equipment_fishing_bait_committed SessionId=%s RodItemInstanceId=%s Revision=%lld RodEquipmentRevision=%lld World=%s NetMode=%d Authority=true LocalRole=%d Owner=%s RodOwner=%s"),
		*FishingSessionId.ToString(), *Record->RodItemInstanceId.ToString(), Snapshot.Revision,
		RodEquipment->Snapshot.Revision, *GetNameSafe(GetWorld()),
		static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone), static_cast<int32>(GetOwner()->GetLocalRole()),
		*GetNameSafe(GetOwner()), *GetNameSafe(RodEquipment->GetOwner()));
	return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::None, true, Record);
}

FCatFishingUseOperationResult UCatEquipmentComponent::ApplyFishingRodWear(const FGuid FishingSessionId,
	const int64 WearSequence, const double AbsoluteTotal)
{
	FCatFishingUseRecord* Record = FindFishingUseRecord(FishingSessionId);
	const auto Reject = [&](const ECatDomainCommandError Error, const TCHAR* Reason)
	{
		const UCatEquipmentComponent* RodEquipment = Record ? Record->RodEquipment.Get() : nullptr;
		UE_LOG(LogCatEquipment, Warning,
			TEXT("Event=equipment_rod_wear_rejected SessionId=%s RodItemInstanceId=%s WearSequence=%lld AbsoluteWear=%.3f Reason=%s Error=%s World=%s NetMode=%d Authority=%s Owner=%s LocalRole=%d Revision=%lld RodOwner=%s RodEquipmentRevision=%lld"),
			*FishingSessionId.ToString(), Record ? *Record->RodItemInstanceId.ToString() : TEXT("None"),
			WearSequence, AbsoluteTotal, Reason, *UEnum::GetValueAsString(Error), *GetNameSafe(GetWorld()),
			static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone), GetOwner() && GetOwner()->HasAuthority() ? TEXT("true") : TEXT("false"),
			*GetNameSafe(GetOwner()), GetOwner() ? static_cast<int32>(GetOwner()->GetLocalRole()) : 0,
			Snapshot.Revision, *GetNameSafe(RodEquipment ? RodEquipment->GetOwner() : nullptr),
			RodEquipment ? RodEquipment->Snapshot.Revision : -1);
		return MakeFishingUseOperationResult(FishingSessionId, Error, false, Record);
	};
	if (!GetOwner() || !GetOwner()->HasAuthority())
		return Reject(ECatDomainCommandError::DependencyUnavailable, TEXT("NotAuthority"));
	if (!FishingSessionId.IsValid() || WearSequence <= 0 || !FMath::IsFinite(AbsoluteTotal) || AbsoluteTotal < 0.0)
		return Reject(ECatDomainCommandError::InvalidPayload, TEXT("InvalidPayload"));
	if (!Record) return Reject(ECatDomainCommandError::NotFound, TEXT("SessionMissing"));
	if (Record->bReleased)
		return Reject(ECatDomainCommandError::AlreadyResolved, TEXT("SessionReleased"));
	if (WearSequence == Record->LastWearSequence && AbsoluteTotal == Record->AbsoluteRodWear)
		return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::AlreadyResolved, false, Record);
	if (Record->LastWearSequence == MAX_int64 || WearSequence != Record->LastWearSequence + 1
		|| AbsoluteTotal < Record->AbsoluteRodWear)
		return Reject(ECatDomainCommandError::InvalidPayload, TEXT("WearSequenceOrTotalConflict"));
	if (!Record->bBaitCommitted)
		return Reject(ECatDomainCommandError::InvalidPhase, TEXT("BaitNotCommitted"));
	UCatEquipmentComponent* RodEquipment = Record->RodEquipment.Get();
	FCatRunInventorySlot* RodItem = FindFishingRodInstance(*Record);
	if (!RodItem || !FMath::IsFinite(RodItem->RodDurability) || RodItem->RodDurability < 0.0)
		return Reject(ECatDomainCommandError::NotFound, TEXT("BoundRodUnavailable"));
	const double Before = RodItem->RodDurability;
	const bool bWasBroken = RodItem->bRodBroken;
	const double Delta = AbsoluteTotal - Record->AbsoluteRodWear;
	FCatRunInventorySlot FormalRodProjection;
	UCatEquipmentInventoryItemInstance* FormalRod = ResolveFishingRodFormalInstanceFromInventory(*Record, FormalRodProjection);
	if (!FormalRod) return Reject(ECatDomainCommandError::NotFound, TEXT("FormalRodUnavailable"));
	FormalRod->SetRodRuntimeStateFromAuthority(bWasBroken ? 0.0 : FMath::Max(0.0, Before - Delta),
		bWasBroken || Before - Delta <= 0.0);
	RodItem->RodDurability = bWasBroken ? 0.0 : FMath::Max(0.0, Before - Delta);
	RodItem->bRodBroken = RodItem->RodDurability <= 0.0;
	Record->LastWearSequence = WearSequence;
	Record->AbsoluteRodWear = AbsoluteTotal;
	// Snapshot 只投影当前选择；换选另一根竿不能让本会话磨损落在那根竿上。
	if (RodEquipment->Snapshot.RodItemInstanceId == Record->RodItemInstanceId)
	{
		RodEquipment->Snapshot.RodDurability = RodItem->RodDurability;
		RodEquipment->Snapshot.bRodBroken = RodItem->bRodBroken;
	}
	const double Remaining = RodItem->RodDurability;
	const bool bBroken = RodItem->bRodBroken;
	const bool bChanged = Before != Remaining || bWasBroken != bBroken;
	if (bChanged)
	{
		++RodEquipment->Snapshot.Revision;
	}
	const FCatFishingUseOperationResult Result = MakeFishingUseOperationResult(
		FishingSessionId, ECatDomainCommandError::None, true, Record);
	if (WearSequence == 1 || bWasBroken != bBroken
		|| FMath::FloorToDouble(Before / 5.0) != FMath::FloorToDouble(Remaining / 5.0))
	{
		UE_LOG(LogCatEquipment, Log,
			TEXT("Event=equipment_rod_wear_applied SessionId=%s RodItemInstanceId=%s WearSequence=%lld AbsoluteWear=%.3f Delta=%.3f DurabilityBefore=%.3f Durability=%.3f Broken=%s Revision=%lld World=%s NetMode=%d Authority=true Owner=%s LocalRole=%d RodOwner=%s RodEquipmentRevision=%lld"),
			*FishingSessionId.ToString(), *Record->RodItemInstanceId.ToString(), WearSequence, AbsoluteTotal,
			Delta, Before, Remaining, bBroken ? TEXT("true") : TEXT("false"), Snapshot.Revision,
			*GetNameSafe(GetWorld()), static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone),
			*GetNameSafe(GetOwner()), static_cast<int32>(GetOwner()->GetLocalRole()),
			*GetNameSafe(RodEquipment->GetOwner()), RodEquipment->Snapshot.Revision);
	}
	// 广播可重入其他会话的 Begin/Release，不能再解引用原 Record 或改写本次回执的版本。
	if (bChanged) RodEquipment->PublishSnapshot();
	return Result;
}

bool UCatEquipmentComponent::GetFishingRodDurability(const FGuid FishingSessionId,
	double& OutDurability, bool& OutBroken) const
{
	// 耐久读取流程：按会话短记录找到 Begin 冻结的鱼竿实例，再只从正式库存实例本体读取，避免换选后把另一根鱼竿当成旧会话结果。
	OutDurability = 0.0;
	OutBroken = false;
	const FCatFishingUseRecord* Record = FindFishingUseRecord(FishingSessionId);
	if (Record == nullptr)
	{
		return false;
	}

	if (ResolveOwnerInventoryComponent() == nullptr)
	{
		return false;
	}

	FCatRunInventorySlot RodItem;
	const UCatEquipmentInventoryItemInstance* FormalRodInstance =
		ResolveFishingRodFormalInstanceFromInventory(*Record, RodItem);
	if (FormalRodInstance == nullptr || !FMath::IsFinite(RodItem.RodDurability)
		|| RodItem.RodDurability < 0.0)
	{
		return false;
	}
	OutDurability = FormalRodInstance->GetRodDurability();
	OutBroken = FormalRodInstance->IsRodBroken() || OutDurability <= 0.0;
	return FMath::IsFinite(OutDurability) && OutDurability >= 0.0;
}

FCatFishingUseOperationResult UCatEquipmentComponent::ReleaseFishingUse(const FGuid FishingSessionId)
{
	FCatInventoryMutationScope InventoryMutation(ResolveOwnerInventoryComponent());
	// 只退协调者的未消耗鱼饵，并按会话+协调者解除竿主的锁。先关闭双方记录，再发布完整状态。
	FCatFishingUseRecord* Record = FindFishingUseRecord(FishingSessionId);
	UCatEquipmentComponent* RodEquipment = Record ? Record->RodEquipment.Get() : nullptr;
	const auto Reject = [&](const ECatDomainCommandError Error, const TCHAR* Reason)
	{
		UE_LOG(LogCatEquipment, Warning,
			TEXT("Event=equipment_rod_session_release_rejected SessionId=%s RodItemInstanceId=%s Reason=%s Error=%s Revision=%lld RodEquipmentRevision=%lld World=%s NetMode=%d Authority=%s LocalRole=%d Owner=%s RodOwner=%s"),
			*FishingSessionId.ToString(), Record ? *Record->RodItemInstanceId.ToString() : TEXT("None"),
			Reason, *UEnum::GetValueAsString(Error), Snapshot.Revision,
			RodEquipment ? RodEquipment->Snapshot.Revision : -1, *GetNameSafe(GetWorld()),
			static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone),
			GetOwner() && GetOwner()->HasAuthority() ? TEXT("true") : TEXT("false"),
			GetOwner() ? static_cast<int32>(GetOwner()->GetLocalRole()) : 0,
			*GetNameSafe(GetOwner()), *GetNameSafe(RodEquipment ? RodEquipment->GetOwner() : nullptr));
		return MakeFishingUseOperationResult(FishingSessionId, Error, false, Record);
	};
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return Reject(ECatDomainCommandError::DependencyUnavailable, TEXT("NotAuthority"));
	}
	if (!Record)
	{
		return Reject(ECatDomainCommandError::NotFound, TEXT("SessionMissing"));
	}
	if (Record->bReleased)
	{
		return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::AlreadyResolved, false, Record);
	}
	if (!IsFishingUseActive(FishingSessionId))
	{
		return Reject(ECatDomainCommandError::InvalidPhase, TEXT("SessionInactive"));
	}
	const bool bRodOwnerAvailable = RodEquipment && RodEquipment->GetOwner()
		&& RodEquipment->GetOwner()->HasAuthority() && RodEquipment->GetWorld() == GetWorld();
	FCatInventoryItemUseRecord* RodUse = bRodOwnerAvailable
		? RodEquipment->FindInventoryItemUseRecord(Record->RodItemInstanceId) : nullptr;
	if (bRodOwnerAvailable && (!RodUse || RodUse->bReleased || RodUse->BoundFishingSessionId != FishingSessionId
		|| RodUse->FishingUseCoordinator.Get() != this))
	{
		return Reject(ECatDomainCommandError::InvalidPhase, TEXT("RodSessionLockMismatch"));
	}
	bool bInventoryChanged = false;
	if (Record->bBaitQuantityReserved && !Record->bBaitCommitted)
	{
		const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
		const UCatEquipmentDefinition* Bait = Settings
			? Settings->FindRuntimeDefinition(Record->ReservedBaitDefinitionId) : nullptr;
		if (!Bait || Bait->Kind != ECatEquipmentKind::Bait || !Bait->bRunConsumable
			|| GetInventoryStackLimit(*Bait) <= 0)
		{
			return Reject(ECatDomainCommandError::DependencyUnavailable, TEXT("ReservedBaitDefinitionUnavailable"));
		}
		const FName RestoredDefinitionId = Record->ReservedBaitDefinitionId;
		if (!AddInventoryItemQuantity(*Bait, RestoredDefinitionId, 1))
			return Reject(ECatDomainCommandError::DependencyUnavailable, TEXT("ReservedBaitReturnFailed"));
		Record->ReservedBaitDefinitionId = NAME_None;
		Record->bBaitQuantityReserved = false;
		if (Snapshot.BaitDefinitionId == RestoredDefinitionId || Snapshot.BaitDefinitionId.IsNone()
			|| GetInventoryItemQuantity(Snapshot.BaitDefinitionId) <= 0)
		{
			const FCatRunInventorySlot* RestoredSlot = FindFirstInventorySlotByDefinition(RestoredDefinitionId);
			Snapshot.BaitDefinitionId = RestoredDefinitionId;
			Snapshot.BaitItemInstanceId = RestoredSlot ? RestoredSlot->ItemInstanceId : FGuid();
		}
		bInventoryChanged = true;
	}
	Record->bReleased = true;
	if (RodUse)
	{
		RodUse->BoundFishingSessionId.Invalidate();
		RodUse->FishingUseCoordinator.Reset();
	}
	if (bInventoryChanged || (RodUse && RodEquipment == this)) ++Snapshot.Revision;
	if (RodUse && RodEquipment != this) ++RodEquipment->Snapshot.Revision;
	const FCatFishingUseOperationResult Result = MakeFishingUseOperationResult(
		FishingSessionId, ECatDomainCommandError::None, true, Record);
	double RemainingDurability = 0.0;
	bool bRodBroken = false;
	const bool bRodAvailable = GetFishingRodDurability(FishingSessionId, RemainingDurability, bRodBroken);
	UE_LOG(LogCatEquipment, Log,
		TEXT("Event=equipment_rod_session_released SessionId=%s RodItemInstanceId=%s WearSequence=%lld AbsoluteWear=%.3f Durability=%.3f Broken=%s RodAvailable=%s World=%s NetMode=%d Authority=%s Owner=%s LocalRole=%d Revision=%lld RodOwner=%s RodEquipmentRevision=%lld BaitRestored=%s RodLockReleased=%s"),
		*FishingSessionId.ToString(), *Record->RodItemInstanceId.ToString(), Record->LastWearSequence,
		Record->AbsoluteRodWear, RemainingDurability, bRodBroken ? TEXT("true") : TEXT("false"),
		bRodAvailable ? TEXT("true") : TEXT("false"), *GetNameSafe(GetWorld()), static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone),
		GetOwner() && GetOwner()->HasAuthority() ? TEXT("true") : TEXT("false"), *GetNameSafe(GetOwner()),
		static_cast<int32>(GetOwner()->GetLocalRole()), Snapshot.Revision,
		*GetNameSafe(RodEquipment ? RodEquipment->GetOwner() : nullptr),
		RodEquipment ? RodEquipment->Snapshot.Revision : -1,
		bInventoryChanged ? TEXT("true") : TEXT("false"), RodUse ? TEXT("true") : TEXT("false"));
	if (!bRodOwnerAvailable)
	{
		UE_LOG(LogCatEquipment, Warning,
			TEXT("Event=equipment_rod_session_owner_unavailable SessionId=%s RodItemInstanceId=%s Result=CoordinatorReleased BaitRestored=%s Revision=%lld World=%s NetMode=%d Authority=true LocalRole=%d Owner=%s RodOwner=%s"),
			*FishingSessionId.ToString(), *Record->RodItemInstanceId.ToString(), bInventoryChanged ? TEXT("true") : TEXT("false"),
			Snapshot.Revision, *GetNameSafe(GetWorld()), static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone),
			static_cast<int32>(GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()),
			*GetNameSafe(RodEquipment ? RodEquipment->GetOwner() : nullptr));
	}
	const bool bPublishCaller = bInventoryChanged || (RodUse && RodEquipment == this);
	const TWeakObjectPtr<UCatEquipmentComponent> RodEquipmentToPublish = RodUse ? RodEquipment : nullptr;
	if (bPublishCaller) PublishSnapshot();
	if (RodEquipmentToPublish.IsValid() && RodEquipmentToPublish.Get() != this) RodEquipmentToPublish->PublishSnapshot();
	return Result;
}

bool UCatEquipmentComponent::HasActiveFishingUse() const
{
	for (const TPair<FGuid, FCatFishingUseRecord>& Pair : FishingUseRecords)
	{
		if (Pair.Key.IsValid() && !Pair.Value.bReleased) return true;
	}
	for (const TPair<FGuid, FCatInventoryItemUseRecord>& Pair : InventoryItemUseRecords)
	{
		if (!Pair.Value.bReleased && Pair.Value.BoundFishingSessionId.IsValid()) return true;
	}
	return false;
}

bool UCatEquipmentComponent::IsFishingUseActive(const FGuid FishingSessionId) const
{
	const FCatFishingUseRecord* Record = FindFishingUseRecord(FishingSessionId);
	return FishingSessionId.IsValid() && Record && !Record->bReleased;
}

// 营地修竿提交流程：
// 1. 先拒绝正在 Use 或 Fishing Use 的物品，再按 RequestId 返回已缓存终态，避免重放时再次扣材料。
// 2. 读取当前鱼竿、浮木定义和 Owner 正式库存；没有正式库存时直接拒绝，避免维修继续写旧 InventorySlots。
// 3. 校验营地、authority、Revision、鱼竿/浮木定义和材料数量；任一失败都不改库存或旧投影。
// 4. 正式路径先解析当前选择的鱼竿实例，再扣一份浮木并写回同一实例的耐久和断竿状态。
// 5. 投影刷新失败会恢复浮木 entry、鱼竿实例和旧 Snapshot；旧 Snapshot 只作为 UI/存档读模型接收结果。
FCatDomainCommandResult UCatEquipmentComponent::RepairRodAtCamp(const FGuid RequestId, const int64 ExpectedRevision,
	const bool bAtCamp)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (HasActiveFishingUse() || HasActiveInventoryItemUse())
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
		Result.Revision = Snapshot.Revision;
		return Result;
	}
	const FString Key = MakeTerminalKey(TEXT("RepairRod"), RequestId);
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		Result = *Cached;
		MarkCommandReplayed(Result);
		return Result;
	}
	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	UCatEquipmentDefinition* Rod = Settings->FindRuntimeDefinition(Snapshot.RodDefinitionId);
	UCatEquipmentDefinition* Driftwood = Settings->FindRuntimeDefinition(Settings->DriftwoodDefinitionId);
	UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent();
	const int32 FormalDriftwoodSlotIndex = OwnerInventory != nullptr
		? OwnerInventory->FindFirstInventorySlotIndexByDefinitionId(Settings->DriftwoodDefinitionId) : INDEX_NONE;
	const FCatInventoryEntry* FormalDriftwoodEntry = OwnerInventory != nullptr
		? OwnerInventory->GetInventoryEntryAtSlot(FormalDriftwoodSlotIndex) : nullptr;
	const UCatInventoryItemInstance* FormalDriftwoodInstance =
		FormalDriftwoodEntry != nullptr ? FormalDriftwoodEntry->Instance.Get() : nullptr;
	const UCatEquipmentDefinition* FormalDriftwoodDefinition = FormalDriftwoodInstance != nullptr
		? Cast<UCatEquipmentDefinition>(FormalDriftwoodInstance->GetItemDefinition()) : nullptr;
	// 浮木在修竿里是领域命令材料，不是玩家对物品本身执行 Use；因此只要求它是正式库存里的 Driftwood，实际扣量交给 InventoryComponent。
	if (!bAtCamp || !GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid() || !Rod || !Driftwood
		|| OwnerInventory == nullptr || FormalDriftwoodEntry == nullptr || FormalDriftwoodInstance == nullptr
		|| FormalDriftwoodDefinition == nullptr
		|| FormalDriftwoodInstance->GetItemDefinitionId() != Settings->DriftwoodDefinitionId
		|| FormalDriftwoodDefinition->Kind != ECatEquipmentKind::Driftwood
		|| !FormalDriftwoodDefinition->IsRuntimeDefinitionReady()
		|| Driftwood->Kind != ECatEquipmentKind::Driftwood || FormalDriftwoodEntry->StackCount <= 0)
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
	}
	else if (Snapshot.Revision != ExpectedRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
	}
	else
	{
		FCatRunInventorySlot FormalRodSlot;
		UCatEquipmentInventoryItemInstance* FormalRodInstance =
			ResolveSelectedFormalRodInstanceFromInventory(*OwnerInventory, *Rod, FormalRodSlot);
		if (FormalRodInstance == nullptr)
		{
			Result.Error = ECatDomainCommandError::PolicyUndecided;
		}
		else
		{
			const TArray<FCatInventoryEntry> SavedEntries = OwnerInventory->GetInventoryEntries();
			const FCatEquipmentLoadoutSnapshot SavedSnapshot = Snapshot;
			const double SavedRodDurability = FormalRodInstance->GetRodDurability();
			const bool bSavedRodBroken = FormalRodInstance->IsRodBroken();
			if (OwnerInventory->ConsumeItemAtSlot(FormalDriftwoodSlotIndex, 1))
			{
				FormalRodInstance->SetRodRuntimeStateFromAuthority(Rod->MaximumRodDurability, false);
				if (RefreshInventoryProjectionFromInventoryComponentFromAuthority(Rod, FormalRodSlot.DefinitionId))
				{
					Result.bCommitted = true;
					Result.Error = ECatDomainCommandError::None;
				}
				else
				{
					FormalRodInstance->SetRodRuntimeStateFromAuthority(SavedRodDurability, bSavedRodBroken);
					OwnerInventory->ReplaceInventoryEntriesFromAuthority(
						SavedEntries, GetConfiguredInventorySlotCapacity());
					Snapshot = SavedSnapshot;
					Result.Error = ECatDomainCommandError::PolicyUndecided;
				}
			}
			else
			{
				Result.Error = ECatDomainCommandError::PolicyUndecided;
			}
		}
	}
	Result.Revision = Snapshot.Revision;
	TerminalCache.Add(Key, Result);
	return Result;
}

// Snapshot 复制回调流程：客户端只刷新只读表现；不会自动装备、补充普通饵数量或修复断竿。
void UCatEquipmentComponent::OnRep_Snapshot()
{
	OnSnapshotChanged.Broadcast();
}

// 库存容量读取流程：正式默认来自 InventorySettings，旧 EquipmentSettings 非默认值仅作为迁移期测试和诊断覆盖。
int32 UCatEquipmentComponent::GetConfiguredInventorySlotCapacity() const
{
	return ResolvePlayerInventorySlotCapacity();
}

// 正式随身库存解析流程：只从当前 Owner 上读取 InventoryComponent；Equipment 不创建或缓存库存组件，避免迁移期出现第二份背包归属。
UCatInventoryComponent* UCatEquipmentComponent::ResolveOwnerInventoryComponent() const
{
	AActor* Owner = GetOwner();
	return Owner != nullptr ? Owner->FindComponentByClass<UCatInventoryComponent>() : nullptr;
}

// 单格堆叠读取流程：定义资产统一回答有效上限；Equipment 不再重复解释项目默认堆叠配置。
int32 UCatEquipmentComponent::GetInventoryStackLimit(const UCatEquipmentDefinition& Definition) const
{
	return Definition.GetMaxStackCount();
}

// 库存格数组补齐流程：只把配置声明的可见格补成空格；不删除多出来的已有格，避免配置调小后静默吞物品。
void UCatEquipmentComponent::EnsureInventorySlotArray()
{
	const int32 SlotCapacity = GetConfiguredInventorySlotCapacity();
	if (Snapshot.InventorySlots.Num() < SlotCapacity)
	{
		Snapshot.InventorySlots.AddDefaulted(SlotCapacity - Snapshot.InventorySlots.Num());
	}
}

bool UCatEquipmentComponent::NormalizeInventorySlots()
{
	// 存量格修复流程：
	// 1. 只遍历当前旧随身库存格，不改活动 Use 记录和选择快照。
	// 2. 有内容的格子交给定义归一化，给旧数据补实例身份并补齐鱼竿状态。
	// 3. 空格清回默认值，避免残留实例 ID 让选择、导出或旧读者误以为还有投影物品。
	// 4. 返回是否真的改动过格子字段，让上层决定是否推进 Revision 和发布快照。
	bool bChanged = false;
	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	for (FCatRunInventorySlot& Slot : Snapshot.InventorySlots)
	{
		const FCatRunInventorySlot Before = Slot;
		const UCatEquipmentDefinition* Definition = Settings && CatRunInventorySlotOperations::IsInventorySlotOccupied(Slot)
			? Settings->FindRuntimeDefinition(Slot.DefinitionId) : nullptr;
		if (Definition)
		{
			CatRunInventorySlotOperations::NormalizeStoredItemSlot(Slot, *Definition);
		}
		else if (!CatRunInventorySlotOperations::IsInventorySlotOccupied(Slot))
		{
			Slot = FCatRunInventorySlot();
		}
		bChanged = bChanged || Slot.DefinitionId != Before.DefinitionId
			|| Slot.ItemInstanceId != Before.ItemInstanceId
			|| Slot.Quantity != Before.Quantity
			|| Slot.RodDurability != Before.RodDurability
			|| Slot.bRodBroken != Before.bRodBroken;
	}
	return bChanged;
}

// 兼容载荷到正式实例的建模流程：
// 1. 先按载荷槽位实例 ID 复用正式库存里已有对象，避免存档恢复或旧格式导入时让客户端看到全新的物品对象身份。
// 2. 没有可复用对象时按定义声明的库存实例类创建，并要求它仍是装备适配实例，防止普通实例吞掉鱼竿耐久。
// 3. 最后把定义、实例 ID、运行宿主和鱼竿状态写回实例，让正式库存和输入载荷表达同一份物品。
UCatEquipmentInventoryItemInstance* UCatEquipmentComponent::CreateOrUpdateFormalItemInstanceFromSlot(
	const FCatRunInventorySlot& Slot,
	UCatEquipmentDefinition& Definition,
	const TMap<FGuid, UCatEquipmentInventoryItemInstance*>& ExistingInstances)
{
	if (!CatRunInventorySlotOperations::IsInventorySlotOccupied(Slot) || !Slot.ItemInstanceId.IsValid())
	{
		return nullptr;
	}

	UCatEquipmentInventoryItemInstance* Instance = nullptr;
	if (UCatEquipmentInventoryItemInstance* const* ExistingInstance = ExistingInstances.Find(Slot.ItemInstanceId))
	{
		Instance = *ExistingInstance;
	}
	if (Instance == nullptr)
	{
		const TSubclassOf<UCatInventoryItemInstance> ResolvedInstanceClass =
			UCatInventoryItemDefinition::ResolveItemInstanceClass(&Definition);
		UClass* InstanceClass = ResolvedInstanceClass.Get();
		if (InstanceClass == nullptr
			|| !InstanceClass->IsChildOf(UCatEquipmentInventoryItemInstance::StaticClass()))
		{
			return nullptr;
		}

		Instance = NewObject<UCatEquipmentInventoryItemInstance>(GetOwner(), InstanceClass);
	}
	if (Instance == nullptr)
	{
		return nullptr;
	}

	Instance->SetItemDefinition(&Definition);
	Instance->SetItemInstanceIdFromAuthority(Slot.ItemInstanceId);
	Instance->SetRuntimeOwnerActor(GetOwner());
	Instance->SetRodRuntimeStateFromAuthority(Slot.RodDurability, Slot.bRodBroken);
	return Instance;
}

// 兼容载荷到正式库存的建模流程：
// 1. 先收集目标正式库存里已有的装备实例，后续按 ItemInstanceId 复用，保持正式库存对象身份稳定。
// 2. 再按输入载荷的格位顺序建立正式 entries，空格保留为空 entry，不改变 UI 和存档目前依赖的格位顺序。
// 3. 每个占用格必须解析到运行就绪定义、合法数量和唯一实例 ID；任一失败都会清空输出并让导入整体拒绝。
bool UCatEquipmentComponent::BuildFormalEntriesFromSnapshot(const FCatEquipmentLoadoutSnapshot& SourceSnapshot,
	UCatInventoryComponent& TargetInventory,
	TArray<FCatInventoryEntry>& OutEntries)
{
	OutEntries.Reset();
	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	if (Settings == nullptr)
	{
		return false;
	}

	TMap<FGuid, UCatEquipmentInventoryItemInstance*> ExistingInstances;
	for (const FCatInventoryEntry& ExistingEntry : TargetInventory.GetInventoryEntries())
	{
		UCatEquipmentInventoryItemInstance* ExistingInstance =
			Cast<UCatEquipmentInventoryItemInstance>(ExistingEntry.Instance);
		if (ExistingInstance != nullptr && ExistingInstance->GetItemInstanceId().IsValid())
		{
			ExistingInstances.Add(ExistingInstance->GetItemInstanceId(), ExistingInstance);
		}
	}

	TSet<FGuid> SeenItemInstanceIds;
	OutEntries.Reserve(SourceSnapshot.InventorySlots.Num());
	for (const FCatRunInventorySlot& Slot : SourceSnapshot.InventorySlots)
	{
		FCatInventoryEntry& Entry = OutEntries.AddDefaulted_GetRef();
		Entry = FCatInventoryEntry(&TargetInventory);
		if (!CatRunInventorySlotOperations::IsInventorySlotOccupied(Slot))
		{
			continue;
		}

		UCatEquipmentDefinition* Definition = Settings->FindRuntimeDefinition(Slot.DefinitionId);
		const int32 StackLimit = Definition != nullptr ? GetInventoryStackLimit(*Definition) : 0;
		if (Definition == nullptr
			|| !Definition->IsInventoryRuntimeDefinitionReady()
			|| Slot.Quantity <= 0
			|| Slot.Quantity > StackLimit
			|| !Slot.ItemInstanceId.IsValid()
			|| SeenItemInstanceIds.Contains(Slot.ItemInstanceId))
		{
			OutEntries.Reset();
			return false;
		}

		UCatEquipmentInventoryItemInstance* Instance =
			CreateOrUpdateFormalItemInstanceFromSlot(Slot, *Definition, ExistingInstances);
		if (Instance == nullptr)
		{
			OutEntries.Reset();
			return false;
		}

		SeenItemInstanceIds.Add(Slot.ItemInstanceId);
		Entry.Instance = Instance;
		Entry.StackCount = Slot.Quantity;
		Entry.LastObservedCount = Slot.Quantity;
		Entry.SlotOwnerComponent = &TargetInventory;
	}

	return true;
}

// 兼容载荷导入正式库存流程：
// 1. 只在 authority Owner 上执行；没有正式库存组件的兼容宿主直接跳过，后续只替换旧 Snapshot 读模型。
// 2. 先把指定保存载荷建模成正式 entries，再用 InventoryComponent 的整表替换入口提交，避免恢复路径留下半同步状态。
// 3. 普通 Equipment 发布不调用这里，防止钓具选择变化把旧投影反向写成背包事实。
bool UCatEquipmentComponent::ImportSnapshotInventoryToOwnerInventoryComponent(
	const FCatEquipmentLoadoutSnapshot& SourceSnapshot)
{
	AActor* Owner = GetOwner();
	if (Owner == nullptr || !Owner->HasAuthority())
	{
		return true;
	}

	UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent();
	if (OwnerInventory == nullptr)
	{
		return true;
	}

	TArray<FCatInventoryEntry> FormalEntries;
	return BuildFormalEntriesFromSnapshot(SourceSnapshot, *OwnerInventory, FormalEntries)
		&& OwnerInventory->ReplaceInventoryEntriesFromAuthority(FormalEntries, GetConfiguredInventorySlotCapacity());
}

// 公开投影刷新流程：
// 1. 公开入口只负责把 Owner 正式库存投影回旧格位，不声明新增物品，避免普通同步抢钓具选择。
// 2. 实际比较、版本推进和发布交给带参数入口，保证营地转移和普通投影刷新共用同一条旧投影规则。
bool UCatEquipmentComponent::RefreshInventoryProjectionFromInventoryComponentFromAuthority()
{
	return RefreshInventoryProjectionFromInventoryComponentFromAuthority(nullptr, NAME_None);
}

// 正式库存到旧投影刷新流程：
// 1. 只在 authority 上读取 Owner 的 InventoryComponent；客户端复制读模型不能反向生成服务器快照。
// 2. 先把正式库存条目投成旧 InventorySlots，并记录刷新前的钓具选择，用于判断是否需要发布新 Equipment Revision。
// 3. 调用方传入新增定义时才尝试自动选择；这样营地交换能保留旧体验，普通格位刷新不会无故抢当前选择。
// 4. 旧格位或选择有任一变化时才推进 Equipment Revision 并发布旧读模型；正式背包事实仍只来自 InventoryComponent。
bool UCatEquipmentComponent::RefreshInventoryProjectionFromInventoryComponentFromAuthority(
	const UCatEquipmentDefinition* GrantedDefinition, const FName GrantedDefinitionId)
{
	AActor* Owner = GetOwner();
	if (Owner == nullptr || !Owner->HasAuthority())
	{
		return false;
	}

	TArray<FCatRunInventorySlot> ProjectedSlots;
	if (!BuildSnapshotInventorySlotsFromOwnerInventoryComponent(ProjectedSlots))
	{
		return false;
	}

	const FName PreviousRodDefinitionId = Snapshot.RodDefinitionId;
	const FGuid PreviousRodItemInstanceId = Snapshot.RodItemInstanceId;
	const double PreviousRodDurability = Snapshot.RodDurability;
	const bool bPreviousRodBroken = Snapshot.bRodBroken;
	const FName PreviousBaitDefinitionId = Snapshot.BaitDefinitionId;
	const FGuid PreviousBaitItemInstanceId = Snapshot.BaitItemInstanceId;
	const FName PreviousFloatDefinitionId = Snapshot.FloatDefinitionId;
	const FGuid PreviousFloatItemInstanceId = Snapshot.FloatItemInstanceId;
	const FName PreviousScoopNetDefinitionId = Snapshot.ScoopNetDefinitionId;
	const FGuid PreviousScoopNetItemInstanceId = Snapshot.ScoopNetItemInstanceId;

	const bool bInventoryProjectionChanged =
		!AreLegacyInventorySlotArraysEquivalent(Snapshot.InventorySlots, ProjectedSlots);
	if (bInventoryProjectionChanged)
	{
		Snapshot.InventorySlots = MoveTemp(ProjectedSlots);
	}

	if (GrantedDefinition != nullptr && !GrantedDefinitionId.IsNone())
	{
		AutoSelectGrantedInventoryItem(*GrantedDefinition, GrantedDefinitionId);
	}

	// 鱼竿耐久是双精度运行值；这里用引擎小容差过滤浮点微差，避免没有真实选择变化时空转推进 Equipment Revision。
	const bool bSelectionChanged = PreviousRodDefinitionId != Snapshot.RodDefinitionId
		|| PreviousRodItemInstanceId != Snapshot.RodItemInstanceId
		|| !FMath::IsNearlyEqual(PreviousRodDurability, Snapshot.RodDurability, KINDA_SMALL_NUMBER)
		|| bPreviousRodBroken != Snapshot.bRodBroken
		|| PreviousBaitDefinitionId != Snapshot.BaitDefinitionId
		|| PreviousBaitItemInstanceId != Snapshot.BaitItemInstanceId
		|| PreviousFloatDefinitionId != Snapshot.FloatDefinitionId
		|| PreviousFloatItemInstanceId != Snapshot.FloatItemInstanceId
		|| PreviousScoopNetDefinitionId != Snapshot.ScoopNetDefinitionId
		|| PreviousScoopNetItemInstanceId != Snapshot.ScoopNetItemInstanceId;
	if (!bInventoryProjectionChanged && !bSelectionChanged)
	{
		return true;
	}

	++Snapshot.Revision;
	PublishSnapshot();
	return true;
}

// 正式库存投影构建流程：
// 1. 先从 Owner 取得正式库存组件并按配置容量保留空格数量，确保旧 UI 和存档看到的格位不缩水。
// 2. 再逐格读取正式库存实例；空格保持默认旧槽位，非空格必须是装备库存实例并能自我投影。
// 3. 任一非空格无法投影都会失败返回，调用方不能用半份旧快照覆盖现有消费者读模型。
bool UCatEquipmentComponent::BuildSnapshotInventorySlotsFromOwnerInventoryComponent(
	TArray<FCatRunInventorySlot>& OutSlots) const
{
	OutSlots.Reset();
	const UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent();
	if (OwnerInventory == nullptr)
	{
		return false;
	}

	const TArray<FCatInventoryEntry> FormalEntries = OwnerInventory->GetInventoryEntries();
	const int32 SnapshotSlotCount = FMath::Max(GetConfiguredInventorySlotCapacity(), FormalEntries.Num());
	OutSlots.SetNum(SnapshotSlotCount);
	for (int32 SlotIndex = 0; SlotIndex < FormalEntries.Num(); ++SlotIndex)
	{
		const FCatInventoryEntry& Entry = FormalEntries[SlotIndex];
		if (Entry.Instance == nullptr || Entry.StackCount <= 0)
		{
			continue;
		}

		const UCatEquipmentInventoryItemInstance* EquipmentInstance =
			Cast<UCatEquipmentInventoryItemInstance>(Entry.Instance);
		if (EquipmentInstance == nullptr
			|| !EquipmentInstance->BuildLegacyRunInventorySlot(Entry.StackCount, OutSlots[SlotIndex]))
		{
			OutSlots.Reset();
			return false;
		}
	}

	return true;
}

// 正式 entry 到旧槽位投影流程：
// 1. 只接受装备库存实例，避免 Equipment 从通用库存格里猜测鱼竿、鱼饵或鱼漂规则。
// 2. 数量和实例状态都从正式 entry 读出，旧槽位只是传给既有定义裁决和迁移期 UI 的只读载荷。
// 3. 投影失败时返回 false，调用方必须保持正式库存事实不被旧 Snapshot 半覆盖。
bool UCatEquipmentComponent::BuildLegacyRunInventorySlotFromFormalEntry(
	const FCatInventoryEntry& Entry, FCatRunInventorySlot& OutSlot) const
{
	const UCatEquipmentInventoryItemInstance* EquipmentInstance =
		Cast<UCatEquipmentInventoryItemInstance>(Entry.Instance);
	return EquipmentInstance != nullptr
		&& EquipmentInstance->BuildLegacyRunInventorySlot(Entry.StackCount, OutSlot);
}

// 钓鱼选择库存候选解析流程：
// 1. 先拒绝空定义，并清空输出槽位，避免调用方误用上一次的局部缓存。
// 2. Owner 正式库存容量完整时，把 InventoryComponent 当成唯一库存事实；实例 ID 优先精确定位，没有实例 ID 时按定义扫描正式槽位。
// 3. 扫描时沿用旧鱼竿选择的“可用优先、否则第一命中”口径，保证旧按定义选择入口不会意外选中断竿。
// 4. 正式 entry 必须能投影成旧槽位且定义一致，才允许 Equipment 更新钓鱼选择和鱼竿运行态。
// 5. 只有没有正式库存或正式库存尚未完成迁移容量初始化时，才只读回退旧 Snapshot，避免新链路继续信任过期投影。
bool UCatEquipmentComponent::TryResolveSelectionInventorySlot(const FName DefinitionId,
	const FGuid ItemInstanceId, FCatRunInventorySlot& OutSlot) const
{
	OutSlot = FCatRunInventorySlot();
	if (DefinitionId.IsNone())
	{
		return false;
	}

	const UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent();
	if (OwnerInventory != nullptr && OwnerInventory->GetInventorySlotCount() >= GetConfiguredInventorySlotCapacity())
	{
		if (ItemInstanceId.IsValid())
		{
			const int32 SlotIndex = OwnerInventory->FindInventorySlotIndexFromInstanceId(ItemInstanceId);
			const FCatInventoryEntry* FormalEntry = OwnerInventory->GetInventoryEntryAtSlot(SlotIndex);
			if (FormalEntry == nullptr || FormalEntry->StackCount <= 0
				|| !BuildLegacyRunInventorySlotFromFormalEntry(*FormalEntry, OutSlot)
				|| OutSlot.DefinitionId != DefinitionId)
			{
				OutSlot = FCatRunInventorySlot();
				return false;
			}
			return true;
		}

		FCatRunInventorySlot FirstMatchedSlot;
		bool bFoundFirstMatchedSlot = false;
		const TArray<FCatInventoryEntry> FormalEntries = OwnerInventory->GetInventoryEntries();
		for (const FCatInventoryEntry& FormalEntry : FormalEntries)
		{
			FCatRunInventorySlot ProjectedSlot;
			if (FormalEntry.StackCount <= 0
				|| !BuildLegacyRunInventorySlotFromFormalEntry(FormalEntry, ProjectedSlot)
				|| ProjectedSlot.DefinitionId != DefinitionId)
			{
				continue;
			}
			if (!bFoundFirstMatchedSlot)
			{
				FirstMatchedSlot = ProjectedSlot;
				bFoundFirstMatchedSlot = true;
			}
			if (!ProjectedSlot.bRodBroken && FMath::IsFinite(ProjectedSlot.RodDurability)
				&& ProjectedSlot.RodDurability > 0.0)
			{
				OutSlot = ProjectedSlot;
				return true;
			}
		}
		if (!bFoundFirstMatchedSlot)
		{
			return false;
		}
		OutSlot = FirstMatchedSlot;
		return true;
	}

	const FCatRunInventorySlot* LegacySlot = ItemInstanceId.IsValid()
		? FindInventorySlotByInstanceId(ItemInstanceId) : FindFirstInventorySlotByDefinition(DefinitionId);
	if (LegacySlot == nullptr || LegacySlot->DefinitionId != DefinitionId)
	{
		return false;
	}
	OutSlot = *LegacySlot;
	return true;
}

// 选中鱼竿正式实例解析流程：
// 1. 先按 Snapshot 保存的实例 ID 回正式库存查槽；没有实例 ID 或槽位为空都表示旧选择已经失去库存事实。
// 2. 再把正式 entry 投影成旧槽位，用同一套归一化规则确认定义、实例和单件数量都对应当前选择。
// 3. 最后返回可写装备实例；调用方只有拿到它时才能修复或损伤耐久，不能退回只改旧 Snapshot。
UCatEquipmentInventoryItemInstance* UCatEquipmentComponent::ResolveSelectedFormalRodInstanceFromInventory(
	UCatInventoryComponent& OwnerInventory, const UCatEquipmentDefinition& RodDefinition,
	FCatRunInventorySlot& OutProjectedSlot) const
{
	OutProjectedSlot = FCatRunInventorySlot();
	if (!Snapshot.RodItemInstanceId.IsValid()
		|| Snapshot.RodDefinitionId != RodDefinition.EquipmentDefinitionId
		|| RodDefinition.Kind != ECatEquipmentKind::Rod)
	{
		return nullptr;
	}

	const int32 SlotIndex = OwnerInventory.FindInventorySlotIndexFromInstanceId(Snapshot.RodItemInstanceId);
	const FCatInventoryEntry* FormalEntry = OwnerInventory.GetInventoryEntryAtSlot(SlotIndex);
	UCatEquipmentInventoryItemInstance* FormalInstance = FormalEntry != nullptr
		? Cast<UCatEquipmentInventoryItemInstance>(FormalEntry->Instance) : nullptr;
	if (FormalInstance == nullptr
		|| !BuildLegacyRunInventorySlotFromFormalEntry(*FormalEntry, OutProjectedSlot))
	{
		OutProjectedSlot = FCatRunInventorySlot();
		return nullptr;
	}

	if (OutProjectedSlot.ItemInstanceId != Snapshot.RodItemInstanceId
		|| OutProjectedSlot.DefinitionId != Snapshot.RodDefinitionId
		|| OutProjectedSlot.Quantity != 1)
	{
		OutProjectedSlot = FCatRunInventorySlot();
		return nullptr;
	}

	return FormalInstance;
}

UCatEquipmentComponent::FCatFishingUseRecord* UCatEquipmentComponent::FindFishingUseRecord(const FGuid FishingSessionId)
{
	return FishingUseRecords.Find(FishingSessionId);
}

const UCatEquipmentComponent::FCatFishingUseRecord* UCatEquipmentComponent::FindFishingUseRecord(const FGuid FishingSessionId) const
{
	return FishingUseRecords.Find(FishingSessionId);
}

bool UCatEquipmentComponent::TryBuildHeldInventoryUseSlot(const FGuid ItemInstanceId,
	FCatRunInventorySlot& OutSlot) const
{
	// 正式部署投影流程：只从 Owner 库存活动区读取同一实例，把它转换为旧槽位给配置和 Fishing 预检使用；查询失败必须清空输出，避免继续信任过期镜像。
	OutSlot = FCatRunInventorySlot();
	UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent();
	const FCatInventoryEntry* HeldEntry =
		OwnerInventory != nullptr ? OwnerInventory->FindHeldInventoryEntryFromAuthority(ItemInstanceId) : nullptr;
	if (HeldEntry == nullptr || HeldEntry->StackCount <= 0
		|| !BuildLegacyRunInventorySlotFromFormalEntry(*HeldEntry, OutSlot))
	{
		OutSlot = FCatRunInventorySlot();
		return false;
	}

	if (OutSlot.ItemInstanceId != ItemInstanceId || OutSlot.Quantity != 1)
	{
		OutSlot = FCatRunInventorySlot();
		return false;
	}

	return true;
}

UCatEquipmentInventoryItemInstance* UCatEquipmentComponent::ResolveFishingRodFormalInstanceFromInventory(
	const FCatFishingUseRecord& Record, FCatRunInventorySlot& OutProjectedSlot) const
{
	if (Record.SessionId.IsValid() && !Record.RodEquipment.IsValid()) return nullptr;
	// Fishing 鱼竿正式实例解析流程：
	// 1. 先用 Begin 冻结的实例 ID 查可见库存格；找不到时再查库存活动区，覆盖正在部署的世界鱼竿。
	// 2. 命中 entry 后必须投影回旧槽位并核对定义、实例和单件数量，防止同定义另一根竿承接磨损。
	// 3. 只有正式装备实例本体有效时才返回可写指针，调用方随后把耐久写回库存实例而不是写 Equipment 镜像。
	OutProjectedSlot = FCatRunInventorySlot();
	if (!Record.RodItemInstanceId.IsValid() || Record.RodDefinitionId.IsNone())
	{
		return nullptr;
	}

	UCatEquipmentComponent* RodEquipment = Record.RodEquipment.IsValid() ? Record.RodEquipment.Get() : const_cast<UCatEquipmentComponent*>(this);
	if (Record.SessionId.IsValid() && !Record.bReleased)
	{
		const FCatInventoryItemUseRecord* Use = RodEquipment->FindInventoryItemUseRecord(Record.RodItemInstanceId);
		if (!Use || Use->bReleased || Use->BoundFishingSessionId != Record.SessionId || Use->FishingUseCoordinator.Get() != this) return nullptr;
	}
	UCatInventoryComponent* OwnerInventory = RodEquipment->ResolveOwnerInventoryComponent();
	if (OwnerInventory == nullptr)
	{
		return nullptr;
	}

	const int32 SlotIndex = OwnerInventory->FindInventorySlotIndexFromInstanceId(Record.RodItemInstanceId);
	const FCatInventoryEntry* FormalEntry = OwnerInventory->GetInventoryEntryAtSlot(SlotIndex);
	if (FormalEntry == nullptr)
	{
		FormalEntry = OwnerInventory->FindHeldInventoryEntryFromAuthority(Record.RodItemInstanceId);
	}

	UCatEquipmentInventoryItemInstance* FormalInstance = FormalEntry != nullptr
		? Cast<UCatEquipmentInventoryItemInstance>(FormalEntry->Instance) : nullptr;
	if (FormalInstance == nullptr
		|| !BuildLegacyRunInventorySlotFromFormalEntry(*FormalEntry, OutProjectedSlot))
	{
		OutProjectedSlot = FCatRunInventorySlot();
		return nullptr;
	}

	if (OutProjectedSlot.ItemInstanceId != Record.RodItemInstanceId
		|| OutProjectedSlot.DefinitionId != Record.RodDefinitionId
		|| OutProjectedSlot.Quantity != 1)
	{
		OutProjectedSlot = FCatRunInventorySlot();
		return nullptr;
	}

	return FormalInstance;
}

UCatEquipmentComponent::FCatInventoryItemUseRecord* UCatEquipmentComponent::FindInventoryItemUseRecord(
	const FGuid ItemInstanceId)
{
	// 活动 Use 记录查找流程：调用方必须提供实例 ID，本函数只返回本 Character 生命周期内的记录指针，不创建默认记录。
	return InventoryItemUseRecords.Find(ItemInstanceId);
}

const UCatEquipmentComponent::FCatInventoryItemUseRecord* UCatEquipmentComponent::FindInventoryItemUseRecord(
	const FGuid ItemInstanceId) const
{
	// 活动 Use 记录只读查找流程：供并发 gate、状态同步和回滚读取同一份实例副本，不允许借查询改变记录状态。
	return InventoryItemUseRecords.Find(ItemInstanceId);
}

bool UCatEquipmentComponent::HasActiveInventoryItemUse() const
{
	// 活动物品使用 gate 流程：以 Inventory held-entry 为部署占用事实；缺少正式库存时不再让 Equipment 玩法镜像成为库存锁。
	if (const UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent())
	{
		return OwnerInventory->HasActiveHeldInventoryEntriesFromAuthority();
	}
	return false;
}

FCatRunInventorySlot* UCatEquipmentComponent::FindInventorySlotByInstanceId(const FGuid ItemInstanceId)
{
	// 实例格查找流程：先拒绝空 GUID，再只在有内容的旧投影格中匹配，防止空格残留字段被当成可见物品。
	if (!ItemInstanceId.IsValid())
	{
		return nullptr;
	}
	for (FCatRunInventorySlot& Slot : Snapshot.InventorySlots)
	{
		if (Slot.ItemInstanceId == ItemInstanceId && CatRunInventorySlotOperations::IsInventorySlotOccupied(Slot))
		{
			return &Slot;
		}
	}
	return nullptr;
}

const FCatRunInventorySlot* UCatEquipmentComponent::FindInventorySlotByInstanceId(
	const FGuid ItemInstanceId) const
{
	// 实例格只读查找流程：和可写版本保持同一命中口径，供选择解析、预检和诊断读取旧投影里的实例身份。
	if (!ItemInstanceId.IsValid())
	{
		return nullptr;
	}
	for (const FCatRunInventorySlot& Slot : Snapshot.InventorySlots)
	{
		if (Slot.ItemInstanceId == ItemInstanceId && CatRunInventorySlotOperations::IsInventorySlotOccupied(Slot))
		{
			return &Slot;
		}
	}
	return nullptr;
}

const FCatRunInventorySlot* UCatEquipmentComponent::FindFirstInventorySlotByDefinition(const FName DefinitionId) const
{
	// 按定义回退到实例的流程：旧 UI 没有实例 ID 时只在旧投影里落到一份可见物品，不能让上层只拿 DefinitionId 改状态。
	// 1. 旧 UI 或定义型选择路径只给 DefinitionId 时，先在旧投影里记住第一份同定义物品。
	// 2. 如果同定义里有未断且耐久有效的鱼竿，优先返回它，避免自动选中一根不可用的断竿。
	// 3. 没找到可用鱼竿时返回第一份匹配物品，让上层继续按自己的规则给出失败或选择结果。
	if (DefinitionId.IsNone())
	{
		return nullptr;
	}
	const FCatRunInventorySlot* FirstMatchedSlot = nullptr;
	for (const FCatRunInventorySlot& Slot : Snapshot.InventorySlots)
	{
		if (Slot.DefinitionId == DefinitionId && CatRunInventorySlotOperations::IsInventorySlotOccupied(Slot))
		{
			if (!FirstMatchedSlot)
			{
				FirstMatchedSlot = &Slot;
			}
			if (!Slot.bRodBroken && FMath::IsFinite(Slot.RodDurability) && Slot.RodDurability > 0.0)
			{
				return &Slot;
			}
		}
	}
	return FirstMatchedSlot;
}

void UCatEquipmentComponent::SyncSelectedRodStateToSelectedInstance()
{
	// 鱼竿状态同步流程：
	// 1. 当前选择快照仍服务 UI 与老调用方；可见格或 held-entry 里的正式库存实例才是鱼竿状态事实源。
	// 2. 正式路径先写回库存实例本体，再同步旧活动 Use 镜像，避免收杆前后出现两份耐久。
	// 3. 没有正式库存组件时直接返回，不再把耐久写进旧投影格或活动 Use 副本。
	// 4. 已收口记录不再改写，避免收杆后的历史记录影响新的实例状态。
	if (!Snapshot.RodItemInstanceId.IsValid())
	{
		return;
	}
	if (ResolveOwnerInventoryComponent() == nullptr)
	{
		return;
	}

	FCatFishingUseRecord SelectedRodRecord;
	SelectedRodRecord.RodItemInstanceId = Snapshot.RodItemInstanceId;
	SelectedRodRecord.RodDefinitionId = Snapshot.RodDefinitionId;
	FCatRunInventorySlot ProjectedRodSlot;
	UCatEquipmentInventoryItemInstance* FormalRodInstance =
		ResolveFishingRodFormalInstanceFromInventory(SelectedRodRecord, ProjectedRodSlot);
	if (FormalRodInstance == nullptr)
	{
		return;
	}

	FormalRodInstance->SetRodRuntimeStateFromAuthority(Snapshot.RodDurability, Snapshot.bRodBroken);
	Snapshot.RodDurability = FormalRodInstance->GetRodDurability();
	Snapshot.bRodBroken = FormalRodInstance->IsRodBroken();
	if (FCatInventoryItemUseRecord* ActiveUse = FindInventoryItemUseRecord(Snapshot.RodItemInstanceId))
	{
		if (!ActiveUse->bReleased && ActiveUse->Item.ItemInstanceId == Snapshot.RodItemInstanceId)
		{
			ActiveUse->Item.RodDurability = Snapshot.RodDurability;
			ActiveUse->Item.bRodBroken = Snapshot.bRodBroken;
		}
	}
}

// 自动选择流程：
// 1. 新获得的物品只在当前选择缺失、旧选择无法对应投影/正式 held-entry，或已选竿已断/耐久非法时介入。
// 2. 鱼竿选择刷新会记录具体实例并读取这根实例自己的耐久；断竿收口时会从当前投影中寻找可用替代竿。
// 3. 鱼饵、鱼漂和抄网也保留被选中的实例身份，但不会因为新增同类物品抢占仍有效的选择。
// 4. 这个流程不移出库存物品，也不创建独立装备栏；Fishing Begin 会按当前选择自行暂存要消耗的那一份饵。
void UCatEquipmentComponent::AutoSelectGrantedInventoryItem(const UCatEquipmentDefinition& Definition,
	const FName DefinitionId)
{
	if (DefinitionId.IsNone())
	{
		return;
	}
	if (Definition.Kind == ECatEquipmentKind::Rod)
	{
		const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
		const FCatRunInventorySlot* PreferredUsableSlot = nullptr;
		const FCatRunInventorySlot* FirstUsableSlot = nullptr;
		// 回退槽只服务“当前选择缺失”的修复路径；断竿替换必须拿到可用竿，不能把另一根坏竿当成替代结果。
		const FCatRunInventorySlot* PreferredFallbackSlot = nullptr;
		const FCatRunInventorySlot* FirstFallbackSlot = nullptr;
		for (const FCatRunInventorySlot& Slot : Snapshot.InventorySlots)
		{
			const UCatEquipmentDefinition* SlotDefinition = Settings != nullptr
				? Settings->FindRuntimeDefinition(Slot.DefinitionId) : nullptr;
			if (!CatRunInventorySlotOperations::IsInventorySlotOccupied(Slot)
				|| SlotDefinition == nullptr || SlotDefinition->Kind != ECatEquipmentKind::Rod)
			{
				continue;
			}

			if (FirstFallbackSlot == nullptr)
			{
				FirstFallbackSlot = &Slot;
			}
			const bool bSlotUsable = !Slot.bRodBroken && FMath::IsFinite(Slot.RodDurability)
				&& Slot.RodDurability > 0.0;
			if (bSlotUsable && FirstUsableSlot == nullptr)
			{
				FirstUsableSlot = &Slot;
			}
			if (Slot.DefinitionId == DefinitionId)
			{
				if (PreferredFallbackSlot == nullptr)
				{
					PreferredFallbackSlot = &Slot;
				}
				if (bSlotUsable)
				{
					PreferredUsableSlot = &Slot;
					break;
				}
			}
		}
		if (FirstFallbackSlot == nullptr)
		{
			// 当前投影里没有任何鱼竿实例时不能制造选择；保持原快照，让上层继续看到缺失状态。
			return;
		}
		const FCatRunInventorySlot* SelectedStoredRod =
			FindInventorySlotByInstanceId(Snapshot.RodItemInstanceId);
		const bool bSelectedStoredRodMatches = SelectedStoredRod != nullptr
			&& SelectedStoredRod->DefinitionId == Snapshot.RodDefinitionId;
		FCatRunInventorySlot ActiveSelectedRodSlot;
		bool bSelectedRodIsInUse = false;
		if (ResolveOwnerInventoryComponent() != nullptr)
		{
			bSelectedRodIsInUse = TryBuildHeldInventoryUseSlot(Snapshot.RodItemInstanceId, ActiveSelectedRodSlot)
				&& ActiveSelectedRodSlot.DefinitionId == Snapshot.RodDefinitionId;
		}
		else if (const FCatInventoryItemUseRecord* ActiveSelectedRod =
			FindInventoryItemUseRecord(Snapshot.RodItemInstanceId))
		{
			bSelectedRodIsInUse = !ActiveSelectedRod->bReleased
				&& ActiveSelectedRod->Item.DefinitionId == Snapshot.RodDefinitionId;
		}
		const bool bSelectedRodUnavailable = Snapshot.RodDefinitionId.IsNone()
			|| (!bSelectedStoredRodMatches && !bSelectedRodIsInUse);
		const bool bStoredSelectedRodBroken = bSelectedStoredRodMatches
			&& (SelectedStoredRod->bRodBroken || !FMath::IsFinite(SelectedStoredRod->RodDurability)
				|| SelectedStoredRod->RodDurability <= 0.0);
		const bool bSelectedRodNeedsReplacement = !bSelectedRodIsInUse
			&& (Snapshot.bRodBroken || !FMath::IsFinite(Snapshot.RodDurability)
				|| Snapshot.RodDurability <= 0.0 || bStoredSelectedRodBroken);
		// 可用替代和身份回退分开计算：断竿替换只能选可用竿，缺失选择才允许回到投影确认的坏竿。
		const FCatRunInventorySlot* ReplacementSlot = PreferredUsableSlot != nullptr
			? PreferredUsableSlot : FirstUsableSlot;
		const FCatRunInventorySlot* FallbackSlot = PreferredFallbackSlot != nullptr
			? PreferredFallbackSlot : FirstFallbackSlot;
		// 当前选择完全丢失时允许落到投影确认的任一鱼竿实例；已断选择只接受可用替代竿，保留坏竿给维修链读取。
		const FCatRunInventorySlot* TargetSlot = bSelectedRodNeedsReplacement
			? ReplacementSlot
			: (bSelectedRodUnavailable
				? (ReplacementSlot != nullptr ? ReplacementSlot : FallbackSlot)
				: nullptr);
		if (TargetSlot != nullptr)
		{
			const FName PreviousDefinitionId = Snapshot.RodDefinitionId;
			const FGuid PreviousItemInstanceId = Snapshot.RodItemInstanceId;
			const double PreviousRodDurability = Snapshot.RodDurability;
			const bool bPreviousRodBroken = Snapshot.bRodBroken;
			Snapshot.RodDefinitionId = TargetSlot->DefinitionId;
			Snapshot.RodItemInstanceId = TargetSlot->ItemInstanceId;
			Snapshot.RodDurability = TargetSlot->RodDurability;
			Snapshot.bRodBroken = TargetSlot->bRodBroken;
			UE_LOG(LogCatEquipment, Log,
				TEXT("Event=equipment_rod_auto_selected Reason=%s PreviousDefinition=%s PreviousItem=%s PreviousDurability=%.3f PreviousBroken=%s SelectedDefinition=%s SelectedItem=%s SelectedDurability=%.3f SelectedBroken=%s ActiveUse=%s Revision=%lld Owner=%s World=%s NetMode=%d"),
				bSelectedRodUnavailable ? TEXT("Unavailable") : TEXT("BrokenOrInvalid"),
				*PreviousDefinitionId.ToString(),
				*PreviousItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), PreviousRodDurability,
				bPreviousRodBroken ? TEXT("true") : TEXT("false"), *Snapshot.RodDefinitionId.ToString(),
				*Snapshot.RodItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), Snapshot.RodDurability,
				Snapshot.bRodBroken ? TEXT("true") : TEXT("false"),
				bSelectedRodIsInUse ? TEXT("true") : TEXT("false"), Snapshot.Revision, *GetNameSafe(GetOwner()),
				*GetNameSafe(GetWorld()), static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone));
		}
		return;
	}
	const auto SelectGrantedNonRodItemIfNeeded =
		[this, DefinitionId](const TCHAR* KindName, FName& InOutDefinitionId, FGuid& InOutItemInstanceId)
	{
		const FCatRunInventorySlot* SelectedSlot = FindInventorySlotByInstanceId(InOutItemInstanceId);
		const bool bSelectedItemValid = SelectedSlot != nullptr
			&& SelectedSlot->DefinitionId == InOutDefinitionId && SelectedSlot->Quantity > 0;
		if (!InOutDefinitionId.IsNone() && bSelectedItemValid)
		{
			return;
		}

		const FName PreviousDefinitionId = InOutDefinitionId;
		const FGuid PreviousItemInstanceId = InOutItemInstanceId;
		const FName PreferredDefinitionId = InOutDefinitionId.IsNone() ? DefinitionId : InOutDefinitionId;
		const FCatRunInventorySlot* ReplacementSlot = FindFirstInventorySlotByDefinition(PreferredDefinitionId);
		if (ReplacementSlot == nullptr && PreferredDefinitionId != DefinitionId)
		{
			ReplacementSlot = FindFirstInventorySlotByDefinition(DefinitionId);
		}
		if (ReplacementSlot == nullptr)
		{
			if (InOutDefinitionId.IsNone())
			{
				InOutDefinitionId = DefinitionId;
				InOutItemInstanceId = FGuid();
			}
			return;
		}

		InOutDefinitionId = ReplacementSlot->DefinitionId;
		InOutItemInstanceId = ReplacementSlot->ItemInstanceId;
		UE_LOG(LogCatEquipment, Log,
			TEXT("Event=equipment_item_auto_selected Kind=%s Reason=%s PreviousDefinition=%s PreviousItem=%s SelectedDefinition=%s SelectedItem=%s Revision=%lld Owner=%s World=%s NetMode=%d"),
			KindName, PreviousDefinitionId.IsNone() ? TEXT("Unavailable") : TEXT("MissingInstance"),
			*PreviousDefinitionId.ToString(),
			*PreviousItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), *InOutDefinitionId.ToString(),
			*InOutItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), Snapshot.Revision, *GetNameSafe(GetOwner()),
			*GetNameSafe(GetWorld()), static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone));
	};
	if (Definition.Kind == ECatEquipmentKind::Bait)
	{
		SelectGrantedNonRodItemIfNeeded(TEXT("Bait"), Snapshot.BaitDefinitionId, Snapshot.BaitItemInstanceId);
		return;
	}
	if (Definition.Kind == ECatEquipmentKind::Float)
	{
		SelectGrantedNonRodItemIfNeeded(TEXT("Float"), Snapshot.FloatDefinitionId, Snapshot.FloatItemInstanceId);
		return;
	}
	if (Definition.Kind == ECatEquipmentKind::ScoopNet)
	{
		SelectGrantedNonRodItemIfNeeded(TEXT("ScoopNet"), Snapshot.ScoopNetDefinitionId,
			Snapshot.ScoopNetItemInstanceId);
	}
}

FCatFishingUseReservationResult UCatEquipmentComponent::MakeFishingUseReservationResult(const FGuid FishingSessionId,
	const ECatDomainCommandError Error, const bool bReserved, const FCatFishingUseRecord* Record) const
{
	FCatFishingUseReservationResult Result;
	Result.SessionId = FishingSessionId;
	Result.Error = Error;
	Result.EquipmentRevision = Snapshot.Revision;
	Result.bReserved = bReserved;
	GetFishingRodDurability(FishingSessionId, Result.RemainingRodDurability, Result.bRodBroken);
	if (!Record) Record = FindFishingUseRecord(FishingSessionId);
	if (Record)
	{
		Result.WearSequence = Record->LastWearSequence;
		Result.AbsoluteRodWear = Record->AbsoluteRodWear;
	}
	return Result;
}

FCatFishingUseOperationResult UCatEquipmentComponent::MakeFishingUseOperationResult(const FGuid FishingSessionId,
	const ECatDomainCommandError Error, const bool bApplied, const FCatFishingUseRecord* Record) const
{
	FCatFishingUseOperationResult Result;
	Result.SessionId = FishingSessionId;
	Result.Error = Error;
	Result.EquipmentRevision = Snapshot.Revision;
	Result.bApplied = bApplied;
	GetFishingRodDurability(FishingSessionId, Result.RemainingRodDurability, Result.bRodBroken);
	if (!Record) Record = FindFishingUseRecord(FishingSessionId);
	if (Record)
	{
		Result.WearSequence = Record->LastWearSequence;
		Result.AbsoluteRodWear = Record->AbsoluteRodWear;
	}
	return Result;
}

// 幂等键流程：组合操作名与 RequestId，只存在本 Character 内存；不承担跨局 Profile 或平台身份。
FString UCatEquipmentComponent::MakeTerminalKey(const TCHAR* Operation, const FGuid RequestId)
{
	return FString::Printf(TEXT("%s|%s"), Operation, *RequestId.ToString(EGuidFormats::DigitsWithHyphens));
}

// Snapshot 发布流程：只要求 Owner 立即复制并广播旧读模型变化；正式背包事实必须由 InventoryComponent 或显式恢复导入入口提交。
void UCatEquipmentComponent::PublishSnapshot()
{
	if (bDeferringSnapshotPublication)
	{
		bSnapshotPublicationPending = true;
		return;
	}
	if (AActor* Owner = GetOwner(); Owner && Owner->HasAuthority())
	{
		Owner->ForceNetUpdate();
	}
	OnSnapshotChanged.Broadcast();
}


void UCatEquipmentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseFishingUsesForShutdown(TEXT("EndPlay"));
	Super::EndPlay(EndPlayReason);
}

void UCatEquipmentComponent::OnComponentDestroyed(const bool bDestroyingHierarchy)
{
	ReleaseFishingUsesForShutdown(TEXT("OnComponentDestroyed"));
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UCatEquipmentComponent::DestroyComponent(const bool bPromoteChildren)
{
	if (bDestroyComponentInProgress) return;
	TGuardValue<bool> DestroyGuard(bDestroyComponentInProgress, true);
	ReleaseFishingUsesForShutdown(TEXT("DestroyComponent"));
	Super::DestroyComponent(bPromoteChildren);
}

void UCatEquipmentComponent::ReleaseFishingUsesForShutdown(const TCHAR* Reason)
{
	TArray<FGuid> SessionIds;
	for (const TPair<FGuid, FCatFishingUseRecord>& Pair : FishingUseRecords)
	{
		if (!Pair.Value.bReleased) SessionIds.Add(Pair.Key);
	}
	if (!SessionIds.IsEmpty())
	{
		UE_LOG(LogCatEquipment, Log,
			TEXT("Event=equipment_fishing_shutdown_requested Reason=%s ActiveSessions=%d AlreadyEnding=%s Revision=%lld World=%s NetMode=%d Authority=%s LocalRole=%d Owner=%s"),
			Reason, SessionIds.Num(), bEndingPlay ? TEXT("true") : TEXT("false"), Snapshot.Revision,
			*GetNameSafe(GetWorld()), static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone),
			GetOwner() && GetOwner()->HasAuthority() ? TEXT("true") : TEXT("false"),
			GetOwner() ? static_cast<int32>(GetOwner()->GetLocalRole()) : 0, *GetNameSafe(GetOwner()));
	}
	if (bEndingPlay) return;
	bEndingPlay = true;
	if (UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr)
	{
		Fishing->PreserveFishingResourcesForEquipmentShutdown(this);
		// 迁移已移除原记录。未被正式 Session 接收的 Begin 暂存仍沿原回滚路径收口。
		SessionIds.Reset();
		for (const auto& Pair : FishingUseRecords)
			if (!Pair.Value.bReleased) SessionIds.Add(Pair.Key);
	}
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		for (const FGuid SessionId : SessionIds)
		{
			const FCatFishingUseOperationResult Result = ReleaseFishingUse(SessionId);
			if (!Result.bApplied && Result.Error != ECatDomainCommandError::AlreadyResolved)
			{
				// 销毁中的库存无法继续持有暂存物；即使目录已经卸载，也必须解除另一宿主的精确占用。
				FCatFishingUseRecord* Record = FindFishingUseRecord(SessionId);
				UCatEquipmentComponent* RodEquipment = Record ? Record->RodEquipment.Get() : nullptr;
				FCatInventoryItemUseRecord* RodUse = RodEquipment && Record
					? RodEquipment->FindInventoryItemUseRecord(Record->RodItemInstanceId) : nullptr;
				const bool bOwnsLock = RodUse && RodUse->BoundFishingSessionId == SessionId
					&& RodUse->FishingUseCoordinator.Get() == this;
				if (Record) Record->bReleased = true;
				if (bOwnsLock)
				{
					RodUse->BoundFishingSessionId.Invalidate();
					RodUse->FishingUseCoordinator.Reset();
					++RodEquipment->Snapshot.Revision;
				}
				UE_LOG(LogCatEquipment, Warning,
					TEXT("Event=equipment_rod_session_exit_cleanup SessionId=%s Error=%s RodLockReleased=%s Revision=%lld RodEquipmentRevision=%lld World=%s NetMode=%d Authority=true LocalRole=%d Owner=%s RodOwner=%s"),
					*SessionId.ToString(), *UEnum::GetValueAsString(Result.Error), bOwnsLock ? TEXT("true") : TEXT("false"),
					Snapshot.Revision, RodEquipment ? RodEquipment->Snapshot.Revision : -1,
					*GetNameSafe(GetWorld()), static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone),
					static_cast<int32>(GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()),
					*GetNameSafe(RodEquipment ? RodEquipment->GetOwner() : nullptr));
				if (bOwnsLock && RodEquipment != this) RodEquipment->PublishSnapshot();
			}
		}
	}
	if (!SessionIds.IsEmpty())
	{
		int32 UnreleasedSessions = 0;
		for (const FGuid SessionId : SessionIds)
		{
			if (IsFishingUseActive(SessionId)) ++UnreleasedSessions;
		}
		UE_LOG(LogCatEquipment, Log,
			TEXT("Event=equipment_fishing_shutdown_completed Reason=%s RequestedSessions=%d UnreleasedSessions=%d Revision=%lld World=%s NetMode=%d Authority=%s LocalRole=%d Owner=%s"),
			Reason, SessionIds.Num(), UnreleasedSessions, Snapshot.Revision, *GetNameSafe(GetWorld()),
			static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone),
			GetOwner() && GetOwner()->HasAuthority() ? TEXT("true") : TEXT("false"),
			GetOwner() ? static_cast<int32>(GetOwner()->GetLocalRole()) : 0, *GetNameSafe(GetOwner()));
	}
}

bool UCatEquipmentComponent::MoveFishingResourcesToCustodian(UCatEquipmentComponent* Target,
	const TArray<FGuid>& SessionIds, const TArray<FGuid>& RodItemInstanceIds)
{
	if (!Target || Target == this || !GetOwner() || !GetOwner()->HasAuthority()
		|| !Target->GetOwner() || !Target->GetOwner()->HasAuthority() || Target->GetWorld() != GetWorld()
		|| Target->bEndingPlay) return false;
	// 所有跨宿主锁在修改前检查。整个转存不广播，调用者重绑所有消费者后再发布。
	for (const FGuid SessionId : SessionIds)
	{
		const FCatFishingUseRecord* Record = FindFishingUseRecord(SessionId);
		UCatEquipmentComponent* RodEquipment = Record ? Record->RodEquipment.Get(true) : nullptr;
		const FCatInventoryItemUseRecord* Use = RodEquipment && Record
			? RodEquipment->FindInventoryItemUseRecord(Record->RodItemInstanceId) : nullptr;
		if (!Record || Record->bReleased || Target->FishingUseRecords.Contains(SessionId)
			|| !Use || Use->bReleased || Use->BoundFishingSessionId != SessionId
			|| Use->FishingUseCoordinator.Get(true) != this
			|| (RodEquipment == this && !RodItemInstanceIds.Contains(Record->RodItemInstanceId))) return false;
	}
	for (const FGuid ItemId : RodItemInstanceIds)
	{
		const FCatInventoryItemUseRecord* Use = FindInventoryItemUseRecord(ItemId);
		if (!Use || Use->bReleased || Use->Item.ItemInstanceId != ItemId || Use->Item.Quantity != 1
			|| FindInventorySlotByInstanceId(ItemId) || Target->InventoryItemUseRecords.Contains(ItemId)) return false;
		if (Use->BoundFishingSessionId.IsValid())
		{
			UCatEquipmentComponent* Coordinator = Use->FishingUseCoordinator.Get(true);
			const FCatFishingUseRecord* Record = Coordinator ? Coordinator->FindFishingUseRecord(Use->BoundFishingSessionId) : nullptr;
			if (!Record || Record->bReleased || Record->RodEquipment.Get(true) != this || Record->RodItemInstanceId != ItemId
				|| (Coordinator == this && !SessionIds.Contains(Use->BoundFishingSessionId))) return false;
		}
	}
	UCatInventoryComponent* SourceInventory = ResolveOwnerInventoryComponent();
	UCatInventoryComponent* TargetInventory = Target->ResolveOwnerInventoryComponent();
	if (!SourceInventory || !SourceInventory->MoveHeldInventoryEntriesToCustodianFromAuthority(TargetInventory, RodItemInstanceIds)) return false;
	for (const FGuid SessionId : SessionIds)
	{
		FCatFishingUseRecord Record = MoveTemp(FishingUseRecords.FindChecked(SessionId));
		FishingUseRecords.Remove(SessionId);
		if (Record.RodEquipment.Get(true) == this) Record.RodEquipment = Target;
		Target->FishingUseRecords.Add(SessionId, MoveTemp(Record));
	}
	for (const FGuid ItemId : RodItemInstanceIds)
	{
		FCatInventoryItemUseRecord Use = MoveTemp(InventoryItemUseRecords.FindChecked(ItemId));
		InventoryItemUseRecords.Remove(ItemId);
		if (Use.FishingUseCoordinator.Get(true) == this) Use.FishingUseCoordinator = Target;
		if (Use.BoundFishingSessionId.IsValid())
		{
			UCatEquipmentComponent* Coordinator = Use.FishingUseCoordinator.Get(true);
			Coordinator->FishingUseRecords.FindChecked(Use.BoundFishingSessionId).RodEquipment = Target;
		}
		Target->InventoryItemUseRecords.Add(ItemId, MoveTemp(Use));
	}
	for (const FGuid SessionId : SessionIds)
	{
		const FCatFishingUseRecord& Record = Target->FishingUseRecords.FindChecked(SessionId);
		UCatEquipmentComponent* RodEquipment = Record.RodEquipment.Get(true);
		RodEquipment->InventoryItemUseRecords.FindChecked(Record.RodItemInstanceId).FishingUseCoordinator = Target;
	}
	++Snapshot.Revision;
	++Target->Snapshot.Revision;
	return true;
}

const AActor* UCatEquipmentComponent::GetInventoryTransferAuthorityActor() const
{
	return GetOwner();
}

ECatDomainCommandError UCatEquipmentComponent::ReadInventoryTransferEndpoint(const FName Channel,
	const FGuid EntryId, FCatInventoryEndpointSnapshot& OutSnapshot) const
{
	OutSnapshot = FCatInventoryEndpointSnapshot{};
	OutSnapshot.Revision = Snapshot.Revision;
	if (Channel == TEXT("Stored"))
	{
		if (EntryId.IsValid()) return ECatDomainCommandError::InvalidPayload;
		// 已部署实例不得同时残留在可转移库存中；其归还必须由 ActiveUse 源端点单次迁移。
		for (const FCatRunInventorySlot& Slot : Snapshot.InventorySlots)
		{
			const FCatInventoryItemUseRecord* ActiveUse = FindInventoryItemUseRecord(Slot.ItemInstanceId);
			if (CatRunInventorySlotOperations::IsInventorySlotOccupied(Slot) && ActiveUse && !ActiveUse->bReleased)
			{
				return ECatDomainCommandError::InvalidPayload;
			}
		}
		if (!BuildSnapshotInventorySlotsFromOwnerInventoryComponent(OutSnapshot.Slots))
			return ECatDomainCommandError::DependencyUnavailable;
		OutSnapshot.Capacity = FMath::Max(OutSnapshot.Slots.Num(), GetConfiguredInventorySlotCapacity());
		return ECatDomainCommandError::None;
	}
	if (Channel != TEXT("ActiveUse") || !EntryId.IsValid()) return ECatDomainCommandError::InvalidPayload;
	OutSnapshot.Capacity = 1;
	OutSnapshot.bCanReceive = false;
	OutSnapshot.bAllowPartial = false;
	OutSnapshot.bAllowSwap = false;
	const FCatInventoryItemUseRecord* Record = FindInventoryItemUseRecord(EntryId);
	if (!Record) return ECatDomainCommandError::NotFound;
	OutSnapshot.Slots.Add(Record->Item); // 拒绝或重复归还也向通道提供原实例，用于冻结兼容回执。
	if (Record->bReleased) return ECatDomainCommandError::AlreadyResolved;
	FCatRunInventorySlot FormalHeldSlot;
	if (!TryBuildHeldInventoryUseSlot(EntryId, FormalHeldSlot)) return ECatDomainCommandError::NotFound;
	OutSnapshot.Slots[0] = FormalHeldSlot;
	if (Record->ItemInstanceId != EntryId || Record->Item.ItemInstanceId != EntryId || Record->Item.Quantity != 1)
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	const UCatEquipmentDefinition* Definition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(Record->Item.DefinitionId);
	if (!Definition) return ECatDomainCommandError::DependencyUnavailable;
	const ECatDomainCommandError UnUseError = Definition->UnUse(Record->Item);
	if (UnUseError != ECatDomainCommandError::None) return UnUseError;
	if (Record->BoundFishingSessionId.IsValid()) return ECatDomainCommandError::InvalidPhase;
	return ECatDomainCommandError::None;
}

int32 UCatEquipmentComponent::GetInventoryTransferStackLimit(const FName DefinitionId) const
{
	const UCatEquipmentDefinition* Definition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(DefinitionId);
	return Definition ? GetInventoryStackLimit(*Definition) : 0;
}

void UCatEquipmentComponent::ApplyInventoryTransferWritesSilently(
	const TConstArrayView<FCatInventoryEndpointWrite> Writes, const int64 NewRevision)
{
	TMap<FGuid, int32> PreviousStoredQuantities;
	for (const FCatRunInventorySlot& Slot : Snapshot.InventorySlots)
	{
		if (CatRunInventorySlotOperations::IsInventorySlotOccupied(Slot))
		{
			PreviousStoredQuantities.Add(Slot.ItemInstanceId, Slot.Quantity);
		}
	}
	bool bStoredChanged = false;
	// 同宿主各端点先全部提交，再修正选择；不可因为 writes 顺序把尚未释放的坏竿当成仍在使用。
	for (const FCatInventoryEndpointWrite& Write : Writes)
	{
		if (Write.Channel == TEXT("Stored"))
		{
			// 正式 entries 已由服务一次准备；Snapshot 只读其提交后的投影。
			ResolveOwnerInventoryComponent()->ReplaceInventoryEntriesFromAuthority(Write.FormalEntries, Write.Slots.Num());
			BuildSnapshotInventorySlotsFromOwnerInventoryComponent(Snapshot.InventorySlots);
			bStoredChanged = true;
		}
		else if (Write.Channel == TEXT("ActiveUse"))
		{
			const bool bCleared = !Write.Slots.ContainsByPredicate([](const FCatRunInventorySlot& Slot)
			{
				return CatRunInventorySlotOperations::IsInventorySlotOccupied(Slot);
			});
			if (bCleared)
			{
				ResolveOwnerInventoryComponent()->RetireHeldInventoryEntryFromAuthority(Write.EntryId);
				if (FCatInventoryItemUseRecord* Record = FindInventoryItemUseRecord(Write.EntryId)) Record->bReleased = true;
			}
		}
	}
	if (bStoredChanged)
	{
		for (const FCatRunInventorySlot& Slot : Snapshot.InventorySlots)
		{
			if (!CatRunInventorySlotOperations::IsInventorySlotOccupied(Slot)) continue;
			const int32* PreviousQuantity = PreviousStoredQuantities.Find(Slot.ItemInstanceId);
			if (PreviousQuantity && Slot.Quantity <= *PreviousQuantity) continue;
			const UCatEquipmentDefinition* Definition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(Slot.DefinitionId);
			if (!Definition) continue; // 预检已验证目录；提交期间不生成替代实例或额外广播。
			if (Definition->Kind == ECatEquipmentKind::Rod && Slot.ItemInstanceId == Snapshot.RodItemInstanceId)
			{
				Snapshot.RodDefinitionId = Slot.DefinitionId;
				Snapshot.RodDurability = Slot.RodDurability;
				Snapshot.bRodBroken = Slot.bRodBroken;
			}
			AutoSelectGrantedInventoryItem(*Definition, Slot.DefinitionId);
		}
	}
	Snapshot.Revision = NewRevision;
}

void UCatEquipmentComponent::PublishInventoryTransfer()
{
	PublishSnapshot();
}

bool UCatEquipmentComponent::TryGetInventoryRodForDeployment(FCatRunInventorySlot& OutRod) const
{
	OutRod = FCatRunInventorySlot{};
	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	const auto IsDeployableRod = [this, Settings](const FCatRunInventorySlot& Slot)
	{
		if (!Slot.ItemInstanceId.IsValid() || !CatRunInventorySlotOperations::IsInventorySlotOccupied(Slot)
			|| Slot.bRodBroken || !FMath::IsFinite(Slot.RodDurability) || Slot.RodDurability <= 0.0)
		{
			return false;
		}
		const FCatInventoryItemUseRecord* UseRecord = FindInventoryItemUseRecord(Slot.ItemInstanceId);
		const UCatEquipmentDefinition* Definition = Settings->FindRuntimeDefinition(Slot.DefinitionId);
		return (!UseRecord || UseRecord->bReleased) && Definition && Definition->Kind == ECatEquipmentKind::Rod
			&& Definition->KeepsInventoryInstanceWhileUsed();
	};
	const FCatRunInventorySlot* Selected = FindInventorySlotByInstanceId(Snapshot.RodItemInstanceId);
	if (Selected && IsDeployableRod(*Selected))
	{
		OutRod = *Selected;
		return true;
	}
	for (const FCatRunInventorySlot& Slot : Snapshot.InventorySlots)
	{
		if (IsDeployableRod(Slot))
		{
			OutRod = Slot;
			return true;
		}
	}
	return false;
}

UCatEquipmentComponent* UCatEquipmentComponent::GetFishingRodEquipment(const FGuid FishingSessionId) const
{
	const FCatFishingUseRecord* Record = FindFishingUseRecord(FishingSessionId);
	UCatEquipmentComponent* RodEquipment = Record ? Record->RodEquipment.Get() : nullptr;
	return RodEquipment && !RodEquipment->bEndingPlay ? RodEquipment : nullptr;
}

UCatInventoryComponent* UCatEquipmentComponent::GetInventoryTransferInventory() const
{
	return ResolveOwnerInventoryComponent();
}

FCatRunInventorySlot* UCatEquipmentComponent::FindFishingRodInstance(const FCatFishingUseRecord& Record)
{
	FCatRunInventorySlot FormalSlot;
	if (!ResolveFishingRodFormalInstanceFromInventory(Record, FormalSlot)) return nullptr;
	UCatEquipmentComponent* RodEquipment = Record.RodEquipment.Get();
	if (!RodEquipment) return nullptr;
	if (FCatInventoryItemUseRecord* Use = RodEquipment->FindInventoryItemUseRecord(Record.RodItemInstanceId); Use && !Use->bReleased)
	{
		Use->Item = FormalSlot;
		return &Use->Item;
	}
	return nullptr;
}

int32 UCatEquipmentComponent::GetInventoryItemQuantity(const FName DefinitionId) const
{
	const UCatInventoryComponent* Inventory = ResolveOwnerInventoryComponent();
	return Inventory ? Inventory->CountVisibleInventoryQuantityByDefinitionId(DefinitionId) : 0;
}

bool UCatEquipmentComponent::RemoveInventoryItemQuantityFromInstance(const FGuid ItemInstanceId,
	const int32 Quantity, FCatRunInventorySlot& OutConsumedItem)
{
	UCatInventoryComponent* Inventory = ResolveOwnerInventoryComponent();
	if (!Inventory) return false;
	const int32 SlotIndex = Inventory->FindInventorySlotIndexFromInstanceId(ItemInstanceId);
	const FCatInventoryEntry* Entry = Inventory->GetInventoryEntryAtSlot(SlotIndex);
	if (!Entry || !BuildLegacyRunInventorySlotFromFormalEntry(*Entry, OutConsumedItem) || Quantity <= 0 || Entry->StackCount < Quantity) return false;
	OutConsumedItem.Quantity = Quantity;
	if (!Inventory->ConsumeItemAtSlot(SlotIndex, Quantity)) return false;
	if (!BuildSnapshotInventorySlotsFromOwnerInventoryComponent(Snapshot.InventorySlots)) return false;
	if (Snapshot.BaitItemInstanceId == ItemInstanceId && !FindInventorySlotByInstanceId(ItemInstanceId))
	{
		const FCatRunInventorySlot* Replacement = FindFirstInventorySlotByDefinition(OutConsumedItem.DefinitionId);
		Snapshot.BaitItemInstanceId = Replacement ? Replacement->ItemInstanceId : FGuid();
	}
	if (const UCatEquipmentDefinition* Definition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(OutConsumedItem.DefinitionId))
		AutoSelectGrantedInventoryItem(*Definition, OutConsumedItem.DefinitionId);
	return true;
}

bool UCatEquipmentComponent::AddInventoryItemQuantity(const UCatEquipmentDefinition& Definition,
	const FName DefinitionId, const int32 Quantity)
{
	UCatInventoryComponent* Inventory = ResolveOwnerInventoryComponent();
	if (!Inventory) return false;
	FCatInventoryReceiveBatch Batch;
	FCatInventoryDefinitionEntry& Entry = Batch.DefinitionEntries.AddDefaulted_GetRef();
	Entry.ItemDefinition = const_cast<UCatEquipmentDefinition*>(&Definition);
	Entry.Count = Quantity;
	if (!Inventory->TryReturnReservedInventoryBatchFromAuthority(Batch, Quantity)) return false;
	return BuildSnapshotInventorySlotsFromOwnerInventoryComponent(Snapshot.InventorySlots);
}

FCatDomainCommandResult UCatEquipmentComponent::MoveInventorySlotFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const int32 SourceSlotIndex, const int32 TargetSlotIndex)
{
	FCatInventoryTransferRequest Request;
	Request.RequestId = RequestId;
	Request.Initiator = GetOwner();
	Request.Source.Host = this;
	Request.Target.Host = this;
	Request.ExpectedSourceRevision = ExpectedRevision;
	Request.ExpectedTargetRevision = ExpectedRevision;
	Request.SourceSlotIndex = SourceSlotIndex;
	Request.TargetSlotIndex = TargetSlotIndex;
	Request.Mode = ECatInventoryTransferMode::DragToSlot;
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (UCatInventoryTransferService* Service = GetWorld() ? GetWorld()->GetSubsystem<UCatInventoryTransferService>() : nullptr)
	{
		const FCatInventoryTransferResult Transfer = Service->TransferFromAuthority(Request);
		Result.bCommitted = Transfer.bCommitted;
		Result.Error = Transfer.Error;
		Result.Revision = Transfer.SourceRevision;
	}
	return Result;
}
