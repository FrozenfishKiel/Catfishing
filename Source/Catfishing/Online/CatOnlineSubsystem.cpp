#include "Online/CatOnlineSubsystem.h"

#include "Async/Async.h"
#include "Logging/CatLog.h"
#include "Online/CatOnlineSettings.h"
#include "Save/CatSaveSubsystem.h"
#include "Settings/CatGameUserSettings.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Interfaces/OnlinePresenceInterface.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "Online/OnlineSessionNames.h"
#include "UObject/UObjectGlobals.h"

#if WITH_STEAMWORKS
THIRD_PARTY_INCLUDES_START
#include "steam/steam_api.h"
THIRD_PARTY_INCLUDES_END
#endif

namespace CatOnlineNames
{
	/** 项目唯一 NamedSession 名称；Create/Join/Resolve/Destroy 全部使用同一键，避免本地形成第二份会话事实。 */
	static const FName GameSession(TEXT("GameSession"));
	/** Frontend 的稳定长包名；WorldState 与旅行目标共用，PIE 前缀在比较前统一剥离。 */
	static const FString Frontend(TEXT("/Game/Catfishing/Maps/Frontend"));
	/** Session 搜索摘要中的地图键；它只用于平台发现信息，不参与 World 到达判定。 */
	static const FName MapSetting(TEXT("CAT_MAP"));
	/** AppId 480 是共享测试池；项目键把 Catfishing 会话与其他 Spacewar 开发房间隔离。 */
	static const FName ProjectSetting(TEXT("CAT_PROJECT"));
	static const FString ProjectId(TEXT("Catfishing"));
	/** 协议键阻止网络合同不兼容的旧构建进入当前房间。 */
	static const FName ProtocolSetting(TEXT("CAT_PROTOCOL_VERSION"));
	static const FString ProtocolVersion(TEXT("1"));
	/** Steam Lobby 元数据里的可展示名称；值由 Steam Lobby 写入，缺失时 UI 回退到 OSS 房主显示名。 */
	static const ANSICHAR* RoomNameLobbyKey = "CAT_ROOM_NAME";
	/** Steam Lobby 的 Host ready 元数据；只有 Lake 的 GameNetDriver 已创建后才由 Host 写入，Client 用它决定何时预载并连接。 */
	static const ANSICHAR* LobbyReadyKey = "CAT_GAME_READY";
	/** 单个 Lobby 内 Client 自动启动的最大次数；轮询和失败收口共用，耗尽后必须由用户退出再加入以开启新预算。 */
	static constexpr int32 MaxClientGameplayStartAttempts = 3;
	/** Host ready 失败后等待下一次发布尝试的秒数；PostLoadMap 与 Lobby 轮询用它写入单调时间，轮询只在 Steam Lobby 可写时消费以避免 NULL 后端刷屏。 */
	static constexpr double HostLobbyReadyRetrySeconds = 2.0;
	/** 平台已确认邀请等待前台和本地身份的最长秒数；接受时冻结期限，持续未就绪也不能重新开始计时。 */
	static constexpr double AcceptedInviteWaitSeconds = 30.0;
}

namespace CatOnlineRoomFacts
{
	/** 将当前 NamedSession 的真实设置翻译成展示策略；无法完整验证时保持 Undecided，避免 UI 擅自选择公开权限。 */
	static ECatSessionAccessPolicy GetAccessPolicy(const FOnlineSessionSettings& Settings)
	{
		if (Settings.bShouldAdvertise && Settings.bAllowJoinViaPresence)
		{
			return ECatSessionAccessPolicy::Public;
		}
		if (Settings.bAllowJoinViaPresenceFriendsOnly)
		{
			return ECatSessionAccessPolicy::FriendsOnly;
		}
		if (!Settings.bShouldAdvertise && Settings.bAllowInvites)
		{
			return ECatSessionAccessPolicy::InviteOnly;
		}
		return ECatSessionAccessPolicy::Undecided;
	}
}

// 初始化流程：先绑定三类引擎生命周期和当前 World 的邀请接口，再注册低频邀请检查并建立 World 快照。
// UE Steam 将冷启动邀请保留到接受委托已绑定；当前 World/接口暂缺时由检查恢复订阅，不解析命令行制造第二条平台入口。
void UCatOnlineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	GetDefault<UCatOnlineSettings>()->TryGetGameplayMapPackage(GameplayMapPackage);
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ThisClass::HandlePostLoadMap);
	if (GEngine)
	{
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &ThisClass::HandleTravelFailure);
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &ThisClass::HandleNetworkFailure);
	}
	RebindInviteDelegate();
	PlatformInviteTickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &ThisClass::TickPlatformInvites), 0.5f);
	if (const UWorld* World = GetWorld())
	{
		const FString PackageName = UWorld::StripPIEPrefixFromPackageName(World->GetPackage()->GetName(), World->StreamingLevelsPrefix);
		SetWorldStateForPackage(PackageName);
		TransportState = WorldState == ECatOnlineWorldState::Lake ? ECatOnlineTransportState::Connected : ECatOnlineTransportState::Idle;
	}
	BroadcastSnapshot(TEXT("online_initialized"));
}

// 销毁流程：先推进 epoch 并移除邀请检查，记录尚未提交的邀请已随 GameInstance 关闭而失效；随后撤销载荷释放许可并解绑 Save 等待，不在关闭时释放或另存世界，最后解除其余委托、清缓存与广播并交还父类。
void UCatOnlineSubsystem::Deinitialize()
{
	++OperationEpoch;
	FTSTicker::RemoveTicker(PlatformInviteTickHandle);
	PlatformInviteTickHandle.Reset();
	if (PendingAcceptedInvite.IsValid())
	{
		UE_LOG(LogCatOnline, Warning, TEXT("Event=online_invite_cancelled InviteId=%s Epoch=%llu Reason=Shutdown"),
			*PendingAcceptedInvite.Value.ToString(EGuidFormats::DigitsWithHyphens), OperationEpoch);
	}
	ClearPendingAcceptedInvite();
	ClearRunReleaseDelegate();
	bReleaseActiveRunOnFrontend = false;
	ClearHostLeaveSaveDelegate();
	ClearRunTeardownDelegate();
	ClearOperationDelegates();
	ClearInviteDelegate();
	StopLobbyFactPolling();
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
	FriendsByHandle.Reset();
	FriendSummaries.Reset();
	FriendsInterface.Reset();
	++FriendsRefreshEpoch;
	bFriendsRefreshPending = false;
	CurrentLobbyId.Reset();
	CurrentLobbyOwnerId.Reset();
	RoomMembers.Reset();
	RoomMaxPlayers = 0;
	RoomCurrentPlayers = 0;
	CurrentRoomName.Reset();
	CurrentSessionAccess = ECatSessionAccessPolicy::Undecided;
	GameplayPreloadRequestId = INDEX_NONE;
	PreloadedGameplayPackage = nullptr;
	SearchResultsByHandle.Reset();
	SearchSummaries.Reset();
	InvitesByHandle.Reset();
	InviteSummaries.Reset();
	ExpectedPackage.Reset();
	GameplayMapPackage.Reset();
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

// Friends 接口定位流程：先用当前 GameInstance 的 World 查询 OSS，再返回该实例的 Friends 接口；这和 Session 查询同样禁止退回无 World 的进程级默认接口，避免 PIE 串到另一个本地玩家。
IOnlineFriendsPtr UCatOnlineSubsystem::GetWorldFriendsInterface() const
{
	const UWorld* World = GetWorld();
	IOnlineSubsystem* OnlineSubsystem = World ? Online::GetSubsystem(World) : nullptr;
	return OnlineSubsystem ? OnlineSubsystem->GetFriendsInterface() : nullptr;
}

// 房间事实刷新流程：先清除上一份 Lobby 投影，再从当前 NamedSession 读取容量、房主名称和权限；只有 Steam SDK 同时确认 SessionId 是已加入 Lobby 时才展开成员和邀请码，未知数据严格保持空值。
void UCatOnlineSubsystem::RefreshRoomSnapshotFacts()
{
	CurrentLobbyId.Reset();
	CurrentLobbyOwnerId.Reset();
	RoomMembers.Reset();
	CurrentRoomName.Reset();
	CurrentSessionAccess = ECatSessionAccessPolicy::Undecided;
	RoomMaxPlayers = 0;
	RoomCurrentPlayers = 0;

	const IOnlineSessionPtr SessionInterface = GetWorldSessionInterface();
	const FNamedOnlineSession* NamedSession = SessionInterface.IsValid()
		? SessionInterface->GetNamedSession(CatOnlineNames::GameSession)
		: nullptr;
	if (!NamedSession)
	{
		return;
	}

	RoomMaxPlayers = NamedSession->SessionSettings.NumPublicConnections;
	RoomCurrentPlayers = FMath::Max(0, RoomMaxPlayers - NamedSession->NumOpenPublicConnections);
	CurrentSessionAccess = CatOnlineRoomFacts::GetAccessPolicy(NamedSession->SessionSettings);
	CurrentRoomName = NamedSession->OwningUserName;
	if (!NamedSession->SessionInfo.IsValid())
	{
		return;
	}

#if WITH_STEAMWORKS
	const FString CandidateLobbyId = NamedSession->SessionInfo->GetSessionId().ToString();
	TCHAR* ParseEnd = nullptr;
	const uint64 NumericLobbyId = FCString::Strtoui64(*CandidateLobbyId, &ParseEnd, 10);
	const CSteamID LobbyId(NumericLobbyId);
	if (CandidateLobbyId.IsEmpty() || !ParseEnd || *ParseEnd != TEXT('\0') || !LobbyId.IsLobby()
		|| !SteamAPI_IsSteamRunning() || !SteamMatchmaking() || !SteamFriends())
	{
		return;
	}

	const char* LobbyRoomName = SteamMatchmaking()->GetLobbyData(LobbyId, CatOnlineNames::RoomNameLobbyKey);
	if (LobbyRoomName && LobbyRoomName[0] != '\0')
	{
		CurrentRoomName = UTF8_TO_TCHAR(LobbyRoomName);
	}
	const int32 MemberCount = SteamMatchmaking()->GetNumLobbyMembers(LobbyId);
	if (MemberCount < 0)
	{
		return;
	}

	CurrentLobbyId = CandidateLobbyId;
	const CSteamID LobbyOwner = SteamMatchmaking()->GetLobbyOwner(LobbyId);
	if (LobbyOwner.IsValid())
	{
		CurrentLobbyOwnerId = LexToString(LobbyOwner.ConvertToUint64());
	}
	for (int32 MemberIndex = 0; MemberIndex < MemberCount; ++MemberIndex)
	{
		const CSteamID MemberId = SteamMatchmaking()->GetLobbyMemberByIndex(LobbyId, MemberIndex);
		if (!MemberId.IsValid())
		{
			continue;
		}
		const char* PersonaName = SteamFriends()->GetFriendPersonaName(MemberId);
		if (!PersonaName || PersonaName[0] == '\0')
		{
			continue;
		}
		FCatOnlineRoomMember& Member = RoomMembers.AddDefaulted_GetRef();
		Member.DisplayName = UTF8_TO_TCHAR(PersonaName);
		Member.bIsLobbyOwner = MemberId == LobbyOwner;
	}
#endif
}

// Lobby 轮询启动流程：Create/Join 后注册 0.5 秒一次的 CoreTicker；首次刷新尚未拿到 LobbyId 时仍保留轮询，让 Steam 回填 SessionInfo 后开始读取，重复调用不会注册第二份句柄。
void UCatOnlineSubsystem::StartLobbyFactPolling()
{
	if (LobbyFactPollHandle.IsValid())
	{
		return;
	}
	RefreshRoomSnapshotFacts();
	LobbyFactPollHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &ThisClass::TickLobbyFacts), 0.5f);
}

