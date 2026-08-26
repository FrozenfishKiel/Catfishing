#include "Equipment/CatEquipmentComponent.h"

#include "Framework/Game/CatGameplayTypes.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

// 构造流程：开启组件复制并关闭 Tick；Snapshot 初始 Revision=0 表示未从 Profile 选择装配。
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

// Snapshot 读取流程：返回服务器真相或客户端最近复制值；不从 Profile/Items 拼接第二份装备事实。
const FCatEquipmentLoadoutSnapshot& UCatEquipmentComponent::GetSnapshot() const
{
	return Snapshot;
}

// 装配流程：按 RequestId 重放后验证 authority/Revision、三份正式定义类别和服务器解锁证明；只允许首次装配，重复同套不补耐久，换装策略未裁时 fail-closed。
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
		|| Float->Kind != ECatEquipmentKind::Float || (Scoop && Scoop->Kind != ECatEquipmentKind::ScoopNet))
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
	else if (!Snapshot.RodDefinitionId.IsNone() || !Snapshot.BaitDefinitionId.IsNone() || !Snapshot.FloatDefinitionId.IsNone())
	{
		// 同一套新 Request 只读取既有耐久，不能借 Configure 免费修竿；任何不同 ID 都属于尚未裁决的换装生命周期。
		const bool bSameLoadout = Snapshot.RodDefinitionId == RodDefinitionId
			&& Snapshot.BaitDefinitionId == BaitDefinitionId && Snapshot.FloatDefinitionId == FloatDefinitionId
			&& Snapshot.ScoopNetDefinitionId == ScoopNetDefinitionId && Snapshot.RodSkinDefinitionId == RodSkinDefinitionId;
		Result.Error = bSameLoadout ? ECatDomainCommandError::AlreadyResolved : ECatDomainCommandError::PolicyUndecided;
	}
	else
	{
		Snapshot.RodDefinitionId = RodDefinitionId;
		Snapshot.BaitDefinitionId = BaitDefinitionId;
		Snapshot.FloatDefinitionId = FloatDefinitionId;
		Snapshot.ScoopNetDefinitionId = ScoopNetDefinitionId;
		Snapshot.RodSkinDefinitionId = RodSkinDefinitionId;
		Snapshot.RodDurability = Rod->MaximumRodDurability;
		Snapshot.bRodBroken = false;
		++Snapshot.Revision;
		PublishSnapshot();
		Result.bCommitted = true;
		Result.Error = ECatDomainCommandError::None;
	}
	Result.Revision = Snapshot.Revision;
	TerminalCache.Add(Key, Result);
	return Result;
}

ECatDomainCommandError UCatEquipmentComponent::ValidateRunConsumableGrant(const FGuid RequestId,
	const FName DefinitionId, const int32 Quantity) const
{
	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	const UCatEquipmentDefinition* Definition = Settings->FindRuntimeDefinition(DefinitionId);
	if (!RequestId.IsValid() || !GetOwner() || !GetOwner()->HasAuthority() || !Definition
		|| !Definition->bRunConsumable || Quantity <= 0)
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	const FString Key = MakeTerminalKey(TEXT("GrantConsumable"), RequestId);
	if (TerminalCache.Contains(Key))
	{
		return ECatDomainCommandError::None;
	}
	if (HasActiveRunConsumableUse())
	{
		return ECatDomainCommandError::InvalidPhase;
	}
	const FCatRunConsumableStack* ExistingStack = Snapshot.Consumables.FindByPredicate(
		[DefinitionId](const FCatRunConsumableStack& Stack)
		{
			return Stack.DefinitionId == DefinitionId;
		});
	const int32 ExistingQuantity = ExistingStack ? ExistingStack->Quantity : 0;
	if (Settings->RunConsumableStackCapacity > 0
		&& ExistingQuantity + Quantity > Settings->RunConsumableStackCapacity)
	{
		return ECatDomainCommandError::CapacityExceeded;
	}
	return ECatDomainCommandError::None;
}

// 耗材授予流程：重放先核对载荷签名，再复用商店预检同一套准入规则；成功只增加一局数量和 Revision。
FCatDomainCommandResult UCatEquipmentComponent::GrantRunConsumableFromAuthority(const FGuid RequestId,
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
	const FString Key = MakeTerminalKey(TEXT("GrantConsumable"), RequestId);
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
	const ECatDomainCommandError Rejection = ValidateRunConsumableGrant(RequestId, DefinitionId, Quantity);
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
		FCatRunConsumableStack& Stack = FindOrAddConsumable(DefinitionId);
		Stack.Quantity += Quantity;
		++Snapshot.Revision;
		PublishSnapshot();
		Result.bCommitted = true;
		Result.Error = ECatDomainCommandError::None;
	}
	Result.Revision = Snapshot.Revision;
	TerminalCache.Add(Key, Result);
	TerminalPayloadByKey.Add(Key, PayloadSignature);
	return Result;
}

