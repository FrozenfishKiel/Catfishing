#include "Equipment/CatEquipmentComponent.h"

#include "Framework/Game/CatGameplayTypes.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Equipment/CatRunInventorySlotOperations.h"
#include "Equipment/Inventory/CatInventoryTransferService.h"
#include "GameFramework/Pawn.h"
#include "Logging/CatLogContext.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "Fishing/CatFishingService.h"

DEFINE_LOG_CATEGORY_STATIC(LogCatEquipment, Log, All);

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

// Snapshot 读取流程：返回服务器真相或客户端最近复制值；不从 Profile 或 Items 拼接第二份随身库存事实。
const FCatEquipmentLoadoutSnapshot& UCatEquipmentComponent::GetSnapshot() const
{
	return Snapshot;
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
		OutSnapshot.Slots = Snapshot.InventorySlots;
		OutSnapshot.Capacity = FMath::Max(Snapshot.InventorySlots.Num(), GetConfiguredInventorySlotCapacity());
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
			Snapshot.InventorySlots = Write.Slots;
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

// 当前钓鱼选择配置流程：
// 1. 先用 RequestId 返回既有终态；部署中的当前鱼竿可继续作为选择上下文，也可切换到本人库存中的另一根竿。
// 2. 每次提交都必须通过服务器目录、authority、Revision、定义类别、消耗属性和 Profile 解锁证明。
// 3. 鱼饵、鱼漂和可选抄网必须存在于随身库存；鱼竿来自库存格，或来自当前尚未收口的同一条 Use 记录。
// 4. 同一套定义和实例选择直接返回 AlreadyResolved；不同选择会切换当前钓鱼选择，并从鱼竿实例读取耐久。
// 5. 成功时只写钓鱼选择、实例身份和当前鱼竿状态，并发布同一份随身库存快照。
FCatDomainCommandResult UCatEquipmentComponent::ConfigureLoadoutFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const FName RodDefinitionId, const FName BaitDefinitionId,
	const FName FloatDefinitionId, const FName ScoopNetDefinitionId, const FName RodSkinDefinitionId,
	const FGuid RodItemInstanceId, const FGuid BaitItemInstanceId, const FGuid FloatItemInstanceId,
	const FGuid ScoopNetItemInstanceId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString Key = MakeTerminalKey(TEXT("ConfigureLoadout"), RequestId);
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		Result = *Cached;
		Result.bCommitted = false;
		Result.Error = ECatDomainCommandError::AlreadyResolved;
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
		const FCatInventoryItemUseRecord* ActiveSelectedRod =
			FindInventoryItemUseRecord(Snapshot.RodItemInstanceId);
		const bool bSelectedRodIsInUse = ActiveSelectedRod && !ActiveSelectedRod->bReleased
			&& ActiveSelectedRod->Item.DefinitionId == Snapshot.RodDefinitionId
			&& ActiveSelectedRod->Item.ItemInstanceId == Snapshot.RodItemInstanceId;
		const bool bRequestsActiveSelectedRod = bSelectedRodIsInUse
			&& RodDefinitionId == ActiveSelectedRod->Item.DefinitionId
			&& (!RodItemInstanceId.IsValid() || RodItemInstanceId == ActiveSelectedRod->Item.ItemInstanceId);
		const auto ResolveSelectedInventorySlot =
			[this](const FName DefinitionId, const FGuid ItemInstanceId) -> const FCatRunInventorySlot*
			{
				if (ItemInstanceId.IsValid())
				{
					const FCatRunInventorySlot* Slot = FindInventorySlotByInstanceId(ItemInstanceId);
					return Slot && Slot->DefinitionId == DefinitionId ? Slot : nullptr;
				}
				return FindFirstInventorySlotByDefinition(DefinitionId);
			};
		const auto ResolveSelectedRodSlot =
			[ActiveSelectedRod, bRequestsActiveSelectedRod, &ResolveSelectedInventorySlot](
				const FName DefinitionId, const FGuid ItemInstanceId) -> const FCatRunInventorySlot*
			{
				if (bRequestsActiveSelectedRod)
				{
					return ActiveSelectedRod ? &ActiveSelectedRod->Item : nullptr;
				}
				return ResolveSelectedInventorySlot(DefinitionId, ItemInstanceId);
			};
		const FCatRunInventorySlot* RodSlotBeforeNormalize = ResolveSelectedRodSlot(RodDefinitionId,
			RodItemInstanceId);
		const FCatRunInventorySlot* BaitSlotBeforeNormalize = ResolveSelectedInventorySlot(BaitDefinitionId,
			BaitItemInstanceId);
		const FCatRunInventorySlot* FloatSlotBeforeNormalize = ResolveSelectedInventorySlot(FloatDefinitionId,
			FloatItemInstanceId);
		const FCatRunInventorySlot* ScoopSlotBeforeNormalize = ScoopNetDefinitionId.IsNone()
			? nullptr : ResolveSelectedInventorySlot(ScoopNetDefinitionId, ScoopNetItemInstanceId);
		if (!RodSlotBeforeNormalize || !BaitSlotBeforeNormalize || !FloatSlotBeforeNormalize
			|| (!ScoopNetDefinitionId.IsNone() && !ScoopSlotBeforeNormalize))
		{
			Result.Error = ECatDomainCommandError::NotFound;
		}
		else
		{
			const bool bNormalized = NormalizeInventorySlots();
			const FCatRunInventorySlot* RodSlot = ResolveSelectedRodSlot(RodDefinitionId, RodItemInstanceId);
			const FCatRunInventorySlot* BaitSlot =
				ResolveSelectedInventorySlot(BaitDefinitionId, BaitItemInstanceId);
			const FCatRunInventorySlot* FloatSlot =
				ResolveSelectedInventorySlot(FloatDefinitionId, FloatItemInstanceId);
			const FCatRunInventorySlot* ScoopSlot = ScoopNetDefinitionId.IsNone()
				? nullptr : ResolveSelectedInventorySlot(ScoopNetDefinitionId, ScoopNetItemInstanceId);
			if (!RodSlot || !BaitSlot || !FloatSlot || (!ScoopNetDefinitionId.IsNone() && !ScoopSlot))
			{
				Result.Error = ECatDomainCommandError::NotFound;
			}
			else
			{
				const FGuid NewScoopItemInstanceId = ScoopSlot ? ScoopSlot->ItemInstanceId : FGuid();
				const bool bSameLoadout = Snapshot.RodDefinitionId == RodDefinitionId
					&& Snapshot.RodItemInstanceId == RodSlot->ItemInstanceId
					&& Snapshot.BaitDefinitionId == BaitDefinitionId
					&& Snapshot.BaitItemInstanceId == BaitSlot->ItemInstanceId
					&& Snapshot.FloatDefinitionId == FloatDefinitionId
					&& Snapshot.FloatItemInstanceId == FloatSlot->ItemInstanceId
					&& Snapshot.ScoopNetDefinitionId == ScoopNetDefinitionId
					&& Snapshot.ScoopNetItemInstanceId == NewScoopItemInstanceId
					&& Snapshot.RodSkinDefinitionId == RodSkinDefinitionId
					&& Snapshot.RodDurability == RodSlot->RodDurability
					&& Snapshot.bRodBroken == RodSlot->bRodBroken;
				if (bSameLoadout && !bNormalized)
				{
					Result.Error = ECatDomainCommandError::AlreadyResolved;
					Result.Revision = Snapshot.Revision;
					TerminalCache.Add(Key, Result);
					return Result;
				}
				const FGuid PreviousRodItemInstanceId = Snapshot.RodItemInstanceId;
				Snapshot.RodDefinitionId = RodDefinitionId;
				Snapshot.RodItemInstanceId = RodSlot->ItemInstanceId;
				Snapshot.BaitDefinitionId = BaitDefinitionId;
				Snapshot.BaitItemInstanceId = BaitSlot->ItemInstanceId;
				Snapshot.FloatDefinitionId = FloatDefinitionId;
				Snapshot.FloatItemInstanceId = FloatSlot->ItemInstanceId;
				Snapshot.ScoopNetDefinitionId = ScoopNetDefinitionId;
				Snapshot.ScoopNetItemInstanceId = NewScoopItemInstanceId;
				Snapshot.RodSkinDefinitionId = RodSkinDefinitionId;
				Snapshot.RodDurability = RodSlot->RodDurability;
				Snapshot.bRodBroken = RodSlot->bRodBroken;
				++Snapshot.Revision;
				PublishSnapshot();
				if (PreviousRodItemInstanceId != Snapshot.RodItemInstanceId)
				{
					UE_LOG(LogCatEquipment, Log,
						TEXT("Event=equipment_rod_selection_changed RequestId=%s PreviousRodItemInstanceId=%s RodItemInstanceId=%s Definition=%s Durability=%.3f Broken=%s Revision=%lld World=%s NetMode=%d Authority=true LocalRole=%d Owner=%s"),
						*RequestId.ToString(), *PreviousRodItemInstanceId.ToString(), *Snapshot.RodItemInstanceId.ToString(),
						*Snapshot.RodDefinitionId.ToString(), Snapshot.RodDurability, Snapshot.bRodBroken ? TEXT("true") : TEXT("false"),
						Snapshot.Revision, *GetNameSafe(GetWorld()), static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone),
						static_cast<int32>(GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()));
				}
				Result.bCommitted = true;
				Result.Error = ECatDomainCommandError::None;
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
// 3. 最后用统一库存格容量做只读预检；这里不扩容数组、不合并数量，只回答整批物品能否一次性交付。
ECatDomainCommandError UCatEquipmentComponent::ValidateInventoryQuantityGrant(const FGuid RequestId,
	const FName DefinitionId, const int32 Quantity) const
{
	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	const UCatEquipmentDefinition* Definition = Settings->FindRuntimeDefinition(DefinitionId);
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
	if (!CanStoreInventoryItem(*Definition, DefinitionId, Quantity))
	{
		return ECatDomainCommandError::CapacityExceeded;
	}
	return ECatDomainCommandError::None;
}

// 数量型库存物品入库流程：
// 1. 先拒绝无效 RequestId，并用 RequestId、定义和数量签名保护终态重放；载荷漂移直接拒绝且不改库存。
// 2. 首次提交复用商店扣款前预检；定义无效、数量无效或容量不足都会保持 Snapshot 不变。
// 3. ExpectedRevision 必须匹配当前随身库存快照，避免陈旧 UI 把较新的持有量覆盖掉。
// 4. 成功时写入随身库存格数组，并在当前选择缺失或旧竿不可用且已收回时修正选择。
// 5. 最后递增 Revision、发布完整快照并缓存终态，后续同请求只读首次结果。
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
	const ECatDomainCommandError Rejection = ValidateInventoryQuantityGrant(RequestId, DefinitionId, Quantity);
	if (Rejection != ECatDomainCommandError::None)
	{
		Result.Error = Rejection;
	}
	else if (Snapshot.Revision != ExpectedRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
	}
	else
	{
		if (AddInventoryItemQuantity(*Definition, DefinitionId, Quantity))
		{
			AutoSelectGrantedInventoryItem(*Definition, DefinitionId);
			++Snapshot.Revision;
			PublishSnapshot();
			Result.bCommitted = true;
			Result.Error = ECatDomainCommandError::None;
		}
		else
		{
			Result.Error = ECatDomainCommandError::CapacityExceeded;
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
// 3. 最后只要求定义是非数量型运行物品；鱼竿、鱼漂、抄网和后续工具都走同一条单实例入库规则。
// 4. 非数量物品也占用同一份随身库存格容量，超出当前库存上限时必须在商店扣款前返回 CapacityExceeded。
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
	const UCatEquipmentDefinition* Definition = Settings->FindRuntimeDefinition(DefinitionId);
	if (!Definition || Definition->bRunConsumable)
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	if (!CanStoreInventoryItem(*Definition, DefinitionId, 1))
	{
		return ECatDomainCommandError::CapacityExceeded;
	}
	return ECatDomainCommandError::None;
}

// 商店、奖励或临时测试来源的非数量物品入库流程：
// 1. 先用 RequestId 和定义 ID 找终态缓存；合法重放只返回首次结果，不重复增加库存数量或推进 Revision。
// 2. 首次提交复用扣款前预检同一套准入规则，并用 ExpectedRevision 防止陈旧 UI 覆盖较新的本人库存。
// 3. 把非数量定义加入随身库存格数组，并在当前选择缺失或旧竿不可用且已收回时修正选择。
// 4. 成功后发布完整快照；UI 从库存格展示所有库存物品，不再生成单独装备栏格子。
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
	const ECatDomainCommandError Admission = ValidateEquipmentGrantFromAuthority(RequestId, DefinitionId);
	if (Admission != ECatDomainCommandError::None)
	{
		Result.Error = Admission;
	}
	else if (Snapshot.Revision != ExpectedRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
	}
	else if (Definition)
	{
		if (AddInventoryItemQuantity(*Definition, DefinitionId, 1))
		{
			AutoSelectGrantedInventoryItem(*Definition, DefinitionId);
			++Snapshot.Revision;
			PublishSnapshot();
			Result.bCommitted = true;
			Result.Error = ECatDomainCommandError::None;
		}
		else
		{
			Result.Error = ECatDomainCommandError::CapacityExceeded;
		}
	}
	Result.Revision = Snapshot.Revision;
	TerminalCache.Add(Key, Result);
	TerminalPayloadByKey.Add(Key, PayloadSignature);
	return Result;
}

// 临时测试路径：只在玩家占有后的 authority 调用；复用正式库存事务，绝不从客户端或 Tick 自动补货。
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
	const UCatEquipmentDefinition* Definition = Settings->FindRuntimeDefinition(DefinitionId);
	if (!Definition || Definition->Kind != ECatEquipmentKind::ScoopNet)
	{
		UE_LOG(LogCatEquipment, Warning,
			TEXT("Event=equipment_starter_scoop_rejected RequestId=%s Definition=%s Error=InvalidScoopDefinition Revision=%lld %s"),
			*RequestId.ToString(), *DefinitionId.ToString(), Snapshot.Revision, *Context);
		return;
	}

	// 已有任一完整抄网即视为满足测试需求；保留玩家已有的有效选择，不额外占用背包格。
	for (const FCatRunInventorySlot& Slot : Snapshot.InventorySlots)
	{
		const UCatEquipmentDefinition* OwnedDefinition = Slot.Quantity > 0 && Slot.ItemInstanceId.IsValid()
			? Settings->FindRuntimeDefinition(Slot.DefinitionId) : nullptr;
		if (!OwnedDefinition || OwnedDefinition->Kind != ECatEquipmentKind::ScoopNet) continue;
		const FName PreviousDefinitionId = Snapshot.ScoopNetDefinitionId;
		const FGuid PreviousItemInstanceId = Snapshot.ScoopNetItemInstanceId;
		AutoSelectGrantedInventoryItem(*OwnedDefinition, Slot.DefinitionId);
		bStarterScoopNetGrantHandled = true;
		if (PreviousDefinitionId != Snapshot.ScoopNetDefinitionId || PreviousItemInstanceId != Snapshot.ScoopNetItemInstanceId)
		{
			++Snapshot.Revision;
			PublishSnapshot();
		}
		UE_LOG(LogCatEquipment, Log,
			TEXT("Event=equipment_starter_scoop_completed RequestId=%s Result=AlreadyOwned Definition=%s ScoopNetItemInstanceId=%s Revision=%lld %s"),
			*RequestId.ToString(), *Snapshot.ScoopNetDefinitionId.ToString(), *Snapshot.ScoopNetItemInstanceId.ToString(),
			Snapshot.Revision, *Context);
		return;
	}

	const FCatDomainCommandResult Grant = GrantEquipmentFromAuthority(RequestId, Snapshot.Revision, DefinitionId);
	bStarterScoopNetGrantHandled = Grant.bCommitted;
	if (Grant.bCommitted)
	{
		UE_LOG(LogCatEquipment, Log,
			TEXT("Event=equipment_starter_scoop_completed RequestId=%s Result=Granted Definition=%s ScoopNetItemInstanceId=%s Quantity=1 Revision=%lld %s"),
			*RequestId.ToString(), *DefinitionId.ToString(), *Snapshot.ScoopNetItemInstanceId.ToString(),
			Snapshot.Revision, *Context);
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
	const FGuid ItemInstanceId, const int32 Quantity)
{
	// 物品使用流程：
	// 1. 先校验 authority、RequestId 和数量，再用实例载荷签名处理幂等重放，避免数量消耗品重复扣量。
	// 2. 再按实例 ID 找到库存格并读取定义，具体能不能 Use、无实现时是否 no-op 都交给物品定义自己裁决。
	// 3. 部署型物品会把整份实例从库存移出；数量消耗物只扣当前实例指定份数；无实现物品保持库存不变。
	// 4. 部署型调用方在库存提交后才生成世界 Actor，生成或注册失败必须 UnUse 同一实例；数量消耗调用方必须先完成自己的玩法前置裁决。
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
	const FString PayloadSignature = FString::Printf(TEXT("ExpectedRevision=%lld|ItemInstance=%s|Quantity=%d"),
		ExpectedRevision, *ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), Quantity);
	if (const FCatInventoryItemUseResult* Cached = InventoryItemUseTerminalCache.Find(Key))
	{
		const FString* CachedPayload = TerminalPayloadByKey.Find(Key);
		if (!CachedPayload || *CachedPayload != PayloadSignature)
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
			return Result;
		}
		Result = *Cached;
		Result.bCommitted = false;
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	const auto Finish = [this, &Key, &PayloadSignature](const FCatInventoryItemUseResult& Completed)
	{
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
	NormalizeInventorySlots();
	const FCatRunInventorySlot* SourceSlot = FindInventorySlotByInstanceId(ItemInstanceId);
	const UCatEquipmentDefinition* Definition = SourceSlot
		? GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(SourceSlot->DefinitionId) : nullptr;
	if (!SourceSlot || !Definition)
	{
		Result.Error = ECatDomainCommandError::NotFound;
		return Finish(Result);
	}
	Result.Item = *SourceSlot;
	const ECatDomainCommandError DefinitionUseError = Definition->Use(*SourceSlot, Quantity);
	if (DefinitionUseError != ECatDomainCommandError::None)
	{
		Result.Error = DefinitionUseError;
		return Finish(Result);
	}
	if (Definition->ConsumesInventoryQuantityOnUse())
	{
		FCatRunInventorySlot ConsumedItem;
		if (!RemoveInventoryItemQuantityFromInstance(ItemInstanceId, Quantity, ConsumedItem))
		{
			Result.Error = ECatDomainCommandError::CapacityExceeded;
			return Finish(Result);
		}
		++Snapshot.Revision;
		PublishSnapshot();
		Result.Item = ConsumedItem;
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

	FCatRunInventorySlot UsedItem;
	if (!RemoveInventoryItemInstance(ItemInstanceId, UsedItem))
	{
		Result.Error = ECatDomainCommandError::NotFound;
		return Finish(Result);
	}
	FCatInventoryItemUseRecord Record;
	Record.ItemInstanceId = UsedItem.ItemInstanceId;
	Record.Item = UsedItem;
	InventoryItemUseRecords.Add(UsedItem.ItemInstanceId, Record);
	if (Definition->Kind == ECatEquipmentKind::Rod)
	{
		Snapshot.RodDefinitionId = UsedItem.DefinitionId;
		Snapshot.RodItemInstanceId = UsedItem.ItemInstanceId;
		Snapshot.RodDurability = UsedItem.RodDurability;
		Snapshot.bRodBroken = UsedItem.bRodBroken;
	}
	++Snapshot.Revision;
	InventoryItemUseRecords.FindChecked(UsedItem.ItemInstanceId).UseRevision = Snapshot.Revision;
	PublishSnapshot();
	Result.Item = UsedItem;
	Result.EquipmentRevision = Snapshot.Revision;
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	return Finish(Result);
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
	Result.Item = Transfer.Item;
	return Result;
}

// 库存整理流程：
// 1. 先用 RequestId、Revision 和源/目标下标查询终态缓存，合法重放必须返回首次结果，不受当前 Fishing 阶段影响。
// 2. 首次请求先检查 authority、RequestId、Revision 和槽位下标，避免陈旧 UI 改写新的随身库存快照。
// 3. 源格必须有物品，目标格移动/合并/交换复用运行库存格通用规则，不读取物品 Use 或 Fishing 会话状态。
// 4. 成功移动后推进 Revision 并发布同一份库存快照；View 只通过 OnSnapshotChanged 重刷。
FCatDomainCommandResult UCatEquipmentComponent::MoveInventorySlotFromAuthority(const FGuid RequestId,
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
			Result.Revision = Snapshot.Revision;
			return Result;
		}
		Result = *Cached;
		MarkCommandReplayed(Result);
		return Result;
	}
	if (!GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid()
		|| SourceSlotIndex < 0 || TargetSlotIndex < 0 || SourceSlotIndex == TargetSlotIndex)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (Snapshot.Revision != ExpectedRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
	}
	else
	{
		EnsureInventorySlotArray();
		if (!Snapshot.InventorySlots.IsValidIndex(SourceSlotIndex)
			|| !Snapshot.InventorySlots.IsValidIndex(TargetSlotIndex))
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
		}
		else
		{
			const auto ResolveStackLimit = [this](const FName DefinitionId)
			{
				const UCatEquipmentDefinition* Definition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(
					DefinitionId);
				return Definition ? GetInventoryStackLimit(*Definition) : 1;
			};
			const CatRunInventorySlotOperations::FMoveSlotsResult MoveResult =
				CatRunInventorySlotOperations::MoveItemBetweenSlots(
					Snapshot.InventorySlots, SourceSlotIndex, TargetSlotIndex, ResolveStackLimit);
			Result.bCommitted = MoveResult.bChanged;
			Result.Error = MoveResult.Error;
		}
	}
	if (Result.bCommitted)
	{
		++Snapshot.Revision;
		PublishSnapshot();
	}
	Result.Revision = Snapshot.Revision;
	TerminalCache.Add(Key, Result);
	TerminalPayloadByKey.Add(Key, PayloadSignature);
	return Result;
}

