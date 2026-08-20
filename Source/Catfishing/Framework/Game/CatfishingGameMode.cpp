#include "Framework/Game/CatfishingGameMode.h"

#include "Framework/Core/CatDomainCommandTypes.h"
#include "Framework/Core/CatStableNetId.h"
#include "Framework/Game/CatfishingGameState.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"

#include "Character/CatCharacter.h"
#include "Collection/CatRunImprintService.h"
#include "Components/StateTreeComponent.h"
#include "Condition/CatConditionComponent.h"
#include "Data/CatDataCatalogValidation.h"
#include "Data/CatFishCatalogSettings.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Environment/CatChumSpotSubsystem.h"
#include "Environment/CatConfiguredEnvironmentProvider.h"
#include "Environment/CatEnvironmentSettings.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Equipment/CatTeamEquipmentLibrary.h"
#include "Fishing/CatFishingService.h"
#include "GameFramework/GameSession.h"
#include "Items/CatItemsService.h"
#include "Logging/CatLog.h"
#include "Online/CatOnlineSettings.h"
#include "Online/CatOnlineSubsystem.h"
#include "OnlineSubsystemTypes.h"
#include "Run/CatRunSettings.h"
#include "Run/CatRunStateTreeEvents.h"
#include "Run/CatSacrificeCoordinator.h"
#include "ShopEconomy/CatShopEconomyService.h"
#include "Social/CatSocialService.h"
#include "StateTree.h"
#include "TimerManager.h"

namespace
{
	/** PIE 无会话身份的 UE 类型标签；服务器只在受限开发准入中创建，客户端提交同类型身份会被拒绝。 */
	const FName CatPieNoSessionUniqueIdType(TEXT("CAT_PIE_NOSESSION"));

	// 临时身份识别流程：先要求 FUniqueNetIdRepl 有效，再只比较服务器保留的类型标签；不会根据字符串前缀接受客户端伪造值。
	bool IsPieNoSessionUniqueId(const FUniqueNetIdRepl& UniqueId)
	{
		return UniqueId.IsValid() && UniqueId->GetType() == CatPieNoSessionUniqueIdType;
	}
}

// 构造流程：一次性指定 Lake 四类框架对象和默认角色；同时创建唯一 StateTree 组件与中性 Environment provider，并关闭组
// 件自动启动，所有 Run 真相仍留在 GameMode 实例。
ACatfishingGameModeBase::ACatfishingGameModeBase()
{
	DefaultPawnClass = ACatCharacter::StaticClass();
	PlayerControllerClass = ACatfishingPlayerController::StaticClass();
	GameStateClass = ACatfishingGameState::StaticClass();
	PlayerStateClass = ACatfishingPlayerState::StaticClass();
	RunStateTreeComponent = CreateDefaultSubobject<UStateTreeComponent>(TEXT("RunStateTree"));
	RunStateTreeComponent->SetStartLogicAutomatically(false);
	EnvironmentProvider = CreateDefaultSubobject<UCatConfiguredEnvironmentProvider>(TEXT("EnvironmentProvider"));
}

// 启动流程：先执行引擎玩法启动并建立 NotStarted 快照，再校验 authority、正式运行数值、Data 内容包、环境接口与
// ST_RunFlow 资产；全部满足才启动唯一 StateTree。
void ACatfishingGameModeBase::StartPlay()
{
	Super::StartPlay();
	RunPublicState = FCatRunPublicState();
	RunPublicState.Phase.RunId = FGuid::NewGuid();
	RunPublicState.Phase.ServerTimeAnchorSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	RunPublicState.Revision = 1;
	RefreshEnvironmentAndPublish();

	// 团队经济与团队装备库的复制投递从这里开始接线。订阅放在启动 gate 之前，
	// 是为了不留下"服务已经可写、但还没人在听"的窗口：那段时间里提交的流水不会有人重建快照，
	// 客户端账本从此少一条，之后没有任何自校正机会。
	if (UWorld* SubscriptionWorld = GetWorld())
	{
		if (UCatShopEconomyService* Shop = SubscriptionWorld->GetSubsystem<UCatShopEconomyService>())
		{
			ShopPublicTransactionHandle = Shop->OnPublicTransactionCommitted.AddWeakLambda(this,
				[this](const FCatShopPublicTransaction&) { PublishShopEconomySnapshot(); });
		}
		if (UCatTeamEquipmentLibrary* Library = SubscriptionWorld->GetSubsystem<UCatTeamEquipmentLibrary>())
		{
			TeamEquipmentLibraryHandle = Library->OnLibraryChanged.AddUObject(this,
				&ThisClass::PublishTeamEquipmentLibrarySnapshot);
		}
	}
	PublishShopEconomySnapshot();
	PublishTeamEquipmentLibrarySnapshot();

	const FCatDataCatalogValidationReport DataCatalogReport = FCatDataCatalogValidator::ValidateRuntimeCatalogs(
		GetDefault<UCatFishCatalogSettings>(), GetDefault<UCatEquipmentSettings>());
	if (!DataCatalogReport.bValid)
	{
		UE_LOG(LogCatRun, Error, TEXT("Event=data_catalog_invalid ContentHash=%s Issues=%d"),
			*DataCatalogReport.ContentHashHex, DataCatalogReport.Issues.Num());
		FailRunStartup(TEXT("DataCatalogInvalid"));
		return;
	}

	const UCatRunSettings* Settings = GetDefault<UCatRunSettings>();
	float DayLengthSeconds = 0.0f;
	int32 QuotaTarget = 0;
	if (!HasAuthority() || !Settings || !Settings->TryGetDayParameters(DayLengthSeconds, QuotaTarget)
		|| !RunStateTreeComponent || !Cast<ICatEnvironmentProvider>(EnvironmentProvider))
	{
		FailRunStartup(TEXT("PrototypeGateOrDependencyUnavailable"));
		return;
	}

	UStateTree* RunFlowAsset = Settings->RunFlowStateTree.LoadSynchronous();
	if (!RunFlowAsset)
	{
		FailRunStartup(TEXT("StateTreeAssetUnavailable"));
		return;
	}

	RunPublicState.QuotaTarget = QuotaTarget;
	bRunCommandsOpen = true;
	RunStateTreeComponent->SetStateTree(RunFlowAsset);
	bRunStartupInProgress = true;
	RunStateTreeComponent->StartLogic();
	bRunStartupInProgress = false;
	if (!RunStateTreeComponent->IsRunning() && RunPublicState.Phase.Phase == ECatRunPhase::NotStarted)
	{
		FailRunStartup(TEXT("StateTreeStartFailed"));
		return;
	}
	UE_LOG(LogCatRun, Log, TEXT("Event=run_started RunId=%s Revision=%lld StateTree=%s"),
		*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Revision, *RunFlowAsset->GetName());
}

// World 收口流程：先关闭新 Run 命令并清白天截止与时段分界两个计时器，再清 HostExit ACK 与上一次发布的环境事件记号；
// 随后清空本局全部窝点并停 StateTree，最后交父类，使迟到 Task/ACK 不能进入新 World。
void ACatfishingGameModeBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bRunCommandsOpen = false;
	ClearDayDeadline();
	GetWorldTimerManager().ClearTimer(TimeOfDayBoundaryTimerHandle);
	TimeOfDayBoundaryTimerHandle.Invalidate();
	GetWorldTimerManager().ClearTimer(HostExitAckTimerHandle);
	HostExitAckTimerHandle.Invalidate();
	PendingHostExitAckStableNetIds.Reset();
	LastPublishedEnvironmentEventId = NAME_None;
	if (UWorld* World = GetWorld())
	{
		if (UCatChumSpotSubsystem* ChumSpots = World->GetSubsystem<UCatChumSpotSubsystem>())
		{
			ChumSpots->ResetRuntimeChumFromAuthority();
		}
		// 与 StartPlay 的订阅成对解除：不解除的话，上一局经济服务的回调会带着已经失效的 GameMode 进入下一张地图。
		if (UCatShopEconomyService* Shop = World->GetSubsystem<UCatShopEconomyService>())
		{
			Shop->OnPublicTransactionCommitted.Remove(ShopPublicTransactionHandle);
		}
		if (UCatTeamEquipmentLibrary* Library = World->GetSubsystem<UCatTeamEquipmentLibrary>())
		{
			Library->OnLibraryChanged.Remove(TeamEquipmentLibraryHandle);
		}
	}
	ShopPublicTransactionHandle.Reset();
	TeamEquipmentLibraryHandle.Reset();
	if (RunStateTreeComponent && RunStateTreeComponent->IsRunning())
	{
		RunStateTreeComponent->StopLogic(TEXT("GameMode EndPlay"));
	}
	Super::EndPlay(EndPlayReason);
}

// PreLogin 流程：先保留引擎 GameSession/UniqueId 兼容检查；客户端提交保留的 PIE 类型始终拒绝。远端无身份只在 Editor
// PIE 无会话 gate 下继续等待服务器于 InitNewPlayer 分配身份，其余路径仍按 StableNetId 建立 Reserved 或拒绝重复占用。
void ACatfishingGameModeBase::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (!ErrorMessage.IsEmpty())
	{
		return;
	}
	if (IsPieNoSessionUniqueId(UniqueId))
	{
		ErrorMessage = TEXT("CAT_PIE_ID_MUST_BE_SERVER_GENERATED");
		UE_LOG(LogCatOnline, Warning, TEXT("Event=identity_prelogin_rejected StableNetId=Valid(Redacted) Address=%s Error=ClientSuppliedPieIdentity"), *Address);
		return;
	}
	if (!UniqueId.IsValid())
	{
		if (IsPieNoSessionAdmissionAllowed())
		{
			UE_LOG(LogCatOnline, Log, TEXT("Event=identity_prelogin_development_allowed StableNetId=Invalid Address=%s Source=PieNoSession"), *Address);
			return;
		}
		ErrorMessage = TEXT("CAT_POLICY_UNDECIDED:InvalidStableNetIdAdmission");
		UE_LOG(LogCatOnline, Warning, TEXT("Event=identity_prelogin_rejected StableNetId=Invalid Address=%s Error=PolicyUndecided"), *Address);
		return;
	}

	const FString StableNetIdKey = CatResolveStableNetId(UniqueId);
	bool bReconnectCandidate = false;
	if (double* Expiry = ReconnectExpiryByStableNetId.Find(StableNetIdKey))
	{
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		if (Now <= *Expiry)
		{
			bReconnectCandidate = true;
		}
		else if (GetDefault<UCatOnlineSettings>()->ExpiredAdmission == ECatPolicyDecision::Enabled)
		{
			ReconnectExpiryByStableNetId.Remove(StableNetIdKey);
		}
		else
		{
			ErrorMessage = TEXT("CAT_POLICY_UNDECIDED:ExpiredReconnectAdmission");
			return;
		}
	}
	if (AdmissionRecords.Contains(StableNetIdKey))
	{
		ErrorMessage = TEXT("CAT_DUPLICATE_STABLE_NET_ID");
		UE_LOG(LogCatOnline, Warning, TEXT("Event=identity_prelogin_rejected StableNetId=%s Address=%s Error=DuplicateOnlineIdentity"),
			*MakeStableNetIdLogValue(UniqueId), *Address);
		return;
	}
	if (bReconnectCandidate)
	{
		PendingReconnectStableNetIds.Add(StableNetIdKey);
	}
	AdmissionRecords.Add(StableNetIdKey);
	UE_LOG(LogCatOnline, Log, TEXT("Event=identity_reserved StableNetId=%s Records=%d"), *MakeStableNetIdLogValue(UniqueId), AdmissionRecords.Num());
}

