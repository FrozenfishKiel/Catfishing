#include "Integration/Fishing/CatFishingOperationJournal.h"

// 接受流程：先按幂等键查既有记录，保证 Close 后旧请求仍可稳定重放；只有首次新请求才检查 Attempt 是否关闭并分配 OperationId。
FCatFishingBoundaryResultHeader FCatFishingOperationJournal::AcceptOrReplay(const FCatFishingJournalRequest& Request)
{
	FCatFishingBoundaryResultHeader Rejected;
	Rejected.SchemaVersion = Request.Header.SchemaVersion;
	Rejected.PayloadHash = Request.PayloadHash;
	Rejected.Revision = Request.Header.ExpectedRevision;

	const FString RequestKey = MakeRequestKey(Request);
	if (FEntry* Existing = EntriesByRequestKey.Find(RequestKey))
	{
		if (Existing->Header.PrincipalId.CanonicalValue != Request.Header.PrincipalId.CanonicalValue)
		{
			Rejected.Disposition = ECatFishingBoundaryDisposition::Rejected;
			Rejected.Error = ECatFishingBoundaryError::InvalidIdentity;
			Rejected.bReplay = true;
			return Rejected;
		}

		if (Existing->Header.SchemaVersion != Request.Header.SchemaVersion
			|| Existing->Header.ExpectedRevision != Request.Header.ExpectedRevision
			|| Existing->PayloadHash != Request.PayloadHash)
		{
			Rejected.Disposition = ECatFishingBoundaryDisposition::Rejected;
			Rejected.Error = ECatFishingBoundaryError::PayloadMismatch;
			Rejected.bReplay = true;
			return Rejected;
		}

		FCatFishingBoundaryResultHeader Replay = Existing->Result;
		Replay.bReplay = true;
		return Replay;
	}

	if (!Request.Header.AttemptId.Value.IsValid()
		|| !Request.Header.RequestId.Value.IsValid()
		|| Request.Header.PrincipalId.CanonicalValue.IsEmpty()
		|| Request.PayloadHash.Bytes.Num() != 32)
	{
		Rejected.Disposition = ECatFishingBoundaryDisposition::Rejected;
		Rejected.Error = ECatFishingBoundaryError::InvalidRequest;
		return Rejected;
	}

	if (IsAttemptClosed(Request.Header.AttemptId))
	{
		Rejected.Disposition = ECatFishingBoundaryDisposition::Rejected;
		Rejected.Error = ECatFishingBoundaryError::AttemptClosed;
		return Rejected;
	}

	FEntry NewEntry;
	NewEntry.OperationKind = Request.OperationKind;
	NewEntry.Header = Request.Header;
	NewEntry.PayloadHash = Request.PayloadHash;
	NewEntry.Result.SchemaVersion = Request.Header.SchemaVersion;
	NewEntry.Result.Disposition = ECatFishingBoundaryDisposition::Pending;
	NewEntry.Result.Error = ECatFishingBoundaryError::None;
	NewEntry.Result.Operation.AttemptId = Request.Header.AttemptId;
	NewEntry.Result.Operation.OperationId = MakeOperationId();
	NewEntry.Result.PayloadHash = Request.PayloadHash;
	NewEntry.Result.Revision = Request.Header.ExpectedRevision;
	NewEntry.Result.bReplay = false;

	RequestKeyByOperationKey.Add(NewEntry.Result.Operation.ToCacheKey(), RequestKey);
	EntriesByRequestKey.Add(RequestKey, NewEntry);
	return NewEntry.Result;
}
// 提交流程：只允许 Pending entry 写入第一个合法终态；Disposition/Error 组合非法、二次提交或 Pending 降级都会破坏 Inbox 稳定性。
bool FCatFishingOperationJournal::CommitResult(
	const FCatFishingOperationKey& Operation,
	const FCatFishingBoundaryResultHeader& Result)
{
	const FString OperationKey = Operation.ToCacheKey();
	const FString* RequestKey = RequestKeyByOperationKey.Find(OperationKey);
	if (!RequestKey)
	{
		return false;
	}

	FEntry* Entry = EntriesByRequestKey.Find(*RequestKey);
	if (!Entry)
	{
		return false;
	}

	if (Entry->Result.Disposition != ECatFishingBoundaryDisposition::Pending
		|| Result.Disposition == ECatFishingBoundaryDisposition::Pending)
	{
		return false;
	}

	const bool bValidCommittedResult = Result.Disposition == ECatFishingBoundaryDisposition::Committed
		&& Result.Error == ECatFishingBoundaryError::None;
	const bool bValidRejectedResult = Result.Disposition == ECatFishingBoundaryDisposition::Rejected
		&& Result.Error != ECatFishingBoundaryError::None;
	if (!bValidCommittedResult && !bValidRejectedResult)
	{
		return false;
	}

	Entry->Result = Result;
	Entry->Result.SchemaVersion = Entry->Header.SchemaVersion;
	Entry->Result.Operation = Operation;
	Entry->Result.PayloadHash = Entry->PayloadHash;
	Entry->Result.bReplay = false;
	return true;
}
// Poll 流程：只读已存在的 Inbox entry；没有命中时返回 false，让 public Poll 包装成 OperationNotFound。
bool FCatFishingOperationJournal::TryPoll(
	const FCatFishingOperationKey& Operation,
	FCatFishingBoundaryResultHeader& OutResult) const
{
	const FString OperationKey = Operation.ToCacheKey();
	const FString* RequestKey = RequestKeyByOperationKey.Find(OperationKey);
	if (!RequestKey)
	{
		return false;
	}

	const FEntry* Entry = EntriesByRequestKey.Find(*RequestKey);
	if (!Entry)
	{
		return false;
	}

	OutResult = Entry->Result;
	OutResult.bReplay = true;
	return true;
}

// Close 流程：只记录 Attempt 已关闭；不可逆点和 drain 语义会由后续 Chum/Capture stage 在现有 entry 上继续推进。
void FCatFishingOperationJournal::CloseAttempt(const FCatFishingAttemptId& AttemptId)
{
	if (AttemptId.Value.IsValid())
	{
		ClosedAttempts.Add(AttemptId.Value);
	}
}

// Key 规则：幂等键只锁定 OperationKind、Attempt 和 RequestId；Principal 留在记录内比较，避免身份漂移绕开同请求重放检查。
FString FCatFishingOperationJournal::MakeRequestKey(const FCatFishingJournalRequest& Request)
{
	return FString::Printf(TEXT("%u|%s|%s"),
		static_cast<uint8>(Request.OperationKind),
		*Request.Header.AttemptId.Value.ToString(EGuidFormats::Digits),
		*Request.Header.RequestId.Value.ToString(EGuidFormats::Digits));
}
// 查询流程：关闭集合只接受有效 AttemptId；空 Attempt 会在请求校验阶段按 InvalidRequest 处理。
bool FCatFishingOperationJournal::IsAttemptClosed(const FCatFishingAttemptId& AttemptId) const
{
	return AttemptId.Value.IsValid() && ClosedAttempts.Contains(AttemptId.Value);
}

// ID 生成流程：当前 Atom A 只承诺 run-local 非空唯一；跨进程恢复和可排序 ID 不属于本原子。
FCatFishingOperationId FCatFishingOperationJournal::MakeOperationId()
{
	FCatFishingOperationId OperationId;
	OperationId.Value = FGuid::NewGuid();
	return OperationId;
}

