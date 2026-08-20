#include "Collection/CatImprintMediaTransportService.h"

#include "Collection/CatImprintMediaSettings.h"
#include "Engine/World.h"
#include "Framework/Core/CatSha256.h"

namespace
{
	/**
	 * 把 32 字节 digest 摊成 64 个小写十六进制字符。
	 * 十六进制只有媒体这一侧需要：Manifest 与 ACK 都以字符串形式跨端比较分块摘要，所以格式化留在领域这一层，
	 * 不下沉到共享的哈希原语里。
	 */
	FString CatImprintDigestToHex(const TArray<uint8>& Digest)
	{
		static constexpr TCHAR HexDigits[] = TEXT("0123456789abcdef");
		FString Hex;
		Hex.Reserve(Digest.Num() * 2);
		for (const uint8 Byte : Digest)
		{
			Hex.AppendChar(HexDigits[(Byte >> 4) & 0x0F]);
			Hex.AppendChar(HexDigits[Byte & 0x0F]);
		}
		return Hex;
	}
}
// 创建条件流程：媒体传输只存在于 authority Game World；客户端本地文件通过 Profile/外部桥处理。
bool UCatImprintMediaTransportService::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

// 反初始化流程：关闭命令门并丢弃本局内存字节、收件人 cursor 与幂等缓存；不会伪造任何 Profile ACK。
void UCatImprintMediaTransportService::Deinitialize()
{
	bCommandsOpen = false;
	Transfers.Reset();
	TransferByPlanKey.Reset();
	TerminalCache.Reset();
	TerminalPayloadSignatures.Reset();
	Super::Deinitialize();
}

// Begin 流程：先检查配置、计划和收件人授权，再按 Candidate/Album 建立唯一 MediaId；相同请求或相同共同事件都只回放既有 ID。
FCatImprintMediaResult UCatImprintMediaTransportService::BeginHostMediaTransfer(const FGuid RequestId,
	const FCatCapturePlan& Plan, const FString& HostStableNetId,
	const TArray<FCatImprintMediaRecipientAuthorization>& Recipients)
{
	const FString TerminalKey = MakeTerminalKey(HostStableNetId, TEXT("BeginMedia"), Plan.CapturePlanId, RequestId);
	const FString PayloadSignature = MakeBeginPayloadSignature(Plan, Recipients);
	FCatImprintMediaResult ReplayResult;
	if (TryResolveTerminalReplay(TerminalKey, PayloadSignature, ReplayResult))
	{
		return ReplayResult;
	}

	FCatImprintMediaResult Result;
	Result.RequestId = RequestId;
	const UCatImprintMediaSettings* Settings = GetDefault<UCatImprintMediaSettings>();
	if (!RequestId.IsValid() || HostStableNetId.IsEmpty() || !IsPlanComplete(Plan))
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (!bCommandsOpen)
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (!Settings || !Settings->IsRuntimeReady())
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
	}
	else if (Recipients.IsEmpty())
	{
		Result.Error = ECatDomainCommandError::PermissionDenied;
	}
	else if (const FGuid* ExistingMediaId = TransferByPlanKey.Find(MakePlanKey(Plan)))
	{
		const FTransferRecord* Existing = Transfers.Find(*ExistingMediaId);
		Result = MakeResult(Existing, RequestId, ECatDomainCommandError::AlreadyResolved);
	}
	else
	{
		TMap<FString, FRecipientRecord> RecipientRecords;
		bool bRecipientsValid = true;
		for (const FCatImprintMediaRecipientAuthorization& Authorization : Recipients)
		{
			if (Authorization.RecipientStableNetId.IsEmpty() || Authorization.MembershipRevision < 0
				|| Authorization.PermissionRevision < 0 || RecipientRecords.Contains(Authorization.RecipientStableNetId))
			{
				bRecipientsValid = false;
				break;
			}
			FRecipientRecord Record;
			Record.Authorization = Authorization;
			RecipientRecords.Add(Authorization.RecipientStableNetId, MoveTemp(Record));
		}
		if (!bRecipientsValid)
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
		}
		else
		{
			FTransferRecord Transfer;
			Transfer.MediaId = FGuid::NewGuid();
			Transfer.SourcePlan = Plan;
			Transfer.HostStableNetId = HostStableNetId;
			Transfer.Recipients = MoveTemp(RecipientRecords);
			Transfer.Revision = 1;
			const FGuid MediaId = Transfer.MediaId;
			FTransferRecord& Stored = Transfers.Add(MediaId, MoveTemp(Transfer));
			TransferByPlanKey.Add(MakePlanKey(Plan), MediaId);
			Result = MakeResult(&Stored, RequestId, ECatDomainCommandError::None);
			Result.bCommitted = true;
		}
	}
	CacheTerminalResult(TerminalKey, PayloadSignature, Result);
	return Result;
}

