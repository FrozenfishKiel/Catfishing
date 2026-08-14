#include "Fishing/CatFishingService.h"

#include "Character/CatCharacter.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "Logging/CatLog.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/CatSurvivalAttributeSet.h"
#include "Condition/CatConditionComponent.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Engine/World.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingSession.h"
#include "Social/CatSocialService.h"
#include "GameFramework/PlayerState.h"

// 创建条件流程：仅 authority Game World 持有会话索引；客户端不能创建平行 StateTree。
bool UCatFishingService::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

// 反初始化流程：先关闭并终止所有会话，再清弱映射；随后交还 WorldSubsystem 生命周期。
void UCatFishingService::Deinitialize()
{
	CloseCommandsAndTerminateAll();
	Sessions.Reset();
	SessionFisherById.Reset();
	ActiveSessionByFisher.Reset();
	StartTerminalCache.Reset();
	DeployedRodByPlayerState.Reset();
	Super::Deinitialize();
}

// 会话创建流程：先按服务器身份/RequestId 重放首次结果并拒绝该身份仍存活的唯一会话，再验证当前 Character、Run、水域以及“Active Controller + 未倒地 + FishingStrength/FightStamina 为正”的统一参战能力。随后用全体合法者的人数、总力量和总体力快照及服务器新熵查询鱼表；只有 Actor 生成、StateTree 初始化全部成功后才登记会话索引与终态结果。巨鱼的 Social 提示在成功事实之后 best-effort 广播，缺少 Social 不回滚会话，也不改写首次 Start 结果。
FCatFishingStartResult UCatFishingService::StartFishingSession(AController* FisherController, const FGuid RequestId)
{
	FCatFishingStartResult Result;
	Result.RequestId = RequestId;
	CompactSessions();
	const FString StableNetId = ResolveStableNetId(FisherController);
	if (!RequestId.IsValid() || StableNetId.IsEmpty())
	{
		Result.Error = ECatDomainCommandError::InvalidIdentity;
		return Result;
	}
	const FString TerminalKey = FString::Printf(TEXT("%s|StartFishing|%s"), *StableNetId,
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	if (const FCatFishingStartResult* Cached = StartTerminalCache.Find(TerminalKey))
	{
		return *Cached;
	}
	const auto Finish = [this, &TerminalKey](const FCatFishingStartResult& TerminalResult)
	{
		StartTerminalCache.Add(TerminalKey, TerminalResult);
		return TerminalResult;
	};
	ACatCharacter* Character = FisherController ? Cast<ACatCharacter>(FisherController->GetPawn()) : nullptr;
	const ACatfishingGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
	UCatWaterQuerySubsystem* WaterQuery = GetWorld() ? GetWorld()->GetSubsystem<UCatWaterQuerySubsystem>() : nullptr;
	if (!bCommandsOpen)
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		return Finish(Result);
	}
	if (ActiveSessionByFisher.Contains(StableNetId))
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
		return Finish(Result);
	}
	if (!Character || !GameState || !WaterQuery)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	const FCatRunPublicState& RunState = GameState->GetRunPublicState();
	FCatWaterQuery Query;
	Query.WorldLocation = Character->GetActorLocation();
	Query.RunPhase = RunState.Phase;
	Query.RunRevision = RunState.Revision;
	const FCatWaterQueryResult WaterResult = WaterQuery->QueryWaterRegion(Query);
	if (!WaterResult.bSucceeded)
	{
		Result.Error = WaterResult.Error == ECatWaterQueryError::FishingClosed
			? ECatDomainCommandError::InvalidPhase : ECatDomainCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	FString ValidatedFisherId;
	ACatCharacter* ValidatedFisherCharacter = nullptr;
	double FisherStrength = 0.0;
	double FisherFightStamina = 0.0;
	if (!TryGetFightCapability(FisherController, ValidatedFisherId, ValidatedFisherCharacter,
		FisherStrength, FisherFightStamina) || ValidatedFisherId != StableNetId || ValidatedFisherCharacter != Character)
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Finish(Result);
	}
	int32 FightCapablePlayerCount = 0;
	double CombinedFishingStrength = 0.0;
	double CombinedFightStamina = 0.0;
	BuildFightCapabilitySnapshot(FightCapablePlayerCount, CombinedFishingStrength, CombinedFightStamina);
	if (FightCapablePlayerCount <= 0)
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Finish(Result);
	}
	double FishWeightKilograms = 0.0;
	// 抽取熵由 authority 在首次请求时生成并立即被终态缓存封存；客户端 GUID 只做幂等关联，不能操纵鱼池随机序列。
	const int32 ServerSelectionEntropy = static_cast<int32>(GetTypeHash(FGuid::NewGuid()));
	UCatFishDefinition* FishDefinition = GetDefault<UCatFishCatalogSettings>()->SelectRuntimeDefinition(
		WaterResult.Region.RegionId, RunState.Environment.TimeOfDay, RunState.Environment.Weather,
		FMath::Clamp(FightCapablePlayerCount, 1, 8), CombinedFishingStrength, CombinedFightStamina,
		ServerSelectionEntropy, FishWeightKilograms);
	if (!FishDefinition || !FMath::IsFinite(FishWeightKilograms) || FishWeightKilograms <= 0.0)
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Finish(Result);
	}
	const FGuid SessionId = FGuid::NewGuid();
	FGuid CastAttemptId = FGuid::NewGuid();
	while (CastAttemptId == SessionId)
	{
		CastAttemptId = FGuid::NewGuid();
	}
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = FisherController;
	ACatFishingSession* Session = GetWorld()->SpawnActor<ACatFishingSession>(ACatFishingSession::StaticClass(),
		Character->GetActorTransform(), SpawnParameters);
	if (Session)
	{
		Session->SetOwner(FisherController);
	}
	if (!Session || !Session->InitializeSession(SessionId, CastAttemptId, FisherController, Character, FishDefinition,
		Character->GetPersonalFishGuardId(), FishWeightKilograms, WaterResult.Region))
	{
		if (Session)
		{
			Session->Destroy();
		}
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Finish(Result);
	}
	Sessions.Add(SessionId, Session);
	SessionFisherById.Add(SessionId, StableNetId);
	ActiveSessionByFisher.Add(StableNetId, SessionId);
	Result.bStarted = true;
	Result.FishingSessionId = SessionId;
	Result.Error = ECatDomainCommandError::None;
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_session_started RequestId=%s SessionId=%s Fish=%s Region=%s Giant=%s WeightKg=%.3f"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens), *SessionId.ToString(EGuidFormats::DigitsWithHyphens),
		*FishDefinition->FishDefinitionId.ToString(), *WaterResult.Region.RegionId.ToString(),
		FishDefinition->BodyClass == ECatFishBodyClass::Giant ? TEXT("true") : TEXT("false"), FishWeightKilograms);
	if (FishDefinition->BodyClass == ECatFishBodyClass::Giant)
	{
		if (UCatSocialService* Social = GetWorld()->GetSubsystem<UCatSocialService>())
		{
			Social->BroadcastGiantFishingPrompt(FisherController, SessionId);
		}
	}
	return Finish(Result);
}

