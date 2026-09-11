#include "Equipment/CatEquipmentComponent.h"
#include "EngineUtils.h"
#include "UObject/UObjectIterator.h"

#include "Equipment/Fragments/CatEquipmentFragment_Bait.h"

#include "Equipment/CatEquipmentInventoryItemInstance.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/CatInventorySettings.h"
#include "Inventory/CatInventoryStatics.h"
#include "Logging/CatLog.h"
#include "Logging/CatLogContext.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogCatEquipment, Log, All);

namespace
{
	// 玩家随身容量读取流程：背包容量只归 InventorySettings，Equipment 不承担容量配置。
	int32 ResolvePlayerInventorySlotCapacity()
	{
		const UCatInventorySettings* InventorySettings = GetDefault<UCatInventorySettings>();
		return InventorySettings != nullptr ? InventorySettings->GetPlayerInventorySlotCapacity() : 0;
	}

}

// 构造流程：开启组件复制并关闭 Tick；Snapshot 初始 Revision=0 表示还没有随身库存提交或钓鱼选择。
UCatEquipmentComponent::UCatEquipmentComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
}

// 复制声明流程：保留父类字段并注册唯一钓鱼读模型；终态缓存和定义对象不复制。
void UCatEquipmentComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, Snapshot);
}

// Snapshot 读取流程：返回服务器钓鱼选择读模型；真实物品实例由 Owner 的 InventoryComponent 持有。
const FCatEquipmentLoadoutSnapshot& UCatEquipmentComponent::GetSnapshot() const
{
	return Snapshot;
}

// 持久化导出流程：
// 1. 先拒绝尚未结算的 Fishing 使用冻结，避免把仍在会话中的饵料伪装成已提交库存。
// 2. 再校验当前钓具选择仍能在正式库存或部署 held entry 中找到同一实例。
// 3. 导出只复制钓具选择和鱼竿摘要；随身库存格由 InventoryComponent 单独序列化。
bool UCatEquipmentComponent::ExportSnapshotFromAuthority(FCatEquipmentLoadoutSnapshot& OutSnapshot, FText& OutFailure) const
{
	OutSnapshot = FCatEquipmentLoadoutSnapshot();
	if (!GetOwner() || !GetOwner()->HasAuthority() || HasActiveFishingUse())
	{
		OutFailure = FText::FromString(TEXT("玩家库存仍有未完成 Fishing 使用事务，等待领域收口后才能保存。"));
		return false;
	}
	FCatEquipmentLoadoutSnapshot Candidate = Snapshot;
	if (!ValidatePersistentSnapshotPayload(Candidate, OutFailure))
	{
		return false;
	}
	OutSnapshot = MoveTemp(Candidate);
	return true;
}

// Retire only records still owned by this inventory; transferred rods continue in world custody.
bool UCatEquipmentComponent::RetireDeploymentAfterPersistentCapture(APlayerState& PlayerState)
{
	UCatInventoryComponent* Inventory = ResolveOwnerInventoryComponent();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Inventory) return false;
	TArray<FCatInventoryEntry> HeldEntries;
	Inventory->AppendHeldInventoryEntriesFromAuthority(HeldEntries);
	TArray<FGuid> RodItemIds;
	for (const FCatInventoryEntry& Entry : HeldEntries)
	{
		const UCatEquipmentInventoryItemInstance* Instance = Cast<UCatEquipmentInventoryItemInstance>(Entry.Instance);
		const UCatEquipmentDefinition* Definition = Instance ? GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentDefinition>(Instance->GetItemDefinitionId()) : nullptr;
		if (!Definition || !Definition->CanServeFishingRod()) continue;
		if (IsFishingRodInUse(Instance->GetItemInstanceId())) return false;
		RodItemIds.Add(Instance->GetItemInstanceId());
	}
	TArray<TWeakObjectPtr<ACatFishingRodActor>> Rods;
	for (TActorIterator<ACatFishingRodActor> It(GetWorld()); It; ++It)
		if (RodItemIds.Contains(It->GetPresentationState().ItemInstanceId)) Rods.Add(*It);
	for (const auto& Rod : Rods) if (Rod.IsValid() && !Rod->Destroy()) return false;
	for (const FGuid ItemId : RodItemIds) if (!Inventory->RetireHeldInventoryEntryFromAuthority(ItemId)) return false;
	UE_LOG(LogCatEquipment, Log, TEXT("Event=persistence_departure_deployment_retired Owner=%s PlayerId=%d Items=%d Rods=%d World=%s NetMode=%d Authority=1"),
		*GetNameSafe(GetOwner()), PlayerState.GetPlayerId(), RodItemIds.Num(), Rods.Num(), *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()));
	return true;
}

