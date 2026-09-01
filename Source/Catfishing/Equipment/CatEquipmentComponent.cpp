#include "Equipment/CatEquipmentComponent.h"

#include "Framework/Game/CatGameplayTypes.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Equipment/CatRunInventorySlotOperations.h"
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
// 1. 先拒绝并发 Fishing 或数量型物品使用事务，再用 RequestId 返回既有终态；已部署的物品因不在库存中自然不会被重新选中。
// 2. 每次提交都必须通过服务器目录、authority、Revision、定义类别、消耗属性和 Profile 解锁证明。
// 3. 鱼竿、鱼饵、鱼漂和可选抄网必须都已经存在于同一份随身库存；新 UI 给实例 ID 时精确选择，旧 UI 只给定义 ID 时服务器会解析到具体实例。
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
	if (HasActiveFishingUse() || HasActiveRunConsumableUse())
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
		Result.Revision = Snapshot.Revision;
		return Result;
	}
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
		const FCatRunInventorySlot* RodSlotBeforeNormalize = ResolveSelectedInventorySlot(RodDefinitionId,
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
			const FCatRunInventorySlot* RodSlot = ResolveSelectedInventorySlot(RodDefinitionId, RodItemInstanceId);
			const FCatRunInventorySlot* BaitSlot = ResolveSelectedInventorySlot(BaitDefinitionId, BaitItemInstanceId);
			const FCatRunInventorySlot* FloatSlot = ResolveSelectedInventorySlot(FloatDefinitionId, FloatItemInstanceId);
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
	Result.Revision = Snapshot.Revision;
	TerminalCache.Add(Key, Result);
	return Result;
}

// 数量型库存授予预检流程：
// 1. 先从目录读取正式定义，并确认 RequestId、authority、定义类型和授予数量都成立；失败时不读取或补写库存格。
// 2. 已经缓存过同 RequestId 的授予结果时放行重放，让商店重试能拿回原回执而不是被当前容量误拦。
// 3. 正在执行其他数量型物品使用事务时拒绝新授予，避免同一份数量和同一 Revision 被两个事务同时解释。
// 4. 最后用统一库存格容量做只读预检；这里不扩容数组、不合并数量，只回答整批物品能否一次性交付。
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
	if (HasActiveRunConsumableUse())
	{
		return ECatDomainCommandError::InvalidPhase;
	}
	if (!CanStoreInventoryItem(*Definition, DefinitionId, Quantity))
	{
		return ECatDomainCommandError::CapacityExceeded;
	}
	return ECatDomainCommandError::None;
}

// 数量型库存物品入库流程：
// 1. 先拒绝无效 RequestId，并用 RequestId、定义和数量签名保护终态重放；载荷漂移直接拒绝且不改库存。
// 2. 首次提交复用商店扣款前预检；定义无效、数量无效、并发占用或容量不足都会保持 Snapshot 不变。
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

// 商店装备入库预检流程：
// 1. 先按 RequestId 和定义 ID 查询既有终态载荷，合法重放放行，载荷漂移拒绝。
// 2. 再确认当前组件属于 authority 角色，并且没有 Fishing 或耗材预留正在占用随身库存快照。
// 3. 最后只接受可用于钓鱼选择的非数量型定义；鱼饵、窝料、草药等数量型物品继续走数量型入库入口。
// 4. 装备型物品也占用同一份随身库存格容量，超出当前库存上限时必须在商店扣款前返回 CapacityExceeded。
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
	if (HasActiveFishingUse() || HasActiveRunConsumableUse())
	{
		return ECatDomainCommandError::InvalidPhase;
	}
	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	const UCatEquipmentDefinition* Definition = Settings->FindRuntimeDefinition(DefinitionId);
	if (!Definition || Definition->bRunConsumable)
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	if (Definition->Kind != ECatEquipmentKind::Rod && Definition->Kind != ECatEquipmentKind::Float
		&& Definition->Kind != ECatEquipmentKind::ScoopNet)
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	if (!CanStoreInventoryItem(*Definition, DefinitionId, 1))
	{
		return ECatDomainCommandError::CapacityExceeded;
	}
	return ECatDomainCommandError::None;
}

