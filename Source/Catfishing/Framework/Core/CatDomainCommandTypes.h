#pragma once

#include "CoreMinimal.h"
#include "CatDomainCommandTypes.generated.h"

/** E/F/G 领域命令共享的策略门；Unset 明确表示尚未裁决，不能被当作 Disabled 或测试默认。 */
UENUM(BlueprintType)
enum class ECatDomainPolicy : uint8
{
	/** 产品或原型策略尚未给出，所有依赖路径必须 fail-closed。 */
	Unset,
	/** 策略已经明确禁止。 */
	Disabled,
	/** 策略已经明确启用。 */
	Enabled
};

/** 跨 Fishing、Items 与 Run 的稳定拒绝语义；服务可附加自己的细分错误，但不能用成功布尔值掩盖失败原因。 */
UENUM(BlueprintType)
enum class ECatDomainCommandError : uint8
{
	/** 首次命令已经完整提交。 */
	None,
	/** RequestId、引用或载荷无效。 */
	InvalidPayload,
	/** 服务器无法从当前 Controller 得到有效且已激活的 StableNetId。 */
	InvalidIdentity,
	/** 对应 runtime gate、数值、资产或公平策略尚未裁决。 */
	PolicyUndecided,
	/** 当前局阶段或聚合阶段不接受该命令。 */
	InvalidPhase,
	/** 目标鱼、容器、会话或协议记录不存在。 */
	NotFound,
	/** 调用方依据的聚合版本已经陈旧；服务保持当前事实不写入，并返回最新 Revision 供重读。 */
	RevisionConflict,
	/** StableNetId 对目标实体没有所需权限。 */
	PermissionDenied,
	/** 容器容量或其他显式资源上限不足。 */
	CapacityExceeded,
	/** 同一聚合已经由另一个合法请求提交终态。 */
	AlreadyResolved,
	/** 命令在不可逆提交点之前被取消。 */
	Cancelled,
	/** StateTree、World、定义表或领域服务等运行依赖不可用。 */
	DependencyUnavailable,
	/** Host teardown 后服务已经关闭新命令。 */
	CommandsClosed
};

/** 所有可重放领域命令的关联上下文；StableNetId 只允许服务器适配器填写，不信任客户端载荷。 */
USTRUCT(BlueprintType)
struct FCatDomainCommandContext
{
	GENERATED_BODY()

	/** 调用方为这次语义意图生成的稳定标识；服务把它与身份、命令类别和聚合 ID 一起去重。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid RequestId;

	/** 调用方读取目标聚合时看到的版本；服务以它阻止陈旧命令覆盖较新事实，冲突时只返回当前版本。 */
	UPROPERTY(BlueprintReadWrite)
	int64 ExpectedRevision = 0;

	/** 服务器从 APlayerState::UniqueId 派生的局内身份键；不向蓝图或公开快照暴露为第二份身份真相。 */
	FString StableNetId;
};

/** 领域命令的公共终态头；具体服务在其后附加实体 ID 和不可变提交数据。 */
USTRUCT(BlueprintType)
struct FCatDomainCommandResult
{
	GENERATED_BODY()

	/** 本次是否发生了首次不可逆写入；缓存重放和同步拒绝都为 false。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCommitted = false;

	/** 与输入命令一致的 RequestId，供重试方关联首次终态。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid RequestId;

	/** 结构化终态原因；None 只允许与首次提交成功一起出现。 */
	UPROPERTY(BlueprintReadOnly)
	ECatDomainCommandError Error = ECatDomainCommandError::InvalidPayload;

	/** 处理后目标聚合的 Revision；拒绝时返回当前 Revision，避免调用方继续使用陈旧快照。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;
};

/** 把首次终态改写成安全的幂等重放结果，避免上层重复执行扣款、发奖等副作用。 */
inline void MarkCommandReplayed(FCatDomainCommandResult& Result)
{
	Result.bCommitted = false;
	Result.Error = ECatDomainCommandError::AlreadyResolved;
}

/** 查询幂等终态缓存后的三种结论。 */
enum class ECatTerminalReplayOutcome : uint8
{
	FirstAttempt,
	Replayed,
	PayloadMismatch
};

/**
 * 共享的终态重放检查。缓存键可以是 FString、FGuid 等领域自己的稳定键；载荷签名防止调用方
 * 复用同一个 RequestId，却偷偷替换订单、物品或版本前提。
 */
template <typename KeyType, typename ResultType, typename MarkReplayedFunc>
ECatTerminalReplayOutcome CatQueryTerminalReplay(
	const TMap<KeyType, ResultType>& TerminalCache,
	const TMap<KeyType, FString>& PayloadByKey,
	const KeyType& TerminalKey,
	const FString& PayloadSignature,
	ResultType& OutResult,
	MarkReplayedFunc MarkReplayed)
{
	const ResultType* Cached = TerminalCache.Find(TerminalKey);
	if (!Cached)
	{
		return ECatTerminalReplayOutcome::FirstAttempt;
	}
	const FString* CachedPayload = PayloadByKey.Find(TerminalKey);
	if (!CachedPayload || *CachedPayload != PayloadSignature)
	{
		return ECatTerminalReplayOutcome::PayloadMismatch;
	}
	OutResult = *Cached;
	MarkReplayed(OutResult);
	return ECatTerminalReplayOutcome::Replayed;
}
