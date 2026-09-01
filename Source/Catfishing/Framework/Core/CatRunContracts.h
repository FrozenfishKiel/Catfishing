#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CatRunContracts.generated.h"

/** 一局公开阶段的稳定语义；状态之间如何转移只由 ST_RunFlow 资产编排，C++ 不维护平行转换表。 */
UENUM(BlueprintType)
enum class ECatRunPhase : uint8
{
	/** Run 与环境真相已初始化，但 StateTree 尚未进入首个可玩状态。 */
	NotStarted,
	/** 白天额度窗口开启；只有该阶段允许提交额度贡献并持有唯一截止计时器。 */
	DayActive,
	/** 当日额度已达成后的普通夜晚；无计时，只等待当前合资格玩家确认翻天。 */
	NormalNight,
	/** 截止时额度未达成后的失败结算夜；无计时，必须等待 SettlementComplete。 */
	FailureSettlementNight,
	/** 产品明确选择成功终局后使用的成功结算夜；默认策略未裁时不可进入。 */
	SuccessSettlementNight,
	/** 结算完成后由 StateTree 进入的一次性收口阶段；禁止再接受玩法命令。 */
	Ending,
	/** StateTree 已完成自然局末拓扑；随后只允许复用 Online RequestLeave 退出。 */
	Ended
};

/** 一局终止原因；QuotaFailed 是结果原因而不是瞬时 Phase，只有进入失败结算夜后才成为终局原因。 */
UENUM(BlueprintType)
enum class ECatRunEndReason : uint8
{
	/** 当前尚无终局原因。 */
	None,
	/** 白天截止时额度不足，并已由 StateTree 进入失败结算夜。 */
	QuotaFailed,
	/** 成功终局策略已明确并完成相应结算。 */
	Success,
	/** 房主主动退出触发强制 teardown；不伪装成自然 StateTree 拓扑。 */
	HostExit,
	/** 正式 runtime gate、数值或 ST_RunFlow 资产缺失，Run 保持 NotStarted。 */
	StartupFailed
};

/** StateTree 事件与结构化结果共享的转移原因；它描述“为什么请求转移”，不直接写公开 Phase。 */
UENUM(BlueprintType)
enum class ECatRunTransitionReason : uint8
{
	/** 当前没有待消费的转移原因。 */
	None,
	/** 当日额度首次达到目标。 */
	QuotaReached,
	/** 白天截止时额度仍不足。 */
	QuotaFailed,
	/** 普通夜晚当前合资格玩家全部确认翻天。 */
	AllEligibleReady,
	/** 失败或成功结算依赖已经收口，可进入 Ending。 */
	SettlementComplete,
	/** 房主离局要求强制停止 RunFlow。 */
	HostExit,
	/** StateTree 自然进入 Ended。 */
	NaturalEnd
};

/** Run 命令的稳定类别；它参与服务器幂等键，避免相同 RequestId 在不同写口互相覆盖。 */
UENUM()
enum class ECatRunCommandType : uint8
{
	/** 已提交献祭结果对当日额度的贡献。 */
	QuotaContribution,
	/** 普通夜晚个人翻天确认。 */
	NextDayReady,
	/** 结算协调器确认所有有界收口已经完成。 */
	SettlementComplete
};

/** Run 写口的稳定错误；错误只描述命令结果，不冒充当前 Phase、Revision 或 teardown 生命周期。 */
UENUM(BlueprintType)
enum class ECatRunCommandError : uint8
{
	/** 命令成功提交。 */
	None,
	/** 当前路径依赖尚未裁决的数值、准入或终局策略。 */
	PolicyUndecided,
	/** Run 已进入 Ending、teardown 或启动失败，不再接受新命令。 */
	CommandsClosed,
	/** 当前公开 Phase 不接受该命令。 */
	InvalidPhase,
	/** StableNetId 无效、未 Active 或不匹配当前 Controller。 */
	InvalidIdentity,
	/** RequestId、贡献量或其他命令载荷无效。 */
	InvalidPayload,
	/** ExpectedRevision 落后或超前于 Run 当前 Revision。 */
	RevisionConflict,
	/** 同一 StableNetId、命令类型与 RequestId 已有首次终态，本次没有重复写入。 */
	AlreadyResolved,
	/** 当前 StateTree 没有运行，不能安全接收转移事件。 */
	StateTreeUnavailable,
	/** 当前玩家不属于本次普通夜晚的合资格集合。 */
	NotEligible,
	/** Run teardown 依赖报告失败，Online 必须保留 Session 并停止退出链。 */
	TeardownFailed
};

