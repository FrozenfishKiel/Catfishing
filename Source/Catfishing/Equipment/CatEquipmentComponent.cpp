#include "Equipment/CatEquipmentComponent.h"
#include "EngineUtils.h"
#include "UObject/UObjectIterator.h"

#include "Equipment/Fragments/CatEquipmentFragment_Bait.h"

#include "Equipment/CatEquipmentInventoryItemInstance.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "Equipment/CatEquipmentItemDefinition.h"
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
		const UCatEquipmentItemDefinition* Definition = Instance ? GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentItemDefinition>(Instance->GetItemId()) : nullptr;
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
	const auto ResolveSelectedInstance = [this, InventorySettings, &OutFailure](const int32  ItemId,
		const FGuid InstanceId, const FName ExpectedSlotId, const bool bAllowHeldEntry,
		FCatInventoryEntry& OutSlot)
	{
		if ((ItemId == 0))
		{
			const bool bEmpty = !InstanceId.IsValid();
			if (!bEmpty)
			{
				OutFailure = FText::FromString(TEXT("空钓具选择携带了实例身份。"));
			}
			OutSlot = FCatInventoryEntry();
			return bEmpty;
		}
		const UCatEquipmentItemDefinition* Definition =
			InventorySettings->FindRuntimeDefinition<UCatEquipmentItemDefinition>(ItemId);
		if (!InstanceId.IsValid() || Definition == nullptr
			|| !Definition->CanServeFishingLoadoutSlot(ExpectedSlotId))
		{
			OutFailure = FText::FromString(TEXT("钓具选择引用了无效定义或实例。"));
			OutSlot = FCatInventoryEntry();
			return false;
		}
		if (bAllowHeldEntry && TryBuildHeldInventoryUseSlot(InstanceId, OutSlot)
			&& OutSlot.Instance->GetItemId() == ItemId)
		{
			return true;
		}
		if (TryResolveSelectionInventorySlot(ItemId, InstanceId, OutSlot))
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
	if (!ResolveSelectedInstance(RestoredSnapshot.RodItemId, RestoredSnapshot.RodItemInstanceId,
			UCatEquipmentItemDefinition::FishingRodLoadoutSlotId(), true, SelectedRod)
		|| !ResolveSelectedInstance(RestoredSnapshot.BaitItemId, RestoredSnapshot.BaitItemInstanceId,
			UCatEquipmentItemDefinition::FishingBaitLoadoutSlotId(), false, SelectedBait)
		|| !ResolveSelectedInstance(RestoredSnapshot.FloatItemId, RestoredSnapshot.FloatItemInstanceId,
			UCatEquipmentItemDefinition::FishingFloatLoadoutSlotId(), false, SelectedFloat)
		|| !ResolveSelectedInstance(RestoredSnapshot.ScoopNetItemId, RestoredSnapshot.ScoopNetItemInstanceId,
			UCatEquipmentItemDefinition::ScoopNetLoadoutSlotId(), false, SelectedScoopNet))
	{
		return false;
	}
	if (!(RestoredSnapshot.RodItemId == 0)
		&& (RestoredSnapshot.RodDurability != CastChecked<UCatEquipmentInventoryItemInstance>(SelectedRod.Instance)->GetRodDurability()
			|| RestoredSnapshot.bRodBroken != CastChecked<UCatEquipmentInventoryItemInstance>(SelectedRod.Instance)->IsRodBroken()))
	{
		OutFailure = FText::FromString(TEXT("鱼竿选择状态与库存实例不一致。"));
		return false;
	}
	if ((RestoredSnapshot.RodItemId == 0)
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
// 2. 每次提交都必须通过正式库存目录、authority、Revision、定义运行能力和实例身份；拥有实例是唯一的玩法装备准入事实。
// 3. 正式角色从 InventoryComponent 精确解析钓具实例；部署中的当前鱼竿只读库存 held entry。
// 4. 同一套定义和实例选择直接返回 AlreadyResolved；不同选择会切换当前钓鱼选择，并从鱼竿实例读取耐久。
// 5. 成功时只写钓鱼选择、实例身份和当前鱼竿运行态，并发布给当前读模型消费者。
FCatDomainCommandResult UCatEquipmentComponent::ConfigureLoadoutFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const int32  RodItemId, const int32  BaitItemId,
	const int32  FloatItemId, const int32  ScoopNetItemId, const FName RodSkinDefinitionId,
	const FGuid RodItemInstanceId, const FGuid BaitItemInstanceId, const FGuid FloatItemInstanceId,
	const FGuid ScoopNetItemInstanceId)
{
	// 钓具配置提交流程：
	// 1. RequestId 命中终态缓存时直接返回首次结果，避免重放请求重新选择或推进 Revision。
	// 2. 再校验服务器权威、定义运行能力、版本和库存实例；任一失败只写错误码，不改变当前选择和库存状态。
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
	const UCatInventorySettings* InventorySettings = GetDefault<UCatInventorySettings>();
	UCatEquipmentItemDefinition* Rod = InventorySettings
		? InventorySettings->FindRuntimeDefinition<UCatEquipmentItemDefinition>(RodItemId) : nullptr;
	UCatEquipmentItemDefinition* Bait = InventorySettings
		? InventorySettings->FindRuntimeDefinition<UCatEquipmentItemDefinition>(BaitItemId) : nullptr;
	UCatEquipmentItemDefinition* Float = InventorySettings
		? InventorySettings->FindRuntimeDefinition<UCatEquipmentItemDefinition>(FloatItemId) : nullptr;
	UCatEquipmentItemDefinition* Scoop = !(ScoopNetItemId == 0) && InventorySettings
		? InventorySettings->FindRuntimeDefinition<UCatEquipmentItemDefinition>(ScoopNetItemId) : nullptr;
	if (!GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid() || !Rod || !Bait || !Float
		|| (!(ScoopNetItemId == 0) && !Scoop))
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else if (Snapshot.Revision != ExpectedRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
	}
	else if (!RodItemInstanceId.IsValid() || !BaitItemInstanceId.IsValid() || !FloatItemInstanceId.IsValid()
		|| ((ScoopNetItemId == 0) && ScoopNetItemInstanceId.IsValid())
		|| (!(ScoopNetItemId == 0) && !ScoopNetItemInstanceId.IsValid()))
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (!Rod->CanServeFishingRod() || !Bait->CanServeFishingBait()
		|| !Float->CanServeFishingFloat() || (Scoop && !Scoop->CanServeScoopNet()))
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else
	{
		FCatInventoryEntry ActiveSelectedRodSlot;
		bool bSelectedRodIsInUse = false;
		if (Snapshot.RodItemInstanceId.IsValid())
		{
			bSelectedRodIsInUse = TryBuildHeldInventoryUseSlot(
				Snapshot.RodItemInstanceId, ActiveSelectedRodSlot)
				&& ActiveSelectedRodSlot.Instance->GetItemId() == Snapshot.RodItemId
				&& ActiveSelectedRodSlot.Instance->GetItemInstanceId() == Snapshot.RodItemInstanceId;
		}
		const bool bRequestsActiveSelectedRod = bSelectedRodIsInUse
			&& RodItemId == ActiveSelectedRodSlot.Instance->GetItemId()
			&& RodItemInstanceId == ActiveSelectedRodSlot.Instance->GetItemInstanceId();
		if (bSelectedRodIsInUse && !bRequestsActiveSelectedRod)
		{
			Result.Error = ECatDomainCommandError::InvalidPhase;
		}
		else
		{
			const auto ResolveSelectedRodSlot =
				[this, ActiveSelectedRodSlot, bRequestsActiveSelectedRod](
					const int32  ItemId, const FGuid ItemInstanceId,
					FCatInventoryEntry& OutSlot) -> bool
				{
					if (bRequestsActiveSelectedRod)
					{
						OutSlot = ActiveSelectedRodSlot;
						return OutSlot.Instance->GetItemId() == ItemId
							&& OutSlot.Instance->GetItemInstanceId() == ItemInstanceId;
					}
					return TryResolveSelectionInventorySlot(ItemId, ItemInstanceId, OutSlot);
				};
			FCatInventoryEntry RodSlot;
			FCatInventoryEntry BaitSlot;
			FCatInventoryEntry FloatSlot;
			FCatInventoryEntry ScoopSlot;
			const bool bHasRodSlot = ResolveSelectedRodSlot(RodItemId, RodItemInstanceId, RodSlot);
			const bool bHasBaitSlot = TryResolveSelectionInventorySlot(BaitItemId, BaitItemInstanceId,
				BaitSlot);
			const bool bHasFloatSlot = TryResolveSelectionInventorySlot(FloatItemId, FloatItemInstanceId,
				FloatSlot);
			const bool bHasScoopSlot = (ScoopNetItemId == 0)
				|| TryResolveSelectionInventorySlot(ScoopNetItemId, ScoopNetItemInstanceId, ScoopSlot);
			if (!bHasRodSlot || !bHasBaitSlot || !bHasFloatSlot || !bHasScoopSlot)
			{
				Result.Error = ECatDomainCommandError::NotFound;
			}
			else
			{
				const FGuid NewScoopItemInstanceId = (ScoopNetItemId == 0)
					? FGuid() : ScoopSlot.Instance->GetItemInstanceId();
				const bool bSameLoadout = Snapshot.RodItemId == RodItemId
					&& Snapshot.RodItemInstanceId == RodSlot.Instance->GetItemInstanceId()
					&& Snapshot.BaitItemId == BaitItemId
					&& Snapshot.BaitItemInstanceId == BaitSlot.Instance->GetItemInstanceId()
					&& Snapshot.FloatItemId == FloatItemId
					&& Snapshot.FloatItemInstanceId == FloatSlot.Instance->GetItemInstanceId()
					&& Snapshot.ScoopNetItemId == ScoopNetItemId
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
				Snapshot.RodItemId = RodItemId;
				Snapshot.RodItemInstanceId = RodSlot.Instance->GetItemInstanceId();
				Snapshot.BaitItemId = BaitItemId;
				Snapshot.BaitItemInstanceId = BaitSlot.Instance->GetItemInstanceId();
				Snapshot.FloatItemId = FloatItemId;
				Snapshot.FloatItemInstanceId = FloatSlot.Instance->GetItemInstanceId();
				Snapshot.ScoopNetItemId = ScoopNetItemId;
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
	const int32  ItemId, const int32 Quantity) const
{
	UCatEquipmentItemDefinition* Definition = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentItemDefinition>(ItemId);
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
	const int64 ExpectedRevision, const int32  ItemId, const int32 Quantity)
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
		ExpectedRevision, *FString::FromInt(ItemId), Quantity);
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
	UCatEquipmentItemDefinition* Definition = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentItemDefinition>(ItemId);
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
			&& !RefreshLoadoutFromInventoryComponentFromAuthority(Definition, ItemId))
		{
			UE_LOG(LogCatEquipment, Warning,
				TEXT("Event=equipment_loadout_refresh_failed Operation=GrantInventoryQuantity Owner=%s Request=%s Definition=%s SnapshotRevision=%lld"),
				*GetNameSafe(GetOwner()), *RequestId.ToString(EGuidFormats::DigitsWithHyphens),
				*FString::FromInt(ItemId), Snapshot.Revision);
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
	const int32  ItemId) const
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid())
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	const FString Key = MakeTerminalKey(TEXT("GrantEquipment"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("Definition=%s"), *FString::FromInt(ItemId));
	if (const FString* CachedPayload = TerminalPayloadByKey.Find(Key))
	{
		return *CachedPayload == PayloadSignature ? ECatDomainCommandError::None
			: ECatDomainCommandError::InvalidPayload;
	}
	UCatEquipmentItemDefinition* Definition = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentItemDefinition>(ItemId);
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
	const int64 ExpectedRevision, const int32  ItemId)
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
	const FString PayloadSignature = FString::Printf(TEXT("Definition=%s"), *FString::FromInt(ItemId));
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

	UCatEquipmentItemDefinition* Definition = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentItemDefinition>(ItemId);
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
			&& !RefreshLoadoutFromInventoryComponentFromAuthority(Definition, ItemId))
		{
			UE_LOG(LogCatEquipment, Warning,
				TEXT("Event=equipment_loadout_refresh_failed Operation=GrantEquipment Owner=%s Request=%s Definition=%s SnapshotRevision=%lld"),
				*GetNameSafe(GetOwner()), *RequestId.ToString(EGuidFormats::DigitsWithHyphens),
				*FString::FromInt(ItemId), Snapshot.Revision);
		}
	}
	Result.Revision = Snapshot.Revision;
	TerminalCache.Add(Key, Result);
	TerminalPayloadByKey.Add(Key, PayloadSignature);
	return Result;
}
#endif

