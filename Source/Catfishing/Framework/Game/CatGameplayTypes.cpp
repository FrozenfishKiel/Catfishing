#include "Framework/Game/CatGameplayTypes.h"

#include "Character/CatCharacter.h"
#include "AbilitySystem/CatAbilityInputConfig.h"
#include "AbilitySystem/CatAbilitySettings.h"
#include "AbilitySystem/CatAbilitySystemComponent.h"
#include "Logging/CatLog.h"
#include "Online/CatOnlineSettings.h"
#include "Online/CatOnlineSubsystem.h"
#include "Camp/CatCampHubActor.h"
#include "Camp/CatCampSettings.h"
#include "Condition/CatConditionComponent.h"
#include "Condition/CatConditionSettings.h"
#include "Collection/CatRunImprintService.h"
#include "Components/StateTreeComponent.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Environment/CatConfiguredEnvironmentProvider.h"
#include "Environment/CatChumFieldReplicationComponent.h"
#include "Environment/CatChumFieldAnchor.h"
#include "Environment/CatChumFieldSubsystem.h"
#include "Environment/CatEnvironmentSettings.h"
#include "Environment/CatWaterRegion.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Equipment/CatTeamEquipmentLibrary.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Fishing/CatFishingService.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameSession.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "Items/CatItemsService.h"
#include "Net/UnrealNetwork.h"
#include "OnlineSubsystemTypes.h"
#include "Profile/CatProfileSubsystem.h"
#include "Run/CatRunSettings.h"
#include "Run/CatSacrificeCoordinator.h"
#include "Run/CatRunStateTreeEvents.h"
#include "ShopEconomy/CatShopEconomyService.h"
#include "ShopEconomy/CatShopOrderCoordinator.h"
#include "StateTree.h"
#include "Social/CatSocialService.h"
#include "TimerManager.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"

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

ACatfishingPlayerController::ACatfishingPlayerController()
{
	FishingCommandComponent = CreateDefaultSubobject<UCatFishingCommandComponent>(TEXT("FishingCommandComponent"));
}

UCatFishingCommandComponent* ACatfishingPlayerController::GetFishingCommandComponent() const
{
	return FishingCommandComponent;
}

// 构造流程：在类默认对象阶段清空 PawnClass；Frontend Controller 只承载 LocalPlayer UI，不自动生成可操控身体。
ACatFrontendGameMode::ACatFrontendGameMode()
{
	DefaultPawnClass = nullptr;
}

// 启动流程：先让引擎完成 GameMode StartPlay，再记录当前地图和无 Pawn 合同；不会调用 Online 或旅行 API。
void ACatFrontendGameMode::StartPlay()
{
	Super::StartPlay();
	UE_LOG(LogCatfishing, Log, TEXT("Event=frontend_gamemode_ready World=%s DefaultPawn=None"), *GetWorld()->GetMapName());
}

// 构造流程：一次性指定 Lake 四类框架对象和默认角色；同时创建唯一 StateTree 组件与中性 Environment provider，并关闭组件自动启动，所有 Run 真相仍留在 GameMode 实例。
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