// 失败预算流程：先重放完整终态并校验 authority/Revision；部署中的物品不允许被这条预算旁路改写，None 不写物资，丢饵只扣特殊饵一份，伤竿只扣显式耐久并可断竿。
FCatFishingFailureResult UCatEquipmentComponent::CommitFishingFailure(const FGuid RequestId,
	const int64 ExpectedRevision, const ECatFishingFailurePenalty Penalty)
{
	FCatFishingFailureResult Result;
	Result.Command.RequestId = RequestId;
	if (HasActiveFishingUse() || HasActiveInventoryItemUse())
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPhase;
		Result.Command.Revision = Snapshot.Revision;
		Result.Penalty = Penalty;
		Result.RemainingRodDurability = Snapshot.RodDurability;
		return Result;
	}
	if (const FCatFishingFailureResult* Cached = FailureTerminalCache.Find(RequestId))
	{
		Result = *Cached;
		Result.Command.bCommitted = false;
		Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	if (!GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid())
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (Snapshot.Revision != ExpectedRevision)
	{
		Result.Command.Error = ECatDomainCommandError::RevisionConflict;
	}
	else if (Penalty == ECatFishingFailurePenalty::None)
	{
		Result.Command.bCommitted = true;
		Result.Command.Error = ECatDomainCommandError::None;
	}
	else if (Penalty == ECatFishingFailurePenalty::LoseSpecialBait)
	{
		UCatEquipmentDefinition* Bait = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(Snapshot.BaitDefinitionId);
		const FCatRunInventorySlot* BaitSlot = FindInventorySlotByInstanceId(Snapshot.BaitItemInstanceId);
		if (!Bait || !Bait->bSpecialBait || !BaitSlot
			|| BaitSlot->DefinitionId != Snapshot.BaitDefinitionId || BaitSlot->Quantity <= 0)
		{
			Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
		}
		else
		{
			FCatRunInventorySlot LostBait;
			if (RemoveInventoryItemQuantityFromInstance(Snapshot.BaitItemInstanceId, 1, LostBait))
			{
				++Snapshot.Revision;
				Result.Command.bCommitted = true;
				Result.Command.Error = ECatDomainCommandError::None;
			}
			else
			{
				Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
			}
		}
	}
	else if (Penalty == ECatFishingFailurePenalty::DamageRod)
	{
		const double Loss = GetDefault<UCatEquipmentSettings>()->RodFailureDurabilityLoss;
		if (!FMath::IsFinite(Loss) || Loss <= 0.0 || Snapshot.RodDefinitionId.IsNone() || Snapshot.bRodBroken)
		{
			Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
		}
		else
		{
			Snapshot.RodDurability = FMath::Max(0.0, Snapshot.RodDurability - Loss);
			Snapshot.bRodBroken = Snapshot.RodDurability <= 0.0;
			SyncSelectedRodStateToSelectedInstance();
			++Snapshot.Revision;
			Result.Command.bCommitted = true;
			Result.Command.Error = ECatDomainCommandError::None;
		}
	}
	else
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
	}
	Result.Penalty = Penalty;
	Result.RemainingRodDurability = Snapshot.RodDurability;
	Result.Command.Revision = Snapshot.Revision;
	if (Result.Command.bCommitted)
	{
		PublishSnapshot();
	}
	FailureTerminalCache.Add(RequestId, Result);
	return Result;
}

