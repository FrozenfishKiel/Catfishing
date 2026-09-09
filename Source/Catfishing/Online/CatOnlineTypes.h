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
	/** 已提交去联机玩法地图的旅行，仍等待目标 World 到达；枚举名 Lake 为兼容既有蓝图而保留。 */
	TravelingToLake,
	/** 当前 World 是联机玩法地图（目前为 Showcase2）；枚举名 Lake 为兼容既有蓝图而保留。 */
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
	/** CreateSession 操作；成功后只建立前台房间，玩法旅行由独立 Start 操作负责。 */
	Create,
	/** FindSessions 操作。 */
	Find,
	/** 搜索结果或已接受邀请汇入的 Join 操作。 */
	Join,
	/** Host 玩法包预载或 Client 收到真实 Host Start 后的旅行操作。 */
	Start,
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
	/** Client 预载完成后无法从 NamedSession 解析连接地址。 */
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
	UnexpectedMap,
	/** 当前平台没有提供 Friends 接口，无法刷新 Steam 好友缓存。 */
	FriendsInterfaceUnavailable,
	/** Steam 好友列表异步读取失败，保留上一代已确认好友事实。 */
	FriendsRefreshFailed,
	/** 当前好友句柄、Host 身份或平台邀请请求无效。 */
	InviteFailed,
	/** Host 尚未成功读取可恢复世界存档，不能开始玩法包预载。 */
	SaveNotLoaded,
	/** 玩法地图异步预载提交失败或回调未得到有效包，禁止旅行。 */
	GameplayPreloadFailed,
	/** Client 无法确认 Host 已提供可加入的 ready 事实；只阻止 Client 自动进图，Host Start 不因此回滚，真实旅行或网络故障使用对应错误。 */
	LobbyReadyPublishFailed,
	/** Host 离开前的世界保存被拒绝、Run 或 Index 写盘失败；退出停止且 Session 保持可用。 */
	HostSaveFailed,
	/** 平台已接受邀请，但另一个 Online 操作或邀请仍在处理；不抢占当前操作，用户需在空闲后重新接受邀请。 */
	InviteAcceptanceBusy,
	/** 当前已拥有 Session，不能自动替用户退出或切换房间；先离房再重新接受邀请。 */
	InviteSessionConflict,
	/** 平台邀请无有效结果、接受账号不匹配或当前地图不允许加入；不以另一账号或替代房间继续。 */
	InviteAcceptanceUnavailable,
	/** 已接受邀请等待前台和本地 Steam 身份就绪超过期限；意图失效，停止自动提交。 */
	InviteAcceptanceExpired,
	/** 会话已离开但本局载荷释放服务缺失或拒绝；不伪造释放成功，后续选槽仍受 Save 的真实状态约束。 */
	ActiveRunReleaseFailed
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

/** Steam 好友列表中的不透明平台身份；UI 只能把它交回邀请入口，不能把值解释成账户、票据或房间 ID。 */
USTRUCT(BlueprintType)
struct FCatOnlineFriendHandle
{
	GENERATED_BODY()

	/** 当前好友缓存代际中的随机键；Online 子系统写入，房间页原样传回，刷新或反初始化后失效。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid Value;

	/** 只检查随机键是否非空，供调用者排除未选择的好友；非空不代表仍属于当前缓存，过期句柄仍由 Online 私有映射拒绝。 */
	bool IsValid() const { return Value.IsValid(); }
};

/** 房间页可展示的 Steam 好友事实；显示信息来自 OSS Friends 缓存，邀请权限仍由 Online 子系统在提交时裁决。 */
USTRUCT(BlueprintType)
struct FCatOnlineFriendSummary
{
	GENERATED_BODY()

	/** 邀请按钮提交时原样交回的当前缓存句柄。 */
	UPROPERTY(BlueprintReadOnly)
	FCatOnlineFriendHandle Handle;

	/** 平台公开的好友显示名；仅用于房间页文本，不作为身份键或日志字段。 */
	UPROPERTY(BlueprintReadOnly)
	FString DisplayName;

	/** 好友是否在线；由 OSS Friends 缓存报告，离线时邀请入口会明确拒绝。 */
	UPROPERTY(BlueprintReadOnly)
	bool bIsOnline = false;

	/** 好友是否正在运行本游戏或另一个可加入会话；这是平台观察值，不代表本地 Session 已邀请成功。 */
	UPROPERTY(BlueprintReadOnly)
	bool bIsPlayingThisGame = false;

	/** 当前好友缓存代际是否已成功提交过一次平台邀请；刷新好友后重置，平台不回执时不把它当作对方已接受。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasInvited = false;
};

/** 当前 Steam Lobby 中的一条真实成员记录；成员数组只在平台已确认本地位于 Lobby 时填充，空数组不伪造人数。 */
USTRUCT(BlueprintType)
struct FCatOnlineRoomMember
{
	GENERATED_BODY()

