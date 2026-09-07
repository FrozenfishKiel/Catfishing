#include "Equipment/CatEquipmentComponent.h"

#include "Equipment/CatEquipmentInventoryItemInstance.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Equipment/CatRunInventorySlotOperations.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Inventory/CatInventoryComponent.h"
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
	// 旧随身库存投影等价判断流程：按 UI 和存档会读取的全部字段比较；只有真实物品格差异才需要推进兼容 Snapshot 版本。
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
// 4. 只有配置真正提交且窝料定义、数量均有效时，正式库存存在就按 InventoryRevision 发货并同步旧投影；没有正式库存的旧宿主才回退 Equipment 兼容入口。
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
	UE_LOG(LogCatCharacter, Log, TEXT("Event=starter_loadout_configure Committed=%s Error=%s Revision=%lld"),
		Configure.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Configure.Error), Configure.Revision);
	if (!Configure.bCommitted || Settings->StarterChumDefinitionId.IsNone() || Settings->StarterChumQuantity <= 0)
	{
		return;
	}
	const FGuid GrantRequestId = FGuid::NewGuid();
	FCatDomainCommandResult Grant;
	if (UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent())
	{
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
		Grant = GrantInventoryQuantityFromAuthority(GrantRequestId, Snapshot.Revision,
			Settings->StarterChumDefinitionId, Settings->StarterChumQuantity);
	}
	UE_LOG(LogCatCharacter, Log, TEXT("Event=starter_chum_grant Committed=%s Error=%s Revision=%lld Definition=%s Quantity=%d"),
		Grant.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Grant.Error), Grant.Revision,
		*Settings->StarterChumDefinitionId.ToString(), Settings->StarterChumQuantity);
}

// 持久化导出流程：
// 1. 先拒绝尚未结算的 Fishing 预留，避免把仍在会话中的饵料伪装成已提交库存。
// 2. 正式 Character 从 InventoryComponent 重建库存格；没有正式库存组件的旧宿主才沿用 Snapshot 里的迁移期投影。
// 3. 最后把成功 Use 后仍由 Equipment 暂存的完整实例合并成收回姿态，完整校验后才交给 Save。
bool UCatEquipmentComponent::ExportSnapshotFromAuthority(FCatEquipmentLoadoutSnapshot& OutSnapshot, FText& OutFailure) const
{
	OutSnapshot = FCatEquipmentLoadoutSnapshot();
	if (!GetOwner() || !GetOwner()->HasAuthority() || HasActiveFishingUse())
	{
		OutFailure = FText::FromString(TEXT("玩家库存仍有未结算 Fishing 预留，等待领域收口后才能保存。"));
		return false;
	}
	FCatEquipmentLoadoutSnapshot Candidate = Snapshot;
	if (ResolveOwnerInventoryComponent() != nullptr
		&& !BuildSnapshotInventorySlotsFromOwnerInventoryComponent(Candidate.InventorySlots))
	{
		OutFailure = FText::FromString(TEXT("正式随身库存无法转换为可保存载荷。"));
		return false;
	}
	for (const TPair<FGuid, FCatInventoryItemUseRecord>& Pair : InventoryItemUseRecords)
	{
		if (Pair.Value.bReleased)
		{
			continue;
		}
		if (Candidate.InventorySlots.ContainsByPredicate([&Pair](const FCatRunInventorySlot& Slot)
			{ return Slot.ItemInstanceId == Pair.Key; }))
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
		(Empty ? *Empty : Candidate.InventorySlots.AddDefaulted_GetRef()) = Pair.Value.Item;
	}
	if (!ValidatePersistentSnapshotPayload(Candidate, OutFailure))
	{
		return false;
	}
	OutSnapshot = MoveTemp(Candidate);
	return true;
}

// 退出部署收口流程：按真实 PlayerState 从 Fishing 查唯一部署竿，核对它仍对应本组件已提交 Use 记录，再销毁表现使 Fishing 正常注销。
// 不把实例再加回即将销毁的组件；持久化调用方已接管正式记录，销毁失败拒绝退出捕获。Destroy 会触发领域回调，因此返回后按 ID 重找使用记录并清掉正式强引用。
bool UCatEquipmentComponent::RetireDeploymentAfterPersistentCapture(APlayerState& PlayerState)
{
	UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr;
	ACatFishingRodActor* Rod = Fishing ? Fishing->FindDeployedRod(&PlayerState) : nullptr;
	if (!Rod)
	{
		return true;
	}
	const FGuid ItemInstanceId = Rod->GetPresentationState().ItemInstanceId;
	const FCatInventoryItemUseRecord* Record = InventoryItemUseRecords.Find(ItemInstanceId);
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Record || Record->bReleased || !Rod->Destroy())
	{
		UE_LOG(LogCatRun, Error, TEXT("Event=persistence_departure_deployment_rejected Owner=%s Rod=%s"),
			*GetNameSafe(GetOwner()), *GetNameSafe(Rod));
		return false;
	}
	if (FCatInventoryItemUseRecord* RemainingRecord = InventoryItemUseRecords.Find(ItemInstanceId))
	{
		RemainingRecord->bReleased = true;
	}
	ActiveFormalInventoryUseInstances.Remove(ItemInstanceId);
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
// 2. 成功后清掉只属于旧 Character 生命周期的请求缓存、活动 Use 记录和正式实例强引用。
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
	FailureTerminalCache.Reset();
	FishingUseRecords.Reset();
	InventoryItemUseRecords.Reset();
	ActiveFormalInventoryUseInstances.Reset();
	InventoryItemUseTerminalCache.Reset();
	PublishSnapshot();
	return true;
}

// 当前钓鱼选择配置流程：
// 1. 先用 RequestId 返回既有终态；部署中的当前鱼竿可继续作为选择上下文，但不能切到另一根鱼竿。
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
		const FCatInventoryItemUseRecord* ActiveSelectedRod =
			FindInventoryItemUseRecord(Snapshot.RodItemInstanceId);
		const bool bSelectedRodIsInUse = ActiveSelectedRod && !ActiveSelectedRod->bReleased
			&& ActiveSelectedRod->Item.DefinitionId == Snapshot.RodDefinitionId
			&& ActiveSelectedRod->Item.ItemInstanceId == Snapshot.RodItemInstanceId;
		const bool bRequestsActiveSelectedRod = bSelectedRodIsInUse
			&& RodDefinitionId == ActiveSelectedRod->Item.DefinitionId
			&& (!RodItemInstanceId.IsValid() || RodItemInstanceId == ActiveSelectedRod->Item.ItemInstanceId);
		if (bSelectedRodIsInUse && !bRequestsActiveSelectedRod)
		{
			Result.Error = ECatDomainCommandError::InvalidPhase;
		}
		else
		{
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
// 3. Owner 正式库存已按配置补齐时，把容量和堆叠预检交给 InventoryComponent；这里不扩容数组，保证 Validate 纯只读。
// 4. 旧测试宿主或尚未初始化正式槽位时才回看 Equipment 旧投影，避免历史入口在迁移期被错误拒绝。
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
	if (OwnerInventory != nullptr && OwnerInventory->GetInventorySlotCount() >= GetConfiguredInventorySlotCapacity())
	{
		return OwnerInventory->ValidateResolvedInventoryDefinitionGrantFromAuthority(
			RequestId, Definition, Quantity);
	}
	if (!CanStoreInventoryItem(*Definition, DefinitionId, Quantity))
	{
		return ECatDomainCommandError::CapacityExceeded;
	}
	return ECatDomainCommandError::None;
}