// 商店装备入库流程：
// 1. 先用 RequestId 和定义 ID 找终态缓存；合法重放只返回首次结果，不重复增加库存数量或推进 Revision。
// 2. 首次提交复用扣款前预检同一套准入规则，并用 ExpectedRevision 防止陈旧 UI 覆盖较新的本人库存。
// 3. 把装备型定义加入随身库存格数组，并按空选择、无库存旧选择或已断/耐久非法同定义竿的规则修正当前选择。
// 4. 成功后发布完整快照；UI 从库存格展示鱼竿/鱼漂/抄网，不再生成单独装备栏格子。
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
	const FGuid ItemInstanceId)
{
	// 物品使用流程：
	// 1. 先校验 authority、RequestId、Revision 和并发阶段，失败时不触碰库存数组。
	// 2. 再按实例 ID 找到库存格并读取定义；当前只有 Rod 有部署实现，其他物品保持 no-op 终态。
	// 3. 成功的 Rod Use 会把整份实例从库存移到活动记录，Snapshot 仍保留当前选择和实例状态供 UI/钓鱼命令读取。
	// 4. 发布后的调用方才允许生成世界 Actor；若后续生成失败，必须调用 UnUse 把同一实例放回库存。
	FCatInventoryItemUseResult Result;
	Result.RequestId = RequestId;
	Result.EquipmentRevision = Snapshot.Revision;
	if (!GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid() || !ItemInstanceId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	if (Snapshot.Revision != ExpectedRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
		return Result;
	}
	if (HasActiveFishingUse() || HasActiveInventoryItemUse() || HasActiveRunConsumableUse())
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
		return Result;
	}
	if (const FCatInventoryItemUseRecord* ExistingRecord = FindInventoryItemUseRecord(ItemInstanceId);
		ExistingRecord && !ExistingRecord->bReleased)
	{
		Result.Item = ExistingRecord->Item;
		Result.Error = ECatDomainCommandError::InvalidPhase;
		return Result;
	}
	NormalizeInventorySlots();
	const FCatRunInventorySlot* SourceSlot = FindInventorySlotByInstanceId(ItemInstanceId);
	const UCatEquipmentDefinition* Definition = SourceSlot
		? GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(SourceSlot->DefinitionId) : nullptr;
	if (!SourceSlot || !Definition)
	{
		Result.Error = ECatDomainCommandError::NotFound;
		return Result;
	}
	if (Definition->Kind != ECatEquipmentKind::Rod)
	{
		Result.Item = *SourceSlot;
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	if (SourceSlot->bRodBroken || !FMath::IsFinite(SourceSlot->RodDurability)
		|| SourceSlot->RodDurability <= 0.0)
	{
		Result.Item = *SourceSlot;
		Result.Error = ECatDomainCommandError::InvalidPhase;
		return Result;
	}

	FCatRunInventorySlot UsedItem;
	if (!RemoveInventoryItemInstance(ItemInstanceId, UsedItem))
	{
		Result.Error = ECatDomainCommandError::NotFound;
		return Result;
	}
	FCatInventoryItemUseRecord Record;
	Record.ItemInstanceId = UsedItem.ItemInstanceId;
	Record.Item = UsedItem;
	InventoryItemUseRecords.Add(UsedItem.ItemInstanceId, Record);
	Snapshot.RodDefinitionId = UsedItem.DefinitionId;
	Snapshot.RodItemInstanceId = UsedItem.ItemInstanceId;
	Snapshot.RodDurability = UsedItem.RodDurability;
	Snapshot.bRodBroken = UsedItem.bRodBroken;
	++Snapshot.Revision;
	InventoryItemUseRecords.FindChecked(UsedItem.ItemInstanceId).UseRevision = Snapshot.Revision;
	PublishSnapshot();
	Result.Item = UsedItem;
	Result.EquipmentRevision = Snapshot.Revision;
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	return Result;
}

FCatInventoryItemUseResult UCatEquipmentComponent::UnUse(const FGuid RequestId, const FGuid ItemInstanceId)
{
	// 物品停止使用流程：
	// 1. 先找到活动 Use 记录；找不到说明调用方没有同一实例的使用权，不能按 DefinitionId 重新造物品。
	// 2. 收口前预检背包是否能原样放回该实例；容量不足时保持场景 Actor 和活动记录不变。
	// 3. 放回成功后才释放活动记录、同步鱼竿选择状态并发布库存快照，调用方随后可以隐藏或销毁世界 Actor。
	FCatInventoryItemUseResult Result;
	Result.RequestId = RequestId;
	Result.EquipmentRevision = Snapshot.Revision;
	if (!GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid() || !ItemInstanceId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	FCatInventoryItemUseRecord* Record = FindInventoryItemUseRecord(ItemInstanceId);
	if (!Record)
	{
		Result.Error = ECatDomainCommandError::NotFound;
		return Result;
	}
	Result.Item = Record->Item;
	if (Record->bReleased)
	{
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	const UCatEquipmentDefinition* Definition =
		GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(Record->Item.DefinitionId);
	if (!Definition)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	FCatRunInventorySlot RestoredItem = Record->Item;
	CatRunInventorySlotOperations::NormalizeStoredItemSlot(RestoredItem, *Definition);
	if (!CanStoreInventorySlot(*Definition, RestoredItem) || !AddInventoryItemSlot(*Definition, RestoredItem))
	{
		Result.Error = ECatDomainCommandError::CapacityExceeded;
		return Result;
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
	return Result;
}

// 库存整理流程：
// 1. 先用 RequestId、Revision 和源/目标下标查询终态缓存，合法重放必须返回首次结果，不受当前 Fishing 或消耗阶段影响。
// 2. 首次请求再检查当前阶段 gate，避免整理库存和正在提交的库存消耗同时改同一份数组。
// 3. 服务器重读当前库存格数组，源格必须有物品，目标格移动/合并/交换复用运行库存格通用规则。
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
	if (HasActiveFishingUse() || HasActiveRunConsumableUse())
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
		Result.Revision = Snapshot.Revision;
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

// 数量型库存扣除流程：按 RequestId 重放并验证 authority、Revision、定义标签与正库存；成功只扣一份并发布 Revision，上层效果必须在结果成功后执行。
FCatDomainCommandResult UCatEquipmentComponent::ConsumeInventoryQuantityFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const FName DefinitionId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (HasActiveRunConsumableUse())
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
		Result.Revision = Snapshot.Revision;
		return Result;
	}
	const FString Key = MakeTerminalKey(TEXT("ConsumeInventoryQuantity"), RequestId);
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		Result = *Cached;
		Result.bCommitted = false;
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	UCatEquipmentDefinition* Definition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(DefinitionId);
	if (!GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid() || !Definition || !Definition->bRunConsumable)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (Snapshot.Revision != ExpectedRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
	}
	else if (GetInventoryItemQuantity(DefinitionId) <= GetPendingReservedFishingBaitCount(DefinitionId)
		+ GetPendingReservedRunConsumableCount(DefinitionId))
	{
		Result.Error = ECatDomainCommandError::CapacityExceeded;
	}
	else if (GetInventoryItemQuantity(DefinitionId) <= 0)
	{
		Result.Error = ECatDomainCommandError::CapacityExceeded;
	}
	else
	{
		if (RemoveInventoryItemQuantity(DefinitionId, 1))
		{
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
	return Result;
}

// 失败预算流程：先重放完整终态并校验 authority/Revision；None 不写物资，丢饵只扣特殊饵一份，伤竿只扣显式耐久并可断竿，单次绝不执行两个分支。
FCatFishingFailureResult UCatEquipmentComponent::CommitFishingFailure(const FGuid RequestId,
	const int64 ExpectedRevision, const ECatFishingFailurePenalty Penalty)
{
	FCatFishingFailureResult Result;
	Result.Command.RequestId = RequestId;
	if (HasActiveFishingUse() || HasActiveRunConsumableUse())
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
		if (!Bait || !Bait->bSpecialBait || GetInventoryItemQuantity(Snapshot.BaitDefinitionId) <= 0)
		{
			Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
		}
		else
		{
			if (RemoveInventoryItemQuantity(Snapshot.BaitDefinitionId, 1))
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
	const FName RodDefinitionId, const FName BaitDefinitionId, const FName FloatDefinitionId,
	const int64 ExpectedRevision)
{
	// 兼容入口流程：旧调用方仍只知道定义 ID，这里只补当前选择实例 ID 并交给正式 Use-aware 入口，不再复制库存可用性判断。
	return BeginFishingUse(FishingSessionId, Snapshot.RodItemInstanceId, RodDefinitionId, BaitDefinitionId,
		FloatDefinitionId, ExpectedRevision);
}

FCatFishingUseReservationResult UCatEquipmentComponent::BeginFishingUse(const FGuid FishingSessionId,
	const FGuid RodItemInstanceId, const FName RodDefinitionId, const FName BaitDefinitionId,
	const FName FloatDefinitionId, const int64 ExpectedRevision)
{
	// 建立 Fishing 使用预留的流程：
	// 1. 先用 SessionId 返回已存在的终态，保证 FishingSession 重放不会再检查或再占库存。
	// 2. 再校验 authority、定义类型、Revision、当前钓鱼选择和鱼竿耐久，任何不一致都保持快照不变。
	// 3. 接着拒绝 RunConsumable 并发；Fishing 自身按 SessionId 并行预留，库存可用量统一扣除所有待提交预留。
	// 4. 鱼竿优先由正在操作的 Actor 提供实例身份；旧调用缺失时回退到当前定义可用实例，鱼饵数量扣除其他 Fishing 预留后仍至少剩一份。
	// 5. 最后只写入本 Session 预留记录，不递增 Revision；真正的库存变化留到 Commit 阶段发布。
	if (const FCatFishingUseRecord* ExistingRecord = FindFishingUseRecord(FishingSessionId))
	{
		const bool bReserved = ExistingRecord->bBaitQuantityReserved && !ExistingRecord->bBaitCommitted
			&& !ExistingRecord->bReleased;
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::AlreadyResolved, bReserved,
			bReserved ? ExistingRecord : nullptr);
	}
	if (HasActiveRunConsumableUse())
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::InvalidPhase, false);
	}

	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	UCatEquipmentDefinition* Rod = Settings->FindRuntimeDefinition(RodDefinitionId);
	UCatEquipmentDefinition* Bait = Settings->FindRuntimeDefinition(BaitDefinitionId);
	UCatEquipmentDefinition* Float = Settings->FindRuntimeDefinition(FloatDefinitionId);
	FGuid ResolvedRodItemInstanceId = RodItemInstanceId;
	if (!ResolvedRodItemInstanceId.IsValid())
	{
		if (const FCatRunInventorySlot* FallbackRodSlot = FindFirstInventorySlotByDefinition(RodDefinitionId))
		{
			ResolvedRodItemInstanceId = FallbackRodSlot->ItemInstanceId;
		}
	}
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::DependencyUnavailable, false);
	}
	if (!FishingSessionId.IsValid() || !ResolvedRodItemInstanceId.IsValid() || RodDefinitionId.IsNone()
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
	if (Snapshot.RodDefinitionId != RodDefinitionId
		|| (Snapshot.RodItemInstanceId.IsValid() && Snapshot.RodItemInstanceId != ResolvedRodItemInstanceId)
		|| Snapshot.BaitDefinitionId != BaitDefinitionId || Snapshot.FloatDefinitionId != FloatDefinitionId)
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::InvalidPayload, false);
	}
	if (Snapshot.bRodBroken || !FMath::IsFinite(Snapshot.RodDurability) || Snapshot.RodDurability <= 0.0)
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::InvalidPhase, false);
	}
	// 鱼饵是否扣数量由 RunConsumable 决定；SpecialBait 只保留偏好、失败惩罚等玩法语义，不能再绕过库存真相。
	if (!Bait->bRunConsumable)
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::InvalidPayload, false);
	}
	if (GetInventoryItemQuantity(FloatDefinitionId) <= 0
		|| GetInventoryItemQuantity(BaitDefinitionId) <= GetPendingReservedFishingBaitCount(BaitDefinitionId))
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::CapacityExceeded, false);
	}

	FCatFishingUseRecord Record;
	Record.SessionId = FishingSessionId;
	Record.RodItemInstanceId = ResolvedRodItemInstanceId;
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
	// 4. Begin 已经保护了一份普通或特殊鱼饵，这里只消费那一份并递增快照 Revision。
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
	// Begin 已经为这场 Fishing 保护一份饵；Commit 只消费这份受保护数量，重放不会再次扣库存。
	if (Record->bBaitQuantityReserved)
	{
		if (GetInventoryItemQuantity(Record->BaitDefinitionId) <= 0)
		{
			return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::CapacityExceeded, false, Record);
		}
		if (RemoveInventoryItemQuantity(Record->BaitDefinitionId, 1))
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

