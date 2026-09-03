#include "Framework/Game/CatGameplayTypes.h"

#include "Character/CatCharacter.h"
#include "AbilitySystem/Config/CatAbilityInputConfig.h"
#include "AbilitySystem/Config/CatAbilitySettings.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/BodyAction/CatBodyActionAbility.h"
#include "Logging/CatLog.h"
#include "Online/CatOnlineSettings.h"
#include "Online/CatOnlineSubsystem.h"
#include "Camp/CatCampInventoryActor.h"
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
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Fishing/CatFishingService.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/Pawn.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "Items/CatItemsService.h"
#include "Interaction/CatInteractable.h"
#include "Interaction/CatInteractionTags.h"
#include "Interaction/CatInteractionTargetingComponent.h"
#include "Net/UnrealNetwork.h"
#include "OnlineSubsystemTypes.h"
#include "Profile/CatProfileSubsystem.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "Run/CatRunSettings.h"
#include "Run/CatSacrificeCoordinator.h"
#include "Run/CatRunStateTreeEvents.h"
#include "ShopEconomy/CatShopEconomyService.h"
#include "ShopEconomy/CatShopInventoryComponent.h"
#include "ShopEconomy/CatShopKioskActor.h"
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

	// 商店购物车 RPC 输入检查流程：先限制原始数组规模，再合并重复 EntryId 检查单品次数，避免恶意客户端把可靠 RPC 变成大内存归一化入口。
	bool IsShopCartRpcPayloadWithinLimits(const TArray<FCatShopCartLineCommand>& Lines)
	{
		if (Lines.IsEmpty() || Lines.Num() > CatShopCartLimits::MaxCartLines)
		{
			return false;
		}
		TMap<FName, int32> CountsByEntryId;
		for (const FCatShopCartLineCommand& Line : Lines)
		{
			if (Line.EntryId.IsNone() || Line.CartCount <= 0
				|| Line.CartCount > CatShopCartLimits::MaxCartCountPerEntry)
			{
				return false;
			}
			int32& Count = CountsByEntryId.FindOrAdd(Line.EntryId);
			if (Line.CartCount > CatShopCartLimits::MaxCartCountPerEntry - Count)
			{
				return false;
			}
			Count += Line.CartCount;
		}
		return true;
	}

	// 容器触达半径流程：外部容器优先复用宿主交互接口的半径，未实现或未声明时才回退 Camp 配置。
	double ResolveContainerReachRadiusCentimeters(const AActor* Host, const UCatCampSettings* Settings)
	{
		if (Host && Host->GetClass()->ImplementsInterface(UCatInteractable::StaticClass()))
		{
			const double Radius = ICatInteractable::Execute_GetInteractionRadius(const_cast<AActor*>(Host));
			if (FMath::IsFinite(Radius) && Radius > 0.0)
			{
				return Radius;
			}
		}
		return Settings && Settings->IsRuntimeReady() ? Settings->InteractionRadiusCentimeters : 0.0;
	}

	// 容器触达判断流程：所有容器都必须有 Items 注册宿主并处于同一交互半径内；鱼实例归属继续由 Items 的 StableNetId 权限校验。
	bool IsContainerHostReachable(const AActor* Host, const ACatCharacter* Character, const UCatCampSettings* Settings)
	{
		const double Radius = ResolveContainerReachRadiusCentimeters(Host, Settings);
		return Host && Character && Radius > 0.0
			&& FVector::DistSquared(Character->GetActorLocation(), Host->GetActorLocation()) <= FMath::Square(Radius);
	}

	// 容器空鱼格查找流程：按钮存缸没有显式 Drop 目标；调用方已先处理非正容量，这里只在有效容量内寻找第一个空位。
	int32 FindFirstFreeFishContainerSlot(const FCatContainerSnapshot& Snapshot)
	{
		for (int32 SlotIndex = 0; SlotIndex < Snapshot.Capacity; ++SlotIndex)
		{
			FCatContainedObjectInstance ExistingObject;
			if (!CatItems::TryGetContainedObjectAt(Snapshot, SlotIndex, ExistingObject))
			{
				return SlotIndex;
			}
		}
		return INDEX_NONE;
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

ACatfishingPlayerController::ACatfishingPlayerController()
{
	FishingCommandComponent = CreateDefaultSubobject<UCatFishingCommandComponent>(TEXT("FishingCommandComponent"));
	InteractionTargetingComponent = CreateDefaultSubobject<UCatInteractionTargetingComponent>(TEXT("InteractionTargetingComponent"));
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

// 启动流程：先执行引擎玩法启动并建立 NotStarted 快照，再订阅商店交易和货架刷新事件，让订单与刷新都会推送最新公开快照；随后校验 authority、正式运行数值、环境接口与 ST_RunFlow 资产，全部满足才显式启动 StateTree，任一步失败均保持 NotStarted/StartupFailed。
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

// World 收口流程：先关闭新 Run 命令并清白天截止/时段刷新计时，再清 HostExit ACK 计时句柄与远端等待集合；随后解除商店交易和货架刷新订阅，避免旧 World 的委托继续发布快照；然后停止仍运行的 StateTree，最后调父类，使迟到 Task/ACK 不能进入新 World。
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

// 默认 Pawn 生成流程：先确认 StartSpot 仍是唯一营地，再读取当前 PawnClass 和 CDO 给营地做碰撞可用性判断；解析成功后只调用引擎 SpawnDefaultPawnAtTransform，后续 SetPawn/InitStartSpot/FinishRestartPlayer 继续留给父类 RestartPlayerAtPlayerStart。
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
		UE_LOG(LogCatfishing, Log,
			TEXT("Event=camp_player_spawned Controller=%s Pawn=%s Camp=%s Transform=%s World=%s NetMode=%d"),
			*GetNameSafe(NewPlayer), *GetNameSafe(SpawnedPawn), *GetNameSafe(Camp),
			*SpawnTransform.ToHumanReadableString(), *GetNameSafe(GetWorld()),
			GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : INDEX_NONE);
	}
	return SpawnedPawn;
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

// 阶段进入流程：先要求 authority、有效 Run 与正在启动/运行的唯一 StateTree，并在写状态前拒绝未裁的成功结算或白天参数。通过后统一清掉旧白天计时与公开截止并复位玩法开关：DayActive 递增天数、清额度/终局原因、重置 Active 玩家 ready、开启 quota/fishing，建立截止与 Morning/Dusk 刷新；NormalNight 冻结当前 ready 资格；两种 settlement 写对应终局原因并清 ready 集合；Ending/Ended/NotStarted 关闭新命令。最后只递增一次 Revision、保存 StateTree 可读结果并刷新 Environment/GameState 组合快照；C++ 始终不选择下一条转移边。
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
	return Result;
}

// Result 条件读取流程：只比较最近一次由阶段入口或事件提交写下的原因；不根据当前 Phase、额度或 ready 集合重建历史。
bool ACatfishingGameModeBase::DoesLastRunFlowResultMatch(const ECatRunTransitionReason ExpectedReason) const
{
	return LastRunFlowResult.bApplied && LastRunFlowResult.Error == ECatRunCommandError::None
		&& LastRunFlowResult.Reason == ExpectedReason;
}

// 额度提交流程：服务器重建身份并先查幂等缓存，再校验 gate/Phase/Revision/载荷；首次写入更新总量与 Revision，未达标直接发布快照，达标时发布关闭命令的同 Revision 快照，再向 StateTree 发送 QuotaReached。
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

// 额度内部流程：先查完整幂等缓存，再校验 gate/Phase/Revision/载荷；首次写入更新总量与 Revision，未达标发布同阶段快照，达标时先释放钓鱼操作位和移动锁，再关闭写口、停白天计时、发布过渡快照并发送唯一 StateTree 事件。
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