FCatFishingUseReservationResult UCatEquipmentComponent::BeginFishingUse(const FGuid FishingSessionId,
	const FGuid RodItemInstanceId, const FGuid BaitItemInstanceId, const FGuid FloatItemInstanceId,
	const FName RodDefinitionId, const FName BaitDefinitionId, const FName FloatDefinitionId,
	const int64 ExpectedRevision, UCatEquipmentComponent* RodEquipment,
	const int64 ExpectedRodEquipmentRevision)
{
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
	const FCatRunInventorySlot* BaitSlot = FindInventorySlotByInstanceId(BaitItemInstanceId);
	const FCatRunInventorySlot* FloatSlot = FindInventorySlotByInstanceId(FloatItemInstanceId);
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
	OutDurability = 0.0;
	OutBroken = false;
	const FCatFishingUseRecord* Record = FindFishingUseRecord(FishingSessionId);
	const FCatRunInventorySlot* RodItem = Record ? FindFishingRodInstance(*Record) : nullptr;
	if (!RodItem || !FMath::IsFinite(RodItem->RodDurability) || RodItem->RodDurability < 0.0) return false;
	OutDurability = RodItem->RodDurability;
	OutBroken = RodItem->bRodBroken || RodItem->RodDurability <= 0.0;
	return true;
}