// 协作转发流程：先清理终态或失效弱引用并定位真实 Session，未找到返回 NotFound；找到后由会话统一校验 Giant/HookedFight/Revision，以及请求者仍是 Active Controller、持有当前 Character、未倒地且力量/体力为正，任何拒绝都发生在参与集合写入前。
FCatDomainCommandResult UCatFishingService::SubmitFightAssist(const FGuid FishingSessionId,
	AController* AssistingController, const FGuid RequestId, const int64 ExpectedRevision)
{
	CompactSessions();
	if (ACatFishingSession* Session = Sessions.FindRef(FishingSessionId).Get())
	{
		return Session->SubmitFightAssist(AssistingController, RequestId, ExpectedRevision);
	}
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	Result.Error = ECatDomainCommandError::NotFound;
	return Result;
}

// 抢抄转发流程：只定位 Session 并转发；首个合法胜者与 FishInstance 创建完全在 Session→Items Compare-and-Commit 中决定。
FCatScoopResult UCatFishingService::RequestScoop(const FGuid FishingSessionId, AController* ScoopingController,
	const FCatScoopCommand& Command)
{
	CompactSessions();
	if (ACatFishingSession* Session = Sessions.FindRef(FishingSessionId).Get())
	{
		const FCatScoopResult Result = Session->RequestScoop(ScoopingController, Command);
		CompactSessions();
		return Result;
	}
	FCatScoopResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	Result.Command.Error = ECatDomainCommandError::NotFound;
	return Result;
}

// Character 中断流程：遍历存活弱引用，精确命中钓手或协作者后终止；Resolved 会话自行保持终态。
void UCatFishingService::TerminateSessionsForCharacter(const ACatCharacter* Character)
{
	CompactSessions();
	for (const TPair<FGuid, TWeakObjectPtr<ACatFishingSession>>& Pair : Sessions)
	{
		if (ACatFishingSession* Session = Pair.Value.Get(); Session && Session->InvolvesCharacter(Character))
		{
			Session->TerminateSession(ECatFishingOutcome::Invalidated, TEXT("Character unavailable"));
		}
	}
	CompactSessions();
}

