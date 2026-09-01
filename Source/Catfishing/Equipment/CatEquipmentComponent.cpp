#include "Equipment/CatEquipmentComponent.h"

#include "Framework/Game/CatGameplayTypes.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Equipment/CatInventoryItemUseRegistry.h"
#include "Equipment/CatRunInventorySlotOperations.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

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

// Snapshot 读取流程：返回服务器真相或客户端最近复制值；不从 Profile 或 Items 拼接第二份随身库存事实。
const FCatEquipmentLoadoutSnapshot& UCatEquipmentComponent::GetSnapshot() const
{
	return Snapshot;
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
// 4. 成功时写入随身库存格数组，并按空选择、无库存旧选择或已断/耐久非法同定义竿的规则修正当前选择。
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

// 商店非数量物品入库流程：
// 1. 先用 RequestId 和定义 ID 找终态缓存；合法重放只返回首次结果，不重复增加库存数量或推进 Revision。
// 2. 首次提交复用扣款前预检同一套准入规则，并用 ExpectedRevision 防止陈旧 UI 覆盖较新的本人库存。
// 3. 把非数量定义加入随身库存格数组，并按空选择、无库存旧选择或已断/耐久非法同定义竿的规则修正当前选择。
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
	if (HasActiveFishingUse())
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
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
	// 物品停止使用流程：
	// 1. 先校验 authority 和 RequestId，再按实例载荷签名处理重放；同一收口请求不会重复放回同一物品。
	// 2. 收口前先看背包是否已经残留同一实例；定义一致时更新那一格并收口，定义不一致则按坏数据拒绝。
	// 3. 没有残留时预检背包是否能原样放回该实例；容量不足时保持场景 Actor 和活动记录不变。
	// 4. 放回成功后才释放活动记录、按需同步鱼竿选择状态并发布库存快照，部署型调用方随后可以隐藏或销毁世界 Actor。
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

// 库存整理流程：
// 1. 先用 RequestId、Revision 和源/目标下标查询终态缓存，合法重放必须返回首次结果，不受当前 Fishing 阶段影响。
// 2. 首次请求先检查 authority、RequestId、Revision 和槽位下标，避免陈旧 UI 改写新的随身库存快照。
// 3. 服务器重读当前库存格数组，若源格或目标格残留正在 Use 的同实例则拒绝，避免旧重复数据继续被普通整理路径传播。
// 4. 源格必须有物品，目标格移动/合并/交换复用运行库存格通用规则。
// 5. 成功移动后推进 Revision 并发布同一份库存快照；View 只通过 OnSnapshotChanged 重刷。
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
			if (IsInventoryItemInstanceBlockedByActiveUse(Snapshot.InventorySlots[SourceSlotIndex].ItemInstanceId)
				|| IsInventoryItemInstanceBlockedByActiveUse(Snapshot.InventorySlots[TargetSlotIndex].ItemInstanceId))
			{
				Result.Error = ECatDomainCommandError::InvalidPhase;
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
	const int64 ExpectedRevision)
{
	// 建立 Fishing 使用预留的流程：
	// 1. 先用 SessionId 返回已存在的终态，保证 FishingSession 重放不会再检查或再占库存。
	// 2. 再校验 authority、定义类型、Revision、当前钓鱼选择和三份实例身份，任何不一致都保持快照不变。
	// 3. 鱼竿实例必须来自活动 Use 记录，鱼饵和鱼漂实例必须仍在库存中，避免场景竿和背包格引用不同物品。
	// 4. 鱼饵预留按 ItemInstanceId 计数；多堆同定义饵不会互相借数量，也不会在 Commit 时误扣另一堆。
	// 5. 最后只写入本 Session 预留记录，不递增 Revision；真正的库存变化留到 Commit 阶段发布。
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
	const FCatRunInventorySlot* BaitSlot = FindInventorySlotByInstanceId(BaitItemInstanceId);
	const FCatRunInventorySlot* FloatSlot = FindInventorySlotByInstanceId(FloatItemInstanceId);
	if (!RodUseRecord || RodUseRecord->bReleased || RodUseRecord->Item.DefinitionId != RodDefinitionId
		|| !BaitSlot || BaitSlot->DefinitionId != BaitDefinitionId
		|| !FloatSlot || FloatSlot->DefinitionId != FloatDefinitionId)
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::NotFound, false);
	}
	if (FloatSlot->Quantity <= 0
		|| BaitSlot->Quantity <= GetPendingReservedFishingBaitCount(BaitItemInstanceId))
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::CapacityExceeded, false);
	}

	FCatFishingUseRecord Record;
	Record.SessionId = FishingSessionId;
	Record.RodItemInstanceId = RodItemInstanceId;
	Record.BaitItemInstanceId = BaitItemInstanceId;
	Record.FloatItemInstanceId = FloatItemInstanceId;
	Record.RodDefinitionId = RodDefinitionId;
	Record.BaitDefinitionId = BaitDefinitionId;
	Record.FloatDefinitionId = FloatDefinitionId;
	Record.ReservationRevision = Snapshot.Revision;
	Record.bBaitQuantityReserved = true;
	FishingUseRecords.Add(FishingSessionId, Record);
	return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::None, true);
}