// Manifest 流程：Host 身份、Manifest 内容和容量边界全部通过后冻结事实；冻结后不同 Manifest 只能报冲突，不能覆盖。
FCatImprintMediaResult UCatImprintMediaTransportService::CommitHostMediaManifest(const FGuid RequestId,
	const FString& HostStableNetId, const FCatImprintMediaManifest& Manifest)
{
	const FString TerminalKey = MakeTerminalKey(HostStableNetId, TEXT("Manifest"), Manifest.MediaId, RequestId);
	const FString PayloadSignature = MakeManifestPayloadSignature(Manifest);
	FCatImprintMediaResult ReplayResult;
	if (TryResolveTerminalReplay(TerminalKey, PayloadSignature, ReplayResult))
	{
		return ReplayResult;
	}

	FTransferRecord* Record = Transfers.Find(Manifest.MediaId);
	FCatImprintMediaResult Result = MakeResult(Record, RequestId, ECatDomainCommandError::NotFound);
	if (!RequestId.IsValid() || !Manifest.MediaId.IsValid() || HostStableNetId.IsEmpty())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (!Record)
	{
		Result.Error = ECatDomainCommandError::NotFound;
	}
	else if (!bCommandsOpen)
	{
		Result = MakeResult(Record, RequestId, ECatDomainCommandError::CommandsClosed);
	}
	else if (Record->bFailed)
	{
		Result = MakeResult(Record, RequestId, ECatDomainCommandError::AlreadyResolved);
	}
	else if (Record->HostStableNetId != HostStableNetId)
	{
		Result = MakeResult(Record, RequestId, ECatDomainCommandError::PermissionDenied);
	}
	else
	{
		FCatImprintMediaManifest NormalizedManifest;
		const ECatDomainCommandError ManifestError = ValidateManifest(*Record, Manifest, NormalizedManifest);
		if (ManifestError != ECatDomainCommandError::None)
		{
			Result = MakeResult(Record, RequestId, ManifestError);
		}
		else if (Record->bManifestCommitted)
		{
			Result = MakeResult(Record, RequestId,
				AreManifestsEquivalent(Record->Manifest, NormalizedManifest)
				? ECatDomainCommandError::AlreadyResolved : ECatDomainCommandError::RevisionConflict);
		}
		else
		{
			Record->Manifest = MoveTemp(NormalizedManifest);
			Record->bManifestCommitted = true;
			++Record->Revision;
			Result = MakeResult(Record, RequestId, ECatDomainCommandError::None);
			Result.bCommitted = true;
		}
	}
	CacheTerminalResult(TerminalKey, PayloadSignature, Result);
	return Result;
}