UCatEquipmentComponent* UCatEquipmentComponent::GetFishingRodEquipment(const FGuid FishingSessionId) const
{
	const FCatFishingUseRecord* Record = FindFishingUseRecord(FishingSessionId);
	UCatEquipmentComponent* RodEquipment = Record ? Record->RodEquipment.Get() : nullptr;
	return RodEquipment && !RodEquipment->bEndingPlay ? RodEquipment : nullptr;
}

FCatFishingUseOperationResult UCatEquipmentComponent::ReleaseFishingUse(const FGuid FishingSessionId)
{
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
		{
			// 这是归还 Begin 暂存物，不是普通入库；背包被玩家填满时追加返还格，避免终态会话卡住或吞掉鱼饵。
			Snapshot.InventorySlots.Add(CatRunInventorySlotOperations::MakeInventoryItemSlot(*Bait,
				RestoredDefinitionId, 1));
		}
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

// 维修流程：验证固定营地事实、Revision、当前 Rod/浮木定义和库存；鱼竿正在 Use 时拒绝，成功只扣一份浮木并恢复同一实例耐久。
FCatDomainCommandResult UCatEquipmentComponent::RepairRodAtCamp(const FGuid RequestId, const int64 ExpectedRevision,
	const bool bAtCamp)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const double DurabilityBefore = Snapshot.RodDurability;
	const auto ReportRepairResult = [&]()
	{
		const FString Diagnostic = FString::Printf(
			TEXT("Event=equipment_rod_repair_result RequestId=%s RodItemInstanceId=%s DurabilityBefore=%.3f Durability=%.3f Broken=%s Committed=%s Error=%s Revision=%lld World=%s NetMode=%d Authority=%s Owner=%s"),
			*RequestId.ToString(), *Snapshot.RodItemInstanceId.ToString(), DurabilityBefore,
			Snapshot.RodDurability, Snapshot.bRodBroken ? TEXT("true") : TEXT("false"),
			Result.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Error),
			Snapshot.Revision, *GetNameSafe(GetWorld()),
			static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone),
			GetOwner() && GetOwner()->HasAuthority() ? TEXT("true") : TEXT("false"), *GetNameSafe(GetOwner()));
		if (Result.bCommitted) { UE_LOG(LogCatEquipment, Log, TEXT("%s"), *Diagnostic); }
		else { UE_LOG(LogCatEquipment, Warning, TEXT("%s"), *Diagnostic); }
	};
	if (HasActiveFishingUse() || HasActiveInventoryItemUse())
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
		Result.Revision = Snapshot.Revision;
		ReportRepairResult();
		return Result;
	}
	const FString Key = MakeTerminalKey(TEXT("RepairRod"), RequestId);
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		Result = *Cached;
		Result.bCommitted = false;
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	UCatEquipmentDefinition* Rod = Settings->FindRuntimeDefinition(Snapshot.RodDefinitionId);
	UCatEquipmentDefinition* Driftwood = Settings->FindRuntimeDefinition(Settings->DriftwoodDefinitionId);
	const FCatRunInventorySlot* DriftwoodSlot = FindFirstInventorySlotByDefinition(Settings->DriftwoodDefinitionId);
	if (!bAtCamp || !GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid() || !Rod || !Driftwood
		|| Driftwood->Kind != ECatEquipmentKind::Driftwood || !DriftwoodSlot || DriftwoodSlot->Quantity <= 0)
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
	}
	else if (Snapshot.Revision != ExpectedRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
	}
	else
	{
		FCatRunInventorySlot ConsumedDriftwood;
		if (RemoveInventoryItemQuantityFromInstance(DriftwoodSlot->ItemInstanceId, 1, ConsumedDriftwood))
		{
			Snapshot.RodDurability = Rod->MaximumRodDurability;
			Snapshot.bRodBroken = false;
			SyncSelectedRodStateToSelectedInstance();
			++Snapshot.Revision;
			PublishSnapshot();
			Result.bCommitted = true;
			Result.Error = ECatDomainCommandError::None;
		}
		else
		{
			Result.Error = ECatDomainCommandError::PolicyUndecided;
		}
	}
	Result.Revision = Snapshot.Revision;
	TerminalCache.Add(Key, Result);
	ReportRepairResult();
	return Result;
}