// 部署保管流程：先校验权限和请求，重放相同载荷或拒绝身份漂移；首次请求写入处理中终态，再检查选择版本及鱼竿状态。
// 库存借出成功后更新当前竿身份、耐久与版本，缓存成功再发布装备读模型；库存只移动实物，不执行效果回调。
FCatInventoryItemUseResult UCatEquipmentComponent::Use(const FGuid RequestId, const int64 ExpectedRevision,
	const FGuid ItemInstanceId, const int32 Quantity)
{
	FCatInventoryItemUseResult Result; Result.RequestId = RequestId;
	auto* Inventory = ResolveOwnerInventoryComponent();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Inventory || !RequestId.IsValid() || !ItemInstanceId.IsValid() || Quantity != 1)
	{ Result.Error = ECatDomainCommandError::InvalidPayload; return Result; }
	const FString Key = MakeTerminalKey(TEXT("DeployItem"), RequestId);
	const FString Payload = FString::Printf(TEXT("%s|%lld"), *ItemInstanceId.ToString(), ExpectedRevision);
	if (const auto* Cached = ItemUseTerminalCache.Find(Key))
	{
		if (TerminalPayloadByKey.FindRef(Key) != Payload) { Result.Error = ECatDomainCommandError::InvalidPayload; return Result; }
		Result = *Cached; MarkInventoryItemUseReplayed(Result); return Result;
	}
	Result.Error = ECatDomainCommandError::AlreadyResolved;
	ItemUseTerminalCache.Add(Key, Result); TerminalPayloadByKey.Add(Key, Payload);
	const auto* Entry = Inventory->GetInventoryEntryAtSlot(Inventory->FindInventorySlotIndexFromInstanceId(ItemInstanceId));
	auto* Item = Entry ? Cast<UCatEquipmentInventoryItemInstance>(Entry->Instance) : nullptr;
	const auto* Definition = Item ? Cast<UCatEquipmentItemDefinition>(Item->GetItemDefinition()) : nullptr;
	if (Snapshot.Revision != ExpectedRevision) Result.Error = ECatDomainCommandError::RevisionConflict;
	else if (!Definition || !Definition->CanServeFishingRod() || Item->IsRodBroken()) Result.Error = ECatDomainCommandError::InvalidPayload;
	else
	{
		const auto Held = Inventory->HoldInventoryItemInstanceFromAuthority(RequestId, ItemInstanceId, Result.Item);
		Result.bCommitted = Held.bCommitted; Result.Error = Held.Error;
		if (Held.bCommitted)
		{
			Snapshot.RodItemId = Item->GetItemId(); Snapshot.RodItemInstanceId = ItemInstanceId;
			Snapshot.RodDurability = Item->GetRodDurability(); Snapshot.bRodBroken = Item->IsRodBroken();
			++Snapshot.Revision;
			// 发布前固定请求终态，读模型观察者重入时只能取得同一次部署结果。
			ItemUseTerminalCache.Add(Key, Result);
			PublishSnapshot();
		}
	}
	ItemUseTerminalCache.Add(Key, Result);
	return Result;
}

