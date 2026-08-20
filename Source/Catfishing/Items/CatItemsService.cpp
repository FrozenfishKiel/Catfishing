#include "Items/CatItemsService.h"

#include "Logging/CatLog.h"
#include "Engine/World.h"
#include "Items/CatContainerReplicationComponent.h"

namespace
{
	/** 献祭预留在持有键中的目的段；献祭协调器的预留、取消和提交都用它，其他系统不得复用这个字面量。 */
	constexpr const TCHAR* SacrificeHoldPurpose = TEXT("Sacrifice");

	/** 商店售卖冻结在持有键中的目的段；它把售卖冻结与献祭预留分成两个互不相通的键空间。 */
	constexpr const TCHAR* SaleHoldPurpose = TEXT("Sale");
}

// 创建条件流程：只允许 Game/PIE 的 authority World 持有可写 Items；客户端 World 不创建第二份容器聚合。
bool UCatItemsService::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

// 反初始化流程：先关闭命令并取消可逆预留，再清组件弱引用、容器和终态缓存；局内实物不会进入下一张地图。
void UCatItemsService::Deinitialize()
{
	CloseCommandsAndCancelReservations();
	Containers.Reset();
	ReservationByFish.Reset();
	Reservations.Reset();
	ReservationTerminalCache.Reset();
	ReservationPayloadByKey.Reset();
	TheftEscrows.Reset();
	CaptureTerminalCache.Reset();
	CapturePayloadByKey.Reset();
	CaptureByFishingSession.Reset();
	TransferTerminalCache.Reset();
	TransferPayloadByKey.Reset();
	ConsumeTerminalCache.Reset();
	ConsumePayloadByKey.Reset();
	TheftTerminalCache.Reset();
	TheftPayloadByKey.Reset();
	Super::Deinitialize();
}

// 容器注册流程：逐项验证组件、ID、类型与个人身份，再建立 Revision=1 的空快照并发布；同 ID 只允许同一组件幂等重放。
bool UCatItemsService::RegisterContainer(UCatContainerReplicationComponent* ReplicationComponent, const FGuid ContainerId,
	const ECatContainerKind Kind, const FString& OwnerStableNetId, const int32 Capacity)
{
	if (!bCommandsOpen || !ReplicationComponent || !ContainerId.IsValid() || Kind == ECatContainerKind::Unknown
		|| (Kind == ECatContainerKind::PersonalGuard && OwnerStableNetId.IsEmpty()))
	{
		return false;
	}
	if (FContainerRecord* Existing = Containers.Find(ContainerId))
	{
		return Existing->ReplicationComponent.Get() == ReplicationComponent;
	}
	FContainerRecord& Record = Containers.Add(ContainerId);
	Record.Snapshot.ContainerId = ContainerId;
	Record.Snapshot.Kind = Kind;
	Record.Snapshot.Revision = 1;
	Record.OwnerStableNetId = Kind == ECatContainerKind::PersonalGuard ? OwnerStableNetId : FString();
	Record.Capacity = FMath::Max(0, Capacity);
	Record.ReplicationComponent = ReplicationComponent;
	PublishContainer(Record);
	return true;
}

// 容器注销流程：只处理弱引用精确匹配的那条记录，迟到的旧 Actor 不能删掉同 ID 的新注册容器。
// 命中后还要分两种情况：该容器仍欠着偷鱼 escrow 的返还槽位时，只清掉宿主弱引用而保留记录，
// 否则赃物被追回时就没有容器可以放回去；没有欠账时才整条移除。
void UCatItemsService::UnregisterContainer(UCatContainerReplicationComponent* ReplicationComponent)
{
	if (!ReplicationComponent)
	{
		return;
	}
	for (auto It = Containers.CreateIterator(); It; ++It)
	{
		if (It.Value().ReplicationComponent.Get() == ReplicationComponent)
		{
			if (CountReservedReturnSlots(It.Key()) > 0)
			{
				It.Value().ReplicationComponent.Reset();
			}
			else
			{
				It.RemoveCurrent();
			}
			return;
		}
	}
}

// 快照查询流程：按稳定容器 ID 复制公开 DTO；服务端私有容量、身份和预留永远不写入输出。
bool UCatItemsService::TryGetContainerSnapshot(const FGuid ContainerId, FCatContainerSnapshot& OutSnapshot) const
{
	const FContainerRecord* Record = Containers.Find(ContainerId);
	if (!Record)
	{
		OutSnapshot = FCatContainerSnapshot();
		return false;
	}
	OutSnapshot = Record->Snapshot;
	return true;
}

