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

// 装配流程：先用 RequestId 和三槽定义签名确认合法重放，再验证 authority、Revision、正式定义类别和服务器解锁证明；重复同套不补耐久。
FCatDomainCommandResult UCatEquipmentComponent::ConfigureLoadoutFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const FName RodDefinitionId, const FName BaitDefinitionId, const FName FloatDefinitionId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (!RequestId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	const FString Key = MakeTerminalKey(TEXT("ConfigureLoadout"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("ExpectedRevision=%lld|Rod=%s|Bait=%s|Float=%s"),
		ExpectedRevision,
		*RodDefinitionId.ToString(),
		*BaitDefinitionId.ToString(),
		*FloatDefinitionId.ToString());
	switch (CatQueryTerminalReplay(TerminalCache, TerminalPayloadByKey, Key, PayloadSignature, Result, MarkCommandReplayed))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
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
	else if (!GetOwner() || !GetOwner()->HasAuthority() || !Rod || !Bait || !Float)
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
	TerminalPayloadByKey.Add(Key, PayloadSignature);
	return Result;
}

// Starter 装配流程：先拒绝无效请求和非 authority，再只在空 Loadout 上读取配置三件套；真正写入仍委托完整 Configure 校验链。
FCatDomainCommandResult UCatEquipmentComponent::ConfigureStarterLoadoutFromAuthority(const FGuid RequestId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	Result.Revision = Snapshot.Revision;
	if (!RequestId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	if (!Snapshot.RodDefinitionId.IsNone() || !Snapshot.BaitDefinitionId.IsNone() || !Snapshot.FloatDefinitionId.IsNone())
	{
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	FName StarterRodDefinitionId = NAME_None;
	FName StarterBaitDefinitionId = NAME_None;
	FName StarterFloatDefinitionId = NAME_None;
	if (!GetDefault<UCatEquipmentSettings>()->TryGetStarterLoadout(
		StarterRodDefinitionId, StarterBaitDefinitionId, StarterFloatDefinitionId))
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Result;
	}
	return ConfigureLoadoutFromAuthority(RequestId, Snapshot.Revision,
		StarterRodDefinitionId, StarterBaitDefinitionId, StarterFloatDefinitionId);
}

// 耗材授予流程：先用 RequestId、定义、数量和 Revision 签名确认合法重放，再验证 authority、正数量与正式 consumable 定义。
FCatDomainCommandResult UCatEquipmentComponent::GrantRunConsumableFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const FName DefinitionId, const int32 Quantity)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (!RequestId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	const FString Key = MakeTerminalKey(TEXT("GrantConsumable"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("ExpectedRevision=%lld|Definition=%s|Quantity=%d"),
		ExpectedRevision,
		*DefinitionId.ToString(),
		Quantity);
	switch (CatQueryTerminalReplay(TerminalCache, TerminalPayloadByKey, Key, PayloadSignature, Result, MarkCommandReplayed))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}
	UCatEquipmentDefinition* Definition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(DefinitionId);
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Definition || !Definition->bRunConsumable || Quantity <= 0)
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
	TerminalPayloadByKey.Add(Key, PayloadSignature);
	return Result;
}

// 耗材消费流程：先用 RequestId、定义和 Revision 签名确认合法重放，再验证 authority、正式 consumable 与正库存；成功只扣一份。
FCatDomainCommandResult UCatEquipmentComponent::ConsumeRunConsumableFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const FName DefinitionId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (!RequestId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	const FString Key = MakeTerminalKey(TEXT("ConsumeConsumable"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("ExpectedRevision=%lld|Definition=%s"),
		ExpectedRevision,
		*DefinitionId.ToString());
	switch (CatQueryTerminalReplay(TerminalCache, TerminalPayloadByKey, Key, PayloadSignature, Result, MarkCommandReplayed))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}
	UCatEquipmentDefinition* Definition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(DefinitionId);
	FCatRunConsumableStack* Stack = FindConsumable(DefinitionId);
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Definition || !Definition->bRunConsumable)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (Snapshot.Revision != ExpectedRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
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
	TerminalPayloadByKey.Add(Key, PayloadSignature);
	return Result;
}