	/** 平台公开的成员显示名；Steam Friends 接口读取，不能作为稳定身份或权限凭据。 */
	UPROPERTY(BlueprintReadOnly)
	FString DisplayName;

	/** 此成员是否是 Steam Lobby 当前 owner；平台 owner 与本地创建者角色同时供 UI 展示，开始权限仍以后者为准。 */
	UPROPERTY(BlueprintReadOnly)
	bool bIsLobbyOwner = false;
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

	/** 平台层已接受但尚未提交 Join 的邀请摘要；Online 自动消费一次，失败后须在 Steam 重新接受，不要求前端提供确认页。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatSessionInviteSummary> AcceptedInvites;

	/** 已接受的平台邀请正在等待前台和本地身份就绪；Online 在有限期限内写入与清空，Model 只据此显示等待文本。 */
	UPROPERTY(BlueprintReadOnly)
	bool bIsAcceptedInvitePending = false;

	/** OSS Friends 缓存的公开好友摘要；ReadFriendsList 完成后整代替换，未加载时保持空数组而不是虚构离线好友。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatOnlineFriendSummary> Friends;

	/** 当前 Lobby 的真实成员记录；只有 Steam SDK 确认本地是该 Lobby 成员时填充。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatOnlineRoomMember> RoomMembers;

	/** 当前房间可展示名称；优先读取 Steam Lobby 元数据，缺失时才保留 OSS 已确认的房主显示名。 */
	UPROPERTY(BlueprintReadOnly)
	FString RoomName;

	/** 当前 Session 实际公开的访问策略；从 NamedSession 设置推导，未能验证时保持 Undecided。 */
	UPROPERTY(BlueprintReadOnly)
	ECatSessionAccessPolicy SessionAccess = ECatSessionAccessPolicy::Undecided;

	/** 当前 NamedSession 关联的真实 Steam Lobby ID；不存在或非 Steam Lobby 时为空，不生成替代邀请码。 */
	UPROPERTY(BlueprintReadOnly)
	FString LobbyId;

	/** 可交给 Steam 客户端的真实 joinlobby URI；只能在 LobbyId 与 owner 均经平台确认时存在。 */
	UPROPERTY(BlueprintReadOnly)
	FString JoinLobbyUri;

	/** 当前 Session 设置报告的最大公开连接数；不是 UI 默认值，无法读取时保留零。 */
	UPROPERTY(BlueprintReadOnly)
	int32 MaxPlayers = 0;

	/** 当前 Session 设置报告的已占用公开连接数；成员列表无法读取时仍不把这个值展开成伪成员。 */
	UPROPERTY(BlueprintReadOnly)
	int32 CurrentPlayers = 0;

	/** 当前进程是否是创建该 NamedSession 的 Host；只由 Create/Join 回调写入的 SessionRole 推导。 */
	UPROPERTY(BlueprintReadOnly)
	bool bIsHost = false;

	/** 当前 Start 流程是否已把玩法地图异步预载请求提交给引擎；Host 和 Client 都会写入，完成回调前只代表包请求仍挂起，不代表旅行已开始。 */
	UPROPERTY(BlueprintReadOnly)
	bool bIsGameplayLoadPending = false;

	/** 当前 Start 或回主菜单流程是否有地图包预载请求仍在引擎异步队列中；它只说明包还没回调，不代表世界已经切换完成。 */
	UPROPERTY(BlueprintReadOnly)
	bool bIsMapPreloadPending = false;

	/** 当前 GameInstance 是否已经进入引擎 LoadMap 阻塞段；由 PreLoadMap/PostLoadMap 成对写入，UI 用它保持真实切图遮罩而不靠时间兜底。 */
	UPROPERTY(BlueprintReadOnly)
	bool bIsEngineLoadMapPending = false;

	/** 最近一次进入引擎 LoadMap 的目标地图名；只用于展示和诊断当前正在等待哪个引擎切图阶段，PostLoadMap 后清空。 */
	UPROPERTY(BlueprintReadOnly)
	FString EngineLoadMapName;

	/** 当前地图包是否提供可读取的引擎加载百分比；false 表示 Online 此刻没有可量化进度，UI 不能用时间或动画自行编百分比。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasMapLoadProgress = false;

	/** 当前地图包加载百分比，单位是 0 到 100；只有 bHasMapLoadProgress 为 true 时才是有效 Model 数据，View 才允许写入进度条。 */
	UPROPERTY(BlueprintReadOnly)
	float MapLoadProgressPercent = 0.0f;
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