FCatFishingUseOperationResult UCatEquipmentComponent::CommitFishingBait(const FGuid FishingSessionId)
{
	// 立即提交入口只负责串起“先提交、再发布”的固定顺序；真实扣减仍集中在 Deferred 版本，避免两条入口各自维护库存语义。
	const FCatFishingUseOperationResult Result = CommitFishingBaitDeferred(FishingSessionId);
	if (Result.bApplied) PublishDeferredFishingBait(FishingSessionId);
	return Result;
}

FCatFishingUseOperationResult UCatEquipmentComponent::CommitFishingBaitDeferred(const FGuid FishingSessionId)
{
	// 延迟提交鱼饵的流程：
	// 1. 先找到 Begin 阶段留下的记录；没有记录说明 Fishing 从未拿到装备使用权。
	// 2. 已释放或已提交的记录只返回终态，不允许重复扣同一份库存。
	// 3. 只有该 Session 自己仍处于活动预留态才能提交，旧会话 tombstone 不会补扣。
	// 4. Begin 已经保护了选中鱼饵实例的一份数量，这里只扣那一格并递增快照 Revision。
	// 5. 只标记已提交，不广播快照；调用方可以先完成 Fishing 自己的终态事件，再显式 Publish。
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
	// Begin 已经为这场 Fishing 保护选中饵实例的一份数量；Commit 只消费这份受保护数量，重放不会再次扣库存。
	if (Record->bBaitQuantityReserved)
	{
		FCatRunInventorySlot ConsumedBait;
		const FCatRunInventorySlot* CurrentBaitSlot = FindInventorySlotByInstanceId(Record->BaitItemInstanceId);
		if (CurrentBaitSlot && CurrentBaitSlot->DefinitionId == Record->BaitDefinitionId
			&& RemoveInventoryItemQuantityFromInstance(Record->BaitItemInstanceId, 1, ConsumedBait))
		{
			++Snapshot.Revision;
			Record->bBaitCommitted = true;
		}
		else
		{
			return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::CapacityExceeded, false, Record);
		}
	}
	else
	{
		Record->bBaitCommitted = true;
	}
	return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::None, true, Record);
}

void UCatEquipmentComponent::PublishDeferredFishingBait(const FGuid FishingSessionId)
{
	// 发布延迟扣饵结果：只有已经提交且尚未发布的记录会广播快照；重复调用只更新本地终态标记，不会制造第二次 UI/网络变化。
	FCatFishingUseRecord* Record = FindFishingUseRecord(FishingSessionId);
	if (!Record || !Record->bBaitCommitted || Record->bBaitCommitPublished) return;
	Record->bBaitCommitPublished = true;
	if (Record->bBaitQuantityReserved) PublishSnapshot();
}