// Lobby 轮询停止流程：仅移除本子系统自己注册的 ticker，再清空 ready 观察值、Host 发布截止点、Client 尝试次数和退避时间；只有离开旧 Lobby 才为下一次加入释放完整重试预算。
void UCatOnlineSubsystem::StopLobbyFactPolling()
{
	if (LobbyFactPollHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(LobbyFactPollHandle);
	}
	LobbyFactPollHandle.Reset();
	bLobbyReadyObserved = false;
	NextHostLobbyReadyPublishAttemptTime = 0.0;
	ClientGameplayStartAttempts = 0;
	NextClientGameplayStartTime = 0.0;
}

// Lobby 轮询流程：先保存公开事实，再从 Steam SDK 重建成员与 ready。
// Host 已在玩法图、当前空闲且 ready 未写入时，只有 Steam Lobby 可写并到达 NextHostLobbyReadyPublishAttemptTime 才重试发布；失败只推迟下一次尝试，不触发 DestroySession 或 Frontend travel。
// Client 只在 ready 成立、预算未耗尽且退避到期时开始预载；其余轮询只有事实变化才广播，避免 NULL 后端每 0.5 秒刷 ready 失败日志。
bool UCatOnlineSubsystem::TickLobbyFacts(const float DeltaSeconds)
{
	(void)DeltaSeconds;
	const FString PreviousLobbyId = CurrentLobbyId;
	const FString PreviousOwnerId = CurrentLobbyOwnerId;
	const FString PreviousRoomName = CurrentRoomName;
	const int32 PreviousMaxPlayers = RoomMaxPlayers;
	const int32 PreviousCurrentPlayers = RoomCurrentPlayers;
	const ECatSessionAccessPolicy PreviousAccess = CurrentSessionAccess;
	const TArray<FCatOnlineRoomMember> PreviousMembers = RoomMembers;
	const bool bWasReady = bLobbyReadyObserved;

	RefreshRoomSnapshotFacts();
	bLobbyReadyObserved = IsCurrentLobbyReady();
	bool bCanAttemptHostReadyPublish = false;
#if WITH_STEAMWORKS
	bCanAttemptHostReadyPublish = !CurrentLobbyId.IsEmpty() && SteamAPI_IsSteamRunning() && SteamMatchmaking();
#endif
	const double CurrentTime = FPlatformTime::Seconds();
	if (SessionRole == ECatOnlineSessionRole::Host && WorldState == ECatOnlineWorldState::Lake
		&& ActiveOperation == ECatOnlineOperation::None && !bLobbyReadyObserved && bCanAttemptHostReadyPublish
		&& CurrentTime >= NextHostLobbyReadyPublishAttemptTime)
	{
		if (IsHostGameplayWorldReadyForClientAdmission() && PublishLobbyReady())
		{
			NextHostLobbyReadyPublishAttemptTime = 0.0;
			bLobbyReadyObserved = true;
		}
		else
		{
			NextHostLobbyReadyPublishAttemptTime = CurrentTime + CatOnlineNames::HostLobbyReadyRetrySeconds;
		}
	}

	bool bMembersChanged = PreviousMembers.Num() != RoomMembers.Num();
	if (!bMembersChanged)
	{
		for (int32 Index = 0; Index < RoomMembers.Num(); ++Index)
		{
			if (PreviousMembers[Index].DisplayName != RoomMembers[Index].DisplayName
				|| PreviousMembers[Index].bIsLobbyOwner != RoomMembers[Index].bIsLobbyOwner)
			{
				bMembersChanged = true;
				break;
			}
		}
	}
	const bool bFactsChanged = PreviousLobbyId != CurrentLobbyId || PreviousOwnerId != CurrentLobbyOwnerId
		|| PreviousRoomName != CurrentRoomName || PreviousMaxPlayers != RoomMaxPlayers
		|| PreviousCurrentPlayers != RoomCurrentPlayers || PreviousAccess != CurrentSessionAccess
		|| bMembersChanged || bWasReady != bLobbyReadyObserved;
	if (SessionRole == ECatOnlineSessionRole::Client && WorldState == ECatOnlineWorldState::Frontend
		&& ActiveOperation == ECatOnlineOperation::None && bLobbyReadyObserved
		&& ClientGameplayStartAttempts < CatOnlineNames::MaxClientGameplayStartAttempts
		&& FPlatformTime::Seconds() >= NextClientGameplayStartTime)
	{
		BeginClientGameplayPreload();
		return true;
	}
	if (bFactsChanged)
	{
		BroadcastSnapshot(TEXT("online_lobby_facts_changed"));
	}
	return true;
}

// Lobby ready 查询流程：仅在 Steam SDK 已确认当前 SessionId 是 Lobby 且可读取元数据时检查 CAT_GAME_READY；平台不可用、成员资格不明或键缺失都 fail-closed 为 false。
bool UCatOnlineSubsystem::IsCurrentLobbyReady() const
{
#if WITH_STEAMWORKS
	if (CurrentLobbyId.IsEmpty() || !SteamAPI_IsSteamRunning() || !SteamMatchmaking())
	{
		return false;
	}
	TCHAR* ParseEnd = nullptr;
	const uint64 NumericLobbyId = FCString::Strtoui64(*CurrentLobbyId, &ParseEnd, 10);
	const CSteamID LobbyId(NumericLobbyId);
	if (!ParseEnd || *ParseEnd != TEXT('\0') || !LobbyId.IsLobby() || SteamMatchmaking()->GetNumLobbyMembers(LobbyId) < 0)
	{
		return false;
	}
	const char* ReadyValue = SteamMatchmaking()->GetLobbyData(LobbyId, CatOnlineNames::LobbyReadyKey);
	return ReadyValue && FCStringAnsi::Stricmp(ReadyValue, "1") == 0;
#else
	return false;
#endif
}

// Host 玩法 World 就绪检查流程：先确认当前 World 已经是 listen 玩法图且 GameNetDriver 存在；再读取 authority GameMode 的 Run 公开事实和 Host 命令门。
// 返回值只决定是否发布客户端准入 ready；失败会记录原因并让本次 ready 不发布，不提交 DestroySession 或 Frontend travel。
bool UCatOnlineSubsystem::IsHostGameplayWorldReadyForClientAdmission() const
{
	UWorld* World = GetWorld();
	if (!World || WorldState != ECatOnlineWorldState::Lake || World->GetNetMode() != NM_ListenServer || !GEngine
		|| !GEngine->FindNamedNetDriver(World, NAME_GameNetDriver))
	{
		UE_LOG(LogCatOnline, Warning,
			TEXT("Event=online_lobby_ready_rejected RequestId=%s Epoch=%llu World=%s NetMode=%d Reason=GameplayWorldUnavailable"),
			*ActiveRequestId.ToString(EGuidFormats::DigitsWithHyphens), OperationEpoch,
			World ? *World->GetName() : TEXT("None"), World ? static_cast<int32>(World->GetNetMode()) : -1);
		return false;
	}
	const ACatfishingGameModeBase* GameMode = World->GetAuthGameMode<ACatfishingGameModeBase>();
	const APlayerController* HostController = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController() : nullptr;
	if (!GameMode || !HostController)
	{
		UE_LOG(LogCatOnline, Warning,
			TEXT("Event=online_lobby_ready_rejected RequestId=%s Epoch=%llu World=%s NetMode=%d Reason=%s"),
			*ActiveRequestId.ToString(EGuidFormats::DigitsWithHyphens), OperationEpoch,
			*World->GetName(), static_cast<int32>(World->GetNetMode()), GameMode ? TEXT("HostControllerMissing") : TEXT("GameModeMissing"));
		return false;
	}
	const FCatRunPublicState& Run = GameMode->GetRunPublicState();
	if (!Run.Phase.RunId.IsValid() || Run.Phase.Phase == ECatRunPhase::NotStarted
		|| Run.EndReason == ECatRunEndReason::StartupFailed || !GameMode->CanAcceptGameplayCommand(HostController))
	{
		UE_LOG(LogCatOnline, Warning, TEXT("Event=online_lobby_ready_rejected RequestId=%s Epoch=%llu World=%s NetMode=%d RunId=%s Phase=%s EndReason=%s"),
			*ActiveRequestId.ToString(EGuidFormats::DigitsWithHyphens), OperationEpoch, *World->GetName(), static_cast<int32>(World->GetNetMode()),
			*Run.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), *UEnum::GetValueAsString(Run.Phase.Phase), *UEnum::GetValueAsString(Run.EndReason));
		return false;
	}
	return true;
}