// Teardown 流程：永久关闭新入口，并让每个存活会话进入 Terminated；不等待旧半场或创建补偿鱼。
void UCatFishingService::CloseCommandsAndTerminateAll()
{
	bCommandsOpen = false;
	for (const TPair<FGuid, TWeakObjectPtr<ACatFishingSession>>& Pair : Sessions)
	{
		if (ACatFishingSession* Session = Pair.Value.Get())
		{
			Session->TerminateSession(ECatFishingOutcome::Invalidated, TEXT("Run teardown"));
		}
	}
}

// Session 查询流程：先压缩终态/失效弱引用，再做只读查找；失败查询不建立任何缓存或索引项。
ACatFishingSession* UCatFishingService::FindSession(const FGuid FishingSessionId)
{
	CompactSessions();
	if (!FishingSessionId.IsValid())
	{
		return nullptr;
	}
	const TWeakObjectPtr<ACatFishingSession>* WeakSession = Sessions.Find(FishingSessionId);
	ACatFishingSession* Session = WeakSession ? WeakSession->Get() : nullptr;
	return Session && !Session->IsTerminal() ? Session : nullptr;
}

// Controller 活动会话查询：所有输出先清零，再交叉验证服务器身份、正反索引、存活 Session 与公开 SessionId。
bool UCatFishingService::TryGetActiveSessionForController(const AController* Controller,
	FGuid& OutFishingSessionId, FCatFishingSessionSnapshot& OutSnapshot)
{
	OutFishingSessionId.Invalidate();
	OutSnapshot = FCatFishingSessionSnapshot{};
	CompactSessions();
	const FString StableNetId = ResolveStableNetId(Controller);
	const FGuid* MappedSessionId = StableNetId.IsEmpty() ? nullptr : ActiveSessionByFisher.Find(StableNetId);
	if (!MappedSessionId || !MappedSessionId->IsValid())
	{
		return false;
	}
	const FGuid SessionId = *MappedSessionId;
	const FString MappedFisherId = SessionFisherById.FindRef(SessionId);
	ACatFishingSession* Session = FindSession(SessionId);
	if (!Session || MappedFisherId != StableNetId)
	{
		return false;
	}
	const FCatFishingSessionSnapshot& Snapshot = Session->GetSnapshot();
	if (Snapshot.FishingSessionId != SessionId)
	{
		return false;
	}
	OutFishingSessionId = SessionId;
	OutSnapshot = Snapshot;
	return true;
}

// 鱼竿查询流程：先移除双端任一失效的弱条目，再按服务器 PlayerState 身份只读查找。
ACatFishingRodActor* UCatFishingService::FindDeployedRod(const APlayerState* PlayerState)
{
	CompactDeployedRods();
	if (!PlayerState)
	{
		return nullptr;
	}
	const TWeakObjectPtr<APlayerState> PlayerKey(const_cast<APlayerState*>(PlayerState));
	const TWeakObjectPtr<ACatFishingRodActor>* WeakRod = DeployedRodByPlayerState.Find(PlayerKey);
	return WeakRod ? WeakRod->Get() : nullptr;
}

// 鱼竿登记流程：双端必须存活；首次登记成功，相同 Actor 幂等重放，不替换已有存活 Actor。
bool UCatFishingService::RegisterDeployedRod(APlayerState* PlayerState, ACatFishingRodActor* RodActor)
{
	CompactDeployedRods();
	if (!IsValid(PlayerState) || !IsValid(RodActor))
	{
		return false;
	}
	const TWeakObjectPtr<APlayerState> PlayerKey(PlayerState);
	if (const TWeakObjectPtr<ACatFishingRodActor>* Existing = DeployedRodByPlayerState.Find(PlayerKey))
	{
		return Existing->Get() == RodActor;
	}
	DeployedRodByPlayerState.Add(PlayerKey, RodActor);
	return true;
}

// 鱼竿注销流程：ExpectedRodActor 必须与当前存活值精确匹配；missing/null/mismatch 都保持无副作用。
void UCatFishingService::UnregisterDeployedRod(const APlayerState* PlayerState,
	const ACatFishingRodActor* ExpectedRodActor)
{
	CompactDeployedRods();
	if (!PlayerState || !ExpectedRodActor)
	{
		return;
	}
	const TWeakObjectPtr<APlayerState> PlayerKey(const_cast<APlayerState*>(PlayerState));
	const TWeakObjectPtr<ACatFishingRodActor>* Existing = DeployedRodByPlayerState.Find(PlayerKey);
	if (Existing && Existing->Get() == ExpectedRodActor)
	{
		DeployedRodByPlayerState.Remove(PlayerKey);
	}
}

// Session 诊断流程：逐项统计存活且未终态会话，不把弱 Map 的物理条目数误报为活动数量。
int32 UCatFishingService::GetTrackedSessionCountForDiagnostics() const
{
	int32 LiveSessionCount = 0;
	for (const TPair<FGuid, TWeakObjectPtr<ACatFishingSession>>& Pair : Sessions)
	{
		const ACatFishingSession* Session = Pair.Value.Get();
		if (Session && !Session->IsTerminal())
		{
			++LiveSessionCount;
		}
	}
	return LiveSessionCount;
}

