#include "Equipment/CatEquipmentComponent.h"

#include "Framework/Game/CatGameplayTypes.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Equipment/CatRunInventorySlotOperations.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

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
	const int64 ExpectedRevision)
{
	// 建立 Fishing 使用预留的流程：
	// 1. 先用 SessionId 返回已存在的终态，保证 FishingSession 重放不会再检查或再占库存。
	// 2. 再校验 authority、定义类型、Revision、当前钓鱼选择和三份实例身份，任何不一致都保持快照不变。
	// 3. 鱼竿实例必须来自活动 Use 记录，鱼饵和鱼漂实例必须仍在库存中，避免场景竿和背包格引用不同物品。
	// 4. 通过后立即把选中鱼饵实例的一份移进本 Session 记录并发布库存变化；之后玩家整理或转移背包不会破坏结算。
	// 5. 冻结本场鱼竿实例；耐久后续按增量直接写回该实例，不依赖当前选择。
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
	if (FloatSlot->Quantity <= 0)
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::CapacityExceeded, false);
	}
	if (RodUseRecord->Item.bRodBroken || !FMath::IsFinite(RodUseRecord->Item.RodDurability)
		|| RodUseRecord->Item.RodDurability <= 0.0)
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::InvalidPhase, false);
	}
	FCatRunInventorySlot ReservedBaitItem;
	if (!RemoveInventoryItemQuantityFromInstance(BaitItemInstanceId, 1, ReservedBaitItem))
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::CapacityExceeded, false);
	}

	FCatFishingUseRecord Record;
	Record.RodItemInstanceId = RodItemInstanceId;
	Record.RodDefinitionId = RodDefinitionId;
	Record.ReservedBaitDefinitionId = ReservedBaitItem.DefinitionId;
	Record.bBaitQuantityReserved = true;
	FishingUseRecords.Add(FishingSessionId, Record);
	++Snapshot.Revision;
	PublishSnapshot();
	UE_LOG(LogCatEquipment, Log,
		TEXT("Event=equipment_rod_session_bound SessionId=%s RodItemInstanceId=%s Definition=%s Durability=%.3f Revision=%lld World=%s NetMode=%d Authority=true Owner=%s"),
		*FishingSessionId.ToString(), *RodItemInstanceId.ToString(), *RodDefinitionId.ToString(),
		RodUseRecord->Item.RodDurability, Snapshot.Revision, *GetNameSafe(GetWorld()),
		static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone), *GetNameSafe(GetOwner()));
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
	FCatFishingUseRecord* Record = FindFishingUseRecord(FishingSessionId);
	const auto Reject = [&](const ECatDomainCommandError Error, const TCHAR* Reason)
	{
		UE_LOG(LogCatEquipment, Warning,
			TEXT("Event=equipment_rod_wear_rejected SessionId=%s RodItemInstanceId=%s WearSequence=%lld AbsoluteWear=%.3f Reason=%s Error=%s World=%s NetMode=%d Authority=%s Owner=%s"),
			*FishingSessionId.ToString(), Record ? *Record->RodItemInstanceId.ToString() : TEXT("None"),
			WearSequence, AbsoluteTotal, Reason, *UEnum::GetValueAsString(Error), *GetNameSafe(GetWorld()),
			static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone), GetOwner() && GetOwner()->HasAuthority() ? TEXT("true") : TEXT("false"),
			*GetNameSafe(GetOwner()));
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
		UE_LOG(LogCatEquipment, Log,
			TEXT("Event=equipment_rod_wear_applied SessionId=%s RodItemInstanceId=%s WearSequence=%lld AbsoluteWear=%.3f Delta=%.3f DurabilityBefore=%.3f Durability=%.3f Broken=%s Revision=%lld World=%s NetMode=%d Authority=true Owner=%s"),
			*FishingSessionId.ToString(), *Record->RodItemInstanceId.ToString(), WearSequence, AbsoluteTotal,
			Delta, Before, Remaining, bBroken ? TEXT("true") : TEXT("false"), Snapshot.Revision,
			*GetNameSafe(GetWorld()), static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone), *GetNameSafe(GetOwner()));
	}
	return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::None, true, Record);
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

FCatFishingUseOperationResult UCatEquipmentComponent::ReleaseFishingUse(const FGuid FishingSessionId)
{
	// Fishing 使用释放流程：
	// 1. 先按 SessionId 找到 Begin 留下的短生命周期记录；旧会话和重复释放只返回稳定终态。
	// 2. 如果饵料还没确认消耗，就把这一份按 DefinitionId 作为数量物品归还到随身库存，背包已满时追加返还格。
	// 3. 归还后显式修正同定义空选择，再关闭记录；已确认消耗的会话只关闭记录，不再碰库存。
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
		const UCatEquipmentDefinition* Bait = Settings
			? Settings->FindRuntimeDefinition(Record->ReservedBaitDefinitionId) : nullptr;
		if (!Bait || Bait->Kind != ECatEquipmentKind::Bait || !Bait->bRunConsumable
			|| GetInventoryStackLimit(*Bait) <= 0)
		{
			return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::DependencyUnavailable,
				false, Record);
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
		++Snapshot.Revision;
		PublishSnapshot();
	}
	Record->bReleased = true;
	double RemainingDurability = 0.0;
	bool bRodBroken = false;
	const bool bRodAvailable = GetFishingRodDurability(FishingSessionId, RemainingDurability, bRodBroken);
	UE_LOG(LogCatEquipment, Log,
		TEXT("Event=equipment_rod_session_released SessionId=%s RodItemInstanceId=%s WearSequence=%lld AbsoluteWear=%.3f Durability=%.3f Broken=%s RodAvailable=%s World=%s NetMode=%d Authority=%s Owner=%s"),
		*FishingSessionId.ToString(), *Record->RodItemInstanceId.ToString(), Record->LastWearSequence,
		Record->AbsoluteRodWear, RemainingDurability, bRodBroken ? TEXT("true") : TEXT("false"),
		bRodAvailable ? TEXT("true") : TEXT("false"), *GetNameSafe(GetWorld()), static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone),
		GetOwner() && GetOwner()->HasAuthority() ? TEXT("true") : TEXT("false"), *GetNameSafe(GetOwner()));
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
	const int32 DurabilityBand = FMath::IsFinite(Snapshot.RodDurability)
		? FMath::FloorToInt(Snapshot.RodDurability / 5.0) : INDEX_NONE;
	if (Snapshot.RodItemInstanceId.IsValid()
		&& (LastLoggedRodInstanceId != Snapshot.RodItemInstanceId
			|| LastLoggedRodDurabilityBand != DurabilityBand || bLastLoggedRodBroken != Snapshot.bRodBroken))
	{
		UE_LOG(LogCatEquipment, Log,
			TEXT("Event=equipment_rod_durability_replicated RodItemInstanceId=%s Durability=%.3f Broken=%s Revision=%lld World=%s NetMode=%d Authority=%s LocalRole=%d Owner=%s"),
			*Snapshot.RodItemInstanceId.ToString(), Snapshot.RodDurability,
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
