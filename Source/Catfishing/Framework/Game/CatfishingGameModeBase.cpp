#include "Framework/Game/CatfishingGameModeBase.h"

#include "Framework/Game/CatfishingGameState.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "Character/CatCharacter.h"
#include "AbilitySystem/Attributes/CatRunAttributeSet.h"
#include "AbilitySystem/Attributes/CatRunModifierAttributeSet.h"
#include "AbilitySystem/Executions/CatRunApplySacrificeExecutionCalculation.h"
#include "AbilitySystem/Executions/CatRunStartDayExecutionCalculation.h"
#include "AbilitySystem/Effects/CatRunStartDayEffect.h"
#include "AbilitySystem/Effects/CatRunSacrificeContributionEffect.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "AbilitySystemComponent.h"
#include "Logging/CatLog.h"
#include "Online/CatOnlineSettings.h"
#include "Online/CatOnlineSubsystem.h"
#include "Camp/CatCampHubActor.h"
#include "Camp/CatCampSettings.h"
#include "Condition/CatConditionComponent.h"
#include "Collection/CatRunImprintService.h"
#include "Components/StateTreeComponent.h"
#include "Environment/CatConfiguredEnvironmentProvider.h"
#include "Environment/CatChumFieldAnchor.h"
#include "Environment/CatChumFieldSubsystem.h"
#include "Environment/CatEnvironmentSettings.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Fishing/CatFishingService.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "OnlineSubsystemTypes.h"
#include "Run/CatRunSettings.h"
#include "Run/CatSacrificeCoordinator.h"
#include "Run/CatRunStateTreeEvents.h"
#include "Save/CatSaveSubsystem.h"
#include "Save/CatSaveSettings.h"
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

	// 营地出生点扫描流程：遍历当前 World 内所有有效营地，只有恰好一座时返回；零座或多座都写明确日志并保持 fail-closed。
	ACatCampHubActor* FindUniqueCampPlayerStart(UWorld* World, const AController* Player, const TCHAR* Caller)
	{
		ACatCampHubActor* FirstCamp = nullptr;
		ACatCampHubActor* DuplicateCamp = nullptr;
		int32 CampCount = 0;
		if (World)
		{
			for (TActorIterator<ACatCampHubActor> It(World); It; ++It)
			{
				ACatCampHubActor* Camp = *It;
				if (!IsValid(Camp))
				{
					continue;
				}
				++CampCount;
				if (!FirstCamp)
				{
					FirstCamp = Camp;
				}
				else if (!DuplicateCamp)
				{
					DuplicateCamp = Camp;
				}
			}
		}
		if (CampCount != 1)
		{
			UE_LOG(LogCatfishing, Error,
				TEXT("Event=camp_player_start_rejected Caller=%s Controller=%s Reason=%s CampCount=%d FirstCamp=%s DuplicateCamp=%s World=%s NetMode=%d"),
				Caller, *GetNameSafe(Player), CampCount <= 0 ? TEXT("NoCampPlayerStart") : TEXT("DuplicateCampPlayerStart"),
				CampCount, *GetNameSafe(FirstCamp), *GetNameSafe(DuplicateCamp), *GetNameSafe(World),
				World ? static_cast<int32>(World->GetNetMode()) : INDEX_NONE);
			return nullptr;
		}
		return FirstCamp;
	}

	// 当前玩家出生序号解析流程：按 GameState 当前 PlayerArray 中仍活跃且非纯旁观的玩家顺序即时计算；目标尚未出现在数组中时排在当前活跃队列末尾，不写入持久槽位或重连记忆。
	int32 ResolveCurrentCampEntryIndex(const AGameStateBase* GameState, const AController* NewPlayer)
	{
		const APlayerState* TargetPlayerState = NewPlayer ? NewPlayer->PlayerState : nullptr;
		if (!GameState || !TargetPlayerState || TargetPlayerState->IsInactive()
			|| TargetPlayerState->IsOnlyASpectator())
		{
			return INDEX_NONE;
		}

		int32 EntryIndex = 0;
		for (const APlayerState* PlayerState : GameState->PlayerArray)
		{
			if (!PlayerState || PlayerState->IsInactive() || PlayerState->IsOnlyASpectator())
			{
				continue;
			}
			if (PlayerState == TargetPlayerState)
			{
				return EntryIndex;
			}
			++EntryIndex;
		}
		return EntryIndex;
	}
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

// 启动流程：先执行引擎启动、建立 NotStarted 快照并订阅商店变化；验证 authority、运行配置、环境和 StateTree 后，恢复 Save 共享世界与已经生成的 Pawn。
// 任何依赖或恢复失败都保持 StartupFailed；全部通过才开放命令、启动 StateTree 并安排定期检查点。
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
			ShopInventoryRefreshedHandle = Shop->OnShopInventoryRefreshed.AddWeakLambda(this,
				[this]() { PublishShopEconomySnapshot(); });
		}
	}
	PublishShopEconomySnapshot();

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
	// 存档世界恢复流程：在 Run StateTree 打开新命令前先恢复共享营地和世界鱼；Save 已完成全局预检，失败则保持 StartupFailed，不能带着半恢复库存进入玩法。
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UCatSaveSubsystem* SaveSubsystem = GameInstance->GetSubsystem<UCatSaveSubsystem>()
			; SaveSubsystem && !SaveSubsystem->RestoreWorldAfterHostsReady(*this))
		{
			FailRunStartup(TEXT("PersistenceWorldRestoreRejected"));
			return;
		}
		// 初始玩家可早于 StartPlay 生成；共享世界就绪后补恢复已存在的 Pawn，不能只依赖之后的 Spawn 回调。
		if (UCatSaveSubsystem* SaveSubsystem = GameInstance->GetSubsystem<UCatSaveSubsystem>())
		{
			for (TActorIterator<APlayerController> It(GetWorld()); It; ++It)
			{
				if (ACatCharacter* Character = Cast<ACatCharacter>(It->GetPawn()))
				{
					if (!SaveSubsystem->RestorePlayerAfterSpawn(**It, *Character))
					{
						FailRunStartup(TEXT("PersistenceInitialPlayerRestoreRejected"));
						return;
					}
				}
			}
		}
	}

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
	StartPersistenceCheckpointTimer();
	UE_LOG(LogCatRun, Log, TEXT("Event=run_started RunId=%s Revision=%lld StateTree=%s"),
		*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Revision, *RunFlowAsset->GetName());
}

// 检查点计时器启动流程：先清上一次 World 可能遗留的句柄，再确认 Save 子系统已持有活动槽且设置给出有效秒数；没有载入世界槽时不创建空档案或伪造检查点。
void ACatfishingGameModeBase::StartPersistenceCheckpointTimer()
{
	GetWorldTimerManager().ClearTimer(PersistenceCheckpointTimerHandle);
	PersistenceCheckpointTimerHandle.Invalidate();
	UGameInstance* GameInstance = GetGameInstance();
	UCatSaveSubsystem* SaveSubsystem = GameInstance ? GameInstance->GetSubsystem<UCatSaveSubsystem>() : nullptr;
	float IntervalSeconds = 0.0f;
	if (!SaveSubsystem || SaveSubsystem->GetActiveSlotId().IsNone()
		|| !GetDefault<UCatSaveSettings>()->TryGetCheckpointIntervalSeconds(IntervalSeconds))
	{
		return;
	}
	GetWorldTimerManager().SetTimer(PersistenceCheckpointTimerHandle, this,
		&ThisClass::HandlePersistenceCheckpoint, IntervalSeconds, true);
	UE_LOG(LogCatRun, Log, TEXT("Event=persistence_checkpoint_timer_started Slot=%s IntervalSeconds=%.2f"),
		*SaveSubsystem->GetActiveSlotId().ToString(), IntervalSeconds);
}

// 检查点触发流程：只在 authority Run 仍打开时请求 Save 子系统采集世界；请求未受理或异步失败不会改 Run/StateTree，下一轮计时仍可继续尝试。
void ACatfishingGameModeBase::HandlePersistenceCheckpoint()
{
	if (!HasAuthority() || !bRunCommandsOpen)
	{
		return;
	}
	UGameInstance* GameInstance = GetGameInstance();
	UCatSaveSubsystem* SaveSubsystem = GameInstance ? GameInstance->GetSubsystem<UCatSaveSubsystem>() : nullptr;
	if (!SaveSubsystem)
	{
		return;
	}
	const FCatSaveResult Result = SaveSubsystem->RequestSaveActiveRun();
	if (!Result.bAccepted)
	{
		UE_LOG(LogCatRun, Warning, TEXT("Event=persistence_checkpoint_rejected RequestId=%s Reason=%s"),
			*Result.RequestId.ToString(EGuidFormats::DigitsWithHyphens), *Result.Message.ToString());
	}
}

// World 收口流程：先关闭 Run 命令并解除所有 Controller 的 Pawn 捕获通知，再清跳天、检查点、白天和 HostExit ACK 计时及等待集合。
// 随后解除商店订阅并停止 StateTree，最后调用父类；此处不发起异步保存，最终落盘必须已由 Online 离开前协议取得回执。
void ACatfishingGameModeBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bRunCommandsOpen = false;
	for (TActorIterator<APlayerController> It(GetWorld()); It; ++It)
	{
		It->GetOnNewPawnNotifier().RemoveAll(this);
	}
#if !UE_BUILD_SHIPPING
	ClearDebugSkipToNextDayRequest();
#endif
	CloseShopForSettlementNight();
	ClearDayDeadline();
	GetWorldTimerManager().ClearTimer(PersistenceCheckpointTimerHandle);
	PersistenceCheckpointTimerHandle.Invalidate();
	PendingHostExitAckStableNetIds.Reset();
	if (UWorld* World = GetWorld())
	{
		if (UCatShopEconomyService* Shop = World->GetSubsystem<UCatShopEconomyService>())
		{
			Shop->OnPublicTransactionCommitted.Remove(ShopPublicTransactionHandle);
			Shop->OnShopInventoryRefreshed.Remove(ShopInventoryRefreshedHandle);
		}
	}
	ShopPublicTransactionHandle.Reset();
	ShopInventoryRefreshedHandle.Reset();
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

// 重启玩家流程：先保持引擎对空 Controller 和待销毁 Controller 的早退，再直接扫描本项目唯一营地，避免蓝图覆盖 Find/ChoosePlayerStart 把普通 PlayerStart 带回主出生链；营地缺失、重复或被非营地替代时调用 FailedToRestartPlayer，绝不沿用旧 StartSpot 或 WorldSettings 原点。
void ACatfishingGameModeBase::RestartPlayer(AController* NewPlayer)
{
	if (!NewPlayer || NewPlayer->IsPendingKillPending())
	{
		return;
	}
	AActor* StartSpot = FindUniqueCampPlayerStart(GetWorld(), NewPlayer, TEXT("RestartPlayer"));
	if (!Cast<ACatCampHubActor>(StartSpot))
	{
		UE_LOG(LogCatfishing, Error,
			TEXT("Event=camp_player_restart_rejected Controller=%s Reason=CampPlayerStartUnavailable StartSpot=%s World=%s NetMode=%d"),
			*GetNameSafe(NewPlayer), *GetNameSafe(StartSpot), *GetNameSafe(GetWorld()),
			GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : INDEX_NONE);
		FailedToRestartPlayer(NewPlayer);
		return;
	}
	RestartPlayerAtPlayerStart(NewPlayer, StartSpot);
}

// 玩家出生点查找流程：忽略客户端 Portal 名和 Controller 历史 StartSpot，每次都重新走唯一营地扫描；返回空时由 RestartPlayer 统一拒绝生成，防止引擎默认原点回退。
AActor* ACatfishingGameModeBase::FindPlayerStart_Implementation(AController* Player, const FString& IncomingName)
{
	if (!IncomingName.IsEmpty())
	{
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=camp_player_start_portal_ignored Controller=%s IncomingName=%s Reason=CampIsOnlyPlayerStart World=%s NetMode=%d"),
			*GetNameSafe(Player), *IncomingName, *GetNameSafe(GetWorld()),
			GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : INDEX_NONE);
	}
	return FindUniqueCampPlayerStart(GetWorld(), Player, TEXT("FindPlayerStart"));
}

// 玩家出生点选择流程：只接受当前 World 唯一 ACatCampHubActor，普通 PlayerStart、tagged PlayerStart 和历史 StartSpot 都不进入候选；成功日志用于联机包核对服务器裁决。
AActor* ACatfishingGameModeBase::ChoosePlayerStart_Implementation(AController* Player)
{
	ACatCampHubActor* Camp = FindUniqueCampPlayerStart(GetWorld(), Player, TEXT("ChoosePlayerStart"));
	if (Camp)
	{
		UE_LOG(LogCatfishing, Log,
			TEXT("Event=camp_player_start_selected Controller=%s Camp=%s World=%s NetMode=%d"),
			*GetNameSafe(Player), *GetNameSafe(Camp), *GetNameSafe(GetWorld()),
			GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : INDEX_NONE);
	}
	return Camp;
}