// 鱼竿诊断流程：只统计 key/value 双有效的弱登记，不直接返回 Map::Num。
int32 UCatFishingService::GetDeployedRodCountForDiagnostics() const
{
	int32 LiveRodCount = 0;
	for (const TPair<TWeakObjectPtr<APlayerState>, TWeakObjectPtr<ACatFishingRodActor>>& Pair
		: DeployedRodByPlayerState)
	{
		if (Pair.Key.IsValid() && Pair.Value.IsValid())
		{
			++LiveRodCount;
		}
	}
	return LiveRodCount;
}

// 弱索引压缩流程：移除已销毁或 Resolved/Terminated 会话，并用会话到身份反向键释放精确单活跃槽位；开始终态缓存保留供网络重放。
void UCatFishingService::CompactSessions()
{
	for (auto It = Sessions.CreateIterator(); It; ++It)
	{
		ACatFishingSession* Session = It.Value().Get();
		if (!Session || Session->IsTerminal())
		{
			const FGuid SessionId = It.Key();
			const FString StableNetId = SessionFisherById.FindRef(SessionId);
			if (!StableNetId.IsEmpty() && ActiveSessionByFisher.FindRef(StableNetId) == SessionId)
			{
				ActiveSessionByFisher.Remove(StableNetId);
			}
			SessionFisherById.Remove(SessionId);
			It.RemoveCurrent();
		}
	}
}

// 鱼竿 Registry 压缩流程：任一弱端失效即删除整条登记，不保留可阻塞后续 Place 的幽灵槽位。
void UCatFishingService::CompactDeployedRods()
{
	for (auto It = DeployedRodByPlayerState.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || !It.Value().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

// 身份解析流程：只读当前 Controller 的继承 UniqueId；它仅作为服务器私有幂等/单活跃键，不进入 Fishing 公开快照。
FString UCatFishingService::ResolveStableNetId(const AController* Controller)
{
	const APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
	return PlayerState && PlayerState->GetUniqueId().IsValid() ? PlayerState->GetUniqueId()->ToString() : FString();
}

// 参与者谓词流程：先清所有输出，再用服务器 GameMode Active gate、当前 Pawn、Condition 与 ASC 逐层验证；只有身份有效、未倒地且两项能力都为正有限值才返回真。
bool UCatFishingService::TryGetFightCapability(const AController* Controller, FString& OutStableNetId,
	ACatCharacter*& OutCharacter, double& OutFishingStrength, double& OutFightStamina)
{
	OutStableNetId.Reset();
	OutCharacter = nullptr;
	OutFishingStrength = 0.0;
	OutFightStamina = 0.0;
	const UWorld* World = Controller ? Controller->GetWorld() : nullptr;
	const ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr;
	const UCatConditionComponent* Conditions = Character ? Character->GetConditionComponent() : nullptr;
	const UAbilitySystemComponent* ASC = Character ? Character->GetAbilitySystemComponent() : nullptr;
	const FString StableNetId = ResolveStableNetId(Controller);
	if (!World || !GameMode || !Character || !Conditions || !ASC || StableNetId.IsEmpty()
		|| !GameMode->CanAcceptGameplayCommand(Controller) || Conditions->GetSnapshot().bDowned)
	{
		return false;
	}
	const double Strength = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFishingStrengthAttribute());
	const double FightStamina = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	if (!FMath::IsFinite(Strength) || Strength <= 0.0
		|| !FMath::IsFinite(FightStamina) || FightStamina <= 0.0)
	{
		return false;
	}
	OutStableNetId = StableNetId;
	OutCharacter = Character;
	OutFishingStrength = Strength;
	OutFightStamina = FightStamina;
	return true;
}

// 协作快照流程：所有输出先清零，然后遍历当前 Controller 并只累加统一谓词接受的玩家；断线、倒地或零能力玩家不能扩大 Giant 池。
void UCatFishingService::BuildFightCapabilitySnapshot(int32& OutParticipantCount,
	double& OutFishingStrength, double& OutFightStamina) const
{
	OutParticipantCount = 0;
	OutFishingStrength = 0.0;
	OutFightStamina = 0.0;
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const AController* Controller = It->Get();
		FString StableNetId;
		ACatCharacter* Character = nullptr;
		double Strength = 0.0;
		double FightStamina = 0.0;
		if (TryGetFightCapability(Controller, StableNetId, Character, Strength, FightStamina))
		{
			++OutParticipantCount;
			OutFishingStrength += Strength;
			OutFightStamina += FightStamina;
		}
	}
}