// 捕获提交流程：先用最小合法字段生成终态键和载荷签名，重放必须签名一致，再验证预分配鱼 ID、个人鱼护、身份、Revision、容量和冻结定义值。
// SacrificeContribution 只拒绝 0：它是从鱼定义冻结下来的世界进度增减量，负值是臭臭鱼这类鱼的正常取值，
// 只有 0 才意味着调用方没带上定义值，那种鱼放进鱼护后献祭链会拿到一个没有出处的进度增量。
FCatCaptureCommitResult UCatItemsService::CommitCapture(const FCatCaptureCommitCommand& Command)
{
	FCatCaptureCommitResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	Result.Command.Error = ECatDomainCommandError::InvalidPayload;
	if (!Command.Context.RequestId.IsValid() || !Command.FishingSessionId.IsValid() || !Command.FishInstanceId.IsValid()
		|| !Command.TargetContainerId.IsValid()
		|| Command.Context.StableNetId.IsEmpty() || Command.FishDefinitionId.IsNone()
		|| !FMath::IsFinite(Command.WeightKilograms) || Command.WeightKilograms <= 0.0 || Command.SacrificeContribution == 0)
	{
		return Result;
	}
	const FString CacheKey = MakeTerminalKey(Command.Context.StableNetId, TEXT("Capture"),
		Command.FishingSessionId, Command.Context.RequestId);
	const FString PayloadSignature = FString::Printf(
		TEXT("ExpectedRevision=%lld|FishInstanceId=%s|FishDefinitionId=%s|TargetContainerId=%s|Weight=%.17g|Contribution=%d"),
		Command.Context.ExpectedRevision,
		*Command.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*Command.FishDefinitionId.ToString(),
		*Command.TargetContainerId.ToString(EGuidFormats::DigitsWithHyphens),
		Command.WeightKilograms,
		Command.SacrificeContribution);
	switch (CatQueryTerminalReplay(CaptureTerminalCache, CapturePayloadByKey, CacheKey, PayloadSignature, Result, [](FCatCaptureCommitResult& R){ MarkCommandReplayed(R.Command); }))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}
	// FishingSession 是捕获竞争的聚合作用域；服务级映射在任何容器写入前复核，防止旁路或不同 RequestId 为同一会话生成第二个 FishInstance。
	if (const FCatCaptureCommittedResult* ExistingCapture = CaptureByFishingSession.Find(Command.FishingSessionId))
	{
		Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
		Result.Command.Revision = ExistingCapture->ContainerRevision;
		Result.Committed = *ExistingCapture;
		CaptureTerminalCache.Add(CacheKey, Result);
		CapturePayloadByKey.Add(CacheKey, PayloadSignature);
		return Result;
	}
	FContainerRecord* Target = Containers.Find(Command.TargetContainerId);
	if (!bCommandsOpen)
	{
		Result.Command.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (!Target)
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
	}
	else if (Target->Snapshot.Kind != ECatContainerKind::PersonalGuard || Target->OwnerStableNetId != Command.Context.StableNetId)
	{
		Result.Command.Error = ECatDomainCommandError::PermissionDenied;
	}
	else if (Target->Snapshot.Revision != Command.Context.ExpectedRevision)
	{
		Result.Command.Error = ECatDomainCommandError::RevisionConflict;
	}
	else if (Target->Capacity <= 0)
	{
		Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
	}
	else if (Target->Snapshot.Fish.Num() + CountReservedReturnSlots(Command.TargetContainerId) >= Target->Capacity)
	{
		Result.Command.Error = ECatDomainCommandError::CapacityExceeded;
	}
	else
	{
		FCatFishInstance Fish;
		Fish.FishInstanceId = Command.FishInstanceId;
		Fish.FishDefinitionId = Command.FishDefinitionId;
		Fish.OwnerStableNetId = Command.Context.StableNetId;
		Fish.SourceFishingSessionId = Command.FishingSessionId;
		Fish.SacrificeContribution = Command.SacrificeContribution;
		Fish.WeightKilograms = Command.WeightKilograms;
		Target->Snapshot.Fish.Add(Fish);
		++Target->Snapshot.Revision;
		PublishContainer(*Target);
		Result.Command.bCommitted = true;
		Result.Command.Error = ECatDomainCommandError::None;
		Result.Command.Revision = Target->Snapshot.Revision;
		Result.Committed.CaptureRequestId = Command.Context.RequestId;
		Result.Committed.FishingSessionId = Command.FishingSessionId;
		Result.Committed.FishInstance = Fish;
		Result.Committed.ContainerId = Command.TargetContainerId;
		Result.Committed.ContainerRevision = Target->Snapshot.Revision;
		CaptureByFishingSession.Add(Command.FishingSessionId, Result.Committed);
	}
	Result.Command.Revision = Target ? Target->Snapshot.Revision : 0;
	CaptureTerminalCache.Add(CacheKey, Result);
	CapturePayloadByKey.Add(CacheKey, PayloadSignature);
	UE_LOG(LogCatItems, Log, TEXT("Event=items_capture_terminal RequestId=%s SessionId=%s Committed=%s Error=%s ContainerRevision=%lld"),
		*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		*Command.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
		Result.Command.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Command.Error), Result.Command.Revision);
	return Result;
}

// 原子转移流程：先用源容器聚合、RequestId 和载荷签名确认合法重放，再同时校验两容器、源权限、双 Revision、锁和目标容量。
FCatDomainCommandResult UCatItemsService::TransferOwnedFish(const FCatFishTransferCommand& Command)
{
	FCatDomainCommandResult Result;
	Result.RequestId = Command.Context.RequestId;
	Result.Error = ECatDomainCommandError::InvalidPayload;
	if (!Command.Context.RequestId.IsValid() || Command.Context.StableNetId.IsEmpty() || !Command.FishInstanceId.IsValid()
		|| !Command.SourceContainerId.IsValid() || !Command.TargetContainerId.IsValid()
		|| Command.SourceContainerId == Command.TargetContainerId)
	{
		return Result;
	}
	const FString CacheKey = MakeTerminalKey(Command.Context.StableNetId, TEXT("Transfer"),
		Command.SourceContainerId, Command.Context.RequestId);
	const FString PayloadSignature = FString::Printf(
		TEXT("ExpectedRevision=%lld|FishInstanceId=%s|TargetContainerId=%s|ExpectedTargetRevision=%lld"),
		Command.Context.ExpectedRevision,
		*Command.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*Command.TargetContainerId.ToString(EGuidFormats::DigitsWithHyphens),
		Command.ExpectedTargetRevision);
	switch (CatQueryTerminalReplay(TransferTerminalCache, TransferPayloadByKey, CacheKey, PayloadSignature, Result, MarkCommandReplayed))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}
	FContainerRecord* Source = Containers.Find(Command.SourceContainerId);
	FContainerRecord* Target = Containers.Find(Command.TargetContainerId);
	int32 FishIndex = INDEX_NONE;
	if (!bCommandsOpen)
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (!Source || !Target)
	{
		Result.Error = ECatDomainCommandError::NotFound;
	}
	else if (Source->Snapshot.Kind != ECatContainerKind::PersonalGuard || Source->OwnerStableNetId != Command.Context.StableNetId)
	{
		Result.Error = ECatDomainCommandError::PermissionDenied;
	}
	else if (Source->Snapshot.Revision != Command.Context.ExpectedRevision || Target->Snapshot.Revision != Command.ExpectedTargetRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
	}
	else if (ReservationByFish.Contains(Command.FishInstanceId))
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
	}
	else if (Target->Capacity <= 0)
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
	}
	else if (Target->Snapshot.Fish.Num() + CountReservedReturnSlots(Command.TargetContainerId) >= Target->Capacity)
	{
		Result.Error = ECatDomainCommandError::CapacityExceeded;
	}
	else
	{
		FishIndex = Source->Snapshot.Fish.IndexOfByPredicate([&Command](const FCatFishInstance& Fish)
		{
			return Fish.FishInstanceId == Command.FishInstanceId;
		});
		if (FishIndex == INDEX_NONE)
		{
			Result.Error = ECatDomainCommandError::NotFound;
		}
		else
		{
			const FCatFishInstance Fish = Source->Snapshot.Fish[FishIndex];
			Source->Snapshot.Fish.RemoveAt(FishIndex);
			Target->Snapshot.Fish.Add(Fish);
			++Source->Snapshot.Revision;
			++Target->Snapshot.Revision;
			PublishContainer(*Source);
			PublishContainer(*Target);
			Result.bCommitted = true;
			Result.Error = ECatDomainCommandError::None;
			Result.Revision = Source->Snapshot.Revision;
		}
	}
	Result.Revision = Source ? Source->Snapshot.Revision : 0;
	TransferTerminalCache.Add(CacheKey, Result);
	TransferPayloadByKey.Add(CacheKey, PayloadSignature);
	UE_LOG(LogCatItems, Log, TEXT("Event=items_transfer_terminal RequestId=%s Committed=%s Error=%s SourceRevision=%lld"),
		*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens), Result.bCommitted ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(Result.Error), Result.Revision);
	return Result;
}