// 启动失败流程：先释放可能残留的钓鱼操作位和移动锁，再保持 NotStarted、清计时器与写口、写 StartupFailed 并递增 Revision；不会启动备用 C++ FSM 或假装 StateTree 已运行。
void ACatfishingGameModeBase::FailRunStartup(const TCHAR* Reason)
{
	if (UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr)
	{
		Fishing->SuspendFishingAndReleaseOperators();
	}
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

#if !UE_BUILD_SHIPPING
// 开发期服务器快照流程：先创建一次性副本并标记当前实例是否有 authority；不是服务器时立刻返回空快照。服务器路径只复制私有门禁、StateTree 运行态和最近事件结果给调试面板，既不写 GameState，也不发网络同步，避免形成第二套同步状态。
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
	return Snapshot;
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
	if (UCatShopEconomyService* Shop = GetWorld() ? GetWorld()->GetSubsystem<UCatShopEconomyService>() : nullptr)
	{
		Shop->CloseCommands();
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

APlayerState* ACatfishingGameModeBase::ResolvePlayerStateByStableNetId(const FString& StableNetId) const
{
	const FAdmissionRecord* Record = StableNetId.IsEmpty() ? nullptr : AdmissionRecords.Find(StableNetId);
	return Record && Record->Phase == EAdmissionPhase::Active && Record->Controller.IsValid()
		? Record->Controller->PlayerState : nullptr;
}

// GameState 构造流程：只创建 ChumField 公开复制组件，让客户端可读窝点表现事实；Run、Help 和 Shop 快照仍保持默认值，等待 authority setter 写入。
ACatfishingGameState::ACatfishingGameState()
{
	ChumFieldReplication = CreateDefaultSubobject<UCatChumFieldReplicationComponent>(TEXT("ChumFieldReplication"));
}

// GameState 开始流程：先完成父类 BeginPlay，再写一条实际类日志；不在这里补算 Run、Social 或商店快照，避免绕过 GameMode 的唯一写口。
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

// 商店经济快照写入流程：只接受服务器 authority，整体替换团队余额、货架和公开交易记录；写入后立即请求网络更新并广播本机 UI 重读事件。
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

// 商店经济快照读取流程：返回服务器最终值或客户端最近复制值；调用方只能展示，不通过该引用确认交付或修改团队余额。
const FCatShopPublicEconomySnapshot& ACatfishingGameState::GetShopEconomySnapshot() const
{
	return ShopEconomySnapshot;
}

// ChumField 复制组件写口读取流程：仅 authority 返回可写组件，客户端得到空指针，防止表现层绕过环境/窝点服务发布公共窝点。
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

// 商店经济复制回调流程：客户端收到整份公开快照后只广播重读通知并写诊断日志；不在 RepNotify 中确认订单交付或推导余额变化。
void ACatfishingGameState::OnRep_ShopEconomySnapshot()
{
	OnShopEconomySnapshotChanged.Broadcast();
	UE_LOG(LogCatfishing, Verbose,
		TEXT("Event=shop_economy_snapshot_received Balance=%d WalletRevision=%lld Stocks=%d Transactions=%d"),
		ShopEconomySnapshot.Balance, ShopEconomySnapshot.WalletRevision,
		ShopEconomySnapshot.Stocks.Num(),
		ShopEconomySnapshot.Transactions.Num());
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

// PlayerState 复制注册流程：保留父类 UniqueId 等身份字段，再注册个人 ready、公开鱼图鉴摘要和装备解锁投影；
// ready 不包含全员转移判断，鱼图鉴和解锁都只是本局公开/授权摘要，不复制 Profile 私有记录。
void ACatfishingPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, bReadyForNextDay);
	DOREPLIFETIME(ThisClass, PublicFishCollection);
	DOREPLIFETIME(ThisClass, AuthorizedEquipmentUnlockIds);
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

// 装备解锁摘要写入流程：只允许 authority 接收本人 owning client 的 durable Profile 摘要；数量、空值和重复不合法时保留旧授权，避免坏包清掉或扩大神授予范围。
bool ACatfishingPlayerState::SetAuthorizedEquipmentUnlocksFromAuthority(const TArray<FName>& UnlockIds)
{
	if (!HasAuthority() || UnlockIds.Num() > 256)
	{
		return false;
	}
	TSet<FName> UniqueUnlockIds;
	TArray<FName> NewUnlockIds;
	for (const FName UnlockId : UnlockIds)
	{
		if (UnlockId.IsNone() || UniqueUnlockIds.Contains(UnlockId))
		{
			return false;
		}
		UniqueUnlockIds.Add(UnlockId);
		NewUnlockIds.Add(UnlockId);
	}
	AuthorizedEquipmentUnlockIds = MoveTemp(NewUnlockIds);
	ForceNetUpdate();
	return true;
}

// Unlock Grant 授权流程：只接收已经由服务器投递记录确认 ACK 的不可变 Grant；非 Unlock、空 ID 或非 authority 都不会改变本局装备授权。
bool ACatfishingPlayerState::AuthorizeEquipmentUnlockFromProfileGrant(const FCatProfileGrant& Grant)
{
	if (!HasAuthority() || Grant.Kind != ECatProfileGrantKind::Unlock || Grant.UnlockId.IsNone())
	{
		return false;
	}
	if (!AuthorizedEquipmentUnlockIds.Contains(Grant.UnlockId))
	{
		AuthorizedEquipmentUnlockIds.Add(Grant.UnlockId);
		ForceNetUpdate();
	}
	return true;
}

// 装备解锁证明读取流程：None 表示定义明确声明 starter；非空 UnlockId 必须命中服务器当前 PlayerState 授权快照，本地 SaveGame 不能被 Equipment 组件直接读取。
bool ACatfishingPlayerState::HasServerAuthorizedEquipmentUnlock(const FName UnlockId) const
{
	return UnlockId.IsNone() || AuthorizedEquipmentUnlockIds.Contains(UnlockId);
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

// Pawn 写入流程：先保留 PlayerController 引擎内部的 SetPawn 行为，再让 owning client 的 LocalPlayer UI 消费当前最终 Pawn。
void ACatfishingPlayerController::SetPawn(APawn* InPawn)
{
	Super::SetPawn(InPawn);
	NotifyLocalPlayerUISubsystemPawnChanged();
}

// 本地启动流程：父类完成 Actor 生命周期后，幂等安装本 Controller 的玩法输入层；
// 如果本机 durable Profile 已可读，再把装备解锁摘要投影给服务器 PlayerState，缺失时保持服务器 fail-closed。
void ACatfishingPlayerController::BeginPlay()
{
	Super::BeginPlay();
	ApplyInputMappingContext();
	PublishProfileEquipmentUnlocksIfAvailable();
}

// 输入绑定流程：只接受项目配置的 EnhancedInputComponent；未接入的 Action 独立跳过，不阻塞其余输入。
// 完成玩法输入绑定后通知 LocalPlayer UI 重新检查确认键，覆盖客户端 InputComponent 晚于 UI 子系统就绪的时序。
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
		for (const FCatNativeInputAction& Entry : AbilityInputConfig->NativeInputActions)
		{
			EnhancedInput->BindAction(Entry.InputAction, ETriggerEvent::Started,
				this, &ThisClass::NativeInputTagPressed, Entry.InputTag);
		}
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
	NotifyLocalPlayerUISubsystemPawnChanged();
}

// UI 通知流程：
// 1. 只允许 owning client 执行，服务器上的远端 Controller 不创建或刷新任何 LocalPlayer UI。
// 2. 从当前 Controller 持有的 LocalPlayer 取得 UI 子系统；没有 LocalPlayer 说明还处于服务器或非玩家上下文，直接跳过。
// 3. 子系统按当前 Controller/Pawn 重新对齐 HUD、库存和交互提示，并在输入链晚到时重装 UI Action。
void ACatfishingPlayerController::NotifyLocalPlayerUISubsystemPawnChanged()
{
	if (!IsLocalController())
	{
		return;
	}
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UCatLocalPlayerUISubsystem* UISubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<UCatLocalPlayerUISubsystem>() : nullptr;
	if (UISubsystem)
	{
		UISubsystem->RefreshPlayerLakeUIForController(this);
	}
}

// 输入后处理流程：先保留父类每帧输入收尾，再把本帧 Delta/GamePaused 交给当前 Pawn 的 ASC；没有有效 ASC 时保持静默，不缓存旧 Pawn。
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

// Profile 解锁发布流程：只在 owning client 有 LocalPlayer Profile 时复制 UnlockIds 并走服务器 RPC；Profile 不可用时保持服务器 fail-closed 授权。
void ACatfishingPlayerController::PublishProfileEquipmentUnlocksIfAvailable()
{
	if (!IsLocalController())
	{
		return;
	}
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UCatProfileSubsystem* Profile = LocalPlayer ? LocalPlayer->GetSubsystem<UCatProfileSubsystem>() : nullptr;
	TArray<FName> UnlockIds;
	if (Profile && Profile->GetEquipmentUnlockSnapshot(UnlockIds))
	{
		ServerPublishEquipmentUnlocks(UnlockIds);
	}
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

// Controller 钓鱼 gate 流程：现取 authority GameMode 并使用 Fishing 专用白天规则；它只服务抛竿、鱼竿操作、协作、抢抄和玩家打窝，不影响 Social、翻天 ready 或结算 RPC。
bool ACatfishingPlayerController::CanForwardFishingCommand() const
{
	const ACatfishingGameModeBase* GameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	return GameMode && GameMode->CanAcceptFishingCommand(this);
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
		if (Grant.Kind == ECatProfileGrantKind::Unlock)
		{
			PublishProfileEquipmentUnlocksIfAvailable();
		}
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
			FCatProfileGrant AcknowledgedGrant;
			if (Service->TryGetAcknowledgedGrant(GrantId, AcknowledgedGrant)
				&& AcknowledgedGrant.Kind == ECatProfileGrantKind::Unlock)
			{
				if (ACatfishingPlayerState* CatPlayerState = GetPlayerState<ACatfishingPlayerState>())
				{
					CatPlayerState->AuthorizeEquipmentUnlockFromProfileGrant(AcknowledgedGrant);
				}
			}
			if (ACatfishingGameModeBase* GameMode = GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>())
			{
				GameMode->NotifyHostExitGrantAckProgress();
			}
		}
	}
}

// 装备解锁摘要 RPC 流程：服务器只把 owning client 提交的 durable Profile 摘要写到当前 PlayerState；非法摘要保留旧授权，不回写 Profile 或生成 Grant。
void ACatfishingPlayerController::ServerPublishEquipmentUnlocks_Implementation(const TArray<FName>& UnlockIds)
{
	if (ACatfishingPlayerState* CatPlayerState = GetPlayerState<ACatfishingPlayerState>())
	{
		CatPlayerState->SetAuthorizedEquipmentUnlocksFromAuthority(UnlockIds);
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

// 搏斗协作 RPC 流程：先过钓鱼白天 gate，再转交会话键、幂等键与 ExpectedRevision；Session 继续验证 Giant 与 HookedFight。
void ACatfishingPlayerController::ServerAssistFishingSession_Implementation(const FGuid FishingSessionId,
	const FGuid RequestId, const int64 ExpectedRevision)
{
	if (!CanForwardFishingCommand())
	{
		return;
	}
	if (FishingCommandComponent)
	{
		FishingCommandComponent->ForwardLegacyAssist(FishingSessionId, RequestId, ExpectedRevision);
	}
}

// 抄网 RPC 流程：先过钓鱼白天 gate，再把客户端意图交给命令组件；后续由命令组件和 Session 裁决范围，并在成功时完成抄网结果。
void ACatfishingPlayerController::ServerRequestScoop_Implementation(const FGuid FishingSessionId,
	FCatScoopCommand Command)
{
	if (!CanForwardFishingCommand())
	{
		return;
	}
	if (FishingCommandComponent)
	{
		FishingCommandComponent->ForwardLegacyScoop(FishingSessionId, Command);
	}
}

// 献祭 RPC 路由流程：服务器 RPC 只把 UI/输入意图打包成 BodyAction GameplayEvent；没有正式 Ability 接管时回送依赖错误，不直接碰 Coordinator。
void ACatfishingPlayerController::ServerRequestSacrifice_Implementation(FCatSacrificeCommand Command)
{
	UCatBodyActionPayload* Payload = CreateBodyActionPayload(ECatBodyActionAbilityCommand::RequestSacrifice);
	if (Payload)
	{
		Payload->SacrificeCommand = Command;
	}
	if (!SubmitBodyActionThroughAbility(Payload))
	{
		FCatSacrificeResult Result;
		Result.RequestId = Command.Context.RequestId;
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		DeliverSacrificeResultToOwningClient(Result);
	}
}

// 献祭 Ability 提交流程：先保存外部 RequestId；玩法 gate 关闭时回送 CommandsClosed，依赖缺失时回送 DependencyUnavailable。合法路径清除客户端身份并只调用唯一 Coordinator，随后把领域结果原样可靠发给 owning client；Controller 不重算 Revision、不找容器也不改鱼或额度。
void ACatfishingPlayerController::SubmitSacrificeFromBodyActionAbility(FCatSacrificeCommand Command)
{
	FCatSacrificeResult Result;
	Result.RequestId = Command.Context.RequestId;
	if (!CanForwardGameplayCommand())
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (UCatSacrificeCoordinator* Coordinator = GetWorld()
		? GetWorld()->GetSubsystem<UCatSacrificeCoordinator>() : nullptr)
	{
		Command.Context.StableNetId.Reset();
		Result = Coordinator->RequestSacrifice(this, Command);
	}
	else
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	DeliverSacrificeResultToOwningClient(Result);
}

// 献祭结果客户端流程：可靠接收服务器协调器的完整阶段结果并整体替换本机读模型；随后广播本机通知供 UI Model 刷新，不参与任何服务器恢复或写入。
void ACatfishingPlayerController::ClientReceiveSacrificeResult_Implementation(const FCatSacrificeResult& Result)
{
	LastSacrificeResult = Result;
	OnSacrificeResultReceived.Broadcast(Result);
}

// 献祭结果读取流程：返回 owning client 最近收到的完整副本；调用方只能展示 RequestId、阶段与 Revision，不能据此直接操作 Items 或 Run。
FCatSacrificeResult ACatfishingPlayerController::GetLastSacrificeResult() const
{
	return LastSacrificeResult;
}

// 营地休息 RPC 路由流程：只把固定营地和 RequestId 投给 BodyAction Ability；没有正式 Ability 接管时回送依赖错误。
void ACatfishingPlayerController::ServerRequestCampRest_Implementation(ACatCampHubActor* Camp, const FGuid RequestId)
{
	UCatBodyActionPayload* Payload = CreateBodyActionPayload(ECatBodyActionAbilityCommand::CampRest);
	if (Payload)
	{
		Payload->Camp = Camp;
		Payload->RequestId = RequestId;
	}
	if (!SubmitBodyActionThroughAbility(Payload))
	{
		FCatDomainCommandResult Result;
		Result.RequestId = RequestId;
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		DeliverCampCommandResultToOwningClient(Result);
	}
}

// 营地休息 Ability 提交流程：先保留 RequestId；玩法 gate、无效 Camp 或领域调用分别产生 CommandsClosed、DependencyUnavailable 或 Camp 原始结果，最后统一可靠回送 owning client。
void ACatfishingPlayerController::SubmitCampRestFromBodyActionAbility(ACatCampHubActor* Camp, const FGuid RequestId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (!CanForwardGameplayCommand())
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (!Camp || Camp->GetWorld() != GetWorld())
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else
	{
		Result = Camp->RequestRest(this, RequestId);
	}
	DeliverCampCommandResultToOwningClient(Result);
}

// 篝火回看 RPC 路由流程：只把营地回看意图投给 BodyAction Ability；没有正式 Ability 接管时回送依赖错误。
void ACatfishingPlayerController::ServerRequestCampfirePlayback_Implementation(ACatCampHubActor* Camp,
	const FGuid RequestId)
{
	UCatBodyActionPayload* Payload = CreateBodyActionPayload(ECatBodyActionAbilityCommand::CampfirePlayback);
	if (Payload)
	{
		Payload->Camp = Camp;
		Payload->RequestId = RequestId;
	}
	if (!SubmitBodyActionThroughAbility(Payload))
	{
		FCatDomainCommandResult Result;
		Result.RequestId = RequestId;
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		DeliverCampCommandResultToOwningClient(Result);
	}
}

// 篝火回看 Ability 提交流程：先保留 RequestId；玩法 gate、无效 Camp 或领域调用分别产生 CommandsClosed、DependencyUnavailable 或 Camp 原始结果。Controller 只回送结果，表现 multicast 仍由 Camp 在全员 CapturePlan 成功后触发。
void ACatfishingPlayerController::SubmitCampfirePlaybackFromBodyActionAbility(ACatCampHubActor* Camp,
	const FGuid RequestId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (!CanForwardGameplayCommand())
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (!Camp || Camp->GetWorld() != GetWorld())
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else
	{
		Result = Camp->RequestCampfirePlayback(this, RequestId);
	}
	DeliverCampCommandResultToOwningClient(Result);
}

// 普通容器库存拖拽 RPC 流程：owning client 只把请求送到自己的 PlayerController；服务器直接进入 Items 提交，地面鱼护箱子按外部箱子处理，不依赖 Actor Owner，也不投 BodyAction/Social。
void ACatfishingPlayerController::ServerTransferObjectBetweenContainers_Implementation(const FGuid RequestId,
	const ECatContainedObjectKind ObjectKind, const FGuid ObjectInstanceId, const FGuid SourceContainerId,
	const ECatContainerKind SourceContainerKind,
	const int32 SourceContainerSlotIndex, const int64 ExpectedSourceRevision, const FGuid TargetContainerId,
	const ECatContainerKind TargetContainerKind, const int32 TargetContainerSlotIndex,
	const int64 ExpectedTargetRevision)
{
	SubmitTransferObjectBetweenContainersFromServerRequest(RequestId, ObjectKind, ObjectInstanceId,
		SourceContainerId, SourceContainerKind, SourceContainerSlotIndex, ExpectedSourceRevision,
		TargetContainerId, TargetContainerKind, TargetContainerSlotIndex, ExpectedTargetRevision);
}

// 普通容器库存服务端提交流程：
// 1. 先验证玩法命令 gate、RPC 参数形状、当前 Character、Items 服务和服务器身份；客户端身份只作为请求来源，不会让客户端取得容器宿主 Actor 权威。
// 2. 再从 Items 重读源/目标容器宿主、种类、快照和源槽位对象，要求客户端提交的容器类型与注册事实一致，源格仍是同一个物体。
// 3. 接着只做容器宿主距离校验；地面鱼护箱子是外部箱子库存，不要求拖拽者拥有鱼护，也不进入 Social 偷鱼协议。
// 4. 最后构造 Items 转移命令，由 Items 按容器策略和 Revision 原子提交，并把结果可靠回送 owning client；当前 Items 只对鱼对象提交，其余 ObjectKind 保持策略拒绝。
void ACatfishingPlayerController::SubmitTransferObjectBetweenContainersFromServerRequest(const FGuid RequestId,
	const ECatContainedObjectKind ObjectKind, const FGuid ObjectInstanceId,
	const FGuid SourceContainerId, const ECatContainerKind SourceContainerKind,
	const int32 SourceContainerSlotIndex, const int64 ExpectedSourceRevision, const FGuid TargetContainerId,
	const ECatContainerKind TargetContainerKind, const int32 TargetContainerSlotIndex,
	const int64 ExpectedTargetRevision)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	UCatItemsService* Items = GetWorld() ? GetWorld()->GetSubsystem<UCatItemsService>() : nullptr;
	const APlayerState* CurrentPlayerState = PlayerState;
	if (!CanForwardGameplayCommand())
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (!RequestId.IsValid() || ObjectKind == ECatContainedObjectKind::Unknown || !ObjectInstanceId.IsValid()
		|| !SourceContainerId.IsValid()
		|| !TargetContainerId.IsValid() || SourceContainerSlotIndex == INDEX_NONE
		|| TargetContainerSlotIndex == INDEX_NONE
		|| SourceContainerKind == ECatContainerKind::Unknown || TargetContainerKind == ECatContainerKind::Unknown)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (!ControlledCharacter || !Items || !CurrentPlayerState || !CurrentPlayerState->GetUniqueId().IsValid())
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else
	{
		ECatContainerKind ActualSourceKind = ECatContainerKind::Unknown;
		ECatContainerKind ActualTargetKind = ECatContainerKind::Unknown;
		AActor* SourceHost = nullptr;
		AActor* TargetHost = nullptr;
		const UCatCampSettings* CampSettings = GetDefault<UCatCampSettings>();
		FCatContainerSnapshot SourceSnapshot;
		FCatContainerSnapshot TargetSnapshot;
		if (!Items->TryGetContainerHost(SourceContainerId, ActualSourceKind, SourceHost)
			|| !Items->TryGetContainerHost(TargetContainerId, ActualTargetKind, TargetHost)
			|| !Items->TryGetContainerSnapshot(SourceContainerId, SourceSnapshot)
			|| !Items->TryGetContainerSnapshot(TargetContainerId, TargetSnapshot))
		{
			Result.Error = ECatDomainCommandError::NotFound;
		}
		else if (ActualSourceKind != SourceContainerKind || ActualTargetKind != TargetContainerKind)
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
		}
		else if (!IsContainerHostReachable(SourceHost, ControlledCharacter, CampSettings)
			|| !IsContainerHostReachable(TargetHost, ControlledCharacter, CampSettings))
		{
			Result.Error = ECatDomainCommandError::PermissionDenied;
		}
		else
		{
			FCatContainedObjectInstance MatchedObject;
			const bool bSourceSlotStillMatches = CatItems::TryGetContainedObjectAt(SourceSnapshot,
				SourceContainerSlotIndex, MatchedObject)
				&& MatchedObject.ObjectKind == ObjectKind
				&& MatchedObject.ObjectInstanceId == ObjectInstanceId;
			const FCatContainedObjectInstance* Object = bSourceSlotStillMatches ? &MatchedObject : nullptr;
			FCatContainedObjectInstance TargetSlotObject;
			const bool bTargetSlotOccupied = CatItems::TryGetContainedObjectAt(TargetSnapshot,
				TargetContainerSlotIndex, TargetSlotObject);
			if (!Object)
			{
				Result.Error = ECatDomainCommandError::NotFound;
			}
			else if (bTargetSlotOccupied && TargetSlotObject.ObjectKind != ObjectKind)
			{
				Result.Error = ECatDomainCommandError::PolicyUndecided;
			}
			else
			{
				FCatContainerObjectTransferCommand Command;
				Command.Context.RequestId = RequestId;
				Command.Context.ExpectedRevision = ExpectedSourceRevision;
				Command.Context.StableNetId = CurrentPlayerState->GetUniqueId()->ToString();
				Command.ObjectKind = ObjectKind;
				Command.ObjectInstanceId = ObjectInstanceId;
				Command.SourceContainerId = SourceContainerId;
				Command.SourceContainerSlotIndex = SourceContainerSlotIndex;
				Command.TargetContainerId = TargetContainerId;
				Command.TargetContainerSlotIndex = TargetContainerSlotIndex;
				Command.ExpectedTargetRevision = ExpectedTargetRevision;
				Result = Items->TransferContainedObject(Command);
			}
		}
	}
	DeliverCampCommandResultToOwningClient(Result);
}

// 一键存入共享鱼缸 RPC 流程：owning client 只提交鱼护源格和鱼实例；目标鱼缸不接受客户端指定，统一交给服务器从固定营地解析。
void ACatfishingPlayerController::ServerStoreFishInSharedTank_Implementation(const FGuid RequestId,
	const FGuid FishInstanceId, const FGuid SourceContainerId, const int32 SourceContainerSlotIndex,
	const int64 ExpectedSourceRevision)
{
	SubmitStoreFishInSharedTankFromServerRequest(RequestId, FishInstanceId, SourceContainerId,
		SourceContainerSlotIndex, ExpectedSourceRevision);
}

// 一键存缸服务端提交流程：
// 1. 先验证按钮请求形状、玩法命令 gate、当前 Character 和 Items 服务，避免在无效局状态下扫描营地。
// 2. 再从当前 World 的固定营地中寻找已配置、已注册且玩家可触达的 SharedFishTank，并选第一个空鱼格作为目标。
// 3. 找到目标后立即复用普通容器转移入口；源鱼护身份、同一条鱼、双容器距离、Revision、展示资格和幂等仍由通用路径与 Items 原子裁决。
void ACatfishingPlayerController::SubmitStoreFishInSharedTankFromServerRequest(const FGuid RequestId,
	const FGuid FishInstanceId, const FGuid SourceContainerId, const int32 SourceContainerSlotIndex,
	const int64 ExpectedSourceRevision)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	UWorld* World = GetWorld();
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	UCatItemsService* Items = World ? World->GetSubsystem<UCatItemsService>() : nullptr;
	if (!CanForwardGameplayCommand())
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (!RequestId.IsValid() || !FishInstanceId.IsValid() || !SourceContainerId.IsValid()
		|| SourceContainerSlotIndex == INDEX_NONE)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (!World || !ControlledCharacter || !Items)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else
	{
		const UCatCampSettings* CampSettings = GetDefault<UCatCampSettings>();
		AActor* TargetHost = nullptr;
		FCatContainerSnapshot TargetSnapshot;
		int32 TargetSlotIndex = INDEX_NONE;
		bool bFoundSharedTank = false;
		bool bFoundReachableSharedTank = false;
		bool bFoundPolicyClosedSharedTank = false;
		for (TActorIterator<ACatCampHubActor> It(World); It; ++It)
		{
			ACatCampHubActor* Camp = *It;
			FCatContainerSnapshot CandidateSnapshot;
			if (!IsValid(Camp) || !Camp->TryGetSharedFishTankSnapshot(CandidateSnapshot)
				|| CandidateSnapshot.Kind != ECatContainerKind::SharedFishTank)
			{
				continue;
			}
			ECatContainerKind CandidateHostKind = ECatContainerKind::Unknown;
			AActor* CandidateHost = nullptr;
			if (!Items->TryGetContainerHost(CandidateSnapshot.ContainerId, CandidateHostKind, CandidateHost)
				|| CandidateHostKind != ECatContainerKind::SharedFishTank)
			{
				continue;
			}
			bFoundSharedTank = true;
			if (!IsContainerHostReachable(CandidateHost, ControlledCharacter, CampSettings))
			{
				continue;
			}
			bFoundReachableSharedTank = true;
			if (CandidateSnapshot.Capacity <= 0)
			{
				bFoundPolicyClosedSharedTank = true;
				continue;
			}
			const int32 CandidateSlotIndex = FindFirstFreeFishContainerSlot(CandidateSnapshot);
			if (CandidateSlotIndex == INDEX_NONE)
			{
				continue;
			}
			TargetHost = CandidateHost;
			TargetSnapshot = CandidateSnapshot;
			TargetSlotIndex = CandidateSlotIndex;
			break;
		}
		if (!TargetHost)
		{
			Result.Error = !bFoundSharedTank
				? ECatDomainCommandError::DependencyUnavailable
				: (!bFoundReachableSharedTank
					? ECatDomainCommandError::PermissionDenied
					: (bFoundPolicyClosedSharedTank
						? ECatDomainCommandError::PolicyUndecided : ECatDomainCommandError::CapacityExceeded));
		}
		else
		{
			UE_LOG(LogCatItems, Log,
				TEXT("Event=fish_guard_store_shared_tank_resolved Request=%s Fish=%s SourceContainer=%s SourceSlot=%d TargetTank=%s TargetContainer=%s TargetSlot=%d TargetRevision=%lld"),
				*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
				*FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
				*SourceContainerId.ToString(EGuidFormats::DigitsWithHyphens), SourceContainerSlotIndex,
				*GetNameSafe(TargetHost),
				*TargetSnapshot.ContainerId.ToString(EGuidFormats::DigitsWithHyphens), TargetSlotIndex,
				TargetSnapshot.Revision);
			SubmitTransferObjectBetweenContainersFromServerRequest(RequestId, ECatContainedObjectKind::Fish,
				FishInstanceId,
				SourceContainerId, ECatContainerKind::FishGuard, SourceContainerSlotIndex,
				ExpectedSourceRevision,
				TargetSnapshot.ContainerId, ECatContainerKind::SharedFishTank, TargetSlotIndex,
				TargetSnapshot.Revision);
			return;
		}
	}
	if (Result.Error != ECatDomainCommandError::None)
	{
		UE_LOG(LogCatItems, Warning,
			TEXT("Event=fish_guard_store_shared_tank_rejected Request=%s Fish=%s SourceContainer=%s SourceSlot=%d Error=%s Revision=%lld"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			*FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
			*SourceContainerId.ToString(EGuidFormats::DigitsWithHyphens), SourceContainerSlotIndex,
			*UEnum::GetValueAsString(Result.Error), Result.Revision);
	}
	DeliverCampCommandResultToOwningClient(Result);
}

// 搬运救援 RPC 路由流程：只把目标和营地投给 BodyAction Ability；没有正式 Ability 接管时回送依赖错误。
void ACatfishingPlayerController::ServerRescueCharacterToCamp_Implementation(ACatCampHubActor* Camp,
	ACatCharacter* TargetCharacter, const FGuid RequestId)
{
	UCatBodyActionPayload* Payload = CreateBodyActionPayload(ECatBodyActionAbilityCommand::RescueCharacterToCamp);
	if (Payload)
	{
		Payload->Camp = Camp;
		Payload->TargetCharacter = TargetCharacter;
		Payload->RequestId = RequestId;
	}
	if (!SubmitBodyActionThroughAbility(Payload))
	{
		FCatDomainCommandResult Result;
		Result.RequestId = RequestId;
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		DeliverCampCommandResultToOwningClient(Result);
	}
}

// 搬运救援 Ability 提交流程：先保留 RequestId；玩法 gate 与 Camp/目标 World 引用失败时返回结构化拒绝，合法路径只转交 Camp/Condition 裁决 Teleport 和倒地事实，最终结果可靠回送 owning client。
void ACatfishingPlayerController::SubmitRescueCharacterToCampFromBodyActionAbility(ACatCampHubActor* Camp,
	ACatCharacter* TargetCharacter, const FGuid RequestId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (!CanForwardGameplayCommand())
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (!Camp || !TargetCharacter || Camp->GetWorld() != GetWorld() || TargetCharacter->GetWorld() != GetWorld())
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else
	{
		Result = Camp->RescueToCamp(this, TargetCharacter, RequestId);
	}
	DeliverCampCommandResultToOwningClient(Result);
}

// 公共领域结果客户端流程：可靠接收 Camp、容器移动和钓具选择等结果并整体替换本机读模型；随后广播本机通知供 UI Model 刷新，不解释错误、不重算 Revision，也不触发新的领域命令。
void ACatfishingPlayerController::ClientReceiveCampCommandResult_Implementation(
	const FCatDomainCommandResult& Result)
{
	LastCampCommandResult = Result;
	OnCampCommandResultReceived.Broadcast(Result);
}

// 公共领域结果读取流程：返回 owning client 最近收到的完整结果副本，供 UI 按 RequestId 关联反馈；服务器权限和领域真相不读取该缓存。
FCatDomainCommandResult ACatfishingPlayerController::GetLastCampCommandResult() const
{
	return LastCampCommandResult;
}

// 公共仓库 Actor 取用 RPC 流程：
// 1. 先过统一玩法 gate，再解析当前 Pawn 的 EquipmentComponent；客户端传来的仓库 Actor 只作为候选目标。
// 2. 要求公共仓库与 Controller 处于同一 World，并按仓库自身交互半径复核玩家仍在箱子旁边。
// 3. 距离和依赖都通过后，把源槽、数量和双方 Revision 交给公共仓库提交；公共仓库负责扣公共格并授予玩家随身库存。
// 4. 任一失败都可靠回送公共领域结果，UI 只按 RequestId 关闭 pending 并显示原因。
void ACatfishingPlayerController::ServerWithdrawCampInventoryItemAtActor_Implementation(
	ACatCampInventoryActor* CampInventory, const FGuid RequestId, const int64 ExpectedCampInventoryRevision,
	const int32 SourceSlotIndex, const int32 Quantity, const int64 ExpectedEquipmentRevision)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (!CanForwardGameplayCommand())
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		DeliverCampCommandResultToOwningClient(Result);
		return;
	}

	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	UCatEquipmentComponent* Equipment =
		ControlledCharacter ? ControlledCharacter->GetEquipmentComponent() : nullptr;
	const UCatCampSettings* CampSettings = GetDefault<UCatCampSettings>();
	if (!RequestId.IsValid() || !CampInventory || CampInventory->GetWorld() != GetWorld() || !Equipment)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		DeliverCampCommandResultToOwningClient(Result);
		return;
	}
	if (!IsContainerHostReachable(CampInventory, ControlledCharacter, CampSettings))
	{
		Result.Error = ECatDomainCommandError::PermissionDenied;
		DeliverCampCommandResultToOwningClient(Result);
		return;
	}

	Result = CampInventory->WithdrawToEquipmentFromAuthority(RequestId, ExpectedCampInventoryRevision,
		SourceSlotIndex, Quantity, Equipment, ExpectedEquipmentRevision);
	DeliverCampCommandResultToOwningClient(Result);
}