// StartSpot 复用判断流程：固定返回 false，让重连、重新生成和外部 K2_FindPlayerStart 调用都重新经过唯一营地裁决；本方法不清 Controller 状态，只阻断引擎选择旧点的分支。
bool ACatfishingGameModeBase::ShouldSpawnAtStartSpot(AController* Player)
{
	return false;
}

// 默认 Pawn 生成流程：验证唯一营地和 PawnClass 后解析合法出生位置并生成 Character；绑定解除占有时的同步捕获，再通过 Save 恢复库存和位置。
// 恢复拒绝时关闭 Run、销毁这次 Pawn 并返回空；成功后由父类继续 SetPawn 和 FinishRestartPlayer，初始 Pawn 早于世界恢复时由 StartPlay 补齐。
APawn* ACatfishingGameModeBase::SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot)
{
	const ACatCampHubActor* Camp = Cast<ACatCampHubActor>(StartSpot);
	if (!Camp)
	{
		UE_LOG(LogCatfishing, Error,
			TEXT("Event=camp_player_spawn_rejected Controller=%s Reason=NonCampStartSpot StartSpot=%s World=%s NetMode=%d"),
			*GetNameSafe(NewPlayer), *GetNameSafe(StartSpot), *GetNameSafe(GetWorld()),
			GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : INDEX_NONE);
		return nullptr;
	}

	UClass* PawnClass = GetDefaultPawnClassForController(NewPlayer);
	const APawn* PawnToFit = PawnClass ? Cast<APawn>(PawnClass->GetDefaultObject()) : nullptr;
	const int32 PreferredEntryIndex = ResolveCurrentCampEntryIndex(GameState, NewPlayer);
	FTransform SpawnTransform;
	if (!PawnClass || !PawnToFit || PreferredEntryIndex == INDEX_NONE
		|| !Camp->TryResolvePlayerEntryTransform(PreferredEntryIndex, PawnToFit, SpawnTransform))
	{
		UE_LOG(LogCatfishing, Error,
			TEXT("Event=camp_player_spawn_rejected Controller=%s Reason=TransformUnavailable PawnClass=%s Camp=%s PreferredIndex=%d World=%s NetMode=%d"),
			*GetNameSafe(NewPlayer), *GetNameSafe(PawnClass), *GetNameSafe(Camp), PreferredEntryIndex,
			*GetNameSafe(GetWorld()), GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : INDEX_NONE);
		return nullptr;
	}

	APawn* SpawnedPawn = SpawnDefaultPawnAtTransform(NewPlayer, SpawnTransform);
	if (!SpawnedPawn)
	{
		UE_LOG(LogCatfishing, Error,
			TEXT("Event=camp_player_spawn_failed Controller=%s PawnClass=%s Camp=%s Transform=%s World=%s NetMode=%d"),
			*GetNameSafe(NewPlayer), *GetNameSafe(PawnClass), *GetNameSafe(Camp),
			*SpawnTransform.ToHumanReadableString(), *GetNameSafe(GetWorld()),
			GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : INDEX_NONE);
	}
	else
	{
		// 玩家存档恢复流程：Pawn 已有 authority Owner 与组件，但尚未交给 Controller；库存或位置恢复失败时销毁本次 Pawn 并阻断该玩家进入半恢复世界。
		if (ACatCharacter* SpawnedCharacter = Cast<ACatCharacter>(SpawnedPawn))
		{
			// UE 先在 Pawn::Destroyed 中解除占有，之后才调 GameMode::Logout；这条通知在组件 EndPlay 之前同步捕获最后事实。
			NewPlayer->GetOnNewPawnNotifier().RemoveAll(this);
			NewPlayer->GetOnNewPawnNotifier().AddWeakLambda(this,
				[this, WeakController = TWeakObjectPtr<AController>(NewPlayer),
					WeakCharacter = TWeakObjectPtr<ACatCharacter>(SpawnedCharacter)](APawn* NewPawn)
				{
					AController* Controller = WeakController.Get();
					ACatCharacter* Character = WeakCharacter.Get();
					if (!NewPawn && Controller && Character)
					{
						// Character 在 Super::UnPossessed 清除 PlayerState/ASC 前已完成操作位释放与资源托管；这里仅捕获剩余个人库存。
						if (bRunCommandsOpen && IsControllerActive(Controller))
						{
							UCatSaveSubsystem* Save = GetGameInstance()->GetSubsystem<UCatSaveSubsystem>();
							if (Save && !Save->CapturePlayerBeforeLogout(*Controller, Character))
							{
								FailRunStartup(TEXT("PersistenceDepartureCaptureRejected"));
							}
						}
					}
				});
			if (UGameInstance* GameInstance = GetGameInstance())
			{
				if (UCatSaveSubsystem* SaveSubsystem = GameInstance->GetSubsystem<UCatSaveSubsystem>()
					; SaveSubsystem && !SaveSubsystem->RestorePlayerAfterSpawn(*NewPlayer, *SpawnedCharacter))
				{
					UE_LOG(LogCatfishing, Error,
						TEXT("Event=persistence_player_spawn_restore_rejected Controller=%s Pawn=%s"),
						*GetNameSafe(NewPlayer), *GetNameSafe(SpawnedPawn));
					FailRunStartup(TEXT("PersistencePlayerRestoreRejected"));
					SpawnedPawn->Destroy();
					return nullptr;
				}
			}
		}
		UE_LOG(LogCatfishing, Log,
			TEXT("Event=camp_player_spawned Controller=%s Pawn=%s Camp=%s Transform=%s World=%s NetMode=%d"),
			*GetNameSafe(NewPlayer), *GetNameSafe(SpawnedPawn), *GetNameSafe(Camp),
			*SpawnTransform.ToHumanReadableString(), *GetNameSafe(GetWorld()),
			GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : INDEX_NONE);
	}
	return SpawnedPawn;
}

// Character 不可用收口流程：
// 1. 先拒绝非 authority、空 Character 或无 World，避免客户端和销毁尾声改写服务器领域服务。
// 2. 身份仍有效时退出个人操作位，保留同一会话供队友接力；再把活动会话和部署物品移到托管宿主，确保后续保存不会把队友仍使用的实例退役。
// 3. 取消 Social 偷鱼追回后记录协调结果。UnPossessed/EndPlay 共用此入口，已释放成员和已迁出资源的重复调用保持幂等。
void ACatfishingGameModeBase::HandleCharacterUnavailable(ACatCharacter* Character)
{
	UWorld* World = Character ? Character->GetWorld() : nullptr;
	if (!Character || !Character->HasAuthority() || !World)
	{
		return;
	}

	UCatFishingService* Fishing = World->GetSubsystem<UCatFishingService>();
	const APlayerState* DepartingPlayerState = Character->GetPlayerState();
	const int32 DepartingPlayerId = DepartingPlayerState ? DepartingPlayerState->GetPlayerId() : INDEX_NONE;
	bool bResourcesPreserved = false;
	if (Fishing)
	{
		Fishing->ReleaseFishingOperatorForCharacter(Character);
		bResourcesPreserved = Fishing->PreserveFishingResourcesForEquipmentShutdown(Character->GetEquipmentComponent());
	}
	UCatSocialService* Social = World->GetSubsystem<UCatSocialService>();
	if (Social)
	{
		Social->CancelTheftsForCharacter(Character);
	}
	UE_LOG(LogCatRun, Log,
		TEXT("Event=character_unavailable_cleanup Character=%s PlayerId=%d World=%s NetMode=%d Authority=true LocalRole=%d FishingAvailable=%s SocialAvailable=%s ResourcesPreserved=%s Result=CleanupRequested"),
		*GetNameSafe(Character), DepartingPlayerId, *GetNameSafe(World), static_cast<int32>(World->GetNetMode()),
		static_cast<int32>(Character->GetLocalRole()), Fishing ? TEXT("true") : TEXT("false"),
		Social ? TEXT("true") : TEXT("false"), bResourcesPreserved ? TEXT("true") : TEXT("false"));
}

// Logout 流程：先对精确 Active 连接完成或复核末次持久化捕获，再移除准入记录与 Pawn 通知；旧连接不能覆盖新连接的存档，之后继续原有重连 TTL 和夜晚 ready 清理。
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
			if (bRunCommandsOpen && GetGameInstance())
			{
				UCatSaveSubsystem* Save = GetGameInstance()->GetSubsystem<UCatSaveSubsystem>();
				if (Save && !Save->CapturePlayerBeforeLogout(*Exiting))
				{
					FailRunStartup(TEXT("PersistenceLogoutCaptureRejected"));
				}
			}
			Exiting->GetOnNewPawnNotifier().RemoveAll(this);
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

// Fishing/玩家打窝 gate 流程：先复用宽玩法命令 gate，再要求 Run 仍在白天且公开快照允许钓鱼，最后确认当前猫未倒地；夜晚 ready、结算收口、救援和 Social 命令继续走宽 gate，不被钓鱼白天规则误封。
bool ACatfishingGameModeBase::CanAcceptFishingCommand(const AController* Controller) const
{
	const ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr;
	const UCatConditionComponent* Conditions = Character ? Character->GetConditionComponent() : nullptr;
	return CanAcceptGameplayCommand(Controller)
		&& RunPublicState.Phase.Phase == ECatRunPhase::DayActive
		&& RunPublicState.Phase.bFishingAllowed
		&& Conditions && !Conditions->GetSnapshot().bDowned;
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

// 献祭额度预演流程：
// 1. 只读取得 GameState 上的唯一 Run ASC，并核对 Attribute 目标与公开 DTO 仍保持投影一致。
// 2. 用献祭 ExecCalc 暴露的同一套公式把冻结贡献和当前效率换算成实际贡献。
// 3. 在 Items 不可逆提交前检查当前进度与实际贡献相加不会溢出，返回调用方用于 StateTree 达标依赖判断的新进度。
ECatRunCommandError ACatfishingGameModeBase::PreviewRunSacrificeContribution(const FCatQuotaContributionCommand& Command,
	int32& OutAppliedContribution, int64& OutNewProgress) const
{
	OutAppliedContribution = 0;
	OutNewProgress = 0;
	const ACatfishingGameState* RunGameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
	UAbilitySystemComponent* RunASC = RunGameState ? RunGameState->GetRunAbilitySystemComponentFromAuthority() : nullptr;
	if (!RunASC)
	{
		UE_LOG(LogCatRun, Error, TEXT("Event=RunSacrificePreviewFailed Reason=RunASCUnavailable World=%s RequestId=%s"),
			GetWorld() ? *GetWorld()->GetName() : TEXT("None"),
			*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens));
		return ECatRunCommandError::DependencyUnavailable;
	}

	const float CurrentProgress = RunASC->GetNumericAttribute(UCatRunAttributeSet::GetQuotaProgressAttribute());
	const float CurrentQuotaTarget = RunASC->GetNumericAttribute(UCatRunAttributeSet::GetQuotaTargetAttribute());
	if (!FMath::IsFinite(CurrentProgress) || !FMath::IsFinite(CurrentQuotaTarget)
		|| CurrentProgress < 0.0f || CurrentProgress > MAX_int32 || CurrentQuotaTarget > MAX_int32)
	{
		UE_LOG(LogCatRun, Error, TEXT("Event=RunAttributeProjectionMismatch Reason=SacrificePreviewNonFinite AttributeTarget=%.3f AttributeProgress=%.3f DtoTarget=%d DtoProgress=%d RequestId=%s"),
			CurrentQuotaTarget, CurrentProgress, RunPublicState.QuotaTarget, RunPublicState.QuotaProgress,
			*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens));
		return ECatRunCommandError::DependencyUnavailable;
	}
	const int32 AttributeQuotaTarget = FMath::RoundToInt(CurrentQuotaTarget);
	const int32 AttributeQuotaProgress = FMath::RoundToInt(CurrentProgress);
	if (AttributeQuotaTarget <= 0 || AttributeQuotaTarget != RunPublicState.QuotaTarget
		|| AttributeQuotaProgress != RunPublicState.QuotaProgress
		|| !FMath::IsNearlyEqual(CurrentQuotaTarget, static_cast<float>(AttributeQuotaTarget))
		|| !FMath::IsNearlyEqual(CurrentProgress, static_cast<float>(AttributeQuotaProgress)))
	{
		UE_LOG(LogCatRun, Error, TEXT("Event=RunAttributeProjectionMismatch Reason=SacrificePreviewInvalid AttributeTarget=%d AttributeProgress=%d DtoTarget=%d DtoProgress=%d RequestId=%s"),
			AttributeQuotaTarget, AttributeQuotaProgress, RunPublicState.QuotaTarget, RunPublicState.QuotaProgress,
			*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens));
		return ECatRunCommandError::DependencyUnavailable;
	}

	const float SacrificeEfficiency = RunASC->GetNumericAttribute(UCatRunModifierAttributeSet::GetSacrificeEfficiencyAttribute());
	if (!UCatRunApplySacrificeExecutionCalculation::TryCalculateAppliedContribution(static_cast<float>(Command.Contribution),
		SacrificeEfficiency, OutAppliedContribution))
	{
		UE_LOG(LogCatRun, Error, TEXT("Event=RunSacrificePreviewFailed Reason=InvalidAppliedContribution RequestId=%s RawContribution=%d SacrificeEfficiency=%.3f"),
			*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens), Command.Contribution, SacrificeEfficiency);
		return ECatRunCommandError::DependencyUnavailable;
	}

	const double ProjectedProgress = static_cast<double>(AttributeQuotaProgress) + OutAppliedContribution;
	if (!FMath::IsFinite(ProjectedProgress) || ProjectedProgress > MAX_int32)
	{
		UE_LOG(LogCatRun, Error, TEXT("Event=RunSacrificePreviewFailed Reason=ProgressOverflow RequestId=%s AttributeProgress=%.3f AppliedContribution=%d"),
			*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens), CurrentProgress, OutAppliedContribution);
		OutAppliedContribution = 0;
		OutNewProgress = 0;
		return ECatRunCommandError::InvalidPayload;
	}
	OutNewProgress = FMath::RoundToInt64(ProjectedProgress);
	return ECatRunCommandError::None;
}