// Host ready 发布流程：这里只处理 Steam Lobby 元数据写入，不再把平台元数据不可写误判为 Host 玩法地图启动失败。
// 返回 false 表示当前平台无法写 CAT_GAME_READY；Host 仍留在 Lake/Session，Client 只会在前台继续等待 ready 或沿自身失败路径提示退出重加入。
bool UCatOnlineSubsystem::PublishLobbyReady()
{
#if WITH_STEAMWORKS
	UWorld* World = GetWorld();
	if (CurrentLobbyId.IsEmpty() || !SteamAPI_IsSteamRunning() || !SteamMatchmaking())
	{
		UE_LOG(LogCatOnline, Warning,
			TEXT("Event=online_lobby_ready_publish_rejected RequestId=%s Epoch=%llu LobbyId=%s SteamRunning=%d HasMatchmaking=%d"),
			*ActiveRequestId.ToString(EGuidFormats::DigitsWithHyphens), OperationEpoch,
			CurrentLobbyId.IsEmpty() ? TEXT("None") : TEXT("Present"),
			SteamAPI_IsSteamRunning() ? 1 : 0, SteamMatchmaking() ? 1 : 0);
		return false;
	}
	TCHAR* ParseEnd = nullptr;
	const uint64 NumericLobbyId = FCString::Strtoui64(*CurrentLobbyId, &ParseEnd, 10);
	const CSteamID LobbyId(NumericLobbyId);
	if (!ParseEnd || *ParseEnd != TEXT('\0') || !LobbyId.IsLobby())
	{
		UE_LOG(LogCatOnline, Warning,
			TEXT("Event=online_lobby_ready_publish_rejected RequestId=%s Epoch=%llu LobbyId=Invalid Reason=InvalidLobbyId"),
			*ActiveRequestId.ToString(EGuidFormats::DigitsWithHyphens), OperationEpoch);
		return false;
	}
	const bool bPublished = SteamMatchmaking()->SetLobbyData(LobbyId, CatOnlineNames::LobbyReadyKey, "1");
	if (bPublished)
	{
		bLobbyReadyObserved = true;
		UE_LOG(LogCatOnline, Log, TEXT("Event=online_lobby_ready_published RequestId=%s Epoch=%llu World=%s NetMode=%d"),
			*ActiveRequestId.ToString(EGuidFormats::DigitsWithHyphens), OperationEpoch,
			World ? *World->GetName() : TEXT("None"), World ? static_cast<int32>(World->GetNetMode()) : -1);
	}
	else
	{
		UE_LOG(LogCatOnline, Warning,
			TEXT("Event=online_lobby_ready_publish_rejected RequestId=%s Epoch=%llu LobbyId=Present Reason=SetLobbyDataRejected"),
			*ActiveRequestId.ToString(EGuidFormats::DigitsWithHyphens), OperationEpoch);
	}
	return bPublished;
#else
	UE_LOG(LogCatOnline, Warning,
		TEXT("Event=online_lobby_ready_publish_rejected RequestId=%s Epoch=%llu Reason=SteamworksUnavailable"),
		*ActiveRequestId.ToString(EGuidFormats::DigitsWithHyphens), OperationEpoch);
	return false;
#endif
}

// Client 预载启动流程：确认前台 Client、真实 Lobby ready、次数预算和退避截止点后受理 Start 并计次；随后提交真实异步预载，失败统一进入退避，成功回调复核 ready 后才发起 ClientTravel。
void UCatOnlineSubsystem::BeginClientGameplayPreload()
{
	if (ActiveOperation != ECatOnlineOperation::None || WorldState != ECatOnlineWorldState::Frontend
		|| SessionRole != ECatOnlineSessionRole::Client || GameplayMapPackage.IsEmpty() || !IsCurrentLobbyReady()
		|| ClientGameplayStartAttempts >= CatOnlineNames::MaxClientGameplayStartAttempts
		|| FPlatformTime::Seconds() < NextClientGameplayStartTime)
	{
		return;
	}
	FCatOnlineResult Result = BeginOperation(ECatOnlineOperation::Start, ECatOnlineSessionState::Client);
	if (!Result.bAccepted)
	{
		return;
	}
	OperationRole = ECatOnlineSessionRole::Client;
	++ClientGameplayStartAttempts;
	const uint64 SubmittedEpoch = OperationEpoch;
	GameplayPreloadRequestId = INDEX_NONE - 1;
	const int32 SubmittedRequestId = LoadPackageAsync(GameplayMapPackage,
		FLoadPackageAsyncDelegate::CreateUObject(this, &ThisClass::HandleGameplayPackagePreloadComplete, SubmittedEpoch));
	if (ActiveOperation == ECatOnlineOperation::Start && OperationEpoch == SubmittedEpoch
		&& GameplayPreloadRequestId == INDEX_NONE - 1)
	{
		GameplayPreloadRequestId = SubmittedRequestId;
		if (SubmittedRequestId == INDEX_NONE)
		{
			FinishOperationFailure(ECatOnlineError::GameplayPreloadFailed);
			return;
		}
		BroadcastSnapshot(TEXT("online_client_gameplay_preload_queued"));
	}
}

// 好友事实刷新流程：先让旧 opaque 句柄整体失效，再读取 OSS 已完成缓存；每个公开摘要只保留名称与 Presence，原始 FUniqueNetId 仅由私有映射持有以供邀请调用。
void UCatOnlineSubsystem::RefreshFriendSnapshotFacts()
{
	FriendsByHandle.Reset();
	FriendSummaries.Reset();
	if (!FriendsInterface.IsValid())
	{
		return;
	}

	TArray<TSharedRef<FOnlineFriend>> Friends;
	if (!FriendsInterface->GetFriendsList(0, EFriendsLists::ToString(EFriendsLists::Default), Friends))
	{
		return;
	}
	for (const TSharedRef<FOnlineFriend>& Friend : Friends)
	{
		const FGuid HandleValue = FGuid::NewGuid();
		const FOnlineUserPresence& Presence = Friend->GetPresence();
		FriendsByHandle.Add(HandleValue, Friend->GetUserId());
		FCatOnlineFriendSummary& Summary = FriendSummaries.AddDefaulted_GetRef();
		Summary.Handle.Value = HandleValue;
		Summary.DisplayName = Friend->GetDisplayName();
		Summary.bIsOnline = Presence.bIsOnline;
		Summary.bIsPlayingThisGame = Presence.bIsPlayingThisGame;
	}
}

// 好友读取完成流程：先以独立 Friends 代际拒绝迟到回调，再释放精确接口引用；成功时整体替换缓存，失败保留先前已确认数组并只发布结构化错误。
void UCatOnlineSubsystem::HandleReadFriendsListComplete(const int32 LocalUserNum, const bool bWasSuccessful,
	const FString& ListName, const FString& ErrorStr, const uint64 CallbackEpoch)
{
	(void)LocalUserNum;
	(void)ListName;
	(void)ErrorStr;
	if (!bFriendsRefreshPending || CallbackEpoch != FriendsRefreshEpoch)
	{
		UE_LOG(LogCatOnline, Warning, TEXT("Event=online_callback_ignored Callback=Friends Epoch=%llu CurrentEpoch=%llu"), CallbackEpoch, FriendsRefreshEpoch);
		return;
	}
	bFriendsRefreshPending = false;
	if (!bWasSuccessful)
	{
		FriendsInterface.Reset();
		LastError = ECatOnlineError::FriendsRefreshFailed;
		BroadcastSnapshot(TEXT("online_friends_refresh_failed"));
		return;
	}
	RefreshFriendSnapshotFacts();
	FriendsInterface.Reset();
	LastError = ECatOnlineError::None;
	BroadcastSnapshot(TEXT("online_friends_refreshed"));
}

// 好友刷新提交流程：只允许一个 Friends 回调悬挂，避免两个 OSS 缓存结果交错；接口可用时冻结 Friends 代际并提交 ReadFriendsList，最终数组只由完成回调写入。
FCatOnlineResult UCatOnlineSubsystem::RequestRefreshFriends()
{
	if (bFriendsRefreshPending)
	{
		return RejectRequest(ECatOnlineError::CommandAlreadyPending);
	}
	FriendsInterface = GetWorldFriendsInterface();
	if (!FriendsInterface.IsValid())
	{
		return RejectRequest(ECatOnlineError::FriendsInterfaceUnavailable);
	}

	FCatOnlineResult Result;
	Result.bAccepted = true;
	Result.RequestId = FGuid::NewGuid();
	const uint64 SubmittedEpoch = ++FriendsRefreshEpoch;
	bFriendsRefreshPending = true;
	const bool bQueued = FriendsInterface->ReadFriendsList(0, EFriendsLists::ToString(EFriendsLists::Default),
		FOnReadFriendsListComplete::CreateUObject(this, &ThisClass::HandleReadFriendsListComplete, SubmittedEpoch));
	if (!bQueued && bFriendsRefreshPending && FriendsRefreshEpoch == SubmittedEpoch)
	{
		bFriendsRefreshPending = false;
		Result.bAccepted = false;
		Result.Error = ECatOnlineError::FriendsRefreshFailed;
		LastError = Result.Error;
		BroadcastSnapshot(TEXT("online_friends_refresh_rejected"));
	}
	return Result;
}

