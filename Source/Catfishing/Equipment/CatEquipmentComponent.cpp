#include "Equipment/CatEquipmentComponent.h"

#include "Framework/Game/CatGameplayTypes.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
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
// 1. 先拒绝并发 Fishing/数量型物品使用事务，再用 RequestId 返回既有终态，避免重复选择刷新耐久。
// 2. 每次提交都必须通过服务器目录、authority、Revision、定义类别、消耗属性和 Profile 解锁证明。
// 3. 鱼竿、鱼饵、鱼漂和可选抄网必须都已经存在于同一份随身库存；这个入口只选择，不发放。
// 4. 同一套选择直接返回 AlreadyResolved；不同选择会切换当前钓鱼选择，只有鱼竿定义变化时才换用新竿耐久。
// 5. 成功时只写钓鱼选择和当前鱼竿耐久，并发布同一份随身库存快照。
FCatDomainCommandResult UCatEquipmentComponent::ConfigureLoadoutFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const FName RodDefinitionId, const FName BaitDefinitionId,
	const FName FloatDefinitionId, const FName ScoopNetDefinitionId, const FName RodSkinDefinitionId)
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
	else if (GetInventoryItemQuantity(RodDefinitionId) <= 0 || GetInventoryItemQuantity(BaitDefinitionId) <= 0
		|| GetInventoryItemQuantity(FloatDefinitionId) <= 0
		|| (!ScoopNetDefinitionId.IsNone() && GetInventoryItemQuantity(ScoopNetDefinitionId) <= 0))
	{
		Result.Error = ECatDomainCommandError::NotFound;
	}
	else
	{
		const bool bSameLoadout = Snapshot.RodDefinitionId == RodDefinitionId
			&& Snapshot.BaitDefinitionId == BaitDefinitionId && Snapshot.FloatDefinitionId == FloatDefinitionId
			&& Snapshot.ScoopNetDefinitionId == ScoopNetDefinitionId && Snapshot.RodSkinDefinitionId == RodSkinDefinitionId;
		if (bSameLoadout)
		{
			Result.Error = ECatDomainCommandError::AlreadyResolved;
			Result.Revision = Snapshot.Revision;
			TerminalCache.Add(Key, Result);
			return Result;
		}
		const bool bRodChanged = Snapshot.RodDefinitionId != RodDefinitionId;
		Snapshot.RodDefinitionId = RodDefinitionId;
		Snapshot.BaitDefinitionId = BaitDefinitionId;
		Snapshot.FloatDefinitionId = FloatDefinitionId;
		Snapshot.ScoopNetDefinitionId = ScoopNetDefinitionId;
		Snapshot.RodSkinDefinitionId = RodSkinDefinitionId;
		if (bRodChanged)
		{
			Snapshot.RodDurability = Rod->MaximumRodDurability;
			Snapshot.bRodBroken = false;
		}
		++Snapshot.Revision;
		PublishSnapshot();
		Result.bCommitted = true;
		Result.Error = ECatDomainCommandError::None;
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

// 库存整理流程：
// 1. 先用 RequestId、Revision 和源/目标下标查询终态缓存，合法重放必须返回首次结果，不受当前 Fishing 或消耗阶段影响。
// 2. 首次请求再检查当前阶段 gate，避免整理库存和正在提交的库存消耗同时改同一份数组。
// 3. 服务器重读当前库存格数组，源格必须有物品，目标格可以为空、同定义可合并，或不同定义可交换。
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
			FCatRunInventorySlot& SourceSlot = Snapshot.InventorySlots[SourceSlotIndex];
			FCatRunInventorySlot& TargetSlot = Snapshot.InventorySlots[TargetSlotIndex];
			if (SourceSlot.DefinitionId.IsNone() || SourceSlot.Quantity <= 0)
			{
				Result.Error = ECatDomainCommandError::NotFound;
			}
			else if (TargetSlot.DefinitionId.IsNone() || TargetSlot.Quantity <= 0)
			{
				TargetSlot = SourceSlot;
				SourceSlot = FCatRunInventorySlot();
				Result.bCommitted = true;
				Result.Error = ECatDomainCommandError::None;
			}
			else if (TargetSlot.DefinitionId == SourceSlot.DefinitionId)
			{
				const UCatEquipmentDefinition* Definition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(
					SourceSlot.DefinitionId);
				const int32 StackLimit = Definition ? GetInventoryStackLimit(*Definition) : 1;
				const int32 Room = FMath::Max(0, StackLimit - TargetSlot.Quantity);
				const int32 MovedQuantity = FMath::Min(Room, SourceSlot.Quantity);
				if (MovedQuantity > 0)
				{
					TargetSlot.Quantity += MovedQuantity;
					SourceSlot.Quantity -= MovedQuantity;
					if (SourceSlot.Quantity <= 0)
					{
						SourceSlot = FCatRunInventorySlot();
					}
					Result.bCommitted = true;
					Result.Error = ECatDomainCommandError::None;
				}
				else
				{
					Result.Error = ECatDomainCommandError::AlreadyResolved;
				}
			}
			else
			{
				const FCatRunInventorySlot PreviousTarget = TargetSlot;
				TargetSlot = SourceSlot;
				SourceSlot = PreviousTarget;
				Result.bCommitted = true;
				Result.Error = ECatDomainCommandError::None;
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
	const FName RodDefinitionId, const FName BaitDefinitionId, const FName FloatDefinitionId, const int64 ExpectedRevision)
{
	// 建立 Fishing 使用预留的流程：
	// 1. 先用 SessionId 返回已存在的终态，保证 FishingSession 重放不会再检查或再占库存。
	// 2. 再校验 authority、定义类型、Revision、当前钓鱼选择和鱼竿耐久，任何不一致都保持快照不变。
	// 3. 接着拒绝正在进行的 Fishing 或 RunConsumable 操作，让鱼饵库存同一时间只有一个写入意图。
	// 4. 鱼竿和鱼漂必须在随身库存中仍有实物；普通饵和特殊饵都必须声明为 RunConsumable，并且格子数量扣除其他 Fishing 预留后仍至少剩一份。
	// 5. 最后只写入预留记录和 Active Session，不递增 Revision；真正的库存变化留到 Commit 阶段发布。
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
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::DependencyUnavailable, false);
	}
	if (!FishingSessionId.IsValid() || RodDefinitionId.IsNone() || BaitDefinitionId.IsNone() || FloatDefinitionId.IsNone()
		|| !Rod || !Bait || !Float || Rod->Kind != ECatEquipmentKind::Rod || Bait->Kind != ECatEquipmentKind::Bait
		|| Float->Kind != ECatEquipmentKind::Float)
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::InvalidPayload, false);
	}
	if (Snapshot.Revision != ExpectedRevision)
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::RevisionConflict, false);
	}
	if (Snapshot.RodDefinitionId != RodDefinitionId || Snapshot.BaitDefinitionId != BaitDefinitionId
		|| Snapshot.FloatDefinitionId != FloatDefinitionId)
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::InvalidPayload, false);
	}
	if (Snapshot.bRodBroken || !FMath::IsFinite(Snapshot.RodDurability) || Snapshot.RodDurability <= 0.0)
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::InvalidPhase, false);
	}
	if (HasActiveFishingUse() || HasActiveRunConsumableUse())
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::AlreadyResolved, false);
	}
	// 鱼饵是否扣数量由 RunConsumable 决定；SpecialBait 只保留偏好、失败惩罚等玩法语义，不能再绕过库存真相。
	if (!Bait->bRunConsumable)
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::InvalidPayload, false);
	}
	if (GetInventoryItemQuantity(RodDefinitionId) <= 0 || GetInventoryItemQuantity(FloatDefinitionId) <= 0
		|| GetInventoryItemQuantity(BaitDefinitionId) <= GetPendingReservedFishingBaitCount(BaitDefinitionId))
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::CapacityExceeded, false);
	}

	FCatFishingUseRecord Record;
	Record.SessionId = FishingSessionId;
	Record.RodDefinitionId = RodDefinitionId;
	Record.BaitDefinitionId = BaitDefinitionId;
	Record.FloatDefinitionId = FloatDefinitionId;
	Record.ReservationRevision = Snapshot.Revision;
	Record.bBaitQuantityReserved = true;
	FishingUseRecords.Add(FishingSessionId, Record);
	ActiveFishingUseSessionId = FishingSessionId;
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
	// 3. 只有当前 Active Fishing Session 能提交，避免旧会话在新会话开始后补扣。
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
	Record->bWearCommitted = true;
	Record->bReleased = true;
	ActiveFishingUseSessionId.Invalidate();
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
	Record->bBreakCommitted = true;
	Record->bReleased = true;
	ActiveFishingUseSessionId.Invalidate();
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
	ActiveFishingUseSessionId.Invalidate();
	return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::None, true, Record);
}

