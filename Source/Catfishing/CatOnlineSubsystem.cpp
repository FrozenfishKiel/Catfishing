#include "CatOnlineSubsystem.h"

#include "CatLog.h"
#include "CatOnlineSettings.h"
#include "CatGameplayTypes.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "UObject/UObjectGlobals.h"

namespace CatOnlineNames
{
	/** 项目唯一 NamedSession 名称；Create/Join/Resolve/Destroy 全部使用同一键，避免本地形成第二份会话事实。 */
	static const FName GameSession(TEXT("GameSession"));
	/** Frontend 的稳定长包名；WorldState 与旅行目标共用，PIE 前缀在比较前统一剥离。 */
	static const FString Frontend(TEXT("/Game/Catfishing/Maps/Frontend"));
	/** Lake 的稳定长包名；Create 成功后追加 listen，Join 则使用平台解析出的地址。 */
	static const FString Lake(TEXT("/Game/Catfishing/Maps/Lake"));
	/** Session 搜索摘要中的地图键；它只用于平台发现信息，不参与 World 到达判定。 */
	static const FName MapSetting(TEXT("CAT_MAP"));
}

// 初始化流程：先绑定三类引擎生命周期，再按当前 World 尝试绑定对应 OSS 邀请接口，最后建立 World 快照；邀请接口缺失时保持未绑定，不缓存旧 World 或伪造 Session 错误。
void UCatOnlineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ThisClass::HandlePostLoadMap);
	if (GEngine)
	{
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &ThisClass::HandleTravelFailure);
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &ThisClass::HandleNetworkFailure);
	}
	RebindInviteDelegate();
	if (const UWorld* World = GetWorld())
	{
		const FString PackageName = UWorld::StripPIEPrefixFromPackageName(World->GetPackage()->GetName(), World->StreamingLevelsPrefix);
		SetWorldStateForPackage(PackageName);
		TransportState = WorldState == ECatOnlineWorldState::Lake ? ECatOnlineTransportState::Connected : ECatOnlineTransportState::Idle;
	}
	BroadcastSnapshot(TEXT("online_initialized"));
}

// 销毁流程：先推进 epoch 使已投递回调全部失效，再解除平台与引擎委托；随后释放搜索/邀请对象和字符串事实，最后清广播并交还父类。
void UCatOnlineSubsystem::Deinitialize()
{
	++OperationEpoch;
	ClearRunTeardownDelegate();
	ClearOperationDelegates();
	ClearInviteDelegate();
	if (GEngine && TravelFailureHandle.IsValid())
	{
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	TravelFailureHandle.Reset();
	if (GEngine && NetworkFailureHandle.IsValid())
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
	}
	NetworkFailureHandle.Reset();
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	PostLoadMapHandle.Reset();

	ActiveSearch.Reset();
	SearchResultsByHandle.Reset();
	SearchSummaries.Reset();
	InvitesByHandle.Reset();
	InviteSummaries.Reset();
	ExpectedPackage.Reset();
	PendingHostExitAckRequestId.Invalidate();
	ActiveOperation = ECatOnlineOperation::None;
	OperationRole = ECatOnlineSessionRole::None;
	DeferredFailureAfterDestroy = ECatOnlineError::None;
	DeferredFailureAfterTravel = ECatOnlineError::None;
	OnSnapshotChanged.Clear();
	Super::Deinitialize();
}

// Session 接口定位流程：只用当前 GameInstance 的 World 调用 Online::GetSubsystem；OSS 或 Session 接口缺失时返回空，不退回无 World 的进程级实例。
IOnlineSessionPtr UCatOnlineSubsystem::GetWorldSessionInterface() const
{
	const UWorld* World = GetWorld();
	IOnlineSubsystem* OnlineSubsystem = World ? Online::GetSubsystem(World) : nullptr;
	return OnlineSubsystem ? OnlineSubsystem->GetSessionInterface() : nullptr;
}

// 接口错误归类流程：先检查当前 World，再用同一 World 查询 OSS，最后检查 Session 接口；两次查询间接口若恢复则归为可重试 RequestRejected，不把竞态误报为成功。
ECatOnlineError UCatOnlineSubsystem::GetSessionInterfaceError() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return ECatOnlineError::WorldUnavailable;
	}
	IOnlineSubsystem* OnlineSubsystem = Online::GetSubsystem(World);
	if (!OnlineSubsystem)
	{
		return ECatOnlineError::OnlineSubsystemUnavailable;
	}
	return OnlineSubsystem->GetSessionInterface().IsValid()
		? ECatOnlineError::RequestRejected
		: ECatOnlineError::SessionInterfaceUnavailable;
}

// 操作开始流程：优先使用有效外部关联键，否则为本次 UI 意图生成 RequestId；若已有操作则保留旧操作的 RequestId/epoch，只返回独立同步拒绝。
// 受理后推进 epoch；Leave 在首个 pending 快照前统一废止搜索与邀请候选，确保主动离局、Host 通知和网络失败遵守同一代际边界。
FCatOnlineResult UCatOnlineSubsystem::BeginOperation(const ECatOnlineOperation Operation,
	const ECatOnlineSessionState PendingSessionState, const FGuid CorrelationRequestId)
{
	if (ActiveOperation != ECatOnlineOperation::None)
	{
		return RejectRequest(ECatOnlineError::CommandAlreadyPending);
	}

	FCatOnlineResult Result;
	Result.bAccepted = true;
	Result.RequestId = CorrelationRequestId.IsValid() ? CorrelationRequestId : FGuid::NewGuid();
	ActiveRequestId = Result.RequestId;
	++OperationEpoch;
	ActiveOperation = Operation;
	SessionState = PendingSessionState;
	LastError = ECatOnlineError::None;
	OperationRole = ECatOnlineSessionRole::None;
	DeferredFailureAfterDestroy = ECatOnlineError::None;
	DeferredFailureAfterTravel = ECatOnlineError::None;
	if (Operation == ECatOnlineOperation::Leave)
	{
		SearchResultsByHandle.Reset();
		SearchSummaries.Reset();
		InvitesByHandle.Reset();
		InviteSummaries.Reset();
	}
	BroadcastSnapshot(TEXT("online_operation_started"));
	return Result;
}

// 远端 Host exit 流程：先以 CommandAlreadyPending 拒绝任何活动操作且不覆盖原 RequestId/epoch，再验证 Lake Client 与服务器关联键；受理后不提交主动离局标记，复用 Leave 状态机并保存 Destroy 成功后的 ACK 键。
FCatOnlineResult UCatOnlineSubsystem::RequestRemoteHostExit(const FGuid HostExitRequestId)
{
	if (ActiveOperation != ECatOnlineOperation::None)
	{
		return RejectRequest(ECatOnlineError::CommandAlreadyPending);
	}
	if (!HostExitRequestId.IsValid() || WorldState != ECatOnlineWorldState::Lake
		|| SessionRole != ECatOnlineSessionRole::Client)
	{
		return RejectRequest(ECatOnlineError::InvalidState);
	}
	FCatOnlineResult Result = BeginOperation(
		ECatOnlineOperation::Leave, ECatOnlineSessionState::Destroying, HostExitRequestId);
	if (!Result.bAccepted)
	{
		return Result;
	}
	OperationRole = ECatOnlineSessionRole::Client;
	PendingHostExitAckRequestId = HostExitRequestId;
	if (!BeginDestroySession(ECatOnlineError::None))
	{
		PendingHostExitAckRequestId.Invalidate();
		Result.bAccepted = false;
		Result.Error = LastError;
	}
	return Result;
}