// 数量型库存物品入库流程：
// 1. 先拒绝无效 RequestId，并用 RequestId、定义和数量签名保护终态重放；载荷漂移直接拒绝且不改库存。
// 2. 正式库存组件存在时先按配置补齐槽位，再把已解析定义交给 InventoryComponent 的统一发货事务。
// 3. 正式库存写入成功后只从 InventoryComponent 重建 Equipment 旧投影，并按新增定义修正钓鱼选择。
// 4. 投影同步失败只记录诊断，不回滚正式库存事实；后续刷新仍以 InventoryComponent 为准。
// 5. 没有正式库存组件的旧宿主才走旧数组入库，并继续缓存首次终态供重放返回。
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
	if (UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent())
	{
		if (!GetOwner() || !GetOwner()->HasAuthority() || !Definition || !Definition->bRunConsumable
			|| Quantity <= 0)
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
		}
		else if (Snapshot.Revision != ExpectedRevision)
		{
			Result.Error = ECatDomainCommandError::RevisionConflict;
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
// 3. 正式库存已按配置补齐时使用 InventoryComponent 的已解析定义预检，让背包容量和堆叠规则只由正式库存回答。
// 4. 旧测试宿主或尚未初始化正式槽位时才回看 Equipment 旧投影；非数量物品仍按单件容量判断。
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
	if (OwnerInventory != nullptr && OwnerInventory->GetInventorySlotCount() >= GetConfiguredInventorySlotCapacity())
	{
		return OwnerInventory->ValidateResolvedInventoryDefinitionGrantFromAuthority(
			RequestId, Definition, 1);
	}
	if (!CanStoreInventoryItem(*Definition, DefinitionId, 1))
	{
		return ECatDomainCommandError::CapacityExceeded;
	}
	return ECatDomainCommandError::None;
}