// 收回保管流程：先按请求身份重放或占住处理中状态，再查 held 实物与钓鱼占用；占用或非鱼竿来源拒绝归还。
// 库存校验容量并归还后先缓存资源结果，再尝试刷新装备选择；返回值沿用归还结果，不恢复库存快照。
FCatInventoryItemUseResult UCatEquipmentComponent::UnUse(const FGuid RequestId, const FGuid ItemInstanceId)
{
	FCatInventoryItemUseResult Result; Result.RequestId = RequestId;
	auto* Inventory = ResolveOwnerInventoryComponent();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Inventory || !RequestId.IsValid() || !ItemInstanceId.IsValid())
	{ Result.Error = ECatDomainCommandError::InvalidPayload; return Result; }
	const FString Key = MakeTerminalKey(TEXT("ReturnItem"), RequestId);
	const FString Payload = ItemInstanceId.ToString();
	if (const auto* Cached = ItemUseTerminalCache.Find(Key))
	{
		if (TerminalPayloadByKey.FindRef(Key) != Payload) { Result.Error = ECatDomainCommandError::InvalidPayload; return Result; }
		Result = *Cached; MarkInventoryItemUseReplayed(Result); return Result;
	}
	Result.Error = ECatDomainCommandError::AlreadyResolved;
	ItemUseTerminalCache.Add(Key, Result); TerminalPayloadByKey.Add(Key, Payload);
	const auto* Held = Inventory->FindHeldInventoryEntryFromAuthority(ItemInstanceId);
	const auto* Definition = Held && Held->Instance ? Cast<UCatEquipmentItemDefinition>(Held->Instance->GetItemDefinition()) : nullptr;
	if (IsFishingRodInUse(ItemInstanceId)) Result.Error = ECatDomainCommandError::InvalidPhase;
	else if (!Definition || !Definition->CanServeFishingRod()) Result.Error = ECatDomainCommandError::NotFound;
	else
	{
		const auto Returned = Inventory->ReturnHeldInventoryItemInstanceFromAuthority(RequestId, ItemInstanceId, GetConfiguredInventorySlotCapacity(), Result.Item);
		Result.bCommitted = Returned.bCommitted; Result.Error = Returned.Error;
		ItemUseTerminalCache.Add(Key, Result);
		if (Returned.bCommitted) RefreshLoadoutFromInventoryComponentFromAuthority(Definition, Result.Item.Instance->GetItemId());
	}
	ItemUseTerminalCache.Add(Key, Result);
	return Result;
}

