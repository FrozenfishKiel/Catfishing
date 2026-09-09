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
struct FStreamableHandle;
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

	/** 接受服务器 Host exit 通知；并发先于关联键/角色校验拒绝且不覆盖活动关联键，Client 绕过主动离局策略复用 Destroy/Frontend 管线，返回后释放本机载荷。 */
	FCatOnlineResult RequestRemoteHostExit(FGuid HostExitRequestId);

	/** 组装当前四类事实、RequestId/epoch、opaque 摘要和真实加载进度；实现只复制 Online 已观察到的资源/包/旅行事实，不推进异步状态。 */
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

	/** 前台包异步加载回调只消费当前回前台 epoch；成功后才提交回主菜单旅行，失败则按旅行拒绝终态收口。 */
	void HandleFrontendPackagePreloadComplete(const FName& PackageName, UPackage* LoadedPackage, EAsyncLoadingResult::Type Result, uint64 CallbackEpoch);

	/** 启动当前有效 Steam Lobby 的低频事实轮询；Steam 后端不转发公开 OSS 设置通知，因此成员与 ready 只从 SDK 实际数据读取。 */
	void StartLobbyFactPolling();

	/** 停止当前 Lobby 轮询；离开、反初始化与会话销毁时成对清理，不让旧 Lobby 驱动新 World。 */
	void StopLobbyFactPolling();

	/** 低频读取当前 Lobby 的成员、元数据和 ready 标记；Host 到达玩法图后按单调秒截止点重试 ready 发布且不回前台，Client 只在同一 Lobby 首次真实 ready 到达且尚未提交 Start 时开始包预载。 */
	bool TickLobbyFacts(float DeltaSeconds);

	/** 返回当前已加入 Steam Lobby 是否已由 Host 写入 ready；非 Steam、非成员或数据缺失一律返回 false。 */
	bool IsCurrentLobbyReady() const;

	/** 校验 Host 玩法 World 是否已真正可接纳客户端；只读 listen 驱动、Run 阶段和玩法命令门，失败只让本次 ready 不发布，不销毁 Session 或回前台。 */
	bool IsHostGameplayWorldReadyForClientAdmission() const;

	/** Host 玩法 World 已可接纳客户端后尝试写入 Steam Lobby ready；平台元数据不可写只阻止 Client 自动进图，Host 的 Lake 与 Session 保持成功态。 */
	bool PublishLobbyReady();

	/** Client 在真实 Lobby ready 且本 Lobby 尚未提交过 Start 时提交自身玩法启动加载管线并计次；完成后复核 ready 与 OSS 地址再 ClientTravel，失败只保留真实错误。 */
	void BeginClientGameplayPreload();

	/** 启动进入游戏的 Lyra 式真实加载管线；先等配置软引用资源集合，再提交地图包预载，任一同步拒绝都会用当前 Start epoch 收口。 */
	bool BeginGameplayStartPreloadPipeline(uint64 CallbackEpoch);

	/** 提交进入玩法前必须预热的软资源集合；返回 true 表示真实 StreamableHandle 已接管等待，false 表示没有可等资源、已同步完成或已同步失败。 */
	bool BeginGameplayStartupAssetsPreload(uint64 CallbackEpoch);

	/** 提交玩法地图包的真实 LoadPackageAsync 预载；Host 和 Client 共用同一完成回调，成功后才进入各自旅行分支。 */
	bool BeginGameplayMapPackagePreload(uint64 CallbackEpoch);

	/** 从现有设置对象收集进入玩法前会被同步使用的软引用；只读取配置声明的资产，不扫描 Content 或按命名猜资源。 */
	void CollectGameplayStartupAssetPaths(TArray<FSoftObjectPath>& OutAssetPaths) const;

	/** 接收 StreamableHandle 的加载更新；只在当前 Start epoch 内写入资源集合进度并广播快照。 */
	void HandleGameplayStartupAssetsPreloadUpdated(TSharedRef<FStreamableHandle> Handle, uint64 CallbackEpoch);

	/** 接收玩法启动资源集合完成事件；成功后保留 handle 防止预热资源被 GC，并继续提交地图包预载。 */
	void HandleGameplayStartupAssetsPreloadComplete(uint64 CallbackEpoch);

	/** 接收玩法启动资源集合取消事件；只有当前 Start epoch 的真实取消会结束 Start，清理路径中的迟到取消会被 epoch 拒绝。 */
	void HandleGameplayStartupAssetsPreloadCancelled(uint64 CallbackEpoch);

	/** 释放玩法启动资源集合的 handle；Start 失败或返回前台后清空进度，Start 成功时保留已加载资源直到离开本局。 */
	void ClearGameplayStartupAssetsPreload(bool bClearProgress);

	/** 当前是否存在任意地图包预载请求；Start 和 Leave 共用该事实给 UI 判断全局遮罩是否有 Online 模型层来源。 */
	bool IsAnyMapPreloadPending() const;

	/** 读取当前预载地图包最近一次真实进度事件对应的百分比；返回 false 表示引擎尚未发出可量化阶段，调用者不得用时间或本地估算补值。 */
	bool TryGetMapPreloadProgressPercent(float& OutProgressPercent) const;

	/** 开始跟踪一个真实 LoadPackageAsync 包名；它只注册引擎进度事件来刷新快照，不承担完成判断，也不会推动旅行。 */
	void BeginMapPreloadProgressTracking(const FString& PackageName, uint64 CallbackEpoch);

	/** 停止当前地图包进度跟踪并清空观测值；预载失败、终态清理和反初始化都必须成对调用。 */
	void StopMapPreloadProgressTracking();

	/** 将 LoadPackageAsync 可能来自异步加载线程的进度事件收口到 GameThread；只有当前 epoch 和包名匹配时才更新 Online 快照。 */
	void HandleMapPreloadProgressOnGameThread(FName PackageName, EAsyncLoadingProgress ProgressType, uint64 CallbackEpoch);

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

	/** DestroySession 或补偿清理后的统一回前台入口；先预载 Frontend 包，再根据 OperationRole 选择 ServerTravel 或 ClientTravel。 */
	bool BeginTravelToFrontend();

	/** 前台包预载完成后的实际旅行提交点；它复用原 Host/Client 分支并只等待 PostLoadMap 收口。 */
	bool CommitFrontendTravelAfterPreload();

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

	/** PreLoadMap 回调：只记录本 GameInstance 正在进入引擎 LoadMap 阻塞段，让全局遮罩按真实切图生命周期保留而不是靠定时器兜底。 */
	void HandlePreLoadMap(const FWorldContext& WorldContext, const FString& MapName);

	/** PostLoadMap 回调：按 GameInstance/ExpectedPackage 隔离后确认 World 与 Transport；Host 到达玩法图后不因 ready 缺失回前台，只安排低频重试，真实 TravelFailure、NetworkFailure 和 Leave 仍走各自回前台管线。 */
	void HandlePostLoadMap(UWorld* LoadedWorld);

	/** TravelFailure 回调：只消费本 GameInstance 的待确认旅行；Create、Join 与 Start 先 Destroy 补偿，Leave 保留已完成的 Session 清理。 */
	void HandleTravelFailure(UWorld* FailureWorld, ETravelFailure::Type FailureType, const FString& Reason);

	/** NetworkFailure 回调：只消费本 GameInstance 的正式或待连接驱动；前台 Client Start 连接失败只结束本次进入并保留真实错误，空闲 Host 断线仍先保存再清会话，返回后才释放载荷。 */
	void HandleNetworkFailure(UWorld* FailureWorld, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& Reason);

	/** 地图包名归类流程；只写 WorldState，不借包名猜测 NamedSession 或 NetDriver 终态。 */
	bool SetWorldStateForPackage(const FString& PackageName);

	/** 完成当前操作并清空错误；获准释放的 Leave 先等 Frontend 载荷清理，Start 成功保留玩法预热资源，其他终态解绑回调、废止 epoch 并广播稳定快照。 */
	void FinishOperationSuccess();

	/** 以结构化错误结束操作；已安全退出的 Leave 先释放载荷并保留原错，其他失败不释放。前台 Client Start 失败保留 Lobby、真实错误和已提交标记，用户显式离开后才会释放下一次进入机会。 */
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

	/** 当前 Host 或 Client Start 对应的 LoadPackageAsync 请求 ID；非 INDEX_NONE 表示地图包预载仍在等待引擎回调。 */
	int32 GameplayPreloadRequestId = INDEX_NONE;

	/** 当前 Start 预热的玩法启动资源 handle；它代表 UI、输入、鱼表、装备、钓鱼和 Run 等配置软引用的真实异步加载集合。 */
	TSharedPtr<FStreamableHandle> GameplayStartupAssetsHandle;

	/** 当前 Start 是否仍在等待玩法启动资源集合完成；StreamableHandle 完成前为 true，地图包预载开始后为 false。 */
	bool bGameplayStartupAssetLoadPending = false;

	/** 当前玩法启动资源集合是否已有可展示进度；只由 StreamableHandle 更新或完成事件写入。 */
	bool bGameplayStartupAssetLoadProgressAvailable = false;

	/** 当前玩法启动资源集合加载百分比，单位 0 到 100；来自 StreamableHandle::GetProgress，不按时间自增。 */
	float CurrentGameplayStartupAssetLoadProgressPercent = 0.0f;

	/** 当前玩法启动资源集合的可读阶段；UI 用它说明资源数量进展，日志用它定位卡住的加载阶段。 */
	FString CurrentGameplayStartupAssetLoadProgressStatus;

	/** 玩法地图预载成功后暂存的包对象；在 Host/Client 旅行提交和 PostLoadMap 收口之间保持强引用，防止 GC 卸载刚完成的包。 */
	UPROPERTY(Transient)
	TObjectPtr<UPackage> PreloadedGameplayPackage;

	/** 当前回主菜单流程对应的前台地图 LoadPackageAsync 请求 ID；非 INDEX_NONE 表示返回主菜单仍在真实包预载阶段。 */
	int32 FrontendPreloadRequestId = INDEX_NONE;

	/** 前台地图预载成功后暂存的包对象；回主菜单旅行提交后继续保留到 PostLoadMap 或终态清理，避免旅行前被 GC 卸载。 */
	UPROPERTY(Transient)
	TObjectPtr<UPackage> PreloadedFrontendPackage;

	/** 当前正在给 UI 暴露进度的地图长包名；Start 写 Gameplay 包，Leave 写 Frontend 包，空值代表 Online 没有可查询的地图包进度。 */
	FString ActiveMapLoadPackage;

	/** 当前地图包是否已经收到 LoadPackageAsync 的真实进度事件；UI 只在该值为真时展示可量化地图包进度。 */
	bool bMapLoadProgressAvailable = false;

	/** 当前地图包最近一次引擎进度事件对应的百分比，单位 0 到 100；该值只随真实进度事件变化，不按时间自增。 */
	float CurrentMapLoadProgressPercent = 0.0f;

	/** 当前地图包最近一次引擎进度事件的可读阶段；UI 和日志读取它来说明玩家正在等哪一步。 */
	FString CurrentMapLoadProgressStatus;

	/** 当前地图包提交给 LoadPackageAsync 的进度委托；持有它只为接收引擎事件，完成、失败或终态清理时释放。 */
	TSharedPtr<FLoadPackageAsyncProgressDelegate> MapLoadProgressDelegate;

	/** 当前 GameInstance 是否处于引擎 LoadMap 阻塞段；PreLoadMap 写入、PostLoadMap 清空，UI 只把它当真实等待原因。 */
	bool bIsEngineLoadMapPending = false;

	/** 当前引擎 LoadMap 目标名；它来自 PreLoadMap 回调，只用于状态展示和日志，不参与地图到达判定。 */
	FString EngineLoadMapName;

	/** 当前好友刷新代际；每次 Friends 请求递增，完成回调用它拒绝旧 World 或旧请求的结果。 */
	uint64 FriendsRefreshEpoch = 0;

	/** 当前是否仍在等待 OSS Friends 的完成委托；重复刷新会明确拒绝，不让两个缓存回调互相覆盖。 */
	bool bFriendsRefreshPending = false;

	/** 当前 Steam Lobby 事实轮询在 CoreTicker 中的弱句柄；Create/Join 写入、离开或反初始化时移除，必须使用 Ticker 自身的句柄以匹配注册与清理 API。 */
	FTSTicker::FDelegateHandle LobbyFactPollHandle;

	/** 上次轮询观察到的 Host ready 元数据；只由 TickLobbyFacts 写入，用于识别 false→true 并触发 Client 预载。 */
	bool bLobbyReadyObserved = false;

	/** Host 下一次允许重试发布 ready 元数据的单调时间，单位秒；PostLoadMap 和 TickLobbyFacts 在失败时写入、成功或离开 Lobby 时归零，只有 Host 侧轮询读取它。 */
	double NextHostLobbyReadyPublishAttemptTime = 0.0;

	/** 当前 Lobby 中 Client 是否已经提交过玩法启动；每次受理 Start 时递增，离开 Lobby 才清零，避免同一 ready 事实反复自动进图。 */
	int32 ClientGameplayStartAttempts = 0;

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

	/** PreLoadMapWithContext 全局委托的配对解绑句柄。 */
	FDelegateHandle PreLoadMapHandle;

	/** PostLoadMapWithWorld 全局委托的配对解绑句柄。 */
	FDelegateHandle PostLoadMapHandle;

	/** TravelFailure 全局委托的配对解绑句柄。 */
	FDelegateHandle TravelFailureHandle;

	/** NetworkFailure 全局委托的配对解绑句柄。 */
	FDelegateHandle NetworkFailureHandle;
};
