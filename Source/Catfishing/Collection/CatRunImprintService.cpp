#include "Collection/CatRunImprintService.h"

#include "Framework/Core/CatStableNetId.h"
#include "Camp/CatCampSettings.h"
#include "Collection/CatImprintSettings.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Logging/CatLog.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"

namespace
{
	// Grant 载荷等价流程：比较一局投递记录真正承诺的永久授予内容；GrantId 由投递表拥有，候选重放只需证明业务事实一致。
	bool AreRunImprintGrantPayloadsEquivalent(const FCatProfileGrant& Existing, const FCatProfileGrant& Candidate)
	{
		return Existing.Kind == Candidate.Kind
			&& Existing.FishDefinitionId == Candidate.FishDefinitionId
			&& FMath::IsNearlyEqual(Existing.WeightKilograms, Candidate.WeightKilograms)
			&& Existing.CaptureCondition.RegionId == Candidate.CaptureCondition.RegionId
			&& Existing.CaptureCondition.TimeOfDayId == Candidate.CaptureCondition.TimeOfDayId
			&& Existing.CaptureCondition.WeatherId == Candidate.CaptureCondition.WeatherId
			&& Existing.ImprintId == Candidate.ImprintId
			&& Existing.RunAlbumId == Candidate.RunAlbumId
			&& Existing.bRunAlbumCover == Candidate.bRunAlbumCover
			&& Existing.UnlockId == Candidate.UnlockId
			&& Existing.RecipientStableNetId == Candidate.RecipientStableNetId;
	}
}
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
	ScoopGrantByRequest.Reset();
	AlbumByRun.Reset();
	Super::Deinitialize();
}

// 钓起轨归档流程：先验证 committed DTO/钓手身份并构造 FishRecorded Grant；同 CaptureRequestId 只有载荷完全一致才返回
// 既有 GrantId，换鱼、重量、条件或接收者都会拒绝。
FGuid UCatRunImprintService::RecordCommittedCapture(const FCatCaptureCommittedResult& Capture,
	const FString& RecipientStableNetId, const FCatCaptureConditionSnapshot& Condition)
{
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
	if (const FGuid* Existing = CaptureGrantByRequest.Find(Capture.CaptureRequestId))
	{
		const FCatGrantDeliveryRecord* ExistingRecord = GrantDeliveries.Find(*Existing);
		return ExistingRecord && AreRunImprintGrantPayloadsEquivalent(ExistingRecord->Grant, Grant) ? *Existing : FGuid();
	}
	const FGuid GrantId = EnqueueGrant(MoveTemp(Grant));
	if (GrantId.IsValid())
	{
		CaptureGrantByRequest.Add(Capture.CaptureRequestId, GrantId);
	}
	return GrantId;
}

// 抄获轨归档流程：与钓起轨同源同 RequestId，但用独立索引 ScoopGrantByRequest 判重，因此自钓自抄时两轨都会各留一条 Grant。
// 这里不接收重量和条件：抄获轨只承载「谁抄到了这个鱼种」，多带的字段在飞书没有展示口径，会变成没有依据的默认值。
FGuid UCatRunImprintService::RecordCommittedScoop(const FCatCaptureCommittedResult& Capture,
	const FString& ScooperStableNetId)
{
	if (!bCommandsOpen || !Capture.CaptureRequestId.IsValid() || !Capture.FishInstance.FishInstanceId.IsValid()
		|| Capture.FishInstance.FishDefinitionId.IsNone() || ScooperStableNetId.IsEmpty())
	{
		return FGuid();
	}
	FCatProfileGrant Grant;
	Grant.Kind = ECatProfileGrantKind::FishScooped;
	Grant.FishDefinitionId = Capture.FishInstance.FishDefinitionId;
	Grant.RecipientStableNetId = ScooperStableNetId;
	if (const FGuid* Existing = ScoopGrantByRequest.Find(Capture.CaptureRequestId))
	{
		const FCatGrantDeliveryRecord* ExistingRecord = GrantDeliveries.Find(*Existing);
		return ExistingRecord && AreRunImprintGrantPayloadsEquivalent(ExistingRecord->Grant, Grant) ? *Existing : FGuid();
	}
	const FGuid GrantId = EnqueueGrant(MoveTemp(Grant));
	if (GrantId.IsValid())
	{
		ScoopGrantByRequest.Add(Capture.CaptureRequestId, GrantId);
	}
	return GrantId;
}