FCatFishingUseFreezeResult UCatEquipmentComponent::BeginFishingUse(const FGuid FishingSessionId,
	const FGuid RodItemInstanceId, const FGuid BaitItemInstanceId, const FGuid FloatItemInstanceId,
	const int32  RodItemId, const int32  BaitItemId, const int32  FloatItemId,
	const int64 ExpectedRevision, UCatInventoryComponent* RodInventoryComponent)
{
	// Begin 只验证当前饵/漂并绑定原竿；数量始终留在随身库存，真咬才扣。
	if (const FCatFishingUseRecord* ExistingRecord = FindFishingUseRecord(FishingSessionId))
		return MakeFishingUseFreezeResult(FishingSessionId, ECatDomainCommandError::AlreadyResolved,
			!ExistingRecord->bReleased, ExistingRecord);
	const UCatInventorySettings* InventorySettings = GetDefault<UCatInventorySettings>();
	UCatEquipmentItemDefinition* Rod = InventorySettings
		? InventorySettings->FindRuntimeDefinition<UCatEquipmentItemDefinition>(RodItemId) : nullptr;
	UCatEquipmentItemDefinition* Bait = InventorySettings
		? InventorySettings->FindRuntimeDefinition<UCatEquipmentItemDefinition>(BaitItemId) : nullptr;
	UCatEquipmentItemDefinition* Float = InventorySettings
		? InventorySettings->FindRuntimeDefinition<UCatEquipmentItemDefinition>(FloatItemId) : nullptr;
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return MakeFishingUseFreezeResult(FishingSessionId, ECatDomainCommandError::DependencyUnavailable, false);
	}
	if (!FishingSessionId.IsValid() || !RodItemInstanceId.IsValid() || !BaitItemInstanceId.IsValid()
		|| !FloatItemInstanceId.IsValid() || (RodItemId == 0)
		|| (BaitItemId == 0) || (FloatItemId == 0) || !Rod || !Bait || !Float
		|| !Rod->CanServeFishingRod() || !Bait->CanServeFishingBait()
		|| !Float->CanServeFishingFloat())
	{
		return MakeFishingUseFreezeResult(FishingSessionId, ECatDomainCommandError::InvalidPayload, false);
	}
	if (Snapshot.Revision != ExpectedRevision)
	{
		return MakeFishingUseFreezeResult(FishingSessionId, ECatDomainCommandError::RevisionConflict, false);
	}
	if (Snapshot.BaitItemId != BaitItemId || Snapshot.BaitItemInstanceId != BaitItemInstanceId
		|| Snapshot.FloatItemId != FloatItemId || Snapshot.FloatItemInstanceId != FloatItemInstanceId)
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
	if (!RodUseSlot || RodUseSlot->Instance->GetItemId() != RodItemId
		|| RodUseSlot->Instance->GetItemInstanceId() != RodItemInstanceId || RodUseSlot->StackCount != 1
		|| !BaitSlot || BaitSlot->Instance->GetItemId() != BaitItemId
		|| !FloatSlot || FloatSlot->Instance->GetItemId() != FloatItemId)
	{
		return MakeFishingUseFreezeResult(FishingSessionId, ECatDomainCommandError::NotFound, false);
	}
	if (CastChecked<UCatEquipmentInventoryItemInstance>(RodUseSlot->Instance)->IsRodBroken() || !FMath::IsFinite(CastChecked<UCatEquipmentInventoryItemInstance>(RodUseSlot->Instance)->GetRodDurability())
		|| CastChecked<UCatEquipmentInventoryItemInstance>(RodUseSlot->Instance)->GetRodDurability() <= 0.0)
	{
		return MakeFishingUseFreezeResult(FishingSessionId, ECatDomainCommandError::InvalidPhase, false);
	}
	if (FloatSlot->StackCount <= 0 || BaitSlot->StackCount <= 0)
	{
		return MakeFishingUseFreezeResult(FishingSessionId, ECatDomainCommandError::CapacityExceeded, false);
	}
	for (TObjectIterator<UCatEquipmentComponent> It; It; ++It)
	{
		if (It->GetWorld() != GetWorld()) continue;
		for (const auto& Pair : It->FishingUseRecords)
			if (!Pair.Value.bReleased && Pair.Value.RodInventory.Get() == RodInventory
				&& Pair.Value.RodItemInstanceId == RodItemInstanceId)
				return MakeFishingUseFreezeResult(FishingSessionId, ECatDomainCommandError::InvalidPhase, false);
	}
	FCatFishingUseRecord Record;
	Record.RodItemInstanceId = RodItemInstanceId;
	Record.RodItemId = RodItemId;
	Record.RodInventory = RodInventory;
	Record.BaitSourceEquipment = this;
	FishingUseRecords.Add(FishingSessionId, Record);
	++Snapshot.Revision;
	const FCatFishingUseFreezeResult Result = MakeFishingUseFreezeResult(FishingSessionId, ECatDomainCommandError::None, true);
	UE_LOG(LogCatEquipment, Log, TEXT("Event=fishing_use_bound SessionId=%s RodItemInstanceId=%s World=%s NetMode=%d Authority=1 Owner=%s Result=CommittedBeforeNotify"),
		*FishingSessionId.ToString(), *RodItemInstanceId.ToString(), *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), *GetNameSafe(GetOwner()));
	PublishSnapshot();
	return Result;
}