// 好友邀请流程：先验证当前仍是前台 Host 房间和本代 opaque 句柄，再调用 OSS 的真实 Session Invite；平台接受后仅标记本代已发送，不把邀请发送误写成好友已加入或已接受。
FCatOnlineResult UCatOnlineSubsystem::RequestInviteFriend(const FCatOnlineFriendHandle FriendHandle)
{
	if (ActiveOperation != ECatOnlineOperation::None || WorldState != ECatOnlineWorldState::Frontend
		|| SessionRole != ECatOnlineSessionRole::Host || SessionState != ECatOnlineSessionState::Host)
	{
		return RejectRequest(ECatOnlineError::InvalidState);
	}
	const FUniqueNetIdPtr* FriendId = FriendsByHandle.Find(FriendHandle.Value);
	const FCatOnlineFriendSummary* FriendSummary = FriendSummaries.FindByPredicate([FriendHandle](const FCatOnlineFriendSummary& Summary)
	{
		return Summary.Handle.Value == FriendHandle.Value;
	});
	if (!FriendHandle.IsValid() || !FriendId || !FriendId->IsValid() || !FriendSummary || !FriendSummary->bIsOnline)
	{
		return RejectRequest(ECatOnlineError::InvalidHandle);
	}
	const IOnlineSessionPtr SessionInterface = GetWorldSessionInterface();
	if (!SessionInterface.IsValid() || !SessionInterface->GetNamedSession(CatOnlineNames::GameSession)
		|| !SessionInterface->SendSessionInviteToFriend(0, CatOnlineNames::GameSession, **FriendId))
	{
		return RejectRequest(ECatOnlineError::InviteFailed);
	}
	for (FCatOnlineFriendSummary& Summary : FriendSummaries)
	{
		if (Summary.Handle.Value == FriendHandle.Value)
		{
			Summary.bHasInvited = true;
			break;
		}
	}
	FCatOnlineResult Result;
	Result.bAccepted = true;
	Result.RequestId = FGuid::NewGuid();
	LastError = ECatOnlineError::None;
	BroadcastSnapshot(TEXT("online_friend_invite_sent"));
	return Result;
}

// 房主开始流程：先在 Frontend 验证 Host Session、无并发操作和 Save 已加载许可；再冻结操作 epoch 并提交玩法包预载，任何同步拒绝、失败回调或旧 epoch 都不会进入 ServerTravel。
FCatOnlineResult UCatOnlineSubsystem::RequestStartHostedGame()
{
	if (ActiveOperation != ECatOnlineOperation::None)
	{
		return RejectRequest(ECatOnlineError::CommandAlreadyPending);
	}
	if (WorldState != ECatOnlineWorldState::Frontend || SessionRole != ECatOnlineSessionRole::Host
		|| SessionState != ECatOnlineSessionState::Host || GameplayMapPackage.IsEmpty()
		|| GameplayPreloadRequestId != INDEX_NONE)
	{
		return RejectRequest(ECatOnlineError::InvalidState);
	}
	const UGameInstance* GameInstance = GetGameInstance();
	const UCatSaveSubsystem* SaveSubsystem = GameInstance ? GameInstance->GetSubsystem<UCatSaveSubsystem>() : nullptr;
	if (!SaveSubsystem || !SaveSubsystem->HasLoadedRunForTravel())
	{
		return RejectRequest(ECatOnlineError::SaveNotLoaded);
	}

	FCatOnlineResult Result = BeginOperation(ECatOnlineOperation::Start, ECatOnlineSessionState::Host);
	if (!Result.bAccepted)
	{
		return Result;
	}
	OperationRole = ECatOnlineSessionRole::Host;
	OperationSessionInterface = GetWorldSessionInterface();
	if (!OperationSessionInterface.IsValid() || !OperationSessionInterface->GetNamedSession(CatOnlineNames::GameSession))
	{
		FinishOperationFailure(GetSessionInterfaceError());
		Result.bAccepted = false;
		Result.Error = LastError;
		return Result;
	}

	const uint64 SubmittedEpoch = OperationEpoch;
	// -2 表示 LoadPackageAsync 尚未返回请求 ID；引擎若同步回调会先清成 INDEX_NONE，返回后不得把旧 ID 重新写回。
	GameplayPreloadRequestId = INDEX_NONE - 1;
	const int32 SubmittedRequestId = LoadPackageAsync(GameplayMapPackage,
		FLoadPackageAsyncDelegate::CreateUObject(this, &ThisClass::HandleGameplayPackagePreloadComplete, SubmittedEpoch));
	if (ActiveOperation == ECatOnlineOperation::Start && OperationEpoch == SubmittedEpoch
		&& GameplayPreloadRequestId == INDEX_NONE - 1)
	{
		GameplayPreloadRequestId = SubmittedRequestId;
		if (SubmittedRequestId == INDEX_NONE)
		{
			FinishOperationFailure(ECatOnlineError::GameplayPreloadFailed);
			Result.bAccepted = false;
			Result.Error = LastError;
			return Result;
		}
		BroadcastSnapshot(TEXT("online_gameplay_preload_queued"));
	}
	return Result;
}

// 预载进度读取流程：只在当前确有 LoadPackageAsync 请求时查询引擎；引擎返回 -1 表示未知，其他值夹在公开合同的 0..100 内，绝不合成时间驱动的百分比。
float UCatOnlineSubsystem::GetGameplayLoadProgress() const
{
	if (GameplayPreloadRequestId == INDEX_NONE || GameplayMapPackage.IsEmpty())
	{
		return -1.0f;
	}
	const float Progress = GetAsyncLoadPercentage(FName(*GameplayMapPackage));
	return Progress < 0.0f ? -1.0f : FMath::Clamp(Progress, 0.0f, 100.0f);
}

// 预载完成流程：先拒绝旧 epoch、错误包名或非 Start 回调；成功时保活实际包，Host 提交 Listen 旅行，Client 复核真实 Lobby ready 后才解析 OSS 地址并 ClientTravel，失败时不旅行。
void UCatOnlineSubsystem::HandleGameplayPackagePreloadComplete(const FName& PackageName, UPackage* LoadedPackage,
	const EAsyncLoadingResult::Type Result, const uint64 CallbackEpoch)
{
	if (ActiveOperation != ECatOnlineOperation::Start
		|| (OperationRole != ECatOnlineSessionRole::Host && OperationRole != ECatOnlineSessionRole::Client)
		|| CallbackEpoch != OperationEpoch || PackageName.ToString() != GameplayMapPackage)
	{
		UE_LOG(LogCatOnline, Warning, TEXT("Event=online_callback_ignored Callback=GameplayPreload Epoch=%llu CurrentEpoch=%llu"), CallbackEpoch, OperationEpoch);
		return;
	}
	GameplayPreloadRequestId = INDEX_NONE;
	if (Result != EAsyncLoadingResult::Succeeded || !LoadedPackage)
	{
		PreloadedGameplayPackage = nullptr;
		FinishOperationFailure(ECatOnlineError::GameplayPreloadFailed);
		return;
	}
	PreloadedGameplayPackage = LoadedPackage;
	if (OperationRole == ECatOnlineSessionRole::Host)
	{
		if (!BeginHostTravelToGameplayMap())
		{
			PreloadedGameplayPackage = nullptr;
			FinishOperationFailure(ECatOnlineError::TravelRejected);
		}
		return;
	}

	if (!IsCurrentLobbyReady())
	{
		PreloadedGameplayPackage = nullptr;
		FinishOperationFailure(ECatOnlineError::LobbyReadyPublishFailed);
		return;
	}
	OperationSessionInterface = GetWorldSessionInterface();
	FString ConnectString;
	if (!OperationSessionInterface.IsValid()
		|| !OperationSessionInterface->GetResolvedConnectString(CatOnlineNames::GameSession, ConnectString)
		|| ConnectString.IsEmpty())
	{
		PreloadedGameplayPackage = nullptr;
		FinishOperationFailure(ECatOnlineError::ConnectStringUnavailable);
		return;
	}
	if (!BeginClientTravelToGameplayMap(ConnectString))
	{
		FinishOperationFailure(ECatOnlineError::TravelRejected);
	}
}

// 操作开始流程：优先使用有效外部关联键，否则为本次 UI 意图生成 RequestId；若已有操作则保留旧操作的 RequestId/epoch，只返回独立同步拒绝。
// 受理后推进 epoch 并清除上一操作的载荷释放许可；Leave 在首个 pending 快照前统一废止搜索与邀请候选，确保主动离局、Host 通知和网络失败遵守同一代际边界。
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
	bReleaseActiveRunOnFrontend = false;
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

// 远端 Host exit 流程：先拒绝并发并验证 Lake Client 与服务器关联键；受理后不提交主动离局标记，保存 Destroy 后的 ACK 键并允许 Client 回前台后释放本机载荷，复用同一 Leave 状态机。
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
	bReleaseActiveRunOnFrontend = true;
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