// 阶段进入流程：先要求 authority、有效 Run 与正在启动/运行的唯一 StateTree，并在写公开 Phase 前拒绝未裁策略、白天参数或 Run ASC/GE 额度初始化失败。通过后统一清掉旧白天计时与公开截止并复位玩法开关：DayActive 递增天数、从 AttributeSet 投影目标与零进度、重置 Active 玩家 ready、开启 quota/fishing，建立截止与 Morning/Dusk 刷新；NormalNight 冻结当前 ready 资格；两种 settlement 写对应终局原因并清 ready 集合；Ending/Ended/NotStarted 关闭新命令。最后只递增一次 Revision、保存 StateTree 可读结果并刷新 Environment/GameState 组合快照；非 Shipping 跳天加速只在正式阶段已发布后续交正式命令，C++ 始终不选择下一条转移边。
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
	bool bShouldScheduleDayEnvironmentRefreshes = false;
	if (NewPhase == ECatRunPhase::DayActive
		&& !GetDefault<UCatRunSettings>()->TryGetDayParameters(DayLengthSeconds, DayQuotaTarget))
	{
		Result.Error = ECatRunCommandError::PolicyUndecided;
		LastRunFlowResult = Result;
		return Result;
	}
	int32 ExpectedDayQuotaTarget = 0;
	int32 DayStartAttributeTarget = 0;
	int32 DayStartAttributeProgress = 0;
	float DayStartOldTarget = 0.0f;
	float DayStartQuotaTargetMultiplier = 1.0f;
	float DayStartDailyPressure = 1.0f;
	if (NewPhase == ECatRunPhase::DayActive)
	{
		ACatfishingGameState* RunGameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
		UAbilitySystemComponent* DayStartRunASC = RunGameState ? RunGameState->GetRunAbilitySystemComponentFromAuthority() : nullptr;
		if (!DayStartRunASC)
		{
			Result.Error = ECatRunCommandError::DependencyUnavailable;
			LastRunFlowResult = Result;
			UE_LOG(LogCatRun, Error, TEXT("Event=RunStartDayGEFailed Reason=RunASCUnavailable World=%s"),
				GetWorld() ? *GetWorld()->GetName() : TEXT("None"));
			return Result;
		}
		DayStartOldTarget = DayStartRunASC->GetNumericAttribute(UCatRunAttributeSet::GetQuotaTargetAttribute());
		DayStartQuotaTargetMultiplier = DayStartRunASC->GetNumericAttribute(UCatRunModifierAttributeSet::GetQuotaTargetMultiplierAttribute());
		DayStartDailyPressure = DayStartRunASC->GetNumericAttribute(UCatRunModifierAttributeSet::GetDailyPressureAttribute());
		if (!UCatRunStartDayExecutionCalculation::TryCalculateQuotaTarget(static_cast<float>(DayQuotaTarget),
			DayStartQuotaTargetMultiplier, DayStartDailyPressure, ExpectedDayQuotaTarget))
		{
			Result.Error = ECatRunCommandError::DependencyUnavailable;
			LastRunFlowResult = Result;
			UE_LOG(LogCatRun, Error, TEXT("Event=RunStartDayGEFailed Reason=InvalidCalculatedTarget World=%s BaseQuotaTarget=%d Multiplier=%.3f DailyPressure=%.3f"),
				GetWorld() ? *GetWorld()->GetName() : TEXT("None"), DayQuotaTarget,
				DayStartQuotaTargetMultiplier, DayStartDailyPressure);
			return Result;
		}
		const FGameplayEffectSpecHandle StartDaySpec = DayStartRunASC->MakeOutgoingSpec(UCatGE_RunStartDay::StaticClass(), 1.0f,
			DayStartRunASC->MakeEffectContext());
		if (!StartDaySpec.IsValid())
		{
			Result.Error = ECatRunCommandError::DependencyUnavailable;
			LastRunFlowResult = Result;
			UE_LOG(LogCatRun, Error, TEXT("Event=RunStartDayGEFailed Reason=SpecUnavailable World=%s"),
				GetWorld() ? *GetWorld()->GetName() : TEXT("None"));
			return Result;
		}
		StartDaySpec.Data->SetSetByCallerMagnitude(CatFishingAbilityTags::Data_Run_BaseQuotaTarget,
			static_cast<float>(DayQuotaTarget));
		if (!DayStartRunASC->ApplyGameplayEffectSpecToSelf(*StartDaySpec.Data.Get()).WasSuccessfullyApplied())
		{
			Result.Error = ECatRunCommandError::DependencyUnavailable;
			LastRunFlowResult = Result;
			UE_LOG(LogCatRun, Error, TEXT("Event=RunStartDayGEFailed Reason=ApplyRejected World=%s"),
				GetWorld() ? *GetWorld()->GetName() : TEXT("None"));
			return Result;
		}
		const float DayStartCurrentTarget = DayStartRunASC->GetNumericAttribute(UCatRunAttributeSet::GetQuotaTargetAttribute());
		const float DayStartCurrentProgress = DayStartRunASC->GetNumericAttribute(UCatRunAttributeSet::GetQuotaProgressAttribute());
		if (!FMath::IsFinite(DayStartCurrentTarget) || !FMath::IsFinite(DayStartCurrentProgress)
			|| DayStartCurrentTarget <= 0.0f || DayStartCurrentProgress < 0.0f
			|| DayStartCurrentTarget > MAX_int32 || DayStartCurrentProgress > MAX_int32
			|| !FMath::IsNearlyZero(DayStartCurrentProgress))
		{
			Result.Error = ECatRunCommandError::DependencyUnavailable;
			LastRunFlowResult = Result;
			UE_LOG(LogCatRun, Error, TEXT("Event=RunAttributeProjectionMismatch Reason=StartDayNonFinite AttributeTarget=%.3f AttributeProgress=%.3f ExpectedTarget=%d"),
				DayStartCurrentTarget, DayStartCurrentProgress, ExpectedDayQuotaTarget);
			return Result;
		}
		DayStartAttributeTarget = FMath::RoundToInt(DayStartCurrentTarget);
		DayStartAttributeProgress = FMath::RoundToInt(DayStartCurrentProgress);
		if (DayStartAttributeTarget != ExpectedDayQuotaTarget || DayStartAttributeProgress != 0)
		{
			Result.Error = ECatRunCommandError::DependencyUnavailable;
			LastRunFlowResult = Result;
			UE_LOG(LogCatRun, Error, TEXT("Event=RunAttributeProjectionMismatch Reason=StartDayInvalid AttributeTarget=%d AttributeProgress=%d ExpectedTarget=%d DtoTarget=%d DtoProgress=%d"),
				DayStartAttributeTarget, DayStartAttributeProgress, ExpectedDayQuotaTarget,
				RunPublicState.QuotaTarget, RunPublicState.QuotaProgress);
			return Result;
		}
	}

	// 非白天阶段会关闭 Fishing gate；必须在改公开门禁前由服务器释放竿位和 MOVE_None，
	// 否则玩家进入夜晚后连 LeaveRod 都会被同一个 gate 拒绝。
	if (NewPhase != ECatRunPhase::DayActive)
	{
		if (UCatFishingService* Fishing = GetWorld()->GetSubsystem<UCatFishingService>())
		{
			Fishing->SuspendFishingAndReleaseOperators();
		}
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
		RunPublicState.QuotaTarget = DayStartAttributeTarget;
		RunPublicState.QuotaProgress = DayStartAttributeProgress;
		UE_LOG(LogCatRun, Display, TEXT("Event=RunStartDayGEApplied World=%s NetMode=%d Authority=%s Day=%d BaseQuotaTarget=%d Multiplier=%.3f DailyPressure=%.3f OldTarget=%.0f NewTarget=%d Revision=%lld"),
			GetWorld() ? *GetWorld()->GetName() : TEXT("None"), GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : -1,
			HasAuthority() ? TEXT("true") : TEXT("false"), RunPublicState.Phase.DayIndex, DayQuotaTarget,
			DayStartQuotaTargetMultiplier, DayStartDailyPressure, DayStartOldTarget,
			RunPublicState.QuotaTarget, RunPublicState.Revision);
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
		bShouldScheduleDayEnvironmentRefreshes = true;
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
	if (bShouldScheduleDayEnvironmentRefreshes)
	{
		ScheduleDayEnvironmentRefreshes();
	}
	RefreshEnvironmentAndPublish();
	UE_LOG(LogCatRun, Log, TEXT("Event=run_phase_entered RunId=%s Revision=%lld Day=%d Phase=%s Reason=%s Deadline=%.3f"),
		*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Revision,
		RunPublicState.Phase.DayIndex, *UEnum::GetValueAsString(NewPhase), *UEnum::GetValueAsString(Reason),
		RunPublicState.Phase.DeadlineServerTimeSeconds);
#if !UE_BUILD_SHIPPING
	ContinueDebugSkipToNextDayAfterPhaseEntered(NewPhase);
#endif
	return Result;
}

// Result 条件读取流程：只比较最近一次由阶段入口或事件提交写下的原因；不根据当前 Phase、额度或 ready 集合重建历史。
bool ACatfishingGameModeBase::DoesLastRunFlowResultMatch(const ECatRunTransitionReason ExpectedReason) const
{
	return LastRunFlowResult.bApplied && LastRunFlowResult.Error == ECatRunCommandError::None
		&& LastRunFlowResult.Reason == ExpectedReason;
}

// 玩家额度提交流程：服务器重建身份后汇入同一个 Run 写口；身份以外的载荷、属性预演、GE 应用、Revision 和 StateTree 事件都不在 Controller 分支重复实现。
FCatRunCommandResult ACatfishingGameModeBase::SubmitQuotaContribution(AController* RequestingController, const FCatQuotaContributionCommand& Command)
{
	FCatQuotaContributionCommand ServerCommand = Command;
	if (!FillServerCommandIdentity(RequestingController, ServerCommand.Context))
	{
		return MakeRunCommandResult(Command.Context.RequestId, false, ECatRunCommandError::InvalidIdentity);
	}
	return SubmitQuotaContributionInternal(ServerCommand);
}