// Snapshot 复制回调流程：客户端只刷新只读表现；不会自动装备、补充普通饵数量或修复断竿。
void UCatEquipmentComponent::OnRep_Snapshot()
{
	if (LastLoggedScoopNetDefinitionId != Snapshot.ScoopNetDefinitionId
		|| LastLoggedScoopNetItemInstanceId != Snapshot.ScoopNetItemInstanceId)
	{
		const APawn* Pawn = Cast<APawn>(GetOwner());
		UE_LOG(LogCatEquipment, Log,
			TEXT("Event=equipment_scoop_selection_replicated Definition=%s ScoopNetItemInstanceId=%s Revision=%lld World=%s NetMode=%d Authority=%s LocalRole=%d Owner=%s PlayerState=%s StableNetId=%s"),
			*Snapshot.ScoopNetDefinitionId.ToString(), *Snapshot.ScoopNetItemInstanceId.ToString(), Snapshot.Revision,
			*GetPathNameSafe(GetWorld()), static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone),
			GetOwner() && GetOwner()->HasAuthority() ? TEXT("true") : TEXT("false"),
			GetOwner() ? static_cast<int32>(GetOwner()->GetLocalRole()) : 0, *GetNameSafe(GetOwner()),
			*GetNameSafe(Pawn ? Pawn->GetPlayerState() : nullptr),
			*CatLogContext::BuildStableNetIdValue(Pawn ? Pawn->GetPlayerState() : nullptr));
		LastLoggedScoopNetDefinitionId = Snapshot.ScoopNetDefinitionId;
		LastLoggedScoopNetItemInstanceId = Snapshot.ScoopNetItemInstanceId;
	}
	const int32 DurabilityBand = FMath::IsFinite(Snapshot.RodDurability)
		? FMath::FloorToInt(Snapshot.RodDurability / 5.0) : INDEX_NONE;
	if (Snapshot.RodItemInstanceId.IsValid()
		&& (LastLoggedRodInstanceId != Snapshot.RodItemInstanceId
			|| LastLoggedRodDurabilityBand != DurabilityBand || bLastLoggedRodBroken != Snapshot.bRodBroken))
	{
		UE_LOG(LogCatEquipment, Log,
			TEXT("Event=equipment_rod_durability_replicated RodItemInstanceId=%s Definition=%s Durability=%.3f Broken=%s Revision=%lld World=%s NetMode=%d Authority=%s LocalRole=%d Owner=%s"),
			*Snapshot.RodItemInstanceId.ToString(), *Snapshot.RodDefinitionId.ToString(), Snapshot.RodDurability,
			Snapshot.bRodBroken ? TEXT("true") : TEXT("false"), Snapshot.Revision, *GetNameSafe(GetWorld()),
			static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone), GetOwner() && GetOwner()->HasAuthority() ? TEXT("true") : TEXT("false"),
			GetOwner() ? static_cast<int32>(GetOwner()->GetLocalRole()) : 0, *GetNameSafe(GetOwner()));
	}
	LastLoggedRodInstanceId = Snapshot.RodItemInstanceId;
	LastLoggedRodDurabilityBand = DurabilityBand;
	bLastLoggedRodBroken = Snapshot.bRodBroken;
	OnSnapshotChanged.Broadcast();
}

// 库存容量读取流程：配置是本局随身库存可见格子的来源；负数由属性 Clamp 防住，这里仍做运行期保护。
int32 UCatEquipmentComponent::GetConfiguredInventorySlotCapacity() const
{
	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	return Settings ? FMath::Max(0, Settings->InventorySlotCapacity) : 0;
}

// 单格堆叠读取流程：定义资产可直接声明单格上限；未声明时，非数量型物品一格一件，数量型物品沿用项目默认上限。
int32 UCatEquipmentComponent::GetInventoryStackLimit(const UCatEquipmentDefinition& Definition) const
{
	if (Definition.MaxStackSize > 0)
	{
		return FMath::Max(1, Definition.MaxStackSize);
	}
	if (!Definition.bRunConsumable)
	{
		return 1;
	}
	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	const int32 ConfiguredLimit = Settings ? Settings->InventoryQuantityStackCapacity : 0;
	return ConfiguredLimit > 0 ? ConfiguredLimit : MAX_int32;
}