// 启动流程：先执行引擎玩法启动并建立 NotStarted 快照，再依次校验 authority、正式运行数值、环境接口与 ST_RunFlow 资产；全部满足才显式启动 StateTree，任一步失败均保持 NotStarted/StartupFailed。
void ACatfishingGameModeBase::StartPlay()
{
	Super::StartPlay();
	RunPublicState = FCatRunPublicState();
	RunPublicState.Phase.RunId = FGuid::NewGuid();
	RunPublicState.Phase.ServerTimeAnchorSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	RunPublicState.Revision = 1;
	RefreshEnvironmentAndPublish();

	// 在任何玩家订单可达之前订阅领域变化；每次真实提交都重建整份复制快照。
	if (UWorld* World = GetWorld())
	{
		if (UCatShopEconomyService* Shop = World->GetSubsystem<UCatShopEconomyService>())
		{
			ShopPublicTransactionHandle = Shop->OnPublicTransactionCommitted.AddWeakLambda(this,
				[this](const FCatShopPublicTransaction&) { PublishShopEconomySnapshot(); });
		}
		if (UCatTeamEquipmentLibrary* Library = World->GetSubsystem<UCatTeamEquipmentLibrary>())
		{
			TeamEquipmentLibraryHandle = Library->OnLibraryChanged.AddUObject(this,
				&ThisClass::PublishTeamEquipmentLibrarySnapshot);
		}
	}
	PublishShopEconomySnapshot();
	PublishTeamEquipmentLibrarySnapshot();

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

// World 收口流程：先关闭新 Run 命令并清唯一白天截止计时，再清 HostExit ACK 计时句柄与远端等待集合；然后停止仍运行的 StateTree，最后调父类，使迟到 Task/ACK 不能进入新 World。
void ACatfishingGameModeBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bRunCommandsOpen = false;
	CloseShopForSettlementNight();
	ClearDayDeadline();
	GetWorldTimerManager().ClearTimer(HostExitAckTimerHandle);
	HostExitAckTimerHandle.Invalidate();
	PendingHostExitAckStableNetIds.Reset();
	if (UWorld* World = GetWorld())
	{
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

// PreLogin 流程：先保留引擎 GameSession/UniqueId 兼容检查；客户端提交保留的 PIE 类型始终拒绝。远端无身份只在 Editor PIE 无会话 gate 下继续等待服务器于 InitNewPlayer 分配身份，其余路径仍按 StableNetId 建立 Reserved 或拒绝重复占用。
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

	const FString StableNetIdKey = MakeStableNetIdKey(UniqueId);
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
// 2. 无有效身份时仅在 Editor PIE、NoSession 且无 Online 操作的服务器生成一次临时身份，再把有效身份交给父类写入 PlayerState；其他环境原样保留引擎行为。
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

	const FString StableNetIdKey = MakeStableNetIdKey(PlayerState->GetUniqueId());
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

// PostLogin 流程：父类尚未调用 HandleStartingNewPlayer 时先读取 PlayerState 继承 UniqueId；只有命中 Reserved 才提升 Active 并绑定 Controller，随后才进入父类生成/占有 Character。
void ACatfishingGameModeBase::PostLogin(APlayerController* NewPlayer)
{
	ACatfishingPlayerState* PlayerState = NewPlayer ? NewPlayer->GetPlayerState<ACatfishingPlayerState>() : nullptr;
	if (!PlayerState || !PlayerState->GetUniqueId().IsValid())
	{
		RejectPostLoginController(NewPlayer, TEXT("CAT_POLICY_UNDECIDED:MissingStableNetId"));
		return;
	}
	const FString StableNetIdKey = MakeStableNetIdKey(PlayerState->GetUniqueId());
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

// Logout 流程：先读取尚未被引擎清理的 PlayerState；只有 StableNetId 命中且 Active 弱引用等于 Exiting 才移除，旧连接永远不能删除同身份的新记录。PIE 无会话身份随连接立即失效并显式跳过重连 TTL，避免下一次测试误恢复旧玩家。
void ACatfishingGameModeBase::Logout(AController* Exiting)
{
	const APlayerState* PlayerState = Exiting ? Exiting->PlayerState : nullptr;
	FString ReleasedStableNetId;
	if (PlayerState && PlayerState->GetUniqueId().IsValid())
	{
		const FString StableNetIdKey = MakeStableNetIdKey(PlayerState->GetUniqueId());
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
			if (!bPieNoSessionIdentity && OnlineSettings->IsReconnectAdmissionReady() && (!bVoluntary || bKeepVoluntary) && GetWorld())
			{
				ReconnectExpiryByStableNetId.Add(StableNetIdKey,
					GetWorld()->GetTimeSeconds() + OnlineSettings->ReconnectRecordTtlSeconds);
			}
			else if (bPieNoSessionIdentity)
			{
				ReconnectExpiryByStableNetId.Remove(StableNetIdKey);
			}
			UE_LOG(LogCatOnline, Log, TEXT("Event=identity_released StableNetId=%s Result=ControllerMatched Remaining=%d Recovery=%s"),
				*MakeStableNetIdLogValue(PlayerState->GetUniqueId()), AdmissionRecords.Num(),
				bPieNoSessionIdentity ? TEXT("SkippedPieNoSession") : TEXT("PolicyUndecided"));
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
		VoluntaryLeaveStableNetIds.Add(MakeStableNetIdKey(CurrentPlayerState->GetUniqueId()));
	}
}

// StableNetId 映射流程：有效 FUniqueNetIdRepl 只在服务器内转换为字符串键；无效身份返回空，调用者必须先走 PolicyUndecided gate。
FString ACatfishingGameModeBase::MakeStableNetIdKey(const FUniqueNetIdRepl& UniqueId)
{
	return UniqueId.IsValid() ? UniqueId->ToString() : FString();
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
	const FAdmissionRecord* Record = AdmissionRecords.Find(MakeStableNetIdKey(PlayerState->GetUniqueId()));
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

// 玩法命令 gate 流程：要求 authority、本局命令门仍开放且 Controller 精确命中 Active 身份记录；Profile/Capture/Settlement/HostExit 等收口协议走各自校验，不调用本方法。
bool ACatfishingGameModeBase::CanAcceptGameplayCommand(const AController* Controller) const
{
	return HasAuthority() && bRunCommandsOpen && IsControllerActive(Controller);
}

// PostLogin 拒绝流程：优先让 GameSession 执行标准 Kick；GameSession 不可用时通知客户端回主菜单。该分支不调用父类生成 Character，也不删除无法安全匹配到本 Controller 的 Reserved 记录，避免替未裁 TTL/过期准入策略作决定。
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
	Context.StableNetId = PlayerState ? MakeStableNetIdKey(PlayerState->GetUniqueId()) : FString();
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

// 终态重放流程：命中缓存后复制首次结果，但把本次提交标记为 false 并返回 AlreadyResolved；调用者据此不会重复写额度、ready 或发送事件。
bool ACatfishingGameModeBase::TryReplayRunCommand(const FString& CacheKey, FCatRunCommandResult& OutResult) const
{
	const FCatRunCommandResult* Cached = RunCommandTerminalCache.Find(CacheKey);
	if (!Cached)
	{
		return false;
	}
	OutResult = *Cached;
	OutResult.bCommitted = false;
	OutResult.Error = ECatRunCommandError::AlreadyResolved;
	return true;
}

// 终态保存流程：只在不存在时写入首次结果；同键意外重复到达仍返回只读重放语义，不覆盖首次 Revision 或原因。
FCatRunCommandResult ACatfishingGameModeBase::CacheRunCommandResult(const FString& CacheKey, const FCatRunCommandResult& Result)
{
	if (const FCatRunCommandResult* Existing = RunCommandTerminalCache.Find(CacheKey))
	{
		FCatRunCommandResult Replay = *Existing;
		Replay.bCommitted = false;
		Replay.Error = ECatRunCommandError::AlreadyResolved;
		return Replay;
	}
	RunCommandTerminalCache.Add(CacheKey, Result);
	return Result;
}

// 阶段进入流程：先要求 authority、有效 Run 与正在启动/运行的唯一 StateTree，并在写状态前拒绝未裁的成功结算或白天参数。通过后统一清掉旧白天截止并复位玩法开关：DayActive 递增天数、清额度/终局原因、重置 Active 玩家 ready、开启 quota/fishing 并建立唯一 one-shot timer；NormalNight 冻结当前 ready 资格；两种 settlement 写对应终局原因并清 ready 集合；Ending/Ended/NotStarted 关闭新命令。最后只递增一次 Revision、保存 StateTree 可读结果并刷新 Environment/GameState 组合快照；C++ 始终不选择下一条转移边。
FCatRunTransitionResult ACatfishingGameModeBase::EnterRunPhaseFromStateTree(const ECatRunPhase NewPhase, const ECatRunTransitionReason Reason)
{
	FCatRunTransitionResult Result;
	Result.PreviousPhase = RunPublicState.Phase.Phase;
	Result.CurrentPhase = RunPublicState.Phase.Phase;
	Result.Reason = Reason;
	Result.Revision = RunPublicState.Revision;
	if (!HasAuthority() || !RunPublicState.Phase.RunId.IsValid()
		|| !RunStateTreeComponent || (!RunStateTreeComponent->IsRunning() && !bRunStartupInProgress))
	{
		Result.Error = ECatRunCommandError::StateTreeUnavailable;
		LastRunFlowResult = Result;
		return Result;
	}
	if (NewPhase == ECatRunPhase::SuccessSettlementNight && !GetDefault<UCatRunSettings>()->IsSuccessSettlementEnabled())
	{
		Result.Error = ECatRunCommandError::PolicyUndecided;
		LastRunFlowResult = Result;
		return Result;
	}
	float DayLengthSeconds = 0.0f;
	int32 DayQuotaTarget = 0;
	if (NewPhase == ECatRunPhase::DayActive
		&& !GetDefault<UCatRunSettings>()->TryGetDayParameters(DayLengthSeconds, DayQuotaTarget))
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
		if (UCatShopEconomyService* Shop = GetWorld()->GetSubsystem<UCatShopEconomyService>())
		{
			if (Shop->AdvanceShopDay(RunPublicState.Phase.DayIndex))
			{
				PublishShopEconomySnapshot();
			}
		}
		RunPublicState.QuotaProgress = 0;
		RunPublicState.QuotaTarget = DayQuotaTarget;
		RunPublicState.EndReason = ECatRunEndReason::None;
		RunPublicState.Phase.bFishingAllowed = true;
		RunPublicState.Phase.bQuotaOpen = true;
		RunPublicState.Phase.bHasDeadline = true;
		RunPublicState.Phase.DeadlineServerTimeSeconds = RunPublicState.Phase.ServerTimeAnchorSeconds + DayLengthSeconds;
		bRunCommandsOpen = true;
		NightReadyEligibleIds.Reset();
		NightReadyIds.Reset();
		bAllEligibleReadyEventSent = false;
		for (const TPair<FString, FAdmissionRecord>& Pair : AdmissionRecords)
		{
			if (Pair.Value.Phase == EAdmissionPhase::Active)
			{
				if (ACatfishingPlayerState* PlayerState = Pair.Value.Controller.IsValid()
					? Pair.Value.Controller->GetPlayerState<ACatfishingPlayerState>() : nullptr)
				{
					PlayerState->SetNextDayReadyFromAuthority(false);
				}
			}
		}
		GetWorld()->GetTimerManager().SetTimer(DayDeadlineTimerHandle, this,
			&ThisClass::HandleDayDeadlineElapsed, DayLengthSeconds, false);
		break;
	}
	case ECatRunPhase::NormalNight:
		bRunCommandsOpen = true;
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

// 额度提交流程：服务器重建身份并先查幂等缓存，再校验 gate/Phase/Revision/载荷；首次写入更新总量与 Revision，达标时先关闭写口和计时器、发布快照，再向 StateTree 发送 QuotaReached。
FCatRunCommandResult ACatfishingGameModeBase::SubmitQuotaContribution(AController* RequestingController, const FCatQuotaContributionCommand& Command)
{
	FCatQuotaContributionCommand ServerCommand = Command;
	if (!FillServerCommandIdentity(RequestingController, ServerCommand.Context))
	{
		return MakeRunCommandResult(Command.Context.RequestId, false, ECatRunCommandError::InvalidIdentity);
	}
	return SubmitQuotaContributionInternal(ServerCommand);
}

// 献祭预检流程：只读验证服务器身份键、命令 gate、Phase、Revision、正贡献与潜在达标事件依赖；不写终态缓存，因 Items 尚可安全取消预留。
FCatRunCommandResult ACatfishingGameModeBase::ValidateCommittedQuotaContributionFromCoordinator(const FCatQuotaContributionCommand& Command) const
{
	if (Command.Context.StableNetId.IsEmpty())
	{
		return MakeRunCommandResult(Command.Context.RequestId, false, ECatRunCommandError::InvalidIdentity);
	}
	if (!Command.Context.RequestId.IsValid() || Command.Contribution <= 0)
	{
		return MakeRunCommandResult(Command.Context.RequestId, false, ECatRunCommandError::InvalidPayload);
	}
	if (!bRunCommandsOpen)
	{
		return MakeRunCommandResult(Command.Context.RequestId, false, ECatRunCommandError::CommandsClosed);
	}
	if (RunPublicState.Phase.Phase != ECatRunPhase::DayActive || !RunPublicState.Phase.bQuotaOpen)
	{
		return MakeRunCommandResult(Command.Context.RequestId, false, ECatRunCommandError::InvalidPhase);
	}
	if (Command.Context.ExpectedRevision != RunPublicState.Revision)
	{
		return MakeRunCommandResult(Command.Context.RequestId, false, ECatRunCommandError::RevisionConflict);
	}
	const int64 NewProgress = static_cast<int64>(RunPublicState.QuotaProgress) + Command.Contribution;
	if (NewProgress > MAX_int32)
	{
		return MakeRunCommandResult(Command.Context.RequestId, false, ECatRunCommandError::InvalidPayload);
	}
	if (NewProgress >= RunPublicState.QuotaTarget && (!RunStateTreeComponent || !RunStateTreeComponent->IsRunning()))
	{
		return MakeRunCommandResult(Command.Context.RequestId, false, ECatRunCommandError::StateTreeUnavailable);
	}
	// 预检成功只表示当前写口可接受；Items 尚未提交、Run 也没有写入，因此 bCommitted 必须保持 false。
	return MakeRunCommandResult(Command.Context.RequestId, false, ECatRunCommandError::None);
}

// 献祭额度提交流程：只接受协调器已从 Items 记录冻结的服务器身份与贡献，并复用玩家额度完全相同的幂等/Revision/StateTree 实现。
FCatRunCommandResult ACatfishingGameModeBase::SubmitCommittedQuotaContributionFromCoordinator(const FCatQuotaContributionCommand& Command)
{
	if (Command.Context.StableNetId.IsEmpty())
	{
		return MakeRunCommandResult(Command.Context.RequestId, false, ECatRunCommandError::InvalidIdentity);
	}
	return SubmitQuotaContributionInternal(Command);
}

// 额度内部流程：先查完整幂等缓存，再校验 gate/Phase/Revision/载荷；首次写入更新总量与 Revision，达标时关闭写口和计时器并发送唯一 StateTree 事件。
FCatRunCommandResult ACatfishingGameModeBase::SubmitQuotaContributionInternal(const FCatQuotaContributionCommand& ServerCommand)
{
	if (!ServerCommand.Context.RequestId.IsValid())
	{
		return MakeRunCommandResult(ServerCommand.Context.RequestId, false, ECatRunCommandError::InvalidPayload);
	}
	const FString CacheKey = MakeRunCommandCacheKey(ServerCommand.Context.StableNetId,
		ECatRunCommandType::QuotaContribution, ServerCommand.Context.RequestId);
	FCatRunCommandResult Replay;
	if (TryReplayRunCommand(CacheKey, Replay))
	{
		return Replay;
	}
	if (!bRunCommandsOpen)
	{
		return CacheRunCommandResult(CacheKey, MakeRunCommandResult(ServerCommand.Context.RequestId, false, ECatRunCommandError::CommandsClosed));
	}
	if (RunPublicState.Phase.Phase != ECatRunPhase::DayActive || !RunPublicState.Phase.bQuotaOpen)
	{
		return CacheRunCommandResult(CacheKey, MakeRunCommandResult(ServerCommand.Context.RequestId, false, ECatRunCommandError::InvalidPhase));
	}
	if (ServerCommand.Context.ExpectedRevision != RunPublicState.Revision)
	{
		return CacheRunCommandResult(CacheKey, MakeRunCommandResult(ServerCommand.Context.RequestId, false, ECatRunCommandError::RevisionConflict));
	}
	const int64 NewProgress = static_cast<int64>(RunPublicState.QuotaProgress) + ServerCommand.Contribution;
	if (ServerCommand.Contribution <= 0 || NewProgress > MAX_int32)
	{
		return CacheRunCommandResult(CacheKey, MakeRunCommandResult(ServerCommand.Context.RequestId, false, ECatRunCommandError::InvalidPayload));
	}
	const bool bReachesQuota = NewProgress >= RunPublicState.QuotaTarget;
	if (bReachesQuota && (!RunStateTreeComponent || !RunStateTreeComponent->IsRunning()))
	{
		return CacheRunCommandResult(CacheKey, MakeRunCommandResult(ServerCommand.Context.RequestId, false, ECatRunCommandError::StateTreeUnavailable));
	}

	RunPublicState.QuotaProgress = static_cast<int32>(NewProgress);
	++RunPublicState.Revision;
	ECatRunTransitionReason TransitionReason = ECatRunTransitionReason::None;
	if (bReachesQuota)
	{
		TransitionReason = ECatRunTransitionReason::QuotaReached;
		RunPublicState.Phase.bQuotaOpen = false;
		RunPublicState.Phase.bFishingAllowed = false;
		ClearDayDeadline();
	}
	RefreshEnvironmentAndPublish();
	FCatRunCommandResult Result = MakeRunCommandResult(ServerCommand.Context.RequestId, true, ECatRunCommandError::None, TransitionReason);
	Result = CacheRunCommandResult(CacheKey, Result);
	if (bReachesQuota)
	{
		SendRunStateTreeEvent(CatRunStateTreeEvents::QuotaReached, TransitionReason);
	}
	return Result;
}

// 翻天确认流程：服务器重建身份并按幂等键/Revision/普通夜资格校验，只在事实变化时改 PlayerState 与 Revision；最后一名合资格玩家确认时只发送 AllEligibleReady 事件。
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
	FCatRunCommandResult Replay;
	if (TryReplayRunCommand(CacheKey, Replay))
	{
		return Replay;
	}
	if (!bRunCommandsOpen)
	{
		return CacheRunCommandResult(CacheKey, MakeRunCommandResult(ServerCommand.Context.RequestId, false, ECatRunCommandError::CommandsClosed));
	}
	if (RunPublicState.Phase.Phase != ECatRunPhase::NormalNight || bAllEligibleReadyEventSent)
	{
		return CacheRunCommandResult(CacheKey, MakeRunCommandResult(ServerCommand.Context.RequestId, false, ECatRunCommandError::InvalidPhase));
	}
	if (ServerCommand.Context.ExpectedRevision != RunPublicState.Revision)
	{
		return CacheRunCommandResult(CacheKey, MakeRunCommandResult(ServerCommand.Context.RequestId, false, ECatRunCommandError::RevisionConflict));
	}
	if (!NightReadyEligibleIds.Contains(ServerCommand.Context.StableNetId))
	{
		const ECatRunCommandError Error = GetDefault<UCatRunSettings>()->CanAdmitLateNightReady()
			? ECatRunCommandError::NotEligible : ECatRunCommandError::PolicyUndecided;
		return CacheRunCommandResult(CacheKey, MakeRunCommandResult(ServerCommand.Context.RequestId, false, Error));
	}

	ACatfishingPlayerState* PlayerState = RequestingController
		? RequestingController->GetPlayerState<ACatfishingPlayerState>() : nullptr;
	if (!PlayerState)
	{
		return CacheRunCommandResult(CacheKey, MakeRunCommandResult(ServerCommand.Context.RequestId, false, ECatRunCommandError::InvalidIdentity));
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
	FCatRunCommandResult Result = CacheRunCommandResult(CacheKey,
		MakeRunCommandResult(ServerCommand.Context.RequestId, true, ECatRunCommandError::None));
	EvaluateAllEligibleReady();
	return Result;
}

// 结算完成流程：协调器使用专用私有身份键参与同一终态缓存，校验结算 Phase 与 Revision 后只提交 SettlementComplete 事件；目标 Ending 仍由资产选择。
FCatRunCommandResult ACatfishingGameModeBase::CompleteSettlementFromCoordinator(const FGuid RequestId, const int64 ExpectedRevision)
{
	if (!RequestId.IsValid())
	{
		return MakeRunCommandResult(RequestId, false, ECatRunCommandError::InvalidPayload);
	}
	const FString CacheKey = MakeRunCommandCacheKey(TEXT("RunCoordinator"), ECatRunCommandType::SettlementComplete, RequestId);
	FCatRunCommandResult Replay;
	if (TryReplayRunCommand(CacheKey, Replay))
	{
		return Replay;
	}
	if (!bRunCommandsOpen)
	{
		return CacheRunCommandResult(CacheKey, MakeRunCommandResult(RequestId, false, ECatRunCommandError::CommandsClosed));
	}
	if (RunPublicState.Phase.Phase != ECatRunPhase::FailureSettlementNight
		&& RunPublicState.Phase.Phase != ECatRunPhase::SuccessSettlementNight)
	{
		return CacheRunCommandResult(CacheKey, MakeRunCommandResult(RequestId, false, ECatRunCommandError::InvalidPhase));
	}
	if (ExpectedRevision != RunPublicState.Revision)
	{
		return CacheRunCommandResult(CacheKey, MakeRunCommandResult(RequestId, false, ECatRunCommandError::RevisionConflict));
	}
	const FCatRunCommandResult SuccessResult = MakeRunCommandResult(RequestId, true,
		ECatRunCommandError::None, ECatRunTransitionReason::SettlementComplete);
	if (!SendRunStateTreeEvent(CatRunStateTreeEvents::SettlementComplete, ECatRunTransitionReason::SettlementComplete))
	{
		return CacheRunCommandResult(CacheKey, MakeRunCommandResult(RequestId, false, ECatRunCommandError::StateTreeUnavailable));
	}
	return CacheRunCommandResult(CacheKey, SuccessResult);
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

// 全员确认求值流程：仅在普通夜、集合非空、尚未发事件且 ready 覆盖合资格集合时关闭窗口并发送一次事件；本方法不改写 Phase。
void ACatfishingGameModeBase::EvaluateAllEligibleReady()
{
	if (RunPublicState.Phase.Phase != ECatRunPhase::NormalNight || bAllEligibleReadyEventSent
		|| NightReadyEligibleIds.IsEmpty() || NightReadyIds.Num() != NightReadyEligibleIds.Num())
	{
		return;
	}
	bAllEligibleReadyEventSent = true;
	if (!SendRunStateTreeEvent(CatRunStateTreeEvents::AllEligibleReady, ECatRunTransitionReason::AllEligibleReady))
	{
		bAllEligibleReadyEventSent = false;
	}
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

// 白天截止流程：只消费仍开放的同一 DayActive，先关闭钓鱼/额度并递增 Revision、发布快照，再向 StateTree 发送 QuotaFailed；夜晚不创建新计时器。
void ACatfishingGameModeBase::HandleDayDeadlineElapsed()
{
	DayDeadlineTimerHandle.Invalidate();
	if (!HasAuthority() || !bRunCommandsOpen || RunPublicState.Phase.Phase != ECatRunPhase::DayActive
		|| !RunPublicState.Phase.bQuotaOpen)
	{
		return;
	}
	RunPublicState.Phase.bFishingAllowed = false;
	RunPublicState.Phase.bQuotaOpen = false;
	RunPublicState.Phase.bHasDeadline = false;
	RunPublicState.Phase.DeadlineServerTimeSeconds = 0.0;
	RunPublicState.Phase.ServerTimeAnchorSeconds = GetWorld()->GetTimeSeconds();
	++RunPublicState.Revision;
	RefreshEnvironmentAndPublish();
	SendRunStateTreeEvent(CatRunStateTreeEvents::QuotaFailed, ECatRunTransitionReason::QuotaFailed);
}

// 环境发布流程：以当前 Phase 与 Revision 调用只读 provider；成功时替换同 Revision 环境 DTO，随后无论环境是否成功都把 Run 唯一公开聚合写入 GameState，失败只记录诊断而不制造替代天气。
bool ACatfishingGameModeBase::RefreshEnvironmentAndPublish()
{
	const ICatEnvironmentProvider* Provider = Cast<ICatEnvironmentProvider>(EnvironmentProvider);
	const FCatEnvironmentResult EnvironmentResult = Provider
		? Provider->EvaluateEnvironment(RunPublicState.Phase, RunPublicState.Revision)
		: FCatEnvironmentResult();
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
		SubmitNaturalChumFieldIfConfigured();
	}
	else
	{
		UE_LOG(LogCatRun, Error, TEXT("Event=environment_evaluation_failed RunId=%s Revision=%lld Error=%s"),
			*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Revision,
			Provider ? *EnvironmentResult.Error : TEXT("ProviderUnavailable"));
	}
	ACatfishingGameState* CatGameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
	if (CatGameState)
	{
		CatGameState->SetRunPublicStateFromAuthority(RunPublicState);
	}
	return EnvironmentResult.bSucceeded && CatGameState != nullptr;
}

// 自然聚鱼流程：按当前 Day+Event 去重，读取 Environment 显式区域/三轴，扫描唯一同 ID WaterRegion；构造系统身份命令并提交同一聚鱼写口，只有 committed 才记录去重键。
void ACatfishingGameModeBase::SubmitNaturalChumFieldIfConfigured()
{
	if (!HasAuthority() || !RunPublicState.Environment.bHasActiveEvent || !GetWorld())
	{
		return;
	}
	const FString EventKey = FString::Printf(TEXT("%d|%s"), RunPublicState.Phase.DayIndex,
		*RunPublicState.Environment.ActiveEventId.ToString());
	if (SubmittedNaturalChumFieldKeys.Contains(EventKey))
	{
		return;
	}
	FName ChumDefinitionId;
	FName AnchorId;
	if (!GetDefault<UCatEnvironmentSettings>()->TryGetNaturalChumField(ChumDefinitionId, AnchorId))
	{
		return;
	}
	UCatEquipmentDefinition* Definition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(ChumDefinitionId);
	if (!Definition || Definition->Kind != ECatEquipmentKind::Chum || !Definition->ChumInfluence.IsRuntimeReady())
	{
		return;
	}
	ACatChumFieldAnchor* Match = nullptr;
	for (TActorIterator<ACatChumFieldAnchor> It(GetWorld()); It; ++It)
	{
		if (It->AnchorId == AnchorId)
		{
			if (Match)
			{
				UE_LOG(LogCatEnvironment, Error, TEXT("Event=natural_chum_rejected Day=%d EnvironmentEvent=%s Anchor=%s Error=AmbiguousAnchor"),
					RunPublicState.Phase.DayIndex, *RunPublicState.Environment.ActiveEventId.ToString(), *AnchorId.ToString());
				return;
			}
			Match = *It;
		}
	}
	UCatChumFieldSubsystem* Fields = GetWorld()->GetSubsystem<UCatChumFieldSubsystem>();
	if (!Match || !Match->ExpectedWaterRegionHandle.IsValid() || !Fields)
	{
		return;
	}
	FCatPrepareChumFieldRequest Request;
	Request.StableNetId = TEXT("Environment");
	Request.Command.RequestId = FGuid::NewGuid();
	Request.Command.ExpectedWaterRegionHandle = Match->ExpectedWaterRegionHandle;
	Request.Command.ChumDefinitionId = ChumDefinitionId;
	Request.Command.Quantity = 1;
	Request.Command.ClientCandidateWorldPoint = Match->GetActorLocation();
	Request.ServerCorrectedCenter = Match->GetActorLocation();
	Request.Influence = Definition->ChumInfluence;
	Request.Source = ECatChumFieldSource::NaturalEvent;
	Request.ServerTime = GetWorld()->GetTimeSeconds();
	const FCatPrepareChumFieldResult Prepared = Fields->PrepareField(Request);
	if (!Prepared.bPrepared) return;
	const FCatPlaceChumResult Result = Fields->ActivatePreparedFieldDeferred(Prepared.CommitToken, 0);
	Fields->StoreTerminalResult(Request.StableNetId, Result);
	if (Result.bCommitted)
	{
		SubmittedNaturalChumFieldKeys.Add(EventKey);
		Fields->PublishActivatedField(Result.FieldId);
	}
	UE_LOG(LogCatEnvironment, Log, TEXT("Event=natural_chum_terminal RequestId=%s Day=%d EnvironmentEvent=%s Definition=%s Anchor=%s Committed=%s Error=%s Revision=%lld"),
		*Request.Command.RequestId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Phase.DayIndex,
		*RunPublicState.Environment.ActiveEventId.ToString(), *ChumDefinitionId.ToString(), *AnchorId.ToString(),
		Result.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Error),
		Result.ChumFieldSetRevision);
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

// Host teardown 流程：先重放已 Ready 的新 Online 关联键，Pending 期则只接受原 RequestId/epoch；首次请求必须同时具备 Imprint/牺牲协调器与正超时。领域协调器按 Social→Items 等顺序关不可逆命令，Imprint 先最终重投 Grant，然后才关 Run/Timer/StateTree、发 HostExit 并等远端 Destroy ACK；只有远端 ACK 与 durable Grant ACK 全齐才 Ready，超时会报告风险而不伪造 ACK。
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
	if (!SacrificeCoordinator->PrepareForRunTeardown())
	{
		Result.Status = ECatRunTeardownStatus::Failed;
		Result.Error = ECatRunCommandError::TeardownFailed;
		return Result;
	}
	// 先完成最终 Grant 重投，再发送远端退出 RPC；同一 Controller 上的 Reliable RPC 顺序保证 Grant 在 Destroy 通知之前到达。
	const bool bGrantAcksComplete = ImprintService->PrepareForRunTeardown();

	bRunCommandsOpen = false;
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

// Host exit ACK 流程：验证当前等待、RequestId 与 Active Controller 身份后移除精确 StableNetId；最后一个远端 ACK 到达后还要复核 durable Grant ACK，二者都齐才提前完成。
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
	const FString StableNetId = MakeStableNetIdKey(CurrentPlayerState->GetUniqueId());
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

// Host exit ACK 完成流程：幂等清统一计时器并发布真正的 teardown complete；超时只释放有界等待，日志仍保留缺失 Destroy/Grant ACK，绝不改写它们为已确认。
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

void ACatfishingGameModeBase::CloseShopForSettlementNight()
{
	if (UCatShopEconomyService* Shop = GetWorld() ? GetWorld()->GetSubsystem<UCatShopEconomyService>() : nullptr)
	{
		Shop->CloseCommands();
	}
	if (UCatTeamEquipmentLibrary* Library =
		GetWorld() ? GetWorld()->GetSubsystem<UCatTeamEquipmentLibrary>() : nullptr)
	{
		Library->CloseCommands();
	}
}

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

void ACatfishingGameModeBase::PublishTeamEquipmentLibrarySnapshot()
{
	ACatfishingGameState* CatGameState = GetGameState<ACatfishingGameState>();
	UCatTeamEquipmentLibrary* Library =
		GetWorld() ? GetWorld()->GetSubsystem<UCatTeamEquipmentLibrary>() : nullptr;
	if (CatGameState && Library)
	{
		CatGameState->SetTeamEquipmentLibraryFromAuthority(Library->GetSnapshot());
	}
}

APlayerState* ACatfishingGameModeBase::ResolvePlayerStateByStableNetId(const FString& StableNetId) const
{
	const FAdmissionRecord* Record = StableNetId.IsEmpty() ? nullptr : AdmissionRecords.Find(StableNetId);
	return Record && Record->Phase == EAdmissionPhase::Active && Record->Controller.IsValid()
		? Record->Controller->PlayerState : nullptr;
}

// GameState 开始流程：先完成父类注册，再记录实际类型；Run 快照只由 authority GameMode setter 写入。
ACatfishingGameState::ACatfishingGameState()
{
	ChumFieldReplication = CreateDefaultSubobject<UCatChumFieldReplicationComponent>(TEXT("ChumFieldReplication"));
}

void ACatfishingGameState::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogCatfishing, Log, TEXT("Event=gamestate_beginplay Class=%s"), *GetClass()->GetName());
}

// GameState 复制注册流程：先保留父类网络字段，再注册整结构 RunPublicState 与最近 HelpSignal；两者各带 Revision，客户端只经对应 RepNotify 重读完整快照。
void ACatfishingGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, RunPublicState);
	DOREPLIFETIME(ThisClass, LastHelpSignal);
	DOREPLIFETIME(ThisClass, ShopEconomySnapshot);
	DOREPLIFETIME(ThisClass, TeamEquipmentLibrary);
}

// Run 快照写入流程：只接受 authority 实例，把 GameMode 提供的完整 DTO 一次替换并请求立即网络更新；客户端调用不会改本地副本。
void ACatfishingGameState::SetRunPublicStateFromAuthority(const FCatRunPublicState& NewState)
{
	if (!HasAuthority())
	{
		return;
	}
	RunPublicState = NewState;
	ForceNetUpdate();
	OnRunPublicStateChanged.Broadcast();
}

// Run 快照读取流程：返回当前本机观察到的只读组合事实，不补算时间或预测下一阶段。
const FCatRunPublicState& ACatfishingGameState::GetRunPublicState() const
{
	return RunPublicState;
}

// 求助发布流程：只接受 authority，复制完整信号并强制网络更新；GameState 不解释附近范围或自动生成任务。
void ACatfishingGameState::SetHelpSignalFromAuthority(const FCatHelpSignalSnapshot& NewSignal)
{
	if (!HasAuthority())
	{
		return;
	}
	LastHelpSignal = NewSignal;
	ForceNetUpdate();
	OnHelpSignalChanged.Broadcast();
}

// 求助读取流程：返回服务器最终值或客户端最近复制值；消费者自行按全局/范围做表现过滤。
const FCatHelpSignalSnapshot& ACatfishingGameState::GetLastHelpSignal() const
{
	return LastHelpSignal;
}

void ACatfishingGameState::SetShopEconomySnapshotFromAuthority(
	const FCatShopPublicEconomySnapshot& NewSnapshot)
{
	if (!HasAuthority())
	{
		return;
	}
	ShopEconomySnapshot = NewSnapshot;
	ForceNetUpdate();
	OnShopEconomySnapshotChanged.Broadcast();
}

const FCatShopPublicEconomySnapshot& ACatfishingGameState::GetShopEconomySnapshot() const
{
	return ShopEconomySnapshot;
}

void ACatfishingGameState::SetTeamEquipmentLibraryFromAuthority(
	const FCatTeamEquipmentLibrarySnapshot& NewSnapshot)
{
	if (!HasAuthority())
	{
		return;
	}
	TeamEquipmentLibrary = NewSnapshot;
	ForceNetUpdate();
	OnTeamEquipmentLibraryChanged.Broadcast();
}

const FCatTeamEquipmentLibrarySnapshot& ACatfishingGameState::GetTeamEquipmentLibrary() const
{
	return TeamEquipmentLibrary;
}

UCatChumFieldReplicationComponent* ACatfishingGameState::GetChumFieldReplicationFromAuthority()
{
	return HasAuthority() ? ChumFieldReplication : nullptr;
}

// Run 快照复制回调流程：只记录新 Revision/Phase 供诊断；UI 与玩法继续通过 getter 读取，不在客户端推进 StateTree。
void ACatfishingGameState::OnRep_RunPublicState()
{
	OnRunPublicStateChanged.Broadcast();
	UE_LOG(LogCatRun, Verbose, TEXT("Event=run_snapshot_received RunId=%s Revision=%lld Phase=%s Day=%d"),
		*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Revision,
		*UEnum::GetValueAsString(RunPublicState.Phase.Phase), RunPublicState.Phase.DayIndex);
}

// 求助复制回调流程：只记录结构化诊断；客户端不自动进入 Fishing、救援或任务状态。
void ACatfishingGameState::OnRep_HelpSignal()
{
	OnHelpSignalChanged.Broadcast();
	UE_LOG(LogCatfishing, Verbose, TEXT("Event=help_signal_received Kind=%s Revision=%lld Global=%s"),
		*UEnum::GetValueAsString(LastHelpSignal.Kind), LastHelpSignal.Revision,
		LastHelpSignal.bGlobal ? TEXT("true") : TEXT("false"));
}

void ACatfishingGameState::OnRep_ShopEconomySnapshot()
{
	OnShopEconomySnapshotChanged.Broadcast();
	UE_LOG(LogCatfishing, Verbose,
		TEXT("Event=shop_economy_snapshot_received Balance=%d WalletRevision=%lld Transactions=%d"),
		ShopEconomySnapshot.Balance, ShopEconomySnapshot.WalletRevision,
		ShopEconomySnapshot.Transactions.Num());
}

void ACatfishingGameState::OnRep_TeamEquipmentLibrary()
{
	OnTeamEquipmentLibraryChanged.Broadcast();
	UE_LOG(LogCatfishing, Verbose,
		TEXT("Event=team_equipment_library_received Revision=%lld Instances=%d"),
		TeamEquipmentLibrary.Revision, TeamEquipmentLibrary.Instances.Num());
}

// PlayerState 开始流程：先完成父类注册，再记录继承 UniqueId 的有效性和策略允许的日志表示；不复制第二份 StableNetId 或恢复白名单。
void ACatfishingPlayerState::BeginPlay()
{
	Super::BeginPlay();
	const FString StableNetId = GetUniqueId().IsValid()
		&& GetDefault<UCatOnlineSettings>()->StableNetIdExposure == ECatPolicyDecision::Enabled
		? GetUniqueId()->ToString()
		: GetUniqueId().IsValid() ? TEXT("Valid(Redacted)") : TEXT("Invalid");
	UE_LOG(LogCatOnline, Log, TEXT("Event=playerstate_beginplay Class=%s StableNetId=%s Authority=%s"),
		*GetClass()->GetName(), *StableNetId, HasAuthority() ? TEXT("true") : TEXT("false"));
}

// PlayerState 复制注册流程：保留父类 UniqueId 等身份字段，再注册个人 ready 与主动公开鱼图鉴摘要；前者不包含全员转移判断，后者不包含 Profile 私有记录。
void ACatfishingPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, bReadyForNextDay);
	DOREPLIFETIME(ThisClass, PublicFishCollection);
}