// 献祭预检流程：只读验证服务器身份键、命令 gate、Phase、Revision、正贡献，并用 Run ASC 预演效率后的实际贡献与潜在达标事件依赖；不写终态缓存，因 Items 尚可安全取消预留。
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
	int32 AppliedContribution = 0;
	int64 NewProgress = 0;
	const ECatRunCommandError PreviewError = PreviewRunSacrificeContribution(Command, AppliedContribution, NewProgress);
	if (PreviewError != ECatRunCommandError::None)
	{
		return MakeRunCommandResult(Command.Context.RequestId, false, PreviewError);
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

// 额度内部流程：先查完整幂等缓存，再校验 gate/Phase/Revision/载荷；首次写入前通过 Run ASC 预演实际贡献、溢出和达标事件依赖，通过后应用献祭 GE 并把 AttributeSet 进度投影到 RunPublicState。未达标直接发布同阶段快照；达标时先释放钓鱼操作位和移动锁，再关闭写口、停白天计时、发布过渡快照并发送唯一 StateTree 事件。
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
	if (ServerCommand.Contribution <= 0)
	{
		return CacheRunCommandResult(CacheKey, MakeRunCommandResult(ServerCommand.Context.RequestId, false, ECatRunCommandError::InvalidPayload));
	}
	int32 AppliedContribution = 0;
	int64 NewProgress = 0;
	const ECatRunCommandError PreviewError = PreviewRunSacrificeContribution(ServerCommand, AppliedContribution, NewProgress);
	if (PreviewError != ECatRunCommandError::None)
	{
		return CacheRunCommandResult(CacheKey, MakeRunCommandResult(ServerCommand.Context.RequestId, false, PreviewError));
	}
	bool bReachesQuota = NewProgress >= RunPublicState.QuotaTarget;
	if (bReachesQuota && (!RunStateTreeComponent || !RunStateTreeComponent->IsRunning()))
	{
		return CacheRunCommandResult(CacheKey, MakeRunCommandResult(ServerCommand.Context.RequestId, false, ECatRunCommandError::StateTreeUnavailable));
	}
	ACatfishingGameState* RunGameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
	UAbilitySystemComponent* RunASC = RunGameState ? RunGameState->GetRunAbilitySystemComponentFromAuthority() : nullptr;
	if (!RunASC)
	{
		UE_LOG(LogCatRun, Error, TEXT("Event=RunSacrificeGEFailed Reason=RunASCUnavailable World=%s RequestId=%s"),
			GetWorld() ? *GetWorld()->GetName() : TEXT("None"), *ServerCommand.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens));
		return CacheRunCommandResult(CacheKey, MakeRunCommandResult(ServerCommand.Context.RequestId, false, ECatRunCommandError::DependencyUnavailable));
	}
	const float OldProgress = RunASC->GetNumericAttribute(UCatRunAttributeSet::GetQuotaProgressAttribute());
	const FGameplayEffectSpecHandle SacrificeSpec = RunASC->MakeOutgoingSpec(UCatGE_RunApplySacrifice::StaticClass(), 1.0f,
		RunASC->MakeEffectContext());
	if (!SacrificeSpec.IsValid())
	{
		UE_LOG(LogCatRun, Error, TEXT("Event=RunSacrificeGEFailed Reason=SpecUnavailable RequestId=%s"),
			*ServerCommand.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens));
		return CacheRunCommandResult(CacheKey, MakeRunCommandResult(ServerCommand.Context.RequestId, false, ECatRunCommandError::DependencyUnavailable));
	}
	SacrificeSpec.Data->SetSetByCallerMagnitude(CatFishingAbilityTags::Data_Run_Sacrifice_RawContribution,
		static_cast<float>(ServerCommand.Contribution));
	if (!RunASC->ApplyGameplayEffectSpecToSelf(*SacrificeSpec.Data.Get()).WasSuccessfullyApplied())
	{
		UE_LOG(LogCatRun, Error, TEXT("Event=RunSacrificeGEFailed Reason=ApplyRejected RequestId=%s"),
			*ServerCommand.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens));
		return CacheRunCommandResult(CacheKey, MakeRunCommandResult(ServerCommand.Context.RequestId, false, ECatRunCommandError::DependencyUnavailable));
	}
	const float NewProgressAttribute = RunASC->GetNumericAttribute(UCatRunAttributeSet::GetQuotaProgressAttribute());
	const int64 AttributeNewProgress = FMath::RoundToInt64(NewProgressAttribute);
	const int32 AttributeAppliedContribution = FMath::RoundToInt(NewProgressAttribute - OldProgress);
	if (AttributeNewProgress != NewProgress || AttributeAppliedContribution != AppliedContribution)
	{
		UE_LOG(LogCatRun, Warning, TEXT("Event=RunAttributeProjectionMismatch Reason=SacrificePreviewDrift RequestId=%s PreviewApplied=%d AttributeApplied=%d PreviewProgress=%lld AttributeProgress=%lld"),
			*ServerCommand.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens), AppliedContribution,
			AttributeAppliedContribution, NewProgress, AttributeNewProgress);
		AppliedContribution = AttributeAppliedContribution;
		NewProgress = AttributeNewProgress;
		bReachesQuota = NewProgress >= RunPublicState.QuotaTarget;
	}

	RunPublicState.QuotaProgress = static_cast<int32>(NewProgress);
	if (FMath::RoundToInt(RunASC->GetNumericAttribute(UCatRunAttributeSet::GetQuotaTargetAttribute())) != RunPublicState.QuotaTarget
		|| FMath::RoundToInt(NewProgressAttribute) != RunPublicState.QuotaProgress)
	{
		UE_LOG(LogCatRun, Warning, TEXT("Event=RunAttributeProjectionMismatch AttributeTarget=%d AttributeProgress=%d DtoTarget=%d DtoProgress=%d"),
			FMath::RoundToInt(RunASC->GetNumericAttribute(UCatRunAttributeSet::GetQuotaTargetAttribute())),
			FMath::RoundToInt(NewProgressAttribute), RunPublicState.QuotaTarget, RunPublicState.QuotaProgress);
	}
	UE_LOG(LogCatRun, Display, TEXT("Event=RunSacrificeGEApplied World=%s NetMode=%d RequestId=%s RawContribution=%d SacrificeEfficiency=%.3f AppliedContribution=%d OldProgress=%.0f NewProgress=%d QuotaTarget=%d Reached=%s"),
		GetWorld() ? *GetWorld()->GetName() : TEXT("None"), GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : -1,
		*ServerCommand.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens), ServerCommand.Contribution,
		RunASC->GetNumericAttribute(UCatRunModifierAttributeSet::GetSacrificeEfficiencyAttribute()), AppliedContribution,
		OldProgress, RunPublicState.QuotaProgress, RunPublicState.QuotaTarget, bReachesQuota ? TEXT("true") : TEXT("false"));
	++RunPublicState.Revision;
	ECatRunTransitionReason TransitionReason = ECatRunTransitionReason::None;
	if (bReachesQuota)
	{
		TransitionReason = ECatRunTransitionReason::QuotaReached;
		// 额度完成会立即关闭 Fishing gate；先释放操作位，避免过渡到夜晚前留下无法解开的移动锁。
		if (UCatFishingService* Fishing = GetWorld()->GetSubsystem<UCatFishingService>())
		{
			Fishing->SuspendFishingAndReleaseOperators();
		}
		RunPublicState.Phase.bQuotaOpen = false;
		RunPublicState.Phase.bFishingAllowed = false;
		ClearDayTimers();
		RefreshEnvironmentAndPublish();
	}
	else
	{
		RefreshEnvironmentAndPublish();
	}
	FCatRunCommandResult Result = MakeRunCommandResult(ServerCommand.Context.RequestId, true, ECatRunCommandError::None, TransitionReason);
	Result.AppliedContribution = AppliedContribution;
	Result = CacheRunCommandResult(CacheKey, Result);
	if (bReachesQuota)
	{
		SendRunStateTreeEvent(CatRunStateTreeEvents::QuotaReached, TransitionReason);
	}
	return Result;
}

// 翻天确认流程：
// 1. 先由服务器重建命令身份并处理幂等重放，客户端提交的身份字段不能成为权威依据。
// 2. 再校验命令门、普通夜晚阶段、Revision 和夜晚冻结资格；全员 ready 事件已经发出后，普通玩家重复 ready 仍按窗口关闭拒绝。
// 3. 通过后只写夜晚 ready 集合和对应 PlayerState；ready 事实真实变化时才递增 Revision 并发布 RunPublicState。
// 4. 最后调用 EvaluateAllEligibleReady 发送正式 AllEligibleReady 事件；本方法不直接写 Phase 或 DayIndex。
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

// 夜晚 ready 完整性判断流程：先要求普通夜晚和非空资格集合，再逐个确认资格 StableNetId 都已经进入 ready 集合；额外 ready 不会单独触发翻天。
bool ACatfishingGameModeBase::IsAllNightReadyComplete() const
{
	if (RunPublicState.Phase.Phase != ECatRunPhase::NormalNight || NightReadyEligibleIds.IsEmpty())
	{
		return false;
	}
	for (const FString& StableNetId : NightReadyEligibleIds)
	{
		if (!NightReadyIds.Contains(StableNetId))
		{
			return false;
		}
	}
	return true;
}

// AllEligibleReady 发送流程：先只读确认资格集合已经全员 ready；首次发送会关闭撤销窗口，调试重发只在仍停普通夜晚时再次投递同一个 StateTree 事件，不直接改 Phase 或天数。
bool ACatfishingGameModeBase::SendAllEligibleReadyEventIfComplete(const TCHAR* Trigger, const bool bAllowResend)
{
	const TCHAR* TriggerText = Trigger ? Trigger : TEXT("Unknown");
	if (!IsAllNightReadyComplete())
	{
		return false;
	}
	if (bAllEligibleReadyEventSent && !bAllowResend)
	{
		return true;
	}

	const bool bWasAlreadySent = bAllEligibleReadyEventSent;
	bAllEligibleReadyEventSent = true;
	const bool bSent = SendRunStateTreeEvent(CatRunStateTreeEvents::AllEligibleReady,
		ECatRunTransitionReason::AllEligibleReady);
	if (!bSent && !bWasAlreadySent)
	{
		bAllEligibleReadyEventSent = false;
	}
	if (bWasAlreadySent && bSent)
	{
		UE_LOG(LogCatRun, Warning,
			TEXT("Event=run_environment_social_all_ready_event_retried Trigger=%s RunId=%s Revision=%lld Day=%d Eligible=%d Ready=%d"),
			TriggerText, *RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens),
			RunPublicState.Revision, RunPublicState.Phase.DayIndex,
			NightReadyEligibleIds.Num(), NightReadyIds.Num());
	}
	return bSent;
}

// 全员确认求值流程：仅在普通夜、集合非空、尚未发事件且 ready 覆盖合资格集合时关闭窗口并发送一次事件；本方法不改写 Phase。
void ACatfishingGameModeBase::EvaluateAllEligibleReady()
{
	if (bAllEligibleReadyEventSent)
	{
		return;
	}
	SendAllEligibleReadyEventIfComplete(TEXT("EvaluateAllEligibleReady"), false);
}

// 白天计时清理流程：从当前 World 清除截止、Morning 和 Dusk 三个 one-shot 句柄；只停止未来回调，不改公开 deadline 事实。
void ACatfishingGameModeBase::ClearDayTimers()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DayDeadlineTimerHandle);
		World->GetTimerManager().ClearTimer(DayMorningEnvironmentRefreshTimerHandle);
		World->GetTimerManager().ClearTimer(DayDuskEnvironmentRefreshTimerHandle);
	}
	DayDeadlineTimerHandle.Invalidate();
	DayMorningEnvironmentRefreshTimerHandle.Invalidate();
	DayDuskEnvironmentRefreshTimerHandle.Invalidate();
}

// 截止清理流程：先清所有白天计时回调，再同步清空公开 deadline 字段；只在进入新 Phase、启动失败或 teardown 时使用。
void ACatfishingGameModeBase::ClearDayDeadline()
{
	ClearDayTimers();
	RunPublicState.Phase.bHasDeadline = false;
	RunPublicState.Phase.DeadlineServerTimeSeconds = 0.0;
}

// 白天刷新安排流程：读取 Environment 配置换算 Morning/Day/Dusk 分界，再把未来分界安排成本 GameMode 的 one-shot；分界到达只会重发公开快照。
void ACatfishingGameModeBase::ScheduleDayEnvironmentRefreshes()
{
	UWorld* World = GetWorld();
	const UCatEnvironmentSettings* Settings = GetDefault<UCatEnvironmentSettings>();
	double MorningEndServerTimeSeconds = 0.0;
	double DuskStartServerTimeSeconds = 0.0;
	if (!World || !Settings || !Settings->TryResolveTimeOfDayRefreshTimes(RunPublicState.Phase,
		MorningEndServerTimeSeconds, DuskStartServerTimeSeconds))
	{
		UE_LOG(LogCatEnvironment, Warning, TEXT("Event=environment_day_refresh_schedule_skipped RunId=%s Revision=%lld Day=%d"),
			*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Revision,
			RunPublicState.Phase.DayIndex);
		return;
	}
	const double ServerNowSeconds = World->GetTimeSeconds();
	if (MorningEndServerTimeSeconds > ServerNowSeconds)
	{
		World->GetTimerManager().SetTimer(DayMorningEnvironmentRefreshTimerHandle, this,
			&ThisClass::HandleDayEnvironmentRefreshElapsed,
			static_cast<float>(MorningEndServerTimeSeconds - ServerNowSeconds), false);
	}
	if (DuskStartServerTimeSeconds > ServerNowSeconds)
	{
		World->GetTimerManager().SetTimer(DayDuskEnvironmentRefreshTimerHandle, this,
			&ThisClass::HandleDayEnvironmentRefreshElapsed,
			static_cast<float>(DuskStartServerTimeSeconds - ServerNowSeconds), false);
	}
	UE_LOG(LogCatEnvironment, Log, TEXT("Event=environment_day_refresh_scheduled RunId=%s Revision=%lld Day=%d MorningAt=%.3f DuskAt=%.3f"),
		*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Revision,
		RunPublicState.Phase.DayIndex, MorningEndServerTimeSeconds, DuskStartServerTimeSeconds);
}