// Chunk 流程：Host 只能提交 Manifest 声明范围内的块；重复同字节块安全回放，不同字节块拒绝并保留原记录。
FCatImprintMediaResult UCatImprintMediaTransportService::CommitHostMediaChunk(const FGuid RequestId,
	const FString& HostStableNetId, const FGuid MediaId, const int32 ChunkIndex, const TArray<uint8>& Bytes)
{
	const FString TerminalKey = MakeTerminalKey(HostStableNetId, TEXT("Chunk"), MediaId, RequestId, ChunkIndex);
	const FString PayloadSignature = MakeChunkPayloadSignature(Bytes);
	FCatImprintMediaResult ReplayResult;
	if (TryResolveTerminalReplay(TerminalKey, PayloadSignature, ReplayResult))
	{
		return ReplayResult;
	}

	FTransferRecord* Record = Transfers.Find(MediaId);
	FCatImprintMediaResult Result = MakeResult(Record, RequestId, ECatDomainCommandError::NotFound);
	if (!RequestId.IsValid() || !MediaId.IsValid() || HostStableNetId.IsEmpty())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (!Record)
	{
		Result.Error = ECatDomainCommandError::NotFound;
	}
	else if (!bCommandsOpen)
	{
		Result = MakeResult(Record, RequestId, ECatDomainCommandError::CommandsClosed);
	}
	else if (Record->bFailed)
	{
		Result = MakeResult(Record, RequestId, ECatDomainCommandError::AlreadyResolved);
	}
	else if (Record->HostStableNetId != HostStableNetId)
	{
		Result = MakeResult(Record, RequestId, ECatDomainCommandError::PermissionDenied);
	}
	else if (!Record->bManifestCommitted || ChunkIndex < 0 || ChunkIndex >= Record->Manifest.ChunkCount)
	{
		Result = MakeResult(Record, RequestId, ECatDomainCommandError::InvalidPhase);
	}
	else if (Bytes.Num() != GetExpectedChunkSize(Record->Manifest, ChunkIndex))
	{
		Result = MakeResult(Record, RequestId, ECatDomainCommandError::InvalidPayload);
	}
	else
	{
		const FString ChunkHash = ComputePayloadHashHex(Bytes);
		if (!IsSha256Hex(ChunkHash))
		{
			Result = MakeResult(Record, RequestId, ECatDomainCommandError::DependencyUnavailable);
		}
		else if (const FString* ExistingHash = Record->ChunkHashByIndex.Find(ChunkIndex))
		{
			Result = MakeResult(Record, RequestId,
				*ExistingHash == ChunkHash ? ECatDomainCommandError::AlreadyResolved : ECatDomainCommandError::RevisionConflict);
		}
		else
		{
			Record->ChunksByIndex.Add(ChunkIndex, Bytes);
			Record->ChunkHashByIndex.Add(ChunkIndex, ChunkHash);
			++Record->Revision;
			if (!TryFinalizeTransfer(*Record))
			{
				Record->bFailed = true;
				++Record->Revision;
				Result = MakeResult(Record, RequestId, ECatDomainCommandError::InvalidPayload);
			}
			else
			{
				Result = MakeResult(Record, RequestId, ECatDomainCommandError::None);
				Result.bCommitted = true;
			}
		}
	}
	CacheTerminalResult(TerminalKey, PayloadSignature, Result);
	return Result;
}
// 读取流程：每次读取都重新校验收件人版本和 cursor；只返回下一块，不能靠旧 cursor 跳读后续块。
FCatImprintMediaResult UCatImprintMediaTransportService::ReadRecipientChunk(const FGuid MediaId,
	const FString& RecipientStableNetId, const int64 MembershipRevision, const int64 PermissionRevision,
	const int32 ChunkIndex, FCatImprintMediaChunk& OutChunk) const
{
	OutChunk = FCatImprintMediaChunk();
	const FTransferRecord* Record = Transfers.Find(MediaId);
	FCatImprintMediaResult Result = MakeResult(Record, FGuid(), ECatDomainCommandError::NotFound);
	const FRecipientRecord* Recipient = nullptr;
	if (!Record)
	{
		Result.Error = ECatDomainCommandError::NotFound;
	}
	else if (Record->bFailed || !Record->bReadyForRecipients)
	{
		Result = MakeResult(Record, FGuid(), ECatDomainCommandError::InvalidPhase);
	}
	else if (const ECatDomainCommandError AuthError = ValidateRecipient(*Record, RecipientStableNetId,
		MembershipRevision, PermissionRevision, Recipient); AuthError != ECatDomainCommandError::None)
	{
		Result = MakeResult(Record, FGuid(), AuthError);
	}
	else if (Recipient->bComplete)
	{
		Result = MakeResult(Record, FGuid(), ECatDomainCommandError::AlreadyResolved);
		Result.bRecipientComplete = true;
		Result.NextChunkIndex = Recipient->NextChunkIndex;
	}
	else if (ChunkIndex != Recipient->NextChunkIndex)
	{
		Result = MakeResult(Record, FGuid(), ECatDomainCommandError::RevisionConflict);
		Result.NextChunkIndex = Recipient->NextChunkIndex;
	}
	else if (const TArray<uint8>* Bytes = Record->ChunksByIndex.Find(ChunkIndex))
	{
		OutChunk.MediaId = MediaId;
		OutChunk.ChunkIndex = ChunkIndex;
		OutChunk.Bytes = *Bytes;
		OutChunk.Sha256Hex = Record->ChunkHashByIndex.FindRef(ChunkIndex);
		Result = MakeResult(Record, FGuid(), ECatDomainCommandError::None);
		Result.NextChunkIndex = Recipient->NextChunkIndex;
	}
	else
	{
		Result = MakeResult(Record, FGuid(), ECatDomainCommandError::DependencyUnavailable);
	}
	return Result;
}