// 个人 ready 写入流程：仅 authority 可修改；值变化后强制网络更新，使本人的最终确认及时到达客户端。
void ACatfishingPlayerState::SetNextDayReadyFromAuthority(const bool bNewReady)
{
	if (!HasAuthority() || bReadyForNextDay == bNewReady)
	{
		return;
	}
	bReadyForNextDay = bNewReady;
	ForceNetUpdate();
}

// 个人 ready 读取流程：返回服务器最终值或客户端最近复制值，不推导本人是否仍在本夜资格集合。
bool ACatfishingPlayerState::IsReadyForNextDay() const
{
	return bReadyForNextDay;
}

// 公开图鉴写入流程：仅 authority 接受有限数量、唯一非空鱼种、合法状态和有限非负数值；验证全部通过后整体替换并强制网络更新。
bool ACatfishingPlayerState::SetPublicFishCollectionFromAuthority(const TArray<FCatFishCollectionRecord>& Records)
{
	if (!HasAuthority() || Records.Num() > 512)
	{
		return false;
	}
	TSet<FName> UniqueFishIds;
	for (const FCatFishCollectionRecord& Record : Records)
	{
		if (Record.FishDefinitionId.IsNone() || Record.State == ECatFishCollectionState::Unknown
			|| !FMath::IsFinite(Record.BestWeightKilograms) || Record.BestWeightKilograms < 0.0
			|| Record.EncounterCount < 0 || UniqueFishIds.Contains(Record.FishDefinitionId))
		{
			return false;
		}
		UniqueFishIds.Add(Record.FishDefinitionId);
	}
	PublicFishCollection = Records;
	ForceNetUpdate();
	return true;
}

