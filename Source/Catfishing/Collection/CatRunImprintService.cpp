#include "Collection/CatRunImprintService.h"

#include "Framework/Game/CatGameplayTypes.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"

// 创建条件流程：只允许 Game/PIE 的 authority World 持有一局投递记录；客户端只运行自己的 LocalPlayer Profile。
bool UCatRunImprintService::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

// 反初始化流程：先关闭新命令，再清候选、两类投递、去重索引和相册分组；不会把未 ACK 记录改成 Acknowledged。
void UCatRunImprintService::Deinitialize()
{
	bCommandsOpen = false;
	Candidates.Reset();
	CaptureDeliveries.Reset();
	CapturePlanByRecipient.Reset();
	GrantDeliveries.Reset();
	CaptureGrantByRequest.Reset();
	SilhouetteGrantByFishingSession.Reset();
	UnlockGrantByRecipientAndUnlockId.Reset();
	AlbumByRun.Reset();
	Super::Deinitialize();
}

// 捕获归档流程：先按 CaptureRequestId 重放既有 Grant，再验证 committed DTO/接收者；首次建立不可变 FishRecorded Grant 并交统一投递记录。
FGuid UCatRunImprintService::RecordCommittedCapture(const FCatCaptureCommittedResult& Capture,
	const FString& RecipientStableNetId, const FCatCaptureConditionSnapshot& Condition)
{
	if (const FGuid* Existing = CaptureGrantByRequest.Find(Capture.CaptureRequestId))
	{
		return *Existing;
	}
	if (!bCommandsOpen || !Capture.CaptureRequestId.IsValid() || !Capture.FishInstance.FishInstanceId.IsValid()
		|| Capture.FishInstance.FishDefinitionId.IsNone() || RecipientStableNetId.IsEmpty()
		|| !FMath::IsFinite(Capture.FishInstance.WeightKilograms) || Capture.FishInstance.WeightKilograms <= 0.0)
	{
		return FGuid();
	}
	FCatProfileGrant Grant;
	Grant.Kind = ECatProfileGrantKind::FishRecorded;
	Grant.FishDefinitionId = Capture.FishInstance.FishDefinitionId;
	Grant.WeightKilograms = Capture.FishInstance.WeightKilograms;
	Grant.CaptureCondition = Condition;
	Grant.RecipientStableNetId = RecipientStableNetId;
	const FGuid GrantId = EnqueueGrant(MoveTemp(Grant));
	if (GrantId.IsValid())
	{
		CaptureGrantByRequest.Add(Capture.CaptureRequestId, GrantId);
	}
	return GrantId;
}

// 捕获归档预检流程：只读取本局命令门，不创建 Grant；它隔离必需 FishRecorded 与可选 CapturePlan，使上游不会因未配置成像事件而拒绝实物鱼。
bool UCatRunImprintService::CanRecordCommittedCapture() const
{
	return bCommandsOpen;
}

// 剪影归档流程：先按 FishingSessionId 重放，再验证命令仍开放、会话/鱼种/接收者完整；首次只生成 FishSilhouette Grant，不创建实物鱼、CapturePlan 或图片结论。
FGuid UCatRunImprintService::RecordRetryExhaustedSilhouette(const FGuid FishingSessionId,
	const FName FishDefinitionId, const FString& RecipientStableNetId)
{
	if (const FGuid* Existing = SilhouetteGrantByFishingSession.Find(FishingSessionId))
	{
		return *Existing;
	}
	if (!bCommandsOpen || !FishingSessionId.IsValid() || FishDefinitionId.IsNone() || RecipientStableNetId.IsEmpty())
	{
		return FGuid();
	}
	FCatProfileGrant Grant;
	Grant.Kind = ECatProfileGrantKind::FishSilhouette;
	Grant.FishDefinitionId = FishDefinitionId;
	Grant.RecipientStableNetId = RecipientStableNetId;
	const FGuid GrantId = EnqueueGrant(MoveTemp(Grant));
	if (GrantId.IsValid())
	{
		SilhouetteGrantByFishingSession.Add(FishingSessionId, GrantId);
	}
	return GrantId;
}