// 钓具选择校验流程：
// 1. 先确认正式库存目录可读；空选择必须同时清空实例 ID，非空定义由正式库存目录解析。
// 2. 非空选择必须能从正式可见库存解析到同一实例；鱼竿额外允许正在部署的 held entry。
// 3. 鱼竿摘要耐久必须与当前正式实例状态一致；没有鱼竿选择时摘要状态必须为空。
bool UCatEquipmentComponent::ValidatePersistentSnapshotPayload(const FCatEquipmentLoadoutSnapshot& RestoredSnapshot,
	FText& OutFailure) const
{
	OutFailure = FText::GetEmpty();
	const UCatInventorySettings* InventorySettings = GetDefault<UCatInventorySettings>();
	if (!InventorySettings)
	{
		OutFailure = FText::FromString(TEXT("正式库存目录不可用。"));
		return false;
	}
	const auto ResolveSelectedInstance = [this, InventorySettings, &OutFailure](const FName DefinitionId,
		const FGuid InstanceId, const FName ExpectedSlotId, const bool bAllowHeldEntry,
		FCatInventoryEntry& OutSlot)
	{
		if (DefinitionId.IsNone())
		{
			const bool bEmpty = !InstanceId.IsValid();
			if (!bEmpty)
			{
				OutFailure = FText::FromString(TEXT("空钓具选择携带了实例身份。"));
			}
			OutSlot = FCatInventoryEntry();
			return bEmpty;
		}
		const UCatEquipmentDefinition* Definition =
			InventorySettings->FindRuntimeDefinition<UCatEquipmentDefinition>(DefinitionId);
		if (!InstanceId.IsValid() || Definition == nullptr
			|| !Definition->CanServeFishingLoadoutSlot(ExpectedSlotId))
		{
			OutFailure = FText::FromString(TEXT("钓具选择引用了无效定义或实例。"));
			OutSlot = FCatInventoryEntry();
			return false;
		}
		if (bAllowHeldEntry && TryBuildHeldInventoryUseSlot(InstanceId, OutSlot)
			&& OutSlot.Instance->GetItemDefinitionId() == DefinitionId)
		{
			return true;
		}
		if (TryResolveSelectionInventorySlot(DefinitionId, InstanceId, OutSlot))
		{
			return true;
		}
		OutFailure = FText::FromString(TEXT("钓具选择没有指向当前正式库存中的正确实例。"));
		OutSlot = FCatInventoryEntry();
		return false;
	};
	FCatInventoryEntry SelectedRod;
	FCatInventoryEntry SelectedBait;
	FCatInventoryEntry SelectedFloat;
	FCatInventoryEntry SelectedScoopNet;
	if (!ResolveSelectedInstance(RestoredSnapshot.RodDefinitionId, RestoredSnapshot.RodItemInstanceId,
			UCatEquipmentDefinition::FishingRodLoadoutSlotId(), true, SelectedRod)
		|| !ResolveSelectedInstance(RestoredSnapshot.BaitDefinitionId, RestoredSnapshot.BaitItemInstanceId,
			UCatEquipmentDefinition::FishingBaitLoadoutSlotId(), false, SelectedBait)
		|| !ResolveSelectedInstance(RestoredSnapshot.FloatDefinitionId, RestoredSnapshot.FloatItemInstanceId,
			UCatEquipmentDefinition::FishingFloatLoadoutSlotId(), false, SelectedFloat)
		|| !ResolveSelectedInstance(RestoredSnapshot.ScoopNetDefinitionId, RestoredSnapshot.ScoopNetItemInstanceId,
			UCatEquipmentDefinition::ScoopNetLoadoutSlotId(), false, SelectedScoopNet))
	{
		return false;
	}
	if (!RestoredSnapshot.RodDefinitionId.IsNone()
		&& (RestoredSnapshot.RodDurability != CastChecked<UCatEquipmentInventoryItemInstance>(SelectedRod.Instance)->GetRodDurability()
			|| RestoredSnapshot.bRodBroken != CastChecked<UCatEquipmentInventoryItemInstance>(SelectedRod.Instance)->IsRodBroken()))
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

// 随身库存恢复入口流程：
// 1. 先确认 authority 和会话状态，当前角色仍在使用物品时不覆盖钓具选择。
// 2. 调用前 InventoryComponent 必须已经完成库存反序列化；本入口只校验选择引用和鱼竿摘要。
// 3. 成功后清掉只属于本 Character 生命周期的请求缓存和 Fishing 使用冻结记录，再发布选择读模型变化。
bool UCatEquipmentComponent::RestoreSnapshotFromAuthority(const FCatEquipmentLoadoutSnapshot& RestoredSnapshot,
	FText& OutFailure)
{
	OutFailure = FText::GetEmpty();
	if (!GetOwner() || !GetOwner()->HasAuthority() || HasActiveFishingUse() || HasActiveInventoryItemUse()
		|| ResolveOwnerInventoryComponent() == nullptr)
	{
		OutFailure = FText::FromString(TEXT("钓具选择恢复上下文不可用或存在活动使用记录。"));
		return false;
	}
	if (!ValidatePersistentSnapshotPayload(RestoredSnapshot, OutFailure))
	{
		return false;
	}
	Snapshot = RestoredSnapshot;
	Snapshot.Revision = FMath::Max<int64>(1, Snapshot.Revision + 1);
	TerminalCache.Reset();
	TerminalPayloadByKey.Reset();
	FishingUseRecords.Reset();
	PublishSnapshot();
	return true;
}

// 当前钓鱼选择配置流程：
// 1. 先用 RequestId 返回既有终态；部署中的当前鱼竿可继续作为选择上下文，但不能切到另一根鱼竿。
// 2. 每次提交都必须通过正式库存目录、authority、Revision、定义运行能力、实例身份和 Profile 解锁证明。
// 3. 正式角色从 InventoryComponent 精确解析钓具实例；部署中的当前鱼竿只读库存 held entry。
// 4. 同一套定义和实例选择直接返回 AlreadyResolved；不同选择会切换当前钓鱼选择，并从鱼竿实例读取耐久。
// 5. 成功时只写钓鱼选择、实例身份和当前鱼竿运行态，并发布给当前读模型消费者。
FCatDomainCommandResult UCatEquipmentComponent::ConfigureLoadoutFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const FName RodDefinitionId, const FName BaitDefinitionId,
	const FName FloatDefinitionId, const FName ScoopNetDefinitionId, const FName RodSkinDefinitionId,
	const FGuid RodItemInstanceId, const FGuid BaitItemInstanceId, const FGuid FloatItemInstanceId,
	const FGuid ScoopNetItemInstanceId)
{
	// 钓具配置提交流程：
	// 1. RequestId 命中终态缓存时直接返回首次结果，避免重放请求重新选择或推进 Revision。
	// 2. 再校验服务器权威、定义运行能力、解锁权限和版本；任一失败只写错误码，不改变当前选择和库存状态。
	// 3. 当前选中鱼竿正在部署时，只从正式库存 held entry 确认同一实例；没有正式库存时不启用部署 Use。
	// 4. 所有候选装备都必须带具体实例 ID，再解析成正式库存里的占用槽位并写入 Snapshot。
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
	const UCatInventorySettings* InventorySettings = GetDefault<UCatInventorySettings>();
	UCatEquipmentDefinition* Rod = InventorySettings
		? InventorySettings->FindRuntimeDefinition<UCatEquipmentDefinition>(RodDefinitionId) : nullptr;
	UCatEquipmentDefinition* Bait = InventorySettings
		? InventorySettings->FindRuntimeDefinition<UCatEquipmentDefinition>(BaitDefinitionId) : nullptr;
	UCatEquipmentDefinition* Float = InventorySettings
		? InventorySettings->FindRuntimeDefinition<UCatEquipmentDefinition>(FloatDefinitionId) : nullptr;
	UCatEquipmentDefinition* Scoop = !ScoopNetDefinitionId.IsNone() && InventorySettings
		? InventorySettings->FindRuntimeDefinition<UCatEquipmentDefinition>(ScoopNetDefinitionId) : nullptr;
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const ACatfishingPlayerState* PlayerState = OwnerPawn ? OwnerPawn->GetPlayerState<ACatfishingPlayerState>() : nullptr;
	if (!Settings || Settings->ProfileLoadoutTrustPolicy != ECatDomainPolicy::Enabled)
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
	else if (!RodItemInstanceId.IsValid() || !BaitItemInstanceId.IsValid() || !FloatItemInstanceId.IsValid()
		|| (ScoopNetDefinitionId.IsNone() && ScoopNetItemInstanceId.IsValid())
		|| (!ScoopNetDefinitionId.IsNone() && !ScoopNetItemInstanceId.IsValid()))
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (!Rod->CanServeFishingRod() || !Bait->CanServeFishingBait()
		|| !Float->CanServeFishingFloat() || (Scoop && !Scoop->CanServeScoopNet()))
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
		FCatInventoryEntry ActiveSelectedRodSlot;
		bool bSelectedRodIsInUse = false;
		if (Snapshot.RodItemInstanceId.IsValid())
		{
			bSelectedRodIsInUse = TryBuildHeldInventoryUseSlot(
				Snapshot.RodItemInstanceId, ActiveSelectedRodSlot)
				&& ActiveSelectedRodSlot.Instance->GetItemDefinitionId() == Snapshot.RodDefinitionId
				&& ActiveSelectedRodSlot.Instance->GetItemInstanceId() == Snapshot.RodItemInstanceId;
		}
		const bool bRequestsActiveSelectedRod = bSelectedRodIsInUse
			&& RodDefinitionId == ActiveSelectedRodSlot.Instance->GetItemDefinitionId()
			&& RodItemInstanceId == ActiveSelectedRodSlot.Instance->GetItemInstanceId();
		if (bSelectedRodIsInUse && !bRequestsActiveSelectedRod)
		{
			Result.Error = ECatDomainCommandError::InvalidPhase;
		}
		else
		{
			const auto ResolveSelectedRodSlot =
				[this, ActiveSelectedRodSlot, bRequestsActiveSelectedRod](
					const FName DefinitionId, const FGuid ItemInstanceId,
					FCatInventoryEntry& OutSlot) -> bool
				{
					if (bRequestsActiveSelectedRod)
					{
						OutSlot = ActiveSelectedRodSlot;
						return OutSlot.Instance->GetItemDefinitionId() == DefinitionId
							&& OutSlot.Instance->GetItemInstanceId() == ItemInstanceId;
					}
					return TryResolveSelectionInventorySlot(DefinitionId, ItemInstanceId, OutSlot);
				};
			FCatInventoryEntry RodSlot;
			FCatInventoryEntry BaitSlot;
			FCatInventoryEntry FloatSlot;
			FCatInventoryEntry ScoopSlot;
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
					? FGuid() : ScoopSlot.Instance->GetItemInstanceId();
				const bool bSameLoadout = Snapshot.RodDefinitionId == RodDefinitionId
					&& Snapshot.RodItemInstanceId == RodSlot.Instance->GetItemInstanceId()
					&& Snapshot.BaitDefinitionId == BaitDefinitionId
					&& Snapshot.BaitItemInstanceId == BaitSlot.Instance->GetItemInstanceId()
					&& Snapshot.FloatDefinitionId == FloatDefinitionId
					&& Snapshot.FloatItemInstanceId == FloatSlot.Instance->GetItemInstanceId()
					&& Snapshot.ScoopNetDefinitionId == ScoopNetDefinitionId
					&& Snapshot.ScoopNetItemInstanceId == NewScoopItemInstanceId
					&& Snapshot.RodSkinDefinitionId == RodSkinDefinitionId
					&& Snapshot.RodDurability == CastChecked<UCatEquipmentInventoryItemInstance>(RodSlot.Instance)->GetRodDurability()
					&& Snapshot.bRodBroken == CastChecked<UCatEquipmentInventoryItemInstance>(RodSlot.Instance)->IsRodBroken();
				if (bSameLoadout)
				{
					Result.Error = ECatDomainCommandError::AlreadyResolved;
					Result.Revision = Snapshot.Revision;
					TerminalCache.Add(Key, Result);
					return Result;
				}
				Snapshot.RodDefinitionId = RodDefinitionId;
				Snapshot.RodItemInstanceId = RodSlot.Instance->GetItemInstanceId();
				Snapshot.BaitDefinitionId = BaitDefinitionId;
				Snapshot.BaitItemInstanceId = BaitSlot.Instance->GetItemInstanceId();
				Snapshot.FloatDefinitionId = FloatDefinitionId;
				Snapshot.FloatItemInstanceId = FloatSlot.Instance->GetItemInstanceId();
				Snapshot.ScoopNetDefinitionId = ScoopNetDefinitionId;
				Snapshot.ScoopNetItemInstanceId = NewScoopItemInstanceId;
				Snapshot.RodSkinDefinitionId = RodSkinDefinitionId;
				Snapshot.RodDurability = CastChecked<UCatEquipmentInventoryItemInstance>(RodSlot.Instance)->GetRodDurability();
				Snapshot.bRodBroken = CastChecked<UCatEquipmentInventoryItemInstance>(RodSlot.Instance)->IsRodBroken();
				++Snapshot.Revision;
				PublishSnapshot();
				Result.bCommitted = true;
				Result.Error = ECatDomainCommandError::None;
			}
		}
	}
	Result.Revision = Snapshot.Revision;
	TerminalCache.Add(Key, Result);
	return Result;
}

#if WITH_DEV_AUTOMATION_TESTS
// 自动化数量授予预检流程：
// 1. 先从正式库存目录读取定义，并确认 RequestId、authority、定义运行能力和授予数量都成立；失败时不读取或补写库存格。
// 2. 已经缓存过同 RequestId 的授予结果时放行重放，让测试重试能拿回原回执而不是被当前容量误拦。
// 3. Owner 必须提供正式 InventoryComponent；容量和堆叠预检统一交给 InventoryComponent，这里保持只读。
ECatDomainCommandError UCatEquipmentComponent::ValidateInventoryQuantityGrant(const FGuid RequestId,
	const FName DefinitionId, const int32 Quantity) const
{
	UCatEquipmentDefinition* Definition = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentDefinition>(DefinitionId);
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

// 自动化数量授予提交流程：
// 1. 先拒绝无效 RequestId，并用 RequestId、定义和数量签名保护终态重放；载荷漂移直接拒绝且不改库存。
// 2. 正式库存组件存在时先按配置补齐槽位，再把已解析定义交给 InventoryComponent 的统一发货事务。
// 3. 正式库存写入成功后只从 InventoryComponent 重建 Equipment 读模型，并按新增定义修正钓鱼选择。
// 4. 选择刷新失败只记录诊断，不回滚正式库存事实；后续刷新仍以 InventoryComponent 为准。
// 5. 没有正式库存组件时返回依赖错误；Equipment 拒绝充当随身库存备用写入口。
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
	UCatEquipmentDefinition* Definition = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentDefinition>(DefinitionId);
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
				RequestId, Definition, Quantity);
		Result.bCommitted = InventoryGrant.bCommitted;
		Result.Error = InventoryGrant.Error;
		if (InventoryGrant.bCommitted
			&& !RefreshLoadoutFromInventoryComponentFromAuthority(Definition, DefinitionId))
		{
			UE_LOG(LogCatEquipment, Warning,
				TEXT("Event=equipment_loadout_refresh_failed Operation=GrantInventoryQuantity Owner=%s Request=%s Definition=%s SnapshotRevision=%lld"),
				*GetNameSafe(GetOwner()), *RequestId.ToString(EGuidFormats::DigitsWithHyphens),
				*DefinitionId.ToString(), Snapshot.Revision);
		}
	}
	Result.Revision = Snapshot.Revision;
	TerminalCache.Add(Key, Result);
	TerminalPayloadByKey.Add(Key, PayloadSignature);
	return Result;
}

