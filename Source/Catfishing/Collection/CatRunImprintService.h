#pragma once

#include "CoreMinimal.h"
#include "Collection/CatImprintTypes.h"
#include "Items/CatItemTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "CatRunImprintService.generated.h"

class AController;

/** 一局服务器图鉴/印记协调服务；它把 committed 领域事实转成 Grant，并严格分离 CapturePlan 投递与 Grant ACK。 */
UCLASS()
class CATFISHING_API UCatRunImprintService : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 只在 authority Game World 创建；客户端永久档案由 LocalPlayer Profile 子系统独立拥有。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** World 销毁时关闭新事实并清一局候选/投递记录；未 ACK Grant 不会被伪装为已写入玩家档案。 */
	virtual void Deinitialize() override;

	/**
	 * 消费唯一 CaptureCommittedResult 的「钓起」轨，按 CaptureRequestId 只生成一次 FishRecorded Grant 并尝试投递给当前连接。
	 * RecipientStableNetId 必须是上钩成立时的钓手，不是抄网命中者；抄获轨走 RecordCommittedScoop 单独归约。
	 */
	FGuid RecordCommittedCapture(const FCatCaptureCommittedResult& Capture, const FString& RecipientStableNetId,
		const FCatCaptureConditionSnapshot& Condition);

	/**
	 * 消费同一次 CaptureCommittedResult 的「抄获」轨，按 CaptureRequestId 只生成一次 FishScooped Grant 并尝试投递。
	 * 它与 RecordCommittedCapture 用两套独立幂等索引，因此自己钓自己抄时同一次捕获会产出两条 Grant，不去重成一条。
	 * 抄获轨只承载「谁抄到了这个鱼种」这一个事实，因此不接收重量和捕获条件。
	 */
	FGuid RecordCommittedScoop(const FCatCaptureCommittedResult& Capture, const FString& ScooperStableNetId);

	/** 只读判断服务是否仍能为下一条已提交实物鱼建立两轨记录 Grant；调用方在 Items 不可逆写入前检查。 */
	bool CanRecordCommittedCapture() const;

	/**
	 * 只读预检一个候选能否按当前命令门、准入名单、单局上限和不可变字段被接收。
	 * 调用方可在不可逆领域提交前使用；它不创建候选、不投递，也不写日志——上游会高频调用它做 fail-closed 预检，
	 * 在这里记日志会把正常的“本局不产印记”刷成噪音。需要拒绝原因时用 EvaluateCandidateAdmission。
	 */
	bool CanAcceptImprintCandidate(const FCatImprintCandidate& Candidate) const;

	/**
	 * 只读评估一个候选被拒的具体原因；None 表示可以受理。
	 * 它是 CanAcceptImprintCandidate 与 SubmitImprintCandidate 共用的唯一判定，使预检和真正写入不会出现两套口径。
	 * PolicyUndecided 表示事件不在总清单内（含总清单为空），CapacityExceeded 表示本局候选条数已到上限。
	 */
	ECatDomainCommandError EvaluateCandidateAdmission(const FCatImprintCandidate& Candidate) const;

	/** 接收一个由正式领域明确提交的印记候选；被准入名单或单局上限拒绝时写一条 Warning，不静默丢。参与者身份只保存在服务器私有记录。 */
	bool SubmitImprintCandidate(const FCatImprintCandidate& Candidate);

	/** 为去重后的全部合法参与者先一次性建齐 Planned 记录，再逐条尝试投递；未在线的记录保留供重试。 */
	bool CreateCapturePlansForParticipants(FGuid CandidateId, const TArray<FString>& RecipientStableNetIds,
		bool bCampfireCover, TArray<FCatCapturePlan>& OutPlans);

	/** 客户端报告真实本地成像结果；成功必须携带有效 ImprintId，随后才生成独立 GrantId。 */
	FCatDomainCommandResult ReportCaptureResult(AController* ReportingController, FGuid CapturePlanId,
		bool bSucceeded, FGuid ImprintId);

	/** 客户端 durable Profile 完成后按 GrantId ACK；身份由当前 Controller PlayerState 重建。 */
	FCatDomainCommandResult AcknowledgeGrant(AController* ReportingController, FGuid GrantId);

	/** 玩家登录或生成新 Grant 后重投其所有未 ACK Grant 和未终态 CapturePlan；每次重投复用原稳定 ID。 */
	void DeliverPendingForController(AController* Controller);

	/** Run teardown 前关闭新候选、把未成像计划标记失败并最终重投 Grant；返回 false 表示需有界等 ACK，不是立即失败。 */
	bool PrepareForRunTeardown();

	/** 只读判断最终重投后的所有永久 Grant 是否已有真实 durable ACK；超时不会改写这些记录。 */
	bool AreAllGrantAcksComplete() const;

	/** 返回仍未收到 durable ACK 的 Grant 数量；Host 超时日志用它暴露丢失风险，不把超时冒充确认。 */
	int32 GetPendingGrantAckCount() const;

	/**
	 * 结算协调器只读检查指定 Run 的归档是否就绪；未就绪时把具体阻塞原因写进日志，方便区分等待与配置缺失。
	 * 保留布尔返回是因为唯一调用方 ServerRequestSettlementCompletion 仍按布尔门放行；需要原因时用下面的评估入口。
	 */
	bool IsSettlementArchiveReady(FGuid RunId) const;

	/**
	 * 只读评估指定 Run 的归档还差什么，返回第一个仍未满足的原因；不投递、不改任何状态。
	 * 篝火封面只在营地真的配置了封面事件时才作为阻塞项——未配置时本局根本不会产生封面计划，
	 * 继续要求它等于让结算夜永远完不成。测试和诊断用它区分「还要等」和「本局没有封面」。
	 */
	ECatSettlementArchiveBlocker EvaluateSettlementArchiveReadiness(FGuid RunId) const;

