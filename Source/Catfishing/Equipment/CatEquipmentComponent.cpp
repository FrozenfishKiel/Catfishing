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
	const int64 ExpectedRevision, const FName RodDefinitionId, const FName BaitDefinitionId, const FName FloatDefinitionId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (HasActiveFishingUse())
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
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const ACatfishingPlayerState* PlayerState = OwnerPawn ? OwnerPawn->GetPlayerState<ACatfishingPlayerState>() : nullptr;
	if (Settings->ProfileLoadoutTrustPolicy != ECatDomainPolicy::Enabled)
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
	}
	else if (!GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid() || !Rod || !Bait || !Float)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else if (Snapshot.Revision != ExpectedRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
	}
	else if (Rod->Kind != ECatEquipmentKind::Rod || Bait->Kind != ECatEquipmentKind::Bait
		|| Float->Kind != ECatEquipmentKind::Float)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (!PlayerState || !PlayerState->HasServerAuthorizedEquipmentUnlock(Rod->RequiredUnlockId)
		|| !PlayerState->HasServerAuthorizedEquipmentUnlock(Bait->RequiredUnlockId)
		|| !PlayerState->HasServerAuthorizedEquipmentUnlock(Float->RequiredUnlockId))
	{
		Result.Error = ECatDomainCommandError::PermissionDenied;
	}
	else if (!Snapshot.RodDefinitionId.IsNone() || !Snapshot.BaitDefinitionId.IsNone() || !Snapshot.FloatDefinitionId.IsNone())
	{
		// 同一套新 Request 只读取既有耐久，不能借 Configure 免费修竿；任何不同 ID 都属于尚未裁决的换装生命周期。
		const bool bSameLoadout = Snapshot.RodDefinitionId == RodDefinitionId
			&& Snapshot.BaitDefinitionId == BaitDefinitionId && Snapshot.FloatDefinitionId == FloatDefinitionId;
		Result.Error = bSameLoadout ? ECatDomainCommandError::AlreadyResolved : ECatDomainCommandError::PolicyUndecided;
	}
	else
	{
		Snapshot.RodDefinitionId = RodDefinitionId;
		Snapshot.BaitDefinitionId = BaitDefinitionId;
		Snapshot.FloatDefinitionId = FloatDefinitionId;
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

// 耗材授予流程：按 RequestId 重放并验证 authority/Revision/正数量与正式 consumable 定义；成功只增加一局数量和 Revision。
FCatDomainCommandResult UCatEquipmentComponent::GrantRunConsumableFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const FName DefinitionId, const int32 Quantity)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString Key = MakeTerminalKey(TEXT("GrantConsumable"), RequestId);
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		Result = *Cached;
		Result.bCommitted = false;
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	UCatEquipmentDefinition* Definition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(DefinitionId);
	if (!GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid() || !Definition || !Definition->bRunConsumable || Quantity <= 0)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
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
	return Result;
}

// 耗材消费流程：按 RequestId 重放并验证 authority/Revision/正式 consumable 与正库存；成功只扣一份并发布 Revision，上层效果必须在结果成功后执行。
FCatDomainCommandResult UCatEquipmentComponent::ConsumeRunConsumableFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const FName DefinitionId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
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
	else if (Stack && Stack->Quantity <= GetPendingReservedSpecialBaitCount(DefinitionId))
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
	if (HasActiveFishingUse())
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
	if (FishingUseRecords.Contains(FishingSessionId))
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::AlreadyResolved, false);
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
	if (HasActiveFishingUse())
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::AlreadyResolved, false);
	}
	const bool bSpecialBait = Bait->bSpecialBait;
	if (bSpecialBait && !Bait->bRunConsumable)
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::InvalidPayload, false);
	}
	FCatRunConsumableStack* BaitStack = FindConsumable(BaitDefinitionId);
	if (bSpecialBait && (!BaitStack || BaitStack->Quantity <= GetPendingReservedSpecialBaitCount(BaitDefinitionId)))
	{
		return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::CapacityExceeded, false);
	}

	FCatFishingUseRecord Record;
	Record.SessionId = FishingSessionId;
	Record.RodDefinitionId = RodDefinitionId;
	Record.BaitDefinitionId = BaitDefinitionId;
	Record.FloatDefinitionId = FloatDefinitionId;
	Record.ReservationRevision = Snapshot.Revision;
	Record.bSpecialBaitReserved = bSpecialBait;
	FishingUseRecords.Add(FishingSessionId, Record);
	ActiveFishingUseSessionId = FishingSessionId;
	return MakeFishingUseReservationResult(FishingSessionId, ECatDomainCommandError::None, bSpecialBait);
}

FCatFishingUseOperationResult UCatEquipmentComponent::CommitFishingBait(const FGuid FishingSessionId)
{
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
	if (Record->bSpecialBaitReserved)
	{
		FCatRunConsumableStack* Stack = FindConsumable(Record->BaitDefinitionId);
		if (!Stack || Stack->Quantity <= 0)
		{
			return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::CapacityExceeded, false, Record);
		}
		--Stack->Quantity;
		++Snapshot.Revision;
		Record->bBaitCommitted = true;
		PublishSnapshot();
	}
	else
	{
		Record->bBaitCommitted = true;
	}
	return MakeFishingUseOperationResult(FishingSessionId, ECatDomainCommandError::None, true, Record);
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
	if (Record->bReleased || Record->bBreakCommitted)
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

// 维修流程：验证固定营地事实、Revision、当前 Rod/浮木定义和库存；成功只扣一份浮木并恢复当前 Rod 最大耐久，不升级或替换装备。
FCatDomainCommandResult UCatEquipmentComponent::RepairRodAtCamp(const FGuid RequestId, const int64 ExpectedRevision,
	const bool bAtCamp)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (HasActiveFishingUse())
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

int32 UCatEquipmentComponent::GetPendingReservedSpecialBaitCount(const FName DefinitionId) const
{
	int32 ReservedCount = 0;
	for (const TPair<FGuid, FCatFishingUseRecord>& Pair : FishingUseRecords)
	{
		const FCatFishingUseRecord& Record = Pair.Value;
		if (!Record.bReleased && Record.bSpecialBaitReserved && !Record.bBaitCommitted
			&& Record.BaitDefinitionId == DefinitionId)
		{
			++ReservedCount;
		}
	}
	return ReservedCount;
}

FCatFishingUseReservationResult UCatEquipmentComponent::MakeFishingUseReservationResult(const FGuid FishingSessionId,
	const ECatDomainCommandError Error, const bool bReserved) const
{
	FCatFishingUseReservationResult Result;
	Result.SessionId = FishingSessionId;
	Result.Error = Error;
	Result.EquipmentRevision = Snapshot.Revision;
	Result.RemainingRodDurability = Snapshot.RodDurability;
	Result.bReserved = bReserved;
	Result.bRodBroken = Snapshot.bRodBroken;
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