// 直接进食流程：先用源容器聚合、RequestId 和载荷签名确认合法重放，再校验身份、Revision、个人归属/共享缸、未预留与目标鱼。
FCatFishConsumeResult UCatItemsService::ConsumeFish(const FCatFishConsumeCommand& Command)
{
	FCatFishConsumeResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	if (!Command.Context.RequestId.IsValid() || Command.Context.StableNetId.IsEmpty()
		|| !Command.FishInstanceId.IsValid() || !Command.SourceContainerId.IsValid())
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	const FString CacheKey = MakeTerminalKey(Command.Context.StableNetId, TEXT("ConsumeFish"),
		Command.SourceContainerId, Command.Context.RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("ExpectedRevision=%lld|FishInstanceId=%s"),
		Command.Context.ExpectedRevision,
		*Command.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens));
	switch (CatQueryTerminalReplay(ConsumeTerminalCache, ConsumePayloadByKey, CacheKey, PayloadSignature, Result, [](FCatFishConsumeResult& R){ MarkCommandReplayed(R.Command); }))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}
	FContainerRecord* Source = Containers.Find(Command.SourceContainerId);
	int32 FishIndex = INDEX_NONE;
	if (!bCommandsOpen)
	{
		Result.Command.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (!Source)
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
	}
	else if (Source->Snapshot.Revision != Command.Context.ExpectedRevision)
	{
		Result.Command.Error = ECatDomainCommandError::RevisionConflict;
	}
	else if (ReservationByFish.Contains(Command.FishInstanceId))
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPhase;
	}
	else
	{
		FishIndex = Source->Snapshot.Fish.IndexOfByPredicate([&Command](const FCatFishInstance& Fish)
		{
			return Fish.FishInstanceId == Command.FishInstanceId;
		});
		if (FishIndex == INDEX_NONE)
		{
			Result.Command.Error = ECatDomainCommandError::NotFound;
		}
		else if (Source->Snapshot.Kind == ECatContainerKind::PersonalGuard
			&& Source->Snapshot.Fish[FishIndex].OwnerStableNetId != Command.Context.StableNetId)
		{
			Result.Command.Error = ECatDomainCommandError::PermissionDenied;
		}
		else
		{
			Result.Fish = Source->Snapshot.Fish[FishIndex];
			Source->Snapshot.Fish.RemoveAt(FishIndex);
			++Source->Snapshot.Revision;
			PublishContainer(*Source);
			Result.Command.bCommitted = true;
			Result.Command.Error = ECatDomainCommandError::None;
		}
	}
	Result.Command.Revision = Source ? Source->Snapshot.Revision : 0;
	ConsumeTerminalCache.Add(CacheKey, Result);
	ConsumePayloadByKey.Add(CacheKey, PayloadSignature);
	UE_LOG(LogCatItems, Log, TEXT("Event=items_consume_terminal RequestId=%s Committed=%s Error=%s Revision=%lld"),
		*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens), Result.Command.bCommitted ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(Result.Command.Error), Result.Command.Revision);
	return Result;
}

// 献祭预留流程：整体复用鱼冻结机制，只把持有目的固定为献祭；调用方不需要鱼副本，献祭结果只带走冻结贡献。
FCatFishReservationResult UCatItemsService::ReserveFish(const FCatSacrificeCommand& Command)
{
	return HoldFish(Command.Context, Command.ContainerId, Command.FishInstanceId, SacrificeHoldPurpose, nullptr);
}

