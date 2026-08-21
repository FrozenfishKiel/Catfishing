#pragma once

#include "CoreMinimal.h"
#include "Online/CatOnlineTypes.h"
#include "Engine/EngineBaseTypes.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CatOnlineSubsystem.generated.h"

class APlayerController;
class ACatfishingGameModeBase;
class IOnlineSubsystem;
class UNetDriver;
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
	/** 绑定地图、旅行、网络失败和平台邀请回调，并从当前 World 建立初始只读快照。 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** 先让所有 epoch 失效并成对解绑，再清除 opaque 结果和非 UObject 平台引用，保证迟到回调无副作用。 */
	virtual void Deinitialize() override;

	/** 在 Frontend 提交 CreateSession；重复请求优先返回 CommandAlreadyPending 且不覆盖活动关联键，只有平台成功回调后才内部打开配置的玩法地图 listen。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Online")
	FCatOnlineResult RequestCreateSession();

	/** 在 Frontend 提交 FindSessions；重复请求优先返回 CommandAlreadyPending 且不覆盖活动关联键，结果只通过 opaque 句柄和公开摘要进入 Snapshot。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Online")
	FCatOnlineResult RequestFindSessions();

	/** 使用最近一次 Find 生成的 opaque 句柄加入；重复请求先于句柄校验拒绝且不覆盖活动关联键，成功后内部解析地址并 ClientTravel。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Online")
	FCatOnlineResult RequestJoinSession(FCatSessionSearchHandle SearchHandle);

	/** 使用已接受邀请的 opaque 句柄复用 Join 管线；重复请求先于句柄校验拒绝且不覆盖活动关联键。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Online")
	FCatOnlineResult RequestAcceptInvite(FCatSessionInviteHandle InviteHandle);

	/** 根据已确认 SessionRole 选择 Host exit 或 Client leave；重复请求先于角色/策略校验拒绝且不覆盖活动关联键，再以一次 DestroySession 与前台旅行收口。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Online")
	FCatOnlineResult RequestLeave();

	/** 接受服务器 Host exit 通知；重复请求先于关联键/角色校验拒绝且不覆盖活动关联键，并绕过主动离局策略复用 DestroySession/Frontend 管线。 */
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

	/** 开始唯一异步操作，使用可选外部关联键或生成 RequestId 并推进 epoch；Leave 在首个 pending 快照前废止大厅候选。 */
	FCatOnlineResult BeginOperation(ECatOnlineOperation Operation, ECatOnlineSessionState PendingSessionState,
		FGuid CorrelationRequestId = FGuid());

	/** 生成带独立 RequestId 的同步拒绝，更新 LastError 并广播，但不改变 ActiveOperation。 */
	FCatOnlineResult RejectRequest(ECatOnlineError Error);

	/** 让搜索结果或邀请结果汇入同一 JoinSession/Resolve/ClientTravel 管线。 */
	FCatOnlineResult RequestJoinInternal(const FOnlineSessionSearchResult& SearchResult);

	/** 在当前操作 epoch 下绑定 Destroy 回调并提交平台清理；FailureAfterDestroy 非 None 表示旅行/解析失败后的补偿。 */
	bool BeginDestroySession(ECatOnlineError FailureAfterDestroy);

	/** Host Leave 在 DestroySession 前向当前玩法地图 GameMode 提交 Run teardown；同步 Ready、异步 Pending 与失败都保持同一 RequestId/epoch。 */
	bool BeginHostRunTeardown();

	/** CreateSession 成功后的唯一玩法地图 Listen 旅行入口；只写 World/Transport 事实，不改 Session 成功事实。 */
	bool BeginHostTravelToGameplayMap();

	/** JoinSession 成功且地址解析完成后的唯一玩法地图 ClientTravel 入口；调用方仍等待 PostLoadMap 终态。 */
	bool BeginClientTravelToGameplayMap(const FString& ConnectString);

	/** DestroySession 成功后的统一回前台入口；根据 OperationRole 选择 ServerTravel 或 ClientTravel。 */
	bool BeginTravelToFrontend();

	/** CreateSession 回调：epoch 与操作匹配才消费；成功进入 Host 旅行，失败发布结构化终态。 */
	void HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful, uint64 CallbackEpoch);

	/** FindSessions 回调：epoch 匹配时复制兼容结果并生成 opaque 句柄，随后释放搜索对象。 */
	void HandleFindSessionsComplete(bool bWasSuccessful, uint64 CallbackEpoch);

	/** JoinSession 回调：成功后解析 NamedSession 地址并进入唯一 ClientTravel，任何接缝失败都先补偿 Destroy。 */
	void HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result, uint64 CallbackEpoch);

	/** DestroySession 回调：区分正常 Leave 与失败补偿；只在当前 epoch 下推进旅行或发布终态。 */
	void HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful, uint64 CallbackEpoch);

	/** Run teardown 回调：只消费当前 Host Leave 的 RequestId/epoch；Ready 才继续 Destroy，Failed 则保留 Session 并结案。 */
	void HandleRunTeardownCompleted(const FCatRunTeardownResult& Result);

	/** 平台邀请接受回调：只保存 opaque 结果并通知 UI，后续接受动作复用 RequestJoinInternal。 */
	void HandleSessionUserInviteAccepted(bool bWasSuccessful, int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& InviteResult);

	/** PostLoadMap 回调：按 GameInstance/ExpectedPackage 隔离后独立确认 World，并把玩法地图收敛为 Connected、Frontend 收敛为 Idle；最后只结算 ActiveOperation，不改写 Session 事实。 */
	void HandlePostLoadMap(UWorld* LoadedWorld);

	/** TravelFailure 回调：只消费本 GameInstance 的待确认旅行；Create/Join 先 Destroy 补偿，Leave 保留已完成的 Session 清理。 */
	void HandleTravelFailure(UWorld* FailureWorld, ETravelFailure::Type FailureType, const FString& Reason);

	/** NetworkFailure 回调：只消费本 GameInstance 当前 GameNetDriver 或 PendingNetDriver，再幂等启动本地 NamedSession 清理和回前台。 */
	void HandleNetworkFailure(UWorld* FailureWorld, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& Reason);

	/** 地图包名归类流程；只写 WorldState，不借包名猜测 NamedSession 或 NetDriver 终态。 */
	bool SetWorldStateForPackage(const FString& PackageName);

	/** 完成当前操作并清空错误；先解绑平台回调，再使 epoch 失效，最后广播稳定快照。 */
	void FinishOperationSuccess();

	/** 以结构化错误结束当前操作；保留 Session/World/Transport 的真实值，不把错误强行折叠成一份巨型状态。 */
	void FinishOperationFailure(ECatOnlineError Error);

	/** 成对解除当前操作可能绑定的 Create/Find/Join/Destroy 回调，并释放绑定它们的精确 Session 接口。 */
	void ClearOperationDelegates();

	/** 从精确 GameMode 实例解除 Run teardown 委托并清弱引用；重复清理或 World 已销毁时保持幂等。 */
	void ClearRunTeardownDelegate();

	/** 清除旧 World 对应的邀请委托，再按当前 World 的 OSS 接口重新绑定，避免跨 PIE 实例串线。 */
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

	/** 当前 Find 代际的 opaque 句柄到平台结果映射；新 Find 会整代替换，成功 Join、补偿、Leave 或销毁会使其失效。 */
	TMap<FGuid, FOnlineSessionSearchResult> SearchResultsByHandle;

	/** 与私有搜索映射同代的公开摘要；其清理时机必须与句柄映射完全一致，GetSnapshot 只复制本数组。 */
	TArray<FCatSessionSearchSummary> SearchSummaries;

	/** 尚未成功 Join 的已接受邀请映射；异步 Join 失败保留重试，成功、补偿、Leave 或销毁会整体失效。 */
	TMap<FGuid, FOnlineSessionSearchResult> InvitesByHandle;

	/** 与邀请私有映射同生命周期的公开摘要；不包含原始平台身份或连接字符串。 */
	TArray<FCatSessionInviteSummary> InviteSummaries;

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