FCatFishingUseOperationResult UCatEquipmentComponent::CommitFishingBaitDeferred(const FGuid FishingSessionId)
{
	FCatFishingUseRecord* Record = FindFishingUseRecord(FishingSessionId);
	const auto Reject = [&](const ECatDomainCommandError Error, const TCHAR* Reason)
	{
		UE_LOG(LogCatEquipment, Warning, TEXT("Event=fishing_bait_commit_rejected SessionId=%s Reason=%s Error=%s World=%s NetMode=%d Authority=%d LocalRole=%d Owner=%s"),
			*FishingSessionId.ToString(), Reason, *UEnum::GetValueAsString(Error), *GetNameSafe(GetWorld()),
			GetWorld() ? int32(GetWorld()->GetNetMode()) : -1, GetOwner() && GetOwner()->HasAuthority(),
			GetOwner() ? int32(GetOwner()->GetLocalRole()) : -1, *GetNameSafe(GetOwner()));
		return MakeFishingUseOperationResult(FishingSessionId, Error, false, Record);
	};
	if (!GetOwner() || !GetOwner()->HasAuthority()) return Reject(ECatDomainCommandError::DependencyUnavailable, TEXT("NotAuthority"));
	if (!Record) return Reject(ECatDomainCommandError::NotFound, TEXT("NoUseRecord"));
	if (Record->bBaitCommitted)
		return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::AlreadyResolved, false, Record);
	if (Record->bReleased) return Reject(ECatDomainCommandError::InvalidPhase, TEXT("ReleasedBeforeBite"));
	UCatEquipmentComponent* Source = Record->BaitSourceEquipment.Get();
	UCatInventoryComponent* Inventory = Source ? Source->ResolveOwnerInventoryComponent() : nullptr;
	if (!Source || !Source->GetOwner() || Source->GetOwner()->IsActorBeingDestroyed() || !Inventory)
		return Reject(ECatDomainCommandError::DependencyUnavailable, TEXT("BaitSourceUnavailable"));
	const int32 BaitId = Source->Snapshot.BaitItemId;
	const FGuid BaitInstanceId = Source->Snapshot.BaitItemInstanceId;
	UCatEquipmentItemDefinition* Bait = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentItemDefinition>(BaitId);
	const int32 SlotIndex = Inventory->FindInventorySlotIndexFromInstanceId(BaitInstanceId);
	const FCatInventoryEntry* Entry = Inventory->GetInventoryEntryAtSlot(SlotIndex);
	if (!Bait || !Bait->CanServeFishingBait() || !Bait->bRunConsumable || !Entry || !Entry->Instance
		|| Entry->Instance->GetItemId() != BaitId || Entry->StackCount < 1)
		return Reject(ECatDomainCommandError::NotFound, TEXT("CurrentBaitUnavailable"));
	if (!Inventory->ConsumeItemAtSlotInternal(SlotIndex, 1, false))
		return Reject(ECatDomainCommandError::CapacityExceeded, TEXT("ConsumeFailed"));
	// 唯一数量写入已完成。先关闭记录，再刷新读模型和通知，回调可迁移/销毁协调器。
	Record->bBaitCommitted = true;
	Source->ReconcileLoadoutSelectionsWithInventory(Bait, BaitId);
	++Source->Snapshot.Revision;
	if (Source != this) ++Snapshot.Revision;
	const FCatFishingUseOperationResult Result = MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::None, true, Record);
	UE_LOG(LogCatEquipment, Log, TEXT("Event=fishing_current_bait_committed SessionId=%s Bait=%s BaitInstanceId=%s Quantity=1 World=%s NetMode=%d Authority=1 LocalRole=%d Source=%s Owner=%s Result=CommittedBeforeNotify"),
		*FishingSessionId.ToString(), *FString::FromInt(BaitId), *BaitInstanceId.ToString(), *GetNameSafe(GetWorld()),
		int32(GetWorld()->GetNetMode()), int32(GetOwner()->GetLocalRole()), *GetNameSafe(Source->GetOwner()), *GetNameSafe(GetOwner()));
	const TWeakObjectPtr<UCatEquipmentComponent> WeakSource = Source;
	const TWeakObjectPtr<UCatEquipmentComponent> WeakCoordinator = this;
	Inventory->BroadcastInventoryChange(SlotIndex);
	if (WeakSource.IsValid()) WeakSource->PublishSnapshot();
	if (WeakCoordinator.IsValid() && WeakCoordinator != WeakSource) WeakCoordinator->PublishSnapshot();
	return Result;
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

// 断竿报废流程：
// 1. 先要求 authority、会话记录与正式库存都在；缺任一项都不动库存事实。
// 2. 按 Begin 冻结的实例 ID 解析当前实例并确认它**真的已断**——没断的竿绝不在这里消失。
// 3. 按它此刻所在的位置移除：正在部署走 held entry 退役，已经在可见格就清那一格。
// 4. 最后把指向它的钓具选择清空并重新校正，保持未选竿，等待玩家主动取竿。
bool UCatEquipmentComponent::RetireBrokenFishingRodFromAuthority(const FGuid FishingSessionId)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	const FCatFishingUseRecord* Record = FindFishingUseRecord(FishingSessionId);
	if (Record == nullptr || !Record->RodItemInstanceId.IsValid())
	{
		return false;
	}
	UCatInventoryComponent* RodInventory = Record->RodInventory.Get();
	if (RodInventory == nullptr)
	{
		return false;
	}
	FCatInventoryEntry RodItem;
	const UCatEquipmentInventoryItemInstance* FormalRodInstance =
		ResolveFishingRodFormalInstanceFromInventory(*Record, RodItem);
	if (FormalRodInstance == nullptr || !FormalRodInstance->IsRodBroken())
	{
		return false;
	}

	const FGuid RodItemInstanceId = Record->RodItemInstanceId;
	bool bRemoved = RodInventory->RetireHeldInventoryEntryFromAuthority(RodItemInstanceId);
	if (!bRemoved)
	{
		const int32 SlotIndex = RodInventory->FindInventorySlotIndexFromInstanceId(RodItemInstanceId);
		FCatInventoryEntry RemovedEntry;
		bRemoved = SlotIndex != INDEX_NONE
			&& RodInventory->RemoveInventoryEntryAtSlotFromAuthority(SlotIndex, RemovedEntry);
	}
	if (!bRemoved)
	{
		UE_LOG(LogCatEquipment, Warning,
			TEXT("Event=equipment_broken_rod_retire_failed SessionId=%s RodItemInstanceId=%s Owner=%s Reason=ItemNotFoundInInventory"),
			*FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			*RodItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(GetOwner()));
		return false;
	}

	// 竿可能是借来的：实例住在竿主的库存里，而这条会话记录挂在抛竿者身上。
	// 两边的选择读模型都可能指着刚被销毁的那根，所以两边都要清并各自重新校正（与磨损写回的处理同源）。
	UCatEquipmentComponent* RodOwnerEquipment = RodInventory->GetOwner()
		? RodInventory->GetOwner()->FindComponentByClass<UCatEquipmentComponent>() : nullptr;
	const auto ClearRetiredRodSelection = [RodItemInstanceId](UCatEquipmentComponent& Equipment)
	{
		if (Equipment.Snapshot.RodItemInstanceId == RodItemInstanceId)
		{
			Equipment.Snapshot.RodItemId = 0;
			Equipment.Snapshot.RodItemInstanceId.Invalidate();
			Equipment.Snapshot.RodDurability = 0.0;
			Equipment.Snapshot.bRodBroken = false;
		}
		Equipment.ReconcileLoadoutSelectionsWithInventory(nullptr, 0);
		++Equipment.Snapshot.Revision;
		Equipment.PublishSnapshot();
	};
	ClearRetiredRodSelection(*this);
	if (RodOwnerEquipment != nullptr && RodOwnerEquipment != this)
	{
		ClearRetiredRodSelection(*RodOwnerEquipment);
	}
	UE_LOG(LogCatEquipment, Log,
		TEXT("Event=equipment_broken_rod_retired SessionId=%s RodItemInstanceId=%s Owner=%s Revision=%lld Result=ItemDestroyed"),
		*FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
		*RodItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(GetOwner()), Snapshot.Revision);
	return true;
}

