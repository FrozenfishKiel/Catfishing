#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "CatSacrificeContracts.generated.h"

/** 献祭跨 Items/Run 短协议的单向阶段；ItemsCommitted 之后不允许退回或恢复鱼实例。 */
UENUM(BlueprintType)
enum class ECatSacrificeStage : uint8
{
	/** 协调器已接受外部命令，但尚未在 Items 建立预留。 */
	Received,
	/** Items 已锁定目标鱼，仍可取消并释放预留。 */
	Reserved,
	/** Run 已确认当前阶段与额度写口可接受该值，仍未消费鱼。 */
	RunAccepted,
	/** Items 已不可逆移除鱼；后续失败只能重试 Run apply。 */
	ItemsCommitted,
	/** Run 已幂等应用额度；协调器只剩发布终态。 */
	RunApplied,
	/** 两个聚合都完成且终态已缓存。 */
	Completed,
	/** Items commit 前明确取消；鱼仍在原容器。 */
	Cancelled,
	/** Items commit 前发生不可恢复拒绝；协议没有消费鱼。 */
	Failed
};

/** 祭坛交互与 UI 共用的唯一外部命令；调用方不能直接预留、消费 Items 或增加 Run 额度。 */
USTRUCT(BlueprintType)
struct FCatSacrificeCommand
{
	GENERATED_BODY()

	/** 协议的 RequestId、Items 预期 Revision 与服务器身份；Run Revision 单独携带以避免混用两个聚合版本。 */
	UPROPERTY(BlueprintReadWrite)
	FCatDomainCommandContext Context;

	/** 目标鱼实例的局内不可变标识；价值由已提交鱼实例引用的真实定义派生，不接受客户端数值。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid FishInstanceId;

	/** 目标鱼当前所在容器；用于把 ExpectedRevision 精确绑定到 Items 聚合。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid ContainerId;

	/** 调用方看到的 Run Revision；协调器在消费鱼前用它完成 RunAccepted 校验。 */
	UPROPERTY(BlueprintReadWrite)
	int64 ExpectedRunRevision = 0;
};

/** 献祭协议的可重放结果；它只描述协调进度，不复制鱼实例或 Run 额度真相。 */
USTRUCT(BlueprintType)
struct FCatSacrificeResult
{
	GENERATED_BODY()

	/** 与外部命令一致的 RequestId；同一幂等键始终返回同一协议记录。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid RequestId;

	/** 当前已持久于本局服务器内存的协议阶段；ItemsCommitted 及以后意味着鱼不可恢复。 */
	UPROPERTY(BlueprintReadOnly)
	ECatSacrificeStage Stage = ECatSacrificeStage::Received;

	/** Completed 表示跨聚合提交完成；处理中或拒绝均为 false。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCompleted = false;

	/** 当前结构化错误；Items commit 后的暂时失败保留错误但不进入 Failed 终态。 */
	UPROPERTY(BlueprintReadOnly)
	ECatDomainCommandError Error = ECatDomainCommandError::None;

	/** Items 最近一次提交后的容器 Revision；用于证明鱼的预留或消费事实。 */
	UPROPERTY(BlueprintReadOnly)
	int64 ItemsRevision = 0;

	/** Run 最近一次观察或提交后的 Revision；它不与 Items Revision 合并。 */
	UPROPERTY(BlueprintReadOnly)
	int64 RunRevision = 0;

	/** 本次献祭计入当日额度的条数；额度是纯数量口径，单条献祭恒为 1，未到 Reserved 时保持 0。 */
	UPROPERTY(BlueprintReadOnly)
	int32 AppliedQuotaCount = 0;

	/** 本次献祭对世界进度的增减；从已预留或已提交的真实鱼实例读取，臭臭鱼这类鱼可以为负，未到 Reserved 时保持 0。 */
	UPROPERTY(BlueprintReadOnly)
	int32 AppliedWorldProgressDelta = 0;
};