/** Environment 拥有的正式天气轴；具体出现概率与转移仍由数据配置，不由 Run 推导。 */
UENUM(BlueprintType)
enum class ECatEnvironmentWeather : uint8
{
	/** 天气未配置；启用鱼种天气过滤后必须 fail-closed，测试期旁路过滤时可作为只读占位。 */
	Unknown,
	/** 晴朗天气。 */
	Clear,
	/** 降雨天气；只影响环境/分布，Wet 的表现触发由 Character 消费。 */
	Rain,
	/** 雾天气。 */
	Fog
};

/** 局内白天的正式时段轴；夜晚不进入该枚举，因为夜晚禁止钓鱼。 */
UENUM(BlueprintType)
enum class ECatEnvironmentTimeOfDay : uint8
{
	/** 时段未配置或当前不是可钓白天；启用鱼种时段过滤后必须 fail-closed。 */
	Unknown,
	/** 白天开始段。 */
	Morning,
	/** 白天中段。 */
	Day,
	/** 白天结束段。 */
	Dusk
};

/** Run 唯一写入的阶段与时钟快照；Environment、Fishing 和 UI 只能消费，不得反向修改。 */
USTRUCT(BlueprintType)
struct FCatRunPhaseSnapshot
{
	GENERATED_BODY()

	/** 当前 Lake 一局的随机标识；StartPlay 初始化，World 销毁后失效。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid RunId;

	/** 当前天序号；首个 DayActive 为 1，仅在 StateTree 进入新的 DayActive 时递增。 */
	UPROPERTY(BlueprintReadOnly)
	int32 DayIndex = 0;

	/** 当前由 ST_RunFlow 进入的公开阶段；客户端不得据此自行推进下一阶段。 */
	UPROPERTY(BlueprintReadOnly)
	ECatRunPhase Phase = ECatRunPhase::NotStarted;

	/** 最近一次阶段/截止事实发布时的服务器时间锚点，单位秒；客户端据此本地推算而非等待每秒复制。 */
	UPROPERTY(BlueprintReadOnly)
	double ServerTimeAnchorSeconds = 0.0;

	/** 当前白天的服务器截止时间，单位秒；bHasDeadline=false 时该值为 0 且不得用于夜晚倒计时。 */
	UPROPERTY(BlueprintReadOnly)
	double DeadlineServerTimeSeconds = 0.0;

	/** 当前是否存在白天截止点；进入任何夜晚、Ending、Ended 或 teardown 都必须清为 false。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasDeadline = false;

	/** 当前是否允许钓鱼规则消费本阶段；白天截止或额度完成后会先关闭，夜晚始终为 false。 */
	UPROPERTY(BlueprintReadOnly)
	bool bFishingAllowed = false;

	/** 当前是否仍接受额度贡献；它在 DayActive 内提供截止/达标竞争的精确服务器边界。 */
	UPROPERTY(BlueprintReadOnly)
	bool bQuotaOpen = false;
};

/** Environment 对外发布的最小只读快照；不包含 Run Phase，避免复制第二份日夜真相。 */
USTRUCT(BlueprintType)
struct FCatEnvironmentSnapshot
{
	GENERATED_BODY()

	/** 当前天气语义；正式 Environment provider 从显式配置生产，Unknown 表示未配置或生产失败，消费者不得自行猜测天气。 */
	UPROPERTY(BlueprintReadOnly)
	ECatEnvironmentWeather Weather = ECatEnvironmentWeather::Unknown;

	/** 当前局内白天时段；Environment 只从 Run 服务器时钟锚点计算，不读取现实时间。 */
	UPROPERTY(BlueprintReadOnly)
	ECatEnvironmentTimeOfDay TimeOfDay = ECatEnvironmentTimeOfDay::Unknown;

	/** 当前是否存在已裁决的公共环境事件；配置未就绪时环境提供者返回 false。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasActiveEvent = false;

	/** 当前公共环境事件的稳定名称；无事件时为 NAME_None，不预建天气/营地/印记空类。 */
	UPROPERTY(BlueprintReadOnly)
	FName ActiveEventId = NAME_None;

	/** 本环境结果消费的 Run Revision；用于证明它对应哪一份只读阶段快照。 */
	UPROPERTY(BlueprintReadOnly)
	int64 SourceRunRevision = 0;
};

/** Environment 只读求值结果；实现只能返回快照或结构化失败，不能回写 Run 或自行复制。 */
USTRUCT(BlueprintType)
struct FCatEnvironmentResult
{
	GENERATED_BODY()

	/** Environment 是否成功消费输入快照；false 时 Run 会发布同 Revision 的空环境，避免旧环境事实跨阶段残留。 */
	UPROPERTY(BlueprintReadOnly)
	bool bSucceeded = false;