// 商店非数量物品入库流程：
// 1. 先用 RequestId 和定义 ID 找终态缓存；合法重放只返回首次结果，不重复增加库存数量或推进 Revision。
// 2. 正式库存组件存在时先按配置补齐槽位，再把已解析定义交给 InventoryComponent 的统一发货事务。
// 3. 正式库存写入成功后刷新 Equipment 旧投影和钓鱼选择；投影失败只记诊断，库存事实不再反向回滚。
// 4. 没有正式库存组件的旧宿主才走旧数组入库，并保留原先的 ExpectedRevision 和终态缓存语义。
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
	if (UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent())
	{
		if (!GetOwner() || !GetOwner()->HasAuthority() || !Definition || Definition->bRunConsumable)
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
		}
		else if (Snapshot.Revision != ExpectedRevision)
		{
			Result.Error = ECatDomainCommandError::RevisionConflict;
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

// 临时抄网补给流程：
// 1. 先确认玩家 Pawn、authority、配置开关和本 Character 生命周期去重标记，避免客户端或重复占有自动刷物品。
// 2. 再记录一次诊断请求并通过库存目录校验配置定义确实是抄网，配置错误只写日志，不改随身库存。
// 3. 如果正式库存或旧投影已有任一完整抄网，就只修正缺失选择并发布必要快照，不因容量临时变小移除玩家已有物品。
// 4. 没有抄网时必须至少给基础竿、漂、饵和抄网留下四格；容量不足只记录拒绝，等容量恢复后允许再次尝试。
// 5. 最后优先让 InventoryComponent 按正式库存 Revision 发货；没有正式库存的旧宿主才回退 Equipment 兼容入口。
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

	// 正式库存已经有抄网时只刷新旧投影和选择；不依赖旧 Snapshot 才能知道玩家是否已经持有这件物品。
	if (OwnerInventory != nullptr)
	{
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
		if (PreviousDefinitionId != Snapshot.ScoopNetDefinitionId
			|| PreviousItemInstanceId != Snapshot.ScoopNetItemInstanceId)
		{
			++Snapshot.Revision;
			PublishSnapshot();
		}
		UE_LOG(LogCatEquipment, Log,
			TEXT("Event=equipment_starter_scoop_completed RequestId=%s Result=AlreadyOwned Definition=%s ScoopNetItemInstanceId=%s Revision=%lld %s"),
			*RequestId.ToString(), *Snapshot.ScoopNetDefinitionId.ToString(),
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
	if (OwnerInventory != nullptr)
	{
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
	}
	else
	{
		Grant = GrantEquipmentFromAuthority(RequestId, Snapshot.Revision, DefinitionId);
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
	const FGuid ItemInstanceId, const int32 Quantity)
{
	// 物品使用流程：
	// 1. 先校验 authority、RequestId 和数量，再用实例载荷签名处理幂等重放，避免数量消耗品重复扣量。
	// 2. 正式库存存在时按实例 ID 回到 InventoryComponent 槽位，旧槽位只作为定义裁决的只读投影。
	// 3. 部署型物品由正式库存移出整份 entry，数量消耗物由正式库存扣指定份数；失败会恢复正式 entries 和旧 Snapshot。
	// 4. 没有正式库存组件的旧宿主才回退 Snapshot 数组写入，保护历史测试夹具和临时 Actor。
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
		MarkInventoryItemUseReplayed(Result);
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
	if (UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent())
	{
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

		const TArray<FCatInventoryEntry> SavedEntries = OwnerInventory->GetInventoryEntries();
		const FCatEquipmentLoadoutSnapshot SavedSnapshot = Snapshot;
		if (Definition->ConsumesInventoryQuantityOnUse())
		{
			if (!OwnerInventory->ConsumeItemAtSlot(FormalSlotIndex, Quantity)
				|| !RefreshInventoryProjectionFromInventoryComponentFromAuthority())
			{
				OwnerInventory->ReplaceInventoryEntriesFromAuthority(
					SavedEntries, GetConfiguredInventorySlotCapacity());
				Snapshot = SavedSnapshot;
				Result.Error = ECatDomainCommandError::DependencyUnavailable;
				return Finish(Result);
			}

			Result.Item = SourceItem;
			Result.Item.Quantity = Quantity;
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

		FCatInventoryEntry RemovedEntry;
		if (!OwnerInventory->RemoveInventoryEntryAtSlotFromAuthority(FormalSlotIndex, RemovedEntry))
		{
			Result.Error = ECatDomainCommandError::NotFound;
			return Finish(Result);
		}

		UCatEquipmentInventoryItemInstance* RemovedInstance =
			Cast<UCatEquipmentInventoryItemInstance>(RemovedEntry.Instance);
		FCatInventoryItemUseRecord Record;
		Record.ItemInstanceId = SourceItem.ItemInstanceId;
		Record.Item = SourceItem;
		InventoryItemUseRecords.Add(SourceItem.ItemInstanceId, Record);
		ActiveFormalInventoryUseInstances.Add(SourceItem.ItemInstanceId, RemovedInstance);
		if (!RefreshInventoryProjectionFromInventoryComponentFromAuthority())
		{
			ActiveFormalInventoryUseInstances.Remove(SourceItem.ItemInstanceId);
			InventoryItemUseRecords.Remove(SourceItem.ItemInstanceId);
			OwnerInventory->ReplaceInventoryEntriesFromAuthority(
				SavedEntries, GetConfiguredInventorySlotCapacity());
			Snapshot = SavedSnapshot;
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
		Result.EquipmentRevision = Snapshot.Revision;
		Result.bCommitted = true;
		Result.Error = ECatDomainCommandError::None;
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

bool UCatEquipmentComponent::TryReplayInventoryItemUseTerminal(const FGuid RequestId, const int64 ExpectedRevision,
	const FGuid ItemInstanceId, const int32 Quantity, FCatInventoryItemUseResult& OutResult) const
{
	// 库存 Use 重放查询流程：
	// 1. 先复原 Use 使用的终态键和载荷签名，不读取当前库存格或定义，避免成功扣除后的空格阻断二段提交。
	// 2. 没有缓存返回 false，调用方继续执行首次提交 preflight；载荷漂移返回 true+InvalidPayload，阻止同 RequestId 改目标。
	// 3. 命中缓存时返回 MarkInventoryItemUseReplayed 后的结果，让协调器按首次成功或失败决定是否补放后续领域提交。
	OutResult = FCatInventoryItemUseResult();
	OutResult.RequestId = RequestId;
	OutResult.EquipmentRevision = Snapshot.Revision;
	if (!RequestId.IsValid() || !ItemInstanceId.IsValid() || Quantity <= 0)
	{
		return false;
	}
	const FString Key = MakeTerminalKey(TEXT("UseInventoryItem"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("ExpectedRevision=%lld|ItemInstance=%s|Quantity=%d"),
		ExpectedRevision, *ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), Quantity);
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
	// 物品停止使用流程：
	// 1. 先校验 authority 和 RequestId，再按实例载荷签名处理重放；同一收口请求不会重复放回同一物品。
	// 2. 正式库存存在时把 Use 期间强持有的同一 UObject 放回 InventoryComponent，不按 DefinitionId 重新生成。
	// 3. 归还前把正式库存槽位数追上当前配置，正式入库成功后再刷新旧投影和钓具选择。
	// 4. 没有正式库存组件的旧宿主才回退 Snapshot 数组写入，继续兼容历史夹具。
	FCatInventoryItemUseResult Result;
	Result.RequestId = RequestId;
	Result.EquipmentRevision = Snapshot.Revision;
	if (!GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid() || !ItemInstanceId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	const FString Key = MakeTerminalKey(TEXT("UnUseInventoryItem"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("ItemInstance=%s"),
		*ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens));
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
	const auto Finish = [this, &Key, &PayloadSignature](const FCatInventoryItemUseResult& Completed)
	{
		InventoryItemUseTerminalCache.Add(Key, Completed);
		TerminalPayloadByKey.Add(Key, PayloadSignature);
		return Completed;
	};
	FCatInventoryItemUseRecord* Record = FindInventoryItemUseRecord(ItemInstanceId);
	if (!Record)
	{
		Result.Error = ECatDomainCommandError::NotFound;
		return Finish(Result);
	}
	Result.Item = Record->Item;
	if (Record->bReleased)
	{
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		return Finish(Result);
	}
	const UCatEquipmentDefinition* Definition =
		GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(Record->Item.DefinitionId);
	if (!Definition)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	FCatRunInventorySlot RestoredItem = Record->Item;
	CatRunInventorySlotOperations::NormalizeStoredItemSlot(RestoredItem, *Definition);
	const ECatDomainCommandError DefinitionUnUseError = Definition->UnUse(RestoredItem);
	if (DefinitionUnUseError != ECatDomainCommandError::None)
	{
		Result.Error = DefinitionUnUseError;
		return Finish(Result);
	}
	if (UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent())
	{
		// 正式收口只能归还 Use 时移出的那一个实例；缺少强引用说明活动记录和库存事实已经分叉，必须拒绝。
		UCatEquipmentInventoryItemInstance* FormalInstance =
			ActiveFormalInventoryUseInstances.FindRef(RestoredItem.ItemInstanceId);
		if (FormalInstance == nullptr)
		{
			Result.Error = ECatDomainCommandError::DependencyUnavailable;
			return Finish(Result);
		}
		if (OwnerInventory->FindInventorySlotIndexFromInstance(FormalInstance) != INDEX_NONE)
		{
			Result.Error = ECatDomainCommandError::InvalidPhase;
			return Finish(Result);
		}

		FormalInstance->SetRodRuntimeStateFromAuthority(RestoredItem.RodDurability, RestoredItem.bRodBroken);
		// 运行期容量可能被设置或测试夹具调整；正式库存归还必须先跟上配置，不能只让旧 Snapshot 扩容。
		OwnerInventory->SetInventorySlotCountFromAuthority(GetConfiguredInventorySlotCapacity());
		FCatInventoryReceiveBatch ReceiveBatch;
		FCatInventoryInstanceEntry& InstanceEntry = ReceiveBatch.InstanceEntries.AddDefaulted_GetRef();
		InstanceEntry.ItemInstance = FormalInstance;
		InstanceEntry.Count = RestoredItem.Quantity;
		if (!OwnerInventory->CanFullyAcceptInventoryBatch(ReceiveBatch))
		{
			Result.Error = ECatDomainCommandError::CapacityExceeded;
			return Finish(Result);
		}

		const TArray<FCatInventoryEntry> SavedEntries = OwnerInventory->GetInventoryEntries();
		const FCatEquipmentLoadoutSnapshot SavedSnapshot = Snapshot;
		const FCatInventoryItemUseRecord SavedRecord = *Record;
		if (!OwnerInventory->TryAddInventoryBatch(ReceiveBatch))
		{
			Result.Error = ECatDomainCommandError::DependencyUnavailable;
			return Finish(Result);
		}

		// 自动选竿必须在活动记录释放后再判断；否则刚收回的坏竿仍会被当作部署中选择，挡住健康替代竿。
		Record->Item = RestoredItem;
		Record->bReleased = true;
		if (!RefreshInventoryProjectionFromInventoryComponentFromAuthority(Definition, RestoredItem.DefinitionId))
		{
			OwnerInventory->ReplaceInventoryEntriesFromAuthority(
				SavedEntries, GetConfiguredInventorySlotCapacity());
			Snapshot = SavedSnapshot;
			*Record = SavedRecord;
			Result.Error = ECatDomainCommandError::DependencyUnavailable;
			return Finish(Result);
		}

		ActiveFormalInventoryUseInstances.Remove(RestoredItem.ItemInstanceId);
		if (Definition->Kind == ECatEquipmentKind::Rod && Snapshot.RodItemInstanceId == RestoredItem.ItemInstanceId)
		{
			Snapshot.RodDefinitionId = RestoredItem.DefinitionId;
			Snapshot.RodDurability = RestoredItem.RodDurability;
			Snapshot.bRodBroken = RestoredItem.bRodBroken;
		}
		Result.Item = RestoredItem;
		Result.EquipmentRevision = Snapshot.Revision;
		Result.bCommitted = true;
		Result.Error = ECatDomainCommandError::None;
		return Finish(Result);
	}
	if (FCatRunInventorySlot* ExistingStoredItem = FindInventorySlotByInstanceId(RestoredItem.ItemInstanceId))
	{
		if (ExistingStoredItem->DefinitionId != RestoredItem.DefinitionId)
		{
			Result.Error = ECatDomainCommandError::InvalidPhase;
			return Finish(Result);
		}
		*ExistingStoredItem = RestoredItem;
	}
	else if (!CanStoreInventorySlot(*Definition, RestoredItem) || !AddInventoryItemSlot(*Definition, RestoredItem))
	{
		Result.Error = ECatDomainCommandError::CapacityExceeded;
		return Finish(Result);
	}
	Record->Item = RestoredItem;
	Record->bReleased = true;
	if (Definition->Kind == ECatEquipmentKind::Rod && Snapshot.RodItemInstanceId == RestoredItem.ItemInstanceId)
	{
		Snapshot.RodDefinitionId = RestoredItem.DefinitionId;
		Snapshot.RodDurability = RestoredItem.RodDurability;
		Snapshot.bRodBroken = RestoredItem.bRodBroken;
	}
	AutoSelectGrantedInventoryItem(*Definition, RestoredItem.DefinitionId);
	++Snapshot.Revision;
	PublishSnapshot();
	Result.Item = RestoredItem;
	Result.EquipmentRevision = Snapshot.Revision;
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	return Finish(Result);
}

// 失败预算提交流程：
// 1. 先拒绝正在 Use 或 Fishing Use 的物品，再按 RequestId 返回已缓存终态，避免重放时再次扣饵或耐久。
// 2. 首次请求校验 authority、Revision 和惩罚枚举；None 只提交一次无物资变化的终态。
// 3. 丢特殊饵在正式库存存在时按旧选择实例 ID 回到 InventoryComponent 扣一份，旧 Snapshot 只在扣量后重建投影。
// 4. 伤竿在正式库存存在时解析当前选择的鱼竿实例，直接写实例耐久和断竿状态，再刷新旧 Snapshot 投影。
// 5. 正式写入后的投影刷新失败会回滚库存 entry、鱼竿实例或旧 Snapshot；没有正式库存的历史宿主才走旧数组回退。
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
		MarkCommandReplayed(Result.Command);
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
		UCatEquipmentDefinition* Bait =
			GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(Snapshot.BaitDefinitionId);
		UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent();
		FCatRunInventorySlot FormalBaitSlot;
		int32 FormalBaitSlotIndex = INDEX_NONE;
		const FCatRunInventorySlot* BaitSlot = nullptr;
		if (OwnerInventory != nullptr)
		{
			// 正式库存存在时，失败惩罚只把旧选择实例 ID 映射回 InventoryComponent 槽位；旧 Snapshot 不再直接扣量。
			FormalBaitSlotIndex = OwnerInventory->FindInventorySlotIndexFromInstanceId(Snapshot.BaitItemInstanceId);
			const FCatInventoryEntry* FormalBaitEntry =
				OwnerInventory->GetInventoryEntryAtSlot(FormalBaitSlotIndex);
			if (FormalBaitEntry != nullptr
				&& BuildLegacyRunInventorySlotFromFormalEntry(*FormalBaitEntry, FormalBaitSlot))
			{
				BaitSlot = &FormalBaitSlot;
			}
		}
		else
		{
			BaitSlot = FindInventorySlotByInstanceId(Snapshot.BaitItemInstanceId);
		}
		if (!Bait || !Bait->bSpecialBait || !BaitSlot
			|| BaitSlot->DefinitionId != Snapshot.BaitDefinitionId || BaitSlot->Quantity <= 0)
		{
			Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
		}
		else if (OwnerInventory != nullptr)
		{
			const FName LostBaitDefinitionId = Snapshot.BaitDefinitionId;
			const TArray<FCatInventoryEntry> SavedEntries = OwnerInventory->GetInventoryEntries();
			const FCatEquipmentLoadoutSnapshot SavedSnapshot = Snapshot;
			if (BaitSlot->Quantity <= 1)
			{
				Snapshot.BaitItemInstanceId = FGuid();
			}
			if (OwnerInventory->ConsumeItemAtSlot(FormalBaitSlotIndex, 1)
				&& RefreshInventoryProjectionFromInventoryComponentFromAuthority(Bait, LostBaitDefinitionId))
			{
				Result.Command.bCommitted = true;
				Result.Command.Error = ECatDomainCommandError::None;
			}
			else
			{
				OwnerInventory->ReplaceInventoryEntriesFromAuthority(
					SavedEntries, GetConfiguredInventorySlotCapacity());
				Snapshot = SavedSnapshot;
				Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
			}
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
		UCatEquipmentDefinition* Rod =
			GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(Snapshot.RodDefinitionId);
		if (!FMath::IsFinite(Loss) || Loss <= 0.0 || Snapshot.RodDefinitionId.IsNone() || Snapshot.bRodBroken
			|| Rod == nullptr || Rod->Kind != ECatEquipmentKind::Rod)
		{
			Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
		}
		else if (UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent())
		{
			FCatRunInventorySlot FormalRodSlot;
			UCatEquipmentInventoryItemInstance* FormalRodInstance =
				ResolveSelectedFormalRodInstanceFromInventory(*OwnerInventory, *Rod, FormalRodSlot);
			if (FormalRodInstance == nullptr || FormalRodSlot.bRodBroken
				|| !FMath::IsFinite(FormalRodSlot.RodDurability) || FormalRodSlot.RodDurability <= 0.0)
			{
				Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
			}
			else
			{
				const FCatEquipmentLoadoutSnapshot SavedSnapshot = Snapshot;
				const double SavedRodDurability = FormalRodInstance->GetRodDurability();
				const bool bSavedRodBroken = FormalRodInstance->IsRodBroken();
				const double NewDurability = FMath::Max(0.0, FormalRodSlot.RodDurability - Loss);
				const bool bNewBroken = NewDurability <= 0.0;
				FormalRodInstance->SetRodRuntimeStateFromAuthority(NewDurability, bNewBroken);
				if (RefreshInventoryProjectionFromInventoryComponentFromAuthority(Rod, FormalRodSlot.DefinitionId))
				{
					Result.Command.bCommitted = true;
					Result.Command.Error = ECatDomainCommandError::None;
				}
				else
				{
					FormalRodInstance->SetRodRuntimeStateFromAuthority(SavedRodDurability, bSavedRodBroken);
					Snapshot = SavedSnapshot;
					Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
				}
			}
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
	const int64 ExpectedRevision)
{
	// 建立 Fishing 使用预留的流程：
	// 1. 先用 SessionId 返回已存在的终态，保证 FishingSession 重放不会再检查或再占库存。
	// 2. 再校验 authority、定义类型、Revision、当前钓鱼选择和三份实例身份，任何不一致都保持快照不变。
	// 3. 鱼竿实例必须来自活动 Use 记录，鱼饵和鱼漂实例优先从正式库存确认，避免场景竿和背包格引用不同物品。
	// 4. 通过后立即从正式库存扣掉选中鱼饵实例的一份，并只把可归还的定义放进本 Session 记录。
	// 5. 记录只保存这场 Fishing 自己要消耗或归还的饵料和耐久累计，不再给库存拖放提供通用占用 gate。
	if (const FCatFishingUseRecord* ExistingRecord = FindFishingUseRecord(FishingSessionId))
	{
		const bool bReserved = ExistingRecord->bBaitQuantityReserved && !ExistingRecord->bBaitCommitted
			&& !ExistingRecord->bReleased;
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::AlreadyResolved, bReserved,
			bReserved ? ExistingRecord : nullptr);
	}
	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	UCatEquipmentDefinition* Rod = Settings->FindRuntimeDefinition(RodDefinitionId);
	UCatEquipmentDefinition* Bait = Settings->FindRuntimeDefinition(BaitDefinitionId);
	UCatEquipmentDefinition* Float = Settings->FindRuntimeDefinition(FloatDefinitionId);
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::DependencyUnavailable, false);
	}
	if (!FishingSessionId.IsValid() || !RodItemInstanceId.IsValid() || !BaitItemInstanceId.IsValid()
		|| !FloatItemInstanceId.IsValid() || RodDefinitionId.IsNone()
		|| BaitDefinitionId.IsNone() || FloatDefinitionId.IsNone() || !Rod || !Bait || !Float
		|| Rod->Kind != ECatEquipmentKind::Rod || Bait->Kind != ECatEquipmentKind::Bait
		|| Float->Kind != ECatEquipmentKind::Float)
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::InvalidPayload, false);
	}
	if (Snapshot.Revision != ExpectedRevision)
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::RevisionConflict, false);
	}
	if (Snapshot.RodDefinitionId != RodDefinitionId || Snapshot.RodItemInstanceId != RodItemInstanceId
		|| Snapshot.BaitDefinitionId != BaitDefinitionId || Snapshot.BaitItemInstanceId != BaitItemInstanceId
		|| Snapshot.FloatDefinitionId != FloatDefinitionId || Snapshot.FloatItemInstanceId != FloatItemInstanceId)
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::InvalidPayload, false);
	}
	if (Snapshot.bRodBroken || !FMath::IsFinite(Snapshot.RodDurability) || Snapshot.RodDurability <= 0.0)
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::InvalidPhase, false);
	}
	if (!Bait->bRunConsumable)
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::InvalidPayload, false);
	}
	const FCatInventoryItemUseRecord* RodUseRecord = FindInventoryItemUseRecord(RodItemInstanceId);
	UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent();
	FCatRunInventorySlot FormalBaitSlot;
	FCatRunInventorySlot FormalFloatSlot;
	int32 FormalBaitSlotIndex = INDEX_NONE;
	const FCatRunInventorySlot* BaitSlot = nullptr;
	const FCatRunInventorySlot* FloatSlot = nullptr;
	if (OwnerInventory != nullptr)
	{
		// Fishing 只消费正式库存里的数量物；旧 Snapshot 在这里仅提供当前选择，不能再当库存事实源。
		FormalBaitSlotIndex = OwnerInventory->FindInventorySlotIndexFromInstanceId(BaitItemInstanceId);
		const int32 FormalFloatSlotIndex = OwnerInventory->FindInventorySlotIndexFromInstanceId(FloatItemInstanceId);
		const FCatInventoryEntry* FormalBaitEntry = OwnerInventory->GetInventoryEntryAtSlot(FormalBaitSlotIndex);
		const FCatInventoryEntry* FormalFloatEntry = OwnerInventory->GetInventoryEntryAtSlot(FormalFloatSlotIndex);
		if (FormalBaitEntry != nullptr
			&& BuildLegacyRunInventorySlotFromFormalEntry(*FormalBaitEntry, FormalBaitSlot))
		{
			BaitSlot = &FormalBaitSlot;
		}
		if (FormalFloatEntry != nullptr
			&& BuildLegacyRunInventorySlotFromFormalEntry(*FormalFloatEntry, FormalFloatSlot))
		{
			FloatSlot = &FormalFloatSlot;
		}
	}
	else
	{
		BaitSlot = FindInventorySlotByInstanceId(BaitItemInstanceId);
		FloatSlot = FindInventorySlotByInstanceId(FloatItemInstanceId);
	}
	if (!RodUseRecord || RodUseRecord->bReleased || RodUseRecord->Item.DefinitionId != RodDefinitionId
		|| !BaitSlot || BaitSlot->DefinitionId != BaitDefinitionId
		|| !FloatSlot || FloatSlot->DefinitionId != FloatDefinitionId)
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::NotFound, false);
	}
	if (FloatSlot->Quantity <= 0)
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::CapacityExceeded, false);
	}
	FCatRunInventorySlot ReservedBaitItem;
	if (OwnerInventory != nullptr)
	{
		const TArray<FCatInventoryEntry> SavedEntries = OwnerInventory->GetInventoryEntries();
		const FCatEquipmentLoadoutSnapshot SavedSnapshot = Snapshot;
		ReservedBaitItem = *BaitSlot;
		ReservedBaitItem.Quantity = 1;
		if (BaitSlot->Quantity <= 1)
		{
			Snapshot.BaitItemInstanceId = FGuid();
		}
		if (!OwnerInventory->ConsumeItemAtSlot(FormalBaitSlotIndex, 1))
		{
			Snapshot = SavedSnapshot;
			return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::CapacityExceeded, false);
		}
		if (!RefreshInventoryProjectionFromInventoryComponentFromAuthority(Bait, BaitDefinitionId))
		{
			OwnerInventory->ReplaceInventoryEntriesFromAuthority(
				SavedEntries, GetConfiguredInventorySlotCapacity());
			Snapshot = SavedSnapshot;
			return MakeFishingUseReservationResult(FishingSessionId,
				ECatDomainCommandError::DependencyUnavailable, false);
		}
	}
	else if (!RemoveInventoryItemQuantityFromInstance(BaitItemInstanceId, 1, ReservedBaitItem))
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::CapacityExceeded, false);
	}

	FCatFishingUseRecord Record;
	Record.RodItemInstanceId = RodItemInstanceId;
	Record.RodDefinitionId = RodDefinitionId;
	Record.ReservedBaitDefinitionId = ReservedBaitItem.DefinitionId;
	Record.bBaitQuantityReserved = true;
	FishingUseRecords.Add(FishingSessionId, Record);
	if (OwnerInventory == nullptr)
	{
		++Snapshot.Revision;
		PublishSnapshot();
	}
	return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::None, true);
}