FCatRunConsumableUseResult UCatEquipmentComponent::BeginRunConsumableUse(const FGuid OperationId,
	const FName DefinitionId, const int32 Quantity, const int64 ExpectedRevision)
{
	// 建立通用一局耗材预留的流程：
	// 1. 先按 OperationId 返回已有记录，保证草药、窝料或道具使用的网络重放不重复占库存。
	// 2. 再校验 authority、请求参数和 Revision；失败时不写入任何预留状态。
	// 3. 接着拒绝与 Fishing 或其他 RunConsumable 操作并发，避免两条提交链同时改同一份库存格数组。
	// 4. 可用数量必须减去 Fishing 已保护但尚未提交的鱼饵份数，这样普通饵也不会被 RunConsumable 入口双花。
	// 5. 最后写入预留记录并保持快照不发布，实际扣减由 Commit/Publish 阶段完成。
	if (const FCatRunConsumableUseRecord* Existing = RunConsumableUseRecords.Find(OperationId))
	{
		return MakeRunConsumableUseResult(OperationId, ECatDomainCommandError::AlreadyResolved, Existing);
	}
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return MakeRunConsumableUseResult(OperationId, ECatDomainCommandError::DependencyUnavailable);
	}
	if (!OperationId.IsValid() || DefinitionId.IsNone() || Quantity <= 0)
	{
		return MakeRunConsumableUseResult(OperationId, ECatDomainCommandError::InvalidPayload);
	}
	if (Snapshot.Revision != ExpectedRevision)
	{
		return MakeRunConsumableUseResult(OperationId, ECatDomainCommandError::RevisionConflict);
	}
	if (HasActiveFishingUse() || HasActiveRunConsumableUse())
	{
		return MakeRunConsumableUseResult(OperationId, ECatDomainCommandError::InvalidPhase);
	}
	UCatEquipmentDefinition* Definition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(DefinitionId);
	if (!Definition || !Definition->bRunConsumable)
	{
		return MakeRunConsumableUseResult(OperationId, ECatDomainCommandError::InvalidPayload);
	}
	if (GetInventoryItemQuantity(DefinitionId) - GetPendingReservedFishingBaitCount(DefinitionId) < Quantity)
	{
		return MakeRunConsumableUseResult(OperationId, ECatDomainCommandError::CapacityExceeded);
	}
	FCatRunConsumableUseRecord Record;
	Record.OperationId = OperationId;
	Record.DefinitionId = DefinitionId;
	Record.Quantity = Quantity;
	Record.ReservationRevision = Snapshot.Revision;
	Record.ResultRevision = Snapshot.Revision;
	RunConsumableUseRecords.Add(OperationId, Record);
	ActiveRunConsumableUseOperationId = OperationId;
	return MakeRunConsumableUseResult(OperationId, ECatDomainCommandError::None,
		RunConsumableUseRecords.Find(OperationId));
}