// 自动化非数量授予预检流程：
// 1. 先按 RequestId 和定义 ID 查询既有终态载荷，合法重放放行，载荷漂移拒绝。
// 2. 再确认当前组件属于 authority 角色，并读取正式定义和单实例容量。
// 3. 正式库存组件是唯一容量裁决者；缺少它时直接返回依赖错误，Equipment 读模型不参与容量裁决。
// 4. 非数量物品仍按单件载荷交给 InventoryComponent 预演，避免测试夹具自己维护容量规则。
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
	UCatEquipmentDefinition* Definition = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentDefinition>(DefinitionId);
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

// 自动化非数量授予提交流程：
// 1. 先用 RequestId 和定义 ID 找终态缓存；合法重放只返回首次结果，不重复增加库存数量或推进 Revision。
// 2. 正式库存组件存在时先按配置补齐槽位，再把已解析定义交给 InventoryComponent 的统一发货事务。
// 3. 正式库存写入成功后刷新 Equipment 读模型和钓鱼选择；刷新失败只记诊断，库存事实保持已提交状态。
// 4. 没有正式库存组件时返回依赖错误；库存写入只由 InventoryComponent 执行。
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

	UCatEquipmentDefinition* Definition = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentDefinition>(DefinitionId);
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
				RequestId, Definition, 1);
		Result.bCommitted = InventoryGrant.bCommitted;
		Result.Error = InventoryGrant.Error;
		if (InventoryGrant.bCommitted
			&& !RefreshLoadoutFromInventoryComponentFromAuthority(Definition, DefinitionId))
		{
			UE_LOG(LogCatEquipment, Warning,
				TEXT("Event=equipment_loadout_refresh_failed Operation=GrantEquipment Owner=%s Request=%s Definition=%s SnapshotRevision=%lld"),
				*GetNameSafe(GetOwner()), *RequestId.ToString(EGuidFormats::DigitsWithHyphens),
				*DefinitionId.ToString(), Snapshot.Revision);
		}
	}
	Result.Revision = Snapshot.Revision;
	TerminalCache.Add(Key, Result);
	TerminalPayloadByKey.Add(Key, PayloadSignature);
	return Result;
}
#endif

FCatInventoryItemUseResult UCatEquipmentComponent::Use(const FGuid RequestId, const int64 ExpectedRevision,
	const FGuid ItemInstanceId, const int32 Quantity)
{
	// 物品使用流程：
	// 1. 先做 Equipment 入口自己的 authority 和载荷校验；通过后立即进入正式 InventoryComponent。
	// 2. ExpectedRevision 只保护钓具选择读模型；库存不再以请求方看到的版本拒绝本次正式事务。
	// 3. 库存负责定义裁决、扣量、借出、活动记录和终态重放，Equipment 的提交前回调只检查选择读模型版本是否仍然匹配。
	// 4. 库存 mutation 后，提交后回调刷新读模型并同步部署鱼竿选择；回调失败时库存会在写终态缓存前回滚刚才的库存变化。
	// 5. 没有正式库存组件时返回依赖错误；库存扣量和借出只通过 InventoryComponent 执行。
	FCatInventoryItemUseResult Result;
	Result.RequestId = RequestId;
	if (!GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid() || !ItemInstanceId.IsValid()
		|| Quantity <= 0)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}

	UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent();
	if (OwnerInventory == nullptr)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	const FCatEquipmentLoadoutSnapshot SavedSnapshot = Snapshot;
	const FString PayloadContext = FString::Printf(
		TEXT("ExpectedEquipmentRevision=%lld"), ExpectedRevision);
	Result = OwnerInventory->UseItemInstanceFromAuthority(
		RequestId,
		ItemInstanceId,
		Quantity,
		PayloadContext,
		[this, ExpectedRevision](FCatInventoryItemUseResult&)
		{
			return Snapshot.Revision == ExpectedRevision
				? ECatDomainCommandError::None : ECatDomainCommandError::RevisionConflict;
		},
		[this, SavedSnapshot](FCatInventoryItemUseResult& CommittedResult)
		{
			const UCatEquipmentDefinition* Definition =
				GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentDefinition>(
					CommittedResult.Item.Instance->GetItemDefinitionId());
			if (Definition == nullptr || !RefreshLoadoutFromInventoryComponentFromAuthority())
			{
				Snapshot = SavedSnapshot;
				return false;
			}
			if (Definition->CanServeFishingRod())
			{
				// 部署鱼竿已离开可见库存格，但当前选择仍要指向同一活动实例；小容差只过滤浮点转换微差，不吞掉真实耐久变化。
				const FName PreviousRodDefinitionId = Snapshot.RodDefinitionId;
				const FGuid PreviousRodItemInstanceId = Snapshot.RodItemInstanceId;
				const double PreviousRodDurability = Snapshot.RodDurability;
				const bool bPreviousRodBroken = Snapshot.bRodBroken;
				Snapshot.RodDefinitionId = CommittedResult.Item.Instance->GetItemDefinitionId();
				Snapshot.RodItemInstanceId = CommittedResult.Item.Instance->GetItemInstanceId();
				Snapshot.RodDurability = CastChecked<UCatEquipmentInventoryItemInstance>(CommittedResult.Item.Instance)->GetRodDurability();
				Snapshot.bRodBroken = CastChecked<UCatEquipmentInventoryItemInstance>(CommittedResult.Item.Instance)->IsRodBroken();
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
			return true;
		});
	return Result;
}

bool UCatEquipmentComponent::TryReplayInventoryItemUseTerminal(const FGuid RequestId, const int64 ExpectedRevision,
	const FGuid ItemInstanceId, const int32 Quantity, FCatInventoryItemUseResult& OutResult) const
{
	// 库存 Use 重放查询流程：
	// 1. 先复原 Equipment 版本组成的请求签名，再转交 InventoryComponent 查询正式终态缓存。
	// 2. 查询不读取当前库存格或定义，避免成功扣除后的空格阻断二段提交。
	// 3. 没有缓存返回 false，调用方继续执行首次提交 preflight；载荷漂移返回 true+InvalidPayload，阻止同 RequestId 改目标。
	// 4. 命中缓存时返回库存标记后的重放结果，让命令调用方按首次成功或失败决定是否补放后续领域提交。
	OutResult = FCatInventoryItemUseResult();
	OutResult.RequestId = RequestId;
	const UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent();
	if (OwnerInventory == nullptr || !RequestId.IsValid() || !ItemInstanceId.IsValid() || Quantity <= 0)
	{
		return false;
	}
	const FString PayloadContext = FString::Printf(
		TEXT("ExpectedEquipmentRevision=%lld"), ExpectedRevision);
	const bool bFound = OwnerInventory->TryReplayItemUseTerminalFromAuthority(
		RequestId, ItemInstanceId, Quantity, PayloadContext, OutResult);
	return bFound;
}

FCatInventoryItemUseResult UCatEquipmentComponent::UnUse(const FGuid RequestId, const FGuid ItemInstanceId)
{
	// 物品停止使用流程：
	// 1. 先校验 Equipment 入口的 authority 和请求载荷，再把正式收口交给 InventoryComponent。
	// 2. 库存从 held entry 当前实例重建归还载荷并执行定义侧 UnUse 裁决，活动 Use 事实只保存在 InventoryComponent。
	// 3. 库存归还成功后，提交后回调刷新读模型并按收回鱼竿状态修正选择；回调失败时库存会重新借回或恢复 held entry。
	// 4. 没有正式库存组件时返回依赖错误；归还路径只按 held entry 和实例身份重建库存格。
	FCatInventoryItemUseResult Result;
	Result.RequestId = RequestId;
	if (!GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid() || !ItemInstanceId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent();
	if (OwnerInventory == nullptr)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	if (IsFishingRodInUse(ItemInstanceId))
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
		UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_rod_return_rejected RequestId=%s RodItemInstanceId=%s Reason=ActiveFishingUse World=%s Owner=%s"),
			*RequestId.ToString(), *ItemInstanceId.ToString(), *GetNameSafe(GetWorld()), *GetNameSafe(GetOwner()));
		return Result;
	}
	const FCatEquipmentLoadoutSnapshot SavedSnapshot = Snapshot;
	Result = OwnerInventory->UnUseItemInstanceFromAuthority(
		RequestId,
		ItemInstanceId,
		GetConfiguredInventorySlotCapacity(),
		FString(),
		[this, SavedSnapshot](FCatInventoryItemUseResult& CommittedResult)
		{
			const UCatEquipmentDefinition* Definition =
				GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentDefinition>(
					CommittedResult.Item.Instance->GetItemDefinitionId());
			if (Definition == nullptr
				|| !RefreshLoadoutFromInventoryComponentFromAuthority(
					Definition, CommittedResult.Item.Instance->GetItemDefinitionId()))
			{
				Snapshot = SavedSnapshot;
				return false;
			}
			if (Definition->CanServeFishingRod()
				&& Snapshot.RodItemInstanceId == CommittedResult.Item.Instance->GetItemInstanceId())
			{
				Snapshot.RodDefinitionId = CommittedResult.Item.Instance->GetItemDefinitionId();
				Snapshot.RodDurability = CastChecked<UCatEquipmentInventoryItemInstance>(CommittedResult.Item.Instance)->GetRodDurability();
				Snapshot.bRodBroken = CastChecked<UCatEquipmentInventoryItemInstance>(CommittedResult.Item.Instance)->IsRodBroken();
			}
			return true;
		});
	return Result;
}