// 捕获归档预检流程：只读取本局命令门，不创建 Grant；它隔离必需的两轨记录与可选 CapturePlan，使上游不会因未配置成像事件而拒绝实物鱼。
bool UCatRunImprintService::CanRecordCommittedCapture() const
{
	return bCommandsOpen;
}

// 候选预检流程：把唯一判定委托给 EvaluateCandidateAdmission，只把结构化原因压成布尔。
// 这里不写日志：上游（钓鱼提交、营地篝火）会在每次不可逆写入前调用它，未配置总清单时正常返回拒绝，记日志会淹没真实告警。
bool UCatRunImprintService::CanAcceptImprintCandidate(const FCatImprintCandidate& Candidate) const
{
	return EvaluateCandidateAdmission(Candidate) == ECatDomainCommandError::None;
}

// 候选准入评估流程：依次判命令门、稳定字段与参与者去重、总清单、同 CandidateId 重放、单局上限。
// 顺序有两处是刻意的：总清单排在重放判断之前，因为事件名是候选的不可变属性，被砍掉的事件不该靠“上次进来过”继续成立；
// 单局上限排在重放判断之后，因为重放的是已经占过名额的同一条候选，再计一次会让它在自己的上限上撞死。
ECatDomainCommandError UCatRunImprintService::EvaluateCandidateAdmission(const FCatImprintCandidate& Candidate) const
{
	if (!bCommandsOpen)
	{
		return ECatDomainCommandError::CommandsClosed;
	}
	if (!Candidate.CandidateId.IsValid() || !Candidate.RunId.IsValid() || Candidate.EventType.IsNone()
		|| !Candidate.SubjectId.IsValid() || Candidate.ParticipantCount <= 0
		|| Candidate.ParticipantCount != Candidate.ParticipantStableNetIds.Num())
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	TSet<FString> UniqueParticipants;
	for (const FString& StableNetId : Candidate.ParticipantStableNetIds)
	{
		if (StableNetId.IsEmpty() || UniqueParticipants.Contains(StableNetId))
		{
			return ECatDomainCommandError::InvalidPayload;
		}
		UniqueParticipants.Add(StableNetId);
	}
	const UCatImprintSettings* Settings = GetDefault<UCatImprintSettings>();
	if (!Settings || !Settings->IsImprintEventAllowed(Candidate.EventType))
	{
		// 总清单缺失或事件不在册：这两种情况在产品口径上是同一件事——这条触发点还没被印记册收编，不该产出印记。
		return ECatDomainCommandError::PolicyUndecided;
	}
	if (const FCatImprintCandidate* Existing = Candidates.Find(Candidate.CandidateId))
	{
		return Existing->RunId == Candidate.RunId && Existing->EventType == Candidate.EventType
			&& Existing->SubjectId == Candidate.SubjectId
			&& Existing->FishDefinitionId == Candidate.FishDefinitionId
			&& Existing->ParticipantCount == Candidate.ParticipantCount
			&& Existing->ParticipantStableNetIds == Candidate.ParticipantStableNetIds
			? ECatDomainCommandError::None : ECatDomainCommandError::InvalidPayload;
	}
	if (CountAcceptedCandidatesForRun(Candidate.RunId) >= Settings->MaxRunImprintCandidates)
	{
		// 上限未配置时 MaxRunImprintCandidates 为 0，这个比较对第一条新候选就成立，因此“没校准数值”与“已经拍满了”走同一条拒绝路径。
		return ECatDomainCommandError::CapacityExceeded;
	}
	return ECatDomainCommandError::None;
}