FCatFishingUseOperationResult UCatEquipmentComponent::CommitFishingBaitDeferred(const FGuid FishingSessionId)
{
	// 确认消耗鱼饵的流程：
	// 1. 先找到 Begin 阶段留下的记录；没有记录说明 Fishing 从未拿到装备使用权。
	// 2. 已释放或已提交的记录只返回终态，不允许重复处理同一份暂存饵。
	// 3. 只有该 Session 自己仍处于活动预留态才能提交，旧会话 tombstone 不会补消耗。
	// 4. Begin 已经把饵从库存移入记录并发布快照；这里只清掉暂存副本并标记已消耗。
	FCatFishingUseRecord* Record = FindFishingUseRecord(FishingSessionId);
	if (!Record)
	{
		return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::NotFound, false);
	}
	if (Record->bReleased || Record->bBaitCommitted)
	{
		return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::AlreadyResolved, false, Record);
	}
	if (!IsFishingUseActive(FishingSessionId))
	{
		return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::InvalidPhase, false, Record);
	}
	if (Record->bBaitQuantityReserved)
	{
		if (Record->ReservedBaitDefinitionId.IsNone())
		{
			return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::InvalidPhase, false, Record);
		}
		Record->ReservedBaitDefinitionId = NAME_None;
		Record->bBaitQuantityReserved = false;
	}
	Record->bBaitCommitted = true;
	return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::None, true, Record);
}