// 白天语义刷新流程：只在同一个 DayActive 仍有 deadline 且 quota 仍开放时递增 Revision 并重新求值环境；到夜晚的推进仍完全交给 StateTree。
void ACatfishingGameModeBase::HandleDayEnvironmentRefreshElapsed()
{
	if (!HasAuthority() || !bRunCommandsOpen || RunPublicState.Phase.Phase != ECatRunPhase::DayActive
		|| !RunPublicState.Phase.bHasDeadline || !RunPublicState.Phase.bQuotaOpen)
	{
		return;
	}
	++RunPublicState.Revision;
	RefreshEnvironmentAndPublish();
	UE_LOG(LogCatEnvironment, Log, TEXT("Event=environment_day_segment_refreshed RunId=%s Revision=%lld Day=%d TimeOfDay=%s"),
		*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Revision,
		RunPublicState.Phase.DayIndex, *UEnum::GetValueAsString(RunPublicState.Environment.TimeOfDay));
}

// 白天截止流程：只消费仍开放的同一 DayActive，先关闭钓鱼/额度并停白天计时，保留公开 deadline 发布同 Revision 过渡快照，再向 StateTree 发送 QuotaFailed。
void ACatfishingGameModeBase::HandleDayDeadlineElapsed()
{
	DayDeadlineTimerHandle.Invalidate();
	if (!HasAuthority() || !bRunCommandsOpen || RunPublicState.Phase.Phase != ECatRunPhase::DayActive
		|| !RunPublicState.Phase.bQuotaOpen)
	{
		return;
	}
	// 截止时先收口钓鱼并恢复所有操作角色移动，再把新命令门关闭。
	if (UCatFishingService* Fishing = GetWorld()->GetSubsystem<UCatFishingService>())
	{
		Fishing->SuspendFishingAndReleaseOperators();
	}
	RunPublicState.Phase.bFishingAllowed = false;
	RunPublicState.Phase.bQuotaOpen = false;
	ClearDayTimers();
	++RunPublicState.Revision;
	RefreshEnvironmentAndPublish();
	SendRunStateTreeEvent(CatRunStateTreeEvents::QuotaFailed, ECatRunTransitionReason::QuotaFailed);
}

// 环境发布流程：以当前 Phase 与 Revision 调用只读 provider；成功且同 Revision 时替换环境 DTO，失败或版本不齐时发布同 Revision 空环境，最后把唯一公开聚合写入 GameState；本流程不写角色身体或表现状态。
bool ACatfishingGameModeBase::RefreshEnvironmentAndPublish()
{
	const ICatEnvironmentProvider* Provider = Cast<ICatEnvironmentProvider>(EnvironmentProvider);
	const FCatEnvironmentResult EnvironmentResult = Provider
		? Provider->EvaluateEnvironment(RunPublicState.Phase, RunPublicState.Revision)
		: FCatEnvironmentResult();
	const bool bEnvironmentSucceeded = EnvironmentResult.bSucceeded
		&& EnvironmentResult.Snapshot.SourceRunRevision == RunPublicState.Revision;
	if (bEnvironmentSucceeded)
	{
		RunPublicState.Environment = EnvironmentResult.Snapshot;
		SubmitNaturalChumFieldIfConfigured();
	}
	else
	{
		RunPublicState.Environment = FCatEnvironmentSnapshot();
		RunPublicState.Environment.SourceRunRevision = RunPublicState.Revision;
		const FString EnvironmentError = Provider && EnvironmentResult.bSucceeded
			? FString::Printf(TEXT("RevisionMismatch:%lld"), EnvironmentResult.Snapshot.SourceRunRevision)
			: (Provider ? EnvironmentResult.Error : FString(TEXT("ProviderUnavailable")));
		UE_LOG(LogCatRun, Error, TEXT("Event=environment_evaluation_failed RunId=%s Revision=%lld SourceRunRevision=%lld Error=%s"),
			*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Revision,
			EnvironmentResult.Snapshot.SourceRunRevision,
			EnvironmentError.IsEmpty() ? TEXT("Unknown") : *EnvironmentError);
	}
	ACatfishingGameState* CatGameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
	if (CatGameState)
	{
		CatGameState->SetRunPublicStateFromAuthority(RunPublicState);
	}
	return bEnvironmentSucceeded && CatGameState != nullptr;
}

// 自然聚鱼流程：读取 Environment 显式事件与锚点后按 Run+Day+Event+Anchor 去重，扫描唯一同 ID WaterRegion；构造系统身份命令并提交同一聚鱼写口，只有 committed 才记录去重键。
void ACatfishingGameModeBase::SubmitNaturalChumFieldIfConfigured()
{
	if (!HasAuthority() || !RunPublicState.Environment.bHasActiveEvent || !GetWorld())
	{
		return;
	}
	FName ChumDefinitionId;
	FName AnchorId;
	if (!GetDefault<UCatEnvironmentSettings>()->TryGetNaturalChumField(ChumDefinitionId, AnchorId))
	{
		return;
	}
	const FString EventKey = FString::Printf(TEXT("%s|%d|%s|%s"),
		*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Phase.DayIndex,
		*RunPublicState.Environment.ActiveEventId.ToString(), *AnchorId.ToString());
	if (SubmittedNaturalChumFieldKeys.Contains(EventKey))
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
				UE_LOG(LogCatEnvironment, Error, TEXT("Event=natural_chum_rejected RunId=%s Day=%d EnvironmentEvent=%s Anchor=%s Error=AmbiguousAnchor"),
					*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens),
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
	UE_LOG(LogCatEnvironment, Log, TEXT("Event=natural_chum_terminal RequestId=%s RunId=%s Day=%d EnvironmentEvent=%s Definition=%s Anchor=%s Committed=%s Error=%s Revision=%lld"),
		*Request.Command.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Phase.DayIndex,
		*RunPublicState.Environment.ActiveEventId.ToString(), *ChumDefinitionId.ToString(), *AnchorId.ToString(),
		Result.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Error),
		Result.ChumFieldSetRevision);
}

// StateTree 事件提交流程：先验证组件正在运行与 Tag 有效，再保存一份不改变 Phase 的结构化结果并发送事件；最后写一条默认可见日志，让房主端能核对“事件已入队”和后续“阶段已进入”是否成对出现。
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
	UE_LOG(LogCatRun, Display, TEXT("Event=run_state_tree_event_sent RunId=%s Revision=%lld Day=%d Phase=%s EventTag=%s Reason=%s"),
		*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Revision,
		RunPublicState.Phase.DayIndex, *UEnum::GetValueAsString(RunPublicState.Phase.Phase),
		*EventTag.ToString(), *UEnum::GetValueAsString(Reason));
	return true;
}

// 启动失败流程：先释放可能残留的钓鱼操作位和移动锁，再停止已启动的 StateTree、保持 NotStarted、清计时器与写口、写 StartupFailed 并递增 Revision；不会启动备用 C++ FSM 或假装 StateTree 已运行。
void ACatfishingGameModeBase::FailRunStartup(const TCHAR* Reason)
{
	if (UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr)
	{
		Fishing->SuspendFishingAndReleaseOperators();
	}
	bRunCommandsOpen = false;
	ClearDayDeadline();
	if (RunStateTreeComponent && RunStateTreeComponent->IsRunning())
	{
		RunStateTreeComponent->StopLogic(TEXT("RunStartupFailed"));
	}
	RunPublicState.Phase.Phase = ECatRunPhase::NotStarted;
	RunPublicState.Phase.bFishingAllowed = false;
	RunPublicState.Phase.bQuotaOpen = false;
	RunPublicState.EndReason = ECatRunEndReason::StartupFailed;
	++RunPublicState.Revision;
	RefreshEnvironmentAndPublish();
	UE_LOG(LogCatRun, Error, TEXT("Event=run_startup_failed RunId=%s Revision=%lld Reason=%s"),
		*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Revision, Reason);
}

// Host teardown 流程：先重放已 Ready 的新 Online 关联键，Pending 期则只接受原 RequestId/epoch；首次请求必须同时具备 Imprint 与牺牲协调器。领域协调器按 Social→Items 等顺序关不可逆命令，Imprint 先最终重投 Grant，然后才关 Run/Timer/StateTree、发 HostExit 并等远端 Destroy ACK；只有远端 ACK 与 durable Grant ACK 全齐才 Ready，不用计时器把等待伪装成完成。
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
	Result.Status = bHostExitAckWaitComplete ? ECatRunTeardownStatus::Ready : ECatRunTeardownStatus::Pending;
	const int32 PendingGrantAcks = ImprintService->GetPendingGrantAckCount();
	UE_LOG(LogCatRun, Log, TEXT("Event=run_teardown_%s RequestId=%s Epoch=%lld Revision=%lld PendingRemoteAcks=%d PendingGrantAcks=%d"),
		bHostExitAckWaitComplete ? TEXT("ready") : TEXT("pending"),
		*Request.RequestId.ToString(EGuidFormats::DigitsWithHyphens), Request.OperationEpoch, RunPublicState.Revision,
		PendingHostExitAckStableNetIds.Num(), PendingGrantAcks);
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
		CompleteHostExitAckWait();
	}
}

// Host exit ACK 完成流程：只在远端 Destroy ACK 与最终 Grant ACK 全部真实到达后发布 teardown complete；重复调用保持幂等。
void ACatfishingGameModeBase::CompleteHostExitAckWait()
{
	if (bHostExitAckWaitComplete || !ActiveHostExitRequestId.IsValid() || ActiveHostExitOperationEpoch <= 0)
	{
		return;
	}
	bHostExitAckWaitComplete = true;
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
	UE_LOG(LogCatRun, Log, TEXT("Event=run_teardown_acks_complete RequestId=%s Epoch=%lld MissingRemoteAcks=%d MissingGrantAcks=%d"),
		*Result.RequestId.ToString(EGuidFormats::DigitsWithHyphens), Result.OperationEpoch,
		MissingAckCount, MissingGrantAckCount);
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

#if !UE_BUILD_SHIPPING
// 开发期服务器快照流程：先创建一次性副本并标记当前实例是否有 authority；不是服务器时立刻返回空快照。服务器路径只复制私有门禁、StateTree 运行态、最近事件结果和跳天请求诊断给调试面板，既不写 GameState，也不发网络同步，避免形成第二套同步状态。
FCatRunAuthorityDebugSnapshot ACatfishingGameModeBase::GetAuthorityDebugSnapshotForDebug() const
{
	FCatRunAuthorityDebugSnapshot Snapshot;
	Snapshot.bHasAuthorityGameMode = HasAuthority();
	if (!Snapshot.bHasAuthorityGameMode)
	{
		return Snapshot;
	}

	Snapshot.bRunCommandsOpen = bRunCommandsOpen;
	Snapshot.bRunStateTreeAssigned = RunStateTreeComponent != nullptr;
	Snapshot.bRunStateTreeRunning = RunStateTreeComponent && RunStateTreeComponent->IsRunning();
	Snapshot.bRunStartupInProgress = bRunStartupInProgress;
	Snapshot.bAllEligibleReadyEventSent = bAllEligibleReadyEventSent;
	Snapshot.NightReadyEligibleCount = NightReadyEligibleIds.Num();
	Snapshot.NightReadyCount = NightReadyIds.Num();
	Snapshot.LastRunFlowResult = LastRunFlowResult;
	Snapshot.bDebugSkipToNextDayRequested = bDebugSkipToNextDayRequested;
	Snapshot.DebugSkipToNextDayRunId = DebugSkipToNextDayRunId;
	Snapshot.DebugSkipToNextDayDayIndex = DebugSkipToNextDayDayIndex;
	return Snapshot;
}

// 开发期跳天请求匹配流程：只比较本 GameMode 当前 RunId 与 DayIndex，判断迟到的下一帧 ready 是否还属于发起时那一天；它不读取客户端、不推进 StateTree。
bool ACatfishingGameModeBase::IsDebugSkipToNextDayRequestCurrent() const
{
	return bDebugSkipToNextDayRequested
		&& DebugSkipToNextDayRunId == RunPublicState.Phase.RunId
		&& DebugSkipToNextDayDayIndex == RunPublicState.Phase.DayIndex;
}

// 开发期跳天请求清理流程：只清空调试输入留下的短生命周期标记，不回滚已经经正式命令写入的额度、ready、Revision 或 Phase。
void ACatfishingGameModeBase::ClearDebugSkipToNextDayRequest()
{
	bDebugSkipToNextDayRequested = false;
	DebugSkipToNextDayRunId.Invalidate();
	DebugSkipToNextDayDayIndex = 0;
}

// 开发期补额度玩家选择流程：扫描服务器当前可见 Controller，返回第一名仍通过正式玩法命令 gate 的 Active 玩家；找不到时调试指令失败，不伪造系统玩家。
APlayerController* ACatfishingGameModeBase::FindDebugQuotaCompletionController() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Controller = It->Get();
		if (Controller && CanAcceptGameplayCommand(Controller))
		{
			return Controller;
		}
	}
	return nullptr;
}