// Create 流程：先以 CommandAlreadyPending 拒绝活动操作且不覆盖其关联键，再依次检查前台、SessionAccess 策略、World-aware Session 接口和兼容合同；创建平台设置时把公开容量对齐营地自动出生容量，最后绑定携带 epoch 的回调后提交平台请求。
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
	if (GameplayMapPackage.IsEmpty())
	{
		return RejectRequest(ECatOnlineError::InvalidState);
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
	// 会话公开容量必须与营地自动出生格数一致；否则第 5 名玩家能加入房间却只能在 GameMode 生成阶段被拒绝。
	SessionSettings.NumPublicConnections = CatGameplayPlayerLimits::MaxCampSpawnPlayers;
	SessionSettings.NumPrivateConnections = 0;
	SessionSettings.bAllowJoinInProgress = true;
	SessionSettings.bAllowInvites = true;
	SessionSettings.bUsesPresence = true;
	SessionSettings.bUseLobbiesIfAvailable = true;
	SessionSettings.bShouldAdvertise = Settings->SessionAccess != ECatSessionAccessPolicy::InviteOnly;
	SessionSettings.bAllowJoinViaPresence = Settings->SessionAccess == ECatSessionAccessPolicy::Public;
	SessionSettings.bAllowJoinViaPresenceFriendsOnly = Settings->SessionAccess == ECatSessionAccessPolicy::FriendsOnly;
	SessionSettings.Set(CatOnlineNames::MapSetting, GameplayMapPackage, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	SessionSettings.Set(CatOnlineNames::ProjectSetting, CatOnlineNames::ProjectId, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	SessionSettings.Set(CatOnlineNames::ProtocolSetting, CatOnlineNames::ProtocolVersion, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
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
	if (GameplayMapPackage.IsEmpty())
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
	ActiveSearch->QuerySettings.Set(SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);
	ActiveSearch->QuerySettings.Set(CatOnlineNames::ProjectSetting, CatOnlineNames::ProjectId, EOnlineComparisonOp::Equals);
	ActiveSearch->QuerySettings.Set(CatOnlineNames::ProtocolSetting, CatOnlineNames::ProtocolVersion, EOnlineComparisonOp::Equals);
	ActiveSearch->QuerySettings.Set(CatOnlineNames::MapSetting, GameplayMapPackage, EOnlineComparisonOp::Equals);
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

// 邀请 Join 流程：先拒绝并发及无效句柄，再复制平台结果并在进入 Join 前消费待提交意图；广播重入或下一次轮询不能再次提交同一邀请。Join 的成功、失败与回调 epoch 仍由统一管线处理。
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
	ClearPendingAcceptedInvite();
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

// Leave 流程：先拒绝并发并核对地图、角色与 Client 主动离局策略；受理时保留真实 Session。前台房间及 Client 可直接清理并在返回后释放本局载荷；Lake Host 不先获释放许可，必须等保存成功才 teardown 和 Destroy。
FCatOnlineResult UCatOnlineSubsystem::RequestLeave()
{
	if (ActiveOperation != ECatOnlineOperation::None)
	{
		return RejectRequest(ECatOnlineError::CommandAlreadyPending);
	}
	if ((WorldState != ECatOnlineWorldState::Lake && WorldState != ECatOnlineWorldState::Frontend)
		|| SessionRole == ECatOnlineSessionRole::None)
	{
		return RejectRequest(ECatOnlineError::InvalidState);
	}
	if (WorldState == ECatOnlineWorldState::Lake && SessionRole == ECatOnlineSessionRole::Client
		&& GetDefault<UCatOnlineSettings>()->VoluntaryLeaveRecovery == ECatPolicyDecision::Undecided)
	{
		return RejectRequest(ECatOnlineError::PolicyUndecided);
	}
	if (WorldState == ECatOnlineWorldState::Lake && SessionRole == ECatOnlineSessionRole::Client)
	{
		if (ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(GetWorld()->GetFirstPlayerController()))
		{
			Controller->ServerMarkVoluntaryLeave();
		}
	}

	FCatOnlineResult Result = BeginOperation(ECatOnlineOperation::Leave, SessionState);
	if (!Result.bAccepted)
	{
		return Result;
	}
	OperationRole = SessionRole;
	bReleaseActiveRunOnFrontend = WorldState == ECatOnlineWorldState::Frontend || SessionRole == ECatOnlineSessionRole::Client;
	const bool bLeaveSubmitted = OperationRole == ECatOnlineSessionRole::Host && WorldState == ECatOnlineWorldState::Lake
		? BeginHostLeaveSave()
		: BeginDestroySession(ECatOnlineError::None);
	if (!bLeaveSubmitted)
	{
		Result.bAccepted = false;
		Result.Error = LastError;
	}
	return Result;
}

// 离开保存流程：先确认 Lake Host 与 Save 来源，再绑定完成通知后发起活动世界保存；受理返回的 Save RequestId 与 Online epoch 配对。保存接口拒绝时立刻解绑结案，Session 和 Run 均未开始关闭。
bool UCatOnlineSubsystem::BeginHostLeaveSave()
{
	UCatSaveSubsystem* SaveSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCatSaveSubsystem>() : nullptr;
	if (!SaveSubsystem || ActiveOperation != ECatOnlineOperation::Leave
		|| OperationRole != ECatOnlineSessionRole::Host || WorldState != ECatOnlineWorldState::Lake)
	{
		FinishOperationFailure(ECatOnlineError::HostSaveFailed);
		return false;
	}
	ClearHostLeaveSaveDelegate();
	HostLeaveSaveSubsystem = SaveSubsystem;
	const uint64 SubmittedEpoch = OperationEpoch;
	const TWeakObjectPtr<UCatOnlineSubsystem> WeakThis(this);
	HostLeaveSaveHandle = SaveSubsystem->OnSaveCompleted.AddWeakLambda(this,
		[WeakThis, SubmittedEpoch](const FGuid SaveRequestId, const bool bSuccess)
		{
			// UE AsyncSaveGameToSlot 的序列化失败分支会同步回调；统一投递到游戏线程任务，等 RequestSaveActiveRun 返回关联键后再校验，避免丢失同步失败。
			AsyncTask(ENamedThreads::GameThread, [WeakThis, SaveRequestId, bSuccess, SubmittedEpoch]()
			{
				if (UCatOnlineSubsystem* Subsystem = WeakThis.Get())
				{
					Subsystem->HandleHostLeaveSaveCompleted(SaveRequestId, bSuccess, SubmittedEpoch);
				}
			});
		});
	const FCatSaveResult SaveResult = SaveSubsystem->RequestSaveActiveRun();
	if (ActiveOperation != ECatOnlineOperation::Leave || OperationEpoch != SubmittedEpoch)
	{
		return true;
	}
	if (!SaveResult.bAccepted || !SaveResult.RequestId.IsValid())
	{
		UE_LOG(LogCatOnline, Warning, TEXT("Event=online_host_leave_save_rejected RequestId=%s SaveRequestId=%s Epoch=%llu"),
			*ActiveRequestId.ToString(EGuidFormats::DigitsWithHyphens), *SaveResult.RequestId.ToString(EGuidFormats::DigitsWithHyphens), OperationEpoch);
		FinishOperationFailure(ECatOnlineError::HostSaveFailed);
		return false;
	}
	HostLeaveSaveRequestId = SaveResult.RequestId;
	UE_LOG(LogCatOnline, Log, TEXT("Event=online_host_leave_save_pending RequestId=%s SaveRequestId=%s Epoch=%llu"),
		*ActiveRequestId.ToString(EGuidFormats::DigitsWithHyphens), *HostLeaveSaveRequestId.ToString(EGuidFormats::DigitsWithHyphens), OperationEpoch);
	BroadcastSnapshot(TEXT("online_host_leave_waiting_save"));
	return true;
}

// 保存完成流程：按 Save RequestId、Online epoch、活动角色和订阅句柄拒绝旧通知；匹配后先解绑，失败只发布退出错误并保留 Session。最终持久化成功才授予回前台后的载荷释放许可并提交 Run teardown，沿用同一次 Leave 关联键。
void UCatOnlineSubsystem::HandleHostLeaveSaveCompleted(const FGuid SaveRequestId, const bool bSuccess, const uint64 CallbackEpoch)
{
	if (ActiveOperation != ECatOnlineOperation::Leave || OperationRole != ECatOnlineSessionRole::Host
		|| CallbackEpoch != OperationEpoch || !HostLeaveSaveHandle.IsValid() || SaveRequestId != HostLeaveSaveRequestId)
	{
		UE_LOG(LogCatOnline, Warning, TEXT("Event=online_callback_ignored Callback=HostLeaveSave SaveRequestId=%s Epoch=%llu CurrentEpoch=%llu"),
			*SaveRequestId.ToString(EGuidFormats::DigitsWithHyphens), CallbackEpoch, OperationEpoch);
		return;
	}
	ClearHostLeaveSaveDelegate();
	if (!bSuccess || WorldState != ECatOnlineWorldState::Lake)
	{
		UE_LOG(LogCatOnline, Warning, TEXT("Event=online_host_leave_save_failed RequestId=%s SaveRequestId=%s Epoch=%llu"),
			*ActiveRequestId.ToString(EGuidFormats::DigitsWithHyphens), *SaveRequestId.ToString(EGuidFormats::DigitsWithHyphens), OperationEpoch);
		FinishOperationFailure(ECatOnlineError::HostSaveFailed);
		return;
	}
	UE_LOG(LogCatOnline, Log, TEXT("Event=online_host_leave_save_completed RequestId=%s SaveRequestId=%s Epoch=%llu"),
		*ActiveRequestId.ToString(EGuidFormats::DigitsWithHyphens), *SaveRequestId.ToString(EGuidFormats::DigitsWithHyphens), OperationEpoch);
	bReleaseActiveRunOnFrontend = true;
	BeginHostRunTeardown();
}

// 保存订阅清理流程：只从提交时保留的精确 Save 实例移除当前句柄，再清空弱来源与关联键；已经排队的游戏线程回调还会通过失效句柄或 epoch 被拒绝。
void UCatOnlineSubsystem::ClearHostLeaveSaveDelegate()
{
	if (UCatSaveSubsystem* SaveSubsystem = HostLeaveSaveSubsystem.Get(); SaveSubsystem && HostLeaveSaveHandle.IsValid())
	{
		SaveSubsystem->OnSaveCompleted.Remove(HostLeaveSaveHandle);
	}
	HostLeaveSaveHandle.Reset();
	HostLeaveSaveSubsystem.Reset();
	HostLeaveSaveRequestId.Invalidate();
}

// 退出释放流程：只接受本次 Leave 已获许可、NamedSession 已清理且 World 确认 Frontend 的终态；保存失败、Create 失败及仍在路上的 World 均不能进入。
// Save busy 时订阅其精确实例并保留 Leave/epoch；变化回调投递游戏线程后复核代际，避免在 Save 自己的 OnChanged/OnSaveCompleted 广播栈中清载荷。
// 可释放时先解绑并消费许可，再调用会同步广播的 ReleaseActiveRun；返回后复核 epoch，最后发布原退出结果。服务缺失或拒绝明确报错，不伪造已释放。
void UCatOnlineSubsystem::FinishLeaveAfterRunRelease()
{
	if (ActiveOperation != ECatOnlineOperation::Leave || !bReleaseActiveRunOnFrontend
		|| WorldState != ECatOnlineWorldState::Frontend || SessionState != ECatOnlineSessionState::NoSession
		|| SessionRole != ECatOnlineSessionRole::None)
	{
		return;
	}
	UCatSaveSubsystem* SaveSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCatSaveSubsystem>() : nullptr;
	if (!SaveSubsystem)
	{
		bReleaseActiveRunOnFrontend = false;
		UE_LOG(LogCatOnline, Warning, TEXT("Event=online_run_release_failed RequestId=%s Epoch=%llu Reason=SaveUnavailable ReturnError=%s"),
			*ActiveRequestId.ToString(EGuidFormats::DigitsWithHyphens), OperationEpoch, *UEnum::GetValueAsString(DeferredFailureAfterTravel));
		FinishOperationFailure(ECatOnlineError::ActiveRunReleaseFailed);
		return;
	}
	if (SaveSubsystem->IsBusy())
	{
		if (!RunReleaseChangedHandle.IsValid())
		{
			RunReleaseSaveSubsystem = SaveSubsystem;
			const uint64 SubmittedEpoch = OperationEpoch;
			const TWeakObjectPtr<UCatOnlineSubsystem> WeakThis(this);
			RunReleaseChangedHandle = SaveSubsystem->OnChanged.AddWeakLambda(this, [WeakThis, SubmittedEpoch]()
			{
				AsyncTask(ENamedThreads::GameThread, [WeakThis, SubmittedEpoch]()
				{
					if (UCatOnlineSubsystem* Subsystem = WeakThis.Get(); Subsystem && Subsystem->OperationEpoch == SubmittedEpoch
						&& Subsystem->RunReleaseChangedHandle.IsValid())
					{
						Subsystem->FinishLeaveAfterRunRelease();
					}
				});
			});
			UE_LOG(LogCatOnline, Log, TEXT("Event=online_run_release_waiting RequestId=%s Epoch=%llu World=%s NetMode=%d Reason=SaveBusy"),
				*ActiveRequestId.ToString(EGuidFormats::DigitsWithHyphens), OperationEpoch, *GetNameSafe(GetWorld()),
				GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : -1);
			BroadcastSnapshot(TEXT("online_leave_waiting_run_release"));
		}
		return;
	}

	ClearRunReleaseDelegate();
	bReleaseActiveRunOnFrontend = false;
	const uint64 SubmittedEpoch = OperationEpoch;
	const bool bReleased = SaveSubsystem->ReleaseActiveRun();
	if (OperationEpoch != SubmittedEpoch || ActiveOperation != ECatOnlineOperation::Leave)
	{
		UE_LOG(LogCatOnline, Warning, TEXT("Event=online_callback_ignored Callback=RunRelease Epoch=%llu CurrentEpoch=%llu"),
			SubmittedEpoch, OperationEpoch);
		return;
	}
	if (!bReleased)
	{
		UE_LOG(LogCatOnline, Warning, TEXT("Event=online_run_release_failed RequestId=%s Epoch=%llu Reason=SaveRejected ReturnError=%s"),
			*ActiveRequestId.ToString(EGuidFormats::DigitsWithHyphens), OperationEpoch, *UEnum::GetValueAsString(DeferredFailureAfterTravel));
		FinishOperationFailure(ECatOnlineError::ActiveRunReleaseFailed);
		return;
	}
	UE_LOG(LogCatOnline, Log, TEXT("Event=online_run_release_completed RequestId=%s Epoch=%llu World=%s NetMode=%d"),
		*ActiveRequestId.ToString(EGuidFormats::DigitsWithHyphens), OperationEpoch, *GetNameSafe(GetWorld()),
		GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : -1);
	if (DeferredFailureAfterTravel != ECatOnlineError::None)
	{
		FinishOperationFailure(DeferredFailureAfterTravel);
	}
	else
	{
		FinishOperationSuccess();
	}
}

// 释放等待解绑流程：从保存的精确 Save 实例移除变化订阅，再清句柄和弱引用；已排队回调还必须通过 epoch 与有效句柄检查，不会影响下一次离房。
void UCatOnlineSubsystem::ClearRunReleaseDelegate()
{
	if (UCatSaveSubsystem* SaveSubsystem = RunReleaseSaveSubsystem.Get(); SaveSubsystem && RunReleaseChangedHandle.IsValid())
	{
		SaveSubsystem->OnChanged.Remove(RunReleaseChangedHandle);
	}
	RunReleaseChangedHandle.Reset();
	RunReleaseSaveSubsystem.Reset();
}

// Host Run 收口流程：保存成功后取得当前 authority GameMode 并在调用前绑定完成委托，再提交携带 Online RequestId/epoch 的 teardown；同步广播可能重入本子系统，因此返回后只在操作仍属于本代时解释直接结果。
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

// Snapshot 读取流程：逐字段复制四类事实和公开摘要，并从唯一待提交邀请派生等待标记；平台搜索对象、连接字符串、接受者账号与委托句柄均不会离开子系统。
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
	Snapshot.bIsAcceptedInvitePending = PendingAcceptedInvite.IsValid();
	Snapshot.Friends = FriendSummaries;
	Snapshot.RoomMembers = RoomMembers;
	Snapshot.RoomName = CurrentRoomName;
	Snapshot.SessionAccess = CurrentSessionAccess;
	Snapshot.LobbyId = CurrentLobbyId;
	if (!CurrentLobbyId.IsEmpty() && !CurrentLobbyOwnerId.IsEmpty())
	{
#if WITH_STEAMWORKS
		if (SteamUtils())
		{
			Snapshot.JoinLobbyUri = FString::Printf(TEXT("steam://joinlobby/%u/%s/%s"),
				SteamUtils()->GetAppID(), *CurrentLobbyId, *CurrentLobbyOwnerId);
		}
#endif
	}
	Snapshot.MaxPlayers = RoomMaxPlayers;
	Snapshot.CurrentPlayers = RoomCurrentPlayers;
	Snapshot.bIsHost = SessionRole == ECatOnlineSessionRole::Host;
	Snapshot.bIsGameplayLoadPending = GameplayPreloadRequestId != INDEX_NONE;
	Snapshot.GameplayLoadProgress = GetGameplayLoadProgress();
	return Snapshot;
}

// Destroy 提交流程：补偿开始先废止所有大厅候选；再在当前 World 精确取得 Session 接口，NamedSession 已不存在时把清理视为幂等完成。
// 平台调用返回后复核提交时的 operation、epoch 与 Destroy 句柄，避免同步完成已经旅行或结案后再次广播 queued/失败。
bool UCatOnlineSubsystem::BeginDestroySession(const ECatOnlineError FailureAfterDestroy)
{
	DeferredFailureAfterDestroy = FailureAfterDestroy;
	StopLobbyFactPolling();
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
			if (WorldState == ECatOnlineWorldState::Lake)
			{
				// Session 已被平台提前移除时仍可能停在 listen 地图；复用 Host 回前台链路，避免把无可加入房间的 Host 留在玩法地图。
				DeferredFailureAfterDestroy = ECatOnlineError::None;
				DeferredFailureAfterTravel = FailureAfterDestroy;
				if (!BeginTravelToFrontend())
				{
					FinishOperationFailure(FailureAfterDestroy);
				}
			}
			else
			{
				FinishOperationFailure(FailureAfterDestroy);
			}
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

// Host 旅行流程：只在已完成预载的 Host Start 内执行；此时绝不提前发布可连接信号，只提交 GameplayMap?listen，目标 World 已生成 GameNetDriver 后由 PostLoadMap 写 Steam Lobby ready。
bool UCatOnlineSubsystem::BeginHostTravelToGameplayMap()
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || GameplayMapPackage.IsEmpty()
		|| ActiveOperation != ECatOnlineOperation::Start || OperationRole != ECatOnlineSessionRole::Host
		|| !OperationSessionInterface.IsValid())
	{
		return false;
	}
	FNamedOnlineSession* NamedSession = OperationSessionInterface->GetNamedSession(CatOnlineNames::GameSession);
	if (!NamedSession)
	{
		return false;
	}
	if (!World->ServerTravel(GameplayMapPackage + TEXT("?listen"), false))
	{
		return false;
	}
	ExpectedPackage = GameplayMapPackage;
	WorldState = ECatOnlineWorldState::TravelingToLake;
	TransportState = ECatOnlineTransportState::TravelQueued;
	BroadcastSnapshot(TEXT("online_host_travel_queued"));
	return true;
}

// Client 旅行流程：Client 已预载成功、复核 Lobby ready 并解析真实地址后临时取得本地控制器调用 ClientTravel；不保存 Controller 引用，PostLoadMap 才发布到达终态。
bool UCatOnlineSubsystem::BeginClientTravelToGameplayMap(const FString& ConnectString)
{
	APlayerController* PlayerController = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController() : nullptr;
	if (!PlayerController || ConnectString.IsEmpty() || GameplayMapPackage.IsEmpty())
	{
		return false;
	}
	PlayerController->ClientTravel(ConnectString, TRAVEL_Absolute);
	ExpectedPackage = GameplayMapPackage;
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

// Create 回调流程：拒绝名称、操作或 epoch 不匹配的迟到事件；成功后确立 Host Lobby，再恢复 Settings 的语音发送偏好，随后建立真实房间快照并留在 Frontend。Steam 已在完成回执前注册本地 talker，必须在本轮 Voice Tick 前纠正其默认开启行为。
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
	if (UCatGameUserSettings* Settings = UCatGameUserSettings::Get())
	{
		Settings->RestoreVoiceChatForLocalPlayers(GetWorld());
	}
	RefreshRoomSnapshotFacts();
	StartLobbyFactPolling();
	FinishOperationSuccess();
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

// Join 回调流程：只消费当前 Join epoch；成功后确立 Client Lobby 并立即让 Settings 恢复已保存的语音发送偏好，覆盖 Steam 本地 talker 注册的默认开启。随后废止候选并启动真实数据轮询，留在 Frontend 等 Host ready，绝不因 Join 成功自行旅行。
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
	// 同步完成先消费回调句柄；失败可保留搜索候选，但平台确认邀请已在提交前消费，必须重新接受才会再次加入。
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
	if (UCatGameUserSettings* Settings = UCatGameUserSettings::Get())
	{
		Settings->RestoreVoiceChatForLocalPlayers(GetWorld());
	}
	SearchResultsByHandle.Reset();
	SearchSummaries.Reset();
	InvitesByHandle.Reset();
	InviteSummaries.Reset();
	RefreshRoomSnapshotFacts();
	StartLobbyFactPolling();
	FinishOperationSuccess();
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
		if (WorldState == ECatOnlineWorldState::Lake)
		{
			// 补偿链已经成功清掉 Session 但仍停在玩法图时，继续走 Host 回前台链路；否则只结案会把无房间的玩家滞留在 Lake。
			DeferredFailureAfterDestroy = ECatOnlineError::None;
			DeferredFailureAfterTravel = Failure;
			if (!BeginTravelToFrontend())
			{
				FinishOperationFailure(Failure);
			}
		}
		else
		{
			FinishOperationFailure(Failure);
		}
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

// 邀请接受流程：先核对真实平台结果、单本地玩家账号与房间兼容性，再拒绝忙或已有会话，绝不替用户离房。
// 可受理时只保存一个带固定期限和操作代际的意图并广播；下一次生命周期检查会等 Frontend/身份就绪后自动复用 RequestAcceptInvite，不再等待不存在的确认页。
void UCatOnlineSubsystem::HandleSessionUserInviteAccepted(const bool bWasSuccessful, const int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& InviteResult)
{
	ECatOnlineError Error = ECatOnlineError::None;
	const IOnlineSessionPtr Sessions = GetWorldSessionInterface();
	if (!bWasSuccessful || ControllerId != 0 || !UserId.IsValid() || !UserId->IsValid() || !InviteResult.IsValid())
	{
		Error = ECatOnlineError::InviteAcceptanceUnavailable;
	}
	else if (!HasCompatibleSessionSettings(InviteResult.Session.SessionSettings))
	{
		Error = ECatOnlineError::SessionCompatibilityMismatch;
	}
	else if (ActiveOperation != ECatOnlineOperation::None || PendingAcceptedInvite.IsValid())
	{
		Error = ECatOnlineError::InviteAcceptanceBusy;
	}
	else if (SessionState != ECatOnlineSessionState::NoSession || SessionRole != ECatOnlineSessionRole::None
		|| (Sessions.IsValid() && Sessions->GetNamedSession(CatOnlineNames::GameSession)))
	{
		Error = ECatOnlineError::InviteSessionConflict;
	}
	if (Error != ECatOnlineError::None)
	{
		const FCatOnlineResult Rejected = RejectRequest(Error);
		UE_LOG(LogCatOnline, Warning, TEXT("Event=online_invite_rejected RequestId=%s Epoch=%llu Error=%s"),
			*Rejected.RequestId.ToString(EGuidFormats::DigitsWithHyphens), OperationEpoch, *UEnum::GetValueAsString(Error));
		return;
	}

	const FGuid HandleValue = FGuid::NewGuid();
	PendingAcceptedInvite.Value = HandleValue;
	PendingInviteUserId = MoveTemp(UserId);
	PendingInviteDeadline = FPlatformTime::Seconds() + CatOnlineNames::AcceptedInviteWaitSeconds;
	PendingInviteOperationEpoch = OperationEpoch;
	InvitesByHandle.Add(HandleValue, InviteResult);
	FCatSessionInviteSummary& Summary = InviteSummaries.AddDefaulted_GetRef();
	Summary.Handle.Value = HandleValue;
	Summary.OwnerDisplayName = InviteResult.Session.OwningUserName;
	LastError = ECatOnlineError::None;
	UE_LOG(LogCatOnline, Log, TEXT("Event=online_invite_pending InviteId=%s Epoch=%llu WaitSeconds=%.0f"),
		*HandleValue.ToString(EGuidFormats::DigitsWithHyphens), OperationEpoch, CatOnlineNames::AcceptedInviteWaitSeconds);
	BroadcastSnapshot(TEXT("online_invite_accepted"));
}

// 邀请检查流程：维护当前 World 的唯一订阅；没有意图时立即返回，不输出稳定轮询日志。
// 有意图时先拒绝并发、被其他操作取代或已有 Session，再检查固定期限、Frontend、本地玩家及 Steam 登录账号；暂未就绪只等待，账号不符或地图不允许则终止。
// 全部就绪才调用既有 RequestAcceptInvite；它会在 Join 广播前消费句柄。同步拒绝和异步结果均通过原快照通知回显，不另建 Join 或旅行状态机。
bool UCatOnlineSubsystem::TickPlatformInvites(float DeltaSeconds)
{
	(void)DeltaSeconds;
	RebindInviteDelegate();
	if (!PendingAcceptedInvite.IsValid())
	{
		return true;
	}
	const FCatSessionInviteHandle InviteHandle = PendingAcceptedInvite;
	ECatOnlineError Error = ECatOnlineError::None;
	const IOnlineSessionPtr Sessions = GetWorldSessionInterface();
	UWorld* World = GetWorld();
	UGameInstance* GameInstance = GetGameInstance();
	ULocalPlayer* Player = GameInstance ? GameInstance->GetFirstGamePlayer() : nullptr;
	IOnlineSubsystem* Subsystem = World ? Online::GetSubsystem(World) : nullptr;
	const IOnlineIdentityPtr Identity = Subsystem ? Subsystem->GetIdentityInterface() : nullptr;
	const FUniqueNetIdPtr LocalUserId = Identity.IsValid() ? Identity->GetUniquePlayerId(0) : nullptr;
	if (ActiveOperation != ECatOnlineOperation::None || PendingInviteOperationEpoch != OperationEpoch)
	{
		Error = ECatOnlineError::InviteAcceptanceBusy;
	}
	else if (SessionState != ECatOnlineSessionState::NoSession || SessionRole != ECatOnlineSessionRole::None
		|| (Sessions.IsValid() && Sessions->GetNamedSession(CatOnlineNames::GameSession)))
	{
		Error = ECatOnlineError::InviteSessionConflict;
	}
	else if (FPlatformTime::Seconds() >= PendingInviteDeadline)
	{
		Error = ECatOnlineError::InviteAcceptanceExpired;
	}
	else if ((WorldState != ECatOnlineWorldState::Unknown && WorldState != ECatOnlineWorldState::Frontend)
		|| (Player && Player->GetControllerId() != 0))
	{
		Error = ECatOnlineError::InviteAcceptanceUnavailable;
	}
	else if (!World || WorldState != ECatOnlineWorldState::Frontend || !Sessions.IsValid()
		|| !Player || !Player->GetPlayerController(World) || !Identity.IsValid()
		|| Identity->GetLoginStatus(0) != ELoginStatus::LoggedIn || !LocalUserId.IsValid() || !LocalUserId->IsValid())
	{
		return true;
	}
	else if (!PendingInviteUserId.IsValid() || *LocalUserId != *PendingInviteUserId)
	{
		Error = ECatOnlineError::InviteAcceptanceUnavailable;
	}
	if (Error != ECatOnlineError::None)
	{
		ClearPendingAcceptedInvite();
		const FCatOnlineResult Rejected = RejectRequest(Error);
		UE_LOG(LogCatOnline, Warning, TEXT("Event=online_invite_failed InviteId=%s RequestId=%s Epoch=%llu Error=%s"),
			*InviteHandle.Value.ToString(EGuidFormats::DigitsWithHyphens), *Rejected.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			OperationEpoch, *UEnum::GetValueAsString(Error));
		return true;
	}
	const FCatOnlineResult Result = RequestAcceptInvite(InviteHandle);
	UE_LOG(LogCatOnline, Log, TEXT("Event=online_invite_join_submitted InviteId=%s RequestId=%s Epoch=%llu Accepted=%d Error=%s"),
		*InviteHandle.Value.ToString(EGuidFormats::DigitsWithHyphens), *Result.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		OperationEpoch, Result.bAccepted, *UEnum::GetValueAsString(Result.Error));
	return true;
}

// 意图清理流程：先删除该意图对应的 opaque 结果与摘要，再清空接受账号、固定期限和冻结代际；不触碰已受理的 Join 委托，也不解绑长期平台邀请订阅。
void UCatOnlineSubsystem::ClearPendingAcceptedInvite()
{
	const FGuid HandleValue = PendingAcceptedInvite.Value;
	InvitesByHandle.Remove(HandleValue);
	InviteSummaries.RemoveAll([HandleValue](const FCatSessionInviteSummary& Summary) { return Summary.Handle.Value == HandleValue; });
	PendingAcceptedInvite.Value.Invalidate();
	PendingInviteUserId.Reset();
	PendingInviteDeadline = 0.0;
	PendingInviteOperationEpoch = 0;
}

// 地图完成流程：先隔离空 World 和其他 GameInstance，再重绑邀请接口；来源包回载时保留 pending 等 TravelFailure，意外包进入补偿。
// 命中 ExpectedPackage 后确认 World 与 Transport；Host 到达玩法图后即收口 Start，ready 缺失只影响 Client 准入并写入 Host 低频重试截止点，真正的 TravelFailure、NetworkFailure 和 Leave 仍由各自入口回前台。
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
		const bool bReturnedToSource = (ExpectedPackage == GameplayMapPackage && PackageName == CatOnlineNames::Frontend)
			|| (ExpectedPackage == CatOnlineNames::Frontend && PackageName == GameplayMapPackage);
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
			if ((ActiveOperation == ECatOnlineOperation::Create || ActiveOperation == ECatOnlineOperation::Join || ActiveOperation == ECatOnlineOperation::Start)
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
		if (ActiveOperation == ECatOnlineOperation::Start && OperationRole == ECatOnlineSessionRole::Host
			&& WorldState == ECatOnlineWorldState::Lake)
		{
			RefreshRoomSnapshotFacts();
			if (IsHostGameplayWorldReadyForClientAdmission())
			{
				if (!PublishLobbyReady())
				{
					NextHostLobbyReadyPublishAttemptTime = FPlatformTime::Seconds() + CatOnlineNames::HostLobbyReadyRetrySeconds;
					BroadcastSnapshot(TEXT("online_lobby_ready_publish_unavailable"));
				}
				else
				{
					NextHostLobbyReadyPublishAttemptTime = 0.0;
				}
			}
			else
			{
				NextHostLobbyReadyPublishAttemptTime = FPlatformTime::Seconds() + CatOnlineNames::HostLobbyReadyRetrySeconds;
				BroadcastSnapshot(TEXT("online_lobby_ready_withheld"));
			}
		}
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

// 旅行失败流程：先按 GameInstance 过滤并记录来源 World；Client Start 仍在 Frontend 时保留 Lobby 进入有界重试，重复失败通知不再消耗预算；其他已建会话走 Destroy 补偿，Leave 不触发第二次旅行。
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

	if (SessionRole == ECatOnlineSessionRole::Client && SessionState == ECatOnlineSessionState::Client
		&& WorldState == ECatOnlineWorldState::Frontend && ClientGameplayStartAttempts > 0
		&& (ActiveOperation == ECatOnlineOperation::Start || ActiveOperation == ECatOnlineOperation::None))
	{
		if (ActiveOperation == ECatOnlineOperation::Start)
		{
			FinishOperationFailure(ECatOnlineError::TravelFailed);
		}
		return;
	}
	if ((ActiveOperation == ECatOnlineOperation::Create || ActiveOperation == ECatOnlineOperation::Join || ActiveOperation == ECatOnlineOperation::Start)
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
// 前台 Client 的 PendingNetDriver 连接失败只结束本次 Start 并退避；引擎负责释放失败驱动，本地 Lobby 保留供下一次尝试，耗尽预算后由用户显式离开。
// 空闲时的断线复用 Leave：Lake Host 仍须先通过持久化回执，失败不自行 Destroy；前台房间和 Client 可清理，完成返回后再释放本机载荷。
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

	if (bIsCurrentPendingDriver && SessionRole == ECatOnlineSessionRole::Client
		&& SessionState == ECatOnlineSessionState::Client && ClientGameplayStartAttempts > 0
		&& (ActiveOperation == ECatOnlineOperation::Start || ActiveOperation == ECatOnlineOperation::None))
	{
		const UWorld* CurrentWorld = GetWorld();
		const FString CurrentPackage = CurrentWorld ? UWorld::StripPIEPrefixFromPackageName(
			CurrentWorld->GetPackage()->GetName(), CurrentWorld->StreamingLevelsPrefix) : FString();
		if (CurrentPackage == CatOnlineNames::Frontend)
		{
			SetWorldStateForPackage(CurrentPackage);
			if (ActiveOperation == ECatOnlineOperation::Start)
			{
				FinishOperationFailure(ECatOnlineError::NetworkFailure);
			}
			return;
		}
	}
	if (ActiveOperation == ECatOnlineOperation::Create || ActiveOperation == ECatOnlineOperation::Join || ActiveOperation == ECatOnlineOperation::Start)
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

	FCatOnlineResult CleanupResult = BeginOperation(ECatOnlineOperation::Leave, SessionState);
	if (!CleanupResult.bAccepted)
	{
		return;
	}
	OperationRole = SessionRole;
	DeferredFailureAfterTravel = ECatOnlineError::NetworkFailure;
	bReleaseActiveRunOnFrontend = WorldState == ECatOnlineWorldState::Frontend || SessionRole == ECatOnlineSessionRole::Client;
	if (SessionRole != ECatOnlineSessionRole::None)
	{
		if (SessionRole == ECatOnlineSessionRole::Host && WorldState == ECatOnlineWorldState::Lake)
		{
			BeginHostLeaveSave();
		}
		else
		{
			BeginDestroySession(ECatOnlineError::None);
		}
	}
	else
	{
		bReleaseActiveRunOnFrontend = false;
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
	if (!GameplayMapPackage.IsEmpty() && PackageName == GameplayMapPackage)
	{
		WorldState = ECatOnlineWorldState::Lake;
		return true;
	}
	WorldState = ECatOnlineWorldState::Error;
	return false;
}

// 成功结案流程：获准释放的 Leave 先确认无会话且回到 Frontend，再交给释放收口；busy 时不提前宣告退出完成。
// 释放已完成或无需释放时，撤销许可并解绑两类 Save、Run 与平台回调，清操作和预载引用、推进 epoch，最后广播成功；不改真实 Session/World/Transport。
void UCatOnlineSubsystem::FinishOperationSuccess()
{
	if (ActiveOperation == ECatOnlineOperation::Leave && bReleaseActiveRunOnFrontend
		&& WorldState == ECatOnlineWorldState::Frontend && SessionState == ECatOnlineSessionState::NoSession
		&& SessionRole == ECatOnlineSessionRole::None)
	{
		FinishLeaveAfterRunRelease();
		return;
	}
	const bool bFinishingGameplayStart = ActiveOperation == ECatOnlineOperation::Start;
	bReleaseActiveRunOnFrontend = false;
	ClearRunReleaseDelegate();
	ClearHostLeaveSaveDelegate();
	ClearRunTeardownDelegate();
	ClearOperationDelegates();
	ActiveSearch.Reset();
	ExpectedPackage.Reset();
	ActiveOperation = ECatOnlineOperation::None;
	OperationRole = ECatOnlineSessionRole::None;
	DeferredFailureAfterDestroy = ECatOnlineError::None;
	DeferredFailureAfterTravel = ECatOnlineError::None;
	PendingHostExitAckRequestId.Invalidate();
	if (bFinishingGameplayStart)
	{
		GameplayPreloadRequestId = INDEX_NONE;
		PreloadedGameplayPackage = nullptr;
	}
	LastError = ECatOnlineError::None;
	++OperationEpoch;
	BroadcastSnapshot(TEXT("online_operation_succeeded"));
}

// 失败结案流程：若 Leave 已安全清会话并回 Frontend 且获释放许可，保留原错误并等待载荷释放；保存失败没有许可，Destroy/返回失败未达到终态，均不清载荷。
// 其他情况撤销释放许可并解绑所有等待，清操作和预载、废止 epoch；前台 Client Start 前两次按 2 秒、4 秒退避，第三次明确停止，其余失败保持原错误。
void UCatOnlineSubsystem::FinishOperationFailure(const ECatOnlineError Error)
{
	if (ActiveOperation == ECatOnlineOperation::Leave && bReleaseActiveRunOnFrontend
		&& WorldState == ECatOnlineWorldState::Frontend && SessionState == ECatOnlineSessionState::NoSession
		&& SessionRole == ECatOnlineSessionRole::None)
	{
		DeferredFailureAfterTravel = Error;
		FinishLeaveAfterRunRelease();
		return;
	}
	const bool bFinishingGameplayStart = ActiveOperation == ECatOnlineOperation::Start;
	const bool bRetryableClientStart = bFinishingGameplayStart && OperationRole == ECatOnlineSessionRole::Client
		&& SessionState == ECatOnlineSessionState::Client && WorldState == ECatOnlineWorldState::Frontend;
	bReleaseActiveRunOnFrontend = false;
	ClearRunReleaseDelegate();
	ClearHostLeaveSaveDelegate();
	ClearRunTeardownDelegate();
	ClearOperationDelegates();
	ActiveSearch.Reset();
	ExpectedPackage.Reset();
	ActiveOperation = ECatOnlineOperation::None;
	OperationRole = ECatOnlineSessionRole::None;
	DeferredFailureAfterDestroy = ECatOnlineError::None;
	DeferredFailureAfterTravel = ECatOnlineError::None;
	PendingHostExitAckRequestId.Invalidate();
	if (bFinishingGameplayStart)
	{
		GameplayPreloadRequestId = INDEX_NONE;
		PreloadedGameplayPackage = nullptr;
	}
	LastError = Error;
	if (bRetryableClientStart)
	{
		if (ClientGameplayStartAttempts >= CatOnlineNames::MaxClientGameplayStartAttempts)
		{
			LastError = ECatOnlineError::ClientStartRetryExhausted;
			UE_LOG(LogCatOnline, Warning, TEXT("Event=online_client_start_retry_exhausted RequestId=%s Epoch=%llu Attempts=%d Cause=%s Recovery=LeaveAndRejoin"),
				*ActiveRequestId.ToString(EGuidFormats::DigitsWithHyphens), OperationEpoch, ClientGameplayStartAttempts, *UEnum::GetValueAsString(Error));
		}
		else
		{
			const double RetryDelaySeconds = 2.0 * ClientGameplayStartAttempts;
			NextClientGameplayStartTime = FPlatformTime::Seconds() + RetryDelaySeconds;
			UE_LOG(LogCatOnline, Warning, TEXT("Event=online_client_start_retry_scheduled RequestId=%s Epoch=%llu Attempts=%d DelaySeconds=%.1f Cause=%s"),
				*ActiveRequestId.ToString(EGuidFormats::DigitsWithHyphens), OperationEpoch, ClientGameplayStartAttempts, RetryDelaySeconds, *UEnum::GetValueAsString(Error));
		}
	}
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

// 邀请重绑流程：先查询当前 World 的真实接口；与现有订阅一致时不动委托，避免低频检查反复解绑。接口变化才移除旧订阅并绑定新接口；暂缺接口保持空，等待后续地图或生命周期检查恢复。
void UCatOnlineSubsystem::RebindInviteDelegate()
{
	const IOnlineSessionPtr CurrentSessions = GetWorldSessionInterface();
	if (CurrentSessions == InviteSessionInterface && InviteAcceptedHandle.IsValid())
	{
		return;
	}
	ClearInviteDelegate();
	InviteSessionInterface = CurrentSessions;
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
	RefreshRoomSnapshotFacts();
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

// 兼容合同检查流程：会话必须使用 Presence Lobby，并携带本项目、协议、地图标识和当前营地出生容量；AppId 480 的其他开发房间或旧 8 人房间不会进入公开句柄映射、Join 或邀请接受。
bool UCatOnlineSubsystem::HasCompatibleSessionSettings(const FOnlineSessionSettings& Settings) const
{
	FString ProjectId;
	FString ProtocolVersion;
	FString MapName;
	return Settings.bUsesPresence
		&& Settings.bUseLobbiesIfAvailable
		&& Settings.NumPublicConnections == CatGameplayPlayerLimits::MaxCampSpawnPlayers
		&& Settings.Get(CatOnlineNames::ProjectSetting, ProjectId)
		&& ProjectId == CatOnlineNames::ProjectId
		&& Settings.Get(CatOnlineNames::ProtocolSetting, ProtocolVersion)
		&& ProtocolVersion == CatOnlineNames::ProtocolVersion
		&& Settings.Get(CatOnlineNames::MapSetting, MapName)
		&& !GameplayMapPackage.IsEmpty()
		&& MapName == GameplayMapPackage;
}