FCatFishingUseOperationResult UCatEquipmentComponent::ApplyFishingRodWear(const FGuid FishingSessionId,
	const int64 WearSequence, const double AbsoluteTotal)
{
	// 鱼竿磨损写回流程：
	// 1. 先按 Session 读取 Begin 记录并建立统一拒绝日志；authority、载荷、会话状态、序号和饵料提交缺任一项都不写耐久。
	// 2. 再按 Begin 冻结的实例 ID 查库存或部署 Use 记录，确保跨场耐久扣在原始鱼竿而不是当前选择的另一根竿。
	// 3. 最后只按累计磨损差额写回实例，并在当前选择仍是同一竿时投影 Snapshot；重复序号只返回终态不重扣。
	FCatFishingUseRecord* Record = FindFishingUseRecord(FishingSessionId);
	const auto Reject = [&](const ECatDomainCommandError Error, const TCHAR* Reason)
	{
		UE_LOG(LogCatCharacter, Warning,
			TEXT("Event=equipment_rod_wear_rejected SessionId=%s RodItemInstanceId=%s WearSequence=%lld AbsoluteWear=%.3f Reason=%s Error=%s World=%s NetMode=%d Authority=%s Owner=%s"),
			*FishingSessionId.ToString(), Record ? *Record->RodItemInstanceId.ToString() : TEXT("None"),
			WearSequence, AbsoluteTotal, Reason, *UEnum::GetValueAsString(Error), *GetNameSafe(GetWorld()),
			static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone),
			GetOwner() && GetOwner()->HasAuthority() ? TEXT("true") : TEXT("false"), *GetNameSafe(GetOwner()));
		return MakeFishingUseOperationResult(FishingSessionId, Error, false, Record);
	};
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return Reject(ECatDomainCommandError::DependencyUnavailable, TEXT("NotAuthority"));
	}
	if (!FishingSessionId.IsValid() || WearSequence <= 0 || !FMath::IsFinite(AbsoluteTotal) || AbsoluteTotal < 0.0)
	{
		return Reject(ECatDomainCommandError::InvalidPayload, TEXT("InvalidPayload"));
	}
	if (!Record)
	{
		return Reject(ECatDomainCommandError::NotFound, TEXT("SessionMissing"));
	}
	if (Record->bReleased)
	{
		return Reject(ECatDomainCommandError::AlreadyResolved, TEXT("SessionReleased"));
	}
	if (WearSequence == Record->LastWearSequence && AbsoluteTotal == Record->AbsoluteRodWear)
	{
		return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::AlreadyResolved, false, Record);
	}
	if (Record->LastWearSequence == MAX_int64 || WearSequence != Record->LastWearSequence + 1
		|| AbsoluteTotal < Record->AbsoluteRodWear)
	{
		return Reject(ECatDomainCommandError::InvalidPayload, TEXT("WearSequenceOrTotalConflict"));
	}
	if (!Record->bBaitCommitted)
	{
		return Reject(ECatDomainCommandError::InvalidPhase, TEXT("BaitNotCommitted"));
	}
	FCatRunInventorySlot* RodItem = FindFishingRodInstance(*Record);
	if (!RodItem || !FMath::IsFinite(RodItem->RodDurability) || RodItem->RodDurability < 0.0)
	{
		return Reject(ECatDomainCommandError::NotFound, TEXT("BoundRodUnavailable"));
	}
	// 到这里才允许改写实例：Record 保存累计磨损用于后续去重，RodItem 保存真实剩余耐久。
	const double Before = RodItem->RodDurability;
	const bool bWasBroken = RodItem->bRodBroken;
	const double Delta = AbsoluteTotal - Record->AbsoluteRodWear;
	RodItem->RodDurability = bWasBroken ? 0.0 : FMath::Max(0.0, Before - Delta);
	RodItem->bRodBroken = RodItem->RodDurability <= 0.0;
	Record->LastWearSequence = WearSequence;
	Record->AbsoluteRodWear = AbsoluteTotal;
	if (Snapshot.RodItemInstanceId == Record->RodItemInstanceId)
	{
		Snapshot.RodDurability = RodItem->RodDurability;
		Snapshot.bRodBroken = RodItem->bRodBroken;
	}
	const double Remaining = RodItem->RodDurability;
	const bool bBroken = RodItem->bRodBroken;
	const bool bChanged = Before != Remaining || bWasBroken != bBroken;
	if (bChanged)
	{
		++Snapshot.Revision;
		PublishSnapshot();
	}
	if (WearSequence == 1 || bWasBroken != bBroken
		|| FMath::FloorToDouble(Before / 5.0) != FMath::FloorToDouble(Remaining / 5.0))
	{
		UE_LOG(LogCatCharacter, Log,
			TEXT("Event=equipment_rod_wear_applied SessionId=%s RodItemInstanceId=%s WearSequence=%lld AbsoluteWear=%.3f Delta=%.3f DurabilityBefore=%.3f Durability=%.3f Broken=%s Revision=%lld World=%s NetMode=%d Authority=true Owner=%s"),
			*FishingSessionId.ToString(), *Record->RodItemInstanceId.ToString(), WearSequence, AbsoluteTotal,
			Delta, Before, Remaining, bBroken ? TEXT("true") : TEXT("false"), Snapshot.Revision,
			*GetNameSafe(GetWorld()), static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone),
			*GetNameSafe(GetOwner()));
	}
	return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::None, true, Record);
}