// 鱼冻结流程：先按持有键与业务载荷重放首次结果（重放成功前还要确认那把锁此刻真的还在），再校验身份、容器 Revision、
// 鱼归属与未被其他持有占用；成功只增加 Revision 和锁，不提前删除复制数组。
// 键里带持有目的的原因：献祭协调器和商店都拿客户端 RequestId 当幂等键，两边可能给同一容器发同一个 GUID；
// 键不区分目的时，一方的提交就能把另一方冻结的鱼删掉，而两边各自看到的都是"我的请求正常完成"。
FCatFishReservationResult UCatItemsService::HoldFish(const FCatDomainCommandContext& Context, const FGuid ContainerId,
	const FGuid FishInstanceId, const TCHAR* HoldPurpose, FCatFishInstance* OutFish)
{
	FCatFishReservationResult Result;
	if (!Context.RequestId.IsValid() || !FishInstanceId.IsValid()
		|| !ContainerId.IsValid() || Context.StableNetId.IsEmpty())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	const FString ReservationKey = MakeReservationKey(Context.StableNetId, ContainerId,
		Context.RequestId, HoldPurpose);
	const FString PayloadSignature = FString::Printf(TEXT("ExpectedRevision=%lld|FishInstanceId=%s"),
		Context.ExpectedRevision,
		*FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens));
	// 传空的 MarkReplayed 不是偷懒：本领域的结果结构里没有"本次是否发生写入"这一位，重放要么原样交回首次成功，
	// 要么原样交回首次拒绝的原因（陈旧 Revision 就是陈旧 Revision，不能被改写成 AlreadyResolved），所以没有终态头要改。
	switch (CatQueryTerminalReplay(ReservationTerminalCache, ReservationPayloadByKey, ReservationKey,
		PayloadSignature, Result, [](FCatFishReservationResult&){}))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
	{
		const FReservationRecord* Held = Reservations.Find(ReservationKey);
		// 终态缓存答的是"这个请求当时得到了什么"，预留记录答的是"此刻这条鱼还冻着没有"，两者故意不同寿：
		// 缓存要留下来挡住"取消之后拿同一个 RequestId 再锁一次鱼"，所以它必然会比锁活得久。
		// 也正因为如此，重放时不能把当时那句"冻结成功"当成现在的事实——ReleaseFishHold 和 teardown 都会把记录连同锁一起
		// 拿走，那之后鱼已经还给容器，谁都可以转移、吃掉或献祭它。此时再重放一次成功，调用方会以为自己手上有一条锁住的
		// 鱼，而 OutFish 无鱼可填，它就拿着一条全零的鱼往下走（商店链会拿 0 千克去问价）。
		// 所以这里现查一次锁：锁没了就照实说这个请求已经被取消，让调用方换个 RequestId 重新发起。
		if (Result.bReserved && !Held)
		{
			FCatFishReservationResult ReleasedResult;
			ReleasedResult.Error = ECatDomainCommandError::Cancelled;
			if (const FContainerRecord* CurrentContainer = Containers.Find(ContainerId))
			{
				ReleasedResult.ContainerRevision = CurrentContainer->Snapshot.Revision;
			}
			return ReleasedResult;
		}
		// 走到这里说明载荷一致、锁也还在，才把冻结的那条鱼交回去；换鱼或换版本前提的同 RequestId 请求上面已经被判冲突，
		// 连首次冻的是哪条鱼都看不到。
		if (OutFish && Held)
		{
			*OutFish = Held->Fish;
		}
		return Result;
	}
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}
	FContainerRecord* Container = Containers.Find(ContainerId);
	const auto Finish = [this, &ReservationKey, &PayloadSignature, &Result]()
	{
		ReservationTerminalCache.Add(ReservationKey, Result);
		ReservationPayloadByKey.Add(ReservationKey, PayloadSignature);
		return Result;
	};
	if (!bCommandsOpen)
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		return Finish();
	}
	if (!Container)
	{
		Result.Error = ECatDomainCommandError::NotFound;
		return Finish();
	}
	if (Container->Snapshot.Revision != Context.ExpectedRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
		Result.ContainerRevision = Container->Snapshot.Revision;
		return Finish();
	}
	const int32 FishIndex = Container->Snapshot.Fish.IndexOfByPredicate([FishInstanceId](const FCatFishInstance& Fish)
	{
		return Fish.FishInstanceId == FishInstanceId;
	});
	if (FishIndex == INDEX_NONE)
	{
		Result.Error = ECatDomainCommandError::NotFound;
		return Finish();
	}
	const FCatFishInstance& Fish = Container->Snapshot.Fish[FishIndex];
	// 个人鱼护里的鱼只有它自己的主人能冻结；共用鱼缸装的是团队储备，任何在场玩家都能冻结，不要求原钓手在场。
	const bool bOwnerMayReserve = Container->Snapshot.Kind == ECatContainerKind::SharedFishTank
		|| Fish.OwnerStableNetId == Context.StableNetId;
	if (!bOwnerMayReserve)
	{
		Result.Error = ECatDomainCommandError::PermissionDenied;
		return Finish();
	}
	if (ReservationByFish.Contains(FishInstanceId))
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
		return Finish();
	}
	FReservationRecord& Reservation = Reservations.Add(ReservationKey);
	Reservation.ContainerId = ContainerId;
	Reservation.Fish = Fish;
	ReservationByFish.Add(Reservation.Fish.FishInstanceId, ReservationKey);
	++Container->Snapshot.Revision;
	PublishContainer(*Container);
	Result.bReserved = true;
	Result.Error = ECatDomainCommandError::None;
	Result.ContainerRevision = Container->Snapshot.Revision;
	Result.SacrificeContribution = Reservation.Fish.SacrificeContribution;
	if (OutFish)
	{
		*OutFish = Reservation.Fish;
	}
	return Finish();
}

// 预留取消流程：整体复用持有释放机制，只把持有目的固定为献祭；协调器不需要鱼副本。
FCatDomainCommandResult UCatItemsService::CancelFishReservation(const FString& StableNetId, const FGuid RequestId,
	const FGuid ContainerId)
{
	return ReleaseFishHold(StableNetId, ContainerId, RequestId, SacrificeHoldPurpose, nullptr);
}