FCatFishingUseOperationResult UCatEquipmentComponent::SetAccumulatedFishingRodWear(const FGuid FishingSessionId,
	const int64 WearSequence, const double AbsoluteTotal)
{
	FCatFishingUseRecord* Record = FindFishingUseRecord(FishingSessionId);
	if (!Record)
	{
		return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::NotFound, false);
	}
	if (Record->bReleased || Record->bBreakCommitted || Record->bWearCommitted)
	{
		return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::AlreadyResolved, false, Record);
	}
	if (!IsFishingUseActive(FishingSessionId))
	{
		return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::InvalidPhase, false, Record);
	}
	if (!FMath::IsFinite(AbsoluteTotal) || AbsoluteTotal < 0.0 || AbsoluteTotal < Record->AbsoluteRodWear)
	{
		return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::InvalidPayload, false, Record);
	}
	if (Record->LastWearSequence == 0 && WearSequence != 1)
	{
		return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::InvalidPayload, false, Record);
	}
	if (WearSequence == Record->LastWearSequence)
	{
		const ECatDomainCommandError Error = AbsoluteTotal == Record->AbsoluteRodWear
			? ECatDomainCommandError::AlreadyResolved : ECatDomainCommandError::InvalidPayload;
		return MakeFishingUseOperationResult(FishingSessionId, Error, false, Record);
	}
	if (WearSequence != Record->LastWearSequence + 1)
	{
		return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::InvalidPayload, false, Record);
	}
	Record->LastWearSequence = WearSequence;
	Record->AbsoluteRodWear = AbsoluteTotal;
	return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::None, true, Record);
}

FCatFishingUseOperationResult UCatEquipmentComponent::CommitFishingRodWear(const FGuid FishingSessionId)
{
	FCatFishingUseRecord* Record = FindFishingUseRecord(FishingSessionId);
	if (!Record)
	{
		return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::NotFound, false);
	}
	if (Record->bReleased || Record->bWearCommitted || Record->bBreakCommitted)
	{
		return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::AlreadyResolved, false, Record);
	}
	if (!IsFishingUseActive(FishingSessionId) || Record->LastWearSequence == 0)
	{
		return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::InvalidPhase, false, Record);
	}
	if (Record->AbsoluteRodWear >= Snapshot.RodDurability)
	{
		return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::InvalidPhase, false, Record);
	}
	Snapshot.RodDurability -= Record->AbsoluteRodWear;
	SyncSelectedRodStateToSelectedInstance();
	Record->bWearCommitted = true;
	Record->bReleased = true;
	++Snapshot.Revision;
	PublishSnapshot();
	return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::None, true, Record);
}

FCatFishingUseOperationResult UCatEquipmentComponent::CommitFishingRodBreak(const FGuid FishingSessionId)
{
	FCatFishingUseRecord* Record = FindFishingUseRecord(FishingSessionId);
	if (!Record)
	{
		return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::NotFound, false);
	}
	if (Record->bReleased || Record->bBreakCommitted || Record->bWearCommitted)
	{
		return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::AlreadyResolved, false, Record);
	}
	if (!IsFishingUseActive(FishingSessionId))
	{
		return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::InvalidPhase, false, Record);
	}
	Snapshot.RodDurability = 0.0;
	Snapshot.bRodBroken = true;
	SyncSelectedRodStateToSelectedInstance();
	Record->bBreakCommitted = true;
	Record->bReleased = true;
	++Snapshot.Revision;
	PublishSnapshot();
	return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::None, true, Record);
}