// 公共仓库 Actor 整理 RPC 流程：
// 1. 先过统一玩法 gate；客户端传来的仓库 Actor 只作为候选目标，不能直接授权改公共仓库。
// 2. 要求公共仓库与当前 World 匹配，并用仓库自身交互半径复核玩家仍在箱子旁边。
// 3. 通过后只提交源/目标槽位和公共仓库 Revision；移动、合并、交换规则由公共仓库复用运行库存格规则。
// 4. 无论成功或拒绝都可靠回送公共领域结果并写入明确日志，让营地仓库 UI 用同一条 pending 反馈链路收束。
void ACatfishingPlayerController::ServerMoveCampInventorySlotAtActor_Implementation(
	ACatCampInventoryActor* CampInventory, const FGuid RequestId, const int64 ExpectedCampInventoryRevision,
	const int32 SourceSlotIndex, const int32 TargetSlotIndex)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (!CanForwardGameplayCommand())
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=move_camp_inventory_slot_rejected Reason=CommandsClosedOrInactive Request=%s Camp=%s Source=%d Target=%d"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(CampInventory),
			SourceSlotIndex, TargetSlotIndex);
		DeliverCampCommandResultToOwningClient(Result);
		return;
	}

	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	const UCatCampSettings* CampSettings = GetDefault<UCatCampSettings>();
	if (!RequestId.IsValid() || !CampInventory || CampInventory->GetWorld() != GetWorld() || !ControlledCharacter)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=move_camp_inventory_slot_rejected Reason=DependencyUnavailable Request=%s Camp=%s Character=%s Source=%d Target=%d"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(CampInventory),
			*GetNameSafe(ControlledCharacter), SourceSlotIndex, TargetSlotIndex);
		DeliverCampCommandResultToOwningClient(Result);
		return;
	}
	if (!IsContainerHostReachable(CampInventory, ControlledCharacter, CampSettings))
	{
		Result.Error = ECatDomainCommandError::PermissionDenied;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=move_camp_inventory_slot_rejected Reason=PermissionDenied Request=%s Camp=%s Character=%s Source=%d Target=%d"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(CampInventory),
			*GetNameSafe(ControlledCharacter), SourceSlotIndex, TargetSlotIndex);
		DeliverCampCommandResultToOwningClient(Result);
		return;
	}

	Result = CampInventory->MoveInventorySlotFromAuthority(RequestId, ExpectedCampInventoryRevision,
		SourceSlotIndex, TargetSlotIndex);
	UE_LOG(LogCatfishing, Log,
		TEXT("Event=move_camp_inventory_slot Committed=%s Error=%s Revision=%lld Camp=%s Source=%d Target=%d"),
		Result.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Error),
		Result.Revision, *GetNameSafe(CampInventory), SourceSlotIndex, TargetSlotIndex);
	DeliverCampCommandResultToOwningClient(Result);
}