// 玩家初始化流程：
// 1. 先拒绝任何调用方传入的保留 PIE 身份，保证临时 GUID 只能由当前 authority World 生成。
// 2. 无有效身份时仅在 Editor PIE、NoSession 且无 Online 操作的服务器生成一次临时身份，再把有效身份交给父类写入
// PlayerState；其他环境原样保留引擎行为。
// 3. 父类成功后，远端正式玩家继续消费 PreLogin 已写的 Reserved；只有本地 Controller 或本次生成的临时身份在缺记录时补 Reserved。
// 4. 任何父类失败或重复占用都返回错误，不留下新的准入记录；PostLogin 仍负责唯一的 Reserved→Active 提升与 Character 生成。
FString ACatfishingGameModeBase::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId,
	const FString& Options, const FString& Portal)
{
	if (IsPieNoSessionUniqueId(UniqueId))
	{
		return TEXT("CAT_PIE_ID_MUST_BE_SERVER_GENERATED");
	}

	FUniqueNetIdRepl EffectiveUniqueId = UniqueId;
	bool bGeneratedPieIdentity = false;
	if (!EffectiveUniqueId.IsValid() && IsPieNoSessionAdmissionAllowed())
	{
		const FString GeneratedValue = FString::Printf(TEXT("PIE-%s"),
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		const FUniqueNetIdRef GeneratedUniqueId = FUniqueNetIdString::Create(GeneratedValue, CatPieNoSessionUniqueIdType);
		EffectiveUniqueId = FUniqueNetIdRepl(GeneratedUniqueId);
		bGeneratedPieIdentity = true;
	}

	const FString ErrorMessage = Super::InitNewPlayer(NewPlayerController, EffectiveUniqueId, Options, Portal);
	if (!ErrorMessage.IsEmpty())
	{
		return ErrorMessage;
	}

	const APlayerState* PlayerState = NewPlayerController ? NewPlayerController->PlayerState : nullptr;
	if (!PlayerState || !PlayerState->GetUniqueId().IsValid())
	{
		return ErrorMessage;
	}

	const bool bLocalControllerNeedsReservation = NewPlayerController->IsLocalController();
	if (!bGeneratedPieIdentity && !bLocalControllerNeedsReservation)
	{
		return ErrorMessage;
	}

	const FString StableNetIdKey = CatResolveStableNetId(PlayerState->GetUniqueId());
	if (AdmissionRecords.Contains(StableNetIdKey))
	{
		return TEXT("CAT_DUPLICATE_STABLE_NET_ID");
	}

	AdmissionRecords.Add(StableNetIdKey);
	UE_LOG(LogCatOnline, Log, TEXT("Event=identity_reserved StableNetId=%s Records=%d Source=%s"),
		*MakeStableNetIdLogValue(PlayerState->GetUniqueId()), AdmissionRecords.Num(),
		bGeneratedPieIdentity ? TEXT("PieNoSession") : TEXT("LocalController"));
	return ErrorMessage;
}

// PostLogin 流程：父类尚未调用 HandleStartingNewPlayer 时先读取 PlayerState 继承 UniqueId；只有命中 Reserved 才提升
// Active 并绑定 Controller，随后才进入父类生成/占有 Character。
void ACatfishingGameModeBase::PostLogin(APlayerController* NewPlayer)
{
	ACatfishingPlayerState* PlayerState = NewPlayer ? NewPlayer->GetPlayerState<ACatfishingPlayerState>() : nullptr;
	if (!PlayerState || !PlayerState->GetUniqueId().IsValid())
	{
		RejectPostLoginController(NewPlayer, TEXT("CAT_POLICY_UNDECIDED:MissingStableNetId"));
		return;
	}
	const FString StableNetIdKey = CatResolveStableNetId(PlayerState->GetUniqueId());
	FAdmissionRecord* Record = AdmissionRecords.Find(StableNetIdKey);
	if (!Record || Record->Phase != EAdmissionPhase::Reserved || Record->Controller.IsValid())
	{
		RejectPostLoginController(NewPlayer, TEXT("CAT_IDENTITY_RESERVATION_MISMATCH"));
		return;
	}

	Record->Phase = EAdmissionPhase::Active;
	Record->Controller = NewPlayer;
	const bool bWasReconnect = PendingReconnectStableNetIds.Remove(StableNetIdKey) > 0;
	if (bWasReconnect)
	{
		ReconnectExpiryByStableNetId.Remove(StableNetIdKey);
	}
	UE_LOG(LogCatOnline, Log, TEXT("Event=identity_activated StableNetId=%s PlayerState=%s Controller=%s"),
		*MakeStableNetIdLogValue(PlayerState->GetUniqueId()), *PlayerState->GetClass()->GetName(), *NewPlayer->GetClass()->GetName());

	// 会话成员关系只有服务器建立得起来：Online 拿它裁决房主资格、加入顺序和踢人合法性，本身不监听登录。
	// 房主判定用的是"这个 Controller 是不是跑在服务器进程上"——Lake 是 listen server，本机 Controller 就是开房的那个人。
	const bool bIsSessionHost = NewPlayer->IsLocalController();
	if (UGameInstance* OwningGameInstance = GetGameInstance())
	{
		if (UCatOnlineSubsystem* Online = OwningGameInstance->GetSubsystem<UCatOnlineSubsystem>())
		{
			Online->RegisterSessionMember(StableNetIdKey, bIsSessionHost);
		}
	}
	if (bIsSessionHost)
	{
		// 局主身份要同时登记给 Social：不登记的话没有人能改运行期偷取/恶作剧权限，
		// 开关会一直停在 ini 默认值，房主想中途关掉偷鱼也关不掉。
		if (UCatSocialService* Social = GetWorld() ? GetWorld()->GetSubsystem<UCatSocialService>() : nullptr)
		{
			Social->SetHostAuthority(NewPlayer);
			if (ACatfishingGameState* CatGameState = GetGameState<ACatfishingGameState>())
			{
				CatGameState->SetSocialPolicyFromAuthority(Social->GetSocialPolicy());
			}
		}
	}
	Super::PostLogin(NewPlayer);
	if (ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(NewPlayer))
	{
		CatController->ClientRefreshPublicFishCollection();
	}
	if (UCatRunImprintService* ImprintService = GetWorld() ? GetWorld()->GetSubsystem<UCatRunImprintService>() : nullptr)
	{
		ImprintService->DeliverPendingForController(NewPlayer);
	}
	if (RunPublicState.Phase.Phase == ECatRunPhase::NormalNight && GetDefault<UCatRunSettings>()->CanAdmitLateNightReady())
	{
		PlayerState->SetNextDayReadyFromAuthority(false);
		NightReadyEligibleIds.Add(StableNetIdKey);
		EvaluateAllEligibleReady();
	}
	UE_LOG(LogCatfishing, Log, TEXT("Event=lake_postlogin_complete Controller=%s Pawn=%s"),
		*NewPlayer->GetClass()->GetName(), NewPlayer->GetPawn() ? *NewPlayer->GetPawn()->GetClass()->GetName() : TEXT("None"));
}

// 生成前复核流程：在引擎 RestartPlayer 之前再次验证 StableNetId 对应 Active Controller；不匹配时保持无 Pawn，匹配时才调用父类生成与占有。
void ACatfishingGameModeBase::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	if (!IsControllerActive(NewPlayer))
	{
		UE_LOG(LogCatOnline, Error, TEXT("Event=identity_spawn_blocked Controller=%s Error=ControllerRecordMismatch"),
			NewPlayer ? *NewPlayer->GetName() : TEXT("None"));
		return;
	}
	UE_LOG(LogCatOnline, Log, TEXT("Event=identity_before_character Controller=%s Result=Active"), *NewPlayer->GetName());
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}

// Logout 流程：先读取尚未被引擎清理的 PlayerState；只有 StableNetId 命中且 Active 弱引用等于 Exiting 才移除，旧连接永
// 远不能删除同身份的新记录。PIE 无会话身份随连接立即失效并显式跳过重连 TTL，避免下一次测试误恢复旧玩家。
void ACatfishingGameModeBase::Logout(AController* Exiting)
{
	const APlayerState* PlayerState = Exiting ? Exiting->PlayerState : nullptr;
	FString ReleasedStableNetId;
	if (PlayerState && PlayerState->GetUniqueId().IsValid())
	{
		const FString StableNetIdKey = CatResolveStableNetId(PlayerState->GetUniqueId());
		const bool bPieNoSessionIdentity = IsPieNoSessionUniqueId(PlayerState->GetUniqueId());
		const FAdmissionRecord* Record = AdmissionRecords.Find(StableNetIdKey);
		if (Record && Record->Phase == EAdmissionPhase::Active && Record->Controller.Get() == Exiting)
		{
			AdmissionRecords.Remove(StableNetIdKey);
			ReleasedStableNetId = StableNetIdKey;
			const UCatOnlineSettings* OnlineSettings = GetDefault<UCatOnlineSettings>();
			const bool bVoluntary = VoluntaryLeaveStableNetIds.Remove(StableNetIdKey) > 0;
			const bool bKeepVoluntary = bVoluntary
				&& OnlineSettings->VoluntaryLeaveRecovery == ECatPolicyDecision::Enabled;
			// 被房主踢出的人也是从这条路走的：引擎眼里踢人和掉线都只是一次 Logout。
			// 不在这里拦一道，下面就会照常给他写一条 60 秒重连准入，他马上就能再连进来，踢人等于没发生。
			const bool bKicked = KickedStableNetIds.Contains(StableNetIdKey);
			if (!bPieNoSessionIdentity && !bKicked && OnlineSettings->IsReconnectAdmissionReady()
				&& (!bVoluntary || bKeepVoluntary) && GetWorld())
			{
				ReconnectExpiryByStableNetId.Add(StableNetIdKey,
					GetWorld()->GetTimeSeconds() + OnlineSettings->ReconnectRecordTtlSeconds);
			}
			else if (bPieNoSessionIdentity || bKicked)
			{
				// 连历史遗留的准入记录一并抹掉：被踢之前掉过一次线的人，否则还留着上一条没过期的窗口。
				ReconnectExpiryByStableNetId.Remove(StableNetIdKey);
			}
			// 会话成员关系与准入记录同生共死；房主踢人时 Online 已经先移除过一次，这里重复调用是幂等的。
			if (UGameInstance* OwningGameInstance = GetGameInstance())
			{
				if (UCatOnlineSubsystem* Online = OwningGameInstance->GetSubsystem<UCatOnlineSubsystem>())
				{
					Online->UnregisterSessionMember(StableNetIdKey);
				}
			}
			UE_LOG(LogCatOnline, Log, TEXT("Event=identity_released StableNetId=%s Result=ControllerMatched Remaining=%d Recovery=%s"),
				*MakeStableNetIdLogValue(PlayerState->GetUniqueId()), AdmissionRecords.Num(),
				bKicked ? TEXT("SuppressedHostKick")
					: (bPieNoSessionIdentity ? TEXT("SkippedPieNoSession")
						: (ReconnectExpiryByStableNetId.Contains(StableNetIdKey) ? TEXT("ReconnectWindowRecorded")
							: TEXT("NoReconnectWindow"))));
		}
		else
		{
			UE_LOG(LogCatOnline, Warning, TEXT("Event=identity_release_ignored StableNetId=%s Error=ControllerMismatch"),
				*MakeStableNetIdLogValue(PlayerState->GetUniqueId()));
		}
	}
	if (!ReleasedStableNetId.IsEmpty())
	{
		NightReadyEligibleIds.Remove(ReleasedStableNetId);
		NightReadyIds.Remove(ReleasedStableNetId);
		EvaluateAllEligibleReady();
	}
	Super::Logout(Exiting);
}

// 主动离局标记流程：只接受当前 Active Controller，读取继承 UniqueId 后写入短生命周期集合；Logout 精确消费，旧连接不能标记新占用。
void ACatfishingGameModeBase::MarkVoluntaryLeave(AController* Controller)
{
	if (!IsControllerActive(Controller))
	{
		return;
	}
	const APlayerState* CurrentPlayerState = Controller ? Controller->PlayerState : nullptr;
	if (CurrentPlayerState && CurrentPlayerState->GetUniqueId().IsValid())
	{
		VoluntaryLeaveStableNetIds.Add(CatResolveStableNetId(CurrentPlayerState->GetUniqueId()));
	}
}