// 持有释放流程：只消费未提交且容器匹配的记录，移除 Fish 锁并增加容器 Revision；已提交记录返回 AlreadyResolved 且不恢复鱼。
// 这里刻意不去删同键的终态缓存：那条缓存正是用来拒绝"取消之后同一个 RequestId 又想把鱼锁回去"的重放，删了这道门就没了。
// 代价是缓存会比锁活得久，所以由 HoldFish 的重放段现查锁来收口，而不是靠这里清缓存。
FCatDomainCommandResult UCatItemsService::ReleaseFishHold(const FString& StableNetId, const FGuid ContainerId,
	const FGuid RequestId, const TCHAR* HoldPurpose, FCatFishInstance* OutFish)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString ReservationKey = MakeReservationKey(StableNetId, ContainerId, RequestId, HoldPurpose);
	FReservationRecord* Reservation = Reservations.Find(ReservationKey);
	FContainerRecord* Container = Containers.Find(ContainerId);
	if (!Reservation || !Container || Reservation->ContainerId != ContainerId)
	{
		Result.Error = ECatDomainCommandError::NotFound;
		return Result;
	}
	if (OutFish)
	{
		*OutFish = Reservation->Fish;
	}
	if (Reservation->bCommitted)
	{
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		Result.Revision = Reservation->CommittedRevision;
		return Result;
	}
	ReservationByFish.Remove(Reservation->Fish.FishInstanceId);
	Reservations.Remove(ReservationKey);
	++Container->Snapshot.Revision;
	PublishContainer(*Container);
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::Cancelled;
	Result.Revision = Container->Snapshot.Revision;
	return Result;
}

// 预留提交流程：整体复用持有提交机制，只把持有目的固定为献祭；协调器只读贡献值，不需要鱼副本。
FCatFishReservationCommitResult UCatItemsService::CommitFishReservation(const FString& StableNetId,
	const FGuid RequestId, const FGuid ContainerId)
{
	return CommitFishHold(StableNetId, ContainerId, RequestId, SacrificeHoldPurpose, nullptr);
}

// 持有提交流程：重复提交原样返回；首次提交确认鱼仍存在且锁属于该持有键，再不可逆删除数组、释放锁、记录提交 Revision 并发布。
FCatFishReservationCommitResult UCatItemsService::CommitFishHold(const FString& StableNetId,
	const FGuid ContainerId, const FGuid RequestId, const TCHAR* HoldPurpose, FCatFishInstance* OutFish)
{
	FCatFishReservationCommitResult Result;
	const FString ReservationKey = MakeReservationKey(StableNetId, ContainerId, RequestId, HoldPurpose);
	FReservationRecord* Reservation = Reservations.Find(ReservationKey);
	FContainerRecord* Container = Containers.Find(ContainerId);
	if (!Reservation || !Container || Reservation->ContainerId != ContainerId)
	{
		Result.Error = ECatDomainCommandError::NotFound;
		return Result;
	}
	if (OutFish)
	{
		*OutFish = Reservation->Fish;
	}
	if (Reservation->bCommitted)
	{
		Result.bCommitted = true;
		Result.Error = ECatDomainCommandError::None;
		Result.ContainerRevision = Reservation->CommittedRevision;
		Result.SacrificeContribution = Reservation->Fish.SacrificeContribution;
		return Result;
	}
	const FString* LockOwner = ReservationByFish.Find(Reservation->Fish.FishInstanceId);
	const int32 FishIndex = Container->Snapshot.Fish.IndexOfByPredicate([Reservation](const FCatFishInstance& Fish)
	{
		return Fish.FishInstanceId == Reservation->Fish.FishInstanceId;
	});
	if (!LockOwner || *LockOwner != ReservationKey || FishIndex == INDEX_NONE)
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
		return Result;
	}
	Container->Snapshot.Fish.RemoveAt(FishIndex);
	ReservationByFish.Remove(Reservation->Fish.FishInstanceId);
	++Container->Snapshot.Revision;
	Reservation->bCommitted = true;
	Reservation->CommittedRevision = Container->Snapshot.Revision;
	PublishContainer(*Container);
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	Result.ContainerRevision = Reservation->CommittedRevision;
	Result.SacrificeContribution = Reservation->Fish.SacrificeContribution;
	return Result;
}

// 售卖冻结流程：商店写钱包之前先用同一套持有机制把这条鱼锁住；锁定期间它不能被转移、吃掉、献祭或偷走，
// 所以钱包入账失败时只要释放冻结，鱼就原样还在，不会出现钱和鱼同时到手。
// 这里不查鱼定义、不算价格、不碰钱包：Items 只回答"这条鱼现在能不能被取出"。
// 重放判定必须先于持有核心：核心对首提和重放返回同一结构，只有先看一眼记录，才能把重试报成 AlreadyResolved 而不是第二次成交。
FCatFishSaleHoldResult UCatItemsService::PrepareFishSale(const FCatFishSaleHoldCommand& Command)
{
	FCatFishSaleHoldResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	const bool bHeldBefore = Reservations.Contains(MakeReservationKey(Command.Context.StableNetId,
		Command.ContainerId, Command.Context.RequestId, SaleHoldPurpose));
	const FCatFishReservationResult Hold = HoldFish(Command.Context, Command.ContainerId, Command.FishInstanceId,
		SaleHoldPurpose, &Result.Fish);
	Result.Command.Revision = Hold.ContainerRevision;
	if (!Hold.bReserved)
	{
		Result.Command.Error = Hold.Error;
		return Result;
	}
	Result.Command.bCommitted = !bHeldBefore;
	Result.Command.Error = bHeldBefore ? ECatDomainCommandError::AlreadyResolved : ECatDomainCommandError::None;
	UE_LOG(LogCatItems, Log, TEXT("Event=items_fish_sale_prepared RequestId=%s ContainerId=%s Committed=%s Revision=%lld"),
		*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		*Command.ContainerId.ToString(EGuidFormats::DigitsWithHyphens),
		Result.Command.bCommitted ? TEXT("true") : TEXT("false"), Result.Command.Revision);
	return Result;
}

// 售卖回退流程：钱包没入账或入账失败时释放同一售卖请求的冻结，这条鱼原位恢复可转移、可吃、可献祭、可被偷。
// 已经 drain 过的售卖不会被回退：那时钱已经入账、鱼已经不在容器里，恢复它就是凭空多出一条鱼。
FCatFishSaleHoldResult UCatItemsService::CancelPreparedFishSale(const FString& StableNetId, const FGuid ContainerId,
	const FGuid SaleRequestId)
{
	FCatFishSaleHoldResult Result;
	Result.Command = ReleaseFishHold(StableNetId, ContainerId, SaleRequestId, SaleHoldPurpose, &Result.Fish);
	return Result;
}