// 同步拒绝流程：生成本次拒绝自己的 RequestId；没有活跃操作时更新最近 RequestId，有活跃操作时不覆盖其关联键，最后广播错误快照。
FCatOnlineResult UCatOnlineSubsystem::RejectRequest(const ECatOnlineError Error)
{
	FCatOnlineResult Result;
	Result.RequestId = FGuid::NewGuid();
	Result.Error = Error;
	if (ActiveOperation == ECatOnlineOperation::None)
	{
		ActiveRequestId = Result.RequestId;
	}
	LastError = Error;
	BroadcastSnapshot(TEXT("online_request_rejected"));
	return Result;
}

// Create 流程：先以 CommandAlreadyPending 拒绝活动操作且不覆盖其关联键，再依次检查前台、SessionAccess 策略、World-aware Session 接口和兼容合同；绑定携带 epoch 的回调后提交平台请求。
// UE 5.8 的 OSS 允许在 API 返回前同步触发完成委托，因此返回后必须同时复核 operation、epoch 与句柄；只有回调尚未消费本次提交时，false 才发布一次 RequestRejected。
FCatOnlineResult UCatOnlineSubsystem::RequestCreateSession()
{
	if (ActiveOperation != ECatOnlineOperation::None)
	{
		return RejectRequest(ECatOnlineError::CommandAlreadyPending);
	}
	if (WorldState != ECatOnlineWorldState::Frontend || SessionRole != ECatOnlineSessionRole::None)
	{
		return RejectRequest(ECatOnlineError::InvalidState);
	}
	const UCatOnlineSettings* Settings = GetDefault<UCatOnlineSettings>();
	if (Settings->SessionAccess == ECatSessionAccessPolicy::Undecided)
	{
		return RejectRequest(ECatOnlineError::PolicyUndecided);
	}

	FCatOnlineResult Result = BeginOperation(ECatOnlineOperation::Create, ECatOnlineSessionState::Creating);
	if (!Result.bAccepted)
	{
		return Result;
	}
	OperationSessionInterface = GetWorldSessionInterface();
	if (!OperationSessionInterface.IsValid())
	{
		SessionState = ECatOnlineSessionState::NoSession;
		FinishOperationFailure(GetSessionInterfaceError());
		Result.bAccepted = false;
		Result.Error = LastError;
		return Result;
	}
	if (OperationSessionInterface->GetNamedSession(CatOnlineNames::GameSession))
	{
		SessionState = ECatOnlineSessionState::Error;
		FinishOperationFailure(ECatOnlineError::InvalidState);
		Result.bAccepted = false;
		Result.Error = LastError;
		return Result;
	}

	FOnlineSessionSettings SessionSettings;
	SessionSettings.NumPublicConnections = 8;
	SessionSettings.NumPrivateConnections = 0;
	SessionSettings.bAllowJoinInProgress = true;
	SessionSettings.bAllowInvites = true;
	SessionSettings.bUsesPresence = true;
	SessionSettings.bUseLobbiesIfAvailable = true;
	SessionSettings.bShouldAdvertise = Settings->SessionAccess != ECatSessionAccessPolicy::InviteOnly;
	SessionSettings.bAllowJoinViaPresence = Settings->SessionAccess == ECatSessionAccessPolicy::Public;
	SessionSettings.bAllowJoinViaPresenceFriendsOnly = Settings->SessionAccess == ECatSessionAccessPolicy::FriendsOnly;
	SessionSettings.Set(CatOnlineNames::MapSetting, CatOnlineNames::Lake, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	if (!HasCompatibleSessionSettings(SessionSettings))
	{
		SessionState = ECatOnlineSessionState::NoSession;
		FinishOperationFailure(ECatOnlineError::SessionCompatibilityMismatch);
		Result.bAccepted = false;
		Result.Error = LastError;
		return Result;
	}

	const uint64 SubmittedEpoch = OperationEpoch;
	CreateSessionHandle = OperationSessionInterface->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleCreateSessionComplete, SubmittedEpoch));
	const bool bRequestQueued = OperationSessionInterface->CreateSession(0, CatOnlineNames::GameSession, SessionSettings);
	const bool bStillAwaitingCompletion = ActiveOperation == ECatOnlineOperation::Create
		&& OperationEpoch == SubmittedEpoch
		&& CreateSessionHandle.IsValid();
	if (!bRequestQueued && bStillAwaitingCompletion)
	{
		SessionState = ECatOnlineSessionState::NoSession;
		FinishOperationFailure(ECatOnlineError::RequestRejected);
		Result.bAccepted = false;
		Result.Error = LastError;
	}
	return Result;
}

// Find 流程：先以 CommandAlreadyPending 拒绝活动操作且不覆盖其关联键，再检查前台与公开搜索策略；受理新 Find 前整代清除旧搜索句柄，使 pending 快照也不暴露上一代结果。
// 提交后按 operation、epoch 与句柄识别同步完成；只有 OSS 返回 false 且回调仍未消费本代时才结为 RequestRejected。
FCatOnlineResult UCatOnlineSubsystem::RequestFindSessions()
{
	if (ActiveOperation != ECatOnlineOperation::None)
	{
		return RejectRequest(ECatOnlineError::CommandAlreadyPending);
	}
	if (WorldState != ECatOnlineWorldState::Frontend || SessionRole != ECatOnlineSessionRole::None)
	{
		return RejectRequest(ECatOnlineError::InvalidState);
	}
	const UCatOnlineSettings* Settings = GetDefault<UCatOnlineSettings>();
	if (Settings->SessionAccess == ECatSessionAccessPolicy::Undecided)
	{
		return RejectRequest(ECatOnlineError::PolicyUndecided);
	}
	if (Settings->SessionAccess == ECatSessionAccessPolicy::InviteOnly)
	{
		return RejectRequest(ECatOnlineError::InvalidState);
	}

	SearchResultsByHandle.Reset();
	SearchSummaries.Reset();
	FCatOnlineResult Result = BeginOperation(ECatOnlineOperation::Find, ECatOnlineSessionState::Searching);
	if (!Result.bAccepted)
	{
		return Result;
	}
	OperationSessionInterface = GetWorldSessionInterface();
	if (!OperationSessionInterface.IsValid())
	{
		SessionState = ECatOnlineSessionState::NoSession;
		FinishOperationFailure(GetSessionInterfaceError());
		Result.bAccepted = false;
		Result.Error = LastError;
		return Result;
	}

	ActiveSearch = MakeShared<FOnlineSessionSearch>();
	ActiveSearch->MaxSearchResults = 50;
	ActiveSearch->bIsLanQuery = false;
	const uint64 SubmittedEpoch = OperationEpoch;
	FindSessionsHandle = OperationSessionInterface->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &ThisClass::HandleFindSessionsComplete, SubmittedEpoch));
	const bool bRequestQueued = OperationSessionInterface->FindSessions(0, ActiveSearch.ToSharedRef());
	const bool bStillAwaitingCompletion = ActiveOperation == ECatOnlineOperation::Find
		&& OperationEpoch == SubmittedEpoch
		&& FindSessionsHandle.IsValid();
	if (!bRequestQueued && bStillAwaitingCompletion)
	{
		SessionState = ECatOnlineSessionState::NoSession;
		ActiveSearch.Reset();
		FinishOperationFailure(ECatOnlineError::RequestRejected);
		Result.bAccepted = false;
		Result.Error = LastError;
	}
	return Result;
}