// StableNetId 日志流程：只有显式 Enabled 才输出原始平台值；Disabled 与 Undecided 均输出脱敏状态，避免静态默认值形成隐私裁决。
FString ACatfishingGameModeBase::MakeStableNetIdLogValue(const FUniqueNetIdRepl& UniqueId)
{
	if (!UniqueId.IsValid())
	{
		return TEXT("Invalid");
	}
	return GetDefault<UCatOnlineSettings>()->StableNetIdExposure == ECatPolicyDecision::Enabled
		? UniqueId->ToString()
		: TEXT("Valid(Redacted)");
}

// Active 匹配流程：从 Controller 的 PlayerState 读取引擎唯一身份，再检查服务器记录阶段与弱引用；不通过名字、地址或 Pawn 反推身份。
bool ACatfishingGameModeBase::IsControllerActive(const AController* Controller) const
{
	const APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
	if (!PlayerState || !PlayerState->GetUniqueId().IsValid())
	{
		return false;
	}
	const FAdmissionRecord* Record = AdmissionRecords.Find(CatResolveStableNetId(PlayerState->GetUniqueId()));
	return Record && Record->Phase == EAdmissionPhase::Active && Record->Controller.Get() == Controller;
}

// 开发准入 gate 流程：
// 1. 非 Editor 编译直接返回 false，使打包 Game/Server 不包含可启用的匿名准入路径。
// 2. Editor 中只接受真实 PIE World 与三种服务器 NetMode，客户端 World 和普通 Editor World 均拒绝。
// 3. 最后读取 GameInstance 的 Online 唯一快照；只有没有 NamedSession、没有会话角色且没有活动操作时才允许服务器生成临时身份。
bool ACatfishingGameModeBase::IsPieNoSessionAdmissionAllowed() const
{
#if WITH_EDITOR
	const UWorld* World = GetWorld();
	if (!World || World->WorldType != EWorldType::PIE)
	{
		return false;
	}

	const ENetMode NetMode = World->GetNetMode();
	if (NetMode != NM_Standalone && NetMode != NM_ListenServer && NetMode != NM_DedicatedServer)
	{
		return false;
	}

	const UGameInstance* GameInstance = World->GetGameInstance();
	const UCatOnlineSubsystem* Online = GameInstance ? GameInstance->GetSubsystem<UCatOnlineSubsystem>() : nullptr;
	if (!Online)
	{
		return false;
	}

	const FCatOnlineSnapshot Snapshot = Online->GetSnapshot();
	return Snapshot.SessionState == ECatOnlineSessionState::NoSession
		&& Snapshot.SessionRole == ECatOnlineSessionRole::None
		&& Snapshot.ActiveOperation == ECatOnlineOperation::None;
#else
	return false;
#endif
}

// 玩法命令 gate 流程：要求 authority、本局命令门仍开放且 Controller 精确命中 Active 身份记录；
// Profile/Capture/Settlement/HostExit 等收口协议走各自校验，不调用本方法。
bool ACatfishingGameModeBase::CanAcceptGameplayCommand(const AController* Controller) const
{
	return HasAuthority() && bRunCommandsOpen && IsControllerActive(Controller);
}

// 房主踢人流程：先确认请求者自己还是本局的 Active 玩家，再把"他是不是房主、目标是不是成员"交给 Online 裁决；
// 裁决通过之后先写重连压制标记，再走 GameSession 断开。
// 这个顺序不能反：KickPlayer 会同步触发 Logout，标记必须在那之前就位，否则 Logout 照常给被踢者写 60 秒重连准入，人马上就能回来。
// NightReady 集合的清理由 Logout 既有路径完成，这里不重复处理。
ECatOnlineError ACatfishingGameModeBase::RequestHostKick(AController* RequesterController, const FString& TargetStableNetId)
{
	const APlayerState* RequesterPlayerState = RequesterController ? RequesterController->PlayerState : nullptr;
	if (!HasAuthority() || !IsControllerActive(RequesterController) || !GameSession || !RequesterPlayerState)
	{
		return ECatOnlineError::InvalidState;
	}
	// 请求者身份只从他自己的 PlayerState 重建；客户端载荷里唯一能指定的是"踢谁"。
	const FString RequesterStableNetId = CatResolveStableNetId(RequesterPlayerState->GetUniqueId());
	UGameInstance* OwningGameInstance = GetGameInstance();
	UCatOnlineSubsystem* Online = OwningGameInstance ? OwningGameInstance->GetSubsystem<UCatOnlineSubsystem>() : nullptr;
	if (!Online || RequesterStableNetId.IsEmpty() || TargetStableNetId.IsEmpty())
	{
		return ECatOnlineError::InvalidState;
	}
	const ECatOnlineError KickError = Online->CommitHostKick(RequesterStableNetId, TargetStableNetId);
	if (KickError != ECatOnlineError::None)
	{
		return KickError;
	}

	KickedStableNetIds.Add(TargetStableNetId);
	const FAdmissionRecord* TargetRecord = AdmissionRecords.Find(TargetStableNetId);
	APlayerController* TargetController = (TargetRecord && TargetRecord->Phase == EAdmissionPhase::Active)
		? Cast<APlayerController>(TargetRecord->Controller.Get())
		: nullptr;
	if (!TargetController)
	{
		// 目标已经不在场：成员关系已经裁掉、重连压制标记也已写下，这次踢人的效果本身是完整的，
		// 不需要再去断一条不存在的连接。
		UE_LOG(LogCatOnline, Log, TEXT("Event=host_kick_committed Target=Absent Records=%d"), AdmissionRecords.Num());
		return ECatOnlineError::None;
	}
	GameSession->KickPlayer(TargetController, NSLOCTEXT("Catfishing", "HostKickReason", "Removed from the session by the host."));
	UE_LOG(LogCatOnline, Log, TEXT("Event=host_kick_committed Target=Active Records=%d"), AdmissionRecords.Num());
	return ECatOnlineError::None;
}

// PostLogin 拒绝流程：优先让 GameSession 执行标准 Kick；GameSession 不可用时通知客户端回主菜单。该分支不调用父类生成
// Character，也不删除无法安全匹配到本 Controller 的 Reserved 记录，避免替未裁 TTL/过期准入策略作决定。
void ACatfishingGameModeBase::RejectPostLoginController(APlayerController* NewPlayer, const FString& Reason)
{
	UE_LOG(LogCatOnline, Error, TEXT("Event=identity_postlogin_rejected Controller=%s Reason=%s"),
		NewPlayer ? *NewPlayer->GetName() : TEXT("None"), *Reason);
	if (!NewPlayer)
	{
		return;
	}
	if (GameSession)
	{
		GameSession->KickPlayer(NewPlayer, FText::FromString(Reason));
	}
	else
	{
		NewPlayer->ClientReturnToMainMenuWithTextReason(FText::FromString(Reason));
	}
}

// 命令身份适配流程：只接受已激活且仍与记录弱引用匹配的 Controller，再从继承 PlayerState UniqueId 写入服务器上下文；客户端无法提供或覆盖 StableNetId。
bool ACatfishingGameModeBase::FillServerCommandIdentity(const AController* Controller, FCatRunCommandContext& Context) const
{
	if (!IsControllerActive(Controller))
	{
		Context.StableNetId.Reset();
		return false;
	}
	const APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
	Context.StableNetId = PlayerState ? CatResolveStableNetId(PlayerState->GetUniqueId()) : FString();
	return !Context.StableNetId.IsEmpty();
}

