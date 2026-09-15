#pragma once

#include "CoreMinimal.h"
#include "Collection/CatImprintTypes.h"
#include "FishContainers/CatFishContainerTypes.h"
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

	/** 消费唯一 CaptureCommittedResult，按 CaptureRequestId 只生成一次 FishRecorded Grant 并尝试投递给当前连接。 */
	FGuid RecordCommittedCapture(const FCatCaptureCommittedResult& Capture, const FString& RecipientStableNetId,
		const FCatCaptureConditionSnapshot& Condition);

	/** 只读判断服务是否仍能为下一条已提交实物鱼建立 FishRecorded Grant；调用方在 Items 不可逆写入前检查。 */
	bool CanRecordCommittedCapture() const;

	/**
	 * 咬钩成立时给这一竿的钓手写一份剪影 Grant（图鉴 §3.1.3:98、钓鱼规则 §5.6:285）。
	 * 永不撤销：之后跑掉、断竿、放弃、试探期空竿、真咬超时，都不回滚这一层。
	 * EncounterKey＝本次咬钩机会的稳定键，重放同一机会只生成一份 Grant。
	 * 墓碑：2026-09-10 删掉的重试预算方案里的 RecordRetryExhaustedSilhouette 是它的前身，
	 * 那条路只在「重试耗尽」才给剪影，与「上钩成立就揭」相反，随方案一起删除，名字进了禁用符号表
	 * （Scripts/audit_fishing_failure_removal.py:25），不要复活。
	 */
	FGuid RecordFishEncounterSilhouette(FName FishDefinitionId, const FString& RecipientStableNetId,
		FGuid EncounterKey);

	/** 本人吃掉一条鱼后写知识层 Grant（图鉴 §3.1.4:124「谁吃谁记」）；按接收者+鱼种去重，重复吃不重复授予。 */
	FGuid RecordFishKnowledge(FName FishDefinitionId, const FString& RecipientStableNetId);

	/**
	 * 这条鱼对该接收者是不是「首钓新鱼种」（图鉴 §3.1.8:149）。
	 * 服务端图鉴事实有两个来源：本局已经发出的 FishRecorded Grant（CaptureGrantByRequest 的鱼种投影），
	 * 与该玩家入局时经 PlayerState 公开的跨局图鉴摘要。两者任一命中即不是首次。
	 * 调用方必须在 RecordCommittedCapture 之前问，本次捕获自己会立刻把该鱼种写进本局集合。
	 */
	bool IsFirstFishRecordForRecipient(const FString& RecipientStableNetId, FName FishDefinitionId) const;

	/** 记录一份外部里程碑已裁决的装备解锁 Grant；本服务只负责不可变投递和 ACK，不决定解锁条件。 */
	FGuid RecordCommittedUnlock(FName UnlockId, const FString& RecipientStableNetId);

	/** 只读预检一个候选能否按当前命令门和不可变字段被接收；调用方可在不可逆领域提交前使用，且不会创建候选或投递。 */
	bool CanAcceptImprintCandidate(const FCatImprintCandidate& Candidate) const;

	/** 接收一个由正式领域明确提交的印记候选；参与者身份只保存在服务器私有记录。 */
	bool SubmitImprintCandidate(const FCatImprintCandidate& Candidate);

	/** 为单个精确接收者创建或重放 CapturePlan；内部复用批量两阶段内核，Planned 落盘后才尝试投递。 */
	FCatCapturePlan CreateCapturePlan(FGuid CandidateId, const FString& RecipientStableNetId, bool bCampfireCover);

	/** 为去重后的全部合法参与者先一次性建齐 Planned 记录，再逐条尝试投递；未在线的记录保留供重试。 */
	bool CreateCapturePlansForParticipants(FGuid CandidateId, const TArray<FString>& RecipientStableNetIds,
		bool bCampfireCover, TArray<FCatCapturePlan>& OutPlans);

	/** 客户端报告真实本地成像结果；成功必须携带有效 ImprintId，随后才生成独立 GrantId。 */
	FCatDomainCommandResult ReportCaptureResult(AController* ReportingController, FGuid CapturePlanId,
		bool bSucceeded, FGuid ImprintId);

	/** 客户端 durable Profile 完成后按 GrantId ACK；身份由当前 Controller PlayerState 重建。 */
	FCatDomainCommandResult AcknowledgeGrant(AController* ReportingController, FGuid GrantId);

	/** 只读取得已经 durable ACK 的 Grant 内容；调用方用它做服务器侧投影，不接受客户端在 ACK 中夹带内容。 */
	bool TryGetAcknowledgedGrant(FGuid GrantId, FCatProfileGrant& OutGrant) const;

	/** 玩家登录或生成新 Grant 后重投其所有未 ACK Grant 和未终态 CapturePlan；每次重投复用原稳定 ID。 */
	void DeliverPendingForController(AController* Controller);

	/** Run teardown 前关闭新候选、把未成像计划标记失败并最终重投 Grant；返回 false 表示需等待真实 ACK，不是立即失败。 */
	bool PrepareForRunTeardown();

	/** 只读判断最终重投后的所有永久 Grant 是否已有真实 durable ACK；退出等待不会改写这些记录。 */
	bool AreAllGrantAcksComplete() const;

	/** 返回仍未收到 durable ACK 的 Grant 数量；Host 退出等待日志用它暴露仍在处理的持久化回执。 */
	int32 GetPendingGrantAckCount() const;