// 售卖 drain 流程：钱包已经入账之后，不可逆移除同一售卖请求冻结的那条鱼。
// 钱包成功但 drain 失败时可以用同一 RequestId 重试；重试成功后再重放只返回首次的 Revision 与同一条鱼，不会二次移除。
FCatFishSaleHoldResult UCatItemsService::CommitPreparedFishSale(const FString& StableNetId, const FGuid ContainerId,
	const FGuid SaleRequestId)
{
	FCatFishSaleHoldResult Result;
	Result.Command.RequestId = SaleRequestId;
	// 同上：是否已经 drain 过必须在调用持有核心之前读，核心把重放和首提返回成同一结构。
	const FReservationRecord* Held = Reservations.Find(
		MakeReservationKey(StableNetId, ContainerId, SaleRequestId, SaleHoldPurpose));
	const bool bDrainedBefore = Held && Held->bCommitted;
	const FCatFishReservationCommitResult Drain = CommitFishHold(StableNetId, ContainerId, SaleRequestId,
		SaleHoldPurpose, &Result.Fish);
	Result.Command.Revision = Drain.ContainerRevision;
	if (!Drain.bCommitted)
	{
		Result.Command.Error = Drain.Error;
		return Result;
	}
	Result.Command.bCommitted = !bDrainedBefore;
	Result.Command.Error = bDrainedBefore ? ECatDomainCommandError::AlreadyResolved : ECatDomainCommandError::None;
	UE_LOG(LogCatItems, Log, TEXT("Event=items_fish_sale_drained RequestId=%s ContainerId=%s FishInstanceId=%s Committed=%s Revision=%lld"),
		*SaleRequestId.ToString(EGuidFormats::DigitsWithHyphens),
		*ContainerId.ToString(EGuidFormats::DigitsWithHyphens),
		*Result.Fish.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		Result.Command.bCommitted ? TEXT("true") : TEXT("false"), Result.Command.Revision);
	return Result;
}

// 偷鱼开始流程：先用源容器聚合、RequestId 和载荷签名确认合法重放，再校验命令、源容器、Revision、目标鱼、非本人和未预留。
FCatFishTheftResult UCatItemsService::BeginFishTheft(const FCatFishTheftCommand& Command)
{
	FCatFishTheftResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	Result.TheftProtocolId = Command.TheftProtocolId;
	if (!Command.Context.RequestId.IsValid() || Command.Context.StableNetId.IsEmpty()
		|| !Command.TheftProtocolId.IsValid() || !Command.FishInstanceId.IsValid() || !Command.SourceContainerId.IsValid())
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	const FString CacheKey = MakeTerminalKey(Command.Context.StableNetId, TEXT("Theft"),
		Command.SourceContainerId, Command.Context.RequestId);
	const FString PayloadSignature = FString::Printf(
		TEXT("ExpectedRevision=%lld|TheftProtocolId=%s|FishInstanceId=%s"),
		Command.Context.ExpectedRevision,
		*Command.TheftProtocolId.ToString(EGuidFormats::DigitsWithHyphens),
		*Command.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens));
	switch (CatQueryTerminalReplay(TheftTerminalCache, TheftPayloadByKey, CacheKey, PayloadSignature, Result, [](FCatFishTheftResult& R){ MarkCommandReplayed(R.Command); }))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}
	FContainerRecord* Source = Containers.Find(Command.SourceContainerId);
	int32 FishIndex = INDEX_NONE;
	if (!bCommandsOpen)
	{
		Result.Command.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (!Source)
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
	}
	else if (Source->Snapshot.Revision != Command.Context.ExpectedRevision)
	{
		Result.Command.Error = ECatDomainCommandError::RevisionConflict;
	}
	else if (ReservationByFish.Contains(Command.FishInstanceId))
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPhase;
	}
	else
	{
		FishIndex = Source->Snapshot.Fish.IndexOfByPredicate([&Command](const FCatFishInstance& Fish)
		{
			return Fish.FishInstanceId == Command.FishInstanceId;
		});
		if (FishIndex == INDEX_NONE)
		{
			Result.Command.Error = ECatDomainCommandError::NotFound;
		}
		else if (Source->Snapshot.Fish[FishIndex].OwnerStableNetId.IsEmpty()
			|| Source->Snapshot.Fish[FishIndex].OwnerStableNetId == Command.Context.StableNetId)
		{
			Result.Command.Error = ECatDomainCommandError::PermissionDenied;
		}
		else if (TheftEscrows.Contains(Command.TheftProtocolId))
		{
			// ProtocolId 由 Social 生成且全局唯一；碰撞时不得覆盖另一玩家 escrow，也不能退回使用客户端 RequestId。
			Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
		}
		else
		{
			FTheftEscrowRecord& Escrow = TheftEscrows.Add(Command.TheftProtocolId);
			Escrow.ClientRequestId = Command.Context.RequestId;
			Escrow.SourceContainerId = Command.SourceContainerId;
			Escrow.Fish = Source->Snapshot.Fish[FishIndex];
			Escrow.ThiefStableNetId = Command.Context.StableNetId;
			Source->Snapshot.Fish.RemoveAt(FishIndex);
			++Source->Snapshot.Revision;
			PublishContainer(*Source);
			Result.Command.bCommitted = true;
			Result.Command.Error = ECatDomainCommandError::None;
			Result.Fish = Escrow.Fish;
		}
	}
	Result.Command.Revision = Source ? Source->Snapshot.Revision : 0;
	TheftTerminalCache.Add(CacheKey, Result);
	TheftPayloadByKey.Add(CacheKey, PayloadSignature);
	return Result;
}