// 搜索 Join 流程：先以 CommandAlreadyPending 拒绝活动操作且不覆盖其关联键，再从私有映射解析 opaque 句柄并复制平台结果；句柄失效不会触碰平台接口。
FCatOnlineResult UCatOnlineSubsystem::RequestJoinSession(const FCatSessionSearchHandle SearchHandle)
{
	if (ActiveOperation != ECatOnlineOperation::None)
	{
		return RejectRequest(ECatOnlineError::CommandAlreadyPending);
	}
	const FOnlineSessionSearchResult* SearchResult = SearchResultsByHandle.Find(SearchHandle.Value);
	if (!SearchHandle.IsValid() || !SearchResult)
	{
		return RejectRequest(ECatOnlineError::InvalidHandle);
	}
	const FOnlineSessionSearchResult SearchResultCopy = *SearchResult;
	return RequestJoinInternal(SearchResultCopy);
}

// 邀请 Join 流程：先以 CommandAlreadyPending 拒绝活动操作且不覆盖其关联键，再从私有映射解析 opaque 邀请；异步 Join 成败未知时保持本代句柄，只有成功回调或统一清理才使其失效。
FCatOnlineResult UCatOnlineSubsystem::RequestAcceptInvite(const FCatSessionInviteHandle InviteHandle)
{
	if (ActiveOperation != ECatOnlineOperation::None)
	{
		return RejectRequest(ECatOnlineError::CommandAlreadyPending);
	}
	const FOnlineSessionSearchResult* InviteResult = InvitesByHandle.Find(InviteHandle.Value);
	if (!InviteHandle.IsValid() || !InviteResult)
	{
		return RejectRequest(ECatOnlineError::InvalidHandle);
	}
	const FOnlineSessionSearchResult InviteResultCopy = *InviteResult;
	return RequestJoinInternal(InviteResultCopy);
}

// 统一 Join 流程：前台和 SessionAccess gate 通过后检查目标兼容合同；再绑定当前 epoch 回调并提交 JoinSession，邀请与搜索从这里起没有分叉。
// OSS 可能在返回前同步完成，故返回后只有 operation、epoch 与句柄仍指向本次提交时，false 才能结束操作；回调产生的 JoinFailed 或旅行状态不得被覆盖。
FCatOnlineResult UCatOnlineSubsystem::RequestJoinInternal(const FOnlineSessionSearchResult& SearchResult)
{
	if (WorldState != ECatOnlineWorldState::Frontend || SessionRole != ECatOnlineSessionRole::None)
	{
		return RejectRequest(ECatOnlineError::InvalidState);
	}
	if (GetDefault<UCatOnlineSettings>()->SessionAccess == ECatSessionAccessPolicy::Undecided)
	{
		return RejectRequest(ECatOnlineError::PolicyUndecided);
	}
	if (!SearchResult.IsValid())
	{
		return RejectRequest(ECatOnlineError::InvalidHandle);
	}
	if (!HasCompatibleSessionSettings(SearchResult.Session.SessionSettings))
	{
		return RejectRequest(ECatOnlineError::SessionCompatibilityMismatch);
	}

	FCatOnlineResult Result = BeginOperation(ECatOnlineOperation::Join, ECatOnlineSessionState::Joining);
	if (!Result.bAccepted)
	{
		return Result;
	}
	OperationSessionInterface = GetWorldSessionInterface();
	if (!OperationSessionInterface.IsValid())
	{
		SessionState = ECatOnlineSessionState::NoSession;
		FinishOperationFailure(GetSessionInterfaceError());
		Result.bAccepted = false;
		Result.Error = LastError;
		return Result;
	}
	const uint64 SubmittedEpoch = OperationEpoch;
	JoinSessionHandle = OperationSessionInterface->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleJoinSessionComplete, SubmittedEpoch));
	const bool bRequestQueued = OperationSessionInterface->JoinSession(0, CatOnlineNames::GameSession, SearchResult);
	const bool bStillAwaitingCompletion = ActiveOperation == ECatOnlineOperation::Join
		&& OperationEpoch == SubmittedEpoch
		&& JoinSessionHandle.IsValid();
	if (!bRequestQueued && bStillAwaitingCompletion)
	{
		SessionState = ECatOnlineSessionState::NoSession;
		FinishOperationFailure(ECatOnlineError::RequestRejected);
		Result.bAccepted = false;
		Result.Error = LastError;
	}
	return Result;
}

// Leave 流程：先以 CommandAlreadyPending 拒绝活动操作且不覆盖其关联键，再读取已确认角色而不从 NetMode 猜 Host/Client；Client 主动离局策略未裁时明确 gate，Host exit 不借该策略改变收口。
FCatOnlineResult UCatOnlineSubsystem::RequestLeave()
{
	if (ActiveOperation != ECatOnlineOperation::None)
	{
		return RejectRequest(ECatOnlineError::CommandAlreadyPending);
	}
	if (WorldState != ECatOnlineWorldState::Lake || SessionRole == ECatOnlineSessionRole::None)
	{
		return RejectRequest(ECatOnlineError::InvalidState);
	}
	if (SessionRole == ECatOnlineSessionRole::Client
		&& GetDefault<UCatOnlineSettings>()->VoluntaryLeaveRecovery == ECatPolicyDecision::Undecided)
	{
		return RejectRequest(ECatOnlineError::PolicyUndecided);
	}
	if (SessionRole == ECatOnlineSessionRole::Client)
	{
		if (ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(GetWorld()->GetFirstPlayerController()))
		{
			Controller->ServerMarkVoluntaryLeave();
		}
	}

	FCatOnlineResult Result = BeginOperation(ECatOnlineOperation::Leave, ECatOnlineSessionState::Destroying);
	if (!Result.bAccepted)
	{
		return Result;
	}
	OperationRole = SessionRole;
	const bool bLeaveSubmitted = OperationRole == ECatOnlineSessionRole::Host
		? BeginHostRunTeardown()
		: BeginDestroySession(ECatOnlineError::None);
	if (!bLeaveSubmitted)
	{
		Result.bAccepted = false;
		Result.Error = LastError;
	}
	return Result;
}