// 候选提交流程：复用同一份准入评估后才保存首个事实；同 CandidateId 的一致重放保持成功，不创建第二条记录或投递。
// 与只读预检的唯一差别是这里会为被准入名单和单局上限挡下的提交留一条 Warning——这两种拒绝意味着某个来源册真的想拍而没拍成，
// 属于需要回头对清单和数值的信号；字段无效或命令已关闭是调用方自己的问题或正常收口，不在这里放大。
bool UCatRunImprintService::SubmitImprintCandidate(const FCatImprintCandidate& Candidate)
{
	const ECatDomainCommandError Admission = EvaluateCandidateAdmission(Candidate);
	if (Admission != ECatDomainCommandError::None)
	{
		if (Admission == ECatDomainCommandError::PolicyUndecided || Admission == ECatDomainCommandError::CapacityExceeded)
		{
			UE_LOG(LogCatProfile, Warning,
				TEXT("Event=imprint_candidate_rejected RunId=%s CandidateId=%s EventType=%s Reason=%s RunCandidateCount=%d"),
				*Candidate.RunId.ToString(EGuidFormats::DigitsWithHyphens),
				*Candidate.CandidateId.ToString(EGuidFormats::DigitsWithHyphens),
				*Candidate.EventType.ToString(), *UEnum::GetValueAsString(Admission),
				CountAcceptedCandidatesForRun(Candidate.RunId));
		}
		return false;
	}
	if (Candidates.Contains(Candidate.CandidateId))
	{
		return true;
	}
	Candidates.Add(Candidate.CandidateId, Candidate);
	return true;
}

// 单局候选计数流程：线性扫描已受理候选并按 RunId 归并。
// 不额外维护 RunId 索引，是因为这张表的规模本身就被 MaxRunImprintCandidates 夹住，一局最多几条到几十条，
// 多一份索引反而要跟着 teardown 一起维护两处状态。
int32 UCatRunImprintService::CountAcceptedCandidatesForRun(const FGuid RunId) const
{
	int32 Count = 0;
	for (const TPair<FGuid, FCatImprintCandidate>& Pair : Candidates)
	{
		if (Pair.Value.RunId == RunId)
		{
			++Count;
		}
	}
	return Count;
}