FCatFishingUseFreezeResult UCatEquipmentComponent::BeginFishingUse(const FGuid FishingSessionId,
	const FGuid RodItemInstanceId, const FGuid BaitItemInstanceId, const FGuid FloatItemInstanceId,
	const FName RodDefinitionId, const FName BaitDefinitionId, const FName FloatDefinitionId,
	const int64 ExpectedRevision, UCatInventoryComponent* RodInventoryComponent)
{
	// 建立 Fishing 使用冻结的流程：
	// 1. 先用 SessionId 返回已存在的终态，保证 FishingSession 重放不会再检查或再占库存。
	// 2. 再校验 authority、Rod/Bait/Float 运行能力、Revision、当前钓鱼选择和饵/漂实例身份，任何不一致都保持快照不变。
	// 3. 鱼竿实例必须来自传入的正式库存 held-entry；借竿时它属于部署者，饵和漂仍来自当前操作者库存。
	// 4. 通过后立即从正式库存扣掉选中鱼饵实例的一份，并只把可归还的定义放进本 Session 记录。
	// 5. 记录只保存这场 Fishing 自己要消耗或归还的饵料和耐久累计，不参与库存拖放的通用占用判断。
	if (const FCatFishingUseRecord* ExistingRecord = FindFishingUseRecord(FishingSessionId))
	{
		const bool bBaitFrozen = ExistingRecord->bBaitQuantityFrozen && !ExistingRecord->bBaitCommitted
			&& !ExistingRecord->bReleased;
		return MakeFishingUseFreezeResult(FishingSessionId, ECatDomainCommandError::AlreadyResolved, bBaitFrozen,
			bBaitFrozen ? ExistingRecord : nullptr);
	}
	const UCatInventorySettings* InventorySettings = GetDefault<UCatInventorySettings>();
	UCatEquipmentDefinition* Rod = InventorySettings
		? InventorySettings->FindRuntimeDefinition<UCatEquipmentDefinition>(RodDefinitionId) : nullptr;
	UCatEquipmentDefinition* Bait = InventorySettings
		? InventorySettings->FindRuntimeDefinition<UCatEquipmentDefinition>(BaitDefinitionId) : nullptr;
	UCatEquipmentDefinition* Float = InventorySettings
		? InventorySettings->FindRuntimeDefinition<UCatEquipmentDefinition>(FloatDefinitionId) : nullptr;
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return MakeFishingUseFreezeResult(FishingSessionId, ECatDomainCommandError::DependencyUnavailable, false);
	}
	if (!FishingSessionId.IsValid() || !RodItemInstanceId.IsValid() || !BaitItemInstanceId.IsValid()
		|| !FloatItemInstanceId.IsValid() || RodDefinitionId.IsNone()
		|| BaitDefinitionId.IsNone() || FloatDefinitionId.IsNone() || !Rod || !Bait || !Float
		|| !Rod->CanServeFishingRod() || !Bait->CanServeFishingBait()
		|| !Float->CanServeFishingFloat())
	{
		return MakeFishingUseFreezeResult(FishingSessionId, ECatDomainCommandError::InvalidPayload, false);
	}
	if (Snapshot.Revision != ExpectedRevision)
	{
		return MakeFishingUseFreezeResult(FishingSessionId, ECatDomainCommandError::RevisionConflict, false);
	}
	if (Snapshot.BaitDefinitionId != BaitDefinitionId || Snapshot.BaitItemInstanceId != BaitItemInstanceId
		|| Snapshot.FloatDefinitionId != FloatDefinitionId || Snapshot.FloatItemInstanceId != FloatItemInstanceId)
	{
		return MakeFishingUseFreezeResult(FishingSessionId, ECatDomainCommandError::InvalidPayload, false);
	}
	if (!Bait->bRunConsumable)
	{
		return MakeFishingUseFreezeResult(FishingSessionId, ECatDomainCommandError::InvalidPayload, false);
	}
	UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent();
	if (OwnerInventory == nullptr)
	{
		return MakeFishingUseFreezeResult(FishingSessionId, ECatDomainCommandError::DependencyUnavailable, false);
	}
	UCatInventoryComponent* RodInventory = RodInventoryComponent != nullptr ? RodInventoryComponent : OwnerInventory;
	if (RodInventory == nullptr)
	{
		return MakeFishingUseFreezeResult(FishingSessionId, ECatDomainCommandError::DependencyUnavailable, false);
	}
	FCatInventoryEntry RodUseSlotStorage;
	FCatInventoryEntry FormalBaitSlot;
	FCatInventoryEntry FormalFloatSlot;
	int32 FormalBaitSlotIndex = INDEX_NONE;
	const FCatInventoryEntry* RodUseSlot = nullptr;
	const FCatInventoryEntry* BaitSlot = nullptr;
	const FCatInventoryEntry* FloatSlot = nullptr;
	// Fishing 只消费正式库存里的数量物；Equipment 读模型在这里仅提供当前选择，不能再当库存事实源。
	const FCatInventoryEntry* FormalRodEntry = RodInventory->FindHeldInventoryEntryFromAuthority(RodItemInstanceId);
	if (FormalRodEntry != nullptr
		&& TryReadEquipmentInventoryEntry(*FormalRodEntry, RodUseSlotStorage))
	{
		RodUseSlot = &RodUseSlotStorage;
	}
	FormalBaitSlotIndex = OwnerInventory->FindInventorySlotIndexFromInstanceId(BaitItemInstanceId);
	const int32 FormalFloatSlotIndex = OwnerInventory->FindInventorySlotIndexFromInstanceId(FloatItemInstanceId);
	const FCatInventoryEntry* FormalBaitEntry = OwnerInventory->GetInventoryEntryAtSlot(FormalBaitSlotIndex);
	const FCatInventoryEntry* FormalFloatEntry = OwnerInventory->GetInventoryEntryAtSlot(FormalFloatSlotIndex);
	if (FormalBaitEntry != nullptr
		&& TryReadEquipmentInventoryEntry(*FormalBaitEntry, FormalBaitSlot))
	{
		BaitSlot = &FormalBaitSlot;
	}
	if (FormalFloatEntry != nullptr
		&& TryReadEquipmentInventoryEntry(*FormalFloatEntry, FormalFloatSlot))
	{
		FloatSlot = &FormalFloatSlot;
	}
	if (!RodUseSlot || RodUseSlot->Instance->GetItemDefinitionId() != RodDefinitionId
		|| RodUseSlot->Instance->GetItemInstanceId() != RodItemInstanceId || RodUseSlot->StackCount != 1
		|| !BaitSlot || BaitSlot->Instance->GetItemDefinitionId() != BaitDefinitionId
		|| !FloatSlot || FloatSlot->Instance->GetItemDefinitionId() != FloatDefinitionId)
	{
		return MakeFishingUseFreezeResult(FishingSessionId, ECatDomainCommandError::NotFound, false);
	}
	if (CastChecked<UCatEquipmentInventoryItemInstance>(RodUseSlot->Instance)->IsRodBroken() || !FMath::IsFinite(CastChecked<UCatEquipmentInventoryItemInstance>(RodUseSlot->Instance)->GetRodDurability())
		|| CastChecked<UCatEquipmentInventoryItemInstance>(RodUseSlot->Instance)->GetRodDurability() <= 0.0)
	{
		return MakeFishingUseFreezeResult(FishingSessionId, ECatDomainCommandError::InvalidPhase, false);
	}
	if (FloatSlot->StackCount <= 0)
	{
		return MakeFishingUseFreezeResult(FishingSessionId, ECatDomainCommandError::CapacityExceeded, false);
	}
	for (TObjectIterator<UCatEquipmentComponent> It; It; ++It)
	{
		if (It->GetWorld() != GetWorld()) continue;
		for (const auto& Pair : It->FishingUseRecords)
			if (!Pair.Value.bReleased && !Pair.Value.bReturnPending && Pair.Value.RodInventory.Get() == RodInventory
				&& Pair.Value.RodItemInstanceId == RodItemInstanceId)
				return MakeFishingUseFreezeResult(FishingSessionId, ECatDomainCommandError::InvalidPhase, false);
	}
	if (!OwnerInventory->ConsumeItemAtSlotInternal(FormalBaitSlotIndex, 1, false))
		return MakeFishingUseFreezeResult(FishingSessionId, ECatDomainCommandError::CapacityExceeded, false);
	FCatFishingUseRecord Record;
	Record.RodItemInstanceId = RodItemInstanceId;
	Record.RodDefinitionId = RodDefinitionId;
	Record.RodInventory = RodInventory;
	Record.FrozenBaitDefinitionId = BaitDefinitionId;
	Record.bBaitQuantityFrozen = true;
	FishingUseRecords.Add(FishingSessionId, Record);
	ReconcileLoadoutSelectionsWithInventory(Bait, BaitDefinitionId);
	++Snapshot.Revision;
	const FCatFishingUseFreezeResult Result = MakeFishingUseFreezeResult(FishingSessionId, ECatDomainCommandError::None, true);
	UE_LOG(LogCatEquipment, Log, TEXT("Event=fishing_use_frozen SessionId=%s RodItemInstanceId=%s World=%s NetMode=%d Authority=1 Owner=%s Result=CommittedBeforeNotify"),
		*FishingSessionId.ToString(), *RodItemInstanceId.ToString(), *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), *GetNameSafe(GetOwner()));
	OwnerInventory->BroadcastInventoryChange(FormalBaitSlotIndex);
	PublishSnapshot();
	return Result;
}

FCatFishingUseOperationResult UCatEquipmentComponent::CommitFishingBaitDeferred(const FGuid FishingSessionId)
{
	// 确认消耗鱼饵的流程：
	// 1. 先找到 Begin 阶段留下的记录；没有记录说明 Fishing 从未拿到装备使用权。
	// 2. 已释放或已提交的记录只返回终态，不允许重复处理同一份暂存饵。
	// 3. 只有该 Session 自己仍处于活动冻结态才能提交，已结束会话记录不会补消耗。
	// 4. Begin 已扣饵；此处关闭早收退款，保留定义作为上鱼后单次返还的凭证。
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
	if (Record->bBaitQuantityFrozen)
	{
		if (Record->FrozenBaitDefinitionId.IsNone())
		{
			return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::InvalidPhase, false, Record);
		}
		Record->bBaitQuantityFrozen = false;
	}
	Record->bBaitCommitted = true;
	return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::None, true, Record);
}

