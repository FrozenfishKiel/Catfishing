#include "Integration/Fishing/CatFishingFightCursorLedger.h"

#include "Integration/Fishing/CatFishingBoundaryHash.h"

// 接受流程：先按 Attempt/cursor 查既有记录，保证同 cursor 重放不受当前 LastCursor 或 seal 状态误伤；
// 只有新 cursor 才检查 final seal、Last+1 和 Journal。
FCatFishingBoundaryResultHeader FCatFishingFightCursorLedger::AcceptOrReplay(
	const FCatFishingFightExchangeRequest& Request,
	const FCatFishingPayloadHash& PayloadHash)
{
	if (!Request.AttemptId.Value.IsValid()
		|| !Request.RequestId.IsValid()
		|| Request.PrincipalId.CanonicalValue.IsEmpty()
		|| Request.Cursor <= 0
		|| !FMath::IsFinite(Request.FishStaminaCost)
		|| Request.FishStaminaCost <= 0.0
		|| !FMath::IsFinite(Request.ParticipantStaminaCost)
		|| Request.ParticipantStaminaCost <= 0.0
		|| !FMath::IsFinite(Request.RodDurabilityCost)
		|| Request.RodDurabilityCost <= 0.0
		|| PayloadHash.Bytes.Num() != 32)
	{
		return Reject(Request.AttemptId, PayloadHash, Request.ExpectedRevision, ECatFishingBoundaryError::InvalidRequest);
	}

	FAttemptState& State = Attempts.FindOrAdd(Request.AttemptId.Value);
	if (const FCursorRecord* ExistingCursor = State.RecordsByCursor.Find(Request.Cursor))
	{
		if (ExistingCursor->PayloadHash != PayloadHash)
		{
			return Reject(Request.AttemptId, PayloadHash, Request.ExpectedRevision, ECatFishingBoundaryError::PayloadMismatch);
		}

		FCatFishingBoundaryResultHeader Replay = ExistingCursor->Result;
		Replay.bReplay = true;
		return Replay;
	}

	if (State.bFinalCursorSealed && Request.Cursor > State.FinalCursor)
	{
		return Reject(Request.AttemptId, PayloadHash, Request.ExpectedRevision, ECatFishingBoundaryError::AlreadySettled);
	}

	const int64 ExpectedNextCursor = State.LastCursor + 1;
	if (Request.Cursor < ExpectedNextCursor)
	{
		return Reject(Request.AttemptId, PayloadHash, Request.ExpectedRevision, ECatFishingBoundaryError::InvalidOrder);
	}
	if (Request.Cursor > ExpectedNextCursor)
	{
		return Reject(Request.AttemptId, PayloadHash, Request.ExpectedRevision, ECatFishingBoundaryError::CursorGap);
	}

	const FCatFishingBoundaryResultHeader Accepted = Journal.AcceptOrReplay(MakeFightJournalRequest(Request, PayloadHash));
	if (Accepted.Disposition == ECatFishingBoundaryDisposition::Pending)
	{
		FCursorRecord Record;
		Record.PayloadHash = PayloadHash;
		Record.Result = Accepted;
		State.RecordsByCursor.Add(Request.Cursor, Record);
		State.LastCursor = Request.Cursor;
	}
	return Accepted;
}