// 解锁归档流程：按接收者和 UnlockId 重放既有 Grant，再验证命令门与稳定字段；首次只生成 Unlock Grant，不在服务器伪造 Profile 存档。
FGuid UCatRunImprintService::RecordCommittedUnlock(const FName UnlockId, const FString& RecipientStableNetId)
{
	const FString UnlockKey = FString::Printf(TEXT("%s|%s"), *RecipientStableNetId, *UnlockId.ToString());
	if (const FGuid* Existing = UnlockGrantByRecipientAndUnlockId.Find(UnlockKey))
	{
		return *Existing;
	}
	if (!bCommandsOpen || UnlockId.IsNone() || RecipientStableNetId.IsEmpty())
	{
		return FGuid();
	}
	FCatProfileGrant Grant;
	Grant.Kind = ECatProfileGrantKind::Unlock;
	Grant.UnlockId = UnlockId;
	Grant.RecipientStableNetId = RecipientStableNetId;
	const FGuid GrantId = EnqueueGrant(MoveTemp(Grant));
	if (GrantId.IsValid())
	{
		UnlockGrantByRecipientAndUnlockId.Add(UnlockKey, GrantId);
	}
	return GrantId;
}

// 候选预检流程：先验证命令门、稳定键、人数和参与者去重，再核对同 CandidateId 的既有不可变事实；全过程只读，供上游在不可逆提交前 fail-closed。
bool UCatRunImprintService::CanAcceptImprintCandidate(const FCatImprintCandidate& Candidate) const
{
	if (!bCommandsOpen || !Candidate.CandidateId.IsValid() || !Candidate.RunId.IsValid() || Candidate.EventType.IsNone()
		|| !Candidate.SubjectId.IsValid() || Candidate.ParticipantCount <= 0
		|| Candidate.ParticipantCount != Candidate.ParticipantStableNetIds.Num())
	{
		return false;
	}
	TSet<FString> UniqueParticipants;
	for (const FString& StableNetId : Candidate.ParticipantStableNetIds)
	{
		if (StableNetId.IsEmpty() || UniqueParticipants.Contains(StableNetId))
		{
			return false;
		}
		UniqueParticipants.Add(StableNetId);
	}
	if (const FCatImprintCandidate* Existing = Candidates.Find(Candidate.CandidateId))
	{
		return Existing->RunId == Candidate.RunId && Existing->EventType == Candidate.EventType
			&& Existing->SubjectId == Candidate.SubjectId
			&& Existing->FishDefinitionId == Candidate.FishDefinitionId
			&& Existing->ParticipantCount == Candidate.ParticipantCount
			&& Existing->bAllActivePlayersPresent == Candidate.bAllActivePlayersPresent
			&& Existing->ParticipantStableNetIds == Candidate.ParticipantStableNetIds;
	}
	return true;
}

// 候选提交流程：复用完整只读预检后才保存首个事实；同 CandidateId 的一致重放保持成功，不创建第二条记录或投递。
bool UCatRunImprintService::SubmitImprintCandidate(const FCatImprintCandidate& Candidate)
{
	if (!CanAcceptImprintCandidate(Candidate))
	{
		return false;
	}
	if (Candidates.Contains(Candidate.CandidateId))
	{
		return true;
	}
	Candidates.Add(Candidate.CandidateId, Candidate);
	return true;
}

// 单人计划流程：把唯一接收者交给批量两阶段内核；完整 Planned 记录成立时返回同一 Plan，投递暂时不可用不改写 API 成功事实。
FCatCapturePlan UCatRunImprintService::CreateCapturePlan(const FGuid CandidateId,
	const FString& RecipientStableNetId, const bool bCampfireCover)
{
	TArray<FCatCapturePlan> Plans;
	return CreateCapturePlansForParticipants(CandidateId, {RecipientStableNetId}, bCampfireCover, Plans)
		&& Plans.Num() == 1 ? Plans[0] : FCatCapturePlan();
}