// 背包存入公共仓库 RPC 流程：
// 1. 先过统一玩法 gate，再解析当前 Pawn 的 EquipmentComponent；客户端传来的仓库 Actor 只作为候选目标。
// 2. 要求公共仓库与当前 World 匹配，并按仓库自身交互半径复核玩家仍在箱子旁边。
// 3. 通过后把双方 Revision 和槽位交给公共仓库事务；事务会同时改背包快照和公共仓库快照。
// 4. 无论成功或拒绝都可靠回送公共领域结果，让两个库存 UI 只通过各自数据源广播刷新。
void ACatfishingPlayerController::ServerDepositInventoryItemToCampAtActor_Implementation(
	ACatCampInventoryActor* CampInventory, const FGuid RequestId, const int64 ExpectedCampInventoryRevision,
	const int32 TargetCampSlotIndex, const int64 ExpectedEquipmentRevision, const int32 SourceEquipmentSlotIndex)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (!CanForwardGameplayCommand())
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		DeliverCampCommandResultToOwningClient(Result);
		return;
	}

	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	UCatEquipmentComponent* Equipment =
		ControlledCharacter ? ControlledCharacter->GetEquipmentComponent() : nullptr;
	const UCatCampSettings* CampSettings = GetDefault<UCatCampSettings>();
	if (!RequestId.IsValid() || !CampInventory || CampInventory->GetWorld() != GetWorld() || !Equipment)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		DeliverCampCommandResultToOwningClient(Result);
		return;
	}
	if (!IsContainerHostReachable(CampInventory, ControlledCharacter, CampSettings))
	{
		Result.Error = ECatDomainCommandError::PermissionDenied;
		DeliverCampCommandResultToOwningClient(Result);
		return;
	}

	Result = CampInventory->DepositFromEquipmentSlotFromAuthority(RequestId, ExpectedCampInventoryRevision,
		TargetCampSlotIndex, Equipment, ExpectedEquipmentRevision, SourceEquipmentSlotIndex);
	DeliverCampCommandResultToOwningClient(Result);
}