FCatFishingUseOperationResult UCatEquipmentComponent::ReleaseFishingUse(const FGuid FishingSessionId)
{
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

// 维修流程：验证固定营地事实、Revision、当前 Rod/浮木定义和库存；鱼竿正在 Use 时拒绝，成功只扣一份浮木并恢复同一实例耐久。
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
	// 2. 首次提交只接受 authority、正确 Revision、未被 Use 占用的实例和可运行定义；数量型与装备型都走完整实例入库，不重新生成 ItemInstanceId。
	// 3. 精确实例占用会被拒绝，但不会因为玩家正在部署另一件物品就禁止其他实例入库。
	// 4. 成功后按定义自动修正当前选择并发布快照，调用方不需要知道这份实例落到了哪个格子。
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
	else if (IsInventoryItemInstanceBlockedByActiveUse(Item.ItemInstanceId))
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
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

bool UCatEquipmentComponent::IsInventoryItemInstanceInUse(const FGuid ItemInstanceId) const
{
	// 精确实例占用判断流程：无效 ID 不能代表真实物品；有效 ID 只查同一条 Use 记录，Released 后允许收口或重新入库。
	if (!ItemInstanceId.IsValid())
	{
		return false;
	}
	if (const FCatInventoryItemUseRecord* ActiveUse = FindInventoryItemUseRecord(ItemInstanceId))
	{
		return !ActiveUse->bReleased;
	}
	return false;
}

bool UCatEquipmentComponent::IsInventoryItemInstanceBlockedByActiveUse(const FGuid ItemInstanceId) const
{
	// 通用占用 gate 流程：
	// 1. 无效实例不能代表真实物品，空格和旧空 GUID 不阻止整理、交易或入库。
	// 2. 先查本 Equipment 的活动 Use 记录，覆盖正常部署后还未收口的同一实例。
	// 3. 再查尚未释放的 Fishing 记录，只拦当前会话真正保护的鱼饵和鱼漂实例，不把整局钓鱼当成库存锁。
	// 4. 最后查当前服务器 World 的场景物品登记，覆盖公共仓库、交易或坏数据把同一实例送回别处的路径。
	if (!ItemInstanceId.IsValid())
	{
		return false;
	}
	if (IsInventoryItemInstanceInUse(ItemInstanceId))
	{
		return true;
	}
	for (const TPair<FGuid, FCatFishingUseRecord>& Pair : FishingUseRecords)
	{
		const FCatFishingUseRecord& Record = Pair.Value;
		const bool bBaitReservedByActiveFishing = !Record.bReleased && Record.bBaitQuantityReserved
			&& !Record.bBaitCommitted && Record.BaitItemInstanceId == ItemInstanceId;
		const bool bFloatBoundToActiveFishing = !Record.bReleased && Record.FloatItemInstanceId == ItemInstanceId;
		if (bBaitReservedByActiveFishing || bFloatBoundToActiveFishing)
		{
			return true;
		}
	}
	if (UWorld* World = GetWorld())
	{
		if (UCatInventoryItemUseRegistry* ItemUseRegistry = World->GetSubsystem<UCatInventoryItemUseRegistry>())
		{
			return ItemUseRegistry->IsItemInstanceInWorld(ItemInstanceId);
		}
	}
	return false;
}

int32 UCatEquipmentComponent::GetPendingReservedFishingBaitCount(const FGuid BaitItemInstanceId) const
{
	// 鱼饵实例预留数量读取流程：只统计同一 ItemInstanceId 尚未提交、尚未释放的 Fishing 记录，防止同定义多堆鱼饵互相借库存。
	int32 ReservedCount = 0;
	for (const TPair<FGuid, FCatFishingUseRecord>& Pair : FishingUseRecords)
	{
		const FCatFishingUseRecord& Record = Pair.Value;
		if (!Record.bReleased && Record.bBaitQuantityReserved && !Record.bBaitCommitted
			&& Record.BaitItemInstanceId == BaitItemInstanceId)
		{
			++ReservedCount;
		}
	}
	return ReservedCount;
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
// 1. 新获得的物品只在当前选择缺失、旧选择无库存/无活动 Use，或同定义已选竿已断/耐久非法时介入。
// 2. 鱼竿选择刷新会记录具体实例并读取这根实例自己的耐久；鱼饵、鱼漂和抄网也保留被选中的实例身份。
// 3. 这个流程不移出库存物品，也不创建独立装备栏；Fishing 后续仍从同一库存数组扣饵。
void UCatEquipmentComponent::AutoSelectGrantedInventoryItem(const UCatEquipmentDefinition& Definition,
	const FName DefinitionId)
{
	if (DefinitionId.IsNone())
	{
		return;
	}
	if (Definition.Kind == ECatEquipmentKind::Rod)
	{
		const FCatRunInventorySlot* GrantedSlot = nullptr;
		for (const FCatRunInventorySlot& Slot : Snapshot.InventorySlots)
		{
			if (Slot.DefinitionId == DefinitionId && CatRunInventorySlotOperations::IsInventorySlotOccupied(Slot))
			{
				if (!Slot.bRodBroken && FMath::IsFinite(Slot.RodDurability) && Slot.RodDurability > 0.0)
				{
					GrantedSlot = &Slot;
					break;
				}
				if (!GrantedSlot)
				{
					GrantedSlot = &Slot;
				}
			}
		}
		if (!GrantedSlot)
		{
			return;
		}
		const FCatInventoryItemUseRecord* ActiveSelectedRod =
			FindInventoryItemUseRecord(Snapshot.RodItemInstanceId);
		const bool bSelectedRodIsInUse = ActiveSelectedRod && !ActiveSelectedRod->bReleased;
		const bool bSelectedRodUnavailable = Snapshot.RodDefinitionId.IsNone()
			|| (GetInventoryItemQuantity(Snapshot.RodDefinitionId) <= 0 && !bSelectedRodIsInUse);
		const bool bSelectedRodNeedsReplacement = Snapshot.RodDefinitionId == DefinitionId
			&& (Snapshot.bRodBroken || !FMath::IsFinite(Snapshot.RodDurability) || Snapshot.RodDurability <= 0.0);
		if (bSelectedRodUnavailable || bSelectedRodNeedsReplacement)
		{
			Snapshot.RodDefinitionId = DefinitionId;
			Snapshot.RodItemInstanceId = GrantedSlot->ItemInstanceId;
			Snapshot.RodDurability = GrantedSlot->RodDurability;
			Snapshot.bRodBroken = GrantedSlot->bRodBroken;
		}
		return;
	}
	if (Definition.Kind == ECatEquipmentKind::Bait)
	{
		if (Snapshot.BaitDefinitionId.IsNone() || GetInventoryItemQuantity(Snapshot.BaitDefinitionId) <= 0)
		{
			const FCatRunInventorySlot* GrantedSlot = FindFirstInventorySlotByDefinition(DefinitionId);
			Snapshot.BaitDefinitionId = DefinitionId;
			Snapshot.BaitItemInstanceId = GrantedSlot ? GrantedSlot->ItemInstanceId : FGuid();
		}
		return;
	}
	if (Definition.Kind == ECatEquipmentKind::Float)
	{
		if (Snapshot.FloatDefinitionId.IsNone() || GetInventoryItemQuantity(Snapshot.FloatDefinitionId) <= 0)
		{
			const FCatRunInventorySlot* GrantedSlot = FindFirstInventorySlotByDefinition(DefinitionId);
			Snapshot.FloatDefinitionId = DefinitionId;
			Snapshot.FloatItemInstanceId = GrantedSlot ? GrantedSlot->ItemInstanceId : FGuid();
		}
		return;
	}
	if (Definition.Kind == ECatEquipmentKind::ScoopNet)
	{
		if (Snapshot.ScoopNetDefinitionId.IsNone() || GetInventoryItemQuantity(Snapshot.ScoopNetDefinitionId) <= 0)
		{
			const FCatRunInventorySlot* GrantedSlot = FindFirstInventorySlotByDefinition(DefinitionId);
			Snapshot.ScoopNetDefinitionId = DefinitionId;
			Snapshot.ScoopNetItemInstanceId = GrantedSlot ? GrantedSlot->ItemInstanceId : FGuid();
		}
	}
}

FCatFishingUseReservationResult UCatEquipmentComponent::MakeFishingUseReservationResult(const FGuid FishingSessionId,
	const ECatDomainCommandError Error, const bool bReserved, const FCatFishingUseRecord* Record) const
{
	FCatFishingUseReservationResult Result;
	Result.SessionId = FishingSessionId;
	Result.Error = Error;
	Result.EquipmentRevision = Snapshot.Revision;
	Result.RemainingRodDurability = Snapshot.RodDurability;
	Result.bReserved = bReserved;
	Result.bRodBroken = Snapshot.bRodBroken;
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
	Result.RemainingRodDurability = Snapshot.RodDurability;
	Result.bApplied = bApplied;
	Result.bRodBroken = Snapshot.bRodBroken;
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