FCatRunConsumableUseResult UCatEquipmentComponent::CommitRunConsumableUse(const FGuid OperationId)
{
	FCatRunConsumableUseResult Result = CommitRunConsumableUseDeferred(OperationId);
	if (Result.bCommitted)
	{
		PublishDeferredRunConsumableUse(OperationId);
	}
	return Result;
}

// 通用消耗提交流程：
// 1. 先找 Begin 阶段留下的预留记录；没有记录说明调用方没有拿到库存使用权，直接返回 NotFound。
// 2. 已经提交或释放的记录只返回幂等终态，不再次扣库存，也不重新抢占 Active 操作。
// 3. 当前 Active Operation 必须仍是这条记录，防止迟到提交越过新的使用事务。
// 4. 扣减前再次按库存格数组复核总量，再调用统一扣格子逻辑；失败时保持记录和 Revision 不变。
// 5. 扣减成功只写结果 Revision 和 committed 标记，是否广播完整快照由 PublishDeferredRunConsumableUse 控制。
FCatRunConsumableUseResult UCatEquipmentComponent::CommitRunConsumableUseDeferred(const FGuid OperationId)
{
	FCatRunConsumableUseRecord* Record = RunConsumableUseRecords.Find(OperationId);
	if (!Record)
	{
		return MakeRunConsumableUseResult(OperationId, ECatDomainCommandError::NotFound);
	}
	if (Record->bCommitted || Record->bReleased)
	{
		return MakeRunConsumableUseResult(OperationId, ECatDomainCommandError::AlreadyResolved, Record);
	}
	if (ActiveRunConsumableUseOperationId != OperationId)
	{
		return MakeRunConsumableUseResult(OperationId, ECatDomainCommandError::InvalidPhase, Record);
	}
	if (GetInventoryItemQuantity(Record->DefinitionId) < Record->Quantity)
	{
		return MakeRunConsumableUseResult(OperationId, ECatDomainCommandError::CapacityExceeded, Record);
	}
	if (!RemoveInventoryItemQuantity(Record->DefinitionId, Record->Quantity))
	{
		return MakeRunConsumableUseResult(OperationId, ECatDomainCommandError::CapacityExceeded, Record);
	}
	++Snapshot.Revision;
	Record->ResultRevision = Snapshot.Revision;
	Record->bCommitted = true;
	return MakeRunConsumableUseResult(OperationId, ECatDomainCommandError::None, Record);
}