// 公共仓库拖入背包 RPC 流程：
// 1. 先过统一玩法 gate，再解析当前 Pawn 的 EquipmentComponent；客户端传来的仓库 Actor 不直接授权写入。
// 2. 要求公共仓库与当前 World 匹配，并按仓库自身交互半径复核玩家仍在箱子旁边。
// 3. 通过后把双方 Revision 和槽位交给公共仓库事务；事务会同时改公共仓库快照和背包快照。
// 4. 无论成功或拒绝都可靠回送公共领域结果，UI pending 只按 RequestId 收束，不在本地搬格子。
void ACatfishingPlayerController::ServerWithdrawCampInventoryItemToSlotAtActor_Implementation(
	ACatCampInventoryActor* CampInventory, const FGuid RequestId, const int64 ExpectedCampInventoryRevision,
	const int32 SourceCampSlotIndex, const int64 ExpectedEquipmentRevision, const int32 TargetEquipmentSlotIndex)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (!CanForwardGameplayCommand())
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		DeliverCampCommandResultToOwningClient(Result);
		return;
	}

	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	UCatEquipmentComponent* Equipment =
		ControlledCharacter ? ControlledCharacter->GetEquipmentComponent() : nullptr;
	const UCatCampSettings* CampSettings = GetDefault<UCatCampSettings>();
	if (!RequestId.IsValid() || !CampInventory || CampInventory->GetWorld() != GetWorld() || !Equipment)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		DeliverCampCommandResultToOwningClient(Result);
		return;
	}
	if (!IsContainerHostReachable(CampInventory, ControlledCharacter, CampSettings))
	{
		Result.Error = ECatDomainCommandError::PermissionDenied;
		DeliverCampCommandResultToOwningClient(Result);
		return;
	}

	Result = CampInventory->WithdrawToEquipmentSlotFromAuthority(RequestId, ExpectedCampInventoryRevision,
		SourceCampSlotIndex, Equipment, ExpectedEquipmentRevision, TargetEquipmentSlotIndex);
	DeliverCampCommandResultToOwningClient(Result);
}

