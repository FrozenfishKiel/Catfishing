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

/**
 * 声明：重放已有终态时，保证调用方不会把"上次已经写入"误读成"这次又写了一次"；原因是重试路径若据此重复执行副作用，会造成重复扣费与重复发奖。
 * 实现：清掉提交标记并把原因改为 AlreadyResolved，其余字段（RequestId、Revision 及各领域自带数据）保持首次终态原样。
 *       两者缺一不可：保持原样是为了让重试方拿到与首次完全一致的业务结果，不必自己缓存；
 *       清掉 bCommitted 则是因为网络重试、客户端重发和恢复泵都会重放同一 RequestId，
 *       如果这些路径看到 bCommitted=true，就会重复扣耐久、重复入账或重复发奖。
 * 用途：结果类型就是 FCatDomainCommandResult 的领域可直接把它传给 CatQueryTerminalReplay；
 *       结果类型内嵌该结构的领域应自己写一行 lambda 指向内嵌位置。不要把本函数改成模板去猜结构——
 *       各领域的内嵌层级并不一致，猜错会静默改写错字段。
 */
inline void MarkCommandReplayed(FCatDomainCommandResult& Result)
{
	Result.bCommitted = false;
	Result.Error = ECatDomainCommandError::AlreadyResolved;
}

/** 终态重放查询的三种结局；调用方据此决定是直接返回结果，还是继续走首次受理的完整校验链。 */
enum class ECatTerminalReplayOutcome : uint8
{
	/** 这个终态键没有记录，本次是首次受理，调用方应继续正常校验并在结束时写入终态。 */
	FirstAttempt,
	/** 同一终态键已有记录且载荷一致，调用方应立即返回被填充为 AlreadyResolved 的重放结果。 */
	Replayed,
	/** 同一终态键已有记录但载荷不同，说明同一 RequestId 被复用于不同语义意图，调用方应立即返回 InvalidPayload。 */
	PayloadMismatch
};

/**
 * 声明：按终态键查询同一命令是否已有终态，并在载荷一致时把首次结果重放进 OutResult。
 * 实现：先查终态缓存，没有记录直接判为首次受理；有记录时比对同键载荷签名，签名缺失或不同都判为载荷冲突，
 *       签名一致才把缓存结果写回 OutResult 并由 MarkReplayed 改写终态头，使重放不再声称本次发生了写入。
 * 边界：本函数只做重放判定，不写缓存、不改聚合状态；首次受理路径的写入仍由各服务在自己的提交点完成。
 * 泛型说明：ResultType 由各领域自带（可能内嵌 FCatDomainCommandResult），故终态头改写通过 MarkReplayed 回调外置，
 *          由调用方指明该领域的终态头位置，避免本函数假设结果结构。
 * 键类型同样是模板参数：多数领域的幂等键是“身份+用途+RequestId”拼出来的字符串，但也有领域的写口只对单个 Controller 开放，
 *          此时 RequestId 自己就已经唯一，键就是 FGuid。此处原本写死 FString，逼得那类领域只能手抄一份一模一样的判定，
 *          抄出来的副本再各自漂移——这条不变量已经因此复发过多次。载荷签名仍保持 FString：
 *          它是拼给人读的调试串，不需要跟着泛化。
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