// 耗材预留流程：先拒绝无效 RequestId 并校验重放载荷，再验证 authority、Revision、正式定义和扣除活动预留后的可用库存；成功只记录活动预留并缓存终态，不写公开 Snapshot 或推进 Revision。
FCatDomainCommandResult UCatEquipmentComponent::ReserveRunConsumableFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const FName DefinitionId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	Result.Revision = Snapshot.Revision;
	if (!RequestId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	const FString Key = MakeTerminalKey(TEXT("ReserveConsumable"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("ExpectedRevision=%lld|Definition=%s"),
		ExpectedRevision,
		*DefinitionId.ToString());
	switch (CatQueryTerminalReplay(TerminalCache, TerminalPayloadByKey, Key, PayloadSignature, Result, MarkCommandReplayed))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}
	UCatEquipmentDefinition* Definition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(DefinitionId);
	FCatRunConsumableStack* Stack = FindConsumable(DefinitionId);
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Definition || !Definition->bRunConsumable)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (Snapshot.Revision != ExpectedRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
	}
	else if (!Stack || Stack->Quantity <= CountActiveConsumableReservations(DefinitionId))
	{
		Result.Error = ECatDomainCommandError::CapacityExceeded;
	}
	else
	{
		FRunConsumableReservation& Reservation = ActiveConsumableReservations.Add(RequestId);
		Reservation.DefinitionId = DefinitionId;
		Reservation.ExpectedRevision = ExpectedRevision;
		Reservation.PayloadSignature = PayloadSignature;
		Result.bCommitted = true;
		Result.Error = ECatDomainCommandError::None;
	}
	Result.Revision = Snapshot.Revision;
	TerminalCache.Add(Key, Result);
	TerminalPayloadByKey.Add(Key, PayloadSignature);
	return Result;
}