	/** 环境求值后的快照；成功时携带真实语义，失败时至少携带输入 Run Revision 供调用方 fail-closed 发布。 */
	UPROPERTY(BlueprintReadOnly)
	FCatEnvironmentSnapshot Snapshot;

	/** 失败诊断文本；只进入服务器日志，不作为客户端状态机分支。 */
	UPROPERTY(BlueprintReadOnly)
	FString Error;
};

/** GameState 复制的一局公开事实；服务器 GameMode 是唯一写者，客户端只读取这一份组合快照。 */
USTRUCT(BlueprintType)
struct FCatRunPublicState
{
	GENERATED_BODY()

	/** 当前 Run 的阶段与时间锚点快照；不按秒改写或复制。 */
	UPROPERTY(BlueprintReadOnly)
	FCatRunPhaseSnapshot Phase;

	/** 与当前 Run Revision 对齐的环境结果；不重复保存 Phase。 */
	UPROPERTY(BlueprintReadOnly)
	FCatEnvironmentSnapshot Environment;

	/** 当日已提交的额度贡献总量；只由 GameMode 幂等命令写口增加。 */
	UPROPERTY(BlueprintReadOnly)
	int32 QuotaProgress = 0;

	/** 当前已配置的当日目标；默认 Unset，只有 RunSettings 显式 runtime gate 后才发布正值。 */
	UPROPERTY(BlueprintReadOnly)
	int32 QuotaTarget = 0;

	/** Run Aggregate 的单调 Revision；所有读后写命令必须提交匹配的 ExpectedRevision。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;

	/** 当前终局原因；QuotaFailed 只在失败结算夜成立，不作为独立 Phase。 */
	UPROPERTY(BlueprintReadOnly)
	ECatRunEndReason EndReason = ECatRunEndReason::None;

	/** Host teardown 是否已经停止本地写口并完成远端 Destroy/最终 Grant ACK 的统一有界收口；Pending 期间保持 false。 */
	UPROPERTY(BlueprintReadOnly)
	bool bTeardownComplete = false;
};

/** 所有 Run 命令共享的幂等与并发上下文；StableNetId 由服务器 Controller 适配器写入，不信任客户端参数。 */
USTRUCT(BlueprintType)
struct FCatRunCommandContext
{
	GENERATED_BODY()

	/** 调用方为本次语义命令生成的随机标识；与 StableNetId、命令类型共同组成幂等键。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid RequestId;

	/** 调用方基于公开快照观察到的版本；GameMode 用它拒绝陈旧额度/ready 意图，防止跨阶段写入。 */
	UPROPERTY(BlueprintReadWrite)
	int64 ExpectedRevision = 0;

	/** 服务器从 APlayerState::UniqueId 派生的局内身份键；不复制到公开快照或日志原文。 */
	FString StableNetId;
};

/** 已提交献祭结果对 Run 额度的命令；它不拥有鱼或 Items 事务，只消费牺牲协调器提供的 committed Result。 */
USTRUCT(BlueprintType)
struct FCatQuotaContributionCommand
{
	GENERATED_BODY()

	/** 额度写口的幂等与 Revision 上下文。 */
	UPROPERTY(BlueprintReadWrite)
	FCatRunCommandContext Context;

	/** 本次已提交结果贡献的正整数；具体额度曲线与人数缩放由 RunSettings gate 决定。 */
	UPROPERTY(BlueprintReadWrite)
	int32 Contribution = 0;
};

/** 普通夜晚个人翻天确认命令；PlayerState 只复制本人的最终 ready 事实。 */
USTRUCT(BlueprintType)
struct FCatNextDayReadyCommand
{
	GENERATED_BODY()

	/** Ready 写口的幂等与 Revision 上下文。 */
	UPROPERTY(BlueprintReadWrite)
	FCatRunCommandContext Context;

	/** 玩家希望发布的确认值；全员事件发出后本夜窗口关闭，不能再撤销已触发转移。 */
	UPROPERTY(BlueprintReadWrite)
	bool bReady = true;
};

/** Run 命令的首次完整终态；重复幂等键只返回 AlreadyResolved 与原提交 Revision，不重复修改真相。 */
USTRUCT(BlueprintType)
struct FCatRunCommandResult
{
	GENERATED_BODY()

	/** 命令是否实际写入 Run 或个人 ready 真相；同步拒绝与重复重放均为 false。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCommitted = false;

	/** 与输入命令相同的 RequestId；用于 RPC 日志、重试和终态缓存定位。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid RequestId;

	/** 命令终态错误；None 表示首次成功，AlreadyResolved 表示缓存命中且未重写。 */
	UPROPERTY(BlueprintReadOnly)
	ECatRunCommandError Error = ECatRunCommandError::None;