void UCatEquipmentComponent::PublishDeferredRunConsumableUse(const FGuid OperationId)
{
	FCatRunConsumableUseRecord* Record = RunConsumableUseRecords.Find(OperationId);
	if (!Record || !Record->bCommitted || Record->bReleased || Record->bCommitPublished)
	{
		return;
	}
	Record->bCommitPublished = true;
	if (ActiveRunConsumableUseOperationId == OperationId)
	{
		ActiveRunConsumableUseOperationId.Invalidate();
	}
	PublishSnapshot();
}

FCatRunConsumableUseResult UCatEquipmentComponent::ReleaseRunConsumableUse(const FGuid OperationId)
{
	FCatRunConsumableUseRecord* Record = RunConsumableUseRecords.Find(OperationId);
	if (!Record)
	{
		return MakeRunConsumableUseResult(OperationId, ECatDomainCommandError::NotFound);
	}
	if (Record->bCommitted || Record->bReleased)
	{
		return MakeRunConsumableUseResult(OperationId, ECatDomainCommandError::AlreadyResolved, Record);
	}
	if (ActiveRunConsumableUseOperationId != OperationId)
	{
		return MakeRunConsumableUseResult(OperationId, ECatDomainCommandError::InvalidPhase, Record);
	}
	Record->bReleased = true;
	Record->ResultRevision = Record->ReservationRevision;
	ActiveRunConsumableUseOperationId.Invalidate();
	return MakeRunConsumableUseResult(OperationId, ECatDomainCommandError::None, Record);
}