// 预留提交流程：先校验 RequestId、重放载荷和活动预留是否仍与调用前提一致；成功时才扣公开库存、清除预留、推进 Revision 并广播，失败只留下终态结果。
FCatDomainCommandResult UCatEquipmentComponent::CommitReservedRunConsumableFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const FName DefinitionId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	Result.Revision = Snapshot.Revision;
	if (!RequestId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	const FString Key = MakeTerminalKey(TEXT("CommitReservedConsumable"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("ExpectedRevision=%lld|Definition=%s"),
		ExpectedRevision,
		*DefinitionId.ToString());
	switch (CatQueryTerminalReplay(TerminalCache, TerminalPayloadByKey, Key, PayloadSignature, Result, MarkCommandReplayed))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}
	const FRunConsumableReservation* Reservation = ActiveConsumableReservations.Find(RequestId);
	FCatRunConsumableStack* Stack = FindConsumable(DefinitionId);
	if (!Reservation || Reservation->PayloadSignature != PayloadSignature || Reservation->DefinitionId != DefinitionId
		|| Reservation->ExpectedRevision != ExpectedRevision)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (Snapshot.Revision != ExpectedRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
	}
	else if (!Stack || Stack->Quantity <= 0)
	{
		Result.Error = ECatDomainCommandError::CapacityExceeded;
	}
	else
	{
		--Stack->Quantity;
		ActiveConsumableReservations.Remove(RequestId);
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

// 预留释放流程：外部领域效果拒绝后按同一载荷找回活动预留；成功只清除占位并缓存释放终态，不推进公开 Revision，失败不会改动库存或其他预留。
FCatDomainCommandResult UCatEquipmentComponent::ReleaseRunConsumableReservationFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const FName DefinitionId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	Result.Revision = Snapshot.Revision;
	if (!RequestId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	const FString Key = MakeTerminalKey(TEXT("ReleaseReservedConsumable"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("ExpectedRevision=%lld|Definition=%s"),
		ExpectedRevision,
		*DefinitionId.ToString());
	switch (CatQueryTerminalReplay(TerminalCache, TerminalPayloadByKey, Key, PayloadSignature, Result, MarkCommandReplayed))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}
	const FRunConsumableReservation* Reservation = ActiveConsumableReservations.Find(RequestId);
	if (!Reservation || Reservation->PayloadSignature != PayloadSignature || Reservation->DefinitionId != DefinitionId
		|| Reservation->ExpectedRevision != ExpectedRevision)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else
	{
		ActiveConsumableReservations.Remove(RequestId);
		Result.bCommitted = true;
		Result.Error = ECatDomainCommandError::None;
	}
	Result.Revision = Snapshot.Revision;
	TerminalCache.Add(Key, Result);
	TerminalPayloadByKey.Add(Key, PayloadSignature);
	return Result;
}

// 失败预算流程：先用 RequestId、Penalty 和 Revision 签名确认合法重放，再只提交当前允许的失败预算；旧 DamageRod 保持 fail-closed。
FCatFishingFailureResult UCatEquipmentComponent::CommitFishingFailure(const FGuid RequestId,
	const int64 ExpectedRevision, const ECatFishingFailurePenalty Penalty)
{
	FCatFishingFailureResult Result;
	Result.Command.RequestId = RequestId;
	if (!RequestId.IsValid())
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	const FString PayloadSignature = FString::Printf(TEXT("ExpectedRevision=%lld|Penalty=%d"),
		ExpectedRevision,
		static_cast<int32>(Penalty));
	if (const FCatFishingFailureResult* Cached = FailureTerminalCache.Find(RequestId))
	{
		const FString* CachedPayload = FailurePayloadByRequestId.Find(RequestId);
		if (!CachedPayload || *CachedPayload != PayloadSignature)
		{
			Result.Command.Error = ECatDomainCommandError::InvalidPayload;
			return Result;
		}
		Result = *Cached;
		Result.Command.bCommitted = false;
		Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	if (!GetOwner() || !GetOwner()->HasAuthority())
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
		Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
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
	FailurePayloadByRequestId.Add(RequestId, PayloadSignature);
	return Result;
}

// Fight 耐久流程：先用 RequestId、成本和 Revision 签名确认合法重放，再验证 authority、正成本与当前鱼竿；成功只写鱼竿耐久和 RodBroken。
FCatDomainCommandResult UCatEquipmentComponent::CommitFightRodDurabilityFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const double DurabilityCost)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (!RequestId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	const FString Key = MakeTerminalKey(TEXT("FightRodDurability"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("ExpectedRevision=%lld|DurabilityCost=%.17g"),
		ExpectedRevision,
		DurabilityCost);
	switch (CatQueryTerminalReplay(TerminalCache, TerminalPayloadByKey, Key, PayloadSignature, Result, MarkCommandReplayed))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}
	if (!GetOwner() || !GetOwner()->HasAuthority()
		|| !FMath::IsFinite(DurabilityCost) || DurabilityCost <= 0.0)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (Snapshot.Revision != ExpectedRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
	}
	else if (Snapshot.RodDefinitionId.IsNone() || Snapshot.bRodBroken || Snapshot.RodDurability <= 0.0)
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
	}
	else
	{
		Snapshot.RodDurability = FMath::Max(0.0, Snapshot.RodDurability - DurabilityCost);
		Snapshot.bRodBroken = Snapshot.RodDurability <= 0.0;
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

// 预留计数流程：遍历当前活动预留并只统计同一耗材定义；Reserve 用它从公开数量中扣除尚未提交的占位。
int32 UCatEquipmentComponent::CountActiveConsumableReservations(const FName DefinitionId) const
{
	int32 Count = 0;
	for (const TPair<FGuid, FRunConsumableReservation>& Pair : ActiveConsumableReservations)
	{
		if (Pair.Value.DefinitionId == DefinitionId)
		{
			++Count;
		}
	}
	return Count;
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