// 当前选择 RPC 流程：先过统一玩法 gate，当前 Pawn 还必须是项目 Character；EquipmentComponent 会按实例 ID 或定义 ID 验证目录、解锁和库存事实后再写选择。
void ACatfishingPlayerController::ServerConfigureEquipment_Implementation(const FGuid RequestId,
	const int64 ExpectedRevision, const FName RodDefinitionId, const FName BaitDefinitionId,
	const FName FloatDefinitionId, const FName ScoopNetDefinitionId, const FGuid RodItemInstanceId,
	const FGuid BaitItemInstanceId, const FGuid FloatItemInstanceId, const FGuid ScoopNetItemInstanceId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (!CanForwardGameplayCommand())
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		UE_LOG(LogCatfishing, Warning, TEXT("Event=configure_equipment_rejected Reason=CommandsClosedOrInactive Request=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	}
	else if (!RequestId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		UE_LOG(LogCatfishing, Warning, TEXT("Event=configure_equipment_rejected Reason=InvalidRequest Request=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	}
	else
	{
		ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
		UCatEquipmentComponent* Equipment = ControlledCharacter ? ControlledCharacter->GetEquipmentComponent() : nullptr;
		if (!Equipment)
		{
			Result.Error = ECatDomainCommandError::DependencyUnavailable;
			UE_LOG(LogCatfishing, Warning, TEXT("Event=configure_equipment_rejected Reason=NoEquipmentComponent Request=%s"),
				*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
		}
		else
		{
			Result = Equipment->ConfigureLoadoutFromAuthority(RequestId, ExpectedRevision,
				RodDefinitionId, BaitDefinitionId, FloatDefinitionId, ScoopNetDefinitionId, NAME_None,
				RodItemInstanceId, BaitItemInstanceId, FloatItemInstanceId, ScoopNetItemInstanceId);
		}
	}
	UE_LOG(LogCatfishing, Log, TEXT("Event=configure_equipment Committed=%s Error=%s Revision=%lld Rod=%s RodItem=%s Bait=%s BaitItem=%s Float=%s FloatItem=%s Net=%s NetItem=%s"),
		Result.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Error), Result.Revision,
		*RodDefinitionId.ToString(), *RodItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*BaitDefinitionId.ToString(), *BaitItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*FloatDefinitionId.ToString(), *FloatItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*ScoopNetDefinitionId.ToString(), *ScoopNetItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens));
	DeliverCampCommandResultToOwningClient(Result);
}

// 随身库存整理 RPC 流程：Controller 只做玩法 gate、当前 Pawn 和 EquipmentComponent 解析；数组移动规则全部交给 Equipment 聚合。
void ACatfishingPlayerController::ServerMoveInventorySlot_Implementation(const FGuid RequestId,
	const int64 ExpectedRevision, const int32 SourceSlotIndex, const int32 TargetSlotIndex)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (!CanForwardGameplayCommand())
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		UE_LOG(LogCatfishing, Warning, TEXT("Event=move_inventory_slot_rejected Reason=CommandsClosedOrInactive Request=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	}
	else if (!RequestId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		UE_LOG(LogCatfishing, Warning, TEXT("Event=move_inventory_slot_rejected Reason=InvalidRequest Request=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	}
	else
	{
		ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
		UCatEquipmentComponent* Equipment = ControlledCharacter ? ControlledCharacter->GetEquipmentComponent() : nullptr;
		if (!Equipment)
		{
			Result.Error = ECatDomainCommandError::DependencyUnavailable;
			UE_LOG(LogCatfishing, Warning, TEXT("Event=move_inventory_slot_rejected Reason=NoEquipmentComponent Request=%s"),
				*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
		}
		else
		{
			Result = Equipment->MoveInventorySlotFromAuthority(RequestId, ExpectedRevision,
				SourceSlotIndex, TargetSlotIndex);
		}
	}
	UE_LOG(LogCatfishing, Log,
		TEXT("Event=move_inventory_slot Committed=%s Error=%s Revision=%lld Source=%d Target=%d"),
		Result.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Error),
		Result.Revision, SourceSlotIndex, TargetSlotIndex);
	DeliverCampCommandResultToOwningClient(Result);
}

void ACatfishingPlayerController::ServerRequestInteraction_Implementation(AActor* Target, const FGuid RequestId)
{
	if (!CanForwardGameplayCommand() || !RequestId.IsValid() || !IsValid(Target)
		|| Target->GetWorld() != GetWorld()
		|| !Target->GetClass()->ImplementsInterface(UCatInteractable::StaticClass())
		|| !ICatInteractable::Execute_CanInteract(Target, this))
	{
		return;
	}
	ICatInteractable::Execute_Interact(Target, this, RequestId);
}

// 摊位购物车支付 RPC 流程：服务器只接受来源摊位引用和 EntryId/次数意图，不接受客户端提交的价格、库存或收货仓库。
void ACatfishingPlayerController::ServerSubmitShopCartAtKiosk_Implementation(ACatShopKioskActor* ShopKiosk,
	const TArray<FCatShopCartLineCommand>& Lines, const FGuid RequestId, const int64 ExpectedWalletRevision)
{
	SubmitShopCart(ShopKiosk, Lines, RequestId, ExpectedWalletRevision);
}

// 商店购物车转发流程：
// 1. 先建立一份交付结果壳，任何早期 gate 拒绝都要回 owning client，避免商店 UI 一直等待。
// 2. 在查摊位前先限制客户端购物车载荷规模，避免可靠 RPC 被异常大数组拖进后续归一化和查表流程。
// 3. 来源摊位同时证明玩家还在摊位旁边，并提供本摊位自己的商店库存组件作为 EntryId 的权威解释范围。
// 4. 随后遍历当前 World 的 ACatCampHubActor，让营地接口回答能否提供 PublicInventory。
// 5. 没有摊位库存、没有营地或所有营地都没有 PublicInventory 时回送 DependencyUnavailable，不进入扣款；有仓库后才把购物车交给订单协调器按摊位表结算。
// 6. 协调器成功或失败后只把交付段结果回给本玩家；公共仓库和商店公开快照分别通过自己的复制事实刷新。
void ACatfishingPlayerController::SubmitShopCart(ACatShopKioskActor* ShopKiosk,
	const TArray<FCatShopCartLineCommand>& Lines, const FGuid RequestId, const int64 ExpectedWalletRevision)
{
	FCatDomainCommandResult DeliveryResult;
	DeliveryResult.RequestId = RequestId;
	if (!CanForwardGameplayCommand())
	{
		DeliveryResult.Error = ECatDomainCommandError::CommandsClosed;
		DeliverCampCommandResultToOwningClient(DeliveryResult);
		return;
	}
	if (!RequestId.IsValid() || !IsShopCartRpcPayloadWithinLimits(Lines))
	{
		DeliveryResult.Error = ECatDomainCommandError::InvalidPayload;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=shop_cart_invalid_rpc_payload RequestId=%s LineCount=%d MaxLines=%d MaxCountPerEntry=%d"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens), Lines.Num(),
			CatShopCartLimits::MaxCartLines, CatShopCartLimits::MaxCartCountPerEntry);
		DeliverCampCommandResultToOwningClient(DeliveryResult);
		return;
	}
	const APlayerState* CurrentPlayerState = PlayerState;
	UWorld* World = GetWorld();
	UCatShopOrderCoordinator* Coordinator = World ? World->GetSubsystem<UCatShopOrderCoordinator>() : nullptr;
	ACatCampInventoryActor* DeliveryInventory = nullptr;
	UCatShopInventoryComponent* ShopInventory = nullptr;
	if (World && ShopKiosk && ShopKiosk->CanServeOrderFromAuthority(this))
	{
		ShopInventory = ShopKiosk->GetShopInventory();
		for (TActorIterator<ACatCampHubActor> It(World); It; ++It)
		{
			ACatCampHubActor* Camp = *It;
			DeliveryInventory = IsValid(Camp) ? Camp->ResolvePublicInventoryForShopOrder() : nullptr;
			if (DeliveryInventory)
			{
				break;
			}
		}
	}
	if (!Coordinator || !CurrentPlayerState || !CurrentPlayerState->GetUniqueId().IsValid()
		|| !DeliveryInventory || !ShopInventory || !ShopInventory->GetShopInventoryId().IsValid())
	{
		DeliveryResult.Error = ECatDomainCommandError::DependencyUnavailable;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=shop_cart_dependency_missing RequestId=%s LineCount=%d Shop=%s HasShopInventory=%s HasDeliveryInventory=%s Result=RejectedBeforePayment"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens), Lines.Num(), *GetNameSafe(ShopKiosk),
			ShopInventory ? TEXT("true") : TEXT("false"), DeliveryInventory ? TEXT("true") : TEXT("false"));
		DeliverCampCommandResultToOwningClient(DeliveryResult);
		return;
	}

	FCatShopCartCommand Command;
	Command.Context.RequestId = RequestId;
	Command.Context.ExpectedRevision = ExpectedWalletRevision;
	Command.Context.StableNetId = CurrentPlayerState->GetUniqueId()->ToString();
	Command.ShopInventoryId = ShopInventory->GetShopInventoryId();
	Command.Lines = Lines;
	const FCatShopOrderResult Result = Coordinator->SubmitCart(Command, ShopInventory, DeliveryInventory);
	DeliveryResult = Result.Delivery;
	DeliveryResult.RequestId = RequestId;
	UE_LOG(LogCatfishing, Log,
		TEXT("Event=shop_cart_submitted RequestId=%s LineCount=%d Order=%s Delivery=%s"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens), Lines.Num(),
		*UEnum::GetValueAsString(Result.CartTransaction.Command.Error),
		*UEnum::GetValueAsString(Result.Delivery.Error));
	DeliverCampCommandResultToOwningClient(DeliveryResult);
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

// 修竿 RPC 路由流程：只把营地、RequestId 和装备 Revision 投给 BodyAction Ability；Ability 未接管时保持 fail-closed。
void ACatfishingPlayerController::ServerRepairRodAtCamp_Implementation(ACatCampHubActor* Camp,
	const FGuid RequestId, const int64 ExpectedEquipmentRevision)
{
	UCatBodyActionPayload* Payload = CreateBodyActionPayload(ECatBodyActionAbilityCommand::RepairRodAtCamp);
	if (Payload)
	{
		Payload->Camp = Camp;
		Payload->RequestId = RequestId;
		Payload->ExpectedEquipmentRevision = ExpectedEquipmentRevision;
	}
	SubmitBodyActionThroughAbility(Payload);
}

// 修竿 Ability 提交流程：先过统一玩法 gate，再让 Camp 验证本人在固定范围并调用 Equipment 的浮木/耐久事务；不提供远程修理。
void ACatfishingPlayerController::SubmitRepairRodAtCampFromBodyActionAbility(ACatCampHubActor* Camp,
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

// 草药 RPC 路由流程：只把施药目标、RequestId、装备 Revision 和草药实例投给 BodyAction Ability；Ability 未接管时静默关闭。
void ACatfishingPlayerController::ServerUseHerbOnCharacter_Implementation(ACatCharacter* TargetCharacter,
	const FGuid RequestId, const int64 ExpectedEquipmentRevision, const FGuid HerbItemInstanceId)
{
	UCatBodyActionPayload* Payload = CreateBodyActionPayload(ECatBodyActionAbilityCommand::UseHerbOnCharacter);
	if (Payload)
	{
		Payload->TargetCharacter = TargetCharacter;
		Payload->RequestId = RequestId;
		Payload->ExpectedEquipmentRevision = ExpectedEquipmentRevision;
		Payload->HerbItemInstanceId = HerbItemInstanceId;
	}
	SubmitBodyActionThroughAbility(Payload);
}

// 草药 Ability 提交流程：先过统一玩法 gate，再从当前 Pawn 和目标 Character 读服务器位置，要求施药者未倒地且距离不超显式正范围；最后按请求实例通过 Equipment Use 扣量，成功后才恢复目标。
void ACatfishingPlayerController::SubmitUseHerbOnCharacterFromBodyActionAbility(ACatCharacter* TargetCharacter,
	const FGuid RequestId, const int64 ExpectedEquipmentRevision, const FGuid HerbItemInstanceId)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	UCatEquipmentComponent* Equipment = ControlledCharacter ? ControlledCharacter->GetEquipmentComponent() : nullptr;
	UCatConditionComponent* SourceConditions = ControlledCharacter ? ControlledCharacter->GetConditionComponent() : nullptr;
	UCatConditionComponent* Conditions = TargetCharacter ? TargetCharacter->GetConditionComponent() : nullptr;
	const FCatRunInventorySlot* HerbSlot = nullptr;
	if (Equipment)
	{
		for (const FCatRunInventorySlot& Slot : Equipment->GetSnapshot().InventorySlots)
		{
			if (Slot.ItemInstanceId == HerbItemInstanceId)
			{
				HerbSlot = &Slot;
				break;
			}
		}
	}
	UCatEquipmentDefinition* Definition = HerbSlot
		? GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(HerbSlot->DefinitionId) : nullptr;
	const UCatConditionSettings* ConditionSettings = GetDefault<UCatConditionSettings>();
	if (!HerbItemInstanceId.IsValid() || !Equipment || !SourceConditions || SourceConditions->GetSnapshot().bDowned || !Conditions
		|| !HerbSlot || HerbSlot->Quantity <= 0
		|| !Definition || Definition->Kind != ECatEquipmentKind::Herb
		|| !Definition->ConsumesInventoryQuantityOnUse() || !ConditionSettings
		|| !FMath::IsFinite(ConditionSettings->HerbUseRangeCentimeters)
		|| ConditionSettings->HerbUseRangeCentimeters <= 0.0
		|| TargetCharacter->GetWorld() != GetWorld()
		|| FVector::DistSquared(ControlledCharacter->GetActorLocation(), TargetCharacter->GetActorLocation())
			> FMath::Square(ConditionSettings->HerbUseRangeCentimeters)
		|| Conditions->ValidateHerbRecovery(this) != ECatDomainCommandError::None)
	{
		return;
	}
	const FCatInventoryItemUseResult UseResult =
		Equipment->Use(RequestId, ExpectedEquipmentRevision, HerbItemInstanceId);
	if (UseResult.bCommitted)
	{
		Conditions->ApplyCommittedHerbRecovery(this, RequestId);
	}
}

// 直接吃鱼 RPC 路由流程：只把进食目标和鱼消费命令投给 BodyAction Ability；Ability 未接管时回送依赖错误，避免 UI 等不到终态。
void ACatfishingPlayerController::ServerConsumeFish_Implementation(ACatCharacter* EatingCharacter,
	FCatFishConsumeCommand Command)
{
	UCatBodyActionPayload* Payload = CreateBodyActionPayload(ECatBodyActionAbilityCommand::ConsumeFish);
	if (Payload)
	{
		Payload->TargetCharacter = EatingCharacter;
		Payload->FishConsumeCommand = Command;
	}
	if (!SubmitBodyActionThroughAbility(Payload))
	{
		FCatFishConsumeResult Result;
		Result.Command.RequestId = Command.Context.RequestId;
		Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		DeliverFishConsumeResultToOwningClient(Result);
	}
}

// 直接吃鱼 Ability 提交流程：先强制进食者为未倒地当前 Pawn，再用 Items 真实宿主验证共享鱼缸的服务器距离；只有身体 preflight 也成功才不可逆移除鱼并应用状态，所有路径都会把结构化终态回送 owning client。
void ACatfishingPlayerController::SubmitConsumeFishFromBodyActionAbility(ACatCharacter* EatingCharacter,
	FCatFishConsumeCommand Command)
{
	FCatFishConsumeResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	FCatContainerSnapshot Source;
	if (!CanForwardGameplayCommand())
	{
		Result.Command.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (EatingCharacter != GetPawn() || !Command.Context.RequestId.IsValid()
		|| !Command.FishInstanceId.IsValid() || !Command.SourceContainerId.IsValid())
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
	}
	else
	{
		UCatItemsService* Items = GetWorld() ? GetWorld()->GetSubsystem<UCatItemsService>() : nullptr;
		UCatConditionComponent* Conditions = EatingCharacter ? EatingCharacter->GetConditionComponent() : nullptr;
		const APlayerState* CurrentPlayerState = PlayerState;
		if (!Items || !Conditions || !CurrentPlayerState || !CurrentPlayerState->GetUniqueId().IsValid())
		{
			Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		}
		else if (!Items->TryGetContainerSnapshot(Command.SourceContainerId, Source))
		{
			Result.Command.Error = ECatDomainCommandError::NotFound;
		}
		else
		{
			Result.Command.Revision = Source.Revision;
			ECatContainerKind SourceKind = ECatContainerKind::Unknown;
			AActor* SourceHost = nullptr;
			bool bPreflightMayConsume = false;
			if (Conditions->GetSnapshot().bDowned)
			{
				Result.Command.Error = ECatDomainCommandError::InvalidPhase;
			}
			else if (!Items->TryGetContainerHost(Command.SourceContainerId, SourceKind, SourceHost))
			{
				Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
			}
			else if (SourceKind != ECatContainerKind::FishGuard && SourceKind != ECatContainerKind::SharedFishTank)
			{
				Result.Command.Error = ECatDomainCommandError::InvalidPayload;
			}
			else
			{
				const UCatCampSettings* CampSettings = GetDefault<UCatCampSettings>();
				if (!SourceHost)
				{
					Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
				}
				else if (!IsContainerHostReachable(SourceHost, EatingCharacter, CampSettings))
				{
					Result.Command.Error = ECatDomainCommandError::PermissionDenied;
				}
				else
				{
					bPreflightMayConsume = true;
				}
			}
			if (bPreflightMayConsume)
			{
				const FCatFishInstance* Fish = Source.Fish.FindByPredicate([&Command](const FCatFishInstance& Candidate)
				{
					return Candidate.FishInstanceId == Command.FishInstanceId;
				});
				if (!Fish)
				{
					Result.Command.Error = ECatDomainCommandError::NotFound;
				}
				else if (UCatFishDefinition* Definition = GetDefault<UCatFishCatalogSettings>()->FindRuntimeDefinition(Fish->FishDefinitionId))
				{
					Result.Command.Error = Conditions->ValidateFishConsumption(Definition);
					if (Result.Command.Error == ECatDomainCommandError::None)
					{
						Command.Context.StableNetId = CurrentPlayerState->GetUniqueId()->ToString();
						Result = Items->ConsumeFish(Command);
						if (Result.Command.bCommitted)
						{
							Conditions->ConsumeCommittedFish(Command.Context.RequestId, Definition);
						}
					}
				}
				else
				{
					Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
				}
			}
		}
	}
	DeliverFishConsumeResultToOwningClient(Result);
}

// 献祭回执投递流程：单机或 listen server 本地玩家没有远端连接可回送时，直接复用 Client 实现刷新本机缓存；远端玩家保持可靠 RPC 语义。
void ACatfishingPlayerController::DeliverSacrificeResultToOwningClient(const FCatSacrificeResult& Result)
{
	if (HasAuthority() && IsLocalController())
	{
		ClientReceiveSacrificeResult_Implementation(Result);
		return;
	}
	ClientReceiveSacrificeResult(Result);
}

// 公共领域回执投递流程：单机或 listen server 本地玩家没有远端连接可回送时，直接复用 Client 实现刷新本机缓存；远端玩家保持可靠 RPC 语义。
void ACatfishingPlayerController::DeliverCampCommandResultToOwningClient(const FCatDomainCommandResult& Result)
{
	if (HasAuthority() && IsLocalController())
	{
		ClientReceiveCampCommandResult_Implementation(Result);
		return;
	}
	ClientReceiveCampCommandResult(Result);
}

// 直接吃鱼回执投递流程：单机或 listen server 本地玩家没有远端连接可回送时，直接复用 Client 实现刷新本机缓存；远端玩家保持可靠 RPC 语义。
void ACatfishingPlayerController::DeliverFishConsumeResultToOwningClient(const FCatFishConsumeResult& Result)
{
	if (HasAuthority() && IsLocalController())
	{
		ClientReceiveFishConsumeResult_Implementation(Result);
		return;
	}
	ClientReceiveFishConsumeResult(Result);
}

// 直接吃鱼结果客户端流程：可靠接收 Items 消费鱼的完整结果并整体替换本机读模型；随后广播本机通知供 UI Model 刷新，不应用身体或成长效果。
void ACatfishingPlayerController::ClientReceiveFishConsumeResult_Implementation(const FCatFishConsumeResult& Result)
{
	LastFishConsumeResult = Result;
	OnFishConsumeResultReceived.Broadcast(Result);
}

// 直接吃鱼结果读取流程：返回 owning client 最近收到的完整副本；调用方只能展示 RequestId、错误与容器 Revision，不能据此改 Items 容器。
FCatFishConsumeResult ACatfishingPlayerController::GetLastFishConsumeResult() const
{
	return LastFishConsumeResult;
}

// 偷鱼开始 RPC 路由流程：只把偷鱼命令投给 BodyAction Ability；Ability 未接管时回送依赖错误，避免 UI 等不到终态。
void ACatfishingPlayerController::ServerBeginTheft_Implementation(FCatTheftCommand Command)
{
	UCatBodyActionPayload* Payload = CreateBodyActionPayload(ECatBodyActionAbilityCommand::BeginTheft);
	if (Payload)
	{
		Payload->TheftCommand = Command;
	}
	if (!SubmitBodyActionThroughAbility(Payload))
	{
		FCatTheftResult Result;
		Result.Command.RequestId = Command.Context.RequestId;
		Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		ClientReceiveTheftResult(Result);
	}
}

// 偷鱼开始 Ability 提交流程：先过统一玩法 gate，清客户端身份后转交当前 Controller；Social 负责权限、单鱼上限、Timer 和 Items escrow。
void ACatfishingPlayerController::SubmitBeginTheftFromBodyActionAbility(FCatTheftCommand Command)
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

// 偷鱼追回 RPC 路由流程：只把服务器 ProtocolId 投给 BodyAction Ability；Ability 未接管时不触碰 Social escrow。
void ACatfishingPlayerController::ServerCatchTheft_Implementation(const FGuid TheftProtocolId)
{
	UCatBodyActionPayload* Payload = CreateBodyActionPayload(ECatBodyActionAbilityCommand::CatchTheft);
	if (Payload)
	{
		Payload->TheftProtocolId = TheftProtocolId;
	}
	SubmitBodyActionThroughAbility(Payload);
}

// 偷鱼追回 Ability 提交流程：先过统一玩法 gate，再提交当前 Controller 与服务器 ProtocolId；客户端 RequestId 不参与定位 escrow，Social 继续验证真实主人、状态和距离。
void ACatfishingPlayerController::SubmitCatchTheftFromBodyActionAbility(const FGuid TheftProtocolId)
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

// 手动求助 RPC 路由流程：只把 RequestId 和求助类型投给 BodyAction Ability；Ability 未接管时不发布信号。
void ACatfishingPlayerController::ServerRequestManualHelp_Implementation(const FGuid RequestId,
	const ECatHelpSignalKind Kind)
{
	UCatBodyActionPayload* Payload = CreateBodyActionPayload(ECatBodyActionAbilityCommand::RequestManualHelp);
	if (Payload)
	{
		Payload->RequestId = RequestId;
		Payload->HelpKind = Kind;
	}
	SubmitBodyActionThroughAbility(Payload);
}

// 手动求助 Ability 提交流程：先过统一玩法 gate，再转交 Controller、RequestId 和 Manual 类型；Social 拒绝客户端伪造 Giant 提示。
void ACatfishingPlayerController::SubmitManualHelpFromBodyActionAbility(const FGuid RequestId,
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

// 恶作剧 RPC 路由流程：只把目标 PlayerState、RequestId 和交互位置投给 BodyAction Ability；Ability 未接管时不进入 Social。
void ACatfishingPlayerController::ServerRequestMischief_Implementation(APlayerState* TargetPlayerState,
	const FGuid RequestId, const FVector InteractionLocation)
{
	UCatBodyActionPayload* Payload = CreateBodyActionPayload(ECatBodyActionAbilityCommand::RequestMischief);
	if (Payload)
	{
		Payload->TargetPlayerState = TargetPlayerState;
		Payload->RequestId = RequestId;
		Payload->InteractionLocation = InteractionLocation;
	}
	SubmitBodyActionThroughAbility(Payload);
}

// 恶作剧 Ability 提交流程：先过统一玩法 gate，再从当前 World 按 PlayerState 定位目标；找到后交 Social 冷却与 ProtectionSign 裁决。
void ACatfishingPlayerController::SubmitMischiefFromBodyActionAbility(APlayerState* TargetPlayerState,
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

// 放牌 RPC 路由流程：只把 RequestId 和期望位置投给 BodyAction Ability；Ability 未接管时不生成保护牌。
void ACatfishingPlayerController::ServerPlaceProtectionSign_Implementation(const FGuid RequestId,
	const FVector SignLocation)
{
	UCatBodyActionPayload* Payload = CreateBodyActionPayload(ECatBodyActionAbilityCommand::PlaceProtectionSign);
	if (Payload)
	{
		Payload->RequestId = RequestId;
		Payload->SignLocation = SignLocation;
	}
	SubmitBodyActionThroughAbility(Payload);
}

// 放牌 Ability 提交流程：先过统一玩法 gate，再转交 Controller、RequestId 和期望位置；Social 重读 Pawn、配置范围并保证每人唯一 Actor。
void ACatfishingPlayerController::SubmitProtectionSignFromBodyActionAbility(const FGuid RequestId,
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

// 抖水完成 RPC 路由流程：只把 RequestId 投给 BodyAction Ability；Ability 未接管时不直接清 Wet。
void ACatfishingPlayerController::ServerCompleteShakeDry_Implementation(const FGuid RequestId)
{
	UCatBodyActionPayload* Payload = CreateBodyActionPayload(ECatBodyActionAbilityCommand::CompleteShakeDry);
	if (Payload)
	{
		Payload->RequestId = RequestId;
	}
	SubmitBodyActionThroughAbility(Payload);
}

// 抖水完成 Ability 提交流程：先过统一玩法 gate，再取得当前 Character 并验证 RequestId/身体组件；通过后只清 Wet，保留其他身体事实。
void ACatfishingPlayerController::SubmitShakeDryFromBodyActionAbility(const FGuid RequestId)
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

// BodyAction 回调分派流程：
// 1. Ability 先保证它收到了有效载荷和 owning Controller，这里再按动作枚举进入唯一内部提交函数。
// 2. 每个分支只转发原 RPC 参数，不解释 Revision、容器、权限或距离，避免在 Controller/Ability 里复制领域规则。
// 3. 未知动作返回 false，让 Ability 取消本次激活；已识别动作即使领域层拒绝也返回 true，因为事件已经被正式入口接管。
bool ACatfishingPlayerController::ExecuteBodyActionAbilityPayload(const UCatBodyActionPayload& Payload)
{
	if (!Payload.IsRuntimeValid())
	{
		return false;
	}
	switch (Payload.Command)
	{
	case ECatBodyActionAbilityCommand::RequestSacrifice:
		SubmitSacrificeFromBodyActionAbility(Payload.SacrificeCommand);
		return true;
	case ECatBodyActionAbilityCommand::CampRest:
		SubmitCampRestFromBodyActionAbility(Payload.Camp.Get(), Payload.RequestId);
		return true;
	case ECatBodyActionAbilityCommand::CampfirePlayback:
		SubmitCampfirePlaybackFromBodyActionAbility(Payload.Camp.Get(), Payload.RequestId);
		return true;
	case ECatBodyActionAbilityCommand::TransferObjectBetweenContainers:
		if (Payload.bUsesExplicitContainerTransfer)
		{
			SubmitTransferObjectBetweenContainersFromServerRequest(Payload.RequestId,
				Payload.ContainerObjectKind,
				Payload.ContainerObjectInstanceId,
				Payload.SourceContainerId,
				Payload.SourceContainerKind,
				Payload.SourceContainerSlotIndex,
				Payload.ExpectedSourceContainerRevision,
				Payload.TargetContainerId,
				Payload.TargetContainerKind,
				Payload.TargetContainerSlotIndex,
				Payload.ExpectedTargetContainerRevision);
			return true;
		}
		{
			FCatDomainCommandResult Result;
			Result.RequestId = Payload.RequestId;
			Result.Error = ECatDomainCommandError::InvalidPayload;
			DeliverCampCommandResultToOwningClient(Result);
		}
		return true;
	case ECatBodyActionAbilityCommand::RescueCharacterToCamp:
		SubmitRescueCharacterToCampFromBodyActionAbility(Payload.Camp.Get(), Payload.TargetCharacter.Get(),
			Payload.RequestId);
		return true;
	case ECatBodyActionAbilityCommand::RepairRodAtCamp:
		SubmitRepairRodAtCampFromBodyActionAbility(Payload.Camp.Get(), Payload.RequestId,
			Payload.ExpectedEquipmentRevision);
		return true;
	case ECatBodyActionAbilityCommand::UseHerbOnCharacter:
		SubmitUseHerbOnCharacterFromBodyActionAbility(Payload.TargetCharacter.Get(), Payload.RequestId,
			Payload.ExpectedEquipmentRevision, Payload.HerbItemInstanceId);
		return true;
	case ECatBodyActionAbilityCommand::ConsumeFish:
		SubmitConsumeFishFromBodyActionAbility(Payload.TargetCharacter.Get(), Payload.FishConsumeCommand);
		return true;
	case ECatBodyActionAbilityCommand::BeginTheft:
		SubmitBeginTheftFromBodyActionAbility(Payload.TheftCommand);
		return true;
	case ECatBodyActionAbilityCommand::CatchTheft:
		SubmitCatchTheftFromBodyActionAbility(Payload.TheftProtocolId);
		return true;
	case ECatBodyActionAbilityCommand::RequestManualHelp:
		SubmitManualHelpFromBodyActionAbility(Payload.RequestId, Payload.HelpKind);
		return true;
	case ECatBodyActionAbilityCommand::RequestMischief:
		SubmitMischiefFromBodyActionAbility(Payload.TargetPlayerState.Get(), Payload.RequestId,
			Payload.InteractionLocation);
		return true;
	case ECatBodyActionAbilityCommand::PlaceProtectionSign:
		SubmitProtectionSignFromBodyActionAbility(Payload.RequestId, Payload.SignLocation);
		return true;
	case ECatBodyActionAbilityCommand::CompleteShakeDry:
		SubmitShakeDryFromBodyActionAbility(Payload.RequestId);
		return true;
	default:
		return false;
	}
}

// Payload 创建流程：所有非 Fishing 身体动作 RPC 先走这里创建挂在 Controller 下的 transient 载荷；未知命令不分配可投递事件，调用方保持 fail-closed。
UCatBodyActionPayload* ACatfishingPlayerController::CreateBodyActionPayload(
	const ECatBodyActionAbilityCommand Command)
{
	UCatBodyActionPayload* Payload = NewObject<UCatBodyActionPayload>(this);
	return Payload && Payload->InitializeForCommand(Command) ? Payload : nullptr;
}

// BodyAction 事件投递流程：
// 1. 先确认载荷和当前 Character ASC 存在，且当前调用在 authority 上，不允许客户端本地绕过服务器 RPC。
// 2. 再把动作标签、Instigator、Target 和载荷对象投给 ASC 的 GameplayEvent 入口。
// 3. 只有已授予的 UCatGA_BodyActionCommand 响应该事件才返回 true；否则 RPC 不再直接调用领域写口。
bool ACatfishingPlayerController::SubmitBodyActionThroughAbility(UCatBodyActionPayload* Payload)
{
	if (!Payload || !Payload->IsRuntimeValid())
	{
		return false;
	}
	UCatAbilitySystemComponent* AbilitySystem = GetCurrentCatAbilitySystemComponent();
	if (!AbilitySystem || !AbilitySystem->IsOwnerActorAuthoritative())
	{
		return false;
	}
	FGameplayEventData EventData;
	EventData.EventTag = Payload->EventTag;
	EventData.Instigator = GetPawn();
	EventData.Target = Payload->TargetCharacter ? Cast<AActor>(Payload->TargetCharacter.Get()) : GetPawn();
	EventData.OptionalObject = Payload;
	return AbilitySystem->HandleGameplayEvent(Payload->EventTag, &EventData) > 0;
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

// Native 输入分流流程：
// 1. 只处理项目约定的交互标签，其他 Native 标签保持无副作用返回。
// 2. IA_Interact 只进入 PlayerController 持有的唯一 TargetingComponent；提示 UI 不再绑定第二次 E。
// 3. TargetingComponent 对当前 Actor 调用 ICatInteractable，商店、营地公共仓库、鱼护、鱼缸和死鱼各自在 Actor 实现中处理。
void ACatfishingPlayerController::NativeInputTagPressed(const FGameplayTag InputTag)
{
	if (!InputTag.MatchesTagExact(CatInteractionTags::Input_Interact))
	{
		return;
	}
	if (InteractionTargetingComponent)
	{
		InteractionTargetingComponent->TryInteract();
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