bool UCatEquipmentComponent::HasActiveFishingUse() const
{
	return ActiveFishingUseSessionId.IsValid() && IsFishingUseActive(ActiveFishingUseSessionId);
}

bool UCatEquipmentComponent::IsFishingUseActive(const FGuid FishingSessionId) const
{
	const FCatFishingUseRecord* Record = FindFishingUseRecord(FishingSessionId);
	return FishingSessionId.IsValid() && ActiveFishingUseSessionId == FishingSessionId && Record && !Record->bReleased;
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

// 维修流程：验证固定营地事实、Revision、当前 Rod/浮木定义和库存；成功只扣一份浮木并恢复当前 Rod 最大耐久，不升级或替换装备。
FCatDomainCommandResult UCatEquipmentComponent::RepairRodAtCamp(const FGuid RequestId, const int64 ExpectedRevision,
	const bool bAtCamp)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (HasActiveFishingUse() || HasActiveRunConsumableUse())
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

// 单格堆叠读取流程：非数量型物品只能一格一件；数量型物品按配置限制，0 表示用最大整数作为“不限制单格堆叠”。
int32 UCatEquipmentComponent::GetInventoryStackLimit(const UCatEquipmentDefinition& Definition) const
{
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
		if (Slot.DefinitionId.IsNone() || Slot.Quantity <= 0)
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

// 入库写入流程：
// 1. 复用预检保证不会半写入；随后补齐配置容量内的空格。
// 2. 同定义未满格先合并，剩余数量再落到空格。
// 3. 写完只改变 InventorySlots；所有读者都从这份数组重新汇总自己需要的数量。
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
		if (Slot.DefinitionId.IsNone() || Slot.Quantity <= 0)
		{
			const int32 Added = FMath::Min(Remaining, StackLimit);
			Slot.DefinitionId = DefinitionId;
			Slot.Quantity = Added;
			Remaining -= Added;
			if (Remaining <= 0)
			{
				return true;
			}
		}
	}
	return false;
}