FCatFishingUseOperationResult UCatEquipmentComponent::ApplyFishingRodWear(const FGuid FishingSessionId,
	const int64 WearSequence, const double AbsoluteTotal)
{
	// 鱼竿磨损写回流程：
	// 1. 先按 Session 读取 Begin 记录并建立统一拒绝日志；authority、载荷、会话状态、序号和饵料提交缺任一项都不写耐久。
	// 2. 再按 Begin 冻结的实例 ID 查正式库存实例；缺少正式库存时拒绝，耐久只写库存实例。
	// 3. 最后只按累计磨损差额写回实例本体，并在当前选择仍是同一竿时同步 Snapshot；重复序号只返回终态不重扣。
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
	if (Record->bReleased || Record->bReturnPending)
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
	if (ResolveOwnerInventoryComponent() == nullptr)
	{
		return Reject(ECatDomainCommandError::DependencyUnavailable, TEXT("MissingInventory"));
	}
	const double Delta = AbsoluteTotal - Record->AbsoluteRodWear;
	double Before = 0.0;
	bool bWasBroken = false;
	double Remaining = 0.0;
	bool bBroken = false;
	FCatInventoryEntry RodItem;
	UCatEquipmentInventoryItemInstance* FormalRodInstance =
		ResolveFishingRodFormalInstanceFromInventory(*Record, RodItem);
	if (FormalRodInstance == nullptr || !FMath::IsFinite(CastChecked<UCatEquipmentInventoryItemInstance>(RodItem.Instance)->GetRodDurability())
		|| CastChecked<UCatEquipmentInventoryItemInstance>(RodItem.Instance)->GetRodDurability() < 0.0)
	{
		return Reject(ECatDomainCommandError::NotFound, TEXT("BoundRodUnavailable"));
	}

	// 正式库存实例是鱼竿状态事实源；UnUse 会从 held entry 重新转换当前实例，耐久只写库存实例。
	Before = FormalRodInstance->GetRodDurability();
	bWasBroken = FormalRodInstance->IsRodBroken();
	if (!FMath::IsFinite(Before) || Before < 0.0)
	{
		return Reject(ECatDomainCommandError::NotFound, TEXT("BoundRodUnavailable"));
	}
	Remaining = bWasBroken ? 0.0 : FMath::Max(0.0, Before - Delta);
	bBroken = bWasBroken || Remaining <= 0.0;
	FormalRodInstance->SetRodRuntimeStateFromAuthority(Remaining, bBroken);
	Remaining = FormalRodInstance->GetRodDurability();
	bBroken = FormalRodInstance->IsRodBroken();
	Record->LastWearSequence = WearSequence;
	Record->AbsoluteRodWear = AbsoluteTotal;
	if (Snapshot.RodItemInstanceId == Record->RodItemInstanceId)
	{
		Snapshot.RodDurability = Remaining;
		Snapshot.bRodBroken = bBroken;
	}
	const bool bChanged = Before != Remaining || bWasBroken != bBroken;
	if (bChanged)
	{
		++Snapshot.Revision;

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
	UCatEquipmentComponent* RodEquipment = Record->RodInventory.IsValid() && Record->RodInventory->GetOwner()
		? Record->RodInventory->GetOwner()->FindComponentByClass<UCatEquipmentComponent>() : nullptr;
	const bool bUpdateRodOwner = bChanged && RodEquipment && RodEquipment != this
		&& RodEquipment->Snapshot.RodItemInstanceId == Record->RodItemInstanceId;
	if (bUpdateRodOwner)
	{
		RodEquipment->Snapshot.RodDurability = Remaining;
		RodEquipment->Snapshot.bRodBroken = bBroken;
		++RodEquipment->Snapshot.Revision;
	}
	// A notification may transfer the record into custody. Freeze the receipt before any callback.
	const FCatFishingUseOperationResult Result = MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::None, true, Record);
	if (bUpdateRodOwner) RodEquipment->PublishSnapshot();
	if (bChanged) PublishSnapshot();
	return Result;
}

bool UCatEquipmentComponent::GetFishingRodDurability(const FGuid FishingSessionId,
	double& OutDurability, bool& OutBroken) const
{
	// 耐久读取流程：按会话短记录找到 Begin 冻结的鱼竿实例，再只从正式库存实例本体读取，避免换选后把另一根鱼竿当成已结束会话结果。
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

	FCatInventoryEntry RodItem;
	const UCatEquipmentInventoryItemInstance* FormalRodInstance =
		ResolveFishingRodFormalInstanceFromInventory(*Record, RodItem);
	if (FormalRodInstance == nullptr || !FMath::IsFinite(CastChecked<UCatEquipmentInventoryItemInstance>(RodItem.Instance)->GetRodDurability())
		|| CastChecked<UCatEquipmentInventoryItemInstance>(RodItem.Instance)->GetRodDurability() < 0.0)
	{
		return false;
	}
	OutDurability = FormalRodInstance->GetRodDurability();
	OutBroken = FormalRodInstance->IsRodBroken() || OutDurability <= 0.0;
	return FMath::IsFinite(OutDurability) && OutDurability >= 0.0;
}

FCatFishingUseOperationResult UCatEquipmentComponent::ReleaseFishingUse(const FGuid FishingSessionId, const bool bReturnCaughtBait)
{
	FCatFishingUseRecord* Record = FindFishingUseRecord(FishingSessionId);
	if (!Record) return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::NotFound, false);
	if (Record->bReleased) return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::AlreadyResolved, false, Record);
	if (!GetOwner() || !GetOwner()->HasAuthority())
		return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::DependencyUnavailable, false, Record);
	UCatInventoryComponent* Inventory = ResolveOwnerInventoryComponent();
	Record->bReturnCaughtBait |= bReturnCaughtBait;
	const bool bReturnBait = (Record->bBaitQuantityFrozen && !Record->bBaitCommitted)
		|| (Record->bReturnCaughtBait && Record->bBaitCommitted && !Record->FrozenBaitDefinitionId.IsNone());
	if (bReturnBait)
	{
		UCatEquipmentDefinition* Bait = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentDefinition>(Record->FrozenBaitDefinitionId);
		if (!Inventory || !Bait || !Bait->CanServeFishingBait())
			return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::DependencyUnavailable, false, Record);
		FCatInventoryReceiveBatch Batch;
		FCatInventoryDefinitionEntry& Entry = Batch.DefinitionEntries.AddDefaulted_GetRef();
		Entry.ItemDefinition = Bait;
		Entry.Count = 1;
		if (!Inventory->TryAddInventoryBatchInternal(Batch, false))
		{
			if (!Record->bReturnPending)
				UE_LOG(LogCatEquipment, Warning, TEXT("Event=fishing_bait_return_rejected SessionId=%s Owner=%s World=%s NetMode=%d Authority=1 Reason=InventoryCapacity Result=PendingUntilSpaceAvailable"),
					*FishingSessionId.ToString(), *GetNameSafe(GetOwner()), *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()));
			Record->bReturnPending = true;
			WatchPendingBaitReturns();
			return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::CapacityExceeded, false, Record);
		}
		ReconcileLoadoutSelectionsWithInventory(Bait, Record->FrozenBaitDefinitionId);
		Record->FrozenBaitDefinitionId = NAME_None;
		Record->bBaitQuantityFrozen = false;
	}
	Record->bReleased = true;
	Record->bReturnPending = false;
	++Snapshot.Revision;
	const FCatFishingUseOperationResult Result = MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::None, true, Record);
	UE_LOG(LogCatEquipment, Log, TEXT("Event=fishing_use_released SessionId=%s Owner=%s World=%s NetMode=%d Authority=1 BaitReturned=%d Result=ClosedBeforeNotify"),
		*FishingSessionId.ToString(), *GetNameSafe(GetOwner()), *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), bReturnBait);
	if (bReturnBait) Inventory->BroadcastInventoryChange();
	PublishSnapshot();
	return Result;
}

void UCatEquipmentComponent::WatchPendingBaitReturns()
{
	if (PendingBaitReturnHandle.IsValid()) return;
	if (UCatInventoryComponent* Inventory = ResolveOwnerInventoryComponent())
	{
		PendingBaitReturnInventory = Inventory;
		PendingBaitReturnHandle = Inventory->OnInventoryObservedChanged.AddUObject(this, &ThisClass::RetryPendingBaitReturns);
	}
}

void UCatEquipmentComponent::RetryPendingBaitReturns()
{
	if (bRetryingBaitReturns || !GetOwner() || !GetOwner()->HasAuthority()) return;
	TGuardValue<bool> Guard(bRetryingBaitReturns, true);
	TArray<FGuid> Pending;
	for (const auto& Pair : FishingUseRecords)
		if (Pair.Value.bReturnPending && !Pair.Value.bReleased) Pending.Add(Pair.Key);
	for (const FGuid SessionId : Pending) ReleaseFishingUse(SessionId);
}

bool UCatEquipmentComponent::HasActiveFishingUse() const
{
	for (const TPair<FGuid, FCatFishingUseRecord>& Pair : FishingUseRecords)
	{
		if (Pair.Key.IsValid() && !Pair.Value.bReleased && !Pair.Value.bReturnPending) return true;
	}
	return false;
}

bool UCatEquipmentComponent::IsFishingUseActive(const FGuid FishingSessionId) const
{
	const FCatFishingUseRecord* Record = FindFishingUseRecord(FishingSessionId);
	return FishingSessionId.IsValid() && Record && !Record->bReleased && !Record->bReturnPending;
}

// Snapshot 复制回调流程：客户端只刷新只读表现；不会自动装备或补充普通饵数量。
void UCatEquipmentComponent::OnRep_Snapshot()
{
	OnSnapshotChanged.Broadcast();
}

// 库存容量读取流程：默认容量只来自 InventorySettings，Equipment 不承担容量配置。
int32 UCatEquipmentComponent::GetConfiguredInventorySlotCapacity() const
{
	return ResolvePlayerInventorySlotCapacity();
}

// 正式随身库存解析流程：只从当前 Owner 上读取 InventoryComponent；Equipment 不创建或缓存库存组件，避免出现第二份背包归属。
UCatInventoryComponent* UCatEquipmentComponent::ResolveOwnerInventoryComponent() const
{
	AActor* Owner = GetOwner();
	return Owner != nullptr ? Owner->FindComponentByClass<UCatInventoryComponent>() : nullptr;
}

// 单格堆叠读取流程：定义资产统一回答有效上限；Equipment 只读取定义给出的结果。
int32 UCatEquipmentComponent::GetInventoryStackLimit(const UCatEquipmentDefinition& Definition) const
{
	return Definition.GetMaxStackCount();
}

// 公开选择刷新流程：
// 1. 公开入口只负责根据 Owner 正式库存校正钓具选择，不声明新增物品。
// 2. 实际选择一致性校正、版本推进和发布交给带参数入口，保证营地转移和普通刷新共用同一条规则。
bool UCatEquipmentComponent::RefreshLoadoutFromInventoryComponentFromAuthority()
{
	return RefreshLoadoutFromInventoryComponentFromAuthority(nullptr, NAME_None);
}