	/** 首次终态对应的提交后 Run Revision；拒绝时为当前 Revision。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;

	/** 结算该命令时观察到的公开 Phase；它不授权调用方自行转移。 */
	UPROPERTY(BlueprintReadOnly)
	ECatRunPhase Phase = ECatRunPhase::NotStarted;

	/** 首次提交是否产生 StateTree 转移原因；None 表示只更新数值或个人 ready。 */
	UPROPERTY(BlueprintReadOnly)
	ECatRunTransitionReason TransitionReason = ECatRunTransitionReason::None;
};

/** StateTree Task、Condition 与事件载荷共享的结构化结果；只有 GameMode 能创建并保存最新值。 */
USTRUCT(BlueprintType)
struct FCatRunTransitionResult
{
	GENERATED_BODY()

	/** 请求的阶段/结果是否已被 GameMode 接受；false 时 StateTree Task 返回 Failed。 */
	UPROPERTY(BlueprintReadOnly)
	bool bApplied = false;

	/** 处理前的公开阶段；事件原因提交但未转移时与 CurrentPhase 相同。 */
	UPROPERTY(BlueprintReadOnly)
	ECatRunPhase PreviousPhase = ECatRunPhase::NotStarted;

	/** 处理后的公开阶段；只有 EnterPhase Task 可以使其变化。 */
	UPROPERTY(BlueprintReadOnly)
	ECatRunPhase CurrentPhase = ECatRunPhase::NotStarted;

	/** 本次状态进入或外部事件的语义原因；资产据此选择转移而非 C++ switch 拓扑。 */
	UPROPERTY(BlueprintReadOnly)
	ECatRunTransitionReason Reason = ECatRunTransitionReason::None;

	/** GameMode 拒绝本次 StateTree 操作的结构化原因。 */
	UPROPERTY(BlueprintReadOnly)
	ECatRunCommandError Error = ECatRunCommandError::None;

	/** 本结果对应的 Run Revision。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;
};

/** Host Online Leave 发给 Run 的 teardown 状态；Pending 表示当前 Fishing/Social/Items/Imprint/remote ACK 的统一有界收口尚未完成。 */
UENUM()
enum class ECatRunTeardownStatus : uint8
{
	/** Run 已完成本阶段全部收口，Online 可以继续 DestroySession。 */
	Ready,
	/** 当前管线已关闭并收口 Fishing/Social/Items/Imprint，正在有界等待远端退出 ACK 与 durable Grant ACK；Online 必须等待同一 RequestId/epoch 回调。 */
	Pending,
	/** Run 无法安全收口，Online 必须停止 Destroy/旅行链。 */
	Failed
};

/** Online 与 Run teardown 的关联请求；Online RequestId 与 epoch 必须原样贯穿迟到回调过滤。 */
USTRUCT()
struct FCatRunTeardownRequest
{
	GENERATED_BODY()

	/** 当前 Online Leave 的稳定 RequestId。 */
	FGuid RequestId;

	/** 当前 Online 操作代际；完成回调必须精确匹配，旧 World 结果不得推进新退出。 */
	int64 OperationEpoch = 0;
};

/** Run teardown 的结构化结果；Ready/Pending/Failed 均携带原 RequestId/epoch，供 Online 幂等过滤。 */
USTRUCT()
struct FCatRunTeardownResult
{
	GENERATED_BODY()

	/** 当前 teardown 生命周期状态。 */
	ECatRunTeardownStatus Status = ECatRunTeardownStatus::Failed;

	/** 与请求相同的 Online RequestId。 */
	FGuid RequestId;

	/** 与请求相同的 Online epoch。 */
	int64 OperationEpoch = 0;

	/** Failed 的结构化原因；Ready/Pending 保持 None。 */
	ECatRunCommandError Error = ECatRunCommandError::None;
};

/** Run teardown 终态通知；Online 订阅后仍必须复核 RequestId 与 epoch，避免同步或迟到完成重入。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatRunTeardownCompleted, const FCatRunTeardownResult&);

/** Core 中的 Environment 只读求值接口；Run 只认识该合同，不依赖 Environment 具体实现。 */
UINTERFACE(MinimalAPI)
class UCatEnvironmentProvider : public UInterface
{
	GENERATED_BODY()
};

/** Environment 实现消费不可变 Run 快照并返回 Result；禁止持有 Run 写指针或反向修改 GameMode。 */
class CATFISHING_API ICatEnvironmentProvider
{
	GENERATED_BODY()

public:
	/** 只读消费阶段快照与对应 Revision，返回环境结果；失败时不得产生第二份 Phase 或修改输入。 */
	virtual FCatEnvironmentResult EvaluateEnvironment(const FCatRunPhaseSnapshot& RunSnapshot, int64 RunRevision) const = 0;
};
