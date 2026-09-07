#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Online/CatOnlineTypes.h"
#include "Engine/EngineBaseTypes.h"
#include "Interfaces/OnlineFriendsInterface.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UObject/UObjectGlobals.h"
#include "CatOnlineSubsystem.generated.h"

class APlayerController;
class ACatfishingGameModeBase;
class UCatSaveSubsystem;
class IOnlineSubsystem;
class UNetDriver;
class UPackage;
struct FCatRunTeardownResult;

/** Online 快照变更通知；订阅者收到通知后重新读取只读 Snapshot，不持有平台对象。 */
DECLARE_MULTICAST_DELEGATE(FCatOnlineSnapshotChanged);

/**
 * GameInstance 级 Steam Session 深模块。
 * 它是 Create/Find/Join/Invite/Leave、补偿清理与两地图旅行的唯一入口，同时让 Session、World、Transport 和 ActiveOperation 保持四份独立事实。
 */
UCLASS()
class CATFISHING_API UCatOnlineSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** 绑定地图、旅行、网络失败和平台邀请回调，启动邀请生命周期检查，并从当前 World 建立初始只读快照。 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** 先让所有 epoch 失效并成对解绑，再清除 opaque 结果和非 UObject 平台引用，保证迟到回调无副作用。 */
	virtual void Deinitialize() override;

	/** 在 Frontend 提交 CreateSession；成功后确立 Host 房间并留在 Frontend，只有后续 Host Start 才能触发玩法预载与 Listen 旅行。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Online")
	FCatOnlineResult RequestCreateSession();

	/** 在 Frontend 提交 FindSessions；重复请求优先返回 CommandAlreadyPending 且不覆盖活动关联键，结果只通过 opaque 句柄和公开摘要进入 Snapshot。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Online")
	FCatOnlineResult RequestFindSessions();

	/** 使用最近一次 Find 生成的 opaque 句柄加入；重复请求先于句柄校验拒绝且不覆盖活动关联键，成功后留在 Frontend 等真实 Steam Lobby ready 再预载和 ClientTravel。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Online")
	FCatOnlineResult RequestJoinSession(FCatSessionSearchHandle SearchHandle);

	/** 使用已接受邀请的 opaque 句柄复用 Join 管线；平台意图由 Online 自动提交一次，重复请求不覆盖活动关联键，失败须重新接受邀请。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Online")
	FCatOnlineResult RequestAcceptInvite(FCatSessionInviteHandle InviteHandle);

	/** 根据已确认 SessionRole 选择 Host exit 或 Client leave；Lake Host 先等最终保存成功再 teardown、Destroy 和回前台，释放本局载荷后才结案，保存失败保留会话。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Online")
	FCatOnlineResult RequestLeave();

	/** Host 在前台房间确认存档已加载后提交玩法包异步预载；只有同一 epoch 的成功回调才提交唯一 Listen 旅行，Client 永远不能调用。 */
	FCatOnlineResult RequestStartHostedGame();

	/** 请求 OSS 刷新 Steam 好友缓存；完成回调整代替换公开摘要，接口或平台不支持时返回结构化拒绝。 */
	FCatOnlineResult RequestRefreshFriends();

	/** 使用好友缓存中的 opaque 句柄向当前 Host Lobby 发送 Steam 邀请；调用者不能直接接触平台身份。 */
	FCatOnlineResult RequestInviteFriend(FCatOnlineFriendHandle FriendHandle);

	/** 查询引擎为当前玩法包返回的真实异步加载比例；没有有效预载请求或引擎未知时返回 -1。 */
	float GetGameplayLoadProgress() const;

	/** 接受服务器 Host exit 通知；并发先于关联键/角色校验拒绝且不覆盖活动关联键，Client 绕过主动离局策略复用 Destroy/Frontend 管线，返回后释放本机载荷。 */
	FCatOnlineResult RequestRemoteHostExit(FGuid HostExitRequestId);

	/** 组装当前四类事实、RequestId/epoch 与 opaque 摘要；实现只复制数据，不推进异步状态。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Online")
	FCatOnlineSnapshot GetSnapshot() const;

	/** 快照事实变更广播；LocalPlayer UI 子系统按自己的生命周期成对订阅。 */
	FCatOnlineSnapshotChanged OnSnapshotChanged;