// 幂等键生成流程：把服务器身份、稳定命令类别和随机 RequestId 组合为 World 内私有键；键不进入复制、日志或持久化。
FString ACatfishingGameModeBase::MakeRunCommandCacheKey(const FString& StableNetId, const ECatRunCommandType CommandType, const FGuid& RequestId)
{
	return FString::Printf(TEXT("%s|%d|%s"), *StableNetId, static_cast<int32>(CommandType),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
}

// 结果构造流程：从当前聚合读取 Revision/Phase，并只写本次 RequestId、提交事实、错误与转移原因；不会推进 StateTree 或修改缓存。
FCatRunCommandResult ACatfishingGameModeBase::MakeRunCommandResult(const FGuid& RequestId, const bool bCommitted,
	const ECatRunCommandError Error, const ECatRunTransitionReason Reason) const
{
	FCatRunCommandResult Result;
	Result.bCommitted = bCommitted;
	Result.RequestId = RequestId;
	Result.Error = Error;
	Result.Revision = RunPublicState.Revision;
	Result.Phase = RunPublicState.Phase.Phase;
	Result.TransitionReason = Reason;
	return Result;
}

// 终态重放流程：命中缓存后先比较首次业务载荷；只有同载荷重试才返回 AlreadyResolved，载荷漂移会作为新的非法命令暴露。
bool ACatfishingGameModeBase::TryReplayRunCommand(const FString& CacheKey, const FString& PayloadSignature,
	const FGuid& RequestId, FCatRunCommandResult& OutResult) const
{
	// 判定走共享模板，不再在这里手抄一份。以前这份副本把"签名查不到"写成了静默重放（谓词是 CachedPayload && ...），
	// 与模板的 !CachedPayload || ... 正好相反：那意味着一旦某条路径只写了终态、漏写了签名，重试方会拿到一个
	// 签名从未被校验过的旧结果，而模板的做法是立刻判成载荷冲突、把问题暴露出来。Run 命令用的是自己的错误枚举，
	// 所以重放终态头的改写由这里的 lambda 指明，模板不假设结果结构。
	const ECatTerminalReplayOutcome Outcome = CatQueryTerminalReplay(RunCommandTerminalCache, RunCommandPayloadByKey,
		CacheKey, PayloadSignature, OutResult,
		[](FCatRunCommandResult& Replayed)
		{
			Replayed.bCommitted = false;
			Replayed.Error = ECatRunCommandError::AlreadyResolved;
		});
	if (Outcome == ECatTerminalReplayOutcome::FirstAttempt)
	{
		return false;
	}
	if (Outcome == ECatTerminalReplayOutcome::PayloadMismatch)
	{
		OutResult = MakeRunCommandResult(RequestId, false, ECatRunCommandError::InvalidPayload);
	}
	return true;
}

// 终态保存流程：只在不存在时写入首次结果与载荷签名；同键重复保存也必须先验证载荷，避免把不同业务事实折叠成重放。
FCatRunCommandResult ACatfishingGameModeBase::CacheRunCommandResult(const FString& CacheKey,
	const FString& PayloadSignature, const FCatRunCommandResult& Result)
{
	// 与 TryReplayRunCommand 用同一个模板判定，保证"读"和"写前复查"两侧对载荷冲突的口径不会各自漂移——
	// 这里原本也是一份手抄，且同样把签名缺失写成了静默重放。
	FCatRunCommandResult Replay;
	const ECatTerminalReplayOutcome Outcome = CatQueryTerminalReplay(RunCommandTerminalCache, RunCommandPayloadByKey,
		CacheKey, PayloadSignature, Replay,
		[](FCatRunCommandResult& Replayed)
		{
			Replayed.bCommitted = false;
			Replayed.Error = ECatRunCommandError::AlreadyResolved;
		});
	if (Outcome == ECatTerminalReplayOutcome::PayloadMismatch)
	{
		return MakeRunCommandResult(Result.RequestId, false, ECatRunCommandError::InvalidPayload);
	}
	if (Outcome == ECatTerminalReplayOutcome::Replayed)
	{
		return Replay;
	}
	RunCommandTerminalCache.Add(CacheKey, Result);
	RunCommandPayloadByKey.Add(CacheKey, PayloadSignature);
	return Result;
}

// 阶段进入流程：先要求 authority、有效 Run 与正在启动/运行的唯一 StateTree，并在写状态前拒绝未裁/未到最终天数的成功结
// 算或非法白天参数。通过后统一清掉旧白天截止并复位玩法开关：DayActive 递增天数、清当日额度与终局原因、开启钓鱼、执行
// 清晨副作用并建立唯一 one-shot timer；NormalNight 打开献祭额度写口并冻结当前 ready 资格；两种 settlement 写对应终局
// 原因并清 ready 集合；Ending/Ended/NotStarted 关闭新命令。最后只递增一次 Revision、保存 StateTree 可读结果，并按
// Phase 刷新环境后发布 GameState 组合快照；C++ 始终不选择下一条转移边。
// 白天不开献祭窗口、夜晚不建计时器是飞书「局与进程册」的硬约束：白天是限时弧线，献祭和结算都发生在夜里。
FCatRunTransitionResult ACatfishingGameModeBase::EnterRunPhaseFromStateTree(const ECatRunPhase NewPhase, const ECatRunTransitionReason Reason)
{
	FCatRunTransitionResult Result;
	Result.PreviousPhase = RunPublicState.Phase.Phase;
	Result.CurrentPhase = RunPublicState.Phase.Phase;
	Result.Reason = Reason;
	Result.Revision = RunPublicState.Revision;
	bool bCanAcceptStateTreePhase = RunStateTreeComponent
		&& (RunStateTreeComponent->IsRunning() || bRunStartupInProgress);
#if WITH_DEV_AUTOMATION_TESTS
	bCanAcceptStateTreePhase = bCanAcceptStateTreePhase || bAutomationRunPhaseEntryBypass;
#endif
	if (!HasAuthority() || !RunPublicState.Phase.RunId.IsValid() || !bCanAcceptStateTreePhase)
	{
		Result.Error = ECatRunCommandError::StateTreeUnavailable;
		LastRunFlowResult = Result;
		return Result;
	}
	const UCatRunSettings* Settings = GetDefault<UCatRunSettings>();
	if (NewPhase == ECatRunPhase::SuccessSettlementNight
		&& (!Settings || !Settings->CanEnterSuccessSettlementNight(RunPublicState.Phase.DayIndex)))
	{
		Result.Error = ECatRunCommandError::PolicyUndecided;
		LastRunFlowResult = Result;
		return Result;
	}
	float DayLengthSeconds = 0.0f;
	int32 DayQuotaTarget = 0;
	if (NewPhase == ECatRunPhase::DayActive
		&& (!Settings || !Settings->TryGetDayParameters(DayLengthSeconds, DayQuotaTarget)))
	{
		Result.Error = ECatRunCommandError::PolicyUndecided;
		LastRunFlowResult = Result;
		return Result;
	}

	ClearDayDeadline();
	RunPublicState.Phase.Phase = NewPhase;
	RunPublicState.Phase.ServerTimeAnchorSeconds = GetWorld()->GetTimeSeconds();
	RunPublicState.Phase.bFishingAllowed = false;
	RunPublicState.Phase.bQuotaOpen = false;
	RunPublicState.bTeardownComplete = false;

	switch (NewPhase)
	{
	case ECatRunPhase::DayActive:
	{
		++RunPublicState.Phase.DayIndex;
		// 只清当日额度：世界进度是跨天累计层，跟着清会让森林复苏演出和鱼册剪影解锁每天从头再来。
		RunPublicState.QuotaProgress = 0;
		RunPublicState.QuotaTarget = DayQuotaTarget;
		RunPublicState.EndReason = ECatRunEndReason::None;
		RunPublicState.Phase.bFishingAllowed = true;
		RunPublicState.Phase.bHasDeadline = true;
		RunPublicState.Phase.DeadlineServerTimeSeconds = RunPublicState.Phase.ServerTimeAnchorSeconds + DayLengthSeconds;
		bRunCommandsOpen = true;
		NightReadyEligibleIds.Reset();
		NightReadyIds.Reset();
		bAllEligibleReadyEventSent = false;
		ApplyDayBreakSideEffects();
		GetWorld()->GetTimerManager().SetTimer(DayDeadlineTimerHandle, this,
			&ThisClass::HandleDayDeadlineElapsed, DayLengthSeconds, false);
		break;
	}
	case ECatRunPhase::NormalNight:
		bRunCommandsOpen = true;
		// 献祭仪式属于夜晚开场：额度写口只在普通夜打开，翻天确认裁定当日结算后由 EvaluateAllEligibleReady 关闭。
		RunPublicState.Phase.bQuotaOpen = true;
		CaptureNightReadyEligibility();
		break;
	case ECatRunPhase::FailureSettlementNight:
		bRunCommandsOpen = true;
		RunPublicState.EndReason = ECatRunEndReason::QuotaFailed;
		NightReadyEligibleIds.Reset();
		NightReadyIds.Reset();
		CloseShopForSettlementNight();
		break;
	case ECatRunPhase::SuccessSettlementNight:
		bRunCommandsOpen = true;
		RunPublicState.EndReason = ECatRunEndReason::Success;
		NightReadyEligibleIds.Reset();
		NightReadyIds.Reset();
		CloseShopForSettlementNight();
		break;
	case ECatRunPhase::Ending:
	case ECatRunPhase::Ended:
		bRunCommandsOpen = false;
		NightReadyEligibleIds.Reset();
		NightReadyIds.Reset();
		// 关准入门挡不住计时器：偷鱼的进食窗是 World 计时器，不经过玩法 RPC，因此局已经结束、结算也已归档之后，
		// 它照样会到期并让 Items 不可逆吃掉受害者的鱼——那时候鱼数还在变，而结算快照已经定稿。
		// 房主退出那条路一直是靠 CloseCommandsAndResolveAll 把 escrow 原样退回来收口的，正常结束漏了同一步，
		// 这里补上，让"局结束"在两条路径上是同一件事。它对空协议表是空操作，Ending 与 Ended 各进一次也没有第二次副作用。
		if (UCatSocialService* Social = GetWorld() ? GetWorld()->GetSubsystem<UCatSocialService>() : nullptr)
		{
			Social->CloseCommandsAndResolveAll();
		}
		break;
	case ECatRunPhase::NotStarted:
	default:
		bRunCommandsOpen = false;
		break;
	}

	++RunPublicState.Revision;
	Result.bApplied = true;
	Result.CurrentPhase = NewPhase;
	Result.Error = ECatRunCommandError::None;
	Result.Revision = RunPublicState.Revision;
	LastRunFlowResult = Result;
	RefreshEnvironmentAndPublish();
	UE_LOG(LogCatRun, Log, TEXT("Event=run_phase_entered RunId=%s Revision=%lld Day=%d Phase=%s Reason=%s Deadline=%.3f"),
		*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Revision,
		RunPublicState.Phase.DayIndex, *UEnum::GetValueAsString(NewPhase), *UEnum::GetValueAsString(Reason),
		RunPublicState.Phase.DeadlineServerTimeSeconds);
	return Result;
}

// Result 条件读取流程：只比较最近一次由阶段入口或事件提交写下的原因；不根据当前 Phase、额度或 ready 集合重建历史。
bool ACatfishingGameModeBase::DoesLastRunFlowResultMatch(const ECatRunTransitionReason ExpectedReason) const
{
	return LastRunFlowResult.bApplied && LastRunFlowResult.Error == ECatRunCommandError::None
		&& LastRunFlowResult.Reason == ExpectedReason;
}

// 献祭预检流程：只读验证服务器身份键、命令 gate、夜晚献祭窗口、Revision 与正额度条数；不写终态缓存，因 Items 尚可安全取消预留。
// 这里不再检查 StateTree 是否可用：额度达标本身不再触发任何转移事件，当日成败推迟到翻天确认点才裁定。
FCatRunCommandResult ACatfishingGameModeBase::ValidateCommittedQuotaContributionFromCoordinator(const FCatQuotaContributionCommand& Command) const
{
	if (Command.Context.StableNetId.IsEmpty())
	{
		return MakeRunCommandResult(Command.Context.RequestId, false, ECatRunCommandError::InvalidIdentity);
	}
	if (!Command.Context.RequestId.IsValid() || Command.QuotaCount <= 0)
	{
		return MakeRunCommandResult(Command.Context.RequestId, false, ECatRunCommandError::InvalidPayload);
	}
	if (!bRunCommandsOpen)
	{
		return MakeRunCommandResult(Command.Context.RequestId, false, ECatRunCommandError::CommandsClosed);
	}
	if (RunPublicState.Phase.Phase != ECatRunPhase::NormalNight || !RunPublicState.Phase.bQuotaOpen)
	{
		return MakeRunCommandResult(Command.Context.RequestId, false, ECatRunCommandError::InvalidPhase);
	}
	if (Command.Context.ExpectedRevision != RunPublicState.Revision)
	{
		return MakeRunCommandResult(Command.Context.RequestId, false, ECatRunCommandError::RevisionConflict);
	}
	if (static_cast<int64>(RunPublicState.QuotaProgress) + Command.QuotaCount > MAX_int32)
	{
		return MakeRunCommandResult(Command.Context.RequestId, false, ECatRunCommandError::InvalidPayload);
	}
	// 预检成功只表示当前写口可接受；Items 尚未提交、Run 也没有写入，因此 bCommitted 必须保持 false。
	return MakeRunCommandResult(Command.Context.RequestId, false, ECatRunCommandError::None);
}

// 献祭额度提交流程：只接受协调器已从 Items 记录冻结的服务器身份、额度条数与世界进度增减；先把三者连同
// ExpectedRevision 纳入幂等载荷，再校验 gate、夜晚献祭窗口和 Revision，最后一次性写两个计数器并递增一次 Revision。
// 同 RequestId 换任一数值都只能失败，不能重放旧结果。额度达标不再产生 StateTree 事件，也不再提前结束白天：当日成败只在翻天确认点裁定。
FCatRunCommandResult ACatfishingGameModeBase::SubmitCommittedQuotaContributionFromCoordinator(const FCatQuotaContributionCommand& ServerCommand)
{
	if (ServerCommand.Context.StableNetId.IsEmpty())
	{
		return MakeRunCommandResult(ServerCommand.Context.RequestId, false, ECatRunCommandError::InvalidIdentity);
	}
	if (!ServerCommand.Context.RequestId.IsValid())
	{
		return MakeRunCommandResult(ServerCommand.Context.RequestId, false, ECatRunCommandError::InvalidPayload);
	}
	const FString CacheKey = MakeRunCommandCacheKey(ServerCommand.Context.StableNetId,
		ECatRunCommandType::QuotaContribution, ServerCommand.Context.RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("ExpectedRevision=%lld|QuotaCount=%d|WorldProgressDelta=%d"),
		ServerCommand.Context.ExpectedRevision, ServerCommand.QuotaCount, ServerCommand.WorldProgressDelta);
	FCatRunCommandResult Replay;
	if (TryReplayRunCommand(CacheKey, PayloadSignature, ServerCommand.Context.RequestId, Replay))
	{
		return Replay;
	}
	if (!bRunCommandsOpen)
	{
		return CacheRunCommandResult(CacheKey, PayloadSignature,
			MakeRunCommandResult(ServerCommand.Context.RequestId, false, ECatRunCommandError::CommandsClosed));
	}
	if (RunPublicState.Phase.Phase != ECatRunPhase::NormalNight || !RunPublicState.Phase.bQuotaOpen)
	{
		return CacheRunCommandResult(CacheKey, PayloadSignature,
			MakeRunCommandResult(ServerCommand.Context.RequestId, false, ECatRunCommandError::InvalidPhase));
	}
	if (ServerCommand.Context.ExpectedRevision != RunPublicState.Revision)
	{
		// 版本冲突不写终态缓存：它描述的是"你手上的版本旧了"这个当时的世界状态，不是这条命令的最终结局，
		// 它的处置方式本来就是重读后重试。写进缓存会把同一 RequestId 的重试永远钉死在这条冲突上，
		// 让一次本该成功的重试变成永久失败。判据是"这个错误说的是请求本身，还是说的是当时的世界"——说世界的不缓存。
		return MakeRunCommandResult(ServerCommand.Context.RequestId, false, ECatRunCommandError::RevisionConflict);
	}
	const int64 NewQuotaProgress = static_cast<int64>(RunPublicState.QuotaProgress) + ServerCommand.QuotaCount;
	if (ServerCommand.QuotaCount <= 0 || NewQuotaProgress > MAX_int32)
	{
		return CacheRunCommandResult(CacheKey, PayloadSignature,
			MakeRunCommandResult(ServerCommand.Context.RequestId, false, ECatRunCommandError::InvalidPayload));
	}

	RunPublicState.QuotaProgress = static_cast<int32>(NewQuotaProgress);
	// 世界进度允许负增量（臭臭鱼），所以只在这里按下限 0 截断；额度条数不受它影响，臭臭鱼一样算一条。
	RunPublicState.WorldProgress = static_cast<int32>(FMath::Clamp<int64>(
		static_cast<int64>(RunPublicState.WorldProgress) + ServerCommand.WorldProgressDelta, 0, MAX_int32));
	++RunPublicState.Revision;
	RefreshEnvironmentAndPublish();
	return CacheRunCommandResult(CacheKey, PayloadSignature,
		MakeRunCommandResult(ServerCommand.Context.RequestId, true, ECatRunCommandError::None));
}

// 翻天确认流程：服务器重建身份后把 ExpectedRevision 与 ready 值纳入幂等载荷；同 RequestId 不能从确认漂移成撤销或反向漂移。
FCatRunCommandResult ACatfishingGameModeBase::SubmitNextDayReady(AController* RequestingController, const FCatNextDayReadyCommand& Command)
{
	FCatNextDayReadyCommand ServerCommand = Command;
	if (!FillServerCommandIdentity(RequestingController, ServerCommand.Context))
	{
		return MakeRunCommandResult(Command.Context.RequestId, false, ECatRunCommandError::InvalidIdentity);
	}
	if (!ServerCommand.Context.RequestId.IsValid())
	{
		return MakeRunCommandResult(ServerCommand.Context.RequestId, false, ECatRunCommandError::InvalidPayload);
	}
	const FString CacheKey = MakeRunCommandCacheKey(ServerCommand.Context.StableNetId,
		ECatRunCommandType::NextDayReady, ServerCommand.Context.RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("ExpectedRevision=%lld|Ready=%s"),
		ServerCommand.Context.ExpectedRevision, ServerCommand.bReady ? TEXT("true") : TEXT("false"));
	FCatRunCommandResult Replay;
	if (TryReplayRunCommand(CacheKey, PayloadSignature, ServerCommand.Context.RequestId, Replay))
	{
		return Replay;
	}
	if (!bRunCommandsOpen)
	{
		return CacheRunCommandResult(CacheKey, PayloadSignature,
			MakeRunCommandResult(ServerCommand.Context.RequestId, false, ECatRunCommandError::CommandsClosed));
	}
	if (RunPublicState.Phase.Phase != ECatRunPhase::NormalNight || bAllEligibleReadyEventSent)
	{
		return CacheRunCommandResult(CacheKey, PayloadSignature,
			MakeRunCommandResult(ServerCommand.Context.RequestId, false, ECatRunCommandError::InvalidPhase));
	}
	if (ServerCommand.Context.ExpectedRevision != RunPublicState.Revision)
	{
		// 版本冲突不写终态缓存：它描述的是"你手上的版本旧了"这个当时的世界状态，不是这条命令的最终结局，
		// 它的处置方式本来就是重读后重试。写进缓存会把同一 RequestId 的重试永远钉死在这条冲突上，
		// 让一次本该成功的重试变成永久失败。判据是"这个错误说的是请求本身，还是说的是当时的世界"——说世界的不缓存。
		return MakeRunCommandResult(ServerCommand.Context.RequestId, false, ECatRunCommandError::RevisionConflict);
	}
	if (!NightReadyEligibleIds.Contains(ServerCommand.Context.StableNetId))
	{
		const ECatRunCommandError Error = GetDefault<UCatRunSettings>()->CanAdmitLateNightReady()
			? ECatRunCommandError::NotEligible : ECatRunCommandError::PolicyUndecided;
		return CacheRunCommandResult(CacheKey, PayloadSignature,
			MakeRunCommandResult(ServerCommand.Context.RequestId, false, Error));
	}

	ACatfishingPlayerState* PlayerState = RequestingController
		? RequestingController->GetPlayerState<ACatfishingPlayerState>() : nullptr;
	if (!PlayerState)
	{
		return CacheRunCommandResult(CacheKey, PayloadSignature,
			MakeRunCommandResult(ServerCommand.Context.RequestId, false, ECatRunCommandError::InvalidIdentity));
	}
	const bool bWasReady = NightReadyIds.Contains(ServerCommand.Context.StableNetId);
	if (ServerCommand.bReady)
	{
		NightReadyIds.Add(ServerCommand.Context.StableNetId);
	}
	else
	{
		NightReadyIds.Remove(ServerCommand.Context.StableNetId);
	}
	PlayerState->SetNextDayReadyFromAuthority(ServerCommand.bReady);
	if (bWasReady != ServerCommand.bReady)
	{
		++RunPublicState.Revision;
		RefreshEnvironmentAndPublish();
	}
	FCatRunCommandResult Result = CacheRunCommandResult(CacheKey, PayloadSignature,
		MakeRunCommandResult(ServerCommand.Context.RequestId, true, ECatRunCommandError::None));
	EvaluateAllEligibleReady();
	return Result;
}

// 结算完成流程：协调器使用专用私有身份键，并把 ExpectedRevision 纳入同一终态载荷；结算重试不能换 Revision 复用旧 RequestId。
FCatRunCommandResult ACatfishingGameModeBase::CompleteSettlementFromCoordinator(const FGuid RequestId, const int64 ExpectedRevision)
{
	if (!RequestId.IsValid())
	{
		return MakeRunCommandResult(RequestId, false, ECatRunCommandError::InvalidPayload);
	}
	const FString CacheKey = MakeRunCommandCacheKey(TEXT("RunCoordinator"), ECatRunCommandType::SettlementComplete, RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("ExpectedRevision=%lld"), ExpectedRevision);
	FCatRunCommandResult Replay;
	if (TryReplayRunCommand(CacheKey, PayloadSignature, RequestId, Replay))
	{
		return Replay;
	}
	if (!bRunCommandsOpen)
	{
		return CacheRunCommandResult(CacheKey, PayloadSignature,
			MakeRunCommandResult(RequestId, false, ECatRunCommandError::CommandsClosed));
	}
	if (RunPublicState.Phase.Phase != ECatRunPhase::FailureSettlementNight
		&& RunPublicState.Phase.Phase != ECatRunPhase::SuccessSettlementNight)
	{
		return CacheRunCommandResult(CacheKey, PayloadSignature,
			MakeRunCommandResult(RequestId, false, ECatRunCommandError::InvalidPhase));
	}
	if (ExpectedRevision != RunPublicState.Revision)
	{
		// 同上：版本冲突描述的是当时的世界，不是这条命令的结局，不写终态缓存。
		return MakeRunCommandResult(RequestId, false, ECatRunCommandError::RevisionConflict);
	}
	const FCatRunCommandResult SuccessResult = MakeRunCommandResult(RequestId, true,
		ECatRunCommandError::None, ECatRunTransitionReason::SettlementComplete);
	if (!SendRunStateTreeEvent(CatRunStateTreeEvents::SettlementComplete, ECatRunTransitionReason::SettlementComplete))
	{
		// StateTree 送不进事件说明的是"这一刻流程机不可用"，同样是当时的世界状态而不是这条命令的结局。
		// 这里尤其不能缓存：结算完成是结算夜唯一的收口出口，把一次瞬时不可用钉成终态，
		// 就等于让这一局再也走不完结算。
		return MakeRunCommandResult(RequestId, false, ECatRunCommandError::StateTreeUnavailable);
	}
	return CacheRunCommandResult(CacheKey, PayloadSignature, SuccessResult);
}

// 夜间资格冻结流程：从当前 Active 身份记录建立一次性集合，并把对应 PlayerState ready 清零；晚加入/重连不会隐式写入该集合。
void ACatfishingGameModeBase::CaptureNightReadyEligibility()
{
	NightReadyEligibleIds.Reset();
	NightReadyIds.Reset();
	bAllEligibleReadyEventSent = false;
	for (const TPair<FString, FAdmissionRecord>& Pair : AdmissionRecords)
	{
		if (Pair.Value.Phase != EAdmissionPhase::Active || !Pair.Value.Controller.IsValid())
		{
			continue;
		}
		NightReadyEligibleIds.Add(Pair.Key);
		if (ACatfishingPlayerState* PlayerState = Pair.Value.Controller->GetPlayerState<ACatfishingPlayerState>())
		{
			PlayerState->SetNextDayReadyFromAuthority(false);
		}
	}
}

// 全员确认求值流程：仅在普通夜、集合非空、尚未发事件且 ready 覆盖合资格集合时成立；这一刻是本局唯一的当日额度结算点，
// 交齐发 AllEligibleReady 走下一天，未交齐发 QuotaFailed 走失败结算夜。
// 事件发不出去时回滚已发标记并保持额度窗口开着：这次确认等于没发生，玩家可以继续献祭后重新确认，而不是被卡在一个既没翻天又不能献祭的夜里。
// 事件成功发出后才关闭额度窗口并递增 Revision，因为 StateTree 事件要等下一次 Tick 才处理；不关窗口的话，排队期间还能再献一条鱼改写已经裁定的结果。
// 本方法仍然不改写 Phase：目标状态由 ST_RunFlow 资产按事件与结构化原因选择。
void ACatfishingGameModeBase::EvaluateAllEligibleReady()
{
	if (RunPublicState.Phase.Phase != ECatRunPhase::NormalNight || bAllEligibleReadyEventSent
		|| NightReadyEligibleIds.IsEmpty() || NightReadyIds.Num() != NightReadyEligibleIds.Num())
	{
		return;
	}
	const bool bQuotaSatisfied = RunPublicState.QuotaProgress >= RunPublicState.QuotaTarget;
	const FGameplayTag SettlementEvent = bQuotaSatisfied
		? CatRunStateTreeEvents::AllEligibleReady.GetTag() : CatRunStateTreeEvents::QuotaFailed.GetTag();
	const ECatRunTransitionReason SettlementReason = bQuotaSatisfied
		? ECatRunTransitionReason::AllEligibleReady : ECatRunTransitionReason::QuotaFailed;
	bAllEligibleReadyEventSent = true;
	if (!SendRunStateTreeEvent(SettlementEvent, SettlementReason))
	{
		bAllEligibleReadyEventSent = false;
		return;
	}
	RunPublicState.Phase.bQuotaOpen = false;
	++RunPublicState.Revision;
	RefreshEnvironmentAndPublish();
	UE_LOG(LogCatRun, Log, TEXT("Event=run_quota_settled RunId=%s Revision=%lld Day=%d QuotaProgress=%d QuotaTarget=%d WorldProgress=%d Satisfied=%s"),
		*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Revision,
		RunPublicState.Phase.DayIndex, RunPublicState.QuotaProgress, RunPublicState.QuotaTarget,
		RunPublicState.WorldProgress, bQuotaSatisfied ? TEXT("true") : TEXT("false"));
}

// 时段分界排程流程：先无条件清掉上一次的分界计时器，再向 Environment 要下一个严格晚于当前时刻的分界；
// 拿到就按剩余秒数设一次性计时器，拿不到（夜晚、结算夜、收口，或者今天已经进了 Dusk）就停在已清空状态。
// 每次环境发布都重排一次，所以白天锚点或时长被改写后，下一次唤醒会跟着新的白天区间，而不是沿用旧计划。
void ACatfishingGameModeBase::ScheduleNextTimeOfDayBoundary()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	World->GetTimerManager().ClearTimer(TimeOfDayBoundaryTimerHandle);
	TimeOfDayBoundaryTimerHandle.Invalidate();
	const UCatEnvironmentSettings* Settings = GetDefault<UCatEnvironmentSettings>();
	const double NowSeconds = World->GetTimeSeconds();
	double BoundarySeconds = 0.0;
	if (!Settings || !Settings->TryGetNextTimeOfDayBoundarySeconds(RunPublicState.Phase, NowSeconds, BoundarySeconds))
	{
		return;
	}
	const float DelaySeconds = static_cast<float>(BoundarySeconds - NowSeconds);
	if (DelaySeconds <= 0.0f)
	{
		return;
	}
	World->GetTimerManager().SetTimer(TimeOfDayBoundaryTimerHandle, this,
		&ThisClass::HandleTimeOfDayBoundaryReached, DelaySeconds, false);
}