// 开发期夜晚 ready 玩家解析流程：以夜晚冻结的 StableNetId 为主键读取 AdmissionRecords，只返回仍处于 Active 且通过正式宽命令 gate 的 Controller；调试提交不会因为 World 迭代顺序选错玩家。
AController* ACatfishingGameModeBase::FindDebugNightReadyControllerByStableNetId(const FString& StableNetId) const
{
	const FAdmissionRecord* Record = StableNetId.IsEmpty() ? nullptr : AdmissionRecords.Find(StableNetId);
	AController* Controller = Record && Record->Phase == EAdmissionPhase::Active ? Record->Controller.Get() : nullptr;
	return CanAcceptGameplayCommand(Controller) ? Controller : nullptr;
}

// 开发期补足当日额度流程：
// 1. 先校验当前仍是额度、钓鱼和截止时间都开放的 DayActive；不满足时只写拒绝日志并返回 false。
// 2. 再计算还差多少额度，差值无效或超过命令载荷范围时返回 false，避免调试输入制造非法贡献。
// 3. 然后选择一名真实 Active Controller 作为正式命令发起者；没有玩家时拒绝，不伪造系统身份。
// 4. 最后调用 SubmitQuotaContribution，只有首次提交成功且产生 QuotaReached 原因才返回 true；调用方只通过 Trigger 区分 SkipToNight 或 SkipToNextDay 诊断来源。
bool ACatfishingGameModeBase::SubmitDebugQuotaCompletionForCurrentDay(const TCHAR* Trigger)
{
	const TCHAR* TriggerText = Trigger ? Trigger : TEXT("Unknown");
	const FGuid RunId = RunPublicState.Phase.RunId;
	const int32 DayIndex = RunPublicState.Phase.DayIndex;
	const int64 Revision = RunPublicState.Revision;
	if (RunPublicState.Phase.Phase != ECatRunPhase::DayActive || !RunPublicState.Phase.bHasDeadline
		|| !RunPublicState.Phase.bFishingAllowed || !RunPublicState.Phase.bQuotaOpen || RunPublicState.QuotaTarget <= 0)
	{
		UE_LOG(LogCatRun, Warning,
			TEXT("Event=run_environment_social_debug_quota_completion_rejected Trigger=%s Reason=DayNotOpen RunId=%s Revision=%lld Day=%d Phase=%s HasDeadline=%s FishingAllowed=%s QuotaOpen=%s QuotaProgress=%d QuotaTarget=%d"),
			TriggerText, *RunId.ToString(EGuidFormats::DigitsWithHyphens), Revision, DayIndex,
			*UEnum::GetValueAsString(RunPublicState.Phase.Phase),
			RunPublicState.Phase.bHasDeadline ? TEXT("true") : TEXT("false"),
			RunPublicState.Phase.bFishingAllowed ? TEXT("true") : TEXT("false"),
			RunPublicState.Phase.bQuotaOpen ? TEXT("true") : TEXT("false"),
			RunPublicState.QuotaProgress, RunPublicState.QuotaTarget);
		return false;
	}

	const int64 RequiredContribution = static_cast<int64>(RunPublicState.QuotaTarget) - RunPublicState.QuotaProgress;
	if (RequiredContribution <= 0 || RequiredContribution > MAX_int32)
	{
		UE_LOG(LogCatRun, Warning,
			TEXT("Event=run_environment_social_debug_quota_completion_rejected Trigger=%s Reason=InvalidRequiredContribution RunId=%s Revision=%lld Day=%d QuotaProgress=%d QuotaTarget=%d RequiredContribution=%lld"),
			TriggerText, *RunId.ToString(EGuidFormats::DigitsWithHyphens), Revision, DayIndex,
			RunPublicState.QuotaProgress, RunPublicState.QuotaTarget, RequiredContribution);
		return false;
	}

	APlayerController* Controller = FindDebugQuotaCompletionController();
	if (!Controller)
	{
		UE_LOG(LogCatRun, Warning,
			TEXT("Event=run_environment_social_debug_quota_completion_rejected Trigger=%s Reason=NoActiveController RunId=%s Revision=%lld Day=%d"),
			TriggerText, *RunId.ToString(EGuidFormats::DigitsWithHyphens), Revision, DayIndex);
		return false;
	}

	FCatQuotaContributionCommand Command;
	Command.Context.RequestId = FGuid::NewGuid();
	Command.Context.ExpectedRevision = Revision;
	Command.Contribution = static_cast<int32>(RequiredContribution);
	const FCatRunCommandResult Result = SubmitQuotaContribution(Controller, Command);
	UE_LOG(LogCatRun, Display,
		TEXT("Event=run_environment_social_debug_quota_completion_submitted Trigger=%s Controller=%s RequestId=%s Contribution=%d Committed=%s Error=%s ResultRevision=%lld ResultPhase=%s TransitionReason=%s"),
		TriggerText, *GetNameSafe(Controller), *Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		Command.Contribution, Result.bCommitted ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(Result.Error), Result.Revision,
		*UEnum::GetValueAsString(Result.Phase), *UEnum::GetValueAsString(Result.TransitionReason));
	return Result.bCommitted && Result.TransitionReason == ECatRunTransitionReason::QuotaReached;
}

// 开发期跳天 ready 提交流程：
// 1. 先要求当前已经是普通夜晚；其他阶段返回 false，不把白天或结算伪装成 ready 窗口。
// 2. 如果全员 ready 事件已经发出但仍停在夜晚，就重投同一个正式 StateTree 事件，给人工调试一个不改 Phase 的解卡入口。
// 3. 再以夜晚冻结资格集合为驱动逐个找回 Active Controller，用最新 Revision 调 SubmitNextDayReady；不存在的 Controller 会被统计为不可提交，而不是悄悄伪造 ready。
// 4. 扫描中阶段变化或事件已发出就停止，最后按 attempted/committed/rejected/unavailable 和全员事件状态返回；没有可提交玩家、正式命令被拒或仍未全员 ready 时返回 false。
bool ACatfishingGameModeBase::SubmitDebugReadyForEligiblePlayers(const TCHAR* Trigger)
{
	const TCHAR* TriggerText = Trigger ? Trigger : TEXT("Unknown");
	if (RunPublicState.Phase.Phase != ECatRunPhase::NormalNight)
	{
		UE_LOG(LogCatRun, Warning,
			TEXT("Event=run_environment_social_debug_skip_to_next_day_rejected Reason=ReadyRequiresNormalNight Trigger=%s RunId=%s Revision=%lld Day=%d Phase=%s"),
			TriggerText, *RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens),
			RunPublicState.Revision, RunPublicState.Phase.DayIndex,
			*UEnum::GetValueAsString(RunPublicState.Phase.Phase));
		return false;
	}
	if (bAllEligibleReadyEventSent)
	{
		const bool bRetried = SendAllEligibleReadyEventIfComplete(TriggerText, true);
		UE_LOG(LogCatRun, Display,
			TEXT("Event=run_environment_social_debug_skip_to_next_day_waiting_for_advance Trigger=%s Retried=%s RunId=%s Revision=%lld Day=%d Eligible=%d Ready=%d"),
			TriggerText, bRetried ? TEXT("true") : TEXT("false"),
			*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens),
			RunPublicState.Revision, RunPublicState.Phase.DayIndex,
			NightReadyEligibleIds.Num(), NightReadyIds.Num());
		return bRetried;
	}
	if (NightReadyEligibleIds.IsEmpty())
	{
		UE_LOG(LogCatRun, Warning,
			TEXT("Event=run_environment_social_debug_skip_to_next_day_rejected Reason=NoNightReadyEligibility Trigger=%s RunId=%s Revision=%lld Day=%d"),
			TriggerText, *RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens),
			RunPublicState.Revision, RunPublicState.Phase.DayIndex);
		return false;
	}

	int32 AttemptedCount = 0;
	int32 AlreadyReadyCount = 0;
	int32 CommittedCount = 0;
	int32 RejectedCount = 0;
	int32 UnavailableCount = 0;
	for (const FString& StableNetId : NightReadyEligibleIds)
	{
		if (NightReadyIds.Contains(StableNetId))
		{
			++AlreadyReadyCount;
			continue;
		}
		if (RunPublicState.Phase.Phase != ECatRunPhase::NormalNight || bAllEligibleReadyEventSent)
		{
			break;
		}

		AController* Controller = FindDebugNightReadyControllerByStableNetId(StableNetId);
		if (!Controller)
		{
			++UnavailableCount;
			UE_LOG(LogCatRun, Warning,
				TEXT("Event=run_environment_social_debug_skip_to_next_day_ready_skipped Trigger=%s Reason=EligibleControllerUnavailable StableNetId=Valid(Redacted) RunId=%s Revision=%lld Day=%d"),
				TriggerText, *RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens),
				RunPublicState.Revision, RunPublicState.Phase.DayIndex);
			continue;
		}

		FCatNextDayReadyCommand Command;
		Command.Context.RequestId = FGuid::NewGuid();
		Command.Context.ExpectedRevision = RunPublicState.Revision;
		Command.bReady = true;
		const FCatRunCommandResult Result = SubmitNextDayReady(Controller, Command);
		++AttemptedCount;
		if (Result.bCommitted)
		{
			++CommittedCount;
		}
		else
		{
			++RejectedCount;
		}
		UE_LOG(LogCatRun, Display,
			TEXT("Event=run_environment_social_debug_skip_to_next_day_ready_submitted Trigger=%s Controller=%s RequestId=%s Committed=%s Error=%s ResultRevision=%lld ResultPhase=%s TransitionReason=%s"),
			TriggerText, *GetNameSafe(Controller),
			*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			Result.bCommitted ? TEXT("true") : TEXT("false"),
			*UEnum::GetValueAsString(Result.Error), Result.Revision,
			*UEnum::GetValueAsString(Result.Phase), *UEnum::GetValueAsString(Result.TransitionReason));
	}

	if (!bAllEligibleReadyEventSent)
	{
		SendAllEligibleReadyEventIfComplete(TriggerText, false);
	}
	const bool bReadyCompleted = RunPublicState.Phase.Phase != ECatRunPhase::NormalNight || bAllEligibleReadyEventSent;
	UE_LOG(LogCatRun, Display,
		TEXT("Event=run_environment_social_debug_skip_to_next_day_ready_summary Trigger=%s Attempted=%d AlreadyReady=%d Committed=%d Rejected=%d Unavailable=%d Eligible=%d Ready=%d AllReadyEventSent=%s RunId=%s Revision=%lld Day=%d Phase=%s"),
		TriggerText, AttemptedCount, AlreadyReadyCount, CommittedCount, RejectedCount, UnavailableCount,
		NightReadyEligibleIds.Num(), NightReadyIds.Num(),
		bAllEligibleReadyEventSent ? TEXT("true") : TEXT("false"),
		*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Revision,
		RunPublicState.Phase.DayIndex, *UEnum::GetValueAsString(RunPublicState.Phase.Phase));
	return UnavailableCount == 0 && RejectedCount == 0
		&& (AttemptedCount > 0 || AlreadyReadyCount > 0) && bReadyCompleted;
}

