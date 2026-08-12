#pragma once

#include "CoreMinimal.h"
#include "CatOnlineTypes.generated.h"

/** 前台会话可见性与准入方式；该产品策略未裁决时保持 Undecided，并阻止创建会话。 */
UENUM(BlueprintType)
enum class ECatSessionAccessPolicy : uint8
{
	/** 尚未决定公开搜索、好友可见或仅邀请中的哪一种组合。 */
	Undecided,
	/** 会话可被平台搜索，并允许通过 Presence 加入。 */
	Public,
	/** 会话只允许好友 Presence 或平台邀请进入。 */
	FriendsOnly,
	/** 会话不开放搜索，只接受平台邀请。 */
	InviteOnly
};

/** 尚未裁决的二值策略通用表示；Undecided 不能被实现层解释成允许或禁止。 */
UENUM(BlueprintType)
enum class ECatPolicyDecision : uint8
{
	/** 产品尚未裁决，任何依赖该策略的路径必须返回 PolicyUndecided。 */
	Undecided,
	/** 策略明确关闭。 */
	Disabled,
	/** 策略明确开启。 */
	Enabled
};

/** 地图事实只描述当前 GameInstance 所在 World，不代表 Session 或网络驱动已经成功。 */
UENUM(BlueprintType)
enum class ECatOnlineWorldState : uint8
{
	/** 尚未从有效 World 包名建立事实。 */
	Unknown,
	/** 当前 World 是 Frontend。 */
	Frontend,
	/** 已提交去 Lake 的旅行，仍等待目标 World 到达。 */
	TravelingToLake,
	/** 当前 World 是 Lake。 */
	Lake,
	/** 已提交回 Frontend 的旅行，仍等待目标 World 到达。 */
	TravelingToFrontend,
	/** 当前包名不属于两地图合同，无法建立安全 World 事实。 */
	Error
};

/** NamedSession 事实只描述本地 OSS 会话生命周期，不把 World 或 NetDriver 状态伪装成会话结果。 */
UENUM(BlueprintType)
enum class ECatOnlineSessionState : uint8
{
	/** 本地没有已知 NamedSession。 */
	NoSession,
	/** CreateSession 已提交，等待平台回调。 */
	Creating,
	/** FindSessions 已提交，等待平台回调。 */
	Searching,
	/** JoinSession 已提交，等待平台回调。 */
	Joining,
	/** 本地 NamedSession 已由 CreateSession 成功建立。 */
	Host,
	/** 本地 NamedSession 已由 JoinSession 成功建立。 */
	Client,
	/** DestroySession 已提交，等待平台回调。 */
	Destroying,
	/** 会话事实无法安全确认，需要用户重试或重新进入前台。 */
	Error
};

/** 运输事实只描述旅行或网络失败，不代替 NamedSession 和 World 的各自终态。 */
UENUM(BlueprintType)
enum class ECatOnlineTransportState : uint8
{
	/** 当前没有待确认的地图运输。 */
	Idle,
	/** 旅行 API 已受理，尚未由 PostLoadMap 确认目标 World。 */
	TravelQueued,
	/** 目标 World 已到达；该值不证明平台 Session 仍然存在。 */
	Connected,
	/** TravelFailure 或 NetworkFailure 已报告运输失败。 */
	Failed
};

/** 当前唯一异步操作；它与 Session、World、Transport 三类事实分开保存。 */
UENUM(BlueprintType)
enum class ECatOnlineOperation : uint8
{
	/** 没有平台或退出操作在等待终态。 */
	None,
	/** CreateSession 与成功后的 Listen 旅行组成同一个操作。 */
	Create,
	/** FindSessions 操作。 */
	Find,
	/** 搜索结果或已接受邀请汇入的 Join 操作。 */
	Join,
	/** Host 或 Client 的本地 DestroySession 与回前台旅行。 */
	Leave
};

/** 本地 NamedSession 角色；离局入口用它选择 Host 与 Client 的不同旅行方式。 */
UENUM(BlueprintType)
enum class ECatOnlineSessionRole : uint8
{
	/** 当前没有已确认的会话角色。 */
	None,
	/** 当前进程创建了 NamedSession，并以 Listen Server 承载 Lake。 */
	Host,
	/** 当前进程加入了远端 NamedSession。 */
	Client
};