// 批量计划流程：第一阶段去重并完整预检候选归属/旧索引，为所有缺失者预分配 ID 后一次建齐 Planned 记录；第二阶段才逐条 RPC，同步回入或 teardown 只能改变投递阶段，不能阻止其余 Planned 事实存在。
bool UCatRunImprintService::CreateCapturePlansForParticipants(const FGuid CandidateId,
	const TArray<FString>& RecipientStableNetIds, const bool bCampfireCover, TArray<FCatCapturePlan>& OutPlans)
{
	OutPlans.Reset();
	const FCatImprintCandidate* Candidate = Candidates.Find(CandidateId);
	if (!bCommandsOpen || !Candidate || RecipientStableNetIds.IsEmpty()
		|| (bCampfireCover && !Candidate->bAllActivePlayersPresent))
	{
		return false;
	}
	TArray<FString> UniqueRecipients;
	TSet<FString> SeenRecipients;
	for (const FString& RecipientStableNetId : RecipientStableNetIds)
	{
		if (RecipientStableNetId.IsEmpty() || !Candidate->ParticipantStableNetIds.Contains(RecipientStableNetId))
		{
			return false;
		}
		if (SeenRecipients.Contains(RecipientStableNetId))
		{
			continue;
		}
		const FString PlanKey = FString::Printf(TEXT("%s|%s"),
			*CandidateId.ToString(EGuidFormats::Digits), *RecipientStableNetId);
		if (const FGuid* ExistingId = CapturePlanByRecipient.Find(PlanKey);
			ExistingId && !CaptureDeliveries.Contains(*ExistingId))
		{
			return false;
		}
		SeenRecipients.Add(RecipientStableNetId);
		UniqueRecipients.Add(RecipientStableNetId);
	}
	if (UniqueRecipients.IsEmpty())
	{
		return false;
	}

	FGuid AlbumId = AlbumByRun.FindRef(Candidate->RunId);
	if (!AlbumId.IsValid())
	{
		AlbumId = FGuid::NewGuid();
	}
	if (!AlbumId.IsValid())
	{
		return false;
	}
	TArray<FString> NewPlanKeys;
	TArray<FCatImprintCaptureDeliveryRecord> NewRecords;
	for (const FString& RecipientStableNetId : UniqueRecipients)
	{
		const FString PlanKey = FString::Printf(TEXT("%s|%s"),
			*CandidateId.ToString(EGuidFormats::Digits), *RecipientStableNetId);
		if (CapturePlanByRecipient.Contains(PlanKey))
		{
			continue;
		}
		FCatImprintCaptureDeliveryRecord& Record = NewRecords.AddDefaulted_GetRef();
		Record.Plan.CapturePlanId = FGuid::NewGuid();
		if (!Record.Plan.CapturePlanId.IsValid()
			|| CaptureDeliveries.Contains(Record.Plan.CapturePlanId)
			|| NewRecords.ContainsByPredicate([&Record](const FCatImprintCaptureDeliveryRecord& Existing)
			{
				return &Existing != &Record && Existing.Plan.CapturePlanId == Record.Plan.CapturePlanId;
			}))
		{
			return false;
		}
		Record.Plan.CandidateId = Candidate->CandidateId;
		Record.Plan.RunId = Candidate->RunId;
		Record.Plan.RunAlbumId = AlbumId;
		Record.Plan.EventType = Candidate->EventType;
		Record.Plan.SubjectId = Candidate->SubjectId;
		Record.Plan.bCampfireCover = bCampfireCover;
		Record.RecipientStableNetId = RecipientStableNetId;
		NewPlanKeys.Add(PlanKey);
	}
	if (!NewRecords.IsEmpty())
	{
		AlbumByRun.FindOrAdd(Candidate->RunId) = AlbumId;
		for (int32 Index = 0; Index < NewRecords.Num(); ++Index)
		{
			const FGuid PlanId = NewRecords[Index].Plan.CapturePlanId;
			CapturePlanByRecipient.Add(NewPlanKeys[Index], PlanId);
			CaptureDeliveries.Add(PlanId, MoveTemp(NewRecords[Index]));
		}
	}

	TArray<FGuid> PlanIds;
	for (const FString& RecipientStableNetId : UniqueRecipients)
	{
		const FString PlanKey = FString::Printf(TEXT("%s|%s"),
			*CandidateId.ToString(EGuidFormats::Digits), *RecipientStableNetId);
		const FGuid* PlanId = CapturePlanByRecipient.Find(PlanKey);
		if (!PlanId || !CaptureDeliveries.Contains(*PlanId))
		{
			return false;
		}
		PlanIds.Add(*PlanId);
	}
	// 全部 Planned 事实确认可回读后先冻结返回 DTO；后面的 RPC 即使同步回入或触发 teardown，也不会让调用方只拿到半批计划。
	for (const FGuid& PlanId : PlanIds)
	{
		const FCatImprintCaptureDeliveryRecord* Record = CaptureDeliveries.Find(PlanId);
		if (!Record)
		{
			return false;
		}
		OutPlans.Add(Record->Plan);
	}
	// 投递阶段始终按 ID 重取记录，不跨可能同步回入的 Client RPC 持有 TMap 元素引用；离线记录保持 Planned，供重连重投。
	for (const FGuid& PlanId : PlanIds)
	{
		if (FCatImprintCaptureDeliveryRecord* Record = CaptureDeliveries.Find(PlanId))
		{
			DeliverCaptureRecord(*Record);
		}
	}
	return OutPlans.Num() == UniqueRecipients.Num();
}