// 库存扣减流程：先确认总量足够，再从靠前格子扣数量；格子清空后保留数组位置，避免 UI 下标整体漂移。
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

// 自动选择流程：
// 1. 新获得的物品只在当前选择缺失、旧选择无库存，或同定义已选竿已断/耐久非法时介入。
// 2. 鱼竿选择刷新会重置当前耐久为这根新竿的最大耐久；鱼饵、鱼漂和抄网只写稳定 ID。
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
		const bool bSelectedRodUnavailable = Snapshot.RodDefinitionId.IsNone()
			|| GetInventoryItemQuantity(Snapshot.RodDefinitionId) <= 0;
		const bool bSelectedRodNeedsReplacement = Snapshot.RodDefinitionId == DefinitionId
			&& (Snapshot.bRodBroken || !FMath::IsFinite(Snapshot.RodDurability) || Snapshot.RodDurability <= 0.0);
		if (bSelectedRodUnavailable || bSelectedRodNeedsReplacement)
		{
			Snapshot.RodDefinitionId = DefinitionId;
			Snapshot.RodDurability = Definition.MaximumRodDurability;
			Snapshot.bRodBroken = false;
		}
		return;
	}
	if (Definition.Kind == ECatEquipmentKind::Bait)
	{
		if (Snapshot.BaitDefinitionId.IsNone() || GetInventoryItemQuantity(Snapshot.BaitDefinitionId) <= 0)
		{
			Snapshot.BaitDefinitionId = DefinitionId;
		}
		return;
	}
	if (Definition.Kind == ECatEquipmentKind::Float)
	{
		if (Snapshot.FloatDefinitionId.IsNone() || GetInventoryItemQuantity(Snapshot.FloatDefinitionId) <= 0)
		{
			Snapshot.FloatDefinitionId = DefinitionId;
		}
		return;
	}
	if (Definition.Kind == ECatEquipmentKind::ScoopNet)
	{
		if (Snapshot.ScoopNetDefinitionId.IsNone() || GetInventoryItemQuantity(Snapshot.ScoopNetDefinitionId) <= 0)
		{
			Snapshot.ScoopNetDefinitionId = DefinitionId;
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