// Host Run 收口流程：先取得当前 authority GameMode 并在调用前绑定完成委托，再提交携带 Online RequestId/epoch 的 teardown；同步广播可能重入本子系统，因此返回后只在操作仍属于本代时解释直接结果。
bool UCatOnlineSubsystem::BeginHostRunTeardown()
{
	UWorld* World = GetWorld();
	ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	if (!GameMode || ActiveOperation != ECatOnlineOperation::Leave || OperationRole != ECatOnlineSessionRole::Host)
	{
		FinishOperationFailure(ECatOnlineError::RunTeardownFailed);
		return false;
	}

	ClearRunTeardownDelegate();
	RunTeardownGameMode = GameMode;
	RunTeardownHandle = GameMode->OnRunTeardownCompleted().AddUObject(this, &ThisClass::HandleRunTeardownCompleted);
	const FGuid SubmittedRequestId = ActiveRequestId;
	const uint64 SubmittedEpoch = OperationEpoch;
	FCatRunTeardownRequest Request;
	Request.RequestId = SubmittedRequestId;
	Request.OperationEpoch = static_cast<int64>(SubmittedEpoch);
	const FCatRunTeardownResult Result = GameMode->RequestRunTeardown(Request);

	const bool bStillCurrent = ActiveOperation == ECatOnlineOperation::Leave
		&& OperationRole == ECatOnlineSessionRole::Host
		&& ActiveRequestId == SubmittedRequestId
		&& OperationEpoch == SubmittedEpoch
		&& RunTeardownHandle.IsValid();
	if (!bStillCurrent)
	{
		return true;
	}
	if (Result.RequestId != SubmittedRequestId || Result.OperationEpoch != static_cast<int64>(SubmittedEpoch))
	{
		ClearRunTeardownDelegate();
		FinishOperationFailure(ECatOnlineError::RunTeardownFailed);
		return false;
	}
	if (Result.Status == ECatRunTeardownStatus::Pending)
	{
		BroadcastSnapshot(TEXT("online_run_teardown_pending"));
		return true;
	}

	ClearRunTeardownDelegate();
	if (Result.Status != ECatRunTeardownStatus::Ready)
	{
		FinishOperationFailure(ECatOnlineError::RunTeardownFailed);
		return false;
	}
	return BeginDestroySession(ECatOnlineError::None);
}

// Snapshot 读取流程：逐字段复制四类事实和公开摘要；平台搜索对象、连接字符串、原始邀请者 ID 与委托句柄均不会离开子系统。
FCatOnlineSnapshot UCatOnlineSubsystem::GetSnapshot() const
{
	FCatOnlineSnapshot Snapshot;
	Snapshot.WorldState = WorldState;
	Snapshot.SessionState = SessionState;
	Snapshot.TransportState = TransportState;
	Snapshot.ActiveOperation = ActiveOperation;
	Snapshot.SessionRole = SessionRole;
	Snapshot.RequestId = ActiveRequestId;
	Snapshot.OperationEpoch = static_cast<int64>(OperationEpoch);
	Snapshot.LastError = LastError;
	Snapshot.SearchResults = SearchSummaries;
	Snapshot.AcceptedInvites = InviteSummaries;
	return Snapshot;
}

// Destroy 提交流程：补偿开始先废止所有大厅候选；再在当前 World 精确取得 Session 接口，NamedSession 已不存在时把清理视为幂等完成。
// 平台调用返回后复核提交时的 operation、epoch 与 Destroy 句柄，避免同步完成已经旅行或结案后再次广播 queued/失败。
bool UCatOnlineSubsystem::BeginDestroySession(const ECatOnlineError FailureAfterDestroy)
{
	DeferredFailureAfterDestroy = FailureAfterDestroy;
	if (FailureAfterDestroy != ECatOnlineError::None)
	{
		SearchResultsByHandle.Reset();
		SearchSummaries.Reset();
		InvitesByHandle.Reset();
		InviteSummaries.Reset();
	}
	if (!OperationSessionInterface.IsValid())
	{
		OperationSessionInterface = GetWorldSessionInterface();
	}
	if (!OperationSessionInterface.IsValid())
	{
		SessionState = ECatOnlineSessionState::Error;
		FinishOperationFailure(GetSessionInterfaceError());
		return false;
	}
	if (!OperationSessionInterface->GetNamedSession(CatOnlineNames::GameSession))
	{
		SessionState = ECatOnlineSessionState::NoSession;
		SessionRole = ECatOnlineSessionRole::None;
		if (FailureAfterDestroy != ECatOnlineError::None)
		{
			FinishOperationFailure(FailureAfterDestroy);
			return true;
		}
		if (!BeginTravelToFrontend())
		{
			FinishOperationFailure(ECatOnlineError::TravelRejected);
			return false;
		}
		return true;
	}

	SessionState = ECatOnlineSessionState::Destroying;
	const ECatOnlineOperation SubmittedOperation = ActiveOperation;
	const uint64 SubmittedEpoch = OperationEpoch;
	DestroySessionHandle = OperationSessionInterface->AddOnDestroySessionCompleteDelegate_Handle(
		FOnDestroySessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleDestroySessionComplete, SubmittedEpoch));
	const bool bRequestQueued = OperationSessionInterface->DestroySession(CatOnlineNames::GameSession);
	const bool bStillAwaitingCompletion = ActiveOperation == SubmittedOperation
		&& OperationEpoch == SubmittedEpoch
		&& DestroySessionHandle.IsValid();
	if (!bRequestQueued && bStillAwaitingCompletion)
	{
		SessionState = ECatOnlineSessionState::Error;
		FinishOperationFailure(ECatOnlineError::DestroyFailed);
		return false;
	}
	if (bStillAwaitingCompletion)
	{
		BroadcastSnapshot(TEXT("online_destroy_queued"));
	}
	return bRequestQueued || !bStillAwaitingCompletion;
}

// Host 旅行流程：Create 已成功才校验 authority 并提交 Lake?listen；同步受理后只写 World/Transport 目标，最终成功仍由 PostLoadMap 确认。
bool UCatOnlineSubsystem::BeginHostTravelToLake()
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return false;
	}
	if (!World->ServerTravel(CatOnlineNames::Lake + TEXT("?listen"), false))
	{
		return false;
	}
	ExpectedPackage = CatOnlineNames::Lake;
	WorldState = ECatOnlineWorldState::TravelingToLake;
	TransportState = ECatOnlineTransportState::TravelQueued;
	BroadcastSnapshot(TEXT("online_host_travel_queued"));
	return true;
}

// Client 旅行流程：Join 与 Resolve 已成功后临时取得本地控制器并调用 ClientTravel；不保存 Controller 引用，PostLoadMap 才发布到达终态。
bool UCatOnlineSubsystem::BeginClientTravelToLake(const FString& ConnectString)
{
	APlayerController* PlayerController = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController() : nullptr;
	if (!PlayerController || ConnectString.IsEmpty())
	{
		return false;
	}
	PlayerController->ClientTravel(ConnectString, TRAVEL_Absolute);
	ExpectedPackage = CatOnlineNames::Lake;
	WorldState = ECatOnlineWorldState::TravelingToLake;
	TransportState = ECatOnlineTransportState::TravelQueued;
	BroadcastSnapshot(TEXT("online_client_travel_queued"));
	return true;
}