// 开发期跳天阶段续接流程：只在 StateTree 已经正式进入阶段、且请求仍属于同一 Run 时工作；进普通夜晚就安排下一帧正式 ready，进新白天或结算/结束就清请求。
void ACatfishingGameModeBase::ContinueDebugSkipToNextDayAfterPhaseEntered(const ECatRunPhase EnteredPhase)
{
	if (!bDebugSkipToNextDayRequested || DebugSkipToNextDayRunId != RunPublicState.Phase.RunId)
	{
		return;
	}

	if (EnteredPhase == ECatRunPhase::NormalNight && RunPublicState.Phase.DayIndex == DebugSkipToNextDayDayIndex)
	{
		ScheduleDebugSkipToNextDayReadySubmission();
		return;
	}
	if (EnteredPhase == ECatRunPhase::DayActive && RunPublicState.Phase.DayIndex > DebugSkipToNextDayDayIndex)
	{
		UE_LOG(LogCatRun, Display,
			TEXT("Event=run_environment_social_debug_skip_to_next_day_advanced RunId=%s Revision=%lld PreviousDay=%d CurrentDay=%d Phase=%s"),
			*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Revision,
			DebugSkipToNextDayDayIndex, RunPublicState.Phase.DayIndex,
			*UEnum::GetValueAsString(RunPublicState.Phase.Phase));
		ClearDebugSkipToNextDayRequest();
		return;
	}
	if (EnteredPhase == ECatRunPhase::FailureSettlementNight || EnteredPhase == ECatRunPhase::SuccessSettlementNight
		|| EnteredPhase == ECatRunPhase::Ending || EnteredPhase == ECatRunPhase::Ended
		|| EnteredPhase == ECatRunPhase::NotStarted)
	{
		UE_LOG(LogCatRun, Warning,
			TEXT("Event=run_environment_social_debug_skip_to_next_day_stopped RunId=%s Revision=%lld RequestedDay=%d CurrentDay=%d Phase=%s"),
			*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Revision,
			DebugSkipToNextDayDayIndex, RunPublicState.Phase.DayIndex,
			*UEnum::GetValueAsString(RunPublicState.Phase.Phase));
		ClearDebugSkipToNextDayRequest();
	}
}

// 开发期跳天 ready 延迟安排流程：把 ready 提交放到下一帧，避开 StateTree EnterPhase 回调栈内重入 AllEligibleReady；下一帧仍会重新核对 Run 与天数。
void ACatfishingGameModeBase::ScheduleDebugSkipToNextDayReadySubmission()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	World->GetTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateUObject(this, &ThisClass::HandleDebugSkipToNextDayReadyElapsed));
}

// 开发期跳天 ready 延迟执行流程：先确认请求没有跨 Run/跨天，再通过正式 SubmitNextDayReady 提交；提交失败时清掉调试请求，避免界面一直显示一个不会再推进的旧输入。
void ACatfishingGameModeBase::HandleDebugSkipToNextDayReadyElapsed()
{
	if (!IsDebugSkipToNextDayRequestCurrent())
	{
		UE_LOG(LogCatRun, Display,
			TEXT("Event=run_environment_social_debug_skip_to_next_day_stale_ready RunId=%s Revision=%lld RequestedRunId=%s RequestedDay=%d CurrentDay=%d Phase=%s"),
			*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Revision,
			*DebugSkipToNextDayRunId.ToString(EGuidFormats::DigitsWithHyphens), DebugSkipToNextDayDayIndex,
			RunPublicState.Phase.DayIndex, *UEnum::GetValueAsString(RunPublicState.Phase.Phase));
		ClearDebugSkipToNextDayRequest();
		return;
	}
	if (RunPublicState.Phase.Phase != ECatRunPhase::NormalNight)
	{
		UE_LOG(LogCatRun, Warning,
			TEXT("Event=run_environment_social_debug_skip_to_next_day_rejected Reason=ReadyDelayWrongPhase RunId=%s Revision=%lld Day=%d Phase=%s"),
			*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Revision,
			RunPublicState.Phase.DayIndex, *UEnum::GetValueAsString(RunPublicState.Phase.Phase));
		ClearDebugSkipToNextDayRequest();
		return;
	}
	if (!SubmitDebugReadyForEligiblePlayers(TEXT("PhaseEnteredNextTick"))
		&& !bAllEligibleReadyEventSent)
	{
		ClearDebugSkipToNextDayRequest();
	}
}

// 开发期跳到夜晚入口流程：
// 1. 先拒绝无 authority 或无 World 的调用，保证指令只在服务器权威侧生效。
// 2. 如果当前已经是普通夜晚，直接返回 true 并写日志，避免为了确认状态而重复提交 ready。
// 3. 如果当前白天已经因额度完成关闭 quota/fishing，则认为正在等待 StateTree 入夜，不追加第二条额度命令。
// 4. 开放 DayActive 才复用正式额度补足入口发送 QuotaReached；其他阶段只拒绝并清掉跳天调试请求。
bool ACatfishingGameModeBase::ApplyDebugSkipToNight()
{
	if (!HasAuthority() || !GetWorld())
	{
		UE_LOG(LogCatRun, Warning,
			TEXT("Event=run_environment_social_debug_skip_to_night_rejected Reason=AuthorityOrWorldUnavailable"));
		return false;
	}

	if (RunPublicState.Phase.Phase == ECatRunPhase::NormalNight)
	{
		UE_LOG(LogCatRun, Display,
			TEXT("Event=run_environment_social_debug_skip_to_night_already_night RunId=%s Revision=%lld Day=%d"),
			*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens),
			RunPublicState.Revision, RunPublicState.Phase.DayIndex);
		return true;
	}
	if (RunPublicState.Phase.Phase == ECatRunPhase::DayActive
		&& (!RunPublicState.Phase.bFishingAllowed || !RunPublicState.Phase.bQuotaOpen))
	{
		UE_LOG(LogCatRun, Display,
			TEXT("Event=run_environment_social_debug_skip_to_night_waiting RunId=%s Revision=%lld Day=%d HasDeadline=%s FishingAllowed=%s QuotaOpen=%s"),
			*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens),
			RunPublicState.Revision, RunPublicState.Phase.DayIndex,
			RunPublicState.Phase.bHasDeadline ? TEXT("true") : TEXT("false"),
			RunPublicState.Phase.bFishingAllowed ? TEXT("true") : TEXT("false"),
			RunPublicState.Phase.bQuotaOpen ? TEXT("true") : TEXT("false"));
		return true;
	}
	if (RunPublicState.Phase.Phase == ECatRunPhase::DayActive)
	{
		const bool bSubmitted = SubmitDebugQuotaCompletionForCurrentDay(TEXT("SkipToNight"));
		UE_LOG(LogCatRun, Display,
			TEXT("Event=run_environment_social_debug_skip_to_night_requested Accepted=%s RunId=%s Revision=%lld Day=%d Phase=%s"),
			bSubmitted ? TEXT("true") : TEXT("false"),
			*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens),
			RunPublicState.Revision, RunPublicState.Phase.DayIndex,
			*UEnum::GetValueAsString(RunPublicState.Phase.Phase));
		return bSubmitted;
	}

	UE_LOG(LogCatRun, Warning,
		TEXT("Event=run_environment_social_debug_skip_to_night_rejected Reason=InvalidPhase RunId=%s Revision=%lld Day=%d Phase=%s"),
		*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Revision,
		RunPublicState.Phase.DayIndex, *UEnum::GetValueAsString(RunPublicState.Phase.Phase));
	ClearDebugSkipToNextDayRequest();
	return false;
}

// 开发期跳天入口流程：
// 1. 先拒绝无 authority 或无 World 的调用，保证指令只在服务器权威侧生效。
// 2. 如果同一 Run/Day 已有请求，夜晚会补一次正式 ready 或重投已达成的 AllEligibleReady 事件，否则返回 true 表示旧请求仍在等待正式推进。
// 3. DayActive 会先记录请求所属 Run/Day，再提交正式额度补足；额度提交失败会立即清请求并返回 false。
// 4. NormalNight 会记录同一类请求并提交正式 ready；如果没有形成全员 ready 事件则清请求并返回 false。
// 5. 其他阶段不支持跳天，写拒绝日志、清理请求并返回 false；整个方法不直接写 Phase、DayIndex 或客户端 HUD。
bool ACatfishingGameModeBase::ApplyDebugSkipToNextDay()
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || !World)
	{
		UE_LOG(LogCatRun, Warning,
			TEXT("Event=run_environment_social_debug_skip_to_next_day_rejected Reason=AuthorityOrWorldUnavailable"));
		return false;
	}

	if (IsDebugSkipToNextDayRequestCurrent())
	{
		if (RunPublicState.Phase.Phase == ECatRunPhase::NormalNight)
		{
			return SubmitDebugReadyForEligiblePlayers(TEXT("CommandPendingNight"));
		}
		UE_LOG(LogCatRun, Display,
			TEXT("Event=run_environment_social_debug_skip_to_next_day_pending RunId=%s Revision=%lld Day=%d Phase=%s AllReadyEventSent=%s"),
			*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Revision,
			RunPublicState.Phase.DayIndex, *UEnum::GetValueAsString(RunPublicState.Phase.Phase),
			bAllEligibleReadyEventSent ? TEXT("true") : TEXT("false"));
		return true;
	}

	if (RunPublicState.Phase.Phase == ECatRunPhase::DayActive)
	{
		const FGuid RequestedRunId = RunPublicState.Phase.RunId;
		const int32 RequestedDayIndex = RunPublicState.Phase.DayIndex;
		bDebugSkipToNextDayRequested = true;
		DebugSkipToNextDayRunId = RequestedRunId;
		DebugSkipToNextDayDayIndex = RequestedDayIndex;
		if (!SubmitDebugQuotaCompletionForCurrentDay(TEXT("SkipToNextDay")))
		{
			ClearDebugSkipToNextDayRequest();
			return false;
		}
		UE_LOG(LogCatRun, Display,
			TEXT("Event=run_environment_social_debug_skip_to_next_day_requested RunId=%s RequestedDay=%d StartPhase=%s"),
			*RequestedRunId.ToString(EGuidFormats::DigitsWithHyphens), RequestedDayIndex,
			*UEnum::GetValueAsString(ECatRunPhase::DayActive));
		return true;
	}

	if (RunPublicState.Phase.Phase == ECatRunPhase::NormalNight)
	{
		const FGuid RequestedRunId = RunPublicState.Phase.RunId;
		const int32 RequestedDayIndex = RunPublicState.Phase.DayIndex;
		bDebugSkipToNextDayRequested = true;
		DebugSkipToNextDayRunId = RequestedRunId;
		DebugSkipToNextDayDayIndex = RequestedDayIndex;
		if (!SubmitDebugReadyForEligiblePlayers(TEXT("CommandNight"))
			&& !bAllEligibleReadyEventSent)
		{
			ClearDebugSkipToNextDayRequest();
			return false;
		}
		UE_LOG(LogCatRun, Display,
			TEXT("Event=run_environment_social_debug_skip_to_next_day_requested RunId=%s RequestedDay=%d StartPhase=%s"),
			*RequestedRunId.ToString(EGuidFormats::DigitsWithHyphens), RequestedDayIndex,
			*UEnum::GetValueAsString(ECatRunPhase::NormalNight));
		return true;
	}

	UE_LOG(LogCatRun, Warning,
		TEXT("Event=run_environment_social_debug_skip_to_next_day_rejected Reason=InvalidPhase RunId=%s Revision=%lld Day=%d Phase=%s"),
		*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Revision,
		RunPublicState.Phase.DayIndex, *UEnum::GetValueAsString(RunPublicState.Phase.Phase));
	ClearDebugSkipToNextDayRequest();
	return false;
}