// ACK 流程：收件人只能按当前 cursor 顺序确认同一块 hash；每个 ACK 推进自己的 cursor，不影响其他人。
FCatImprintMediaResult UCatImprintMediaTransportService::AcknowledgeRecipientChunk(const FGuid RequestId,
	const FGuid MediaId, const FString& RecipientStableNetId, const int64 MembershipRevision,
	const int64 PermissionRevision, const int32 ChunkIndex, const FString& ChunkHashHex)
{
	const FString TerminalKey = MakeTerminalKey(RecipientStableNetId, TEXT("AckChunk"), MediaId, RequestId, ChunkIndex);
	const FString PayloadSignature = MakeAckPayloadSignature(MembershipRevision, PermissionRevision, ChunkHashHex);
	FCatImprintMediaResult ReplayResult;
	if (TryResolveTerminalReplay(TerminalKey, PayloadSignature, ReplayResult))
	{
		return ReplayResult;
	}

	FTransferRecord* Record = Transfers.Find(MediaId);
	FCatImprintMediaResult Result = MakeResult(Record, RequestId, ECatDomainCommandError::NotFound);
	FRecipientRecord* Recipient = nullptr;
	const FString NormalizedChunkHashHex = ChunkHashHex.TrimStartAndEnd().ToLower();
	if (!RequestId.IsValid() || !MediaId.IsValid() || RecipientStableNetId.IsEmpty())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (!Record)
	{
		Result.Error = ECatDomainCommandError::NotFound;
	}
	else if (!bCommandsOpen)
	{
		Result = MakeResult(Record, RequestId, ECatDomainCommandError::CommandsClosed);
	}
	else if (Record->bFailed || !Record->bReadyForRecipients)
	{
		Result = MakeResult(Record, RequestId, ECatDomainCommandError::InvalidPhase);
	}
	else if (const ECatDomainCommandError AuthError = ValidateRecipientMutable(*Record, RecipientStableNetId,
		MembershipRevision, PermissionRevision, Recipient); AuthError != ECatDomainCommandError::None)
	{
		Result = MakeResult(Record, RequestId, AuthError);
	}
	else if (ChunkIndex < 0 || !IsSha256Hex(NormalizedChunkHashHex))
	{
		Result = MakeResult(Record, RequestId, ECatDomainCommandError::InvalidPayload);
		Result.NextChunkIndex = Recipient->NextChunkIndex;
	}
	else if (ChunkIndex < Recipient->NextChunkIndex)
	{
		const FString StoredHash = Record->ChunkHashByIndex.FindRef(ChunkIndex);
		Result = MakeResult(Record, RequestId,
			StoredHash == NormalizedChunkHashHex
			? ECatDomainCommandError::AlreadyResolved : ECatDomainCommandError::RevisionConflict);
		Result.bRecipientComplete = Recipient->bComplete;
		Result.NextChunkIndex = Recipient->NextChunkIndex;
	}
	else if (ChunkIndex != Recipient->NextChunkIndex)
	{
		Result = MakeResult(Record, RequestId, ECatDomainCommandError::RevisionConflict);
		Result.NextChunkIndex = Recipient->NextChunkIndex;
	}
	else if (Record->ChunkHashByIndex.FindRef(ChunkIndex) != NormalizedChunkHashHex)
	{
		Result = MakeResult(Record, RequestId, ECatDomainCommandError::InvalidPayload);
		Result.NextChunkIndex = Recipient->NextChunkIndex;
	}
	else
	{
		Recipient->AcknowledgedBytes += Record->ChunksByIndex.FindRef(ChunkIndex).Num();
		++Recipient->NextChunkIndex;
		Recipient->bComplete = Recipient->NextChunkIndex >= Record->Manifest.ChunkCount;
		++Record->Revision;
		Result = MakeResult(Record, RequestId, ECatDomainCommandError::None);
		Result.bCommitted = true;
		Result.bRecipientComplete = Recipient->bComplete;
		Result.NextChunkIndex = Recipient->NextChunkIndex;
	}
	CacheTerminalResult(TerminalKey, PayloadSignature, Result);
	return Result;
}