bool UCatEquipmentComponent::GetFishingRodDurability(const FGuid FishingSessionId,
	double& OutDurability, bool& OutBroken) const
{
	// 耐久读取流程：按会话短记录找到 Begin 冻结的鱼竿实例，只返回该实例当前耐久和断竿事实，避免换选后把另一根鱼竿当成旧会话结果。
	OutDurability = 0.0;
	OutBroken = false;
	const FCatFishingUseRecord* Record = FindFishingUseRecord(FishingSessionId);
	const FCatRunInventorySlot* RodItem = Record ? FindFishingRodInstance(*Record) : nullptr;
	if (!RodItem || !FMath::IsFinite(RodItem->RodDurability) || RodItem->RodDurability < 0.0)
	{
		return false;
	}
	OutDurability = RodItem->RodDurability;
	OutBroken = RodItem->bRodBroken || RodItem->RodDurability <= 0.0;
	return true;
}

FCatFishingUseOperationResult UCatEquipmentComponent::ReleaseFishingUse(const FGuid FishingSessionId)
{
	// Fishing 使用释放流程：
	// 1. 先按 SessionId 找到 Begin 留下的短生命周期记录；旧会话和重复释放只返回稳定终态。
	// 2. 如果饵料还没确认消耗，就把这一份按 DefinitionId 归还给正式库存，背包已满时由库存事务追加返还格。
	// 3. 归还后刷新旧投影并修正同定义空选择，再关闭记录；已确认消耗的会话只关闭记录，不再碰库存。
	FCatFishingUseRecord* Record = FindFishingUseRecord(FishingSessionId);
	if (!Record)
	{
		return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::NotFound, false);
	}
	if (Record->bReleased)
	{
		return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::AlreadyResolved, false, Record);
	}
	if (!IsFishingUseActive(FishingSessionId))
	{
		return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::InvalidPhase, false, Record);
	}
	if (Record->bBaitQuantityReserved && !Record->bBaitCommitted)
	{
		const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
		UCatEquipmentDefinition* Bait = Settings
			? Settings->FindRuntimeDefinition(Record->ReservedBaitDefinitionId) : nullptr;
		if (!Bait || Bait->Kind != ECatEquipmentKind::Bait || !Bait->bRunConsumable
			|| GetInventoryStackLimit(*Bait) <= 0)
		{
			return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::DependencyUnavailable,
				false, Record);
		}
		const FName RestoredDefinitionId = Record->ReservedBaitDefinitionId;
		if (UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent())
		{
			const TArray<FCatInventoryEntry> SavedEntries = OwnerInventory->GetInventoryEntries();
			const FCatEquipmentLoadoutSnapshot SavedSnapshot = Snapshot;
			const FCatFishingUseRecord SavedRecord = *Record;
			FCatInventoryReceiveBatch ReceiveBatch;
			FCatInventoryDefinitionEntry& DefinitionEntry =
				ReceiveBatch.DefinitionEntries.AddDefaulted_GetRef();
			DefinitionEntry.ItemDefinition = Bait;
			DefinitionEntry.Count = 1;
			const bool bShouldRepairBaitSelection = Snapshot.BaitDefinitionId == RestoredDefinitionId
				|| Snapshot.BaitDefinitionId.IsNone()
				|| GetInventoryItemQuantity(Snapshot.BaitDefinitionId) <= 0;
			if (bShouldRepairBaitSelection)
			{
				Snapshot.BaitDefinitionId = RestoredDefinitionId;
				Snapshot.BaitItemInstanceId = FGuid();
			}
			if (!OwnerInventory->TryReturnReservedInventoryBatchFromAuthority(ReceiveBatch, 1))
			{
				Snapshot = SavedSnapshot;
				return MakeFishingUseOperationResult(FishingSessionId,
					ECatDomainCommandError::DependencyUnavailable, false, Record);
			}
			Record->ReservedBaitDefinitionId = NAME_None;
			Record->bBaitQuantityReserved = false;
			if (!RefreshInventoryProjectionFromInventoryComponentFromAuthority(Bait, RestoredDefinitionId))
			{
				OwnerInventory->ReplaceInventoryEntriesFromAuthority(
					SavedEntries, GetConfiguredInventorySlotCapacity());
				Snapshot = SavedSnapshot;
				*Record = SavedRecord;
				return MakeFishingUseOperationResult(FishingSessionId,
					ECatDomainCommandError::DependencyUnavailable, false, Record);
			}
		}
		else
		{
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
			++Snapshot.Revision;
			PublishSnapshot();
		}
	}
	Record->bReleased = true;
	return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::None, true, Record);
}