// 入库容量检查流程：
// 1. 先用现有数组长度和配置容量得到本次可用格范围，不在 const 预检里补空格。
// 2. 同定义未满格先吸收数量，再把现有空格和配置补出的空格当作可用目标。
// 3. Remaining 归零才说明整批物品可以一次性提交，调用方不会做半批授予。
bool UCatEquipmentComponent::CanStoreInventoryItem(const UCatEquipmentDefinition& Definition,
	const FName DefinitionId, const int32 Quantity) const
{
	if (DefinitionId.IsNone() || Quantity <= 0)
	{
		return false;
	}
	const int32 StackLimit = GetInventoryStackLimit(Definition);
	if (StackLimit <= 0)
	{
		return false;
	}
	const int32 EffectiveSlotCount = FMath::Max(GetConfiguredInventorySlotCapacity(), Snapshot.InventorySlots.Num());
	int32 Remaining = Quantity;
	for (const FCatRunInventorySlot& Slot : Snapshot.InventorySlots)
	{
		if (Slot.DefinitionId == DefinitionId && Slot.Quantity > 0 && Slot.Quantity < StackLimit)
		{
			Remaining -= FMath::Min(Remaining, StackLimit - Slot.Quantity);
			if (Remaining <= 0)
			{
				return true;
			}
		}
	}
	int32 EmptySlotCount = FMath::Max(0, EffectiveSlotCount - Snapshot.InventorySlots.Num());
	for (const FCatRunInventorySlot& Slot : Snapshot.InventorySlots)
	{
		if (!CatRunInventorySlotOperations::IsInventorySlotOccupied(Slot))
		{
			++EmptySlotCount;
		}
	}
	for (int32 SlotIndex = 0; SlotIndex < EmptySlotCount && Remaining > 0; ++SlotIndex)
	{
		Remaining -= FMath::Min(Remaining, StackLimit);
	}
	return Remaining <= 0;
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
	// 1. 只遍历当前随身库存事实，不改活动 Use 记录和选择快照。
	// 2. 有内容的格子交给定义归一化，给旧数据补实例身份并补齐鱼竿状态。
	// 3. 空格清回默认值，避免残留实例 ID 让 Use 误以为还有物品。
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

// 入库写入流程：
// 1. 复用预检保证不会半写入；随后补齐配置容量内的空格。
// 2. 同定义未满格先合并，并补齐该堆栈的实例身份；剩余数量再按定义创建新的运行期实例落到空格。
// 3. 写完只改变 InventorySlots；所有读者都从这份数组重新汇总自己需要的数量和实例身份。
bool UCatEquipmentComponent::AddInventoryItemQuantity(const UCatEquipmentDefinition& Definition,
	const FName DefinitionId, const int32 Quantity)
{
	if (!CanStoreInventoryItem(Definition, DefinitionId, Quantity))
	{
		return false;
	}
	EnsureInventorySlotArray();
	const int32 StackLimit = GetInventoryStackLimit(Definition);
	int32 Remaining = Quantity;
	for (FCatRunInventorySlot& Slot : Snapshot.InventorySlots)
	{
		if (Slot.DefinitionId == DefinitionId && Slot.Quantity > 0 && Slot.Quantity < StackLimit)
		{
			CatRunInventorySlotOperations::NormalizeStoredItemSlot(Slot, Definition);
			const int32 Added = FMath::Min(Remaining, StackLimit - Slot.Quantity);
			Slot.Quantity += Added;
			Remaining -= Added;
			if (Remaining <= 0)
			{
				return true;
			}
		}
	}
	for (FCatRunInventorySlot& Slot : Snapshot.InventorySlots)
	{
		if (!CatRunInventorySlotOperations::IsInventorySlotOccupied(Slot))
		{
			const int32 Added = FMath::Min(Remaining, StackLimit);
			Slot = CatRunInventorySlotOperations::MakeInventoryItemSlot(Definition, DefinitionId, Added);
			Remaining -= Added;
			if (Remaining <= 0)
			{
				return true;
			}
		}
	}
	return false;
}


bool UCatEquipmentComponent::RemoveInventoryItemQuantityFromInstance(const FGuid ItemInstanceId,
	const int32 Quantity, FCatRunInventorySlot& OutConsumedItem)
{
	// 指定实例扣量流程：
	// 1. 先要求有效实例和正数量，输出始终先清空，避免失败时调用方误用上次结果。
	// 2. 再只按 ItemInstanceId 找到目标数量栈，不因为 DefinitionId 相同就扣别的格子。
	// 3. 成功时 OutConsumedItem 只代表本次消耗的份数；原格剩余数量归零才清空。
	// 4. 如果清空的是当前选中鱼饵实例，就立刻改选同定义的剩余堆栈，避免选择指向空格。
	OutConsumedItem = FCatRunInventorySlot();
	if (!ItemInstanceId.IsValid() || Quantity <= 0)
	{
		return false;
	}
	for (FCatRunInventorySlot& Slot : Snapshot.InventorySlots)
	{
		if (Slot.ItemInstanceId != ItemInstanceId || !CatRunInventorySlotOperations::IsInventorySlotOccupied(Slot))
		{
			continue;
		}
		if (Slot.Quantity < Quantity)
		{
			return false;
		}
		OutConsumedItem = Slot;
		OutConsumedItem.Quantity = Quantity;
		Slot.Quantity -= Quantity;
		const FName ConsumedDefinitionId = OutConsumedItem.DefinitionId;
		if (Slot.Quantity <= 0)
		{
			Slot = FCatRunInventorySlot();
		}
		if (Snapshot.BaitItemInstanceId == ItemInstanceId
			&& !FindInventorySlotByInstanceId(Snapshot.BaitItemInstanceId))
		{
			const FCatRunInventorySlot* ReplacementBaitSlot =
				FindFirstInventorySlotByDefinition(ConsumedDefinitionId);
			Snapshot.BaitItemInstanceId = ReplacementBaitSlot ? ReplacementBaitSlot->ItemInstanceId : FGuid();
		}
		return true;
	}
	return false;
}

bool UCatEquipmentComponent::RemoveInventoryItemInstance(const FGuid ItemInstanceId, FCatRunInventorySlot& OutItem)
{
	// 实例移出流程：只按 ItemInstanceId 命中一格，成功后把完整实例副本交给调用方；这一步不会按 DefinitionId 误扣同类物品。
	OutItem = FCatRunInventorySlot();
	if (!ItemInstanceId.IsValid())
	{
		return false;
	}
	for (FCatRunInventorySlot& Slot : Snapshot.InventorySlots)
	{
		if (Slot.ItemInstanceId == ItemInstanceId && CatRunInventorySlotOperations::IsInventorySlotOccupied(Slot))
		{
			OutItem = Slot;
			Slot = FCatRunInventorySlot();
			return true;
		}
	}
	return false;
}

UCatEquipmentComponent::FCatFishingUseRecord* UCatEquipmentComponent::FindFishingUseRecord(const FGuid FishingSessionId)
{
	return FishingUseRecords.Find(FishingSessionId);
}

const UCatEquipmentComponent::FCatFishingUseRecord* UCatEquipmentComponent::FindFishingUseRecord(const FGuid FishingSessionId) const
{
	return FishingUseRecords.Find(FishingSessionId);
}

FCatRunInventorySlot* UCatEquipmentComponent::FindFishingRodInstance(const FCatFishingUseRecord& Record)
{
	UCatEquipmentComponent* RodEquipment = Record.RodEquipment.Get();
	if (!RodEquipment || RodEquipment->bEndingPlay || !RodEquipment->GetOwner()
		|| !RodEquipment->GetOwner()->HasAuthority() || RodEquipment->GetWorld() != GetWorld()) return nullptr;
	FCatInventoryItemUseRecord* UseRecord = RodEquipment->FindInventoryItemUseRecord(Record.RodItemInstanceId);
	if (!Record.bReleased && (!UseRecord || UseRecord->bReleased
		|| UseRecord->BoundFishingSessionId != Record.SessionId || UseRecord->FishingUseCoordinator.Get() != this)) return nullptr;
	FCatRunInventorySlot* Item = RodEquipment->FindInventorySlotByInstanceId(Record.RodItemInstanceId);
	if (!Item)
	{
		Item = UseRecord && !UseRecord->bReleased ? &UseRecord->Item : nullptr;
	}
	return Item && Item->ItemInstanceId == Record.RodItemInstanceId
		&& Item->DefinitionId == Record.RodDefinitionId && Item->Quantity == 1 ? Item : nullptr;
}

const FCatRunInventorySlot* UCatEquipmentComponent::FindFishingRodInstance(const FCatFishingUseRecord& Record) const
{
	const UCatEquipmentComponent* RodEquipment = Record.RodEquipment.Get();
	if (!RodEquipment || RodEquipment->bEndingPlay || !RodEquipment->GetOwner()
		|| !RodEquipment->GetOwner()->HasAuthority() || RodEquipment->GetWorld() != GetWorld()) return nullptr;
	const FCatInventoryItemUseRecord* UseRecord = RodEquipment->FindInventoryItemUseRecord(Record.RodItemInstanceId);
	if (!Record.bReleased && (!UseRecord || UseRecord->bReleased
		|| UseRecord->BoundFishingSessionId != Record.SessionId || UseRecord->FishingUseCoordinator.Get() != this)) return nullptr;
	const FCatRunInventorySlot* Item = RodEquipment->FindInventorySlotByInstanceId(Record.RodItemInstanceId);
	if (!Item)
	{
		Item = UseRecord && !UseRecord->bReleased ? &UseRecord->Item : nullptr;
	}
	return Item && Item->ItemInstanceId == Record.RodItemInstanceId
		&& Item->DefinitionId == Record.RodDefinitionId && Item->Quantity == 1 ? Item : nullptr;
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
	// 活动物品使用 gate 流程：只要存在尚未 Released 的部署型实例记录，就认为场景正持有物品状态；维修和失败预算必须等收口后再改写它。
	for (const TPair<FGuid, FCatInventoryItemUseRecord>& Pair : InventoryItemUseRecords)
	{
		if (Pair.Key.IsValid() && !Pair.Value.bReleased)
		{
			return true;
		}
	}
	return false;
}

// 库存数量读取流程：按定义 ID 汇总当前随身库存格数组；None、空格和非正数量都统一视为没有可用实物。
int32 UCatEquipmentComponent::GetInventoryItemQuantity(const FName DefinitionId) const
{
	if (DefinitionId.IsNone())
	{
		return 0;
	}
	int32 Quantity = 0;
	for (const FCatRunInventorySlot& Slot : Snapshot.InventorySlots)
	{
		if (Slot.DefinitionId == DefinitionId && Slot.Quantity > 0)
		{
			Quantity += Slot.Quantity;
		}
	}
	return Quantity;
}

FCatRunInventorySlot* UCatEquipmentComponent::FindInventorySlotByInstanceId(const FGuid ItemInstanceId)
{
	// 实例格查找流程：先拒绝空 GUID，再只在有内容的随身库存格中匹配，防止空格残留字段被当成真实物品。
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
	// 实例格只读查找流程：和可写版本保持同一命中口径，供选择解析、预检和诊断读取库存里的真实实例。
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
	// 按定义回退到实例的流程：旧 UI 没有实例 ID 时仍要落到库存里真实存在的一份物品，不能让上层只拿 DefinitionId 改状态。
	// 1. 旧 UI 或定义型选择路径只给 DefinitionId 时，先在库存里记住第一份同定义实物。
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
	// 1. 当前选择快照仍服务 UI 与老调用方，但真正实例可能在背包格里，也可能已经被 Use 移到活动记录里。
	// 2. 如果实例还在库存中，直接写回该格的耐久和断竿状态。
	// 3. 如果实例已经部署到场景，只更新活动记录里的副本，等 UnUse 时再原样归还。
	// 4. 已收口记录不再改写，避免收杆后的历史记录影响新的库存事实。
	if (!Snapshot.RodItemInstanceId.IsValid())
	{
		return;
	}
	if (FCatRunInventorySlot* StoredSlot = FindInventorySlotByInstanceId(Snapshot.RodItemInstanceId))
	{
		StoredSlot->RodDurability = Snapshot.RodDurability;
		StoredSlot->bRodBroken = Snapshot.bRodBroken;
		return;
	}
	if (FCatInventoryItemUseRecord* ActiveUse = FindInventoryItemUseRecord(Snapshot.RodItemInstanceId))
	{
		if (!ActiveUse->bReleased)
		{
			ActiveUse->Item.RodDurability = Snapshot.RodDurability;
			ActiveUse->Item.bRodBroken = Snapshot.bRodBroken;
		}
	}
}

// 自动选择流程：
// 1. 已部署鱼竿保持实例选择，等待 UnUse 归还；库存里的健康已选竿也不被新获得的竿抢占。
// 2. 旧竿缺失或不可用时，优先选本次入库型号的可用实例，再查其他型号；收回坏竿也复用此规则。
// 3. 这个流程不移出库存物品，也不创建独立装备栏；Fishing Begin 会按当前选择自行暂存要消耗的那一份饵。
void UCatEquipmentComponent::AutoSelectGrantedInventoryItem(const UCatEquipmentDefinition& Definition,
	const FName DefinitionId)
{
	if (DefinitionId.IsNone())
	{
		return;
	}
	if (Definition.Kind == ECatEquipmentKind::Rod)
	{
		const FCatInventoryItemUseRecord* ActiveSelectedRod =
			FindInventoryItemUseRecord(Snapshot.RodItemInstanceId);
		if (ActiveSelectedRod && !ActiveSelectedRod->bReleased)
		{
			return;
		}
		const FCatRunInventorySlot* SelectedSlot = FindInventorySlotByInstanceId(Snapshot.RodItemInstanceId);
		const auto IsUsableRod = [](const FCatRunInventorySlot& Slot)
		{
			return Slot.ItemInstanceId.IsValid() && Slot.Quantity == 1 && !Slot.bRodBroken
				&& FMath::IsFinite(Slot.RodDurability) && Slot.RodDurability > 0.0;
		};
		if (SelectedSlot && SelectedSlot->DefinitionId == Snapshot.RodDefinitionId && IsUsableRod(*SelectedSlot))
		{
			return;
		}
		const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
		const FCatRunInventorySlot* GrantedSlot = nullptr;
		const FCatRunInventorySlot* FallbackSlot = nullptr;
		for (const FCatRunInventorySlot& Slot : Snapshot.InventorySlots)
		{
			if (!IsUsableRod(Slot))
			{
				continue;
			}
			const UCatEquipmentDefinition* CandidateDefinition = Settings->FindRuntimeDefinition(Slot.DefinitionId);
			if (!CandidateDefinition || CandidateDefinition->Kind != ECatEquipmentKind::Rod
				|| CandidateDefinition->Use(Slot, 1) != ECatDomainCommandError::None)
			{
				continue;
			}
			if (Slot.DefinitionId == DefinitionId)
			{
				GrantedSlot = &Slot;
				break;
			}
			if (!FallbackSlot)
			{
				FallbackSlot = &Slot;
			}
		}
		GrantedSlot = GrantedSlot ? GrantedSlot : FallbackSlot;
		if (!GrantedSlot)
		{
			return;
		}
		const FName PreviousDefinition = Snapshot.RodDefinitionId;
		const FGuid PreviousInstance = Snapshot.RodItemInstanceId;
		Snapshot.RodDefinitionId = GrantedSlot->DefinitionId;
		Snapshot.RodItemInstanceId = GrantedSlot->ItemInstanceId;
		Snapshot.RodDurability = GrantedSlot->RodDurability;
		Snapshot.bRodBroken = GrantedSlot->bRodBroken;
		const APawn* OwnerPawn = Cast<APawn>(GetOwner());
		const APlayerState* PlayerState = OwnerPawn ? OwnerPawn->GetPlayerState() : nullptr;
		UE_LOG(LogCatEquipment, Log,
			TEXT("Event=equipment_rod_auto_selected Reason=%s PreviousDefinition=%s PreviousRodItemInstanceId=%s Definition=%s RodItemInstanceId=%s Durability=%.3f RevisionBefore=%lld World=%s NetMode=%d Authority=%s LocalRole=%d Owner=%s PlayerId=%d Result=Selected"),
			SelectedSlot ? TEXT("PreviousRodUnusable") : TEXT("PreviousRodMissing"),
			*PreviousDefinition.ToString(), *PreviousInstance.ToString(), *Snapshot.RodDefinitionId.ToString(),
			*Snapshot.RodItemInstanceId.ToString(), Snapshot.RodDurability, Snapshot.Revision, *GetNameSafe(GetWorld()),
			static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone),
			GetOwner() && GetOwner()->HasAuthority() ? TEXT("true") : TEXT("false"),
			GetOwner() ? static_cast<int32>(GetOwner()->GetLocalRole()) : 0, *GetNameSafe(GetOwner()),
			PlayerState ? PlayerState->GetPlayerId() : INDEX_NONE);
		return;
	}
	if (Definition.Kind == ECatEquipmentKind::Bait)
	{
		// 入库已发生，不能用同定义总数量判断旧选择是否有效：耗尽后补回同种饵时总量为正，但实例已清空。
		const FCatRunInventorySlot* SelectedSlot = FindInventorySlotByInstanceId(Snapshot.BaitItemInstanceId);
		if (SelectedSlot && SelectedSlot->DefinitionId == Snapshot.BaitDefinitionId && SelectedSlot->Quantity > 0)
		{
			return;
		}
		// 优先恢复玩家已选种类的剩余堆栈；该种类确实没有库存时，才选择本次补给的种类。
		const FCatRunInventorySlot* ReplacementSlot = FindFirstInventorySlotByDefinition(Snapshot.BaitDefinitionId);
		if (!ReplacementSlot) ReplacementSlot = FindFirstInventorySlotByDefinition(DefinitionId);
		if (!ReplacementSlot) return;
		const FName PreviousDefinition = Snapshot.BaitDefinitionId;
		const FGuid PreviousInstance = Snapshot.BaitItemInstanceId;
		Snapshot.BaitDefinitionId = ReplacementSlot->DefinitionId;
		Snapshot.BaitItemInstanceId = ReplacementSlot->ItemInstanceId;
		const APawn* Pawn = Cast<APawn>(GetOwner());
		UE_LOG(LogCatEquipment, Log,
			TEXT("Event=equipment_bait_auto_selected Reason=PreviousSelectionUnavailable PreviousDefinition=%s PreviousBaitItemInstanceId=%s BaitDefinition=%s BaitItemInstanceId=%s Quantity=%d RevisionBefore=%lld World=%s NetMode=%d Authority=%d LocalRole=%d Owner=%s PlayerState=%s Result=Selected"),
			*PreviousDefinition.ToString(), *PreviousInstance.ToString(), *Snapshot.BaitDefinitionId.ToString(),
			*Snapshot.BaitItemInstanceId.ToString(), ReplacementSlot->Quantity, Snapshot.Revision,
			*GetNameSafe(GetWorld()), static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone),
			GetOwner() && GetOwner()->HasAuthority(), GetOwner() ? static_cast<int32>(GetOwner()->GetLocalRole()) : 0,
			*GetNameSafe(GetOwner()), *GetNameSafe(Pawn ? Pawn->GetPlayerState() : nullptr));
		return;
	}
	if (Definition.Kind == ECatEquipmentKind::Float)
	{
		const FCatRunInventorySlot* SelectedSlot = FindInventorySlotByInstanceId(Snapshot.FloatItemInstanceId);
		if (SelectedSlot && SelectedSlot->DefinitionId == Snapshot.FloatDefinitionId && SelectedSlot->Quantity > 0)
		{
			return;
		}
		// 同定义仍有数量并不代表原实例仍在；先恢复原种类的另一份实物，再考虑本次入库种类。
		const FCatRunInventorySlot* ReplacementSlot = FindFirstInventorySlotByDefinition(Snapshot.FloatDefinitionId);
		if (!ReplacementSlot) ReplacementSlot = FindFirstInventorySlotByDefinition(DefinitionId);
		if (!ReplacementSlot) return;
		const FName PreviousDefinition = Snapshot.FloatDefinitionId;
		const FGuid PreviousInstance = Snapshot.FloatItemInstanceId;
		Snapshot.FloatDefinitionId = ReplacementSlot->DefinitionId;
		Snapshot.FloatItemInstanceId = ReplacementSlot->ItemInstanceId;
		const APawn* Pawn = Cast<APawn>(GetOwner());
		UE_LOG(LogCatEquipment, Log,
			TEXT("Event=equipment_float_auto_selected Reason=PreviousSelectionUnavailable PreviousDefinition=%s PreviousFloatItemInstanceId=%s FloatDefinition=%s FloatItemInstanceId=%s RevisionBefore=%lld World=%s NetMode=%d Authority=%d LocalRole=%d Owner=%s PlayerState=%s Result=Selected"),
			*PreviousDefinition.ToString(), *PreviousInstance.ToString(), *Snapshot.FloatDefinitionId.ToString(),
			*Snapshot.FloatItemInstanceId.ToString(), Snapshot.Revision, *GetNameSafe(GetWorld()),
			static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone),
			GetOwner() && GetOwner()->HasAuthority(), GetOwner() ? static_cast<int32>(GetOwner()->GetLocalRole()) : 0,
			*GetNameSafe(GetOwner()), *GetNameSafe(Pawn ? Pawn->GetPlayerState() : nullptr));
		return;
	}
	if (Definition.Kind == ECatEquipmentKind::ScoopNet)
	{
		const FCatRunInventorySlot* SelectedSlot = FindInventorySlotByInstanceId(Snapshot.ScoopNetItemInstanceId);
		if (SelectedSlot && SelectedSlot->DefinitionId == Snapshot.ScoopNetDefinitionId && SelectedSlot->Quantity > 0)
		{
			return;
		}
		const FCatRunInventorySlot* ReplacementSlot = FindFirstInventorySlotByDefinition(Snapshot.ScoopNetDefinitionId);
		if (!ReplacementSlot) ReplacementSlot = FindFirstInventorySlotByDefinition(DefinitionId);
		if (!ReplacementSlot) return;
		const FName PreviousDefinition = Snapshot.ScoopNetDefinitionId;
		const FGuid PreviousInstance = Snapshot.ScoopNetItemInstanceId;
		Snapshot.ScoopNetDefinitionId = ReplacementSlot->DefinitionId;
		Snapshot.ScoopNetItemInstanceId = ReplacementSlot->ItemInstanceId;
		const APawn* Pawn = Cast<APawn>(GetOwner());
		UE_LOG(LogCatEquipment, Log,
			TEXT("Event=equipment_scoop_auto_selected Reason=PreviousSelectionUnavailable PreviousDefinition=%s PreviousScoopNetItemInstanceId=%s ScoopNetDefinition=%s ScoopNetItemInstanceId=%s RevisionBefore=%lld World=%s NetMode=%d Authority=%d LocalRole=%d Owner=%s PlayerState=%s Result=Selected"),
			*PreviousDefinition.ToString(), *PreviousInstance.ToString(), *Snapshot.ScoopNetDefinitionId.ToString(),
			*Snapshot.ScoopNetItemInstanceId.ToString(), Snapshot.Revision, *GetNameSafe(GetWorld()),
			static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone),
			GetOwner() && GetOwner()->HasAuthority(), GetOwner() ? static_cast<int32>(GetOwner()->GetLocalRole()) : 0,
			*GetNameSafe(GetOwner()), *GetNameSafe(Pawn ? Pawn->GetPlayerState() : nullptr));
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

// Snapshot 发布流程：authority 提交后要求 Owner 立即复制，再向同机只读订阅者广播；订阅者只能重新读取 GetSnapshot。
void UCatEquipmentComponent::PublishSnapshot()
{
	if (AActor* Owner = GetOwner(); Owner && Owner->HasAuthority())
	{
		Owner->ForceNetUpdate();
	}
	OnSnapshotChanged.Broadcast();
}