// 回前台流程：若已经在 Frontend 则幂等结案；Host 用绝对 ServerTravel 切断 listen，Client 用本地 ClientTravel，均不把 DestroySession 误当作驱动关闭。
bool UCatOnlineSubsystem::BeginTravelToFrontend()
{
	if (WorldState == ECatOnlineWorldState::Frontend)
	{
		TransportState = ECatOnlineTransportState::Idle;
		if (DeferredFailureAfterTravel != ECatOnlineError::None)
		{
			const ECatOnlineError Failure = DeferredFailureAfterTravel;
			FinishOperationFailure(Failure);
		}
		else
		{
			FinishOperationSuccess();
		}
		return true;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	if (OperationRole == ECatOnlineSessionRole::Host)
	{
		if (World->GetNetMode() == NM_Client || !World->ServerTravel(CatOnlineNames::Frontend, true))
		{
			return false;
		}
	}
	else if (OperationRole == ECatOnlineSessionRole::Client)
	{
		APlayerController* PlayerController = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController() : nullptr;
		if (!PlayerController)
		{
			return false;
		}
		PlayerController->ClientTravel(CatOnlineNames::Frontend, TRAVEL_Absolute);
	}
	else
	{
		return false;
	}

	ExpectedPackage = CatOnlineNames::Frontend;
	WorldState = ECatOnlineWorldState::TravelingToFrontend;
	TransportState = ECatOnlineTransportState::TravelQueued;
	BroadcastSnapshot(TEXT("online_frontend_travel_queued"));
	return true;
}

// Create 回调流程：拒绝名称、操作或 epoch 不匹配的迟到事件；成功先确立 Host Session，再尝试唯一 Listen 旅行，旅行同步失败则立即 Destroy 补偿。
void UCatOnlineSubsystem::HandleCreateSessionComplete(const FName SessionName, const bool bWasSuccessful, const uint64 CallbackEpoch)
{
	if (SessionName != CatOnlineNames::GameSession || ActiveOperation != ECatOnlineOperation::Create
		|| CallbackEpoch != OperationEpoch || !CreateSessionHandle.IsValid())
	{
		UE_LOG(LogCatOnline, Warning, TEXT("Event=online_callback_ignored Callback=Create Epoch=%llu CurrentEpoch=%llu"), CallbackEpoch, OperationEpoch);
		return;
	}
	if (OperationSessionInterface.IsValid())
	{
		OperationSessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionHandle);
	}
	// 句柄在回调入口立即失效，既阻止同 epoch 重复完成，也让尚未返回的 CreateSession 外层识别本次提交已被消费。
	CreateSessionHandle.Reset();
	if (!bWasSuccessful)
	{
		SessionState = ECatOnlineSessionState::NoSession;
		SessionRole = ECatOnlineSessionRole::None;
		FinishOperationFailure(ECatOnlineError::CreateFailed);
		return;
	}

	SessionState = ECatOnlineSessionState::Host;
	SessionRole = ECatOnlineSessionRole::Host;
	OperationRole = ECatOnlineSessionRole::Host;
	if (!BeginHostTravelToLake())
	{
		BeginDestroySession(ECatOnlineError::TravelRejected);
	}
}

// Find 回调流程：只消费当前 Find epoch；失败清空结果，成功为每个兼容平台结果生成随机句柄和不含原始身份的摘要，随后发布唯一终态。
void UCatOnlineSubsystem::HandleFindSessionsComplete(const bool bWasSuccessful, const uint64 CallbackEpoch)
{
	if (ActiveOperation != ECatOnlineOperation::Find || CallbackEpoch != OperationEpoch || !FindSessionsHandle.IsValid())
	{
		UE_LOG(LogCatOnline, Warning, TEXT("Event=online_callback_ignored Callback=Find Epoch=%llu CurrentEpoch=%llu"), CallbackEpoch, OperationEpoch);
		return;
	}
	if (OperationSessionInterface.IsValid())
	{
		OperationSessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsHandle);
	}
	// 同步完成时先作废句柄，FindSessions 返回后的外层就不会再用同步 false 覆盖本回调的具体结果。
	FindSessionsHandle.Reset();
	SearchResultsByHandle.Reset();
	SearchSummaries.Reset();
	if (!bWasSuccessful || !ActiveSearch.IsValid())
	{
		ActiveSearch.Reset();
		SessionState = ECatOnlineSessionState::NoSession;
		FinishOperationFailure(ECatOnlineError::FindFailed);
		return;
	}

	for (const FOnlineSessionSearchResult& SearchResult : ActiveSearch->SearchResults)
	{
		if (!SearchResult.IsValid() || !HasCompatibleSessionSettings(SearchResult.Session.SessionSettings))
		{
			continue;
		}
		const FGuid HandleValue = FGuid::NewGuid();
		SearchResultsByHandle.Add(HandleValue, SearchResult);
		FCatSessionSearchSummary& Summary = SearchSummaries.AddDefaulted_GetRef();
		Summary.Handle.Value = HandleValue;
		Summary.OwnerDisplayName = SearchResult.Session.OwningUserName;
		Summary.MaxPlayers = SearchResult.Session.SessionSettings.NumPublicConnections;
		Summary.CurrentPlayers = FMath::Max(0, Summary.MaxPlayers - SearchResult.Session.NumOpenPublicConnections);
		Summary.PingMilliseconds = SearchResult.PingInMs;
	}
	ActiveSearch.Reset();
	SessionState = ECatOnlineSessionState::NoSession;
	FinishOperationSuccess();
}

// Join 回调流程：只消费当前 Join epoch；成功后先确立 Client Session 事实，再解析连接地址并提交唯一 ClientTravel，解析/控制器失败均先 Destroy 本地 NamedSession。
void UCatOnlineSubsystem::HandleJoinSessionComplete(const FName SessionName, const EOnJoinSessionCompleteResult::Type Result, const uint64 CallbackEpoch)
{
	if (SessionName != CatOnlineNames::GameSession || ActiveOperation != ECatOnlineOperation::Join
		|| CallbackEpoch != OperationEpoch || !JoinSessionHandle.IsValid())
	{
		UE_LOG(LogCatOnline, Warning, TEXT("Event=online_callback_ignored Callback=Join Epoch=%llu CurrentEpoch=%llu"), CallbackEpoch, OperationEpoch);
		return;
	}
	if (OperationSessionInterface.IsValid())
	{
		OperationSessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionHandle);
	}
	// 同步完成先消费句柄；异步失败仍保留当前搜索/邀请代际供明确重试，成功则在旅行前统一废止全部候选。
	JoinSessionHandle.Reset();
	if (Result != EOnJoinSessionCompleteResult::Success)
	{
		SessionState = ECatOnlineSessionState::NoSession;
		SessionRole = ECatOnlineSessionRole::None;
		FinishOperationFailure(ECatOnlineError::JoinFailed);
		return;
	}

	SessionState = ECatOnlineSessionState::Client;
	SessionRole = ECatOnlineSessionRole::Client;
	OperationRole = ECatOnlineSessionRole::Client;
	SearchResultsByHandle.Reset();
	SearchSummaries.Reset();
	InvitesByHandle.Reset();
	InviteSummaries.Reset();
	FString ConnectString;
	if (!OperationSessionInterface.IsValid()
		|| !OperationSessionInterface->GetResolvedConnectString(CatOnlineNames::GameSession, ConnectString)
		|| !BeginClientTravelToLake(ConnectString))
	{
		BeginDestroySession(ECatOnlineError::ConnectStringUnavailable);
	}
}