// 成像结果流程：用 Controller 重建身份并核对计划接收者；失败和成功都是不可重投终态，成功必须先把 Grant 写入可靠投递表，入队失败时保留原阶段供同结果重试。
FCatDomainCommandResult UCatRunImprintService::ReportCaptureResult(AController* ReportingController,
	const FGuid CapturePlanId, const bool bSucceeded, const FGuid ImprintId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = CapturePlanId;
	const FString StableNetId = ResolveStableNetId(ReportingController);
	FCatImprintCaptureDeliveryRecord* Record = CaptureDeliveries.Find(CapturePlanId);
	if (!CapturePlanId.IsValid() || StableNetId.IsEmpty())
	{
		Result.Error = ECatDomainCommandError::InvalidIdentity;
	}
	else if (!Record)
	{
		Result.Error = ECatDomainCommandError::NotFound;
	}
	else if (Record->RecipientStableNetId != StableNetId)
	{
		Result.Error = ECatDomainCommandError::PermissionDenied;
	}
	else if (Record->Stage == ECatImprintCaptureDeliveryStage::CaptureSucceeded
		|| Record->Stage == ECatImprintCaptureDeliveryStage::CaptureFailed)
	{
		Result.Error = ECatDomainCommandError::AlreadyResolved;
	}
	else if (!bSucceeded)
	{
		Record->Stage = ECatImprintCaptureDeliveryStage::CaptureFailed;
		Result.bCommitted = true;
		Result.Error = ECatDomainCommandError::Cancelled;
	}
	else if (!ImprintId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else
	{
		FCatProfileGrant Grant;
		Grant.Kind = ECatProfileGrantKind::Imprint;
		Grant.ImprintId = ImprintId;
		Grant.RunAlbumId = Record->Plan.RunAlbumId;
		Grant.bRunAlbumCover = Record->Plan.bCampfireCover;
		Grant.RecipientStableNetId = StableNetId;
		// GrantDeliveryRecord 是跨断线重投的 durable 服务器事实；只有它已经可靠入表，CapturePlan 才能进入成功终态，避免成功计划没有可 ACK 的 Grant。
		Result.bCommitted = EnqueueGrant(MoveTemp(Grant)).IsValid();
		if (Result.bCommitted)
		{
			Record->Stage = ECatImprintCaptureDeliveryStage::CaptureSucceeded;
			Record->ImprintId = ImprintId;
			Result.Error = ECatDomainCommandError::None;
		}
		else
		{
			Result.Error = ECatDomainCommandError::DependencyUnavailable;
		}
	}
	return Result;
}

// Grant ACK 流程：按 Controller 重建身份，验证 GrantId/接收者并单向推进 Acknowledged；重复 ACK 返回 AlreadyResolved 且不改变内容。
FCatDomainCommandResult UCatRunImprintService::AcknowledgeGrant(AController* ReportingController, const FGuid GrantId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = GrantId;
	const FString StableNetId = ResolveStableNetId(ReportingController);
	FCatGrantDeliveryRecord* Record = GrantDeliveries.Find(GrantId);
	if (!GrantId.IsValid() || StableNetId.IsEmpty())
	{
		Result.Error = ECatDomainCommandError::InvalidIdentity;
	}
	else if (!Record)
	{
		Result.Error = ECatDomainCommandError::NotFound;
	}
	else if (Record->Grant.RecipientStableNetId != StableNetId)
	{
		Result.Error = ECatDomainCommandError::PermissionDenied;
	}
	else if (Record->Stage == ECatGrantDeliveryStage::Acknowledged)
	{
		Result.Error = ECatDomainCommandError::AlreadyResolved;
	}
	else
	{
		Record->Stage = ECatGrantDeliveryStage::Acknowledged;
		Result.bCommitted = true;
		Result.Error = ECatDomainCommandError::None;
	}
	return Result;
}

// ACK Grant 读取流程：只返回已经进入 Acknowledged 的服务器投递记录；Pending/Delivered 或未知 ID 都不能驱动运行期授权。
bool UCatRunImprintService::TryGetAcknowledgedGrant(const FGuid GrantId, FCatProfileGrant& OutGrant) const
{
	OutGrant = FCatProfileGrant();
	const FCatGrantDeliveryRecord* Record = GrantDeliveries.Find(GrantId);
	if (!GrantId.IsValid() || !Record || Record->Stage != ECatGrantDeliveryStage::Acknowledged)
	{
		return false;
	}
	OutGrant = Record->Grant;
	return true;
}

// 重投流程：先验证当前 Controller 身份，再逐个重投本人的非终态 CapturePlan 和非 ACK Grant；原 ID 与内容始终不变。
void UCatRunImprintService::DeliverPendingForController(AController* Controller)
{
	const FString StableNetId = ResolveStableNetId(Controller);
	if (StableNetId.IsEmpty())
	{
		return;
	}
	for (TPair<FGuid, FCatImprintCaptureDeliveryRecord>& Pair : CaptureDeliveries)
	{
		if (Pair.Value.RecipientStableNetId == StableNetId
			&& Pair.Value.Stage != ECatImprintCaptureDeliveryStage::CaptureSucceeded
			&& Pair.Value.Stage != ECatImprintCaptureDeliveryStage::CaptureFailed)
		{
			DeliverCaptureRecord(Pair.Value);
		}
	}
	for (TPair<FGuid, FCatGrantDeliveryRecord>& Pair : GrantDeliveries)
	{
		if (Pair.Value.Grant.RecipientStableNetId == StableNetId
			&& Pair.Value.Stage != ECatGrantDeliveryStage::Acknowledged)
		{
			DeliverGrantRecord(Pair.Value);
		}
	}
}

// teardown 流程：永久关闭新命令，把仍未成像的计划标为失败，并最终重投全部未 ACK Grant；存在未 ACK 时返回 false 让 GameMode 进入统一有界等待。
bool UCatRunImprintService::PrepareForRunTeardown()
{
	bCommandsOpen = false;
	for (TPair<FGuid, FCatImprintCaptureDeliveryRecord>& Pair : CaptureDeliveries)
	{
		if (Pair.Value.Stage == ECatImprintCaptureDeliveryStage::Planned
			|| Pair.Value.Stage == ECatImprintCaptureDeliveryStage::Delivered)
		{
			Pair.Value.Stage = ECatImprintCaptureDeliveryStage::CaptureFailed;
		}
	}
	for (TPair<FGuid, FCatGrantDeliveryRecord>& Pair : GrantDeliveries)
	{
		if (Pair.Value.Stage != ECatGrantDeliveryStage::Acknowledged)
		{
			DeliverGrantRecord(Pair.Value);
		}
	}
	return AreAllGrantAcksComplete();
}

// Grant ACK 完成读取流程：只扫描独立投递记录的真实 Acknowledged 阶段；teardown 超时、投递次数或连接消失都不能把它变成 true。
bool UCatRunImprintService::AreAllGrantAcksComplete() const
{
	return GetPendingGrantAckCount() == 0;
}

// Grant ACK 计数流程：统计每条尚未由 owning client durable Profile 回 ACK 的记录；只供有界收口判断和诊断，不推进投递状态。
int32 UCatRunImprintService::GetPendingGrantAckCount() const
{
	int32 PendingCount = 0;
	for (const TPair<FGuid, FCatGrantDeliveryRecord>& Pair : GrantDeliveries)
	{
		if (Pair.Value.Stage != ECatGrantDeliveryStage::Acknowledged)
		{
			++PendingCount;
		}
	}
	return PendingCount;
}

#if WITH_DEV_AUTOMATION_TESTS
// 自动化快照流程：先清空输出，再复制当前服务端 GrantDeliveryRecord 值；这个入口只为测试核对 Grant 内容和阶段，不参与运行时投递或 ACK。
void UCatRunImprintService::CopyGrantDeliveryRecordsForAutomation(TArray<FCatGrantDeliveryRecord>& OutRecords) const
{
	OutRecords.Reset();
	GrantDeliveries.GenerateValueArray(OutRecords);
}
#endif

// 结算归档检查流程：要求指定 Run 至少建立一份全员篝火封面计划，所有该 Run 计划均已成功或明确失败，且当前一局全部 Grant 已 ACK；只读不触发投递或改状态。
bool UCatRunImprintService::IsSettlementArchiveReady(const FGuid RunId) const
{
	if (!RunId.IsValid())
	{
		return false;
	}
	bool bHasCampfireCoverPlan = false;
	for (const TPair<FGuid, FCatImprintCaptureDeliveryRecord>& Pair : CaptureDeliveries)
	{
		if (Pair.Value.Plan.RunId != RunId)
		{
			continue;
		}
		bHasCampfireCoverPlan |= Pair.Value.Plan.bCampfireCover;
		if (Pair.Value.Stage == ECatImprintCaptureDeliveryStage::Planned
			|| Pair.Value.Stage == ECatImprintCaptureDeliveryStage::Delivered)
		{
			return false;
		}
	}
	if (!bHasCampfireCoverPlan)
	{
		return false;
	}
	for (const TPair<FGuid, FCatGrantDeliveryRecord>& Pair : GrantDeliveries)
	{
		if (Pair.Value.Stage != ECatGrantDeliveryStage::Acknowledged)
		{
			return false;
		}
	}
	return true;
}

// 身份解析流程：只读取当前 Controller PlayerState 的继承 UniqueId；原始字符串只在服务端私有映射中使用。
FString UCatRunImprintService::ResolveStableNetId(const AController* Controller)
{
	const APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
	return PlayerState && PlayerState->GetUniqueId().IsValid() ? PlayerState->GetUniqueId()->ToString() : FString();
}

// Controller 查找流程：遍历当前 World 的项目控制器并比较继承 UniqueId；不使用名字、地址或缓存旧 Controller。
AController* UCatRunImprintService::FindControllerByStableNetId(const FString& StableNetId) const
{
	if (!GetWorld() || StableNetId.IsEmpty())
	{
		return nullptr;
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AController* Controller = It->Get();
		if (ResolveStableNetId(Controller) == StableNetId)
		{
			return Controller;
		}
	}
	return nullptr;
}

// Grant 入队流程：分配一次 GrantId、保存 Pending 记录后尝试投递；关闭或无接收者时返回无效且不留下半条记录。
FGuid UCatRunImprintService::EnqueueGrant(FCatProfileGrant Grant)
{
	if (!bCommandsOpen || Grant.RecipientStableNetId.IsEmpty())
	{
		return FGuid();
	}
	Grant.GrantId = FGuid::NewGuid();
	FCatGrantDeliveryRecord Record;
	Record.Grant = MoveTemp(Grant);
	const FGuid GrantId = Record.Grant.GrantId;
	FCatGrantDeliveryRecord& Stored = GrantDeliveries.Add(GrantId, MoveTemp(Record));
	DeliverGrantRecord(Stored);
	return GrantId;
}

// CapturePlan 投递流程：现取目标 Controller 并要求项目类型；只给 Planned/Delivered 记录重投，客户端明确失败或成功后都永久停止投递。
bool UCatRunImprintService::DeliverCaptureRecord(FCatImprintCaptureDeliveryRecord& Record)
{
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(FindControllerByStableNetId(Record.RecipientStableNetId));
	if (!Controller || Record.Stage == ECatImprintCaptureDeliveryStage::CaptureSucceeded
		|| Record.Stage == ECatImprintCaptureDeliveryStage::CaptureFailed)
	{
		return false;
	}
	++Record.DeliveryAttempts;
	Record.Stage = ECatImprintCaptureDeliveryStage::Delivered;
	// 客户 RPC 在本机 listen server 路径上可同步重入 ReportCaptureResult；先发布 Delivered，且 RPC 返回后不再写 Record，避免覆盖内层已提交的成功/失败终态。
	Controller->ClientReceiveImprintCapturePlan(Record.Plan);
	return true;
}

// Grant 投递流程：现取目标项目 Controller；发送同一不可变 Grant 后增加 attempts/推进 Delivered，Acknowledged 记录永不重发。
bool UCatRunImprintService::DeliverGrantRecord(FCatGrantDeliveryRecord& Record)
{
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(FindControllerByStableNetId(Record.Grant.RecipientStableNetId));
	if (!Controller || Record.Stage == ECatGrantDeliveryStage::Acknowledged)
	{
		return false;
	}
	++Record.DeliveryAttempts;
	Record.Stage = ECatGrantDeliveryStage::Delivered;
	// 本机客户可在 RPC 调用栈内完成 durable 写入并 ACK；发送前写投递事实，发送后不再触碰可能已进入 Acknowledged 的记录。
	Controller->ClientReceiveProfileGrant(Record.Grant);
	return true;
}