// 团队库装配预检流程：按正式装配入口同一套事实只读判断 authority、RequestId、实例、定义类别、Equipment Revision 和现有三件套。
// 它不写终态缓存，也不发布 Snapshot；调用方用它把“个人装备收不下”挡在团队库删除之前。
ECatDomainCommandError UCatEquipmentComponent::ValidateTeamLibraryEquipFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const FCatTeamEquipmentInstance& Instance) const
{
	const UCatEquipmentDefinition* Definition =
		GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(Instance.DefinitionId);
	const bool bLoadoutComplete = !Snapshot.RodDefinitionId.IsNone()
		&& !Snapshot.BaitDefinitionId.IsNone() && !Snapshot.FloatDefinitionId.IsNone();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid() || !Instance.InstanceId.IsValid()
		|| !Definition || (Definition->Kind != ECatEquipmentKind::Rod && Definition->Kind != ECatEquipmentKind::Bait
			&& Definition->Kind != ECatEquipmentKind::Float))
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	if (Snapshot.Revision != ExpectedRevision)
	{
		return ECatDomainCommandError::RevisionConflict;
	}
	if (!bLoadoutComplete)
	{
		return ECatDomainCommandError::PolicyUndecided;
	}
	return ECatDomainCommandError::None;
}

// 团队库装配流程：先按只读预检挡住无效实例、陈旧 Revision 和未初始化三件套；随后根据定义类别替换唯一槽位。
// 这一步假定调用方已经成功从团队库取走实例；若 payload 重放命中终态缓存，只返回首次结果，不再改第二次个人装备。
FCatDomainCommandResult UCatEquipmentComponent::EquipFromTeamLibraryFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const FCatTeamEquipmentInstance& Instance)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (!RequestId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	const FString Key = MakeTerminalKey(TEXT("EquipFromTeamLibrary"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("ExpectedRevision=%lld|Instance=%s|Definition=%s"),
		ExpectedRevision, *Instance.InstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*Instance.DefinitionId.ToString());
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

	UCatEquipmentDefinition* Definition =
		GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(Instance.DefinitionId);
	const ECatDomainCommandError Admission = ValidateTeamLibraryEquipFromAuthority(RequestId, ExpectedRevision, Instance);
	if (Admission != ECatDomainCommandError::None)
	{
		Result.Error = Admission;
	}
	else if (Definition)
	{
		if (Definition->Kind == ECatEquipmentKind::Rod)
		{
			Snapshot.RodDefinitionId = Instance.DefinitionId;
			Snapshot.RodDurability = Definition->MaximumRodDurability;
			Snapshot.bRodBroken = false;
		}
		else if (Definition->Kind == ECatEquipmentKind::Bait)
		{
			Snapshot.BaitDefinitionId = Instance.DefinitionId;
		}
		else
		{
			Snapshot.FloatDefinitionId = Instance.DefinitionId;
		}
		++Snapshot.Revision;
		PublishSnapshot();
		Result.bCommitted = true;
		Result.Error = ECatDomainCommandError::None;
	}
	Result.Revision = Snapshot.Revision;
	TerminalCache.Add(Key, Result);
	TerminalPayloadByKey.Add(Key, PayloadSignature);
	return Result;
}

// 耗材消费流程：按 RequestId 重放并验证 authority/Revision/正式 consumable 与正库存；成功只扣一份并发布 Revision，上层效果必须在结果成功后执行。
FCatDomainCommandResult UCatEquipmentComponent::ConsumeRunConsumableFromAuthority(const FGuid RequestId,
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
	const FString Key = MakeTerminalKey(TEXT("ConsumeConsumable"), RequestId);
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		Result = *Cached;
		Result.bCommitted = false;
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	UCatEquipmentDefinition* Definition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(DefinitionId);
	FCatRunConsumableStack* Stack = FindConsumable(DefinitionId);
	if (!GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid() || !Definition || !Definition->bRunConsumable)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (Snapshot.Revision != ExpectedRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
	}
	else if (Stack && Stack->Quantity <= GetPendingReservedFishingBaitCount(DefinitionId)
		+ GetPendingReservedRunConsumableCount(DefinitionId))
	{
		Result.Error = ECatDomainCommandError::CapacityExceeded;
	}
	else if (!Stack || Stack->Quantity <= 0)
	{
		Result.Error = ECatDomainCommandError::CapacityExceeded;
	}
	else
	{
		--Stack->Quantity;
		++Snapshot.Revision;
		PublishSnapshot();
		Result.bCommitted = true;
		Result.Error = ECatDomainCommandError::None;
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
		FCatRunConsumableStack* Stack = FindConsumable(Snapshot.BaitDefinitionId);
		if (!Bait || !Bait->bSpecialBait || !Stack || Stack->Quantity <= 0)
		{
			Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
		}
		else
		{
			--Stack->Quantity;
			++Snapshot.Revision;
			Result.Command.bCommitted = true;
			Result.Command.Error = ECatDomainCommandError::None;
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
	// 建立 Fishing 装备预留的流程：
	// 1. 先用 SessionId 返回已存在的终态，保证 FishingSession 重放不会再检查或再占库存。
	// 2. 再校验 authority、定义类型、Revision、当前装配和鱼竿耐久，任何不一致都保持快照不变。
	// 3. 接着拒绝正在进行的 Fishing 或 RunConsumable 操作，让鱼饵库存同一时间只有一个写入意图。
	// 4. 普通饵和特殊饵都必须声明为 RunConsumable，并且数量栈扣除其他 Fishing 预留后仍至少剩一份。
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
	FCatRunConsumableStack* BaitStack = FindConsumable(BaitDefinitionId);
	if (!BaitStack || BaitStack->Quantity <= GetPendingReservedFishingBaitCount(BaitDefinitionId))
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
		FCatRunConsumableStack* Stack = FindConsumable(Record->BaitDefinitionId);
		if (!Stack || Stack->Quantity <= 0)
		{
			return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::CapacityExceeded, false, Record);
		}
		--Stack->Quantity;
		++Snapshot.Revision;
		Record->bBaitCommitted = true;
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
	// 3. 接着拒绝与 Fishing 或其他 RunConsumable 操作并发，避免两条提交链同时改同一份数量栈。
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
	FCatRunConsumableStack* Stack = FindConsumable(DefinitionId);
	if (!Definition || !Definition->bRunConsumable)
	{
		return MakeRunConsumableUseResult(OperationId, ECatDomainCommandError::InvalidPayload);
	}
	if (!Stack || Stack->Quantity - GetPendingReservedFishingBaitCount(DefinitionId) < Quantity)
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
	FCatRunConsumableStack* Stack = FindConsumable(Record->DefinitionId);
	if (!Stack || Stack->Quantity < Record->Quantity)
	{
		return MakeRunConsumableUseResult(OperationId, ECatDomainCommandError::CapacityExceeded, Record);
	}
	Stack->Quantity -= Record->Quantity;
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
	FCatRunConsumableStack* Stack = FindConsumable(Settings->DriftwoodDefinitionId);
	if (!bAtCamp || !GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid() || !Rod || !Driftwood
		|| Driftwood->Kind != ECatEquipmentKind::Driftwood || !Stack || Stack->Quantity <= 0)
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
	}
	else if (Snapshot.Revision != ExpectedRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
	}
	else
	{
		--Stack->Quantity;
		Snapshot.RodDurability = Rod->MaximumRodDurability;
		Snapshot.bRodBroken = false;
		++Snapshot.Revision;
		PublishSnapshot();
		Result.bCommitted = true;
		Result.Error = ECatDomainCommandError::None;
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

// 耗材栈创建流程：按稳定 ID 查找，缺失时追加数量 0 的一局记录；只有验证过定义的调用方使用该辅助。
FCatRunConsumableStack& UCatEquipmentComponent::FindOrAddConsumable(const FName DefinitionId)
{
	if (FCatRunConsumableStack* Existing = FindConsumable(DefinitionId))
	{
		return *Existing;
	}
	FCatRunConsumableStack& NewStack = Snapshot.Consumables.AddDefaulted_GetRef();
	NewStack.DefinitionId = DefinitionId;
	return NewStack;
}

// 耗材栈查询流程：按稳定定义 ID 返回当前一局记录；无匹配不创建占位。
FCatRunConsumableStack* UCatEquipmentComponent::FindConsumable(const FName DefinitionId)
{
	return Snapshot.Consumables.FindByPredicate([DefinitionId](const FCatRunConsumableStack& Stack)
	{
		return Stack.DefinitionId == DefinitionId;
	});
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