private:
	/** 按当前 World 取得对应 OSS Session 接口；PIE 多 World 下不得退回进程级无上下文查询。 */
	IOnlineSessionPtr GetWorldSessionInterface() const;

	/** 区分 World、OnlineSubsystem 和 Session 接口三层缺失；若接口在两次查询间恢复则返回 RequestRejected，让调用者重试而不把竞态写成成功。 */
	ECatOnlineError GetSessionInterfaceError() const;

	/** 开始唯一异步操作，使用外部关联键或生成 RequestId 并推进 epoch、撤销旧释放许可；Leave 在首个 pending 快照前废止大厅候选。 */
	FCatOnlineResult BeginOperation(ECatOnlineOperation Operation, ECatOnlineSessionState PendingSessionState,
		FGuid CorrelationRequestId = FGuid());

	/** 生成带独立 RequestId 的同步拒绝，更新 LastError 并广播，但不改变 ActiveOperation。 */
	FCatOnlineResult RejectRequest(ECatOnlineError Error);

	/** 让搜索结果或邀请结果汇入同一 JoinSession/Resolve/ClientTravel 管线。 */
	FCatOnlineResult RequestJoinInternal(const FOnlineSessionSearchResult& SearchResult);

	/** 获取当前 World 对应的 OSS Friends 接口；与 Session 查询一样不退回进程级默认接口。 */
	IOnlineFriendsPtr GetWorldFriendsInterface() const;

	/** 从当前 NamedSession 抽取 Steam Lobby 的可展示事实；不是 Steam Lobby 或本地并非成员时只保留 Session 人数，不制造成员行。 */
	void RefreshRoomSnapshotFacts();

	/** 从已完成的 OSS Friends 缓存建立新的 opaque 好友句柄映射和公开摘要；原始平台身份只保留在私有映射中。 */
	void RefreshFriendSnapshotFacts();

	/** Friends 回调只消费开始刷新时冻结的 epoch；成功读取缓存，失败保留前一代事实并发布错误。 */
	void HandleReadFriendsListComplete(int32 LocalUserNum, bool bWasSuccessful, const FString& ListName, const FString& ErrorStr, uint64 CallbackEpoch);

	/** 玩法包异步加载回调只消费仍归属 Host 或 Client Start 的 epoch；失败不旅行，Host 成功后提交唯一 Listen 旅行，Client 成功后复核 Lobby ready 并连接，二者都保持包可达直至地图切换。 */
	void HandleGameplayPackagePreloadComplete(const FName& PackageName, UPackage* LoadedPackage, EAsyncLoadingResult::Type Result, uint64 CallbackEpoch);

	/** 启动当前有效 Steam Lobby 的低频事实轮询；Steam 后端不转发公开 OSS 设置通知，因此成员与 ready 只从 SDK 实际数据读取。 */
	void StartLobbyFactPolling();

	/** 停止当前 Lobby 轮询；离开、反初始化与会话销毁时成对清理，不让旧 Lobby 驱动新 World。 */
	void StopLobbyFactPolling();

	/** 低频读取当前 Lobby 的成员、元数据和 ready 标记；事实变化才广播，Client 在本 Lobby 的次数预算与退避时间允许时开始自己的包预载。 */
	bool TickLobbyFacts(float DeltaSeconds);

	/** 返回当前已加入 Steam Lobby 是否已由 Host 写入 ready；非 Steam、非成员或数据缺失一律返回 false。 */
	bool IsCurrentLobbyReady() const;

	/** 校验 Host 玩法 World 是否已真正可接纳客户端；只读 listen 驱动、Run 阶段和玩法命令门，不写 Lobby 元数据。 */
	bool IsHostGameplayWorldReadyForClientAdmission() const;

	/** Host 玩法 World 已可接纳客户端后尝试写入 Steam Lobby ready；平台元数据不可写只会阻止 Client 自动连接，不代表 Host 地图启动失败。 */
	bool PublishLobbyReady();

	/** Client 在真实 Lobby ready 且重试预算允许时提交自身玩法包预载并计次；包成功后复核 ready 与 OSS 地址再 ClientTravel，失败统一进入有界退避。 */
	void BeginClientGameplayPreload();

	/** 在当前操作 epoch 下绑定 Destroy 回调并提交平台清理；FailureAfterDestroy 非 None 表示旅行/解析失败后的补偿。 */
	bool BeginDestroySession(ECatOnlineError FailureAfterDestroy);

	/** Lake Host Leave 启动活动世界保存并订阅最终落盘结果；同步拒绝保留 Session，受理后由匹配 RequestId/epoch 的完成回调继续 teardown。 */
	bool BeginHostLeaveSave();

	/** 消费离开请求所属的世界保存结果；最终持久化成功才允许 teardown 和返回后的载荷释放，失败或过期回调不会销毁 Session。 */
	void HandleHostLeaveSaveCompleted(FGuid SaveRequestId, bool bSuccess, uint64 CallbackEpoch);

	/** 解除本次离开在精确 Save 子系统上的完成订阅并废止保存关联键；终态与反初始化均可幂等调用。 */
	void ClearHostLeaveSaveDelegate();

	/** 在获准释放且已确认 NoSession/Frontend 的 Leave 终态释放 Save；busy 时保留原操作等待变化，完成后继续原成功或错误终态。 */
	void FinishLeaveAfterRunRelease();

	/** 成对解除终态释放所等待的精确 Save 变化通知；不会清除 Save 数据，结案和反初始化均可幂等调用。 */
	void ClearRunReleaseDelegate();

	/** Host Leave 在世界保存成功后向当前玩法地图 GameMode 提交 Run teardown；同步 Ready、异步 Pending 与失败都保持同一 RequestId/epoch。 */
	bool BeginHostRunTeardown();

	/** Host 预载完成后的唯一玩法地图 Listen 旅行入口；只提交旅行并等待 PostLoadMap，Lobby ready 还必须通过目标地图的 listen 与 Run 玩法命令门。 */
	bool BeginHostTravelToGameplayMap();

	/** JoinSession 成功且地址解析完成后的唯一玩法地图 ClientTravel 入口；调用方仍等待 PostLoadMap 终态。 */
	bool BeginClientTravelToGameplayMap(const FString& ConnectString);

	/** DestroySession 成功后的统一回前台入口；根据 OperationRole 选择 ServerTravel 或 ClientTravel。 */
	bool BeginTravelToFrontend();

	/** CreateSession 回调：epoch 与操作匹配才消费；成功后恢复 Settings 的本地语音偏好、建立 Host 房间快照并留在 Frontend，失败发布结构化终态。 */
	void HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful, uint64 CallbackEpoch);

	/** FindSessions 回调：epoch 匹配时复制兼容结果并生成 opaque 句柄，随后释放搜索对象。 */
	void HandleFindSessionsComplete(bool bWasSuccessful, uint64 CallbackEpoch);

	/** JoinSession 回调：成功后恢复 Settings 的本地语音偏好、建立 Client Lobby 并留在 Frontend；真实 ready 触发本机预载后才连接。 */
	void HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result, uint64 CallbackEpoch);

	/** DestroySession 回调：区分正常 Leave 与失败补偿；只在当前 epoch 下推进旅行或发布终态。 */
	void HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful, uint64 CallbackEpoch);

	/** Run teardown 回调：只消费当前 Host Leave 的 RequestId/epoch；Ready 才继续 Destroy，Failed 则保留 Session 并结案。 */
	void HandleRunTeardownCompleted(const FCatRunTeardownResult& Result);

	/** 接收平台用户已确认的邀请；无效、忙或已有会话时明确拒绝，否则建立有期限的意图，由 Online 自身检查就绪后复用 RequestAcceptInvite。 */
	void HandleSessionUserInviteAccepted(bool bWasSuccessful, int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& InviteResult);

	/** 按当前 World 维护邀请订阅，并有限等待 Frontend、本地玩家与接受账号就绪；条件满足只提交一次 Join，超时或被其他操作取代则明确结案。 */
	bool TickPlatformInvites(float DeltaSeconds);

	/** 废止待提交邀请及其 opaque 映射、身份与期限；提交、失败和反初始化共用，已进入 Join 的操作仍由原 epoch 管线收口。 */
	void ClearPendingAcceptedInvite();

	/** PostLoadMap 回调：按 GameInstance/ExpectedPackage 隔离后确认 World 与 Transport；Host 玩法 World 自身未就绪才回滚，Lobby ready 写入失败只记录并阻止客户端自动连接。 */
	void HandlePostLoadMap(UWorld* LoadedWorld);

	/** TravelFailure 回调：只消费本 GameInstance 的待确认旅行；Create、Join 与 Start 先 Destroy 补偿，Leave 保留已完成的 Session 清理。 */
	void HandleTravelFailure(UWorld* FailureWorld, ETravelFailure::Type FailureType, const FString& Reason);

	/** NetworkFailure 回调：只消费本 GameInstance 的正式或待连接驱动；Client 预载连接失败有界重试，空闲 Host 断线仍先保存再清会话，返回后才释放载荷。 */
	void HandleNetworkFailure(UWorld* FailureWorld, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& Reason);

	/** 地图包名归类流程；只写 WorldState，不借包名猜测 NamedSession 或 NetDriver 终态。 */
	bool SetWorldStateForPackage(const FString& PackageName);

	/** 完成当前操作并清空错误；获准释放的 Leave 先等 Frontend 载荷清理，随后解绑回调、废止 epoch 并广播稳定快照。 */
	void FinishOperationSuccess();

	/** 以结构化错误结束操作；已安全退出的 Leave 先释放载荷并保留原错，其他失败不释放。前台 Client Start 保留 Lobby 有界重试，耗尽后提示退出重加入。 */
	void FinishOperationFailure(ECatOnlineError Error);

	/** 成对解除当前操作可能绑定的 Create/Find/Join/Destroy 回调，并释放绑定它们的精确 Session 接口。 */
	void ClearOperationDelegates();

	/** 从精确 GameMode 实例解除 Run teardown 委托并清弱引用；重复清理或 World 已销毁时保持幂等。 */
	void ClearRunTeardownDelegate();

	/** 按当前 World 维护唯一 OSS 邀请订阅；接口未变时保留原委托，接口变化才成对重绑，冷启动暂缺接口可由生命周期检查恢复。 */
	void RebindInviteDelegate();

	/** 成对解除当前邀请委托并释放其精确 Session 接口；空句柄和重复调用保持幂等。 */
	void ClearInviteDelegate();

	/** 广播快照变化并写结构化诊断；日志只公开策略允许的身份表示。 */
	void BroadcastSnapshot(const TCHAR* EventName);

	/** 验证 Presence Lobby 与项目/协议/配置地图标识；Create、搜索、邀请和 Join 共用同一门槛，隔离共享 AppId 480 的其他房间。 */
	bool HasCompatibleSessionSettings(const FOnlineSessionSettings& Settings) const;

	/** 当前 GameInstance 的 World 事实；Initialize 按初始包名建立基线，之后仅由旅行提交、PostLoadMap 和 TravelFailure 写入。 */
	ECatOnlineWorldState WorldState = ECatOnlineWorldState::Unknown;

	/** 本地 NamedSession 事实；仅平台 Session 请求与回调写入。 */
	ECatOnlineSessionState SessionState = ECatOnlineSessionState::NoSession;

	/** 当前运输事实；Initialize 按初始 World 建立基线，之后仅由旅行提交、PostLoadMap、TravelFailure 和 NetworkFailure 写入。 */
	ECatOnlineTransportState TransportState = ECatOnlineTransportState::Idle;

	/** 当前唯一复合操作；BeginOperation 写入，Finish/Deinitialize 清空；请求入口据此拒绝并发，平台操作与 Run teardown 回调再和 OperationEpoch 联合校验。 */
	ECatOnlineOperation ActiveOperation = ECatOnlineOperation::None;

	/** 已确认的本地 NamedSession 角色；Create/Join 成功写入，Destroy 成功清空。 */
	ECatOnlineSessionRole SessionRole = ECatOnlineSessionRole::None;

	/** 当前复合操作冻结的会话角色；Create/Join 成功或 Leave 开始时写入，使 SessionRole 在 Destroy 成功清空后仍能选择正确旅行 API，Finish/Deinitialize 清空。 */
	ECatOnlineSessionRole OperationRole = ECatOnlineSessionRole::None;

	/** 最近一次结构化错误；不承担 World、Session 或 Transport 真相。 */
	ECatOnlineError LastError = ECatOnlineError::None;

	/** Destroy 完成后要发布的原始失败；用于 Create/Join 旅行或解析失败的补偿链。 */
	ECatOnlineError DeferredFailureAfterDestroy = ECatOnlineError::None;

	/** 回前台旅行到达后仍需保留的 NetworkFailure；正常 Leave 保持 None。 */
	ECatOnlineError DeferredFailureAfterTravel = ECatOnlineError::None;

	/** 最近一次受理操作或无活动操作时同步拒绝的 RequestId；受理值贯穿平台回调、旅行与补偿日志，pending 拒绝不会覆盖活动关联键。 */
	FGuid ActiveRequestId;

	/** 远端 Host exit 要在本地 DestroySession 成功后回 ACK 的关联键；普通 Leave 保持无效。 */
	FGuid PendingHostExitAckRequestId;

	/** 每次 Begin/Finish/Deinitialize 单调推进的回调代际；迟到回调携带旧值时只记录并返回。 */
	uint64 OperationEpoch = 0;

	/** 当前待确认的目标包名；跨旅行只保存字符串，不持有旧 World 或 Actor。 */
	FString ExpectedPackage;

	/** Initialize 时从 CatOnlineSettings 冻结的玩法地图长包名；同一 GameInstance 的旅行、到达判定与 Session 过滤始终共用它。 */
	FString GameplayMapPackage;

	/** 当前搜索对象；只在 Find epoch 内存活，回调结案或清理时释放。 */
	TSharedPtr<FOnlineSessionSearch> ActiveSearch;

	/** 当前 Steam 好友缓存代际的 opaque 句柄到平台身份映射；刷新和反初始化会整代替换，UI 永不读取原始身份。 */
	TMap<FGuid, TSharedPtr<const FUniqueNetId>> FriendsByHandle;

	/** 与 FriendsByHandle 同生命周期的公开好友摘要；Snapshot 只复制它，不暴露 OSS Friend 对象。 */
	TArray<FCatOnlineFriendSummary> FriendSummaries;

	/** 当前 World Friends 刷新使用的精确接口；委托完成或清理后释放，避免跨 PIE World 读取缓存。 */
	IOnlineFriendsPtr FriendsInterface;

	/** 当前 NamedSession 的真实 Steam Lobby ID；RefreshRoomSnapshotFacts 每次从 SessionInfo 重新建立，空值代表不可证明。 */
	FString CurrentLobbyId;

	/** 当前 Steam Lobby owner 的真实 ID；只用于生成 joinlobby URI，绝不出现在日志或 UI 独立身份字段。 */
	FString CurrentLobbyOwnerId;

	/** 当前 Lobby 真实成员的公开摘要；Steam SDK 可确认本地已加入时重建，无法确认时保持空数组。 */
	TArray<FCatOnlineRoomMember> RoomMembers;

	/** 当前房间的可展示名称；Session/Lobby 事实刷新时写入，空值代表当前没有可验证的 NamedSession。 */
	FString CurrentRoomName;

	/** 当前房间的真实访问策略；Session 设置刷新时写入，无法推导时保持 Undecided。 */
	ECatSessionAccessPolicy CurrentSessionAccess = ECatSessionAccessPolicy::Undecided;

	/** 当前 NamedSession 的真实容量；从 SessionSettings 读取，零代表当前没有可验证的 NamedSession。 */
	int32 RoomMaxPlayers = 0;

	/** 当前 NamedSession 的真实占用连接数；从 SessionSettings/NumOpenPublicConnections 读取，不能读成员时也不扩展为伪成员。 */
	int32 RoomCurrentPlayers = 0;

	/** 当前 Host 或 Client Start 对应的 LoadPackageAsync 请求 ID；非 INDEX_NONE 表示等待引擎回调，取消、失败或旅行终态都会清空。 */
	int32 GameplayPreloadRequestId = INDEX_NONE;

	/** 预载成功后暂存的地图包；在正式 ServerTravel 前保持强引用，防止 GC 在两阶段切换间卸载刚完成的包。 */
	UPROPERTY(Transient)
	TObjectPtr<UPackage> PreloadedGameplayPackage;

	/** 当前好友刷新代际；每次 Friends 请求递增，完成回调用它拒绝旧 World 或旧请求的结果。 */
	uint64 FriendsRefreshEpoch = 0;

	/** 当前是否仍在等待 OSS Friends 的完成委托；重复刷新会明确拒绝，不让两个缓存回调互相覆盖。 */
	bool bFriendsRefreshPending = false;

	/** 当前 Steam Lobby 事实轮询在 CoreTicker 中的弱句柄；Create/Join 写入、离开或反初始化时移除，必须使用 Ticker 自身的句柄以匹配注册与清理 API。 */
	FTSTicker::FDelegateHandle LobbyFactPollHandle;

	/** 上次轮询观察到的 Host ready 元数据；只由 TickLobbyFacts 写入，用于识别 false→true 并触发 Client 预载。 */
	bool bLobbyReadyObserved = false;

	/** 当前 Lobby 中 Client 已提交的玩法启动次数；每次受理 Start 时递增，离开 Lobby 才清零，ready 抖动不会重置失败预算。 */
	int32 ClientGameplayStartAttempts = 0;

	/** Client 下一次允许重试的单调时间，单位秒；失败结案按次数写入退避截止点，轮询只在到期后提交，离开 Lobby 时归零。 */
	double NextClientGameplayStartTime = 0.0;

	/** 当前 Find 代际的 opaque 句柄到平台结果映射；新 Find 会整代替换，成功 Join、补偿、Leave 或销毁会使其失效。 */
	TMap<FGuid, FOnlineSessionSearchResult> SearchResultsByHandle;

	/** 与私有搜索映射同代的公开摘要；其清理时机必须与句柄映射完全一致，GetSnapshot 只复制本数组。 */
	TArray<FCatSessionSearchSummary> SearchSummaries;

	/** 尚未提交 Join 的已接受邀请映射；平台接受事件写入，提交一次后失效，失败不能靠旧句柄无限重试。 */
	TMap<FGuid, FOnlineSessionSearchResult> InvitesByHandle;

	/** 与邀请私有映射同生命周期的公开摘要；不包含原始平台身份或连接字符串。 */
	TArray<FCatSessionInviteSummary> InviteSummaries;

	/** 当前等待前台与身份就绪的平台确认意图；接受回调写入，提交或拒绝时清空，零值代表没有待处理邀请。 */
	FCatSessionInviteHandle PendingAcceptedInvite;

	/** 平台回调确认的接受者账号；只在私有等待期持有，用于阻止冷启动期间账号切换后误加入，绝不进入快照或日志。 */
	FUniqueNetIdPtr PendingInviteUserId;

	/** 邀请等待的单调截止时间，单位秒；接受时冻结为当前时间加期限，轮询不能延长，终态清零。 */
	double PendingInviteDeadline = 0.0;

	/** 接受邀请时的操作代际；等待期间任何其他操作推进 epoch 都使原意图失效，防止旧邀请在用户完成另一条流程后突然加入。 */
	uint64 PendingInviteOperationEpoch = 0;

	/** GameInstance 邀请生命周期检查的 CoreTicker 句柄；Initialize 注册、Deinitialize 移除，空闲时只维护真实 World 的订阅且不刷日志。 */
	FTSTicker::FDelegateHandle PlatformInviteTickHandle;

	/** 当前操作绑定并调用的精确 Session 接口；提交、查询、解析与回调解绑共用同一实例，Finish/Deinitialize 释放，不作为 World 真相。 */
	IOnlineSessionPtr OperationSessionInterface;

	/** 当前邀请回调绑定到的精确 Session 接口；World 变化时先解绑再替换。 */
	IOnlineSessionPtr InviteSessionInterface;

	/** CreateSession 全局委托的配对解绑句柄。 */
	FDelegateHandle CreateSessionHandle;

	/** FindSessions 全局委托的配对解绑句柄。 */
	FDelegateHandle FindSessionsHandle;

	/** JoinSession 全局委托的配对解绑句柄。 */
	FDelegateHandle JoinSessionHandle;

	/** DestroySession 全局委托的配对解绑句柄。 */
	FDelegateHandle DestroySessionHandle;

	/** 离开前保存所用的精确 Save 子系统；提交时写入弱引用，终态解绑时清空，不延长 GameInstance 生命周期。 */
	TWeakObjectPtr<UCatSaveSubsystem> HostLeaveSaveSubsystem;

	/** 当前 Leave 订阅的最终落盘完成句柄；只消费本次保存，成功、失败和反初始化都会成对解绑。 */
	FDelegateHandle HostLeaveSaveHandle;

	/** Save 为本次离开保存生成的关联键；受理返回后冻结，完成时与 Online epoch 一起校验，其他检查点写盘不能推进退出。 */
	FGuid HostLeaveSaveRequestId;

	/** 本次 Leave 在确认回到 Frontend 后可以释放本局载荷的许可；前台离房和 Client 离房可直接获得，Lake Host 只能由匹配的保存成功回执授权，操作结束即失效。 */
	bool bReleaseActiveRunOnFrontend = false;

	/** 终态释放正在等待的 Save 来源；仅 busy 时保存弱引用用于配对解绑，不复制槽标识、载荷或持久化状态。 */
	TWeakObjectPtr<UCatSaveSubsystem> RunReleaseSaveSubsystem;

	/** 等待 Save 结束 busy 的变化订阅；只有当前 Leave epoch 能继续，释放前、失败与反初始化都会移除以阻止同步重入。 */
	FDelegateHandle RunReleaseChangedHandle;

	/** 当前 Host Leave 绑定的 Run teardown 句柄；完成、失败、World 销毁或子系统反初始化都会消费。 */
	FDelegateHandle RunTeardownHandle;

	/** Run teardown 句柄所属的精确玩法地图 GameMode；只用于成对解绑，不作为 World 或 Session 真相。 */
	TWeakObjectPtr<ACatfishingGameModeBase> RunTeardownGameMode;

	/** SessionUserInviteAccepted 全局委托的配对解绑句柄。 */
	FDelegateHandle InviteAcceptedHandle;

	/** PostLoadMapWithWorld 全局委托的配对解绑句柄。 */
	FDelegateHandle PostLoadMapHandle;

	/** TravelFailure 全局委托的配对解绑句柄。 */
	FDelegateHandle TravelFailureHandle;

	/** NetworkFailure 全局委托的配对解绑句柄。 */
	FDelegateHandle NetworkFailureHandle;
};