// 时段分界到点流程：先失效已经执行完的一次性句柄，再确认仍停在同一个持有截止点的白天；通过后只推进一次 Revision 并重新发布，
// 由发布流程本身换出新的 Morning/Day/Dusk 并排下一个分界。这里不发 StateTree 事件，也不碰钓鱼开关——时段推进不改变 Run 相位。
void ACatfishingGameModeBase::HandleTimeOfDayBoundaryReached()
{
	TimeOfDayBoundaryTimerHandle.Invalidate();
	if (!HasAuthority() || RunPublicState.Phase.Phase != ECatRunPhase::DayActive
		|| !RunPublicState.Phase.bHasDeadline)
	{
		return;
	}
	++RunPublicState.Revision;
	RefreshEnvironmentAndPublish();
}

// 截止清理流程：从当前 World 清除唯一 TimerHandle，并同步清空公开 deadline 三字段；不暂停计时器，因此夜晚和 teardown 不会保留可恢复倒计时。
void ACatfishingGameModeBase::ClearDayDeadline()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DayDeadlineTimerHandle);
	}
	DayDeadlineTimerHandle.Invalidate();
	RunPublicState.Phase.bHasDeadline = false;
	RunPublicState.Phase.DeadlineServerTimeSeconds = 0.0;
}