private:
	/** 创建独立 GrantDeliveryRecord 并尝试投递；调用方必须先保证语义事实只提交一次。 */
	FGuid EnqueueGrant(FCatProfileGrant Grant);

	/** 向当前项目 PlayerController 发送一个 CapturePlan，并只在真实发送时推进 Delivered/attempts。 */
	bool DeliverCaptureRecord(FCatImprintCaptureDeliveryRecord& Record);

	/** 向当前项目 PlayerController 发送一个 Grant，并只在真实发送时推进 Delivered/attempts。 */
	bool DeliverGrantRecord(FCatGrantDeliveryRecord& Record);

	/** 统计指定 Run 已受理的候选条数；单局上限按“成像瞬间”计数，同一候选发给多少参与者都只算一条。 */
	int32 CountAcceptedCandidatesForRun(FGuid RunId) const;

	/** CandidateId 到不可变候选；候选本身不代表已生成图片或 Grant，但它同时是单局上限的计数依据。 */
	TMap<FGuid, FCatImprintCandidate> Candidates;

	/** CapturePlanId 到独立成像投递记录。 */
	TMap<FGuid, FCatImprintCaptureDeliveryRecord> CaptureDeliveries;

	/** CandidateId+recipient 到唯一 CapturePlanId，保证重试不产生新计划。 */
	TMap<FString, FGuid> CapturePlanByRecipient;

	/** GrantId 到独立 durable ACK 投递记录。 */
	TMap<FGuid, FCatGrantDeliveryRecord> GrantDeliveries;

	/** CaptureRequestId 到「钓起」轨 FishRecorded GrantId，保证捕获重放不重复增长图鉴。 */
	TMap<FGuid, FGuid> CaptureGrantByRequest;

	/** CaptureRequestId 到「抄获」轨 FishScooped GrantId；与 CaptureGrantByRequest 分开是为了让同一次捕获能同时存在两轨记录。 */
	TMap<FGuid, FGuid> ScoopGrantByRequest;

	/** RunId 到本地相册稳定分组 ID；服务端只分配 ID，不保存图片或路径。 */
	TMap<FGuid, FGuid> AlbumByRun;

	/** teardown 后永久关闭本 World 的新候选、计划和 Grant 生成。 */
	bool bCommandsOpen = true;
};