// 公开图鉴读取流程：返回服务器最终值或客户端最近复制摘要；没有任何接口返回别人的相册或隐藏记录。
const TArray<FCatFishCollectionRecord>& ACatfishingPlayerState::GetPublicFishCollection() const
{
	return PublicFishCollection;
}

// 装备解锁证明读取流程：None 表示定义明确声明 starter；非空 UnlockId 在服务端 durable Profile/授权证明尚未接线前一律返回 false，客户端本地 SaveGame 不能提升权限。
bool ACatfishingPlayerState::HasServerAuthorizedEquipmentUnlock(const FName UnlockId) const
{
	return UnlockId.IsNone();
}

// 个人 ready 复制回调流程：只记录最终布尔值；客户端不向 GameMode 回发确认，也不计算全员完成。
void ACatfishingPlayerState::OnRep_ReadyForNextDay()
{
	UE_LOG(LogCatRun, Verbose, TEXT("Event=next_day_ready_received Ready=%s"), bReadyForNextDay ? TEXT("true") : TEXT("false"));
}

// 接管流程：先让父类建立 Pawn 所有权与输入链，再记录最终双方类型；不缓存 Pawn，也不从 Controller 复制 StableNetId 到 Character。
void ACatfishingPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (FishingCommandComponent)
	{
		FishingCommandComponent->ResetTransientCommandState();
	}
	RefreshAbilityInputRoute();
	bSprintRequested = false;
	ApplySprintSpeed(InPawn, false);
	UE_LOG(LogCatfishing, Log, TEXT("Event=controller_possessed Controller=%s Pawn=%s"),
		*GetClass()->GetName(), InPawn ? *InPawn->GetClass()->GetName() : TEXT("None"));
}