// 白天截止流程：只消费仍持有截止点的同一 DayActive，先关闭钓鱼、清掉公开截止事实并递增 Revision、发布快照，再向
// StateTree 发送 DayElapsed；夜晚不创建新计时器。
// 这里不判定额度：白天是固定时长弧线，到点只表示入夜，当日成败要等玩家在夜里确认翻天时才裁定。
void ACatfishingGameModeBase::HandleDayDeadlineElapsed()
{
	DayDeadlineTimerHandle.Invalidate();
	if (!HasAuthority() || !bRunCommandsOpen || RunPublicState.Phase.Phase != ECatRunPhase::DayActive
		|| !RunPublicState.Phase.bHasDeadline)
	{
		return;
	}
	RunPublicState.Phase.bFishingAllowed = false;
	RunPublicState.Phase.bHasDeadline = false;
	RunPublicState.Phase.DeadlineServerTimeSeconds = 0.0;
	RunPublicState.Phase.ServerTimeAnchorSeconds = GetWorld()->GetTimeSeconds();
	++RunPublicState.Revision;
	RefreshEnvironmentAndPublish();
	SendRunStateTreeEvent(CatRunStateTreeEvents::DayElapsed, ECatRunTransitionReason::DayElapsed);
}

// 清晨副作用流程：遍历当前 Active 准入记录，逐个把个人 ready 清零、终止该 Character 仍未结算的钓鱼会话，并只对仍然倒
// 地的玩家走 Condition 的营地休息写口把人救起。
// 倒地解除必须走 Condition 公开入口而不是 Run 直接改 Attribute：倒地阈值、恢复方式记录和幂等缓存都只有 Condition 拥
// 有，Run 自己写数值会立刻和身体真相分叉。
// 选营地休息这条路径，是因为飞书写的是"翻天后全员清晨在营地醒来，未获救的倒地者自动救起"；倒地阈值未裁时
// RequestCampRest 返回 PolicyUndecided，这里保持倒地并计入日志，不伪装成已救起。
void ACatfishingGameModeBase::ApplyDayBreakSideEffects()
{
	// 商人猫开市：天数直接用 Run 当前的 DayIndex，商店不自己数天，也就不会和 Run 的天数分叉。
	// AdvanceShopDay 只补显式标了每日进货的条目，而进货量飞书未裁、目录里现在一条都没标，
	// 所以它当前必然返回 false——接线是真的接上了，只是没有货可补。
	if (UCatShopEconomyService* Shop = GetWorld() ? GetWorld()->GetSubsystem<UCatShopEconomyService>() : nullptr)
	{
		if (Shop->AdvanceShopDay(RunPublicState.Phase.DayIndex))
		{
			PublishShopEconomySnapshot();
		}
	}
	UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr;
	int32 RescuedCount = 0;
	int32 UnrescuedCount = 0;
	for (const TPair<FString, FAdmissionRecord>& Pair : AdmissionRecords)
	{
		if (Pair.Value.Phase != EAdmissionPhase::Active || !Pair.Value.Controller.IsValid())
		{
			continue;
		}
		AController* Controller = Pair.Value.Controller.Get();
		if (ACatfishingPlayerState* PlayerState = Controller->GetPlayerState<ACatfishingPlayerState>())
		{
			PlayerState->SetNextDayReadyFromAuthority(false);
		}
		ACatCharacter* Character = Cast<ACatCharacter>(Controller->GetPawn());
		if (!Character)
		{
			continue;
		}
		if (Fishing)
		{
			// 翻天确认要强制打断一切进行中的互动；这里按 Character 逐个终止，而不是关闭整个 Fishing 服务的命令门——新的一天还要继续钓鱼。
			Fishing->TerminateSessionsForCharacter(Character);
		}
		UCatConditionComponent* Conditions = Character->GetConditionComponent();
		if (!Conditions || !Conditions->GetSnapshot().bDowned)
		{
			continue;
		}
		if (Conditions->RequestCampRest(Controller, FGuid::NewGuid(), true).bCommitted)
		{
			++RescuedCount;
		}
		else
		{
			++UnrescuedCount;
		}
	}
	UE_LOG(LogCatRun, Log, TEXT("Event=run_daybreak_side_effects RunId=%s Day=%d FishingService=%s Rescued=%d Unrescued=%d"),
		*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Phase.DayIndex,
		Fishing ? TEXT("true") : TEXT("false"), RescuedCount, UnrescuedCount);
}

// 环境发布流程：先判断当前 Phase 是否仍需要环境事实；白天和普通夜会调用 provider 并在成功时替换同 Revision 环境 DTO，
// Settlement/Ending 发布对齐当前 Revision 的中性快照而不触发天气或自然事件副作用。环境失败只记录诊断而不制造替代天
// 气，GameState 发布仍是唯一公开出口。

bool ACatfishingGameModeBase::RefreshEnvironmentAndPublish()
{
	const ECatRunPhase CurrentPhase = RunPublicState.Phase.Phase;
	const bool bShouldEvaluateEnvironment = CurrentPhase != ECatRunPhase::FailureSettlementNight
		&& CurrentPhase != ECatRunPhase::SuccessSettlementNight
		&& CurrentPhase != ECatRunPhase::Ending
		&& CurrentPhase != ECatRunPhase::Ended;
	bool bEnvironmentSucceeded = true;
	if (!bShouldEvaluateEnvironment)
	{
		// Terminal 阶段不再拥有天气/公共事件副作用；发布中性快照并对齐 Revision，避免旧环境事实被误认为当前状态。
		RunPublicState.Environment = FCatEnvironmentSnapshot();
		RunPublicState.Environment.SourceRunRevision = RunPublicState.Revision;
	}
	else
	{
		const ICatEnvironmentProvider* Provider = Cast<ICatEnvironmentProvider>(EnvironmentProvider);
		const FCatEnvironmentResult EnvironmentResult = Provider
			? Provider->EvaluateEnvironment(RunPublicState.Phase, RunPublicState.Revision)
			: FCatEnvironmentResult();
		bEnvironmentSucceeded = EnvironmentResult.bSucceeded;
		if (EnvironmentResult.bSucceeded)
		{
			RunPublicState.Environment = EnvironmentResult.Snapshot;
			if (EnvironmentResult.Snapshot.Weather == ECatEnvironmentWeather::Rain)
			{
				for (TActorIterator<ACatCharacter> It(GetWorld()); It; ++It)
				{
					if (UCatConditionComponent* Conditions = It->GetConditionComponent())
					{
						Conditions->SetWetFromAuthority(true);
					}
				}
			}
		}
		else
		{
			UE_LOG(LogCatRun, Error, TEXT("Event=environment_evaluation_failed RunId=%s Revision=%lld Error=%s"),
				*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Revision,
				Provider ? *EnvironmentResult.Error : TEXT("ProviderUnavailable"));
		}
	}
	// 事件副作用统一在这里落实，而不是塞进上面成功分支里：终局阶段把事件清成 None、求值失败沿用上一份快照，
	// 这两条路都不该被漏掉或重复处理，而"事件 ID 变没变"这一个判据就能同时覆盖它们。
	ApplyEnvironmentEventSideEffects();
	// 时段必须自己往前走：环境快照里的 Morning/Day/Dusk 是按"现在离白天截止还有多远"算出来的，
	// 没有人在分界时刻叫醒它，它就会一直停在进入白天那一刻的取值。这里按当前 Phase 重排一次分界计时器；
	// 夜晚、结算夜和收口阶段拿不到分界，排程会顺手把计时器清掉。
	ScheduleNextTimeOfDayBoundary();
	ACatfishingGameState* CatGameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
	if (CatGameState)
	{
		CatGameState->SetRunPublicStateFromAuthority(RunPublicState);
	}
	return bEnvironmentSucceeded && CatGameState != nullptr;
}

// 商人猫收摊流程：进结算夜时现取本 World 的经济服务并关闭四个写口；缺服务时什么都不做。
// CloseCommands 幂等，两个结算夜分支各调一次、之后 World teardown 再调一次都不会产生副作用，因此这里不额外记"是否已收摊"。
void ACatfishingGameModeBase::CloseShopForSettlementNight()
{
	if (UCatShopEconomyService* Shop = GetWorld() ? GetWorld()->GetSubsystem<UCatShopEconomyService>() : nullptr)
	{
		Shop->CloseCommands();
	}
	// 装备库要跟着一起关：商店停了但入库还开着的话，一笔卡在半路的订单仍能在结算夜把东西发出来，
	// 而那时候玩家已经不该再拿到任何新装备了。
	if (UCatTeamEquipmentLibrary* Library = GetWorld() ? GetWorld()->GetSubsystem<UCatTeamEquipmentLibrary>() : nullptr)
	{
		Library->CloseCommands();
	}
}

// 经济快照发布流程：现取本 World 的经济服务与 GameState，整体重建后一次替换；缺任何一方时什么都不做。
void ACatfishingGameModeBase::PublishShopEconomySnapshot()
{
	ACatfishingGameState* CatGameState = GetGameState<ACatfishingGameState>();
	UCatShopEconomyService* Shop = GetWorld() ? GetWorld()->GetSubsystem<UCatShopEconomyService>() : nullptr;
	if (!CatGameState || !Shop)
	{
		return;
	}
	CatGameState->SetShopEconomySnapshotFromAuthority(Shop->BuildPublicSnapshot(
		[this](const FString& StableNetId) { return ResolvePlayerStateByStableNetId(StableNetId); }));
}

// 装备库快照发布流程：库快照里没有服务器私有身份，直接整体复制，不需要任何解析。
void ACatfishingGameModeBase::PublishTeamEquipmentLibrarySnapshot()
{
	ACatfishingGameState* CatGameState = GetGameState<ACatfishingGameState>();
	UCatTeamEquipmentLibrary* Library = GetWorld() ? GetWorld()->GetSubsystem<UCatTeamEquipmentLibrary>() : nullptr;
	if (!CatGameState || !Library)
	{
		return;
	}
	CatGameState->SetTeamEquipmentLibraryFromAuthority(Library->GetSnapshot());
}

// 身份解析流程：只认当前 Active 且 Controller 仍有效的准入记录；Reserved、已离局和空键一律返回 nullptr。
APlayerState* ACatfishingGameModeBase::ResolvePlayerStateByStableNetId(const FString& StableNetId) const
{
	const FAdmissionRecord* Record = StableNetId.IsEmpty() ? nullptr : AdmissionRecords.Find(StableNetId);
	if (!Record || Record->Phase != EAdmissionPhase::Active || !Record->Controller.IsValid())
	{
		return nullptr;
	}
	return Record->Controller->PlayerState;
}