// Cursor 流程：清空输出后重新校验授权；成功时只复制该收件人的进度，不暴露其他收件人状态。
FCatImprintMediaResult UCatImprintMediaTransportService::GetRecipientCursor(const FGuid MediaId,
	const FString& RecipientStableNetId, const int64 MembershipRevision, const int64 PermissionRevision,
	FCatImprintMediaCursor& OutCursor) const
{
	OutCursor = FCatImprintMediaCursor();
	const FTransferRecord* Record = Transfers.Find(MediaId);
	FCatImprintMediaResult Result = MakeResult(Record, FGuid(), ECatDomainCommandError::NotFound);
	const FRecipientRecord* Recipient = nullptr;
	if (!Record)
	{
		Result.Error = ECatDomainCommandError::NotFound;
	}
	else if (const ECatDomainCommandError AuthError = ValidateRecipient(*Record, RecipientStableNetId,
		MembershipRevision, PermissionRevision, Recipient); AuthError != ECatDomainCommandError::None)
	{
		Result = MakeResult(Record, FGuid(), AuthError);
	}
	else
	{
		OutCursor.MediaId = MediaId;
		OutCursor.RecipientStableNetId = RecipientStableNetId;
		OutCursor.NextChunkIndex = Recipient->NextChunkIndex;
		OutCursor.ChunkCount = Record->Manifest.ChunkCount;
		OutCursor.AcknowledgedBytes = Recipient->AcknowledgedBytes;
		OutCursor.TotalSizeBytes = Record->Manifest.TotalSizeBytes;
		OutCursor.bComplete = Recipient->bComplete;
		Result = MakeResult(Record, FGuid(), ECatDomainCommandError::None);
		Result.bRecipientComplete = Recipient->bComplete;
		Result.NextChunkIndex = Recipient->NextChunkIndex;
	}
	return Result;
}

// 关闭流程：永久关闭新写命令，并把尚未所有收件人完成 ACK 的传输标为失败；已经 ACK 完的本地副本不被回滚。
void UCatImprintMediaTransportService::CloseCommandsAndFailOpenTransfers()
{
	bCommandsOpen = false;
	for (TPair<FGuid, FTransferRecord>& Pair : Transfers)
	{
		bool bAllRecipientsComplete = Pair.Value.bReadyForRecipients;
		for (const TPair<FString, FRecipientRecord>& RecipientPair : Pair.Value.Recipients)
		{
			bAllRecipientsComplete &= RecipientPair.Value.bComplete;
		}
		if (!bAllRecipientsComplete)
		{
			Pair.Value.bFailed = true;
			++Pair.Value.Revision;
		}
	}
}

// Hash 流程：把整段字节交给共享 SHA-256 原语拿 32 字节 digest，再转成小写十六进制。
// 这里不做任何分块、拼接或字段选取：传进来的字节已经是调用点定好的载荷，本函数只负责摘要和它的字符串形式。
FString UCatImprintMediaTransportService::ComputePayloadHashHex(const TArray<uint8>& Bytes)
{
	return CatImprintDigestToHex(CatSha256::Compute(Bytes));
}

// 结果构造流程：只从当前记录复制媒体 ID、Revision 与 ready 状态；具体是否 committed 由调用点基于首次写入设置。
FCatImprintMediaResult UCatImprintMediaTransportService::MakeResult(const FTransferRecord* Record,
	const FGuid RequestId, const ECatDomainCommandError Error)
{
	FCatImprintMediaResult Result;
	Result.RequestId = RequestId;
	Result.Error = Error;
	if (Record)
	{
		Result.MediaId = Record->MediaId;
		Result.Revision = Record->Revision;
		Result.bReadyForRecipients = Record->bReadyForRecipients;
	}
	return Result;
}

// 幂等键流程：把身份、操作、媒体、请求和可选块序号组合，防止不同收件人或不同块共享终态。
FString UCatImprintMediaTransportService::MakeTerminalKey(const FString& StableNetId, const TCHAR* Operation,
	const FGuid MediaId, const FGuid RequestId, const int32 ChunkIndex)
{
	return FString::Printf(TEXT("%s|%s|%s|%s|%d"), *StableNetId, Operation,
		*MediaId.ToString(EGuidFormats::Digits), *RequestId.ToString(EGuidFormats::Digits), ChunkIndex);
}

// 计划键流程：CandidateId 与 RunAlbumId 共同决定一份媒体；CapturePlanId 是逐收件人投递键，不能用来复制多份 Host 字节。
FString UCatImprintMediaTransportService::MakePlanKey(const FCatCapturePlan& Plan)
{
	return FString::Printf(TEXT("%s|%s"), *Plan.CandidateId.ToString(EGuidFormats::Digits),
		*Plan.RunAlbumId.ToString(EGuidFormats::Digits));
}