// 资源提交流程：先按 OperationKey 查首次 Fight result；未命中时只允许 Pending operation 写入 Committed 并发行一份 FightResourcesApplied Receipt。
FCatFishingFightResult FCatFishingFightCursorLedger::CommitResourcesApplied(
	const FCatFishingOperationKey& Operation,
	const int64 DomainRevision,
	const bool bRodBroken)
{
	const FString OperationCacheKey = Operation.ToCacheKey();
	if (const FCatFishingFightResult* Cached = FightResultByOperationKey.Find(OperationCacheKey))
	{
		FCatFishingFightResult Replay = *Cached;
		Replay.Header.bReplay = true;
		return Replay;
	}

	FCatFishingFightResult Result;
	FCatFishingBoundaryResultHeader Pending;
	if (!Journal.TryPoll(Operation, Pending) || Pending.Disposition != ECatFishingBoundaryDisposition::Pending)
	{
		Result.Header = Reject(Operation.AttemptId, Pending.PayloadHash, DomainRevision, ECatFishingBoundaryError::OperationNotFound);
		return Result;
	}

	FCatFishingBoundaryResultHeader Committed = Pending;
	Committed.Disposition = ECatFishingBoundaryDisposition::Committed;
	Committed.Error = ECatFishingBoundaryError::None;
	Committed.Revision = DomainRevision;
	if (!Journal.CommitResult(Operation, Committed))
	{
		Result.Header = Reject(Operation.AttemptId, Pending.PayloadHash, DomainRevision, ECatFishingBoundaryError::DependencyUnavailable);
		return Result;
	}

	FCatFishingDomainReceipt Receipt;
	Receipt.ReceiptId.Value = FGuid::NewGuid();
	Receipt.Operation = Operation;
	Receipt.Kind = ECatFishingReceiptKind::FightResourcesApplied;
	Receipt.PayloadHash = Pending.PayloadHash;
	Receipt.DomainRevision = DomainRevision;

	Result.Header = Committed;
	Result.Receipts.Add(Receipt);
	Result.bRodBroken = bRodBroken;
	FightResultByOperationKey.Add(OperationCacheKey, Result);
	return Result;
}
// Seal 流程：Final cursor 只允许封存在已接受范围内；只有同一个最终 cursor 的重复 seal 才返回首次结果，首次成功会提交
// Committed 并阻止后续更大 cursor。
FCatFishingBoundaryResultHeader FCatFishingFightCursorLedger::SealFinalCursor(
	const FCatFishingFinalFightCursor& FinalCursor,
	const FGuid RequestId)
{
	const FCatFishingPayloadHash PayloadHash = MakeFinalCursorPayloadHash(FinalCursor);
	FAttemptState* State = Attempts.Find(FinalCursor.AttemptId.Value);
	if (!FinalCursor.AttemptId.Value.IsValid() || !RequestId.IsValid() || FinalCursor.Cursor <= 0 || !State)
	{
		return Reject(FinalCursor.AttemptId, PayloadHash, FinalCursor.Cursor, ECatFishingBoundaryError::InvalidRequest);
	}

	if (State->bFinalCursorSealed)
	{
		if (FinalCursor.Cursor != State->FinalCursor)
		{
			return Reject(FinalCursor.AttemptId, PayloadHash, FinalCursor.Cursor, ECatFishingBoundaryError::AlreadySettled);
		}
		FCatFishingBoundaryResultHeader Replay = State->FinalSealResult;
		Replay.bReplay = true;
		return Replay;
	}

	if (FinalCursor.Cursor > State->LastCursor)
	{
		return Reject(FinalCursor.AttemptId, PayloadHash, FinalCursor.Cursor, ECatFishingBoundaryError::InvalidOrder);
	}

	FCatFishingFightExchangeRequest SealRequest;
	SealRequest.RequestId = RequestId;
	SealRequest.AttemptId = FinalCursor.AttemptId;
	SealRequest.PrincipalId.CanonicalValue = TEXT("system:fight-final-cursor");
	SealRequest.Cursor = FinalCursor.Cursor;
	SealRequest.ExpectedRevision = FinalCursor.Cursor;
	SealRequest.FishStaminaCost = 1.0;
	SealRequest.ParticipantStaminaCost = 1.0;
	SealRequest.RodDurabilityCost = 1.0;
	FCatFishingBoundaryResultHeader Accepted = Journal.AcceptOrReplay(MakeFightJournalRequest(SealRequest, PayloadHash));
	if (Accepted.Disposition != ECatFishingBoundaryDisposition::Pending)
	{
		return Accepted;
	}

	FCatFishingBoundaryResultHeader Committed = Accepted;
	Committed.Disposition = ECatFishingBoundaryDisposition::Committed;
	Committed.Error = ECatFishingBoundaryError::None;
	Committed.Revision = FinalCursor.Cursor;
	if (Journal.CommitResult(Accepted.Operation, Committed))
	{
		State->bFinalCursorSealed = true;
		State->FinalCursor = FinalCursor.Cursor;
		State->FinalSealResult = Committed;
		return Committed;
	}
	return Reject(FinalCursor.AttemptId, PayloadHash, FinalCursor.Cursor, ECatFishingBoundaryError::DependencyUnavailable);
}