FCatFishingUseOperationResult UCatEquipmentComponent::ReleaseFishingUse(const FGuid FishingSessionId)
{
	FCatFishingUseRecord* Record = FindFishingUseRecord(FishingSessionId);
	if (!Record) return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::NotFound, false);
	if (Record->bReleased) return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::AlreadyResolved, false, Record);
	if (!GetOwner() || !GetOwner()->HasAuthority())
		return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::DependencyUnavailable, false, Record);
	Record->bReleased = true;
	++Snapshot.Revision;
	const FCatFishingUseOperationResult Result = MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::None, true, Record);
	UE_LOG(LogCatEquipment, Log, TEXT("Event=fishing_use_released SessionId=%s Owner=%s World=%s NetMode=%d Authority=1 LocalRole=%d BaitCommitted=%d Result=ClosedWithoutRefund"),
		*FishingSessionId.ToString(), *GetNameSafe(GetOwner()), *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()),
		int32(GetOwner()->GetLocalRole()), Record->bBaitCommitted);
	PublishSnapshot();
	return Result;
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

int32  UCatEquipmentComponent::GetCurrentFishingBaitItemId(const FGuid FishingSessionId) const
{
	const FCatFishingUseRecord* Record = FishingUseRecords.Find(FishingSessionId);
	const UCatEquipmentComponent* Source = Record && !Record->bReleased ? Record->BaitSourceEquipment.Get() : nullptr;
	return Source && IsValid(Source->GetOwner()) && !Source->GetOwner()->IsActorBeingDestroyed()
		? Source->GetSnapshot().BaitItemId : 0;
}

bool UCatEquipmentComponent::IsFishingBaitCommitted(const FGuid FishingSessionId) const
{
	const FCatFishingUseRecord* Record = FindFishingUseRecord(FishingSessionId);
	return FishingSessionId.IsValid() && Record && !Record->bReleased && Record->bBaitCommitted;
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
int32 UCatEquipmentComponent::GetInventoryStackLimit(const UCatEquipmentItemDefinition& Definition) const
{
	return Definition.GetMaxStackCount();
}

// 公开选择刷新流程：
// 1. 公开入口只负责根据 Owner 正式库存校正钓具选择，不声明新增物品。
// 2. 实际选择一致性校正、版本推进和发布交给带参数入口，保证营地转移和普通刷新共用同一条规则。
bool UCatEquipmentComponent::RefreshLoadoutFromInventoryComponentFromAuthority()
{
	return RefreshLoadoutFromInventoryComponentFromAuthority(nullptr, 0);
}

// 正式库存到钓具读模型刷新流程：
// 1. 只在 authority 上读取 Owner 的 InventoryComponent；客户端复制读模型不能反向生成服务器快照。
// 2. 先记录刷新前的钓具选择，再校正当前选择：有效选择保持不动，缺失或已离开随身库存的实例会换到可用同类或清空。
// 3. 选择有变化时才推进 Equipment Revision 并发布读模型；正式背包事实仍只来自 InventoryComponent。
bool UCatEquipmentComponent::RefreshLoadoutFromInventoryComponentFromAuthority(
	const UCatEquipmentItemDefinition* GrantedDefinition, const int32  GrantedItemId)
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

	const int32  PreviousRodItemId = Snapshot.RodItemId;
	const FGuid PreviousRodItemInstanceId = Snapshot.RodItemInstanceId;
	const double PreviousRodDurability = Snapshot.RodDurability;
	const bool bPreviousRodBroken = Snapshot.bRodBroken;
	const int32  PreviousBaitItemId = Snapshot.BaitItemId;
	const FGuid PreviousBaitItemInstanceId = Snapshot.BaitItemInstanceId;
	const int32  PreviousFloatItemId = Snapshot.FloatItemId;
	const FGuid PreviousFloatItemInstanceId = Snapshot.FloatItemInstanceId;
	const int32  PreviousScoopNetItemId = Snapshot.ScoopNetItemId;
	const FGuid PreviousScoopNetItemInstanceId = Snapshot.ScoopNetItemInstanceId;

	ReconcileLoadoutSelectionsWithInventory(GrantedDefinition, GrantedItemId);

	// 鱼竿耐久是双精度运行值；这里用引擎小容差过滤浮点微差，避免没有真实选择变化时空转推进 Equipment Revision。
	const bool bSelectionChanged = PreviousRodItemId != Snapshot.RodItemId
		|| PreviousRodItemInstanceId != Snapshot.RodItemInstanceId
		|| !FMath::IsNearlyEqual(PreviousRodDurability, Snapshot.RodDurability, KINDA_SMALL_NUMBER)
		|| bPreviousRodBroken != Snapshot.bRodBroken
		|| PreviousBaitItemId != Snapshot.BaitItemId
		|| PreviousBaitItemInstanceId != Snapshot.BaitItemInstanceId
		|| PreviousFloatItemId != Snapshot.FloatItemId
		|| PreviousFloatItemInstanceId != Snapshot.FloatItemInstanceId
		|| PreviousScoopNetItemId != Snapshot.ScoopNetItemId
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
bool UCatEquipmentComponent::TryResolveSelectionInventorySlot(const int32  ItemId,
	const FGuid ItemInstanceId, FCatInventoryEntry& OutSlot) const
{
	OutSlot = FCatInventoryEntry();
	if ((ItemId == 0))
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
			|| OutSlot.Instance->GetItemId() != ItemId)
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
			|| CandidateSlot.Instance->GetItemId() != ItemId)
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
	UCatInventoryComponent& OwnerInventory, const UCatEquipmentItemDefinition& RodDefinition,
	FCatInventoryEntry& OutSlot) const
{
	OutSlot = FCatInventoryEntry();
	if (!Snapshot.RodItemInstanceId.IsValid()
		|| Snapshot.RodItemId != RodDefinition.ItemId
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
		|| OutSlot.Instance->GetItemId() != Snapshot.RodItemId
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
	if (!Record.RodItemInstanceId.IsValid() || (Record.RodItemId == 0))
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
		|| OutSlot.Instance->GetItemId() != Record.RodItemId
		|| OutSlot.StackCount != 1)
	{
		OutSlot = FCatInventoryEntry();
		return nullptr;
	}

	return FormalInstance;
}