// Hash 格式流程：只接受 64 个十六进制字符；具体大小写在调用点先转小写。
bool UCatImprintMediaTransportService::IsSha256Hex(const FString& HashHex)
{
	if (HashHex.Len() != 64)
	{
		return false;
	}
	for (const TCHAR Character : HashHex)
	{
		const bool bHex = (Character >= TEXT('0') && Character <= TEXT('9'))
			|| (Character >= TEXT('a') && Character <= TEXT('f'))
			|| (Character >= TEXT('A') && Character <= TEXT('F'));
		if (!bHex)
		{
			return false;
		}
	}
	return true;
}

// Manifest 比较流程：冻结后只能接受完全相同的重放，避免 Host 用同 MediaId 悄悄替换字节合同。
bool UCatImprintMediaTransportService::AreManifestsEquivalent(const FCatImprintMediaManifest& Left,
	const FCatImprintMediaManifest& Right)
{
	return Left.MediaId == Right.MediaId
		&& Left.CandidateId == Right.CandidateId
		&& Left.RunAlbumId == Right.RunAlbumId
		&& Left.MimeType.TrimStartAndEnd().ToLower() == Right.MimeType.TrimStartAndEnd().ToLower()
		&& Left.TotalSizeBytes == Right.TotalSizeBytes
		&& Left.ChunkSizeBytes == Right.ChunkSizeBytes
		&& Left.ChunkCount == Right.ChunkCount
		&& Left.Sha256Hex.TrimStartAndEnd().ToLower() == Right.Sha256Hex.TrimStartAndEnd().ToLower();
}

// Begin 签名流程：计划稳定键和收件人授权集合共同决定一次媒体创建请求，收件人顺序不影响签名。
FString UCatImprintMediaTransportService::MakeBeginPayloadSignature(const FCatCapturePlan& Plan,
	const TArray<FCatImprintMediaRecipientAuthorization>& Recipients)
{
	TArray<FString> RecipientParts;
	RecipientParts.Reserve(Recipients.Num());
	for (const FCatImprintMediaRecipientAuthorization& Authorization : Recipients)
	{
		RecipientParts.Add(FString::Printf(TEXT("%s:%s:%s"), *Authorization.RecipientStableNetId,
			*LexToString(Authorization.MembershipRevision), *LexToString(Authorization.PermissionRevision)));
	}
	RecipientParts.Sort();
	return FString::Printf(TEXT("CapturePlan=%s|Candidate=%s|Run=%s|Album=%s|Event=%s|Subject=%s|Recipients=%s"),
		*Plan.CapturePlanId.ToString(EGuidFormats::Digits), *Plan.CandidateId.ToString(EGuidFormats::Digits),
		*Plan.RunId.ToString(EGuidFormats::Digits), *Plan.RunAlbumId.ToString(EGuidFormats::Digits),
		*Plan.EventType.ToString(), *Plan.SubjectId.ToString(EGuidFormats::Digits),
		*FString::Join(RecipientParts, TEXT(",")));
}

// Manifest 签名流程：把 Host 冻结合同里的不可变字段规范化，确保重试不能悄悄替换 MIME、大小或整体 hash。
FString UCatImprintMediaTransportService::MakeManifestPayloadSignature(const FCatImprintMediaManifest& Manifest)
{
	return FString::Printf(TEXT("Media=%s|Candidate=%s|Album=%s|Mime=%s|Total=%s|ChunkSize=%s|ChunkCount=%s|Sha=%s"),
		*Manifest.MediaId.ToString(EGuidFormats::Digits), *Manifest.CandidateId.ToString(EGuidFormats::Digits),
		*Manifest.RunAlbumId.ToString(EGuidFormats::Digits), *Manifest.MimeType.TrimStartAndEnd().ToLower(),
		*LexToString(Manifest.TotalSizeBytes), *LexToString(Manifest.ChunkSizeBytes),
		*LexToString(Manifest.ChunkCount), *Manifest.Sha256Hex.TrimStartAndEnd().ToLower());
}

// Chunk 签名流程：签名只保存块大小和 SHA-256，不复制媒体字节到幂等索引里。
FString UCatImprintMediaTransportService::MakeChunkPayloadSignature(const TArray<uint8>& Bytes)
{
	return FString::Printf(TEXT("Bytes=%s|Sha=%s"), *LexToString(Bytes.Num()), *ComputePayloadHashHex(Bytes));
}