// Pawn 复制刷新流程：owning client 在拿到新 Pawn 后从普通速度开始，旧 Pawn 的按键意图不会穿透重生或旅行边界。
void ACatfishingPlayerController::OnRep_Pawn()
{
	Super::OnRep_Pawn();
	if (FishingCommandComponent)
	{
		FishingCommandComponent->ResetTransientCommandState();
	}
	RefreshAbilityInputRoute();
	bSprintRequested = false;
	ApplySprintSpeed(GetPawn(), false);
}

// 本地输入启动流程：父类完成 Actor 生命周期后，幂等安装本 Controller 配置的唯一玩法输入层。
void ACatfishingPlayerController::BeginPlay()
{
	Super::BeginPlay();
	ApplyInputMappingContext();
}

// 输入绑定流程：只接受项目配置的 EnhancedInputComponent；未接入的 Action 独立跳过，不阻塞其余输入。
void ACatfishingPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	ApplyInputMappingContext();

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EnhancedInput)
	{
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=controller_input_binding_failed Controller=%s Error=EnhancedInputComponentUnavailable"),
			*GetClass()->GetName());
		return;
	}

	if (MoveAction)
	{
		EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ThisClass::Move);
	}
	if (LookAction)
	{
		EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &ThisClass::Look);
	}
	if (JumpAction)
	{
		EnhancedInput->BindAction(JumpAction, ETriggerEvent::Started, this, &ThisClass::StartJump);
		EnhancedInput->BindAction(JumpAction, ETriggerEvent::Completed, this, &ThisClass::StopJump);
		EnhancedInput->BindAction(JumpAction, ETriggerEvent::Canceled, this, &ThisClass::StopJump);
	}
	if (SprintAction)
	{
		EnhancedInput->BindAction(SprintAction, ETriggerEvent::Started, this, &ThisClass::StartSprint);
		EnhancedInput->BindAction(SprintAction, ETriggerEvent::Completed, this, &ThisClass::StopSprint);
		EnhancedInput->BindAction(SprintAction, ETriggerEvent::Canceled, this, &ThisClass::StopSprint);
	}

	const UCatAbilitySettings* AbilitySettings = GetDefault<UCatAbilitySettings>();
	const UCatAbilityInputConfig* AbilityInputConfig = AbilitySettings && AbilitySettings->IsFishingRuntimeReady()
		? AbilitySettings->AbilityInputConfig.LoadSynchronous() : nullptr;
	if (AbilityInputConfig && AbilityBoundInputComponent.Get() != EnhancedInput)
	{
		for (const FCatAbilityInputAction& Entry : AbilityInputConfig->AbilityInputActions)
		{
			EnhancedInput->BindAction(Entry.InputAction, ETriggerEvent::Started,
				this, &ThisClass::AbilityInputTagPressed, Entry.InputTag);
			EnhancedInput->BindAction(Entry.InputAction, ETriggerEvent::Completed,
				this, &ThisClass::AbilityInputTagReleased, Entry.InputTag);
			EnhancedInput->BindAction(Entry.InputAction, ETriggerEvent::Canceled,
				this, &ThisClass::AbilityInputTagReleased, Entry.InputTag);
		}
		AbilityBoundInputComponent = EnhancedInput;
	}
}

void ACatfishingPlayerController::PostProcessInput(const float DeltaTime, const bool bGamePaused)
{
	Super::PostProcessInput(DeltaTime, bGamePaused);
	if (UCatAbilitySystemComponent* AbilitySystem = GetCurrentCatAbilitySystemComponent())
	{
		AbilitySystem->ProcessAbilityInput(DeltaTime, bGamePaused);
	}
}

// Pawn 断开流程：Controller 仍持有 Pawn 时先恢复普通速度并清意图，再交还父类断开占有。
void ACatfishingPlayerController::OnUnPossess()
{
	if (UCatAbilitySystemComponent* AbilitySystem = RoutedAbilitySystem.Get())
	{
		AbilitySystem->ResetAbilityInput();
	}
	if (UCatAbilitySystemComponent* AbilitySystem = GetCurrentCatAbilitySystemComponent();
		AbilitySystem && AbilitySystem != RoutedAbilitySystem.Get())
	{
		AbilitySystem->ResetAbilityInput();
	}
	RoutedAbilitySystem.Reset();
	if (FishingCommandComponent)
	{
		FishingCommandComponent->ResetTransientCommandState();
	}
	ApplySprintSpeed(GetPawn(), false);
	bSprintRequested = false;
	Super::OnUnPossess();
}

// 输入清理流程：只撤销本 Controller 安装的 Context，再交还父类销毁；不干扰诊断或 UI 输入层。
void ACatfishingPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UCatAbilitySystemComponent* AbilitySystem = RoutedAbilitySystem.Get())
	{
		AbilitySystem->ResetAbilityInput();
	}
	RoutedAbilitySystem.Reset();
	AbilityBoundInputComponent.Reset();
	if (FishingCommandComponent)
	{
		FishingCommandComponent->ResetTransientCommandState();
	}
	ApplySprintSpeed(GetPawn(), false);
	bSprintRequested = false;
	RemoveInputMappingContext();
	Super::EndPlay(EndPlayReason);
}

// Mapping Context 安装流程：仅本地 Controller 从自身 LocalPlayer 取 Enhanced Input 子系统，重复调用保持幂等。
void ACatfishingPlayerController::ApplyInputMappingContext()
{
	if (!IsLocalController() || !DefaultMappingContext || AppliedMappingContext)
	{
		return;
	}

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!InputSubsystem)
	{
		return;
	}

	InputSubsystem->AddMappingContext(DefaultMappingContext, InputMappingPriority);
	AppliedInputSubsystem = InputSubsystem;
	AppliedMappingContext = DefaultMappingContext;
}

// Mapping Context 移除流程：使用安装时保存的同一子系统和资产成对清理，World teardown 下弱引用失效也安全。
void ACatfishingPlayerController::RemoveInputMappingContext()
{
	if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = AppliedInputSubsystem.Get();
		InputSubsystem && AppliedMappingContext)
	{
		InputSubsystem->RemoveMappingContext(AppliedMappingContext);
	}
	AppliedInputSubsystem.Reset();
	AppliedMappingContext = nullptr;
}

// 移动输入流程：以控制器水平朝向为基准，Y 驱动前后、X 驱动左右；Pawn 缺失时不制造旁路移动状态。
void ACatfishingPlayerController::Move(const FInputActionValue& Value)
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return;
	}

	const FVector2D Movement = Value.Get<FVector2D>();
	const FRotator YawRotation(0.0, GetControlRotation().Yaw, 0.0);
	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
	ControlledPawn->AddMovementInput(ForwardDirection, Movement.Y);
	ControlledPawn->AddMovementInput(RightDirection, Movement.X);
}

// 视角输入流程：输入资产只提供二维意图，轴反转、缩放和死区由 Mapping Context 的 Modifier 决定。
void ACatfishingPlayerController::Look(const FInputActionValue& Value)
{
	const FVector2D LookAxis = Value.Get<FVector2D>();
	AddYawInput(LookAxis.X);
	AddPitchInput(LookAxis.Y);
}

// 跳跃按下流程：只对当前已占有的 Character 生效，普通 Pawn 不伪造跳跃实现。
void ACatfishingPlayerController::StartJump()
{
	if (ACharacter* ControlledCharacter = Cast<ACharacter>(GetPawn()))
	{
		ControlledCharacter->Jump();
	}
}

// 跳跃释放流程：Completed 与 Canceled 共用同一收口，支持 Character 的可变跳跃时长。
void ACatfishingPlayerController::StopJump()
{
	if (ACharacter* ControlledCharacter = Cast<ACharacter>(GetPawn()))
	{
		ControlledCharacter->StopJumping();
	}
}

// 疾跑按下流程：本地立即应用以保持操控响应，同时仅向 authority 发送布尔意图，客户端不能提交任意速度。
void ACatfishingPlayerController::StartSprint()
{
	SetSprintRequested(true, true);
}

// 疾跑释放流程：Completed/Canceled 幂等共用，窗口失焦或 Mapping Context 取消时也恢复普通速度。
void ACatfishingPlayerController::StopSprint()
{
	SetSprintRequested(false, true);
}

// 疾跑意图更新流程：重复事件仍会修正当前 Pawn 的速度，但只在状态实际变化时发送一次可靠 RPC。
void ACatfishingPlayerController::SetSprintRequested(const bool bNewSprintRequested, const bool bNotifyServer)
{
	const bool bStateChanged = bSprintRequested != bNewSprintRequested;
	bSprintRequested = bNewSprintRequested;
	ApplySprintSpeed(GetPawn(), bSprintRequested);

	if (bStateChanged && bNotifyServer && !HasAuthority())
	{
		ServerSetSprinting(bSprintRequested);
	}
}

// 移动速度应用流程：只修改当前 CharacterMovement 的 MaxWalkSpeed；实际速度仍由移动组件加速度、制动和网络移动决定。
void ACatfishingPlayerController::ApplySprintSpeed(APawn* TargetPawn, const bool bSprinting) const
{
	ACharacter* ControlledCharacter = Cast<ACharacter>(TargetPawn);
	UCharacterMovementComponent* MovementComponent = ControlledCharacter
		? ControlledCharacter->GetCharacterMovement() : nullptr;
	if (!MovementComponent)
	{
		return;
	}

	MovementComponent->MaxWalkSpeed = FMath::Max(0.0f, bSprinting ? SprintMaxSpeed : WalkMaxSpeed);
}

// authority 疾跑流程：客户端只能选择开关，服务器使用自身类默认速度重新应用并参与权威移动校验。
void ACatfishingPlayerController::ServerSetSprinting_Implementation(const bool bNewSprinting)
{
	SetSprintRequested(bNewSprinting, false);
}