// Destroy 回调流程：只消费当前 epoch；失败保留 Session 不确定性，成功清空本地角色；补偿链发布原始失败，正常 Leave 才继续按冻结角色回前台。
void UCatOnlineSubsystem::HandleDestroySessionComplete(const FName SessionName, const bool bWasSuccessful, const uint64 CallbackEpoch)
{
	if (SessionName != CatOnlineNames::GameSession || CallbackEpoch != OperationEpoch || !DestroySessionHandle.IsValid())
	{
		UE_LOG(LogCatOnline, Warning, TEXT("Event=online_callback_ignored Callback=Destroy Epoch=%llu CurrentEpoch=%llu"), CallbackEpoch, OperationEpoch);
		return;
	}
	if (OperationSessionInterface.IsValid())
	{
		OperationSessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionHandle);
	}
	// DestroySession 也可能同步回调；先作废句柄，外层只能观察已推进的旅行或终态，不能再广播 queued。
	DestroySessionHandle.Reset();
	if (!bWasSuccessful)
	{
		SessionState = ECatOnlineSessionState::Error;
		FinishOperationFailure(ECatOnlineError::DestroyFailed);
		return;
	}

	SessionState = ECatOnlineSessionState::NoSession;
	SessionRole = ECatOnlineSessionRole::None;
	if (DeferredFailureAfterDestroy != ECatOnlineError::None)
	{
		const ECatOnlineError Failure = DeferredFailureAfterDestroy;
		FinishOperationFailure(Failure);
		return;
	}
	if (OperationRole == ECatOnlineSessionRole::Client && PendingHostExitAckRequestId == ActiveRequestId)
	{
		if (ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr))
		{
			Controller->ServerAcknowledgeHostExit(PendingHostExitAckRequestId);
		}
		PendingHostExitAckRequestId.Invalidate();
	}
	if (!BeginTravelToFrontend())
	{
		FinishOperationFailure(ECatOnlineError::TravelRejected);
	}
}

// Run teardown 完成流程：先核对当前仍是同一 Host Leave 及相同 RequestId/epoch，再解绑精确 GameMode；Ready 进入唯一 Destroy 链，Failed 以 Online 错误结案，Pending 通知仅保留等待。
void UCatOnlineSubsystem::HandleRunTeardownCompleted(const FCatRunTeardownResult& Result)
{
	if (ActiveOperation != ECatOnlineOperation::Leave
		|| OperationRole != ECatOnlineSessionRole::Host
		|| Result.RequestId != ActiveRequestId
		|| Result.OperationEpoch != static_cast<int64>(OperationEpoch))
	{
		UE_LOG(LogCatOnline, Warning, TEXT("Event=online_callback_ignored Callback=RunTeardown RequestId=%s Epoch=%lld CurrentEpoch=%llu"),
			*Result.RequestId.ToString(EGuidFormats::DigitsWithHyphens), Result.OperationEpoch, OperationEpoch);
		return;
	}
	if (Result.Status == ECatRunTeardownStatus::Pending)
	{
		return;
	}

	ClearRunTeardownDelegate();
	if (Result.Status != ECatRunTeardownStatus::Ready)
	{
		FinishOperationFailure(ECatOnlineError::RunTeardownFailed);
		return;
	}
	BeginDestroySession(ECatOnlineError::None);
}

// 邀请接受流程：平台失败、无效或不兼容结果只更新结构化错误；有效结果生成 opaque 句柄，原始 UserId 不写日志也不进入 Snapshot，等待 UI 显式汇入统一 Join。
void UCatOnlineSubsystem::HandleSessionUserInviteAccepted(const bool bWasSuccessful, const int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& InviteResult)
{
	(void)UserId;
	if (!bWasSuccessful || ControllerId < 0 || !InviteResult.IsValid())
	{
		LastError = ECatOnlineError::InvalidHandle;
		BroadcastSnapshot(TEXT("online_invite_rejected"));
		return;
	}
	if (!HasCompatibleSessionSettings(InviteResult.Session.SessionSettings))
	{
		LastError = ECatOnlineError::SessionCompatibilityMismatch;
		BroadcastSnapshot(TEXT("online_invite_incompatible"));
		return;
	}

	const FGuid HandleValue = FGuid::NewGuid();
	InvitesByHandle.Add(HandleValue, InviteResult);
	FCatSessionInviteSummary& Summary = InviteSummaries.AddDefaulted_GetRef();
	Summary.Handle.Value = HandleValue;
	Summary.OwnerDisplayName = InviteResult.Session.OwningUserName;
	LastError = ECatOnlineError::None;
	BroadcastSnapshot(TEXT("online_invite_accepted"));
}

// 地图完成流程：先隔离空 World 和其他 GameInstance，再重绑该 World 的邀请接口；来源包回载时保留 pending 等 TravelFailure，意外包进入补偿。命中 ExpectedPackage 后先确认独立 World 事实，再按 Lake→Connected、Frontend→Idle 收敛 Transport，最后由原成功/失败路径结算 ActiveOperation；Session 事实始终只由平台回调维护。
void UCatOnlineSubsystem::HandlePostLoadMap(UWorld* LoadedWorld)
{
	if (!LoadedWorld || LoadedWorld->GetGameInstance() != GetGameInstance())
	{
		return;
	}
	RebindInviteDelegate();
	const FString PackageName = UWorld::StripPIEPrefixFromPackageName(LoadedWorld->GetPackage()->GetName(), LoadedWorld->StreamingLevelsPrefix);
	if (!ExpectedPackage.IsEmpty())
	{
		const bool bReturnedToSource = (ExpectedPackage == CatOnlineNames::Lake && PackageName == CatOnlineNames::Frontend)
			|| (ExpectedPackage == CatOnlineNames::Frontend && PackageName == CatOnlineNames::Lake);
		if (bReturnedToSource)
		{
			// UE 的部分失败链先重新载入来源 World，随后才广播 TravelFailure；这里不抢先使 epoch 失效，避免补偿链丢失 Session 事实。
			SetWorldStateForPackage(PackageName);
			BroadcastSnapshot(TEXT("online_source_world_reloaded"));
			return;
		}
		if (PackageName != ExpectedPackage)
		{
			ExpectedPackage.Reset();
			SetWorldStateForPackage(PackageName);
			TransportState = ECatOnlineTransportState::Failed;
			if ((ActiveOperation == ECatOnlineOperation::Create || ActiveOperation == ECatOnlineOperation::Join)
				&& SessionRole != ECatOnlineSessionRole::None)
			{
				BeginDestroySession(ECatOnlineError::UnexpectedMap);
			}
			else
			{
				FinishOperationFailure(ECatOnlineError::UnexpectedMap);
			}
			return;
		}

		ExpectedPackage.Reset();
		SetWorldStateForPackage(PackageName);
		TransportState = WorldState == ECatOnlineWorldState::Lake
			? ECatOnlineTransportState::Connected : ECatOnlineTransportState::Idle;
		if (DeferredFailureAfterTravel != ECatOnlineError::None)
		{
			const ECatOnlineError Failure = DeferredFailureAfterTravel;
			FinishOperationFailure(Failure);
		}
		else
		{
			FinishOperationSuccess();
		}
		return;
	}

	if (!SetWorldStateForPackage(PackageName))
	{
		LastError = ECatOnlineError::UnexpectedMap;
	}
	TransportState = WorldState == ECatOnlineWorldState::Lake ? ECatOnlineTransportState::Connected : ECatOnlineTransportState::Idle;
	BroadcastSnapshot(TEXT("online_world_observed"));
}