// 正式库存到钓具读模型刷新流程：
// 1. 只在 authority 上读取 Owner 的 InventoryComponent；客户端复制读模型不能反向生成服务器快照。
// 2. 先记录刷新前的钓具选择，再校正当前选择：有效选择保持不动，缺失或已离开随身库存的实例会换到可用同类或清空。
// 3. 选择有变化时才推进 Equipment Revision 并发布读模型；正式背包事实仍只来自 InventoryComponent。
bool UCatEquipmentComponent::RefreshLoadoutFromInventoryComponentFromAuthority(
	const UCatEquipmentDefinition* GrantedDefinition, const FName GrantedDefinitionId)
{
	AActor* Owner = GetOwner();
	if (Owner == nullptr || !Owner->HasAuthority())
	{
		return false;
	}
	if (ResolveOwnerInventoryComponent() == nullptr)
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

	ReconcileLoadoutSelectionsWithInventory(GrantedDefinition, GrantedDefinitionId);

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
	if (!bSelectionChanged)
	{
		return true;
	}

	++Snapshot.Revision;
	PublishSnapshot();
	return true;
}

// 装备条目读取流程：先确认实例类型、身份、定义与数量有效，再返回引用同一 UObject 的条目。
// 失败清空输出；这里不复制耐久或鱼载荷，也不修改正式库存。
bool UCatEquipmentComponent::TryReadEquipmentInventoryEntry(
	const FCatInventoryEntry& Entry, FCatInventoryEntry& OutSlot) const
{
	const UCatEquipmentInventoryItemInstance* Instance = Cast<UCatEquipmentInventoryItemInstance>(Entry.Instance);
	if (Instance == nullptr || !Instance->GetItemInstanceId().IsValid()
		|| Instance->GetItemDefinition() == nullptr || Entry.StackCount <= 0)
	{
		OutSlot = FCatInventoryEntry();
		return false;
	}
	OutSlot = Entry;
	return true;
}