// 拒绝构造流程：拒绝头不携带 OperationId，也不会推进 LastCursor 或 FinalCursor。
// Close 流程：只关闭底层 Journal 的新 operation 入口；cursor 记录本身保留，以便已接受帧继续按 replay 规则返回。
void FCatFishingFightCursorLedger::CloseAttempt(const FCatFishingAttemptId& AttemptId)
{
	Journal.CloseAttempt(AttemptId);
}
FCatFishingBoundaryResultHeader FCatFishingFightCursorLedger::Reject(
	const FCatFishingAttemptId& AttemptId,
	const FCatFishingPayloadHash& PayloadHash,
	const int64 Revision,
	const ECatFishingBoundaryError Error)
{
	FCatFishingBoundaryResultHeader Result;
	Result.SchemaVersion = 1;
	Result.Disposition = ECatFishingBoundaryDisposition::Rejected;
	Result.Error = Error;
	Result.Operation.AttemptId = AttemptId;
	Result.PayloadHash = PayloadHash;
	Result.Revision = Revision;
	return Result;
}

// Journal 请求构造流程：Fight 的 RequestId 与 Cursor 分离，RequestId 管重放身份，PayloadHash 管 cursor 业务语义。
FCatFishingJournalRequest FCatFishingFightCursorLedger::MakeFightJournalRequest(
	const FCatFishingFightExchangeRequest& Request,
	const FCatFishingPayloadHash& PayloadHash)
{
	FCatFishingJournalRequest JournalRequest;
	JournalRequest.OperationKind = ECatFishingBoundaryOperationKind::Fight;
	JournalRequest.Header.SchemaVersion = 1;
	JournalRequest.Header.AttemptId = Request.AttemptId;
	JournalRequest.Header.RequestId.Value = Request.RequestId;
	JournalRequest.Header.PrincipalId = Request.PrincipalId;
	JournalRequest.Header.ExpectedRevision = Request.ExpectedRevision;
	JournalRequest.PayloadHash = PayloadHash;
	return JournalRequest;
}

// Seal Hash 流程：只把 Attempt 和最终 cursor 编进 canonical bytes，确保同一 seal 请求跨 RequestId 重放仍描述同一边界。
FCatFishingPayloadHash FCatFishingFightCursorLedger::MakeFinalCursorPayloadHash(
	const FCatFishingFinalFightCursor& FinalCursor)
{
	TArray<uint8> Payload;
	FCatFishingBoundaryHash::AppendGuid(Payload, FinalCursor.AttemptId.Value);
	FCatFishingBoundaryHash::AppendInt64(Payload, FinalCursor.Cursor);

	FCatFishingBoundaryRequestHeader Header;
	Header.SchemaVersion = 1;
	Header.AttemptId = FinalCursor.AttemptId;
	Header.RequestId.Value = FGuid();
	Header.PrincipalId.CanonicalValue = TEXT("system:fight-final-cursor");
	Header.ExpectedRevision = FinalCursor.Cursor;
	return FCatFishingBoundaryHash::HashOperation(ECatFishingBoundaryOperationKind::Fight, Header, Payload);
}