// Controller 玩法 gate 流程：现取当前 World 的 authority GameMode 并委托唯一判断；不缓存 GameMode 或身份，旅行、Logout 与 teardown 后会立即 fail-closed。
bool ACatfishingPlayerController::CanForwardGameplayCommand() const
{
	const ACatfishingGameModeBase* GameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	return GameMode && GameMode->CanAcceptGameplayCommand(this);
}

// 额度 RPC 流程：先过统一玩法 gate，再组装客户端意图；随后由 GameMode 重建身份并完成 Revision/幂等裁决，结果只写结构化日志。
void ACatfishingPlayerController::ServerSubmitQuotaContribution_Implementation(const FGuid RequestId,
	const int64 ExpectedRevision, const int32 Contribution)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	FCatQuotaContributionCommand Command;
	Command.Context.RequestId = RequestId;
	Command.Context.ExpectedRevision = ExpectedRevision;
	Command.Contribution = Contribution;
	ACatfishingGameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	const FCatRunCommandResult Result = GameMode
		? GameMode->SubmitQuotaContribution(this, Command)
		: FCatRunCommandResult();
	UE_LOG(LogCatRun, Log, TEXT("Event=quota_command_result RequestId=%s Committed=%s Error=%s Revision=%lld"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens), Result.bCommitted ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(Result.Error), Result.Revision);
}

// Ready RPC 流程：先过统一玩法 gate，再转发 RequestId/ExpectedRevision/意图布尔值；GameMode 决定资格、个人复制值和全员 StateTree 事件。
void ACatfishingPlayerController::ServerSetNextDayReady_Implementation(const FGuid RequestId,
	const int64 ExpectedRevision, const bool bReady)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	FCatNextDayReadyCommand Command;
	Command.Context.RequestId = RequestId;
	Command.Context.ExpectedRevision = ExpectedRevision;
	Command.bReady = bReady;
	ACatfishingGameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	const FCatRunCommandResult Result = GameMode
		? GameMode->SubmitNextDayReady(this, Command)
		: FCatRunCommandResult();
	UE_LOG(LogCatRun, Log, TEXT("Event=ready_command_result RequestId=%s Committed=%s Error=%s Revision=%lld"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens), Result.bCommitted ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(Result.Error), Result.Revision);
}

// 结算完成 RPC 流程：现取 authority GameMode/Imprint 服务并检查当前 Run 的计划终态与 Grant ACK；通过后才调用 Run 唯一协调入口，不让客户端布尔值直接结束结算夜。
void ACatfishingPlayerController::ServerRequestSettlementCompletion_Implementation(const FGuid RequestId,
	const int64 ExpectedRevision)
{
	ACatfishingGameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	UCatRunImprintService* Imprint = GetWorld() ? GetWorld()->GetSubsystem<UCatRunImprintService>() : nullptr;
	FCatRunCommandResult Result;
	Result.RequestId = RequestId;
	if (GameMode && Imprint && Imprint->IsSettlementArchiveReady(GameMode->GetRunPublicState().Phase.RunId))
	{
		Result = GameMode->CompleteSettlementFromCoordinator(RequestId, ExpectedRevision);
	}
	else
	{
		Result.Error = ECatRunCommandError::TeardownFailed;
	}
	UE_LOG(LogCatRun, Log, TEXT("Event=settlement_completion_result RequestId=%s Committed=%s Error=%s Revision=%lld"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens), Result.bCommitted ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(Result.Error), Result.Revision);
}

// Profile Grant 客户端流程：从当前 LocalPlayer 现取唯一 Profile 子系统并执行两阶段 durable 应用；只有返回 AckAllowed 才调用服务器 ACK，保存失败保持待重投。
void ACatfishingPlayerController::ClientReceiveProfileGrant_Implementation(const FCatProfileGrant& Grant)
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UCatProfileSubsystem* Profile = LocalPlayer ? LocalPlayer->GetSubsystem<UCatProfileSubsystem>() : nullptr;
	const FCatProfileApplyResult Result = Profile ? Profile->ApplyGrant(Grant) : FCatProfileApplyResult();
	if (Result.bAckAllowed)
	{
		ServerAcknowledgeProfileGrant(Grant.GrantId);
		if (Grant.Kind == ECatProfileGrantKind::FishRecorded || Grant.Kind == ECatProfileGrantKind::FishSilhouette)
		{
			TArray<FCatFishCollectionRecord> Records;
			if (Profile->GetFishCollectionSnapshot(Records))
			{
				ServerPublishPublicFishCollection(Records);
			}
		}
	}
}

// Profile ACK 服务器流程：先让 RunImprintService 以当前 Controller 核对并推进独立 DeliveryRecord；真实 ACK 成功或已重放后再通知 GameMode 复核 Host exit 统一等待。
void ACatfishingPlayerController::ServerAcknowledgeProfileGrant_Implementation(const FGuid GrantId)
{
	if (UCatRunImprintService* Service = GetWorld() ? GetWorld()->GetSubsystem<UCatRunImprintService>() : nullptr)
	{
		const FCatDomainCommandResult AckResult = Service->AcknowledgeGrant(this, GrantId);
		if (AckResult.bCommitted || AckResult.Error == ECatDomainCommandError::AlreadyResolved)
		{
			if (ACatfishingGameModeBase* GameMode = GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>())
			{
				GameMode->NotifyHostExitGrantAckProgress();
			}
		}
	}
}

// CapturePlan 客户端流程：把计划交给本 LocalPlayer Profile 的外部成像桥；桥或本地依赖拒绝时立即回报失败，使服务器把该计划收口为终态而不是永久重投阻塞结算。
void ACatfishingPlayerController::ClientReceiveImprintCapturePlan_Implementation(const FCatCapturePlan& Plan)
{
	bool bAcceptedByBridge = false;
	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (UCatProfileSubsystem* Profile = LocalPlayer->GetSubsystem<UCatProfileSubsystem>())
		{
			bAcceptedByBridge = Profile->ReceiveCapturePlan(Plan);
		}
	}
	if (!bAcceptedByBridge && Plan.CapturePlanId.IsValid())
	{
		ServerReportImprintCaptureResult(Plan.CapturePlanId, false, FGuid());
	}
}

// 成像结果服务器流程：只转交计划 ID、结果布尔和真实 ImprintId；服务端通过当前 PlayerState 校验接收者，客户端不能指定 Grant 内容。
void ACatfishingPlayerController::ServerReportImprintCaptureResult_Implementation(const FGuid CapturePlanId,
	const bool bSucceeded, const FGuid ImprintId)
{
	if (UCatRunImprintService* Service = GetWorld() ? GetWorld()->GetSubsystem<UCatRunImprintService>() : nullptr)
	{
		Service->ReportCaptureResult(this, CapturePlanId, bSucceeded, ImprintId);
	}
}

// 搏斗协作 RPC 流程：先过统一玩法 gate，再转交会话键、幂等键与 ExpectedRevision；Session 继续验证 Giant 与 HookedFight。
void ACatfishingPlayerController::ServerAssistFishingSession_Implementation(const FGuid FishingSessionId,
	const FGuid RequestId, const int64 ExpectedRevision)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	if (FishingCommandComponent)
	{
		FishingCommandComponent->ForwardLegacyAssist(FishingSessionId, RequestId, ExpectedRevision);
	}
}

// 抢抄 RPC 流程：先过统一玩法 gate，再清客户端身份并用当前 Pawn 的鱼护覆盖目标；FishingSession/Items 决定首个合法 Compare-and-Commit。
void ACatfishingPlayerController::ServerRequestScoop_Implementation(const FGuid FishingSessionId,
	FCatScoopCommand Command)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	if (FishingCommandComponent)
	{
		FishingCommandComponent->ForwardLegacyScoop(FishingSessionId, Command);
	}
}

// 献祭 RPC 流程：先过统一玩法 gate，清客户端身份后只调用唯一 Coordinator；Items 预留/提交与 Run apply 顺序不在 Controller 复制实现。
void ACatfishingPlayerController::ServerRequestSacrifice_Implementation(FCatSacrificeCommand Command)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	Command.Context.StableNetId.Reset();
	if (UCatSacrificeCoordinator* Coordinator = GetWorld() ? GetWorld()->GetSubsystem<UCatSacrificeCoordinator>() : nullptr)
	{
		Coordinator->RequestSacrifice(this, Command);
	}
}

// 营地休息 RPC 流程：先过统一玩法 gate，再调用当前 World 的 Camp Actor；Camp 现取 Pawn/距离后把身体写入交给 ConditionComponent。
void ACatfishingPlayerController::ServerRequestCampRest_Implementation(ACatCampHubActor* Camp, const FGuid RequestId)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	if (Camp && Camp->GetWorld() == GetWorld())
	{
		Camp->RequestRest(this, RequestId);
	}
}

// 篝火回看 RPC 流程：先过统一玩法 gate，再把 Controller 与 RequestId 交给固定 Camp；Camp 重验范围、结算阶段和全员在场。
void ACatfishingPlayerController::ServerRequestCampfirePlayback_Implementation(ACatCampHubActor* Camp,
	const FGuid RequestId)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	if (Camp && Camp->GetWorld() == GetWorld())
	{
		Camp->RequestCampfirePlayback(this, RequestId);
	}
}

// 鱼护入缸 RPC 流程：先过统一玩法 gate，再转交两个 Revision 和鱼 ID；Camp/Items 重建身份、容器和容量事实。
void ACatfishingPlayerController::ServerTransferFishToTank_Implementation(ACatCampHubActor* Camp,
	const FGuid RequestId, const FGuid FishInstanceId, const int64 ExpectedGuardRevision, const int64 ExpectedTankRevision)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	if (Camp && Camp->GetWorld() == GetWorld())
	{
		Camp->TransferFishToTank(this, RequestId, FishInstanceId, ExpectedGuardRevision, ExpectedTankRevision);
	}
}

// 搬运救援 RPC 流程：先过统一玩法 gate，再验证两个 Actor 属于当前 World 后交固定 Camp；Teleport 与倒地事实由 Camp/Condition 裁决。
void ACatfishingPlayerController::ServerRescueCharacterToCamp_Implementation(ACatCampHubActor* Camp,
	ACatCharacter* TargetCharacter, const FGuid RequestId)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	if (Camp && TargetCharacter && Camp->GetWorld() == GetWorld() && TargetCharacter->GetWorld() == GetWorld())
	{
		Camp->RescueToCamp(this, TargetCharacter, RequestId);
	}
}