/** Online 请求的稳定错误；UI 只展示结果，不据错误自行执行 Session 或旅行补偿。 */
UENUM(BlueprintType)
enum class ECatOnlineError : uint8
{
	/** 最近一次操作没有失败。 */
	None,
	/** 另一异步操作仍在等待终态。 */
	CommandAlreadyPending,
	/** 当前 World、Session 或角色不允许该请求。 */
	InvalidState,
	/** 当前请求依赖尚未裁决的 O 策略。 */
	PolicyUndecided,
	/** 当前 GameInstance 没有可用 World。 */
	WorldUnavailable,
	/** World 对应的 OnlineSubsystem 没有初始化。 */
	OnlineSubsystemUnavailable,
	/** OnlineSubsystem 没有提供 Session 接口。 */
	SessionInterfaceUnavailable,
	/** 平台 API 返回同步 false，且同一 epoch 的完成回调尚未先行结案。 */
	RequestRejected,
	/** CreateSession 回调失败。 */
	CreateFailed,
	/** FindSessions 回调失败。 */
	FindFailed,
	/** JoinSession 回调失败。 */
	JoinFailed,
	/** DestroySession 回调失败，本地 NamedSession 事实仍不安全。 */
	DestroyFailed,
	/** Create/Join 的 Presence 与 Lobby 兼容字段不满足同值合同。 */
	SessionCompatibilityMismatch,
	/** UI 提交的搜索或邀请句柄不属于当前代际；新搜索、成功 Join、补偿、Leave 或销毁都会让旧句柄失效。 */
	InvalidHandle,
	/** Join 成功后无法从 NamedSession 解析连接地址。 */
	ConnectStringUnavailable,
	/** ServerTravel 无权执行或同步拒绝 URL。 */
	TravelRejected,
	/** 已受理旅行随后由引擎报告 TravelFailure。 */
	TravelFailed,
	/** 引擎报告当前 GameInstance 的 NetworkFailure。 */
	NetworkFailure,
	/** 房主离局前 Run 无法完成权威收口；Session 保持原状且退出链停止。 */
	RunTeardownFailed,
	/** 到达的包不符合两地图或当前操作的预期目标。 */
	UnexpectedMap
};

/** View 能表达的最小会话意图；LocalPlayer UI 子系统将其翻译成 Online 公共接口调用。 */
UENUM()
enum class ECatOnlineUIAction : uint8
{
	/** 创建 Session；成功后由 Online 子系统内部进入 Lake Listen Server。 */
	Host,
	/** 搜索可加入的 Session。 */
	Find,
	/** 使用 View 当前保存的 opaque 搜索句柄加入 Session。 */
	Join,
	/** 使用 View 当前保存的 opaque 已接受邀请句柄汇入统一 Join 管线。 */
	AcceptInvite,
	/** 根据已确认角色执行 Host exit 或 Client leave。 */
	Leave
};

/** 对 UI 暴露的搜索句柄；Value 只在当前 GameInstance 的 Online 子系统内部可解析。 */
USTRUCT(BlueprintType)
struct FCatSessionSearchHandle
{
	GENERATED_BODY()

	/** 当前搜索结果的随机 opaque 标识；不得被解释成平台 SessionId。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid Value;

	/** 判断该句柄是否携带可查询标识；实现只读 Value，不访问 OnlineSubsystem。 */
	bool IsValid() const { return Value.IsValid(); }
};

/** 对 UI 暴露的已接受邀请句柄；平台搜索结果只保存在 Online 子系统内部。 */
USTRUCT(BlueprintType)
struct FCatSessionInviteHandle
{
	GENERATED_BODY()

	/** 当前邀请的随机 opaque 标识；不得被解释成邀请者 StableNetId。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid Value;

	/** 判断邀请是否仍可作为本 GameInstance 私有映射的查询键；只读随机 Value，避免 View 通过验证动作接触邀请者平台身份。 */
	bool IsValid() const { return Value.IsValid(); }
};

/** 可公开展示的搜索摘要；不携带 FOnlineSessionSearchResult 或原始 StableNetId。 */
USTRUCT(BlueprintType)
struct FCatSessionSearchSummary
{
	GENERATED_BODY()