// 偷鱼追回流程：按服务器 ProtocolId 定位精确 escrow 与仍存源容器；售卖准备态会拒绝追回，避免钱包结算中途把鱼还回去。
FCatFishTheftResult UCatItemsService::ReturnStolenFish(const FGuid TheftProtocolId)
{
	FCatFishTheftResult Result;
	Result.TheftProtocolId = TheftProtocolId;
	FTheftEscrowRecord* Escrow = TheftEscrows.Find(TheftProtocolId);
	Result.Command.RequestId = Escrow ? Escrow->ClientRequestId : FGuid();
	FContainerRecord* Source = Escrow ? Containers.Find(Escrow->SourceContainerId) : nullptr;
	if (!Escrow || !Source)
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
		return Result;
	}
	if (Escrow->bSalePrepared)
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPhase;
		return Result;
	}
	Result.Fish = Escrow->Fish;
	Source->Snapshot.Fish.Add(Escrow->Fish);
	++Source->Snapshot.Revision;
	PublishContainer(*Source);
	Result.Command.bCommitted = true;
	Result.Command.Error = ECatDomainCommandError::None;
	Result.Command.Revision = Source->Snapshot.Revision;
	TheftEscrows.Remove(TheftProtocolId);
	return Result;
}

// 偷鱼吃掉流程：只处理仍在普通追回窗口内的 escrow；售卖准备态由售卖请求继续 drain，不能被 Timer 当作进食消费。
FCatFishTheftResult UCatItemsService::CommitStolenFishConsumption(const FGuid TheftProtocolId)
{
	FCatFishTheftResult Result;
	Result.TheftProtocolId = TheftProtocolId;
	FTheftEscrowRecord* Escrow = TheftEscrows.Find(TheftProtocolId);
	Result.Command.RequestId = Escrow ? Escrow->ClientRequestId : FGuid();
	if (!Escrow)
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
		return Result;
	}
	if (Escrow->bSalePrepared)
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPhase;
		return Result;
	}
	Result.Fish = Escrow->Fish;
	Result.Command.bCommitted = true;
	Result.Command.Error = ECatDomainCommandError::None;
	if (const FContainerRecord* Source = Containers.Find(Escrow->SourceContainerId))
	{
		Result.Command.Revision = Source->Snapshot.Revision;
	}
	TheftEscrows.Remove(TheftProtocolId);
	return Result;
}

// 售卖准备流程：钱包写入前先把 escrow 锁成“同一售卖请求专用”；它不是终态，不删除鱼，也不推进容器 Revision。
FCatFishTheftResult UCatItemsService::PrepareStolenFishSale(const FGuid TheftProtocolId,
	const FGuid SaleRequestId, const FString& ThiefStableNetId)
{
	FCatFishTheftResult Result;
	Result.TheftProtocolId = TheftProtocolId;
	Result.Command.RequestId = SaleRequestId;
	if (!TheftProtocolId.IsValid() || !SaleRequestId.IsValid() || ThiefStableNetId.IsEmpty())
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	FTheftEscrowRecord* Escrow = TheftEscrows.Find(TheftProtocolId);
	if (!Escrow)
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
		return Result;
	}
	Result.Fish = Escrow->Fish;
	if (Escrow->ThiefStableNetId != ThiefStableNetId)
	{
		Result.Command.Error = ECatDomainCommandError::PermissionDenied;
		return Result;
	}
	if (const FContainerRecord* Source = Containers.Find(Escrow->SourceContainerId))
	{
		Result.Command.Revision = Source->Snapshot.Revision;
	}
	if (Escrow->bSalePrepared)
	{
		Result.Command.Error = Escrow->SaleRequestId == SaleRequestId
			? ECatDomainCommandError::AlreadyResolved : ECatDomainCommandError::InvalidPhase;
		return Result;
	}
	Escrow->bSalePrepared = true;
	Escrow->SaleRequestId = SaleRequestId;
	Result.Command.bCommitted = true;
	Result.Command.Error = ECatDomainCommandError::None;
	return Result;
}

// 售卖取消流程：只允许同一售卖请求在钱包未入账或入账失败后解除准备态；这样追回/吃掉窗口才会恢复原语义。
FCatFishTheftResult UCatItemsService::CancelPreparedStolenFishSale(const FGuid TheftProtocolId,
	const FGuid SaleRequestId, const FString& ThiefStableNetId)
{
	FCatFishTheftResult Result;
	Result.TheftProtocolId = TheftProtocolId;
	Result.Command.RequestId = SaleRequestId;
	if (!TheftProtocolId.IsValid() || !SaleRequestId.IsValid() || ThiefStableNetId.IsEmpty())
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	FTheftEscrowRecord* Escrow = TheftEscrows.Find(TheftProtocolId);
	if (!Escrow)
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
		return Result;
	}
	Result.Fish = Escrow->Fish;
	if (Escrow->ThiefStableNetId != ThiefStableNetId)
	{
		Result.Command.Error = ECatDomainCommandError::PermissionDenied;
		return Result;
	}
	if (const FContainerRecord* Source = Containers.Find(Escrow->SourceContainerId))
	{
		Result.Command.Revision = Source->Snapshot.Revision;
	}
	if (!Escrow->bSalePrepared)
	{
		Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	if (Escrow->SaleRequestId != SaleRequestId)
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPhase;
		return Result;
	}
	Escrow->bSalePrepared = false;
	Escrow->SaleRequestId = FGuid();
	Result.Command.bCommitted = true;
	Result.Command.Error = ECatDomainCommandError::None;
	return Result;
}

