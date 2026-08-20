#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatProfileContracts.h"
#include "CatImprintTypes.generated.h"

/** CapturePlan 成像投递阶段；它只活到本局且不表示永久 Grant ACK。 */
UENUM(BlueprintType)
enum class ECatImprintCaptureDeliveryStage : uint8
{
	/** 计划已裁决但尚未投递。 */
	Planned,
	/** 已向目标拥有者投递，等待本地成像结果。 */
	Delivered,
	/** 客户端报告本地成像成功。 */
	CaptureSucceeded,
	/** 客户端明确报告本地成像失败；这是计划终态，服务器不会重投或据此生成 Grant。 */
	CaptureFailed
};

/**
 * 结算夜归档还差什么才算就绪；它替代原来那个「未就绪就返回 false」的哑布尔值，
 * 让结算协调器和日志能区分「还要继续等」和「本局配置本来就不会产出这一项」。
 */
UENUM(BlueprintType)
enum class ECatSettlementArchiveBlocker : uint8
{
	/** 归档已就绪，结算夜可以推进。 */
	None,
	/** 传入的 RunId 无效，无法定位本局记录；这是调用方的错，不是等待状态。 */
	InvalidRun,
	/** 本局还有 CapturePlan 停在 Planned/Delivered，尚未拿到明确的成功或失败终态。 */
	CaptureDeliveryPending,
	/** 营地已配置篝火封面事件，但本局还没有任何封面计划落地。 */
	CampfireCoverPlanMissing,
	/** 还有永久 Grant 没有收到客户端 durable ACK。 */
	GrantAckPending
};

/** Grant 投递阶段；只按 GrantId 跟踪远端接收/ACK，不借用 CapturePlan 状态。 */
UENUM(BlueprintType)
enum class ECatGrantDeliveryStage : uint8
{
	/** Grant 已生成但尚未投递。 */
	Pending,
	/** 已投递，等待拥有者 durable Journal ACK。 */
	Delivered,
	/** 拥有者确认本地 durable 合并完成。 */
	Acknowledged
};

/** 各领域提交的语义化印记候选；它不包含图片或客户端文件路径。 */
USTRUCT(BlueprintType)
struct FCatImprintCandidate
{
	GENERATED_BODY()

	/** 候选去重 ID；同一 committed Result 重放必须复用。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid CandidateId;

	/** 所属 Run ID。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid RunId;

	/** 稳定事件类型；具体清单未裁时调用方不得制造任意名称。 */
	UPROPERTY(BlueprintReadOnly)
	FName EventType = NAME_None;

	/** 候选主体 ID，例如 FishInstanceId；成像器只把它当稳定引用。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid SubjectId;

	/** 鱼相关候选的稳定鱼种上下文；仅供成像计划与表现读取，候选本身绝不据此生成剪影或其他永久 Grant。 */
	UPROPERTY(BlueprintReadOnly)
	FName FishDefinitionId = NAME_None;

	/** 服务器裁决的参与人数；原始 StableNetId 数组保存在服务私有记录。 */
	UPROPERTY(BlueprintReadOnly)
	int32 ParticipantCount = 0;

	/** 服务器私有参与者 StableNetId；无 UPROPERTY，不进入公共 DTO。 */
	TArray<FString> ParticipantStableNetIds;
};

/** 可重放成像计划；同一计划的所有参与者目标是同一事件/构图语义，不是已写图片。 */
USTRUCT(BlueprintType)
struct FCatCapturePlan
{
	GENERATED_BODY()

	/** 成像投递唯一键；与 GrantId 永不互换。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid CapturePlanId;

	/** 来源候选 ID。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid CandidateId;

	/** 所属 Run/相册分组。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid RunId;

	/** 事件类型与主体；具体镜头参数 O 时不伪造 transform。 */
	UPROPERTY(BlueprintReadOnly)
	FName EventType = NAME_None;

	/** 候选主体 ID。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid SubjectId;

	/** 是否为固定全员篝火封面计划；只有显式正式事件可设 true。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCampfireCover = false;

	/** 服务端在建立计划时冻结的一局相册 ID；它与 RunId 分离，客户端不得自行生成替代值。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid RunAlbumId;
};

/** 服务器按 CapturePlanId 保存的一局成像投递记录；不含 GrantId ACK 状态。 */
USTRUCT()
struct FCatImprintCaptureDeliveryRecord
{
	GENERATED_BODY()

	/** 不可变成像计划。 */
	FCatCapturePlan Plan;

	/** 私有接收者 StableNetId。 */
	FString RecipientStableNetId;

	/** 当前成像投递阶段。 */
	ECatImprintCaptureDeliveryStage Stage = ECatImprintCaptureDeliveryStage::Planned;

	/** 有界重投次数；上限策略未裁时服务只记录不自动重试。 */
	int32 DeliveryAttempts = 0;

	/** 成像成功后由客户端报告的稳定 ImprintId；失败保持无效。 */
	FGuid ImprintId;
};

/** 服务器按 GrantId 保存的一局永久授予投递记录；不含 CapturePlan 重试状态。 */
USTRUCT()
struct FCatGrantDeliveryRecord
{
	GENERATED_BODY()

	/** 不可变 Grant。 */
	FCatProfileGrant Grant;

	/** 当前 Grant 投递/ACK 阶段。 */
	ECatGrantDeliveryStage Stage = ECatGrantDeliveryStage::Pending;

	/** 已投递次数；ACK 丢失时可重投同一 GrantId。 */
	int32 DeliveryAttempts = 0;
};