UCatEquipmentInventoryItemInstance* UCatEquipmentComponent::ResolveDeployedRodItemInstanceFromAuthority(const FGuid RodItemInstanceId) const
{
	// 部署实例解析流程：世界竿只能引用 Owner 库存 held-entry 中的同一实例，不能按定义回退到另一根同类鱼竿。
	if (!GetOwner() || !GetOwner()->HasAuthority() || !RodItemInstanceId.IsValid()) return nullptr;
	FCatInventoryEntry HeldSlot;
	if (!TryBuildHeldInventoryUseSlot(RodItemInstanceId, HeldSlot)) return nullptr;
	UCatEquipmentInventoryItemInstance* Instance = Cast<UCatEquipmentInventoryItemInstance>(HeldSlot.Instance);
	const UCatEquipmentItemDefinition* Definition = Instance ? Cast<UCatEquipmentItemDefinition>(Instance->GetItemDefinition()) : nullptr;
	return Definition && Definition->CanServeFishingRod() && Instance->GetItemInstanceId() == RodItemInstanceId ? Instance : nullptr;
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
	const UCatEquipmentItemDefinition* PreferredDefinition, const int32  PreferredItemId)
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
		&& !(PreferredItemId == 0)
		&& PreferredDefinition->CanServeFishingRod();
	FCatInventoryEntry PreferredRodSlot;
	const bool bHasPreferredRodSlot = bPreferredRod
		&& TryResolveSelectionInventorySlot(PreferredItemId, FGuid(), PreferredRodSlot);
	// 墓碑（2026-09-14，T35，商店 §3.1.2）：库存校正不再扫描备用竿自动替换断竿。
	// 只有明确入库/选择传来的 PreferredDefinition 可以建立新选择。
	FCatInventoryEntry SelectedStoredRod;
	const bool bSelectedStoredRodMatches =
		TryFindInventorySlotByInstanceId(Snapshot.RodItemInstanceId, SelectedStoredRod)
		&& SelectedStoredRod.Instance->GetItemId() == Snapshot.RodItemId;
	FCatInventoryEntry ActiveSelectedRodSlot;
	const bool bSelectedRodIsInUse =
		TryBuildHeldInventoryUseSlot(Snapshot.RodItemInstanceId, ActiveSelectedRodSlot)
		&& ActiveSelectedRodSlot.Instance->GetItemId() == Snapshot.RodItemId;
	const bool bSelectedRodMissing = (Snapshot.RodItemId == 0)
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
	const bool bShouldReplaceRod = (bSelectedRodBrokenOrInvalid || bSelectedRodMissing)
		&& bHasReplacementRodSlot;
	if (bShouldReplaceRod)
	{
		const int32  PreviousItemId = Snapshot.RodItemId;
		const FGuid PreviousItemInstanceId = Snapshot.RodItemInstanceId;
		const double PreviousRodDurability = Snapshot.RodDurability;
		const bool bPreviousRodBroken = Snapshot.bRodBroken;
		Snapshot.RodItemId = ReplacementRodSlot.Instance->GetItemId();
		Snapshot.RodItemInstanceId = ReplacementRodSlot.Instance->GetItemInstanceId();
		Snapshot.RodDurability = CastChecked<UCatEquipmentInventoryItemInstance>(ReplacementRodSlot.Instance)->GetRodDurability();
		Snapshot.bRodBroken = CastChecked<UCatEquipmentInventoryItemInstance>(ReplacementRodSlot.Instance)->IsRodBroken();
		UE_LOG(LogCatEquipment, Log,
			TEXT("Event=equipment_rod_selection_reconciled Reason=%s PreviousDefinition=%s PreviousItem=%s PreviousDurability=%.3f PreviousBroken=%s SelectedDefinition=%s SelectedItem=%s SelectedDurability=%.3f SelectedBroken=%s ActiveUse=%s Revision=%lld Owner=%s World=%s NetMode=%d"),
			bSelectedRodMissing ? TEXT("MissingInstance") : TEXT("BrokenOrInvalid"),
			*FString::FromInt(PreviousItemId),
			*PreviousItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), PreviousRodDurability,
			bPreviousRodBroken ? TEXT("true") : TEXT("false"), *FString::FromInt(Snapshot.RodItemId),
			*Snapshot.RodItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), Snapshot.RodDurability,
			Snapshot.bRodBroken ? TEXT("true") : TEXT("false"),
			bSelectedRodIsInUse ? TEXT("true") : TEXT("false"), Snapshot.Revision, *GetNameSafe(GetOwner()),
			*GetNameSafe(GetWorld()), static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone));
	}
	else if (bSelectedRodMissing)
	{
		const int32  PreviousItemId = Snapshot.RodItemId;
		const FGuid PreviousItemInstanceId = Snapshot.RodItemInstanceId;
		Snapshot.RodItemId = 0;
		Snapshot.RodItemInstanceId.Invalidate();
		Snapshot.RodDurability = 0.0;
		Snapshot.bRodBroken = false;
		UE_LOG(LogCatEquipment, Log,
			TEXT("Event=equipment_rod_selection_reconciled Reason=ClearedMissingInstance PreviousDefinition=%s PreviousItem=%s Revision=%lld Owner=%s World=%s NetMode=%d"),
			*FString::FromInt(PreviousItemId),
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
			PreferredItemId, &VisibleSlots](const int32  CurrentItemId,
				const FName SlotRole, FCatInventoryEntry& OutSlot) -> bool
	{
		OutSlot = FCatInventoryEntry();
		if (PreferredDefinition != nullptr && !(PreferredItemId == 0)
			&& PreferredDefinition->CanServeFishingLoadoutSlot(SlotRole))
		{
			if (TryResolveSelectionInventorySlot(PreferredItemId, FGuid(), OutSlot))
			{
				return true;
			}
		}
		if (TryResolveSelectionInventorySlot(CurrentItemId, FGuid(), OutSlot))
		{
			return true;
		}
		for (const FCatInventoryEntry& Slot : VisibleSlots)
		{
			const UCatEquipmentItemDefinition* SlotDefinition = InventorySettings != nullptr
				? InventorySettings->FindRuntimeDefinition<UCatEquipmentItemDefinition>(Slot.Instance->GetItemId()) : nullptr;
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
			int32& InOutItemId, FGuid& InOutItemInstanceId)
	{
		FCatInventoryEntry SelectedSlot;
		const bool bSelectedItemValid = TryFindInventorySlotByInstanceId(InOutItemInstanceId, SelectedSlot)
			&& SelectedSlot.Instance->GetItemId() == InOutItemId && SelectedSlot.StackCount > 0;
		if (!(InOutItemId == 0) && bSelectedItemValid)
		{
			return;
		}

		const int32  PreviousItemId = InOutItemId;
		const FGuid PreviousItemInstanceId = InOutItemInstanceId;
		FCatInventoryEntry ReplacementSlot;
		if (!FindFirstSlotForSlotRole(InOutItemId, SlotRole, ReplacementSlot))
		{
			InOutItemId = 0;
			InOutItemInstanceId.Invalidate();
			UE_LOG(LogCatEquipment, Log,
				TEXT("Event=equipment_item_selection_reconciled LoadoutSlot=%s Reason=ClearedMissingInstance PreviousDefinition=%s PreviousItem=%s Revision=%lld Owner=%s World=%s NetMode=%d"),
				LoadoutSlotName, *FString::FromInt(PreviousItemId),
				*PreviousItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), Snapshot.Revision,
				*GetNameSafe(GetOwner()), *GetNameSafe(GetWorld()),
				static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone));
			return;
		}

		InOutItemId = ReplacementSlot.Instance->GetItemId();
		InOutItemInstanceId = ReplacementSlot.Instance->GetItemInstanceId();
		UE_LOG(LogCatEquipment, Log,
			TEXT("Event=equipment_item_selection_reconciled LoadoutSlot=%s Reason=%s PreviousDefinition=%s PreviousItem=%s SelectedDefinition=%s SelectedItem=%s Revision=%lld Owner=%s World=%s NetMode=%d"),
			LoadoutSlotName, (PreviousItemId == 0) ? TEXT("Unavailable") : TEXT("MissingInstance"),
			*FString::FromInt(PreviousItemId),
			*PreviousItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), *FString::FromInt(InOutItemId),
			*InOutItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), Snapshot.Revision,
			*GetNameSafe(GetOwner()), *GetNameSafe(GetWorld()),
			static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone));
	};
	ReconcileNonRodSelection(TEXT("Bait"), UCatEquipmentItemDefinition::FishingBaitLoadoutSlotId(),
		Snapshot.BaitItemId, Snapshot.BaitItemInstanceId);
	ReconcileNonRodSelection(TEXT("Float"), UCatEquipmentItemDefinition::FishingFloatLoadoutSlotId(),
		Snapshot.FloatItemId, Snapshot.FloatItemInstanceId);
	ReconcileNonRodSelection(TEXT("ScoopNet"), UCatEquipmentItemDefinition::ScoopNetLoadoutSlotId(),
		Snapshot.ScoopNetItemId, Snapshot.ScoopNetItemInstanceId);
}