// 事件换场流程：先比对上一次发布的事件 ID，没换就什么都不做——环境快照每次重发布都会带上同一个仍在进行的事件，
// 而"出场"在产品口径上只发生一次。真的换了才更新记录并按事件类型分派副作用：森林湖鱼群投窝，会抛印记的那几类拍一张。
// 换成 None（事件结束）只更新记录，没有落幕副作用。
void ACatfishingGameModeBase::ApplyEnvironmentEventSideEffects()
{
	if (!HasAuthority() || !GetWorld() || RunPublicState.Environment.ActiveEventId == LastPublishedEnvironmentEventId)
	{
		return;
	}
	LastPublishedEnvironmentEventId = RunPublicState.Environment.ActiveEventId;
	if (LastPublishedEnvironmentEventId.IsNone())
	{
		return;
	}
	UE_LOG(LogCatEnvironment, Log, TEXT("Event=environment_event_entered RunId=%s Day=%d EnvironmentEvent=%s Weather=%s TimeOfDay=%s Revision=%lld"),
		*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Phase.DayIndex,
		*LastPublishedEnvironmentEventId.ToString(), *UEnum::GetValueAsString(RunPublicState.Environment.Weather),
		*UEnum::GetValueAsString(RunPublicState.Environment.TimeOfDay), RunPublicState.Revision);
	if (LastPublishedEnvironmentEventId == CatEnvironmentEvents::ForestLakeSchool)
	{
		SubmitNaturalAggregationForActiveEvent();
	}
	if (CatEnvironmentEvents::DoesEventProduceImprintCandidate(LastPublishedEnvironmentEventId))
	{
		SubmitEnvironmentImprintCandidateForActiveEvent();
	}
}

// 自然聚鱼流程：落点由配置的 RegionId 对应那片水域的几何给出，投放量由环境配置给出，两者缺一都不投——
// 编一个落点会把窝投到岸上，编一个投放量会凭空改变聚鱼时刻的收益。
// 这里不再自己记去重键：聚鱼时刻的共享冷却本身就是那本账，投窝一旦 committed，Provider 在冷却期内不会再选中这个事件。
// 命令走的是玩家投窝完全相同的写口，Source 只用于审计，不选择另一套鱼群状态。
void ACatfishingGameModeBase::SubmitNaturalAggregationForActiveEvent()
{
	const UCatEnvironmentSettings* Settings = GetDefault<UCatEnvironmentSettings>();
	UCatWaterQuerySubsystem* WaterQuery = GetWorld()->GetSubsystem<UCatWaterQuerySubsystem>();
	UCatChumSpotSubsystem* ChumSpots = GetWorld()->GetSubsystem<UCatChumSpotSubsystem>();
	FVector DropLocation = FVector::ZeroVector;
	FCatChumVector Contribution;
	if (!Settings || !WaterQuery || !ChumSpots
		|| !WaterQuery->TryGetRegionCenter(Settings->NaturalAggregationRegionId, DropLocation)
		|| !Settings->TryGetNaturalAggregationContribution(Contribution))
	{
		UE_LOG(LogCatEnvironment, Warning, TEXT("Event=natural_aggregation_unavailable Day=%d EnvironmentEvent=%s RegionId=%s"),
			RunPublicState.Phase.DayIndex, *LastPublishedEnvironmentEventId.ToString(),
			Settings ? *Settings->NaturalAggregationRegionId.ToString() : TEXT("None"));
		return;
	}
	FCatAggregationCommand Command;
	Command.Context.RequestId = FGuid::NewGuid();
	Command.Context.ExpectedRevision = ChumSpots->GetAggregationRevision();
	Command.Context.StableNetId = TEXT("Environment");
	Command.DropLocation = DropLocation;
	Command.Contribution = Contribution;
	Command.Source = ECatAggregationSource::NaturalEvent;
	const FCatAggregationResult Result = ChumSpots->ContributeChum(Command);
	UE_LOG(LogCatEnvironment, Log, TEXT("Event=natural_aggregation_terminal RequestId=%s Day=%d EnvironmentEvent=%s Region=%s Drop=%s Committed=%s Error=%s Revision=%lld"),
		*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Phase.DayIndex,
		*LastPublishedEnvironmentEventId.ToString(), *Settings->NaturalAggregationRegionId.ToString(),
		*DropLocation.ToCompactString(), Result.Command.bCommitted ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(Result.Command.Error), Result.Command.Revision);
}

// 环境印记候选流程：把这次出场折算成一条候选，参与者取当前全部在场身份（自然事件是全场共同看到的，不属于某一个人）。
// 提交前先用只读预检问一遍印记服务收不收：飞书印记册的触发总清单目前只放行了偷鱼被抓一条，五类环境事件都还没被裁进去，
// 直接调提交会让服务每次都打一条 Warning，把"本局本来就不产环境印记"刷成噪音。等策划把事件写进准入名单，这条链不改代码就通了。
void ACatfishingGameModeBase::SubmitEnvironmentImprintCandidateForActiveEvent()
{
	UCatRunImprintService* Imprint = GetWorld()->GetSubsystem<UCatRunImprintService>();
	if (!Imprint)
	{
		return;
	}
	FCatImprintCandidate Candidate;
	Candidate.CandidateId = FGuid::NewGuid();
	Candidate.RunId = RunPublicState.Phase.RunId;
	Candidate.EventType = LastPublishedEnvironmentEventId;
	for (const TPair<FString, FAdmissionRecord>& Record : AdmissionRecords)
	{
		if (Record.Value.Phase == EAdmissionPhase::Active && Record.Value.Controller.IsValid())
		{
			Candidate.ParticipantStableNetIds.Add(Record.Key);
		}
	}
	Candidate.ParticipantCount = Candidate.ParticipantStableNetIds.Num();
	if (Candidate.ParticipantCount <= 0 || !Imprint->CanAcceptImprintCandidate(Candidate))
	{
		UE_LOG(LogCatEnvironment, Verbose, TEXT("Event=environment_imprint_candidate_skipped Day=%d EnvironmentEvent=%s Participants=%d"),
			RunPublicState.Phase.DayIndex, *LastPublishedEnvironmentEventId.ToString(), Candidate.ParticipantCount);
		return;
	}
	const bool bSubmitted = Imprint->SubmitImprintCandidate(Candidate);
	UE_LOG(LogCatEnvironment, Log, TEXT("Event=environment_imprint_candidate_submitted Day=%d EnvironmentEvent=%s CandidateId=%s Participants=%d Submitted=%s"),
		RunPublicState.Phase.DayIndex, *LastPublishedEnvironmentEventId.ToString(),
		*Candidate.CandidateId.ToString(EGuidFormats::DigitsWithHyphens), Candidate.ParticipantCount,
		bSubmitted ? TEXT("true") : TEXT("false"));
}

// StateTree 事件提交流程：先验证组件正在运行与 Tag 有效，再保存一份不改变 Phase 的结构化结果并发送事件；资产负责消费 Tag 和选择目标边。
bool ACatfishingGameModeBase::SendRunStateTreeEvent(const FGameplayTag EventTag, const ECatRunTransitionReason Reason)
{
	if (!EventTag.IsValid() || !RunStateTreeComponent || !RunStateTreeComponent->IsRunning())
	{
		LastRunFlowResult.bApplied = false;
		LastRunFlowResult.PreviousPhase = RunPublicState.Phase.Phase;
		LastRunFlowResult.CurrentPhase = RunPublicState.Phase.Phase;
		LastRunFlowResult.Reason = Reason;
		LastRunFlowResult.Error = ECatRunCommandError::StateTreeUnavailable;
		LastRunFlowResult.Revision = RunPublicState.Revision;
		return false;
	}
	LastRunFlowResult.bApplied = true;
	LastRunFlowResult.PreviousPhase = RunPublicState.Phase.Phase;
	LastRunFlowResult.CurrentPhase = RunPublicState.Phase.Phase;
	LastRunFlowResult.Reason = Reason;
	LastRunFlowResult.Error = ECatRunCommandError::None;
	LastRunFlowResult.Revision = RunPublicState.Revision;
	RunStateTreeComponent->SendStateTreeEvent(EventTag, FConstStructView(), FName(TEXT("CatRun")));
	return true;
}

// 启动失败流程：保持 NotStarted，清计时器与写口，写 StartupFailed 并递增 Revision；不会启动备用 C++ FSM 或假装 StateTree 已运行。
void ACatfishingGameModeBase::FailRunStartup(const TCHAR* Reason)
{
	bRunCommandsOpen = false;
	ClearDayDeadline();
	RunPublicState.Phase.Phase = ECatRunPhase::NotStarted;
	RunPublicState.Phase.bFishingAllowed = false;
	RunPublicState.Phase.bQuotaOpen = false;
	RunPublicState.EndReason = ECatRunEndReason::StartupFailed;
	++RunPublicState.Revision;
	RefreshEnvironmentAndPublish();
	UE_LOG(LogCatRun, Error, TEXT("Event=run_startup_failed RunId=%s Revision=%lld Reason=%s"),
		*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Revision, Reason);
}