// ACK 签名流程：授权版本和块 hash 必须随 RequestId 一起稳定，避免旧成功 ACK 被不同 hash 复用。
FString UCatImprintMediaTransportService::MakeAckPayloadSignature(const int64 MembershipRevision,
	const int64 PermissionRevision, const FString& ChunkHashHex)
{
	return FString::Printf(TEXT("Membership=%s|Permission=%s|Hash=%s"), *LexToString(MembershipRevision),
		*LexToString(PermissionRevision), *ChunkHashHex.TrimStartAndEnd().ToLower());
}

// 终态重放流程：查找与载荷比对全部走 Framework 的共享模板 CatQueryTerminalReplay，本函数只补两处媒体层特有的语义。
// 一是重放时缓存里的失败终态要原样返回，只有原本成功的终态才改写成 AlreadyResolved——媒体命令的失败原因
// （PermissionDenied、RevisionConflict 等）对客户端是可操作信息，统一抹成 AlreadyResolved 会让重试方看不出该修什么。
// 二是载荷冲突时共享模板不写 OutResult，而 FCatImprintMediaResult 约定「失败也带回已知 MediaId、最新 Revision 和 cursor」，
// 所以这里从缓存补齐这些字段再打上 InvalidPayload，否则被拒绝的调用方只能继续用旧 cursor 重试。
bool UCatImprintMediaTransportService::TryResolveTerminalReplay(const FString& TerminalKey,
	const FString& PayloadSignature, FCatImprintMediaResult& OutResult) const
{
	const ECatTerminalReplayOutcome Outcome = CatQueryTerminalReplay(TerminalCache, TerminalPayloadSignatures,
		TerminalKey, PayloadSignature, OutResult,
		[](FCatImprintMediaResult& Replayed)
		{
			Replayed.bCommitted = false;
			if (Replayed.Error == ECatDomainCommandError::None)
			{
				Replayed.Error = ECatDomainCommandError::AlreadyResolved;
			}
		});
	if (Outcome == ECatTerminalReplayOutcome::FirstAttempt)
	{
		return false;
	}
	if (Outcome == ECatTerminalReplayOutcome::PayloadMismatch)
	{
		// PayloadMismatch 只在缓存确实命中时产生，因此这次查找必然有值。
		OutResult = *TerminalCache.Find(TerminalKey);
		OutResult.bCommitted = false;
		OutResult.Error = ECatDomainCommandError::InvalidPayload;
	}
	return true;
}

// 终态写入流程：结果和签名必须成对缓存；后续检查缺任一侧都按签名漂移处理。
void UCatImprintMediaTransportService::CacheTerminalResult(const FString& TerminalKey,
	const FString& PayloadSignature, const FCatImprintMediaResult& Result)
{
	TerminalCache.Add(TerminalKey, Result);
	TerminalPayloadSignatures.Add(TerminalKey, PayloadSignature);
}

// 计划校验流程：媒体链路依赖共同候选、相册分组和事件主体；缺任何键都会让后续授权或 Grant 无法追踪。
bool UCatImprintMediaTransportService::IsPlanComplete(const FCatCapturePlan& Plan)
{
	return Plan.CapturePlanId.IsValid() && Plan.CandidateId.IsValid() && Plan.RunId.IsValid()
		&& Plan.RunAlbumId.IsValid() && !Plan.EventType.IsNone() && Plan.SubjectId.IsValid();
}

// 块大小流程：最后一块使用剩余字节数，其余块必须等于 Manifest.ChunkSizeBytes。
int32 UCatImprintMediaTransportService::GetExpectedChunkSize(const FCatImprintMediaManifest& Manifest,
	const int32 ChunkIndex)
{
	if (ChunkIndex < 0 || ChunkIndex >= Manifest.ChunkCount)
	{
		return INDEX_NONE;
	}
	if (ChunkIndex == Manifest.ChunkCount - 1)
	{
		const int64 UsedBytes = static_cast<int64>(Manifest.ChunkSizeBytes) * ChunkIndex;
		return static_cast<int32>(Manifest.TotalSizeBytes - UsedBytes);
	}
	return Manifest.ChunkSizeBytes;
}
// 只读授权流程：收件人必须存在且两个授权版本都匹配；不匹配用 RevisionConflict 暴露旧 cursor 风险。
ECatDomainCommandError UCatImprintMediaTransportService::ValidateRecipient(const FTransferRecord& Record,
	const FString& RecipientStableNetId, const int64 MembershipRevision, const int64 PermissionRevision,
	const FRecipientRecord*& OutRecipient)
{
	OutRecipient = Record.Recipients.Find(RecipientStableNetId);
	if (RecipientStableNetId.IsEmpty() || !OutRecipient)
	{
		return ECatDomainCommandError::PermissionDenied;
	}
	if (OutRecipient->Authorization.MembershipRevision != MembershipRevision
		|| OutRecipient->Authorization.PermissionRevision != PermissionRevision)
	{
		return ECatDomainCommandError::RevisionConflict;
	}
	return ECatDomainCommandError::None;
}