// 售卖 drain 流程：只删除已经由同一请求准备好的 escrow；钱包成功但 drain 失败时可重试同一 RequestId，而不能新开一笔售卖。
FCatFishTheftResult UCatItemsService::CommitPreparedStolenFishSale(const FGuid TheftProtocolId,
	const FGuid SaleRequestId, const FString& ThiefStableNetId)
{
	FCatFishTheftResult Result;
	Result.TheftProtocolId = TheftProtocolId;
	Result.Command.RequestId = SaleRequestId;
	if (!TheftProtocolId.IsValid() || !SaleRequestId.IsValid() || ThiefStableNetId.IsEmpty())
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	FTheftEscrowRecord* Escrow = TheftEscrows.Find(TheftProtocolId);
	if (!Escrow)
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
		return Result;
	}
	Result.Fish = Escrow->Fish;
	if (Escrow->ThiefStableNetId != ThiefStableNetId)
	{
		Result.Command.Error = ECatDomainCommandError::PermissionDenied;
		return Result;
	}
	if (!Escrow->bSalePrepared || Escrow->SaleRequestId != SaleRequestId)
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPhase;
		return Result;
	}
	Result.Command.bCommitted = true;
	Result.Command.Error = ECatDomainCommandError::None;
	if (const FContainerRecord* Source = Containers.Find(Escrow->SourceContainerId))
	{
		Result.Command.Revision = Source->Snapshot.Revision;
	}
	TheftEscrows.Remove(TheftProtocolId);
	return Result;
}

// Teardown 流程：永久关闭新命令，普通偷鱼 escrow 会尝试返还；偷鱼售卖准备态不强制返还，避免钱包可能已入账后又把鱼还回去。
// 容器持有（献祭预留与商店售卖冻结）在这里一律释放，不按目的区分：这两条协议都是同一次调用里同步走完
// 冻结→入账→移除，teardown 不会插进中间，所以不存在"钱已入账但冻结被 teardown 释放"的交错。
void UCatItemsService::CloseCommandsAndCancelReservations()
{
	bCommandsOpen = false;
	TArray<FGuid> OpenThefts;
	TheftEscrows.GetKeys(OpenThefts);
	for (const FGuid& TheftProtocolId : OpenThefts)
	{
		ReturnStolenFish(TheftProtocolId);
	}
	TArray<FString> CancelledRequests;
	for (const TPair<FString, FReservationRecord>& Pair : Reservations)
	{
		if (!Pair.Value.bCommitted)
		{
			ReservationByFish.Remove(Pair.Value.Fish.FishInstanceId);
			CancelledRequests.Add(Pair.Key);
		}
	}
	for (const FString& RequestKey : CancelledRequests)
	{
		Reservations.Remove(RequestKey);
	}
}

// 容器权威上下文读取流程：从服务器私有记录返回真实种类/主人，并把复制组件的 Owner 作为空间宿主；任一记录或宿主失效都整体失败，绝不从公开 DTO 猜主人。
bool UCatItemsService::TryGetContainerHost(const FGuid ContainerId, ECatContainerKind& OutKind,
	AActor*& OutAuthorityActor) const
{
	const FContainerRecord* Record = Containers.Find(ContainerId);
	UCatContainerReplicationComponent* Component = Record ? Record->ReplicationComponent.Get() : nullptr;
	AActor* AuthorityActor = Component ? Component->GetOwner() : nullptr;
	if (!Record || !AuthorityActor)
	{
		OutKind = ECatContainerKind::Unknown;
		OutAuthorityActor = nullptr;
		return false;
	}
	OutKind = Record->Snapshot.Kind;
	OutAuthorityActor = AuthorityActor;
	return true;
}

// 容器权威上下文读取流程：先复用不泄露身份的宿主查询，再仅从同一服务器记录补充私有主人；任一实体失效都整体失败。
bool UCatItemsService::TryGetContainerAuthorityContext(const FGuid ContainerId, ECatContainerKind& OutKind,
	FString& OutOwnerStableNetId, AActor*& OutAuthorityActor) const
{
	const FContainerRecord* Record = Containers.Find(ContainerId);
	if (!Record || !TryGetContainerHost(ContainerId, OutKind, OutAuthorityActor))
	{
		OutOwnerStableNetId.Reset();
		return false;
	}
	OutOwnerStableNetId = Record->OwnerStableNetId;
	return true;
}

// 快照发布流程：把服务端组合 DTO 写入精确弱组件；宿主已销毁时只保留服务器事实，绝不回滚已提交事务。
void UCatItemsService::PublishContainer(FContainerRecord& Record)
{
	if (UCatContainerReplicationComponent* Component = Record.ReplicationComponent.Get())
	{
		Component->SetSnapshotFromAuthority(Record.Snapshot);
	}
}

// 终态键流程：仅在服务器内组合身份、操作、聚合 ID 和 RequestId；相同 RequestId 可安全用于另一容器，而同一聚合重试仍只读首次终态。
FString UCatItemsService::MakeTerminalKey(const FString& StableNetId, const TCHAR* Operation,
	const FGuid& AggregateId, const FGuid& RequestId)
{
	return FString::Printf(TEXT("%s|%s|%s|%s"), *StableNetId, Operation,
		*AggregateId.ToString(EGuidFormats::DigitsWithHyphens),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
}

// 持有键流程：把可逆锁精确绑定到持有目的、服务器身份、容器聚合与 RequestId；提交和取消必须同时持有四者，
// 不能用裸 RequestId 解锁他人记录，也不能用献祭的 RequestId 去 drain 一笔售卖冻结。
FString UCatItemsService::MakeReservationKey(const FString& StableNetId, const FGuid& ContainerId,
	const FGuid& RequestId, const TCHAR* HoldPurpose)
{
	return FString::Printf(TEXT("%s|%s|%s|%s"), HoldPurpose, *StableNetId,
		*ContainerId.ToString(EGuidFormats::DigitsWithHyphens),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
}

// 返还槽位统计流程：遍历仍在进行的 escrow 并计算精确源容器数量；容量检查把这些鱼视为仍占位，保证追回无需挤掉别的鱼。
int32 UCatItemsService::CountReservedReturnSlots(const FGuid ContainerId) const
{
	int32 Count = 0;
	for (const TPair<FGuid, FTheftEscrowRecord>& Pair : TheftEscrows)
	{
		if (Pair.Value.SourceContainerId == ContainerId)
		{
			++Count;
		}
	}
	return Count;
}