	/** UI 后续 Join 时原样交回的 opaque 句柄。 */
	UPROPERTY(BlueprintReadOnly)
	FCatSessionSearchHandle Handle;

	/** 平台提供的房主显示名；只用于白盒列表，不作为身份键。 */
	UPROPERTY(BlueprintReadOnly)
	FString OwnerDisplayName;

	/** 搜索时观察到的已占用公开连接数。 */
	UPROPERTY(BlueprintReadOnly)
	int32 CurrentPlayers = 0;

	/** 搜索结果声明的公开连接容量。 */
	UPROPERTY(BlueprintReadOnly)
	int32 MaxPlayers = 0;

	/** 平台搜索返回的往返延迟；不可达时保留平台哨兵值。 */
	UPROPERTY(BlueprintReadOnly)
	int32 PingMilliseconds = 0;
};

/** 已在平台层接受、等待游戏汇入 Join 的邀请摘要；不公开邀请者原始身份。 */
USTRUCT(BlueprintType)
struct FCatSessionInviteSummary
{
	GENERATED_BODY()

	/** UI 接受时原样交回的 opaque 邀请句柄。 */
	UPROPERTY(BlueprintReadOnly)
	FCatSessionInviteHandle Handle;

	/** 邀请目标 Session 的平台房主显示名。 */
	UPROPERTY(BlueprintReadOnly)
	FString OwnerDisplayName;
};

/** Online 的只读合成快照；各字段保留来源边界，UI 不从一个字段推断另一个生命周期。 */
USTRUCT(BlueprintType)
struct FCatOnlineSnapshot
{
	GENERATED_BODY()

	/** 当前 World 事实。 */
	UPROPERTY(BlueprintReadOnly)
	ECatOnlineWorldState WorldState = ECatOnlineWorldState::Unknown;

	/** 当前 NamedSession 事实。 */
	UPROPERTY(BlueprintReadOnly)
	ECatOnlineSessionState SessionState = ECatOnlineSessionState::NoSession;

	/** 当前旅行或网络运输事实。 */
	UPROPERTY(BlueprintReadOnly)
	ECatOnlineTransportState TransportState = ECatOnlineTransportState::Idle;

	/** 当前唯一异步操作。 */
	UPROPERTY(BlueprintReadOnly)
	ECatOnlineOperation ActiveOperation = ECatOnlineOperation::None;

	/** 已确认的本地 Session 角色。 */
	UPROPERTY(BlueprintReadOnly)
	ECatOnlineSessionRole SessionRole = ECatOnlineSessionRole::None;

	/** 当前操作的 RequestId；操作终态后仍保留到下一次请求，方便日志和 UI 对齐。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid RequestId;

	/** 当前操作 epoch；每次 Begin/Invalidate 单调增长，用于拒绝迟到回调。 */
	UPROPERTY(BlueprintReadOnly)
	int64 OperationEpoch = 0;

	/** 最近一次结构化错误；成功终态清零，错误不替代其他三类事实。 */
	UPROPERTY(BlueprintReadOnly)
	ECatOnlineError LastError = ECatOnlineError::None;

	/** 当前搜索结果的公开摘要；每次 Find 开始时清空并重新生成 opaque 句柄。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatSessionSearchSummary> SearchResults;

	/** 平台层已接受但尚未成功 Join 的邀请摘要；异步 Join 失败时仍可供玩家重试。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatSessionInviteSummary> AcceptedInvites;
};

/** Online 请求的同步提交结果；Accepted 表示子系统接管了请求，OSS 完成回调可能在本方法返回前就已同步结案，最终事实仍从 Snapshot 读取。 */
USTRUCT(BlueprintType)
struct FCatOnlineResult
{
	GENERATED_BODY()

	/** 请求是否已由子系统接管；true 时操作可能仍异步等待，也可能已被同步完成回调结案。 */
	UPROPERTY(BlueprintReadOnly)
	bool bAccepted = false;

	/** 本次请求的 RequestId；同步拒绝也有独立 ID，便于诊断重复输入。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid RequestId;

	/** 同步拒绝原因；受理时为 None，最终异步结果从 Snapshot 读取。 */
	UPROPERTY(BlueprintReadOnly)
	ECatOnlineError Error = ECatOnlineError::None;
};