// 开发期强制下一天流程：
// 1. 先要求服务器 authority、有效 Run、可用 StateTreeComponent、Run 配置和 ST_RunFlow 资产；未启动、局末、成功结算或 HostExit 拆局时拒绝，避免把正式收口救成半同步新天。
// 2. 再把救援范围收窄到两类：普通夜晚已经全员 ready 但 StateTree 卡住，或者失败结算夜为了人工测试继续跑后续天数。
// 3. 失败结算夜救援前只恢复商店命令门，让后续 DayActive 的 AdvanceShopDay 能按正式日推进；若 StateTree 没进入新白天，立刻关回商店，避免失败夜半恢复。
// 4. 随后清掉普通跳天请求、停止当前 StateTree、重新指定正式 ST_RunFlow，并用 StartLogic 进入初始 DayActive；DayActive 入口仍负责递增 DayIndex、清额度、重排 deadline、刷新 Environment 和复制 GameState。
// 5. 最后核对公开状态确实进入更大的 DayIndex；失败只写诊断日志，不在本方法里手工补写 Phase 或天数。
bool ACatfishingGameModeBase::ApplyDebugForceNextDay()
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || !World || !RunPublicState.Phase.RunId.IsValid() || !RunStateTreeComponent)
	{
		UE_LOG(LogCatRun, Warning,
			TEXT("Event=run_environment_social_debug_force_next_day_rejected Reason=AuthorityRunOrStateTreeUnavailable"));
		return false;
	}
	const ECatRunPhase PreviousPhase = RunPublicState.Phase.Phase;
	if (RunPublicState.EndReason == ECatRunEndReason::HostExit || ActiveHostExitRequestId.IsValid()
		|| !PendingHostExitAckStableNetIds.IsEmpty() || RunPublicState.bTeardownComplete)
	{
		UE_LOG(LogCatRun, Warning,
			TEXT("Event=run_environment_social_debug_force_next_day_rejected Reason=HostExitTeardownActive RunId=%s Revision=%lld Day=%d Phase=%s EndReason=%s PendingRemoteAcks=%d TeardownComplete=%s"),
			*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens),
			RunPublicState.Revision, RunPublicState.Phase.DayIndex,
			*UEnum::GetValueAsString(PreviousPhase), *UEnum::GetValueAsString(RunPublicState.EndReason),
			PendingHostExitAckStableNetIds.Num(),
			RunPublicState.bTeardownComplete ? TEXT("true") : TEXT("false"));
		return false;
	}
	const bool bRecoveringReadyStuckNormalNight = PreviousPhase == ECatRunPhase::NormalNight
		&& bAllEligibleReadyEventSent && IsAllNightReadyComplete();
	const bool bRecoveringFailureSettlementNight = PreviousPhase == ECatRunPhase::FailureSettlementNight
		&& RunPublicState.EndReason == ECatRunEndReason::QuotaFailed;
	if (!bRecoveringReadyStuckNormalNight && !bRecoveringFailureSettlementNight)
	{
		UE_LOG(LogCatRun, Warning,
			TEXT("Event=run_environment_social_debug_force_next_day_rejected Reason=UnsupportedRecoveryPhase RunId=%s Revision=%lld Day=%d Phase=%s EndReason=%s AllReadyEventSent=%s ReadyComplete=%s"),
			*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens),
			RunPublicState.Revision, RunPublicState.Phase.DayIndex,
			*UEnum::GetValueAsString(PreviousPhase), *UEnum::GetValueAsString(RunPublicState.EndReason),
			bAllEligibleReadyEventSent ? TEXT("true") : TEXT("false"),
			IsAllNightReadyComplete() ? TEXT("true") : TEXT("false"));
		return false;
	}
	const UCatRunSettings* Settings = GetDefault<UCatRunSettings>();
	float DayLengthSeconds = 0.0f;
	int32 QuotaTarget = 0;
	UStateTree* RunFlowAsset = Settings ? Settings->RunFlowStateTree.LoadSynchronous() : nullptr;
	if (!Settings || !Settings->TryGetDayParameters(DayLengthSeconds, QuotaTarget) || !RunFlowAsset)
	{
		UE_LOG(LogCatRun, Warning,
			TEXT("Event=run_environment_social_debug_force_next_day_rejected Reason=RunSettingsOrStateTreeAssetUnavailable RunId=%s Revision=%lld Day=%d Phase=%s"),
			*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens),
			RunPublicState.Revision, RunPublicState.Phase.DayIndex,
			*UEnum::GetValueAsString(PreviousPhase));
		return false;
	}

	const FGuid RunId = RunPublicState.Phase.RunId;
	const int32 PreviousDayIndex = RunPublicState.Phase.DayIndex;
	const int64 PreviousRevision = RunPublicState.Revision;
	if (bRecoveringFailureSettlementNight)
	{
		UCatShopEconomyService* Shop = World->GetSubsystem<UCatShopEconomyService>();
		if (!Shop || !Shop->ReopenCommandsForDebugForceNextDay())
		{
			UE_LOG(LogCatRun, Warning,
				TEXT("Event=run_environment_social_debug_force_next_day_rejected Reason=ShopRecoveryUnavailable RunId=%s Revision=%lld Day=%d Phase=%s"),
				*RunId.ToString(EGuidFormats::DigitsWithHyphens), PreviousRevision,
				PreviousDayIndex, *UEnum::GetValueAsString(PreviousPhase));
			return false;
		}
	}
	ClearDebugSkipToNextDayRequest();
	if (RunStateTreeComponent->IsRunning())
	{
		RunStateTreeComponent->StopLogic(TEXT("Debug Force Next Day"));
	}
	RunStateTreeComponent->SetStateTree(RunFlowAsset);
	bRunStartupInProgress = true;
	RunStateTreeComponent->StartLogic();
	bRunStartupInProgress = false;

	const bool bAdvanced = RunStateTreeComponent->IsRunning()
		&& RunPublicState.Phase.Phase == ECatRunPhase::DayActive
		&& RunPublicState.Phase.DayIndex > PreviousDayIndex;
	if (bAdvanced)
	{
		UE_LOG(LogCatRun, Log,
			TEXT("Event=run_environment_social_debug_force_next_day_result Accepted=true RunId=%s PreviousRevision=%lld CurrentRevision=%lld PreviousDay=%d CurrentDay=%d PreviousPhase=%s CurrentPhase=%s StateTreeRunning=%s"),
			*RunId.ToString(EGuidFormats::DigitsWithHyphens), PreviousRevision, RunPublicState.Revision,
			PreviousDayIndex, RunPublicState.Phase.DayIndex,
			*UEnum::GetValueAsString(PreviousPhase),
			*UEnum::GetValueAsString(RunPublicState.Phase.Phase),
			RunStateTreeComponent->IsRunning() ? TEXT("true") : TEXT("false"));
	}
	else
	{
		if (bRecoveringFailureSettlementNight)
		{
			CloseShopForSettlementNight();
		}
		UE_LOG(LogCatRun, Error,
			TEXT("Event=run_environment_social_debug_force_next_day_result Accepted=false ShopRolledBack=%s RunId=%s PreviousRevision=%lld CurrentRevision=%lld PreviousDay=%d CurrentDay=%d PreviousPhase=%s CurrentPhase=%s StateTreeRunning=%s"),
			bRecoveringFailureSettlementNight ? TEXT("true") : TEXT("false"),
			*RunId.ToString(EGuidFormats::DigitsWithHyphens), PreviousRevision, RunPublicState.Revision,
			PreviousDayIndex, RunPublicState.Phase.DayIndex,
			*UEnum::GetValueAsString(PreviousPhase),
			*UEnum::GetValueAsString(RunPublicState.Phase.Phase),
			RunStateTreeComponent->IsRunning() ? TEXT("true") : TEXT("false"));
	}
	return bAdvanced;
}

// 开发期白天长度调整流程：
// 1. 先校验 authority、World、有限正秒数、可用 timer 秒数和严格未来的服务器截止点；非法输入只写拒绝日志，不改公开状态。
// 2. 再确认当前仍是钓鱼与额度都开放的 DayActive，防止达标/截止后的过渡态被调试指令续命。
// 3. 通过同一份 RunPublicState 重写服务器时间锚点与截止点，重排 Deadline、Morning、Dusk 计时器。
// 4. 最后递增 Revision、刷新 Environment 并发布 GameState，让所有客户端仍走正常复制链看到结果。
bool ACatfishingGameModeBase::ApplyDebugDayLengthSeconds(const double NewDayLengthSeconds)
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || !World)
	{
		UE_LOG(LogCatRun, Warning,
			TEXT("Event=run_environment_social_debug_day_length_rejected Reason=AuthorityOrWorldUnavailable Seconds=%.3f"),
			NewDayLengthSeconds);
		return false;
	}
	if (!FMath::IsFinite(NewDayLengthSeconds) || NewDayLengthSeconds <= 0.0)
	{
		UE_LOG(LogCatRun, Warning,
			TEXT("Event=run_environment_social_debug_day_length_rejected Reason=InvalidSeconds Seconds=%.3f"),
			NewDayLengthSeconds);
		return false;
	}
	const float TimerSeconds = static_cast<float>(NewDayLengthSeconds);
	if (!FMath::IsFinite(TimerSeconds) || TimerSeconds <= 0.0f)
	{
		UE_LOG(LogCatRun, Warning,
			TEXT("Event=run_environment_social_debug_day_length_rejected Reason=InvalidTimerSeconds Seconds=%.9f"),
			NewDayLengthSeconds);
		return false;
	}
	if (!bRunCommandsOpen || RunPublicState.Phase.Phase != ECatRunPhase::DayActive
		|| !RunPublicState.Phase.bHasDeadline || !RunPublicState.Phase.bFishingAllowed
		|| !RunPublicState.Phase.bQuotaOpen)
	{
		UE_LOG(LogCatRun, Warning,
			TEXT("Event=run_environment_social_debug_day_length_rejected Reason=NotOpenActiveDay Seconds=%.3f Phase=%s HasDeadline=%s FishingAllowed=%s QuotaOpen=%s CommandsOpen=%s"),
			NewDayLengthSeconds, *UEnum::GetValueAsString(RunPublicState.Phase.Phase),
			RunPublicState.Phase.bHasDeadline ? TEXT("true") : TEXT("false"),
			RunPublicState.Phase.bFishingAllowed ? TEXT("true") : TEXT("false"),
			RunPublicState.Phase.bQuotaOpen ? TEXT("true") : TEXT("false"),
			bRunCommandsOpen ? TEXT("true") : TEXT("false"));
		return false;
	}

	const double ServerNow = World->GetTimeSeconds();
	const double NewDeadlineServerTimeSeconds = ServerNow + NewDayLengthSeconds;
	if (!FMath::IsFinite(NewDeadlineServerTimeSeconds) || NewDeadlineServerTimeSeconds <= ServerNow)
	{
		UE_LOG(LogCatRun, Warning,
			TEXT("Event=run_environment_social_debug_day_length_rejected Reason=InvalidDeadline Seconds=%.9f ServerNow=%.3f"),
			NewDayLengthSeconds, ServerNow);
		return false;
	}
	const double OldRemainingSeconds = FMath::Max(0.0,
		RunPublicState.Phase.DeadlineServerTimeSeconds - ServerNow);
	ClearDayTimers();
	RunPublicState.Phase.ServerTimeAnchorSeconds = ServerNow;
	RunPublicState.Phase.DeadlineServerTimeSeconds = NewDeadlineServerTimeSeconds;
	RunPublicState.Phase.bHasDeadline = true;
	World->GetTimerManager().SetTimer(DayDeadlineTimerHandle, this,
		&ThisClass::HandleDayDeadlineElapsed, TimerSeconds, false);
	++RunPublicState.Revision;
	ScheduleDayEnvironmentRefreshes();
	RefreshEnvironmentAndPublish();
	UE_LOG(LogCatRun, Display,
		TEXT("Event=run_environment_social_debug_day_length_applied RunId=%s Revision=%lld Day=%d OldRemaining=%.3f NewLength=%.3f Deadline=%.3f"),
		*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Revision,
		RunPublicState.Phase.DayIndex, OldRemainingSeconds, NewDayLengthSeconds,
		RunPublicState.Phase.DeadlineServerTimeSeconds);
	return true;
}
#endif

void ACatfishingGameModeBase::CloseShopForSettlementNight()
{
	// 结算夜商店关闭流程：只读取当前 World 的经济服务，存在时关闭命令写口；缺服务时保持无副作用，后续阶段发布仍按 Run 流程继续。
	if (UCatShopEconomyService* Shop = GetWorld() ? GetWorld()->GetSubsystem<UCatShopEconomyService>() : nullptr)
	{
		Shop->CloseCommands();
	}
}

void ACatfishingGameModeBase::PublishShopEconomySnapshot()
{
	// 商店快照发布流程：先同时取得 GameState 与经济服务，任一缺失都不发布半套数据；成功时把服务生成的公开快照交给 GameState，并用本 GameMode 的 Active 准入表解析 PlayerState。
	ACatfishingGameState* CatGameState = GetGameState<ACatfishingGameState>();
	UCatShopEconomyService* Shop = GetWorld() ? GetWorld()->GetSubsystem<UCatShopEconomyService>() : nullptr;
	if (!CatGameState || !Shop)
	{
		return;
	}
	CatGameState->SetShopEconomySnapshotFromAuthority(Shop->BuildPublicSnapshot(
		[this](const FString& StableNetId) { return ResolvePlayerStateByStableNetId(StableNetId); }));
}

APlayerState* ACatfishingGameModeBase::ResolvePlayerStateByStableNetId(const FString& StableNetId) const
{
	// PlayerState 解析流程：只接受当前 Active 准入记录，空 StableNetId、已释放连接或失效 Controller 都返回空，避免公开快照绑定到旧连接。
	const FAdmissionRecord* Record = StableNetId.IsEmpty() ? nullptr : AdmissionRecords.Find(StableNetId);
	return Record && Record->Phase == EAdmissionPhase::Active && Record->Controller.IsValid()
		? Record->Controller->PlayerState : nullptr;
}