// 钓鱼选择库存候选解析流程：
// 1. 先拒绝空定义，并清空输出槽位，避免调用方误用上一次的局部缓存。
// 2. Owner 库存组件是唯一库存事实；缺组件或容量尚未初始化时直接拒绝选择解析。
// 3. 正式选择路径用实例 ID 精确定位；库存刷新和补选路径才按定义扫描正式槽位。
// 4. 补选扫描沿用鱼竿选择的“可用优先、否则第一命中”口径，避免自动选中断竿。
// 5. 正式 entry 必须持有有效装备实例且定义一致，才允许 Equipment 更新钓鱼选择和鱼竿运行态。
bool UCatEquipmentComponent::TryResolveSelectionInventorySlot(const FName DefinitionId,
	const FGuid ItemInstanceId, FCatInventoryEntry& OutSlot) const
{
	OutSlot = FCatInventoryEntry();
	if (DefinitionId.IsNone())
	{
		return false;
	}

	const UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent();
	if (OwnerInventory == nullptr || OwnerInventory->GetInventorySlotCount() < GetConfiguredInventorySlotCapacity())
	{
		return false;
	}

	if (ItemInstanceId.IsValid())
	{
		const int32 SlotIndex = OwnerInventory->FindInventorySlotIndexFromInstanceId(ItemInstanceId);
		const FCatInventoryEntry* FormalEntry = OwnerInventory->GetInventoryEntryAtSlot(SlotIndex);
		if (FormalEntry == nullptr || FormalEntry->StackCount <= 0
			|| !TryReadEquipmentInventoryEntry(*FormalEntry, OutSlot)
			|| OutSlot.Instance->GetItemDefinitionId() != DefinitionId)
		{
			OutSlot = FCatInventoryEntry();
			return false;
		}
		return true;
	}

	FCatInventoryEntry FirstMatchedSlot;
	bool bFoundFirstMatchedSlot = false;
	const TArray<FCatInventoryEntry> FormalEntries = OwnerInventory->GetInventoryEntries();
	for (const FCatInventoryEntry& FormalEntry : FormalEntries)
	{
		FCatInventoryEntry CandidateSlot;
		if (FormalEntry.StackCount <= 0
			|| !TryReadEquipmentInventoryEntry(FormalEntry, CandidateSlot)
			|| CandidateSlot.Instance->GetItemDefinitionId() != DefinitionId)
		{
			continue;
		}
		if (!bFoundFirstMatchedSlot)
		{
			FirstMatchedSlot = CandidateSlot;
			bFoundFirstMatchedSlot = true;
		}
		if (!CastChecked<UCatEquipmentInventoryItemInstance>(CandidateSlot.Instance)->IsRodBroken() && FMath::IsFinite(CastChecked<UCatEquipmentInventoryItemInstance>(CandidateSlot.Instance)->GetRodDurability())
			&& CastChecked<UCatEquipmentInventoryItemInstance>(CandidateSlot.Instance)->GetRodDurability() > 0.0)
		{
			OutSlot = CandidateSlot;
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

// 选中鱼竿正式实例解析流程：
// 1. 先按 Snapshot 保存的实例 ID 回正式库存查槽；实例身份无效或槽位为空都表示当前选择已经失去库存事实。
// 2. 直接读取正式 entry 的装备实例，确认定义、实例和单件数量都对应当前选择。
	// 3. 最后返回可写装备实例；调用方只有拿到它时才能提交耐久变化，不能退回只改 Equipment 读模型。
UCatEquipmentInventoryItemInstance* UCatEquipmentComponent::ResolveSelectedFormalRodInstanceFromInventory(
	UCatInventoryComponent& OwnerInventory, const UCatEquipmentDefinition& RodDefinition,
	FCatInventoryEntry& OutSlot) const
{
	OutSlot = FCatInventoryEntry();
	if (!Snapshot.RodItemInstanceId.IsValid()
		|| Snapshot.RodDefinitionId != RodDefinition.EquipmentDefinitionId
		|| !RodDefinition.CanServeFishingRod())
	{
		return nullptr;
	}

	const int32 SlotIndex = OwnerInventory.FindInventorySlotIndexFromInstanceId(Snapshot.RodItemInstanceId);
	const FCatInventoryEntry* FormalEntry = OwnerInventory.GetInventoryEntryAtSlot(SlotIndex);
	UCatEquipmentInventoryItemInstance* FormalInstance = FormalEntry != nullptr
		? Cast<UCatEquipmentInventoryItemInstance>(FormalEntry->Instance) : nullptr;
	if (FormalInstance == nullptr
		|| !TryReadEquipmentInventoryEntry(*FormalEntry, OutSlot))
	{
		OutSlot = FCatInventoryEntry();
		return nullptr;
	}

	if (OutSlot.Instance->GetItemInstanceId() != Snapshot.RodItemInstanceId
		|| OutSlot.Instance->GetItemDefinitionId() != Snapshot.RodDefinitionId
		|| OutSlot.StackCount != 1)
	{
		OutSlot = FCatInventoryEntry();
		return nullptr;
	}

	return FormalInstance;
}

UCatEquipmentComponent::FCatFishingUseRecord* UCatEquipmentComponent::FindFishingUseRecord(const FGuid FishingSessionId)
{
	// Fishing 记录可写查找流程：只按 SessionId 命中当前 Character 生命周期内的短记录；找不到时返回空，调用方决定是否创建或拒绝。
	return FishingUseRecords.Find(FishingSessionId);
}

const UCatEquipmentComponent::FCatFishingUseRecord* UCatEquipmentComponent::FindFishingUseRecord(const FGuid FishingSessionId) const
{
	// Fishing 记录只读查找流程：查询路径只能读取 Begin 冻结的库存身份和收口状态，不能借 const 查询补建会话记录。
	return FishingUseRecords.Find(FishingSessionId);
}

bool UCatEquipmentComponent::TryBuildHeldInventoryUseSlot(const FGuid ItemInstanceId,
	FCatInventoryEntry& OutSlot) const
{
	// 正式部署转换流程：只从 Owner 库存活动区读取同一实例，保留同一实例给配置和 Fishing 预检使用；查询失败必须清空输出。
	OutSlot = FCatInventoryEntry();
	UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent();
	const FCatInventoryEntry* HeldEntry =
		OwnerInventory != nullptr ? OwnerInventory->FindHeldInventoryEntryFromAuthority(ItemInstanceId) : nullptr;
	if (HeldEntry == nullptr || HeldEntry->StackCount <= 0
		|| !TryReadEquipmentInventoryEntry(*HeldEntry, OutSlot))
	{
		OutSlot = FCatInventoryEntry();
		return false;
	}

	if (OutSlot.Instance->GetItemInstanceId() != ItemInstanceId || OutSlot.StackCount != 1)
	{
		OutSlot = FCatInventoryEntry();
		return false;
	}

	return true;
}

UCatEquipmentInventoryItemInstance* UCatEquipmentComponent::ResolveFishingRodFormalInstanceFromInventory(
	const FCatFishingUseRecord& Record, FCatInventoryEntry& OutSlot) const
{
	// Fishing 鱼竿正式实例解析流程：
	// 1. 先用 Begin 冻结的实例 ID 查可见库存格；找不到时再查库存活动区，覆盖正在部署的世界鱼竿。
	// 2. 命中 entry 后必须核对装备实例的定义、实例和单件数量，防止同定义另一根竿承接磨损。
	// 3. 只有正式装备实例本体有效时才返回可写指针，调用方随后把耐久写回库存实例。
	OutSlot = FCatInventoryEntry();
	if (!Record.RodItemInstanceId.IsValid() || Record.RodDefinitionId.IsNone())
	{
		return nullptr;
	}

	UCatInventoryComponent* RodInventory = Record.RodInventory.Get();
	if (RodInventory == nullptr)
	{
		return nullptr;
	}

	const int32 SlotIndex = RodInventory->FindInventorySlotIndexFromInstanceId(Record.RodItemInstanceId);
	const FCatInventoryEntry* FormalEntry = RodInventory->GetInventoryEntryAtSlot(SlotIndex);
	if (FormalEntry == nullptr)
	{
		FormalEntry = RodInventory->FindHeldInventoryEntryFromAuthority(Record.RodItemInstanceId);
	}

	UCatEquipmentInventoryItemInstance* FormalInstance = FormalEntry != nullptr
		? Cast<UCatEquipmentInventoryItemInstance>(FormalEntry->Instance) : nullptr;
	if (FormalInstance == nullptr
		|| !TryReadEquipmentInventoryEntry(*FormalEntry, OutSlot))
	{
		OutSlot = FCatInventoryEntry();
		return nullptr;
	}

	if (OutSlot.Instance->GetItemInstanceId() != Record.RodItemInstanceId
		|| OutSlot.Instance->GetItemDefinitionId() != Record.RodDefinitionId
		|| OutSlot.StackCount != 1)
	{
		OutSlot = FCatInventoryEntry();
		return nullptr;
	}

	return FormalInstance;
}

bool UCatEquipmentComponent::HasActiveInventoryItemUse() const
{
	// 活动物品使用 gate 流程：以 Inventory held-entry 为部署占用事实；缺少正式库存时返回未占用。
	if (const UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent())
	{
		return OwnerInventory->HasActiveHeldInventoryEntriesFromAuthority();
	}
	return false;
}

bool UCatEquipmentComponent::TryFindInventorySlotByInstanceId(const FGuid ItemInstanceId,
	FCatInventoryEntry& OutSlot) const
{
	// 实例格查找流程：先回到正式可见库存按实例 ID 找槽，再读取同一装备实例；没有命中时清空输出。
	OutSlot = FCatInventoryEntry();
	if (!ItemInstanceId.IsValid())
	{
		return false;
	}
	const UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent();
	if (OwnerInventory == nullptr)
	{
		return false;
	}
	const int32 SlotIndex = OwnerInventory->FindInventorySlotIndexFromInstanceId(ItemInstanceId);
	const FCatInventoryEntry* Entry = OwnerInventory->GetInventoryEntryAtSlot(SlotIndex);
	if (Entry == nullptr || Entry->StackCount <= 0
		|| !TryReadEquipmentInventoryEntry(*Entry, OutSlot)
		|| OutSlot.Instance->GetItemInstanceId() != ItemInstanceId)
	{
		OutSlot = FCatInventoryEntry();
		return false;
	}
	return true;
}

// 钓具选择一致性校正流程：
// 1. 每次正式库存变化后运行，先验证当前选择的实例是否仍在随身正式库存或正式 held entry 中。
// 2. 有效选择保持不动；缺失选择优先落到指定新增定义或同类可用库存项，找不到可用实例就清空。
// 3. 鱼竿额外区分“选择丢失”和“库存中的坏竿”：丢失必须换或清空，坏竿只有找到可用替代时才自动换。
// 4. 这个流程只改 Equipment 选择读模型，不移动库存物品，也不创建独立装备栏。
void UCatEquipmentComponent::ReconcileLoadoutSelectionsWithInventory(
	const UCatEquipmentDefinition* PreferredDefinition, const FName PreferredDefinitionId)
{
	const UCatInventorySettings* InventorySettings = GetDefault<UCatInventorySettings>();
	const UCatInventoryComponent* OwnerInventory = ResolveOwnerInventoryComponent();
	TArray<FCatInventoryEntry> VisibleSlots;
	if (OwnerInventory != nullptr)
	{
		for (const FCatInventoryEntry& Entry : OwnerInventory->GetInventoryEntries())
		{
			FCatInventoryEntry Slot;
			if (Entry.StackCount > 0
				&& TryReadEquipmentInventoryEntry(Entry, Slot)
				&& (Slot.Instance != nullptr && Slot.StackCount > 0))
			{
				VisibleSlots.Add(Slot);
			}
		}
	}
	const auto IsSlotUsableRod = [](const FCatInventoryEntry& Slot)
	{
		return !CastChecked<UCatEquipmentInventoryItemInstance>(Slot.Instance)->IsRodBroken() && FMath::IsFinite(CastChecked<UCatEquipmentInventoryItemInstance>(Slot.Instance)->GetRodDurability()) && CastChecked<UCatEquipmentInventoryItemInstance>(Slot.Instance)->GetRodDurability() > 0.0;
	};

	const bool bPreferredRod = PreferredDefinition != nullptr
		&& !PreferredDefinitionId.IsNone()
		&& PreferredDefinition->CanServeFishingRod();
	FCatInventoryEntry PreferredRodSlot;
	const bool bHasPreferredRodSlot = bPreferredRod
		&& TryResolveSelectionInventorySlot(PreferredDefinitionId, FGuid(), PreferredRodSlot);
	FCatInventoryEntry FirstUsableRodSlot;
	bool bHasFirstUsableRodSlot = false;
	for (const FCatInventoryEntry& Slot : VisibleSlots)
	{
		const UCatEquipmentDefinition* SlotDefinition = InventorySettings != nullptr
			? InventorySettings->FindRuntimeDefinition<UCatEquipmentDefinition>(Slot.Instance->GetItemDefinitionId()) : nullptr;
		if (!(Slot.Instance != nullptr && Slot.StackCount > 0)
			|| SlotDefinition == nullptr || !SlotDefinition->CanServeFishingRod())
		{
			continue;
		}
		if (!bHasFirstUsableRodSlot && IsSlotUsableRod(Slot))
		{
			FirstUsableRodSlot = Slot;
			bHasFirstUsableRodSlot = true;
		}
	}

	FCatInventoryEntry SelectedStoredRod;
	const bool bSelectedStoredRodMatches =
		TryFindInventorySlotByInstanceId(Snapshot.RodItemInstanceId, SelectedStoredRod)
		&& SelectedStoredRod.Instance->GetItemDefinitionId() == Snapshot.RodDefinitionId;
	FCatInventoryEntry ActiveSelectedRodSlot;
	const bool bSelectedRodIsInUse =
		TryBuildHeldInventoryUseSlot(Snapshot.RodItemInstanceId, ActiveSelectedRodSlot)
		&& ActiveSelectedRodSlot.Instance->GetItemDefinitionId() == Snapshot.RodDefinitionId;
	const bool bSelectedRodMissing = Snapshot.RodDefinitionId.IsNone()
		|| (!bSelectedStoredRodMatches && !bSelectedRodIsInUse);
	const bool bStoredSelectedRodBroken = bSelectedStoredRodMatches
		&& !IsSlotUsableRod(SelectedStoredRod);
	const bool bSelectedRodBrokenOrInvalid = !bSelectedRodIsInUse
		&& (Snapshot.bRodBroken || !FMath::IsFinite(Snapshot.RodDurability)
			|| Snapshot.RodDurability <= 0.0 || bStoredSelectedRodBroken);
	FCatInventoryEntry ReplacementRodSlot;
	bool bHasReplacementRodSlot = false;
	if (bHasPreferredRodSlot && IsSlotUsableRod(PreferredRodSlot))
	{
		ReplacementRodSlot = PreferredRodSlot;
		bHasReplacementRodSlot = true;
	}
	else if (bHasFirstUsableRodSlot)
	{
		ReplacementRodSlot = FirstUsableRodSlot;
		bHasReplacementRodSlot = true;
	}
	const bool bShouldReplaceRod = (bSelectedRodBrokenOrInvalid || bSelectedRodMissing)
		&& bHasReplacementRodSlot;
	if (bShouldReplaceRod)
	{
		const FName PreviousDefinitionId = Snapshot.RodDefinitionId;
		const FGuid PreviousItemInstanceId = Snapshot.RodItemInstanceId;
		const double PreviousRodDurability = Snapshot.RodDurability;
		const bool bPreviousRodBroken = Snapshot.bRodBroken;
		Snapshot.RodDefinitionId = ReplacementRodSlot.Instance->GetItemDefinitionId();
		Snapshot.RodItemInstanceId = ReplacementRodSlot.Instance->GetItemInstanceId();
		Snapshot.RodDurability = CastChecked<UCatEquipmentInventoryItemInstance>(ReplacementRodSlot.Instance)->GetRodDurability();
		Snapshot.bRodBroken = CastChecked<UCatEquipmentInventoryItemInstance>(ReplacementRodSlot.Instance)->IsRodBroken();
		UE_LOG(LogCatEquipment, Log,
			TEXT("Event=equipment_rod_selection_reconciled Reason=%s PreviousDefinition=%s PreviousItem=%s PreviousDurability=%.3f PreviousBroken=%s SelectedDefinition=%s SelectedItem=%s SelectedDurability=%.3f SelectedBroken=%s ActiveUse=%s Revision=%lld Owner=%s World=%s NetMode=%d"),
			bSelectedRodMissing ? TEXT("MissingInstance") : TEXT("BrokenOrInvalid"),
			*PreviousDefinitionId.ToString(),
			*PreviousItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), PreviousRodDurability,
			bPreviousRodBroken ? TEXT("true") : TEXT("false"), *Snapshot.RodDefinitionId.ToString(),
			*Snapshot.RodItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), Snapshot.RodDurability,
			Snapshot.bRodBroken ? TEXT("true") : TEXT("false"),
			bSelectedRodIsInUse ? TEXT("true") : TEXT("false"), Snapshot.Revision, *GetNameSafe(GetOwner()),
			*GetNameSafe(GetWorld()), static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone));
	}
	else if (bSelectedRodMissing)
	{
		const FName PreviousDefinitionId = Snapshot.RodDefinitionId;
		const FGuid PreviousItemInstanceId = Snapshot.RodItemInstanceId;
		Snapshot.RodDefinitionId = NAME_None;
		Snapshot.RodItemInstanceId.Invalidate();
		Snapshot.RodDurability = 0.0;
		Snapshot.bRodBroken = false;
		UE_LOG(LogCatEquipment, Log,
			TEXT("Event=equipment_rod_selection_reconciled Reason=ClearedMissingInstance PreviousDefinition=%s PreviousItem=%s Revision=%lld Owner=%s World=%s NetMode=%d"),
			*PreviousDefinitionId.ToString(),
			*PreviousItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), Snapshot.Revision,
			*GetNameSafe(GetOwner()), *GetNameSafe(GetWorld()),
			static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone));
	}
	else if (bSelectedStoredRodMatches)
	{
		Snapshot.RodDurability = CastChecked<UCatEquipmentInventoryItemInstance>(SelectedStoredRod.Instance)->GetRodDurability();
		Snapshot.bRodBroken = CastChecked<UCatEquipmentInventoryItemInstance>(SelectedStoredRod.Instance)->IsRodBroken();
	}

	const auto FindFirstSlotForSlotRole =
		[InventorySettings, this, PreferredDefinition,
			PreferredDefinitionId, &VisibleSlots](const FName CurrentDefinitionId,
				const FName SlotRole, FCatInventoryEntry& OutSlot) -> bool
	{
		OutSlot = FCatInventoryEntry();
		if (PreferredDefinition != nullptr && !PreferredDefinitionId.IsNone()
			&& PreferredDefinition->CanServeFishingLoadoutSlot(SlotRole))
		{
			if (TryResolveSelectionInventorySlot(PreferredDefinitionId, FGuid(), OutSlot))
			{
				return true;
			}
		}
		if (TryResolveSelectionInventorySlot(CurrentDefinitionId, FGuid(), OutSlot))
		{
			return true;
		}
		for (const FCatInventoryEntry& Slot : VisibleSlots)
		{
			const UCatEquipmentDefinition* SlotDefinition = InventorySettings != nullptr
				? InventorySettings->FindRuntimeDefinition<UCatEquipmentDefinition>(Slot.Instance->GetItemDefinitionId()) : nullptr;
			if ((Slot.Instance != nullptr && Slot.StackCount > 0)
				&& SlotDefinition != nullptr
				&& SlotDefinition->CanServeFishingLoadoutSlot(SlotRole))
			{
				OutSlot = Slot;
				return true;
			}
		}
		return false;
	};
	const auto ReconcileNonRodSelection =
		[this, FindFirstSlotForSlotRole](const TCHAR* LoadoutSlotName,
			const FName SlotRole,
			FName& InOutDefinitionId, FGuid& InOutItemInstanceId)
	{
		FCatInventoryEntry SelectedSlot;
		const bool bSelectedItemValid = TryFindInventorySlotByInstanceId(InOutItemInstanceId, SelectedSlot)
			&& SelectedSlot.Instance->GetItemDefinitionId() == InOutDefinitionId && SelectedSlot.StackCount > 0;
		if (!InOutDefinitionId.IsNone() && bSelectedItemValid)
		{
			return;
		}

		const FName PreviousDefinitionId = InOutDefinitionId;
		const FGuid PreviousItemInstanceId = InOutItemInstanceId;
		FCatInventoryEntry ReplacementSlot;
		if (!FindFirstSlotForSlotRole(InOutDefinitionId, SlotRole, ReplacementSlot))
		{
			InOutDefinitionId = NAME_None;
			InOutItemInstanceId.Invalidate();
			UE_LOG(LogCatEquipment, Log,
				TEXT("Event=equipment_item_selection_reconciled LoadoutSlot=%s Reason=ClearedMissingInstance PreviousDefinition=%s PreviousItem=%s Revision=%lld Owner=%s World=%s NetMode=%d"),
				LoadoutSlotName, *PreviousDefinitionId.ToString(),
				*PreviousItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), Snapshot.Revision,
				*GetNameSafe(GetOwner()), *GetNameSafe(GetWorld()),
				static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone));
			return;
		}

		InOutDefinitionId = ReplacementSlot.Instance->GetItemDefinitionId();
		InOutItemInstanceId = ReplacementSlot.Instance->GetItemInstanceId();
		UE_LOG(LogCatEquipment, Log,
			TEXT("Event=equipment_item_selection_reconciled LoadoutSlot=%s Reason=%s PreviousDefinition=%s PreviousItem=%s SelectedDefinition=%s SelectedItem=%s Revision=%lld Owner=%s World=%s NetMode=%d"),
			LoadoutSlotName, PreviousDefinitionId.IsNone() ? TEXT("Unavailable") : TEXT("MissingInstance"),
			*PreviousDefinitionId.ToString(),
			*PreviousItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), *InOutDefinitionId.ToString(),
			*InOutItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), Snapshot.Revision,
			*GetNameSafe(GetOwner()), *GetNameSafe(GetWorld()),
			static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone));
	};
	ReconcileNonRodSelection(TEXT("Bait"), UCatEquipmentDefinition::FishingBaitLoadoutSlotId(),
		Snapshot.BaitDefinitionId, Snapshot.BaitItemInstanceId);
	ReconcileNonRodSelection(TEXT("Float"), UCatEquipmentDefinition::FishingFloatLoadoutSlotId(),
		Snapshot.FloatDefinitionId, Snapshot.FloatItemInstanceId);
	ReconcileNonRodSelection(TEXT("ScoopNet"), UCatEquipmentDefinition::ScoopNetLoadoutSlotId(),
		Snapshot.ScoopNetDefinitionId, Snapshot.ScoopNetItemInstanceId);
}