#if WITH_DEV_AUTOMATION_TESTS
	/** 自动化专用的 Grant 投递快照；只在开发自动化构建中复制服务端私有记录，避免测试用 pending 数量代替 Grant 内容检查。 */
	void CopyGrantDeliveryRecordsForAutomation(TArray<FCatGrantDeliveryRecord>& OutRecords) const;
#endif

	/** 结算协调器只读检查指定 Run 已有篝火封面计划、所有计划终态且全部永久 Grant 已 ACK。 */
	bool IsSettlementArchiveReady(FGuid RunId) const;

private:
	/** 从当前 Controller 的 APlayerState::UniqueId 解析服务器私有身份；无效时返回空。 */
	static FString ResolveStableNetId(const AController* Controller);

	/** 在当前 World 查找精确 StableNetId 的项目 PlayerController；断线时返回空且保留投递记录。 */
	AController* FindControllerByStableNetId(const FString& StableNetId) const;

	/**
	 * 把一条「某人第一次记录到某鱼种」的公开事实发布到 GameState。
	 * 它只产生展示用广播：不写任何人的图鉴、不创建 Grant、不影响印记准入。
	 * AnnouncementId 复用同一次的 FishRecorded GrantId，使重放与复制重发都只对应一条广播。
	 */
	void AnnounceFishSpeciesDiscovery(const FString& RecipientStableNetId, FName FishDefinitionId,
		FGuid AnnouncementId) const;

	/** 创建独立 GrantDeliveryRecord 并尝试投递；调用方必须先保证语义事实只提交一次。 */
	FGuid EnqueueGrant(FCatProfileGrant Grant);

	/** 向当前项目 PlayerController 发送一个 CapturePlan，并只在真实发送时推进 Delivered/attempts。 */
	bool DeliverCaptureRecord(FCatImprintCaptureDeliveryRecord& Record);

	/** 向当前项目 PlayerController 发送一个 Grant，并只在真实发送时推进 Delivered/attempts。 */
	bool DeliverGrantRecord(FCatGrantDeliveryRecord& Record);

	/** CandidateId 到不可变候选；候选本身不代表已生成图片或 Grant。 */
	TMap<FGuid, FCatImprintCandidate> Candidates;

	/** CapturePlanId 到独立成像投递记录。 */
	TMap<FGuid, FCatImprintCaptureDeliveryRecord> CaptureDeliveries;

	/** CandidateId+recipient 到唯一 CapturePlanId，保证重试不产生新计划。 */
	TMap<FString, FGuid> CapturePlanByRecipient;

	/** GrantId 到独立 durable ACK 投递记录。 */
	TMap<FGuid, FCatGrantDeliveryRecord> GrantDeliveries;

	/** CaptureRequestId 到 FishRecorded GrantId，保证捕获重放不重复增长图鉴。 */
	TMap<FGuid, FGuid> CaptureGrantByRequest;

	/** 「接收者|鱼种」到本局已发出的 FishRecorded GrantId；首钓判定读它，避免同一局内第二条同种鱼再抛一次印记。 */
	TMap<FString, FGuid> FishRecordGrantByRecipientAndFish;

	/** 「接收者|鱼种」到唯一知识层 GrantId；同一个人反复吃同一种鱼不重复生成待 ACK 的 Grant。 */
	TMap<FString, FGuid> KnowledgeGrantByRecipientAndFish;

	/** 「接收者|咬钩机会」到唯一剪影 GrantId；同一次咬钩重放不重复授予剪影。 */
	TMap<FString, FGuid> SilhouetteGrantByRecipientAndEncounter;

	/** Recipient+UnlockId 到唯一 Unlock GrantId；重试或重复里程碑不会制造多份待 ACK 解锁。 */
	TMap<FString, FGuid> UnlockGrantByRecipientAndUnlockId;

	/** RunId 到本地相册稳定分组 ID；服务端只分配 ID，不保存图片或路径。 */
	TMap<FGuid, FGuid> AlbumByRun;

	/** teardown 后永久关闭本 World 的新候选、计划和 Grant 生成。 */
	bool bCommandsOpen = true;
};