// 旅行失败流程：先按 GameInstance 过滤并记录来源 World；Create/Join 的已建 Session 必须先 Destroy 补偿，Leave 已完成的平台清理则直接发布失败且不触发第二次旅行。
void UCatOnlineSubsystem::HandleTravelFailure(UWorld* FailureWorld, const ETravelFailure::Type FailureType, const FString& Reason)
{
	if (!FailureWorld || FailureWorld->GetGameInstance() != GetGameInstance())
	{
		return;
	}
	const FString PackageName = UWorld::StripPIEPrefixFromPackageName(FailureWorld->GetPackage()->GetName(), FailureWorld->StreamingLevelsPrefix);
	SetWorldStateForPackage(PackageName);
	ExpectedPackage.Reset();
	TransportState = ECatOnlineTransportState::Failed;
	UE_LOG(LogCatOnline, Error, TEXT("Event=online_travel_failure RequestId=%s Epoch=%llu Operation=%s Type=%s Reason=%s"),
		*ActiveRequestId.ToString(EGuidFormats::DigitsWithHyphens), OperationEpoch, *UEnum::GetValueAsString(ActiveOperation), ETravelFailure::ToString(FailureType), *Reason);

	if ((ActiveOperation == ECatOnlineOperation::Create || ActiveOperation == ECatOnlineOperation::Join)
		&& SessionRole != ECatOnlineSessionRole::None)
	{
		if (SessionState == ECatOnlineSessionState::Destroying)
		{
			DeferredFailureAfterDestroy = ECatOnlineError::TravelFailed;
			BroadcastSnapshot(TEXT("online_travel_failure_waiting_destroy"));
		}
		else
		{
			BeginDestroySession(ECatOnlineError::TravelFailed);
		}
		return;
	}
	if (ActiveOperation != ECatOnlineOperation::None)
	{
		FinishOperationFailure(ECatOnlineError::TravelFailed);
	}
	else
	{
		LastError = ECatOnlineError::TravelFailed;
		BroadcastSnapshot(TEXT("online_travel_failure_observed"));
	}
}

// 网络失败流程：UE 的全局事件会广播任意 NetDriver，先把来源收窄到本 GameInstance 的当前 GameNetDriver 或 PendingNetDriver，再与 TravelFailure 独立记录和补偿。
// 已建立连接的 Client 与 Listen Host 都落在 GameNetDriver；Join 握手失败则由 PendingNetDriver 广播且 FailureWorld 可能为空，所以两条路径必须分别用注册表和 WorldContext 验证，Beacon 等驱动一律忽略。
void UCatOnlineSubsystem::HandleNetworkFailure(UWorld* FailureWorld, UNetDriver* NetDriver, const ENetworkFailure::Type FailureType, const FString& Reason)
{
	if (!GEngine || !NetDriver)
	{
		return;
	}

	const bool bIsCurrentGameDriver = NetDriver->NetDriverName == NAME_GameNetDriver
		&& FailureWorld
		&& FailureWorld->GetGameInstance() == GetGameInstance()
		&& GEngine->FindNamedNetDriver(FailureWorld, NAME_GameNetDriver) == NetDriver;
	bool bIsCurrentPendingDriver = false;
	if (NetDriver->NetDriverName == NAME_PendingNetDriver)
	{
		// UE 5.8 的 PendingConnectionFailure 用空 World 广播；引擎公开的精确反查同时验证 PendingNetGame 仍存活及其所属 GameInstance。
		const FWorldContext* PendingContext = GEngine->GetWorldContextFromPendingNetGameNetDriver(NetDriver);
		bIsCurrentPendingDriver = PendingContext && PendingContext->OwningGameInstance == GetGameInstance();
	}
	if (!bIsCurrentGameDriver && !bIsCurrentPendingDriver)
	{
		return;
	}

	TransportState = ECatOnlineTransportState::Failed;
	UE_LOG(LogCatOnline, Error, TEXT("Event=online_network_failure RequestId=%s Epoch=%llu Operation=%s Driver=%s Type=%s Reason=%s"),
		*ActiveRequestId.ToString(EGuidFormats::DigitsWithHyphens), OperationEpoch, *UEnum::GetValueAsString(ActiveOperation),
		*NetDriver->NetDriverName.ToString(), ENetworkFailure::ToString(FailureType), *Reason);

	if (ActiveOperation == ECatOnlineOperation::Create || ActiveOperation == ECatOnlineOperation::Join)
	{
		if (SessionState == ECatOnlineSessionState::Destroying)
		{
			DeferredFailureAfterDestroy = ECatOnlineError::NetworkFailure;
			BroadcastSnapshot(TEXT("online_network_failure_waiting_destroy"));
		}
		else if (SessionRole != ECatOnlineSessionRole::None)
		{
			ExpectedPackage.Reset();
			BeginDestroySession(ECatOnlineError::NetworkFailure);
		}
		else
		{
			FinishOperationFailure(ECatOnlineError::NetworkFailure);
		}
		return;
	}
	if (ActiveOperation == ECatOnlineOperation::Leave)
	{
		DeferredFailureAfterTravel = ECatOnlineError::NetworkFailure;
		LastError = ECatOnlineError::NetworkFailure;
		BroadcastSnapshot(TEXT("online_network_failure_during_leave"));
		return;
	}
	if (ActiveOperation != ECatOnlineOperation::None)
	{
		FinishOperationFailure(ECatOnlineError::NetworkFailure);
		return;
	}

	FCatOnlineResult CleanupResult = BeginOperation(ECatOnlineOperation::Leave, ECatOnlineSessionState::Destroying);
	if (!CleanupResult.bAccepted)
	{
		return;
	}
	OperationRole = SessionRole;
	DeferredFailureAfterTravel = ECatOnlineError::NetworkFailure;
	if (SessionRole != ECatOnlineSessionRole::None)
	{
		BeginDestroySession(ECatOnlineError::None);
	}
	else
	{
		SessionState = ECatOnlineSessionState::NoSession;
		FinishOperationFailure(ECatOnlineError::NetworkFailure);
	}
}