// 批量计划流程：第一阶段去重并完整预检候选归属/旧索引，为所有缺失者预分配 ID 后一次建齐 Planned 记录；第二阶段才逐条
// RPC，同步回入或 teardown 只能改变投递阶段，不能阻止其余 Planned 事实存在。
bool UCatRunImprintService::CreateCapturePlansForParticipants(const FGuid CandidateId,
	const TArray<FString>& RecipientStableNetIds, const bool bCampfireCover, TArray<FCatCapturePlan>& OutPlans)
{
	OutPlans.Reset();
	const FCatImprintCandidate* Candidate = Candidates.Find(CandidateId);
	// 封面计划不再要求"全员在场"：飞书已经把篝火改成每晚都点、有谁在就拍谁，
	// 而"封面必须有围坐者"这条约束由下面的收件人校验直接兜住——收件人不能为空，且每个收件人都必须是候选登记过的参与者。
	if (!bCommandsOpen || !Candidate || RecipientStableNetIds.IsEmpty())
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
		if (CaptureDeliveries.Contains(Record.Plan.CapturePlanId)
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

// 成像结果流程：用 Controller 重建身份并核对计划接收者；计划终态只重放同一个成功/失败结论，成功还必须匹配首次
// ImprintId；成功必须先把 Grant 写入可靠投递表，入队失败时保留原阶段供同结果重试。
FCatDomainCommandResult UCatRunImprintService::ReportCaptureResult(AController* ReportingController,
	const FGuid CapturePlanId, const bool bSucceeded, const FGuid ImprintId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = CapturePlanId;
	const FString StableNetId = CatResolveStableNetId(ReportingController);
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
	else if (Record->Stage == ECatImprintCaptureDeliveryStage::CaptureSucceeded)
	{
		Result.Error = bSucceeded && Record->ImprintId == ImprintId
			? ECatDomainCommandError::AlreadyResolved : ECatDomainCommandError::InvalidPayload;
	}
	else if (Record->Stage == ECatImprintCaptureDeliveryStage::CaptureFailed)
	{
		Result.Error = bSucceeded ? ECatDomainCommandError::InvalidPayload : ECatDomainCommandError::AlreadyResolved;
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
	const FString StableNetId = CatResolveStableNetId(ReportingController);
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

// 重投流程：先验证当前 Controller 身份，再逐个重投本人的非终态 CapturePlan 和非 ACK Grant；原 ID 与内容始终不变。
void UCatRunImprintService::DeliverPendingForController(AController* Controller)
{
	const FString StableNetId = CatResolveStableNetId(Controller);
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

// 结算归档评估流程：先扫本 Run 的成像计划，任何一条仍停在 Planned/Delivered 就判为还要等；
// 再按营地是否真的配置了篝火封面事件决定要不要强制存在封面计划——配置为 None 时本局没有任何来源会创建它，
// 继续强制要求会让结算夜永远完不成，所以这里只跳过封面这一项，已配置项目的判定完全不变；
// 最后要求全部永久 Grant 已 durable ACK。整个过程只读，不投递也不改状态。
ECatSettlementArchiveBlocker UCatRunImprintService::EvaluateSettlementArchiveReadiness(const FGuid RunId) const
{
	if (!RunId.IsValid())
	{
		return ECatSettlementArchiveBlocker::InvalidRun;
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
			return ECatSettlementArchiveBlocker::CaptureDeliveryPending;
		}
	}
	const UCatCampSettings* CampSettings = GetDefault<UCatCampSettings>();
	if (CampSettings && !CampSettings->CampfireCoverEventId.IsNone() && !bHasCampfireCoverPlan)
	{
		return ECatSettlementArchiveBlocker::CampfireCoverPlanMissing;
	}
	for (const TPair<FGuid, FCatGrantDeliveryRecord>& Pair : GrantDeliveries)
	{
		if (Pair.Value.Stage != ECatGrantDeliveryStage::Acknowledged)
		{
			return ECatSettlementArchiveBlocker::GrantAckPending;
		}
	}
	return ECatSettlementArchiveBlocker::None;
}

// 结算归档检查流程：把结构化原因归约成协调器需要的布尔门，并在两种值得追查的情况下留日志——
// 未就绪时写出具体阻塞项，就绪但本局压根没配封面事件时写出「这局不会有封面」，避免以后又把它误判成卡死。
bool UCatRunImprintService::IsSettlementArchiveReady(const FGuid RunId) const
{
	const ECatSettlementArchiveBlocker Blocker = EvaluateSettlementArchiveReadiness(RunId);
	if (Blocker != ECatSettlementArchiveBlocker::None)
	{
		UE_LOG(LogCatProfile, Log, TEXT("Event=settlement_archive_not_ready RunId=%s Blocker=%s"),
			*RunId.ToString(EGuidFormats::DigitsWithHyphens), *UEnum::GetValueAsString(Blocker));
		return false;
	}
	const UCatCampSettings* CampSettings = GetDefault<UCatCampSettings>();
	if (!CampSettings || CampSettings->CampfireCoverEventId.IsNone())
	{
		// 用 Log 而不是 Warning：封面事件未裁是当前产品的既定状态，不是运行期异常，不应污染自动化的告警面。
		UE_LOG(LogCatProfile, Log,
			TEXT("Event=settlement_archive_ready_without_campfire_cover RunId=%s Reason=CampfireCoverEventIdUnset"),
			*RunId.ToString(EGuidFormats::DigitsWithHyphens));
	}
	return true;
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
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(CatFindControllerByStableNetId(GetWorld(), Record.RecipientStableNetId));
	if (!Controller || Record.Stage == ECatImprintCaptureDeliveryStage::CaptureSucceeded
		|| Record.Stage == ECatImprintCaptureDeliveryStage::CaptureFailed)
	{
		return false;
	}
	++Record.DeliveryAttempts;
	Record.Stage = ECatImprintCaptureDeliveryStage::Delivered;
	// 客户 RPC 在本机 listen server 路径上可同步重入 ReportCaptureResult；先发布 Delivered，且 RPC 返回后不再写
	// Record，避免覆盖内层已提交的成功/失败终态。
	Controller->ClientReceiveImprintCapturePlan(Record.Plan);
	return true;
}

// Grant 投递流程：现取目标项目 Controller；发送同一不可变 Grant 后增加 attempts/推进 Delivered，Acknowledged 记录永不重发。
bool UCatRunImprintService::DeliverGrantRecord(FCatGrantDeliveryRecord& Record)
{
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(CatFindControllerByStableNetId(GetWorld(), Record.Grant.RecipientStableNetId));
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