// 装配 RPC 流程：先过统一玩法 gate，当前 Pawn 还必须是项目 Character；EquipmentComponent 用服务器目录验证四个定义并原子写入。
void ACatfishingPlayerController::ServerConfigureEquipment_Implementation(const FGuid RequestId,
	const int64 ExpectedRevision, const FName RodDefinitionId, const FName BaitDefinitionId,
	const FName FloatDefinitionId, const FName ScoopNetDefinitionId)
{
	if (!CanForwardGameplayCommand())
	{
		UE_LOG(LogCatfishing, Warning, TEXT("Event=configure_equipment_rejected Reason=CommandsClosedOrInactive Request=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
		return;
	}
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	UCatEquipmentComponent* Equipment = ControlledCharacter ? ControlledCharacter->GetEquipmentComponent() : nullptr;
	if (!Equipment)
	{
		UE_LOG(LogCatfishing, Warning, TEXT("Event=configure_equipment_rejected Reason=NoEquipmentComponent Request=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
		return;
	}
	const FCatDomainCommandResult Result = Equipment->ConfigureLoadoutFromAuthority(RequestId, ExpectedRevision,
		RodDefinitionId, BaitDefinitionId, FloatDefinitionId, ScoopNetDefinitionId);
	UE_LOG(LogCatfishing, Log, TEXT("Event=configure_equipment Committed=%s Error=%s Revision=%lld Rod=%s Bait=%s Float=%s Net=%s"),
		Result.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Error), Result.Revision,
		*RodDefinitionId.ToString(), *BaitDefinitionId.ToString(), *FloatDefinitionId.ToString(), *ScoopNetDefinitionId.ToString());
}

// 一局耗材发放 RPC 流程：先过统一玩法 gate，再从当前 Pawn 取 Equipment；发放本身仍由组件校验定义是否为 run consumable 并做 Revision/幂等裁决。
void ACatfishingPlayerController::ServerGrantRunConsumable_Implementation(const FGuid RequestId,
	const int64 ExpectedRevision, const FName DefinitionId, const int32 Quantity)
{
	if (!CanForwardGameplayCommand())
	{
		UE_LOG(LogCatfishing, Warning, TEXT("Event=grant_consumable_rejected Reason=CommandsClosedOrInactive Request=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
		return;
	}
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	UCatEquipmentComponent* Equipment = ControlledCharacter ? ControlledCharacter->GetEquipmentComponent() : nullptr;
	if (!Equipment)
	{
		UE_LOG(LogCatfishing, Warning, TEXT("Event=grant_consumable_rejected Reason=NoEquipmentComponent Request=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
		return;
	}
	const FCatDomainCommandResult Result = Equipment->GrantRunConsumableFromAuthority(RequestId, ExpectedRevision, DefinitionId, Quantity);
	UE_LOG(LogCatfishing, Log, TEXT("Event=grant_consumable Committed=%s Error=%s Revision=%lld Definition=%s Quantity=%d"),
		Result.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Error), Result.Revision,
		*DefinitionId.ToString(), Quantity);
}

void ACatfishingPlayerController::ServerSubmitShopPurchase_Implementation(const FName EntryId,
	const FGuid RequestId, const int64 ExpectedWalletRevision)
{
	SubmitShopOrder(EntryId, RequestId, ExpectedWalletRevision, false);
}

void ACatfishingPlayerController::ServerClaimFreeShopEntry_Implementation(const FName EntryId,
	const FGuid RequestId, const int64 ExpectedWalletRevision)
{
	SubmitShopOrder(EntryId, RequestId, ExpectedWalletRevision, true);
}

void ACatfishingPlayerController::SubmitShopOrder(const FName EntryId, const FGuid RequestId,
	const int64 ExpectedWalletRevision, const bool bFreeClaim)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	const APlayerState* CurrentPlayerState = PlayerState;
	UCatShopOrderCoordinator* Coordinator =
		GetWorld() ? GetWorld()->GetSubsystem<UCatShopOrderCoordinator>() : nullptr;
	if (!Coordinator || !CurrentPlayerState || !CurrentPlayerState->GetUniqueId().IsValid())
	{
		return;
	}

	FCatShopPurchaseCommand Command;
	Command.Context.RequestId = RequestId;
	Command.Context.ExpectedRevision = ExpectedWalletRevision;
	Command.Context.StableNetId = CurrentPlayerState->GetUniqueId()->ToString();
	Command.EntryId = EntryId;
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	UCatEquipmentComponent* RecipientEquipment =
		ControlledCharacter ? ControlledCharacter->GetEquipmentComponent() : nullptr;
	const FCatShopOrderResult Result = bFreeClaim
		? Coordinator->SubmitFreeClaim(Command, RecipientEquipment)
		: Coordinator->SubmitPurchase(Command, RecipientEquipment);
	UE_LOG(LogCatfishing, Log,
		TEXT("Event=shop_order_submitted RequestId=%s EntryId=%s Free=%s Order=%s Delivery=%s"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens), *EntryId.ToString(),
		bFreeClaim ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(Result.Transaction.Command.Error),
		*UEnum::GetValueAsString(Result.Delivery.Error));
}

void ACatfishingPlayerController::ServerSellFish_Implementation(const FGuid FishInstanceId,
	const FGuid ContainerId, const int64 ExpectedContainerRevision,
	const ECatShopFishSaleSource SourceKind, const FGuid RequestId,
	const int64 ExpectedWalletRevision)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	const APlayerState* CurrentPlayerState = PlayerState;
	UCatShopOrderCoordinator* Coordinator =
		GetWorld() ? GetWorld()->GetSubsystem<UCatShopOrderCoordinator>() : nullptr;
	if (!Coordinator || !CurrentPlayerState || !CurrentPlayerState->GetUniqueId().IsValid())
	{
		return;
	}
	FCatShopFishSaleOrderCommand Command;
	Command.Context.RequestId = RequestId;
	Command.Context.ExpectedRevision = ExpectedWalletRevision;
	Command.Context.StableNetId = CurrentPlayerState->GetUniqueId()->ToString();
	Command.FishInstanceId = FishInstanceId;
	Command.ContainerId = ContainerId;
	Command.ExpectedContainerRevision = ExpectedContainerRevision;
	Command.SourceKind = SourceKind;
	const FCatShopOrderResult Result = Coordinator->SubmitFishSale(Command);
	UE_LOG(LogCatfishing, Log,
		TEXT("Event=shop_fish_sale_submitted RequestId=%s FishInstanceId=%s Wallet=%s Items=%s"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		*FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*UEnum::GetValueAsString(Result.Transaction.Command.Error),
		*UEnum::GetValueAsString(Result.Delivery.Error));
}

void ACatfishingPlayerController::ServerTakeTeamEquipment_Implementation(const FGuid InstanceId,
	const FGuid RequestId, const int64 ExpectedLibraryRevision,
	const int64 ExpectedEquipmentRevision)
{
	if (!CanForwardGameplayCommand() || !RequestId.IsValid())
	{
		return;
	}
	const FString PayloadSignature = FString::Printf(
		TEXT("Instance=%s|LibraryRevision=%lld|EquipmentRevision=%lld"),
		*InstanceId.ToString(EGuidFormats::DigitsWithHyphens), ExpectedLibraryRevision,
		ExpectedEquipmentRevision);
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	switch (CatQueryTerminalReplay(TakeTeamEquipmentTerminalCache,
		TakeTeamEquipmentPayloadByRequest, RequestId, PayloadSignature, Result, MarkCommandReplayed))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=team_equipment_take_payload_mismatch RequestId=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
		return;
	case ECatTerminalReplayOutcome::Replayed:
		return;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}

	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	UCatEquipmentComponent* Equipment =
		ControlledCharacter ? ControlledCharacter->GetEquipmentComponent() : nullptr;
	UCatTeamEquipmentLibrary* Library =
		GetWorld() ? GetWorld()->GetSubsystem<UCatTeamEquipmentLibrary>() : nullptr;
	const APlayerState* CurrentPlayerState = PlayerState;
	if (!Equipment || !Library || !CurrentPlayerState || !CurrentPlayerState->GetUniqueId().IsValid())
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else
	{
		FCatTeamEquipmentTakeCommand Take;
		Take.Context.RequestId = RequestId;
		Take.Context.ExpectedRevision = ExpectedLibraryRevision;
		Take.Context.StableNetId = CurrentPlayerState->GetUniqueId()->ToString();
		Take.InstanceId = InstanceId;
		FCatTeamEquipmentInstance Instance;
		const ECatDomainCommandError Admission = Library->ValidateTake(Take, Instance);
		if (Admission != ECatDomainCommandError::None)
		{
			Result.Error = Admission;
			Result.Revision = Equipment->GetSnapshot().Revision;
		}
		else
		{
			Result = Equipment->EquipFromTeamLibraryFromAuthority(RequestId,
				ExpectedEquipmentRevision, Instance);
			if (Result.bCommitted)
			{
				const FCatTeamEquipmentGrantResult Taken = Library->TakeInstance(Take);
				if (!Taken.Command.bCommitted)
				{
					UE_LOG(LogCatfishing, Error,
						TEXT("Event=team_equipment_take_failed_after_equip RequestId=%s Error=%s"),
						*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
						*UEnum::GetValueAsString(Taken.Command.Error));
				}
			}
		}
	}
	TakeTeamEquipmentTerminalCache.Add(RequestId, Result);
	TakeTeamEquipmentPayloadByRequest.Add(RequestId, PayloadSignature);
}

// 修竿 RPC 流程：先过统一玩法 gate，再让 Camp 验证本人在固定范围并调用 Equipment 的浮木/耐久事务；不提供远程修理。
void ACatfishingPlayerController::ServerRepairRodAtCamp_Implementation(ACatCampHubActor* Camp,
	const FGuid RequestId, const int64 ExpectedEquipmentRevision)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	UCatEquipmentComponent* Equipment = ControlledCharacter ? ControlledCharacter->GetEquipmentComponent() : nullptr;
	if (Camp && Equipment && Camp->GetWorld() == GetWorld())
	{
		Equipment->RepairRodAtCamp(RequestId, ExpectedEquipmentRevision, Camp->IsControllerInCamp(this));
	}
}

// 草药 RPC 流程：先过统一玩法 gate，再从当前 Pawn 和目标 Character 读服务器位置，要求施药者未倒地且距离不超显式正范围；最后完成身体 preflight 才不可逆扣草药并恢复目标。
void ACatfishingPlayerController::ServerUseHerbOnCharacter_Implementation(ACatCharacter* TargetCharacter,
	const FGuid RequestId, const int64 ExpectedEquipmentRevision, const FName HerbDefinitionId)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	UCatEquipmentComponent* Equipment = ControlledCharacter ? ControlledCharacter->GetEquipmentComponent() : nullptr;
	UCatConditionComponent* SourceConditions = ControlledCharacter ? ControlledCharacter->GetConditionComponent() : nullptr;
	UCatConditionComponent* Conditions = TargetCharacter ? TargetCharacter->GetConditionComponent() : nullptr;
	UCatEquipmentDefinition* Definition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(HerbDefinitionId);
	const UCatConditionSettings* ConditionSettings = GetDefault<UCatConditionSettings>();
	if (!Equipment || !SourceConditions || SourceConditions->GetSnapshot().bDowned || !Conditions
		|| !Definition || Definition->Kind != ECatEquipmentKind::Herb || !ConditionSettings
		|| !FMath::IsFinite(ConditionSettings->HerbUseRangeCentimeters)
		|| ConditionSettings->HerbUseRangeCentimeters <= 0.0
		|| TargetCharacter->GetWorld() != GetWorld()
		|| FVector::DistSquared(ControlledCharacter->GetActorLocation(), TargetCharacter->GetActorLocation())
			> FMath::Square(ConditionSettings->HerbUseRangeCentimeters)
		|| Conditions->ValidateHerbRecovery() != ECatDomainCommandError::None)
	{
		return;
	}
	const FCatDomainCommandResult Consume = Equipment->ConsumeRunConsumableFromAuthority(
		RequestId, ExpectedEquipmentRevision, HerbDefinitionId);
	if (Consume.bCommitted)
	{
		Conditions->ApplyCommittedHerbRecovery(this, RequestId);
	}
}

// 直接吃鱼 RPC 流程：先强制进食者为未倒地当前 Pawn，再用 Items 真实宿主验证共享鱼缸的服务器距离；只有身体 preflight 也成功才不可逆移除鱼并应用状态。
void ACatfishingPlayerController::ServerConsumeFish_Implementation(ACatCharacter* EatingCharacter,
	FCatFishConsumeCommand Command)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	if (EatingCharacter != GetPawn())
	{
		return;
	}
	UCatItemsService* Items = GetWorld() ? GetWorld()->GetSubsystem<UCatItemsService>() : nullptr;
	UCatConditionComponent* Conditions = EatingCharacter ? EatingCharacter->GetConditionComponent() : nullptr;
	const APlayerState* CurrentPlayerState = PlayerState;
	FCatContainerSnapshot Source;
	if (!Items || !Conditions || !CurrentPlayerState || !CurrentPlayerState->GetUniqueId().IsValid()
		|| !Items->TryGetContainerSnapshot(Command.SourceContainerId, Source))
	{
		return;
	}
	ECatContainerKind SourceKind = ECatContainerKind::Unknown;
	AActor* SourceHost = nullptr;
	if (Conditions->GetSnapshot().bDowned
		|| !Items->TryGetContainerHost(Command.SourceContainerId, SourceKind, SourceHost))
	{
		return;
	}
	if (SourceKind == ECatContainerKind::SharedFishTank)
	{
		const UCatCampSettings* CampSettings = GetDefault<UCatCampSettings>();
		if (!CampSettings || !CampSettings->IsRuntimeReady() || !SourceHost
			|| FVector::DistSquared(EatingCharacter->GetActorLocation(), SourceHost->GetActorLocation())
				> FMath::Square(CampSettings->InteractionRadiusCentimeters))
		{
			return;
		}
	}
	const FCatFishInstance* Fish = Source.Fish.FindByPredicate([&Command](const FCatFishInstance& Candidate)
	{
		return Candidate.FishInstanceId == Command.FishInstanceId;
	});
	UCatFishDefinition* Definition = Fish
		? GetDefault<UCatFishCatalogSettings>()->FindRuntimeDefinition(Fish->FishDefinitionId) : nullptr;
	if (!Definition)
	{
		return;
	}
	if (Conditions->ValidateFishConsumption(Definition) != ECatDomainCommandError::None)
	{
		return;
	}
	Command.Context.StableNetId = CurrentPlayerState->GetUniqueId()->ToString();
	const FCatFishConsumeResult Consume = Items->ConsumeFish(Command);
	if (Consume.Command.bCommitted)
	{
		Conditions->ConsumeCommittedFish(Command.Context.RequestId, Definition);
	}
}

// 偷鱼开始 RPC 流程：先过统一玩法 gate，清客户端身份后转交当前 Controller；Social 负责权限、单鱼上限、Timer 和 Items escrow。
void ACatfishingPlayerController::ServerBeginTheft_Implementation(FCatTheftCommand Command)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	Command.Context.StableNetId.Reset();
	if (UCatSocialService* Social = GetWorld() ? GetWorld()->GetSubsystem<UCatSocialService>() : nullptr)
	{
		ClientReceiveTheftResult(Social->BeginTheft(this, Command));
	}
}