bool UCatEquipmentComponent::HasActiveFishingUse() const
{
	for (const TPair<FGuid, FCatFishingUseRecord>& Pair : FishingUseRecords)
	{
		if (Pair.Key.IsValid() && !Pair.Value.bReleased) return true;
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
// 2. 读取当前鱼竿、浮木定义和 Owner 正式库存；正式库存存在时只从 InventoryComponent 查询可消费浮木。
// 3. 校验营地、authority、Revision、鱼竿/浮木定义和材料数量；任一失败都不改库存或旧投影。
// 4. 正式路径先解析当前选择的鱼竿实例，再扣一份浮木并写回同一实例的耐久和断竿状态。
// 5. 投影刷新失败会恢复浮木 entry、鱼竿实例和旧 Snapshot；没有正式库存的历史宿主才沿用旧数组扣材料。
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
	int32 FormalDriftwoodSlotIndex = INDEX_NONE;
	FCatRunInventorySlot FormalDriftwoodSlot;
	const FCatRunInventorySlot* DriftwoodSlot = nullptr;
	if (OwnerInventory != nullptr)
	{
		// 修竿材料只从正式库存确认；旧 Snapshot 在这里不再决定浮木数量。
		FormalDriftwoodSlotIndex =
			OwnerInventory->FindFirstInventorySlotIndexByDefinitionId(Settings->DriftwoodDefinitionId);
		const FCatInventoryEntry* FormalDriftwoodEntry =
			OwnerInventory->GetInventoryEntryAtSlot(FormalDriftwoodSlotIndex);
		if (FormalDriftwoodEntry != nullptr
			&& BuildLegacyRunInventorySlotFromFormalEntry(*FormalDriftwoodEntry, FormalDriftwoodSlot))
		{
			DriftwoodSlot = &FormalDriftwoodSlot;
		}
	}
	else
	{
		DriftwoodSlot = FindFirstInventorySlotByDefinition(Settings->DriftwoodDefinitionId);
	}
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
		if (OwnerInventory != nullptr)
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

// 库存容量读取流程：配置是本局随身库存可见格子的来源；负数由属性 Clamp 防住，这里仍做运行期保护。
int32 UCatEquipmentComponent::GetConfiguredInventorySlotCapacity() const
{
	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	return Settings ? FMath::Max(0, Settings->InventorySlotCapacity) : 0;
}

// 正式随身库存解析流程：只从当前 Owner 上读取 InventoryComponent；Equipment 不创建或缓存库存组件，避免迁移期出现第二份背包归属。
UCatInventoryComponent* UCatEquipmentComponent::ResolveOwnerInventoryComponent() const
{
	AActor* Owner = GetOwner();
	return Owner != nullptr ? Owner->FindComponentByClass<UCatInventoryComponent>() : nullptr;
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
	// 1. 只遍历当前旧随身库存格，不改活动 Use 记录和选择快照。
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
// 1. 只在 authority Owner 上执行；没有正式库存组件的旧测试宿主直接跳过，不改变既有 Equipment 行为。
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

bool UCatEquipmentComponent::CanStoreInventorySlot(const UCatEquipmentDefinition& Definition,
	const FCatRunInventorySlot& Item) const
{
	// 完整实例容量预检流程：
	// 1. 先拒绝空实例、非法数量和超过堆叠上限的载荷，保证完整实例入库或 UnUse 不会放回一份坏状态。
	// 2. 已存在同一 ItemInstanceId 时只允许数量型实例合并进原堆栈，装备型实例不能再占第二个格子。
	// 3. 合并空间不足时直接拒绝同一实例拆分；没有同实例时才检查配置容量和现有空格。
	if (!CatRunInventorySlotOperations::IsInventorySlotOccupied(Item) || !Item.ItemInstanceId.IsValid())
	{
		return false;
	}
	const int32 StackLimit = GetInventoryStackLimit(Definition);
	if (StackLimit <= 0 || Item.Quantity > StackLimit)
	{
		return false;
	}
	int32 Remaining = Item.Quantity;
	bool bFoundSameInstance = false;
	for (const FCatRunInventorySlot& Slot : Snapshot.InventorySlots)
	{
		if (Slot.ItemInstanceId == Item.ItemInstanceId && CatRunInventorySlotOperations::IsInventorySlotOccupied(Slot))
		{
			if (Slot.DefinitionId != Item.DefinitionId)
			{
				return false;
			}
			bFoundSameInstance = true;
			if (!Definition.bRunConsumable)
			{
				return false;
			}
			if (Slot.Quantity > 0 && Slot.Quantity < StackLimit)
			{
				Remaining -= FMath::Min(Remaining, StackLimit - Slot.Quantity);
				if (Remaining <= 0)
				{
					return true;
				}
			}
		}
	}
	if (bFoundSameInstance)
	{
		return false;
	}
	const int32 EffectiveSlotCount = FMath::Max(GetConfiguredInventorySlotCapacity(), Snapshot.InventorySlots.Num());
	if (Snapshot.InventorySlots.Num() < EffectiveSlotCount)
	{
		return true;
	}
	for (const FCatRunInventorySlot& Slot : Snapshot.InventorySlots)
	{
		if (!CatRunInventorySlotOperations::IsInventorySlotOccupied(Slot))
		{
			return true;
		}
	}
	return false;
}

bool UCatEquipmentComponent::AddInventoryItemSlot(const UCatEquipmentDefinition& Definition,
	const FCatRunInventorySlot& Item)
{
	// 完整实例放回流程：先尝试合并同一个数量型实例，再找空格原样落位；不会把不同实例只因 DefinitionId 相同就揉成一份。
	FCatRunInventorySlot StoredItem = Item;
	CatRunInventorySlotOperations::NormalizeStoredItemSlot(StoredItem, Definition);
	if (!CanStoreInventorySlot(Definition, StoredItem))
	{
		return false;
	}
	EnsureInventorySlotArray();
	const int32 StackLimit = GetInventoryStackLimit(Definition);
	if (Definition.bRunConsumable)
	{
		int32 Remaining = StoredItem.Quantity;
		for (FCatRunInventorySlot& Slot : Snapshot.InventorySlots)
		{
			if (Slot.ItemInstanceId == StoredItem.ItemInstanceId && Slot.DefinitionId == StoredItem.DefinitionId
				&& Slot.Quantity > 0 && Slot.Quantity < StackLimit)
			{
				const int32 Added = FMath::Min(Remaining, StackLimit - Slot.Quantity);
				Slot.Quantity += Added;
				Remaining -= Added;
				if (Remaining <= 0)
				{
					return true;
				}
			}
		}
		StoredItem.Quantity = Remaining;
	}
	for (FCatRunInventorySlot& Slot : Snapshot.InventorySlots)
	{
		if (!CatRunInventorySlotOperations::IsInventorySlotOccupied(Slot))
		{
			Slot = StoredItem;
			return true;
		}
	}
	return false;
}

FCatDomainCommandResult UCatEquipmentComponent::GrantInventorySlotFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const FCatRunInventorySlot& Item)
{
	// 完整实例授予流程：
	// 1. 用 RequestId 和实例载荷签名保护重放，避免同一取用请求换成另一件物品。
	// 2. 首次提交只接受 authority、正确 Revision、可运行定义和可容纳实例；数量型与装备型都走完整实例入库，不重新生成 ItemInstanceId。
	// 3. 成功后按定义自动修正当前选择并发布快照，调用方不需要知道这份实例落到了哪个格子。
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString Key = MakeTerminalKey(TEXT("GrantInventorySlot"), RequestId);
	const FString PayloadSignature = FString::Printf(
		TEXT("ExpectedRevision=%lld|Definition=%s|Instance=%s|Quantity=%d|RodDurability=%.6f|Broken=%s"),
		ExpectedRevision, *Item.DefinitionId.ToString(),
		*Item.ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), Item.Quantity,
		Item.RodDurability, Item.bRodBroken ? TEXT("true") : TEXT("false"));
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
		|| !CatRunInventorySlotOperations::IsInventorySlotOccupied(Item) || !Item.ItemInstanceId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (Snapshot.Revision != ExpectedRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
	}
	else
	{
		const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
		const UCatEquipmentDefinition* Definition =
			Settings ? Settings->FindRuntimeDefinition(Item.DefinitionId) : nullptr;
		if (!Definition)
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
		}
		else if (AddInventoryItemSlot(*Definition, Item))
		{
			AutoSelectGrantedInventoryItem(*Definition, Item.DefinitionId);
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

bool UCatEquipmentComponent::RemoveInventoryItemQuantityFromInstance(const FGuid ItemInstanceId,
	const int32 Quantity, FCatRunInventorySlot& OutConsumedItem)
{
	// 旧 Snapshot 指定实例扣量流程：
	// 1. 这个入口只服务没有正式 InventoryComponent 的历史宿主；正式角色的扣量必须走库存组件事务。
	// 2. 先要求有效实例和正数量，输出始终先清空，避免失败时调用方误用上次结果。
	// 3. 再只按 ItemInstanceId 找到目标数量栈，不因为 DefinitionId 相同就扣别的格子。
	// 4. 成功时 OutConsumedItem 只代表本次消耗的份数；原格剩余数量归零才清空。
	// 5. 如果清空的是当前选中鱼饵实例，就立刻改选同定义的剩余堆栈，避免选择指向空格。
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
	// 绑定鱼竿查找流程：先按实例 ID 查随身库存；如果竿正部署在世界里，再读活动 Use 记录里的副本，并用定义和数量确认没有串到同类物品。
	FCatRunInventorySlot* Item = FindInventorySlotByInstanceId(Record.RodItemInstanceId);
	if (!Item)
	{
		FCatInventoryItemUseRecord* UseRecord = FindInventoryItemUseRecord(Record.RodItemInstanceId);
		Item = UseRecord && !UseRecord->bReleased ? &UseRecord->Item : nullptr;
	}
	return Item && Item->ItemInstanceId == Record.RodItemInstanceId
		&& Item->DefinitionId == Record.RodDefinitionId && Item->Quantity == 1 ? Item : nullptr;
}

const FCatRunInventorySlot* UCatEquipmentComponent::FindFishingRodInstance(const FCatFishingUseRecord& Record) const
{
	// 绑定鱼竿只读查找流程：与可写版本共用同一实例身份规则，只返回当前仍由库存或活动部署记录持有的那一件。
	const FCatRunInventorySlot* Item = FindInventorySlotByInstanceId(Record.RodItemInstanceId);
	if (!Item)
	{
		const FCatInventoryItemUseRecord* UseRecord = FindInventoryItemUseRecord(Record.RodItemInstanceId);
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
	// 1. 当前选择快照仍服务 UI 与老调用方，但被选实例可能在旧投影格里，也可能已经被 Use 移到活动记录里。
	// 2. 如果实例还在旧投影格中，直接写回该格的耐久和断竿状态。
	// 3. 如果实例已经部署到场景，只更新活动记录里的副本，等 UnUse 时再原样归还。
	// 4. 已收口记录不再改写，避免收杆后的历史记录影响新的实例状态。
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
// 1. 新获得的物品只在当前选择缺失、旧选择无库存/无活动 Use，或已选竿已断/耐久非法时介入。
// 2. 鱼竿选择刷新会记录具体实例并读取这根实例自己的耐久；断竿收口时会从已有库存中寻找可用替代竿。
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
			// 当前库存里没有任何鱼竿实例时不能制造选择；保持原快照，让上层继续看到缺失状态。
			return;
		}
		const FCatRunInventorySlot* SelectedStoredRod =
			FindInventorySlotByInstanceId(Snapshot.RodItemInstanceId);
		const bool bSelectedStoredRodMatches = SelectedStoredRod != nullptr
			&& SelectedStoredRod->DefinitionId == Snapshot.RodDefinitionId;
		const FCatInventoryItemUseRecord* ActiveSelectedRod =
			FindInventoryItemUseRecord(Snapshot.RodItemInstanceId);
		const bool bSelectedRodIsInUse = ActiveSelectedRod && !ActiveSelectedRod->bReleased;
		const bool bSelectedRodUnavailable = Snapshot.RodDefinitionId.IsNone()
			|| (!bSelectedStoredRodMatches && !bSelectedRodIsInUse);
		const bool bStoredSelectedRodBroken = bSelectedStoredRodMatches
			&& (SelectedStoredRod->bRodBroken || !FMath::IsFinite(SelectedStoredRod->RodDurability)
				|| SelectedStoredRod->RodDurability <= 0.0);
		const bool bSelectedRodNeedsReplacement = !bSelectedRodIsInUse
			&& (Snapshot.bRodBroken || !FMath::IsFinite(Snapshot.RodDurability)
				|| Snapshot.RodDurability <= 0.0 || bStoredSelectedRodBroken);
		// 可用替代和身份回退分开计算：断竿替换只能选可用竿，缺失选择才允许回到真实存在的坏竿。
		const FCatRunInventorySlot* ReplacementSlot = PreferredUsableSlot != nullptr
			? PreferredUsableSlot : FirstUsableSlot;
		const FCatRunInventorySlot* FallbackSlot = PreferredFallbackSlot != nullptr
			? PreferredFallbackSlot : FirstFallbackSlot;
		// 当前选择完全丢失时允许落到真实存在的任一鱼竿实例；已断选择只接受可用替代竿，保留坏竿给维修链读取。
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
	// 预留结果组装流程：先写命令终态和当前 Equipment Revision，再从绑定鱼竿实例读取耐久投影；记录缺失时只返回默认耐久，不制造新会话状态。
	FCatFishingUseReservationResult Result;
	Result.SessionId = FishingSessionId;
	Result.Error = Error;
	Result.EquipmentRevision = Snapshot.Revision;
	Result.bReserved = bReserved;
	GetFishingRodDurability(FishingSessionId, Result.RemainingRodDurability, Result.bRodBroken);
	if (!Record)
	{
		Record = FindFishingUseRecord(FishingSessionId);
	}
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
	// 使用结果组装流程：先写操作是否生效，再读取同一 Session 绑定鱼竿的最新耐久和累计磨损；失败或重放结果也保持同一诊断口径。
	FCatFishingUseOperationResult Result;
	Result.SessionId = FishingSessionId;
	Result.Error = Error;
	Result.EquipmentRevision = Snapshot.Revision;
	Result.bApplied = bApplied;
	GetFishingRodDurability(FishingSessionId, Result.RemainingRodDurability, Result.bRodBroken);
	if (!Record)
	{
		Record = FindFishingUseRecord(FishingSessionId);
	}
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
	if (AActor* Owner = GetOwner(); Owner && Owner->HasAuthority())
	{
		Owner->ForceNetUpdate();
	}
	OnSnapshotChanged.Broadcast();
}