// World 归类流程：仅比较稳定长包名并写 WorldState；未知包返回 false，由调用者决定是否结束当前复合操作。
bool UCatOnlineSubsystem::SetWorldStateForPackage(const FString& PackageName)
{
	if (PackageName == CatOnlineNames::Frontend)
	{
		WorldState = ECatOnlineWorldState::Frontend;
		return true;
	}
	if (PackageName == CatOnlineNames::Lake)
	{
		WorldState = ECatOnlineWorldState::Lake;
		return true;
	}
	WorldState = ECatOnlineWorldState::Error;
	return false;
}

// 成功结案流程：先解绑当前平台回调和释放搜索对象，再清操作上下文并推进 epoch；成功只清 LastError，不改已经由各自回调确认的 Session/World/Transport。
void UCatOnlineSubsystem::FinishOperationSuccess()
{
	ClearRunTeardownDelegate();
	ClearOperationDelegates();
	ActiveSearch.Reset();
	ExpectedPackage.Reset();
	ActiveOperation = ECatOnlineOperation::None;
	OperationRole = ECatOnlineSessionRole::None;
	DeferredFailureAfterDestroy = ECatOnlineError::None;
	DeferredFailureAfterTravel = ECatOnlineError::None;
	PendingHostExitAckRequestId.Invalidate();
	LastError = ECatOnlineError::None;
	++OperationEpoch;
	BroadcastSnapshot(TEXT("online_operation_succeeded"));
}

// 失败结案流程：先解绑并使旧 epoch 失效，再清当前复合操作；不覆盖调用者刚确认的 Session/World/Transport 事实，避免错误枚举冒充生命周期真相。
void UCatOnlineSubsystem::FinishOperationFailure(const ECatOnlineError Error)
{
	ClearRunTeardownDelegate();
	ClearOperationDelegates();
	ActiveSearch.Reset();
	ExpectedPackage.Reset();
	ActiveOperation = ECatOnlineOperation::None;
	OperationRole = ECatOnlineSessionRole::None;
	DeferredFailureAfterDestroy = ECatOnlineError::None;
	DeferredFailureAfterTravel = ECatOnlineError::None;
	PendingHostExitAckRequestId.Invalidate();
	LastError = Error;
	++OperationEpoch;
	BroadcastSnapshot(TEXT("online_operation_failed"));
}

// Run teardown 解绑流程：仅在保存的 GameMode 仍有效且句柄有效时移除；随后无条件 Reset 两者，使同步回调、失败结案和 World 销毁可以安全重复清理。
void UCatOnlineSubsystem::ClearRunTeardownDelegate()
{
	if (ACatfishingGameModeBase* GameMode = RunTeardownGameMode.Get(); GameMode && RunTeardownHandle.IsValid())
	{
		GameMode->OnRunTeardownCompleted().Remove(RunTeardownHandle);
	}
	RunTeardownHandle.Reset();
	RunTeardownGameMode.Reset();
}

// 操作委托清理流程：只在保存的精确 Session 接口上逐一清理有效句柄；每个 Clear 同时 Reset 句柄，重复调用不会影响后续 epoch。
void UCatOnlineSubsystem::ClearOperationDelegates()
{
	if (OperationSessionInterface.IsValid())
	{
		if (CreateSessionHandle.IsValid())
		{
			OperationSessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionHandle);
		}
		if (FindSessionsHandle.IsValid())
		{
			OperationSessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsHandle);
		}
		if (JoinSessionHandle.IsValid())
		{
			OperationSessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionHandle);
		}
		if (DestroySessionHandle.IsValid())
		{
			OperationSessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionHandle);
		}
	}
	CreateSessionHandle.Reset();
	FindSessionsHandle.Reset();
	JoinSessionHandle.Reset();
	DestroySessionHandle.Reset();
	OperationSessionInterface.Reset();
}

// 邀请重绑流程：先从旧 World 精确接口解绑，再按新 World 的 OnlineSubsystem 取 Session 接口；缺失时保持未绑定且不改四类事实，不保留旧接口旁路。
void UCatOnlineSubsystem::RebindInviteDelegate()
{
	ClearInviteDelegate();
	InviteSessionInterface = GetWorldSessionInterface();
	if (!InviteSessionInterface.IsValid())
	{
		return;
	}
	InviteAcceptedHandle = InviteSessionInterface->AddOnSessionUserInviteAcceptedDelegate_Handle(
		FOnSessionUserInviteAcceptedDelegate::CreateUObject(this, &ThisClass::HandleSessionUserInviteAccepted));
}

// 邀请解绑流程：只在保存的精确接口上清理有效句柄；随后同时 Reset 句柄和接口，保证 Deinitialize/PostLoadMap 可重复调用。
void UCatOnlineSubsystem::ClearInviteDelegate()
{
	if (InviteSessionInterface.IsValid() && InviteAcceptedHandle.IsValid())
	{
		InviteSessionInterface->ClearOnSessionUserInviteAcceptedDelegate_Handle(InviteAcceptedHandle);
	}
	InviteAcceptedHandle.Reset();
	InviteSessionInterface.Reset();
}

// 快照广播流程：日志关联 RequestId/epoch 和四类事实，World 只临时读取名称/NetMode；原始 StableNetId、连接字符串和平台结果不会进入日志。
void UCatOnlineSubsystem::BroadcastSnapshot(const TCHAR* EventName)
{
	const UWorld* World = GetWorld();
	UE_LOG(LogCatOnline, Log, TEXT("Event=%s RequestId=%s Epoch=%llu Operation=%s Session=%s Role=%s WorldState=%s Transport=%s World=%s NetMode=%d Error=%s"),
		EventName,
		*ActiveRequestId.ToString(EGuidFormats::DigitsWithHyphens),
		OperationEpoch,
		*UEnum::GetValueAsString(ActiveOperation),
		*UEnum::GetValueAsString(SessionState),
		*UEnum::GetValueAsString(SessionRole),
		*UEnum::GetValueAsString(WorldState),
		*UEnum::GetValueAsString(TransportState),
		World ? *World->GetName() : TEXT("None"),
		World ? static_cast<int32>(World->GetNetMode()) : -1,
		*UEnum::GetValueAsString(LastError));
	OnSnapshotChanged.Broadcast();
}

// 兼容合同检查流程：Create 和 Join 都只比较同一对平台设置；任何不相等结果在 Session API 前被拒绝，不交给旅行阶段兜底。
bool UCatOnlineSubsystem::HasCompatibleSessionSettings(const FOnlineSessionSettings& Settings)
{
	return Settings.bUsesPresence == Settings.bUseLobbiesIfAvailable;
}