// 偷鱼结果客户端流程：可靠接收服务器完整终态并整体替换本机读模型；ProtocolId 由此到达 UI，客户端不能据本缓存修改 escrow 或主人事实。
void ACatfishingPlayerController::ClientReceiveTheftResult_Implementation(const FCatTheftResult& Result)
{
	LastTheftResult = Result;
}

// 偷鱼结果读取流程：返回最近一次 Begin/Catch 的本机副本供界面取得 ProtocolId 和阶段；服务器授权仍重读当前 Controller/World 事实。
FCatTheftResult ACatfishingPlayerController::GetLastTheftResult() const
{
	return LastTheftResult;
}

// 偷鱼追回 RPC 流程：先过统一玩法 gate，再提交当前 Controller 与服务器 ProtocolId；客户端 RequestId 不参与定位 escrow，Social 继续验证真实主人、状态和距离。
void ACatfishingPlayerController::ServerCatchTheft_Implementation(const FGuid TheftProtocolId)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	if (UCatSocialService* Social = GetWorld() ? GetWorld()->GetSubsystem<UCatSocialService>() : nullptr)
	{
		ClientReceiveTheftResult(Social->CatchTheft(this, TheftProtocolId));
	}
}

// 手动求助 RPC 流程：先过统一玩法 gate，再转交 Controller、RequestId 和 Manual 类型；Social 拒绝客户端伪造 Giant 提示。
void ACatfishingPlayerController::ServerRequestManualHelp_Implementation(const FGuid RequestId,
	const ECatHelpSignalKind Kind)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	if (UCatSocialService* Social = GetWorld() ? GetWorld()->GetSubsystem<UCatSocialService>() : nullptr)
	{
		Social->RequestManualHelp(this, RequestId, Kind);
	}
}

// 恶作剧 RPC 流程：先过统一玩法 gate，再从当前 World 按 PlayerState 定位目标；找到后交 Social 冷却与 ProtectionSign 裁决。
void ACatfishingPlayerController::ServerRequestMischief_Implementation(APlayerState* TargetPlayerState,
	const FGuid RequestId, const FVector InteractionLocation)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	APlayerController* TargetController = nullptr;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* Candidate = It->Get(); Candidate && Candidate->PlayerState == TargetPlayerState)
		{
			TargetController = Candidate;
			break;
		}
	}
	if (UCatSocialService* Social = GetWorld() ? GetWorld()->GetSubsystem<UCatSocialService>() : nullptr)
	{
		Social->RequestMischief(this, TargetController, RequestId, InteractionLocation);
	}
}

// 放牌 RPC 流程：先过统一玩法 gate，再转交 Controller、RequestId 和期望位置；Social 重读 Pawn、配置范围并保证每人唯一 Actor。
void ACatfishingPlayerController::ServerPlaceProtectionSign_Implementation(const FGuid RequestId,
	const FVector SignLocation)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	if (UCatSocialService* Social = GetWorld() ? GetWorld()->GetSubsystem<UCatSocialService>() : nullptr)
	{
		Social->PlaceProtectionSign(this, RequestId, SignLocation);
	}
}

// 抖水完成流程：先过统一玩法 gate，再取得当前 Character 并验证 RequestId/身体组件；通过后只清 Wet，保留其他身体事实。
void ACatfishingPlayerController::ServerCompleteShakeDry_Implementation(const FGuid RequestId)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	if (RequestId.IsValid() && ControlledCharacter && ControlledCharacter->GetConditionComponent())
	{
		ControlledCharacter->GetConditionComponent()->SetWetFromAuthority(false);
	}
}

UCatAbilitySystemComponent* ACatfishingPlayerController::GetCurrentCatAbilitySystemComponent() const
{
	const ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	return ControlledCharacter ? ControlledCharacter->GetCatAbilitySystemComponent() : nullptr;
}

void ACatfishingPlayerController::RefreshAbilityInputRoute()
{
	UCatAbilitySystemComponent* NewAbilitySystem = GetCurrentCatAbilitySystemComponent();
	if (UCatAbilitySystemComponent* PreviousAbilitySystem = RoutedAbilitySystem.Get();
		PreviousAbilitySystem && PreviousAbilitySystem != NewAbilitySystem)
	{
		PreviousAbilitySystem->ResetAbilityInput();
	}
	if (NewAbilitySystem)
	{
		NewAbilitySystem->ResetAbilityInput();
	}
	RoutedAbilitySystem = NewAbilitySystem;
}

void ACatfishingPlayerController::AbilityInputTagPressed(const FGameplayTag InputTag)
{
	if (UCatAbilitySystemComponent* AbilitySystem = GetCurrentCatAbilitySystemComponent())
	{
		AbilitySystem->AbilityInputTagPressed(InputTag);
	}
}

void ACatfishingPlayerController::AbilityInputTagReleased(const FGameplayTag InputTag)
{
	if (UCatAbilitySystemComponent* AbilitySystem = GetCurrentCatAbilitySystemComponent())
	{
		AbilitySystem->AbilityInputTagReleased(InputTag);
	}
}

// 主动离局 RPC 流程：只把当前 Controller 交给 authority GameMode；标记不销毁 Session、不旅行，并由随后 Logout 精确消费。
void ACatfishingPlayerController::ServerMarkVoluntaryLeave_Implementation()
{
	if (ACatfishingGameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr)
	{
		GameMode->MarkVoluntaryLeave(this);
	}
}

// Host exit 客户端流程：从本地 GameInstance 取得唯一 Online 子系统并提交服务器关联 RequestId；子系统只在本地 DestroySession 成功后回 ACK，失败由 Host 有界超时收口。
void ACatfishingPlayerController::ClientPrepareForHostExit_Implementation(const FGuid RequestId)
{
	UGameInstance* GameInstance = GetGameInstance();
	if (UCatOnlineSubsystem* Online = GameInstance ? GameInstance->GetSubsystem<UCatOnlineSubsystem>() : nullptr)
	{
		Online->RequestRemoteHostExit(RequestId);
	}
}

// Host exit ACK 服务器流程：只把当前 Controller 与关联键交给 authority GameMode；RPC 自身不销毁 Session、不旅行或更改 Run。
void ACatfishingPlayerController::ServerAcknowledgeHostExit_Implementation(const FGuid RequestId)
{
	if (ACatfishingGameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr)
	{
		GameMode->AcknowledgeHostExitClient(this, RequestId);
	}
}

// 公开图鉴刷新客户端流程：从当前 LocalPlayer durable Profile 只读取 FishCollection；读取成功才提交服务器，绝不附带相册或 Journal。
void ACatfishingPlayerController::ClientRefreshPublicFishCollection_Implementation()
{
	TArray<FCatFishCollectionRecord> Records;
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UCatProfileSubsystem* Profile = LocalPlayer ? LocalPlayer->GetSubsystem<UCatProfileSubsystem>() : nullptr;
	if (Profile && Profile->GetFishCollectionSnapshot(Records))
	{
		ServerPublishPublicFishCollection(Records);
	}
}

// 公开图鉴发布服务器流程：只允许当前 Controller 自己的项目 PlayerState 接收，并让 PlayerState 完整校验后整体复制。
void ACatfishingPlayerController::ServerPublishPublicFishCollection_Implementation(const TArray<FCatFishCollectionRecord>& Records)
{
	if (ACatfishingPlayerState* CatPlayerState = GetPlayerState<ACatfishingPlayerState>())
	{
		CatPlayerState->SetPublicFishCollectionFromAuthority(Records);
	}
}