// Host teardown 流程：先重放已 Ready 的新 Online 关联键，Pending 期则只接受原 RequestId/epoch；首次请求必须同时具备
// Imprint/牺牲协调器与正超时。领域协调器按 Social→Items 等顺序关不可逆命令，Imprint 先最终重投 Grant，然后才关
// Run/Timer/StateTree、发 HostExit 并等远端 Destroy ACK；只有远端 ACK 与 durable Grant ACK 全齐才 Ready，超时会报告风
// 险而不伪造 ACK。
FCatRunTeardownResult ACatfishingGameModeBase::RequestRunTeardown(const FCatRunTeardownRequest& Request)
{
	FCatRunTeardownResult Result;
	Result.RequestId = Request.RequestId;
	Result.OperationEpoch = Request.OperationEpoch;
	if (!HasAuthority() || !Request.RequestId.IsValid() || Request.OperationEpoch <= 0)
	{
		Result.Status = ECatRunTeardownStatus::Failed;
		Result.Error = ECatRunCommandError::TeardownFailed;
		return Result;
	}
	if (ActiveHostExitRequestId.IsValid())
	{
		// 本地领域与统一 ACK 已完成后允许 Online 用新 RequestId/epoch 重试 Destroy/Frontend；返回新关联键但绝不重做清理或再次通知远端。
		if (bHostExitAckWaitComplete && RunPublicState.bTeardownComplete)
		{
			Result.Status = ECatRunTeardownStatus::Ready;
			return Result;
		}
		if (Request.RequestId != ActiveHostExitRequestId || Request.OperationEpoch != ActiveHostExitOperationEpoch)
		{
			Result.Status = ECatRunTeardownStatus::Failed;
			Result.Error = ECatRunCommandError::TeardownFailed;
			return Result;
		}
		Result.Status = ECatRunTeardownStatus::Pending;
		return Result;
	}
	TArray<ACatfishingPlayerController*> RemoteControllers;
	TArray<FString> RemoteStableNetIds;
	for (const TPair<FString, FAdmissionRecord>& Pair : AdmissionRecords)
	{
		ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(Pair.Value.Controller.Get());
		if (Pair.Value.Phase == EAdmissionPhase::Active && Controller && !Controller->IsLocalController())
		{
			RemoteControllers.Add(Controller);
			RemoteStableNetIds.Add(Pair.Key);
		}
	}
	UCatSacrificeCoordinator* SacrificeCoordinator = GetWorld() ? GetWorld()->GetSubsystem<UCatSacrificeCoordinator>() : nullptr;
	UCatRunImprintService* ImprintService = GetWorld() ? GetWorld()->GetSubsystem<UCatRunImprintService>() : nullptr;
	if (!SacrificeCoordinator || !ImprintService)
	{
		Result.Status = ECatRunTeardownStatus::Failed;
		Result.Error = ECatRunCommandError::TeardownFailed;
		return Result;
	}
	const bool bNeedsBoundedAckWait = !RemoteControllers.IsEmpty() || !ImprintService->AreAllGrantAcksComplete();
	double HostExitAckTimeoutSeconds = 0.0;
	if (bNeedsBoundedAckWait && !GetDefault<UCatOnlineSettings>()->TryGetHostExitAckTimeout(HostExitAckTimeoutSeconds))
	{
		Result.Status = ECatRunTeardownStatus::Failed;
		Result.Error = ECatRunCommandError::TeardownFailed;
		return Result;
	}
	// 两步都要真的跑完再判成败：献祭协议即使没收完，四个领域的命令门也必须关上，不能因为前一步失败就把写口留开着。
	const bool bSacrificeSettled = SacrificeCoordinator->PrepareForRunTeardown();
	const bool bDomainsClosed = CloseRunDomainCommands();
	// 准入门必须在这里关、而不是等收口判定通过之后再关：上面两个调用都是进函数第一行就关掉自己的写口、与返回值无关，
	// 所以只要走到这里，领域就已经关了。CanAcceptGameplayCommand 只看 bRunCommandsOpen，把它留在 true 上
	// 就形成"门开着、房间全锁了"——玩法 RPC 照常穿过门再撞上 CommandsClosed，其中取用团队装备那条会先把装备
	// 装到角色身上才发现取不走，留下装备与公库各有一份的不一致。收口失败只说明这次退出不能继续往下走，
	// 不代表那些已经关掉的写口还能用。
	bRunCommandsOpen = false;
	if (!bSacrificeSettled || !bDomainsClosed)
	{
		// 这条失败以前完全没有留痕，排查时只能看到"玩家点什么都没反应"。两个分项分开报，是因为它们的后续处理不同：
		// 献祭没收完要查卡住的协议记录，领域没关干净通常是社交 escrow 还不回去。
		UE_LOG(LogCatRun, Error,
			TEXT("Event=run_teardown_failed RequestId=%s SacrificeSettled=%s DomainsClosed=%s"),
			*Request.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			bSacrificeSettled ? TEXT("true") : TEXT("false"),
			bDomainsClosed ? TEXT("true") : TEXT("false"));
		Result.Status = ECatRunTeardownStatus::Failed;
		Result.Error = ECatRunCommandError::TeardownFailed;
		return Result;
	}
	// 先完成最终 Grant 重投，再发送远端退出 RPC；同一 Controller 上的 Reliable RPC 顺序保证 Grant 在 Destroy 通知之前到达。
	const bool bGrantAcksComplete = ImprintService->PrepareForRunTeardown();

	ClearDayDeadline();
	NightReadyEligibleIds.Reset();
	NightReadyIds.Reset();
	if (RunStateTreeComponent && RunStateTreeComponent->IsRunning())
	{
		RunStateTreeComponent->StopLogic(TEXT("Host Online Leave"));
	}
	RunPublicState.Phase.bFishingAllowed = false;
	RunPublicState.Phase.bQuotaOpen = false;
	RunPublicState.EndReason = ECatRunEndReason::HostExit;
	ActiveHostExitRequestId = Request.RequestId;
	ActiveHostExitOperationEpoch = Request.OperationEpoch;
	bHostExitAckWaitComplete = RemoteControllers.IsEmpty() && bGrantAcksComplete;
	RunPublicState.bTeardownComplete = bHostExitAckWaitComplete;
	PendingHostExitAckStableNetIds.Reset();
	for (int32 Index = 0; Index < RemoteControllers.Num(); ++Index)
	{
		PendingHostExitAckStableNetIds.Add(RemoteStableNetIds[Index]);
		RemoteControllers[Index]->ClientPrepareForHostExit(Request.RequestId);
	}
	++RunPublicState.Revision;
	RefreshEnvironmentAndPublish();
	if (!bHostExitAckWaitComplete)
	{
		GetWorldTimerManager().SetTimer(HostExitAckTimerHandle, this, &ThisClass::HandleHostExitAckTimeout,
			static_cast<float>(HostExitAckTimeoutSeconds), false);
	}
	Result.Status = bHostExitAckWaitComplete ? ECatRunTeardownStatus::Ready : ECatRunTeardownStatus::Pending;
	UE_LOG(LogCatRun, Log, TEXT("Event=run_teardown_%s RequestId=%s Epoch=%lld Revision=%lld PendingRemoteAcks=%d"),
		bHostExitAckWaitComplete ? TEXT("ready") : TEXT("pending"),
		*Request.RequestId.ToString(EGuidFormats::DigitsWithHyphens), Request.OperationEpoch, RunPublicState.Revision,
		PendingHostExitAckStableNetIds.Num());
	return Result;
}

// 关门流程：先终止所有未结算的钓鱼会话，再收 Social，最后才关 Items 与 ShopEconomy。
// 顺序不能变：Social 收口时要清偷鱼定时器、把 escrow 里的鱼退回去，而未收口的偷鱼售出还要靠 ShopEconomy 入账、
// 靠 Items 改容器；这两个写口先关就会让钱和鱼卡在中间态。Social 没收干净时宁可不关下游，也不留半条协议。
bool ACatfishingGameModeBase::CloseRunDomainCommands()
{
	UCatItemsService* Items = GetWorld() ? GetWorld()->GetSubsystem<UCatItemsService>() : nullptr;
	UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr;
	UCatSocialService* Social = GetWorld() ? GetWorld()->GetSubsystem<UCatSocialService>() : nullptr;
	UCatShopEconomyService* Shop = GetWorld() ? GetWorld()->GetSubsystem<UCatShopEconomyService>() : nullptr;
	bool bAllClosed = Items != nullptr && Fishing != nullptr && Social != nullptr && Shop != nullptr;
	if (Fishing)
	{
		Fishing->CloseCommandsAndTerminateAll();
	}
	const bool bSocialResolved = Social && Social->CloseCommandsAndResolveAll();
	bAllClosed &= bSocialResolved;
	if (bSocialResolved)
	{
		if (Items)
		{
			Items->CloseCommandsAndCancelReservations();
		}
		// 公款与团队装备库随局清空，所以新交易入口也在这里永久关闭；既有幂等重放仍返回首次终态，不会把已成交订单改写成失败。
		if (Shop)
		{
			Shop->CloseCommands();
		}
	}
	return bAllClosed;
}

// Host exit ACK 流程：验证当前等待、RequestId 与 Active Controller 身份后移除精确 StableNetId；最后一个远端 ACK 到达
// 后还要复核 durable Grant ACK，二者都齐才提前完成。
void ACatfishingGameModeBase::AcknowledgeHostExitClient(AController* Controller, const FGuid RequestId)
{
	if (bHostExitAckWaitComplete || RequestId != ActiveHostExitRequestId || !IsControllerActive(Controller))
	{
		return;
	}
	const APlayerState* CurrentPlayerState = Controller ? Controller->PlayerState : nullptr;
	if (!CurrentPlayerState || !CurrentPlayerState->GetUniqueId().IsValid())
	{
		return;
	}
	const FString StableNetId = CatResolveStableNetId(CurrentPlayerState->GetUniqueId());
	if (PendingHostExitAckStableNetIds.Remove(StableNetId) > 0)
	{
		NotifyHostExitGrantAckProgress();
	}
}

// Grant ACK 进度流程：只有当前确有 Host exit 等待、远端 Destroy ACK 为空且 Imprint 的真实 ACK 全齐才提前完成；不主动重投或篡改投递记录。
void ACatfishingGameModeBase::NotifyHostExitGrantAckProgress()
{
	if (bHostExitAckWaitComplete || !ActiveHostExitRequestId.IsValid() || !PendingHostExitAckStableNetIds.IsEmpty())
	{
		return;
	}
	const UCatRunImprintService* ImprintService = GetWorld() ? GetWorld()->GetSubsystem<UCatRunImprintService>() : nullptr;
	if (ImprintService && ImprintService->AreAllGrantAcksComplete())
	{
		CompleteHostExitAckWait(false);
	}
}

// Host exit ACK 完成流程：幂等清统一计时器并发布真正的 teardown complete；超时只释放有界等待，日志仍保留缺失
// Destroy/Grant ACK，绝不改写它们为已确认。
void ACatfishingGameModeBase::CompleteHostExitAckWait(const bool bTimedOut)
{
	if (bHostExitAckWaitComplete || !ActiveHostExitRequestId.IsValid() || ActiveHostExitOperationEpoch <= 0)
	{
		return;
	}
	bHostExitAckWaitComplete = true;
	GetWorldTimerManager().ClearTimer(HostExitAckTimerHandle);
	HostExitAckTimerHandle.Invalidate();
	const int32 MissingAckCount = PendingHostExitAckStableNetIds.Num();
	const UCatRunImprintService* ImprintService = GetWorld() ? GetWorld()->GetSubsystem<UCatRunImprintService>() : nullptr;
	const int32 MissingGrantAckCount = ImprintService ? ImprintService->GetPendingGrantAckCount() : 0;
	PendingHostExitAckStableNetIds.Reset();
	RunPublicState.bTeardownComplete = true;
	++RunPublicState.Revision;
	RefreshEnvironmentAndPublish();
	FCatRunTeardownResult Result;
	Result.RequestId = ActiveHostExitRequestId;
	Result.OperationEpoch = ActiveHostExitOperationEpoch;
	Result.Status = ECatRunTeardownStatus::Ready;
	RunTeardownCompleted.Broadcast(Result);
	UE_LOG(LogCatRun, Log, TEXT("Event=run_teardown_acks_complete RequestId=%s Epoch=%lld TimedOut=%s MissingRemoteAcks=%d MissingGrantAcks=%d"),
		*Result.RequestId.ToString(EGuidFormats::DigitsWithHyphens), Result.OperationEpoch,
		bTimedOut ? TEXT("true") : TEXT("false"), MissingAckCount, MissingGrantAckCount);
}

// Host exit ACK 超时流程：只完成当前统一有界等待；关联键原样广播，迟到 ACK 仍保留各自真实记录但不能推进下一代退出。
void ACatfishingGameModeBase::HandleHostExitAckTimeout()
{
	CompleteHostExitAckWait(true);
}

// Teardown 委托读取流程：返回 GameMode 生命周期内的唯一完成广播；订阅者必须自行比对 RequestId/epoch。
FCatRunTeardownCompleted& ACatfishingGameModeBase::OnRunTeardownCompleted()
{
	return RunTeardownCompleted;
}

// Run 聚合读取流程：返回服务器内存中的只读引用；客户端必须改读 GameState 复制快照。
const FCatRunPublicState& ACatfishingGameModeBase::GetRunPublicState() const
{
	return RunPublicState;
}

#if WITH_DEV_AUTOMATION_TESTS
// 自动化结果读取流程：直接返回最近一次阶段入口或事件提交写下的结构化结果。没有 ST_RunFlow 资产时事件必然发送失败，但
// 原因字段仍记录了服务器选中的那条边，测试据此断言结算分支。
const FCatRunTransitionResult& ACatfishingGameModeBase::GetLastRunFlowResultForAutomation() const
{
	return LastRunFlowResult;
}

// 自动化种子流程：清掉旧白天计时与聚合状态，再写入最小 RunId、Phase、DayIndex 和 Revision，并开启测试旁路；这样测试能
// 覆盖 GameMode gate，而不需要真实 ST_RunFlow 资产。
void ACatfishingGameModeBase::SeedRunPhaseEntryForAutomation(const ECatRunPhase CurrentPhase,
	const int32 CurrentDayIndex, const int64 CurrentRevision)
{
	ClearDayDeadline();
	RunPublicState = FCatRunPublicState();
	RunPublicState.Phase.RunId = FGuid::NewGuid();
	RunPublicState.Phase.Phase = CurrentPhase;
	RunPublicState.Phase.DayIndex = FMath::Max(0, CurrentDayIndex);
	RunPublicState.Phase.ServerTimeAnchorSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	RunPublicState.Revision = CurrentRevision > 0 ? CurrentRevision : 1;
	bRunCommandsOpen = true;
	NightReadyEligibleIds.Reset();
	NightReadyIds.Reset();
	bAllEligibleReadyEventSent = false;
	bAutomationRunPhaseEntryBypass = true;
}
#endif