FCatFishingUseFreezeResult UCatEquipmentComponent::MakeFishingUseFreezeResult(const FGuid FishingSessionId,
	const ECatDomainCommandError Error, const bool bBaitFrozen, const FCatFishingUseRecord* Record) const
{
	// 装备使用结果组装流程：先写命令终态和当前 Equipment Revision，再从绑定鱼竿实例读取耐久状态；记录缺失时只返回默认耐久，不制造新会话状态。
	FCatFishingUseFreezeResult Result;
	Result.SessionId = FishingSessionId;
	Result.Error = Error;
	Result.EquipmentRevision = Snapshot.Revision;
	Result.bBaitFrozen = bBaitFrozen;
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

// Snapshot 发布流程：只要求 Owner 立即复制并广播读模型变化；正式背包事实必须由 InventoryComponent 或显式恢复导入入口提交。
void UCatEquipmentComponent::PublishSnapshot()
{
	if (AActor* Owner = GetOwner(); Owner && Owner->HasAuthority())
	{
		Owner->ForceNetUpdate();
	}
	OnSnapshotChanged.Broadcast();
}

bool UCatEquipmentComponent::TryGetInventoryRodForDeployment(FCatInventoryEntry& OutRod) const
{
    OutRod = {};
    const UCatInventoryComponent* Inventory = ResolveOwnerInventoryComponent();
    if (!Inventory) return false;
    const auto IsDeployable = [](const FCatInventoryEntry& Entry)
    {
        const auto* Instance = Cast<UCatEquipmentInventoryItemInstance>(Entry.Instance);
        const auto* Definition = Instance ? Cast<UCatEquipmentDefinition>(Instance->GetItemDefinition()) : nullptr;
        return Entry.StackCount == 1 && Definition && Definition->CanServeFishingRod()
            && !Instance->IsRodBroken() && FMath::IsFinite(Instance->GetRodDurability()) && Instance->GetRodDurability() > 0.0;
    };
    for (const FCatInventoryEntry& Entry : Inventory->GetInventoryEntries())
        if (IsDeployable(Entry) && Entry.Instance->GetItemInstanceId() == Snapshot.RodItemInstanceId) { OutRod = Entry; return true; }
    for (const FCatInventoryEntry& Entry : Inventory->GetInventoryEntries())
        if (IsDeployable(Entry)) { OutRod = Entry; return true; }
    return false;
}

bool UCatEquipmentComponent::MoveFishingResourcesToCustodian(UCatEquipmentComponent* Target,
    const TArray<FGuid>& SessionIds, const TArray<FGuid>& RodItemInstanceIds)
{
    if (!Target || Target == this || !GetOwner() || !GetOwner()->HasAuthority()
        || !Target->GetOwner() || !Target->GetOwner()->HasAuthority() || Target->GetWorld() != GetWorld()) return false;
    for (const FGuid SessionId : SessionIds)
    {
        const FCatFishingUseRecord* Record = FindFishingUseRecord(SessionId);
        if (!Record || Record->bReleased || Target->FishingUseRecords.Contains(SessionId)) return false;
    }
    UCatInventoryComponent* SourceInventory = ResolveOwnerInventoryComponent();
    UCatInventoryComponent* TargetInventory = Target->ResolveOwnerInventoryComponent();
    if (!SourceInventory || !TargetInventory) return false;
    TargetInventory->SetInventorySlotCountFromAuthority(FMath::Max(TargetInventory->GetInventorySlotCount(), SessionIds.Num()));
    if (!SourceInventory->MoveHeldInventoryEntriesToCustodianFromAuthority(TargetInventory, RodItemInstanceIds)) return false;
    for (const FGuid SessionId : SessionIds)
    {
        Target->FishingUseRecords.Add(SessionId, MoveTemp(FishingUseRecords.FindChecked(SessionId)));
        FishingUseRecords.Remove(SessionId);
    }
    // Borrowers retain their own bait records; only the transferred rod's authoritative inventory changes.
    for (TObjectIterator<UCatEquipmentComponent> It; It; ++It)
    {
        if (It->GetWorld() != GetWorld()) continue;
        for (auto& Pair : It->FishingUseRecords)
            if (Pair.Value.RodInventory.Get(true) == SourceInventory && RodItemInstanceIds.Contains(Pair.Value.RodItemInstanceId))
                Pair.Value.RodInventory = TargetInventory;
    }
    ReconcileLoadoutSelectionsWithInventory(nullptr, NAME_None);
    Target->ReconcileLoadoutSelectionsWithInventory(nullptr, NAME_None);
    ++Snapshot.Revision;
    ++Target->Snapshot.Revision;
    return true;
}

bool UCatEquipmentComponent::IsFishingRodInUse(const FGuid ItemInstanceId) const
{
    const auto* Inventory = ResolveOwnerInventoryComponent();
    if (!Inventory || !ItemInstanceId.IsValid()) return false;
    for (TObjectIterator<UCatEquipmentComponent> It; It; ++It)
    {
        if (It->GetWorld() != GetWorld()) continue;
        for (const auto& Pair : It->FishingUseRecords)
            if (!Pair.Value.bReleased && !Pair.Value.bReturnPending && Pair.Value.RodInventory.Get(true) == Inventory && Pair.Value.RodItemInstanceId == ItemInstanceId) return true;
    }
    return false;
}

void UCatEquipmentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (auto* Inventory = PendingBaitReturnInventory.Get())
        Inventory->OnInventoryObservedChanged.Remove(PendingBaitReturnHandle);
    PendingBaitReturnHandle.Reset();
    if (GetOwner() && GetOwner()->HasAuthority() && GetWorld())
        if (auto* Fishing = GetWorld()->GetSubsystem<UCatFishingService>())
            Fishing->PreserveFishingResourcesForEquipmentShutdown(this);
    Super::EndPlay(EndPlayReason);
}