bool UCatEquipmentComponent::HasActiveRunConsumableUse() const
{
	const FCatRunConsumableUseRecord* Record = RunConsumableUseRecords.Find(ActiveRunConsumableUseOperationId);
	return ActiveRunConsumableUseOperationId.IsValid() && Record && !Record->bReleased
		&& (!Record->bCommitted || !Record->bCommitPublished);
}

// 维修流程：验证固定营地事实、Revision、当前 Rod/浮木定义和库存；鱼竿正在 Use 时拒绝，成功只扣一份浮木并恢复同一实例耐久。
FCatDomainCommandResult UCatEquipmentComponent::RepairRodAtCamp(const FGuid RequestId, const int64 ExpectedRevision,
	const bool bAtCamp)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (HasActiveFishingUse() || HasActiveInventoryItemUse() || HasActiveRunConsumableUse())
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
	if (!bAtCamp || !GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid() || !Rod || !Driftwood
		|| Driftwood->Kind != ECatEquipmentKind::Driftwood
		|| GetInventoryItemQuantity(Settings->DriftwoodDefinitionId) <= 0)
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
	}
	else if (Snapshot.Revision != ExpectedRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
	}
	else
	{
		if (RemoveInventoryItemQuantity(Settings->DriftwoodDefinitionId, 1))
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
	// 1. 先拒绝空实例、非法数量和超过堆叠上限的载荷，保证 UnUse 不会放回一份坏状态。
	// 2. 数量型物品只允许合并回同一个 ItemInstanceId 的堆栈，避免同定义不同实例被揉到一起。
	// 3. 合并空间不足时再检查配置容量和现有空格；这里只读判断，不提前改变库存数组。
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
	for (const FCatRunInventorySlot& Slot : Snapshot.InventorySlots)
	{
		if (Definition.bRunConsumable && Slot.ItemInstanceId == Item.ItemInstanceId
			&& Slot.DefinitionId == Item.DefinitionId && Slot.Quantity > 0 && Slot.Quantity < StackLimit)
		{
			Remaining -= FMath::Min(Remaining, StackLimit - Slot.Quantity);
			if (Remaining <= 0)
			{
				return true;
			}
		}
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
	// 2. 首次提交只接受 authority、正确 Revision 和可运行定义；数量型与装备型都走完整实例入库，不重新生成 ItemInstanceId。
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
	else if (HasActiveFishingUse() || HasActiveRunConsumableUse())
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
		else if (!Definition->bRunConsumable && Definition->Kind != ECatEquipmentKind::Rod
			&& Definition->Kind != ECatEquipmentKind::Float && Definition->Kind != ECatEquipmentKind::ScoopNet)
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

// 库存扣减流程：先确认总量足够，再从靠前格子扣数量；格子清空后保留数组位置，并修正已选鱼饵实例避免选择指向空格。
bool UCatEquipmentComponent::RemoveInventoryItemQuantity(const FName DefinitionId, const int32 Quantity)
{
	if (DefinitionId.IsNone() || Quantity <= 0 || GetInventoryItemQuantity(DefinitionId) < Quantity)
	{
		return false;
	}
	int32 Remaining = Quantity;
	for (FCatRunInventorySlot& Slot : Snapshot.InventorySlots)
	{
		if (Slot.DefinitionId != DefinitionId || Slot.Quantity <= 0)
		{
			continue;
		}
		const int32 Removed = FMath::Min(Remaining, Slot.Quantity);
		Slot.Quantity -= Removed;
		Remaining -= Removed;
		if (Slot.Quantity <= 0)
		{
			Slot = FCatRunInventorySlot();
		}
		if (Remaining <= 0)
		{
			if (Snapshot.BaitDefinitionId == DefinitionId
				&& !FindInventorySlotByInstanceId(Snapshot.BaitItemInstanceId))
			{
				const FCatRunInventorySlot* ReplacementBaitSlot = FindFirstInventorySlotByDefinition(DefinitionId);
				Snapshot.BaitItemInstanceId = ReplacementBaitSlot ? ReplacementBaitSlot->ItemInstanceId : FGuid();
			}
			return true;
		}
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
	// 活动物品使用 gate 流程：只要存在尚未 Released 的实例记录，就认为库存正被世界表现占用，选择和维修不能并行改它。
	for (const TPair<FGuid, FCatInventoryItemUseRecord>& Pair : InventoryItemUseRecords)
	{
		if (Pair.Key.IsValid() && !Pair.Value.bReleased)
		{
			return true;
		}
	}
	return false;
}

int32 UCatEquipmentComponent::GetPendingReservedFishingBaitCount(const FName DefinitionId) const
{
	int32 ReservedCount = 0;
	for (const TPair<FGuid, FCatFishingUseRecord>& Pair : FishingUseRecords)
	{
		const FCatFishingUseRecord& Record = Pair.Value;
		if (!Record.bReleased && Record.bBaitQuantityReserved && !Record.bBaitCommitted
			&& Record.BaitDefinitionId == DefinitionId)
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
	// 定义回退解析流程：
	// 1. 旧 UI 或兼容入口只给 DefinitionId 时，先在库存里记住第一份同定义实物。
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

int32 UCatEquipmentComponent::GetPendingReservedRunConsumableCount(const FName DefinitionId) const
{
	const FCatRunConsumableUseRecord* Record = RunConsumableUseRecords.Find(ActiveRunConsumableUseOperationId);
	return Record && !Record->bCommitted && !Record->bReleased && Record->DefinitionId == DefinitionId
		? Record->Quantity : 0;
}

FCatRunConsumableUseResult UCatEquipmentComponent::MakeRunConsumableUseResult(const FGuid OperationId,
	const ECatDomainCommandError Error, const FCatRunConsumableUseRecord* Record) const
{
	FCatRunConsumableUseResult Result;
	Result.OperationId = OperationId;
	Result.Error = Error;
	Result.EquipmentRevision = Snapshot.Revision;
	if (Record)
	{
		Result.DefinitionId = Record->DefinitionId;
		Result.Quantity = Record->Quantity;
		Result.EquipmentRevision = Record->ResultRevision;
		Result.bReserved = !Record->bCommitted && !Record->bReleased;
		Result.bCommitted = Record->bCommitted;
		Result.bReleased = Record->bReleased;
	}
	return Result;
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