FCatFishingUseFreezeResult UCatEquipmentComponent::MakeFishingUseFreezeResult(const FGuid FishingSessionId,
	const ECatDomainCommandError Error, const bool bUseAccepted, const FCatFishingUseRecord* Record) const
{
	// 装备使用结果组装流程：先写命令终态和当前 Equipment Revision，再从绑定鱼竿实例读取耐久状态；记录缺失时只返回默认耐久，不制造新会话状态。
	FCatFishingUseFreezeResult Result;
	Result.SessionId = FishingSessionId;
	Result.Error = Error;
	Result.EquipmentRevision = Snapshot.Revision;
	Result.bUseAccepted = bUseAccepted;
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
        const auto* Definition = Instance ? Cast<UCatEquipmentItemDefinition>(Instance->GetItemDefinition()) : nullptr;
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
    ReconcileLoadoutSelectionsWithInventory(nullptr, 0);
    Target->ReconcileLoadoutSelectionsWithInventory(nullptr, 0);
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
            if (!Pair.Value.bReleased && Pair.Value.RodInventory.Get(true) == Inventory && Pair.Value.RodItemInstanceId == ItemInstanceId) return true;
    }
    return false;
}

void UCatEquipmentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (GetOwner() && GetOwner()->HasAuthority() && GetWorld())
        if (auto* Fishing = GetWorld()->GetSubsystem<UCatFishingService>())
            Fishing->PreserveFishingResourcesForEquipmentShutdown(this);
    Super::EndPlay(EndPlayReason);
}