// 可写授权流程：ACK 前重新跑同一套授权版本检查；返回引用后调用方才允许推进 cursor。
ECatDomainCommandError UCatImprintMediaTransportService::ValidateRecipientMutable(FTransferRecord& Record,
	const FString& RecipientStableNetId, const int64 MembershipRevision, const int64 PermissionRevision,
	FRecipientRecord*& OutRecipient)
{
	OutRecipient = Record.Recipients.Find(RecipientStableNetId);
	if (RecipientStableNetId.IsEmpty() || !OutRecipient)
	{
		return ECatDomainCommandError::PermissionDenied;
	}
	if (OutRecipient->Authorization.MembershipRevision != MembershipRevision
		|| OutRecipient->Authorization.PermissionRevision != PermissionRevision)
	{
		return ECatDomainCommandError::RevisionConflict;
	}
	return ECatDomainCommandError::None;
}

// Manifest 校验流程：对设置、计划字段、容量、块推导和整体 hash 格式做完整检查，并输出规范化后的不可变事实。
ECatDomainCommandError UCatImprintMediaTransportService::ValidateManifest(const FTransferRecord& Record,
	const FCatImprintMediaManifest& Manifest, FCatImprintMediaManifest& OutNormalizedManifest) const
{
	const UCatImprintMediaSettings* Settings = GetDefault<UCatImprintMediaSettings>();
	if (!Settings || !Settings->IsRuntimeReady())
	{
		return ECatDomainCommandError::PolicyUndecided;
	}
	if (Manifest.MediaId != Record.MediaId || Manifest.CandidateId != Record.SourcePlan.CandidateId
		|| Manifest.RunAlbumId != Record.SourcePlan.RunAlbumId)
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	const int64 ExpectedChunkCount = Manifest.ChunkSizeBytes > 0
		? (Manifest.TotalSizeBytes + Manifest.ChunkSizeBytes - 1) / Manifest.ChunkSizeBytes : 0;
	if (!Settings->IsMimeTypeAllowed(Manifest.MimeType) || Manifest.TotalSizeBytes <= 0
		|| Manifest.TotalSizeBytes > Settings->MaxMediaBytes || Manifest.ChunkSizeBytes <= 0
		|| Manifest.ChunkSizeBytes > Settings->MaxChunkBytes || Manifest.ChunkCount <= 0
		|| Manifest.ChunkCount > Settings->MaxChunkCount || Manifest.ChunkCount != ExpectedChunkCount)
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	OutNormalizedManifest = Manifest;
	OutNormalizedManifest.MimeType = Manifest.MimeType.TrimStartAndEnd().ToLower();
	OutNormalizedManifest.Sha256Hex = Manifest.Sha256Hex.TrimStartAndEnd().ToLower();
	return IsSha256Hex(OutNormalizedManifest.Sha256Hex)
		? ECatDomainCommandError::None : ECatDomainCommandError::InvalidPayload;
}

// 完成校验流程：块未齐时等待后续上传；块齐后按序拼回完整媒体并比较 Manifest 整体 SHA-256。
bool UCatImprintMediaTransportService::TryFinalizeTransfer(FTransferRecord& Record)
{
	if (Record.ChunksByIndex.Num() < Record.Manifest.ChunkCount)
	{
		return true;
	}
	TArray<uint8> AllBytes;
	AllBytes.Reserve(static_cast<int32>(Record.Manifest.TotalSizeBytes));
	for (int32 ChunkIndex = 0; ChunkIndex < Record.Manifest.ChunkCount; ++ChunkIndex)
	{
		const TArray<uint8>* Chunk = Record.ChunksByIndex.Find(ChunkIndex);
		if (!Chunk)
		{
			return true;
		}
		AllBytes.Append(*Chunk);
	}
	if (AllBytes.Num() != Record.Manifest.TotalSizeBytes
		|| ComputePayloadHashHex(AllBytes) != Record.Manifest.Sha256Hex)
	{
		return false;
	}
	Record.bReadyForRecipients = true;
	return true;
}