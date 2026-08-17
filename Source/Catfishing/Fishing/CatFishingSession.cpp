#include "Fishing/CatFishingSession.h"

#include "Character/CatCharacter.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "Logging/CatLog.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/CatAbilitySystemComponent.h"
#include "AbilitySystem/CatSurvivalAttributeSet.h"
#include "Collection/CatRunImprintService.h"
#include "Data/CatFishDefinition.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/CatFishingService.h"
#include "Components/StateTreeComponent.h"
#include "GameFramework/PlayerState.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Items/CatItemsService.h"
#include "Net/UnrealNetwork.h"
#include "StateTree.h"

// 构造流程：创建唯一 StateTree 组件、关闭自动启动并开启 Actor 复制；阶段只在事件驱动时更新。
ACatFishingSession::ACatFishingSession()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = false;
	StateTreeComponent = CreateDefaultSubobject<UStateTreeComponent>(TEXT("FishingStateTree"));
	StateTreeComponent->SetStartLogicAutomatically(false);
}

// 复制注册流程：保留父类字段并注册单一 Snapshot；私有身份集合和终态缓存不会进入网络。
void ACatFishingSession::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, Snapshot);
}

// 会话初始化流程：只接受 authority、显式 runtime gate、完整鱼定义/重量/水域/身份/鱼护与 Items；全部就绪后设置资产并启动 StateTree，失败销毁由服务负责。
bool ACatFishingSession::InitializeSession(const FGuid InFishingSessionId, const FGuid InCastAttemptId,
	AController* FisherController, ACatCharacter* InFisherCharacter, UCatFishDefinition* InFishDefinition,
	const FGuid InFisherGuardContainerId, const double InFishWeightKilograms, const FCatWaterRegionSnapshot& WaterRegion)
{
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	UStateTree* StateTreeAsset = Settings ? Settings->FishingSessionStateTree.LoadSynchronous() : nullptr;
	const FString StableNetId = ResolveStableNetId(FisherController);
	UCatItemsService* Items = GetWorld() ? GetWorld()->GetSubsystem<UCatItemsService>() : nullptr;
	if (!HasAuthority() || !Settings || !Settings->IsRuntimeReady() || !StateTreeAsset || !StateTreeComponent
		|| !InFishingSessionId.IsValid() || !InCastAttemptId.IsValid() || InFishingSessionId == InCastAttemptId
		|| !InFisherCharacter || !InFishDefinition || !InFishDefinition->IsRuntimeDefinitionReady()
		|| StableNetId.IsEmpty() || !InFisherGuardContainerId.IsValid()
		|| !FMath::IsFinite(InFishWeightKilograms) || InFishWeightKilograms <= 0.0
		|| WaterRegion.RegionId.IsNone() || WaterRegion.GeometryRevision <= 0 || !Items)
	{
		return false;
	}

	Snapshot.FishingSessionId = InFishingSessionId;
	Snapshot.CastAttemptId = InCastAttemptId;
	Snapshot.Revision = 1;
	Snapshot.SnapshotSequence = 0;
	Snapshot.PhaseEpoch = 1;
	Snapshot.Phase = ECatFishingPhase::Created;
	Snapshot.Outcome = ECatFishingOutcome::None;
	Snapshot.FisherPlayerState = FisherController ? FisherController->PlayerState : nullptr;
	Snapshot.FishDefinitionId = InFishDefinition->FishDefinitionId;
	Snapshot.bGiant = InFishDefinition->BodyClass == ECatFishBodyClass::Giant;
	Snapshot.FightParticipantCount = 1;
	Snapshot.FishFightStaminaRemaining = InFishDefinition->FishFightStamina;
	Snapshot.NormalizedFishStamina = InFishDefinition->FishFightStamina > 0.0
		? FMath::Clamp(Snapshot.FishFightStaminaRemaining / InFishDefinition->FishFightStamina, 0.0, 1.0) : 0.0;
	FishDefinition = InFishDefinition;
	FisherCharacter = InFisherCharacter;
	FisherStableNetId = StableNetId;
	FisherGuardContainerId = InFisherGuardContainerId;
	FishWeightKilograms = InFishWeightKilograms;
	WaterRegionSnapshot = WaterRegion;
	FightParticipantIds.Add(StableNetId);
	FightParticipantCharacters.Add(StableNetId, InFisherCharacter);
	RefreshFightSummary();
	ItemsService = Items;
	PublishSnapshot(ECatFishingSnapshotMutation::HighFrequency);
	StateTreeComponent->SetStateTree(StateTreeAsset);
	bStartupInProgress = true;
	StateTreeComponent->StartLogic();
	bStartupInProgress = false;
	if (!StateTreeComponent->IsRunning() && Snapshot.Phase == ECatFishingPhase::Created)
	{
		return false;
	}
	return true;
}

// 阶段进入流程：先验证 authority、唯一 StateTree 生命周期和未结算状态；NearShore 只接受水域包围盒内的服务器目标并冻结该位置，其他阶段清除目标。HookedFight 与 NearShore 保留钓手/协作者供搏斗和巨鱼候选使用，其余阶段把参与集合收回为钓手；随后刷新协作摘要、递增一次 Revision 并复制快照，若资产进入终态则启动有界销毁。C++ 只应用资产已选阶段，不维护转移拓扑。
FCatFishingPhaseResult ACatFishingSession::EnterPhaseFromStateTree(const ECatFishingPhase NewPhase,
	const bool bHasAuthoritativeNearShoreTarget, const FVector AuthoritativeNearShoreTarget)
{
	FCatFishingPhaseResult Result;
	Result.PreviousPhase = Snapshot.Phase;
	Result.CurrentPhase = Snapshot.Phase;
	Result.Revision = Snapshot.Revision;
	if (!HasAuthority())
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	if (IsTerminal() || bCaptureResolved || NewPhase == ECatFishingPhase::Resolved || NewPhase == ECatFishingPhase::Terminated)
	{
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	if (!StateTreeComponent || (!StateTreeComponent->IsRunning() && !bStartupInProgress))
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	if (NewPhase == ECatFishingPhase::NearShore)
	{
		const FBox WaterBounds = FBox::BuildAABB(WaterRegionSnapshot.WorldCenter, WaterRegionSnapshot.HalfExtent);
		if (!bHasAuthoritativeNearShoreTarget || AuthoritativeNearShoreTarget.ContainsNaN()
			|| !WaterBounds.IsInsideOrOn(AuthoritativeNearShoreTarget))
		{
			Result.Error = ECatDomainCommandError::PolicyUndecided;
			return Result;
		}
		NearShoreTargetWorldLocation = AuthoritativeNearShoreTarget;
		bHasNearShoreTarget = true;
	}
	else
	{
		NearShoreTargetWorldLocation = FVector::ZeroVector;
		bHasNearShoreTarget = false;
	}
	if (NewPhase == ECatFishingPhase::HookedFight && !bFightStaminaInitialized)
	{
		UCatAbilitySystemComponent* AbilitySystem = FisherCharacter.IsValid()
			? FisherCharacter->GetCatAbilitySystemComponent() : nullptr;
		if (!AbilitySystem || !AbilitySystem->InitializeFishingStaminaForSession())
		{
			Result.Error = ECatDomainCommandError::DependencyUnavailable;
			return Result;
		}
		bFightStaminaInitialized = true;
		StaminaParticipantsTouched.Add(FisherCharacter);
	}
	Snapshot.Phase = NewPhase;
	Snapshot.PhaseStartedServerTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	// 巨鱼离开 HookedFight 后仍需把合法协作者带入成像候选；NearShore 只冻结参与事实，不再接受新的 assist。
	if (NewPhase != ECatFishingPhase::HookedFight && NewPhase != ECatFishingPhase::NearShore)
	{
		FightParticipantIds.Reset();
		FightParticipantCharacters.Reset();
		FightParticipantIds.Add(FisherStableNetId);
		FightParticipantCharacters.Add(FisherStableNetId, FisherCharacter);
	}
	RefreshFightSummary();
	PublishSnapshot(ECatFishingSnapshotMutation::PhaseChange);
	Result.bApplied = true;
	Result.CurrentPhase = NewPhase;
	Result.Error = ECatDomainCommandError::None;
	Result.Revision = Snapshot.Revision;
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_phase_entered SessionId=%s Phase=%s Revision=%lld"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *UEnum::GetValueAsString(NewPhase), Snapshot.Revision);
	return Result;
}

// 搏斗协作流程：先按服务器 Controller 身份和 RequestId 重放，再要求 Giant、HookedFight 与匹配 Revision；随后复用服务的统一参战能力谓词，非 Active、无当前 Character、倒地或 FishingStrength/FightStamina 非正都在修改集合前拒绝。合法新参与者才刷新协作摘要、递增 Revision 并复制；重复参与者保持集合不变，但同一请求仍冻结为成功终态。
FCatDomainCommandResult ACatFishingSession::SubmitFightAssist(AController* AssistingController, const FGuid RequestId,
	const int64 ExpectedRevision)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString StableNetId = ResolveStableNetId(AssistingController);
	const FString CacheKey = FString::Printf(TEXT("%s|%s"), *StableNetId, *RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	if (const FCatDomainCommandResult* Cached = AssistTerminalCache.Find(CacheKey))
	{
		Result = *Cached;
		Result.bCommitted = false;
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	if (!RequestId.IsValid() || StableNetId.IsEmpty())
	{
		Result.Error = ECatDomainCommandError::InvalidIdentity;
	}
	else if (!Snapshot.bGiant || Snapshot.Phase != ECatFishingPhase::HookedFight || bCaptureResolved)
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
	}
	else if (ExpectedRevision != Snapshot.Revision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
	}
	else
	{
		FString ValidatedStableNetId;
		ACatCharacter* Character = nullptr;
		double FishingStrength = 0.0;
		double FightStamina = 0.0;
		if (!UCatFishingService::TryGetFightCapability(AssistingController, ValidatedStableNetId, Character,
			FishingStrength, FightStamina) || ValidatedStableNetId != StableNetId)
		{
			Result.Error = ECatDomainCommandError::InvalidPhase;
		}
		else
		{
			const bool bNewParticipant = !FightParticipantIds.Contains(StableNetId);
			FightParticipantIds.Add(StableNetId);
			FightParticipantCharacters.Add(StableNetId, Character);
			if (bNewParticipant)
			{
				RefreshFightSummary();
				PublishSnapshot(ECatFishingSnapshotMutation::Discrete);
			}
			Result.bCommitted = true;
			Result.Error = ECatDomainCommandError::None;
		}
	}
	Result.Revision = Snapshot.Revision;
	AssistTerminalCache.Add(CacheKey, Result);
	return Result;
}

// 搏斗交换流程：只接受运行中的 HookedFight、显式正消耗和足够参与人数；重读所有 ASC，力量达标且每只猫体力足够时一次扣除参与者体力与鱼体力并发布 Revision。
FCatDomainCommandResult ACatFishingSession::ResolveFightExchangeFromStateTree(const double FishStaminaCost,
	const double ParticipantStaminaCost)
{
	FCatDomainCommandResult Result;
	Result.RequestId = FGuid::NewGuid();
	if (!HasAuthority() || !StateTreeComponent || !StateTreeComponent->IsRunning()
		|| Snapshot.Phase != ECatFishingPhase::HookedFight || !FishDefinition
		|| !FMath::IsFinite(FishStaminaCost) || FishStaminaCost <= 0.0
		|| !FMath::IsFinite(ParticipantStaminaCost) || ParticipantStaminaCost <= 0.0)
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
		return Result;
	}
	const bool bSummaryChanged = RefreshFightSummary();
	if (Snapshot.FightParticipantCount < FishDefinition->MinimumFightParticipants
		|| Snapshot.CombinedFishingStrength < FishDefinition->FishStrength
		|| Snapshot.CombinedFightStamina < ParticipantStaminaCost * Snapshot.FightParticipantCount)
	{
		PublishRefreshedFightSummaryIfChanged(bSummaryChanged);
		Result.Error = ECatDomainCommandError::InvalidPhase;
		Result.Revision = Snapshot.Revision;
		return Result;
	}
	TArray<UCatAbilitySystemComponent*> ParticipantSystems;
	for (const TPair<FString, TWeakObjectPtr<ACatCharacter>>& Pair : FightParticipantCharacters)
	{
		ACatCharacter* Character = Pair.Value.Get();
		FString ValidatedStableNetId;
		ACatCharacter* ValidatedCharacter = nullptr;
		double FishingStrength = 0.0;
		double FightStamina = 0.0;
		if (!Character || !UCatFishingService::TryGetFightCapability(Character->GetController(),
			ValidatedStableNetId, ValidatedCharacter, FishingStrength, FightStamina)
			|| ValidatedStableNetId != Pair.Key || ValidatedCharacter != Character || FightStamina < ParticipantStaminaCost)
		{
			PublishRefreshedFightSummaryIfChanged(bSummaryChanged);
			Result.Error = ECatDomainCommandError::InvalidPhase;
			Result.Revision = Snapshot.Revision;
			return Result;
		}
		UCatAbilitySystemComponent* ASC = Character->GetCatAbilitySystemComponent();
		if (!ASC)
		{
			Result.Error = ECatDomainCommandError::DependencyUnavailable;
			Result.Revision = Snapshot.Revision;
			return Result;
		}
		ParticipantSystems.Add(ASC);
	}
	for (UCatAbilitySystemComponent* ASC : ParticipantSystems)
	{
		if (!ASC->ApplyFishingStaminaDelta(-static_cast<float>(ParticipantStaminaCost)))
		{
			Result.Error = ECatDomainCommandError::DependencyUnavailable;
			Result.Revision = Snapshot.Revision;
			return Result;
		}
		StaminaParticipantsTouched.Add(Cast<ACatCharacter>(ASC->GetAvatarActor()));
	}
	Snapshot.FishFightStaminaRemaining = FMath::Max(0.0, Snapshot.FishFightStaminaRemaining - FishStaminaCost);
	Snapshot.NormalizedFishStamina = FishDefinition->FishFightStamina > 0.0
		? FMath::Clamp(Snapshot.FishFightStaminaRemaining / FishDefinition->FishFightStamina, 0.0, 1.0) : 0.0;
	PublishSnapshot(ECatFishingSnapshotMutation::HighFrequency);
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	Result.Revision = Snapshot.Revision;
	return Result;
}

// 失败预算流程：先重放本会话唯一终态，再验证 StateTree/钓手 Equipment；首次把随机 RequestId 和当前 Equipment Revision 交给互斥惩罚事务，成功即关闭第二刀。
FCatFishingFailureResult ACatFishingSession::CommitFailureBudgetFromStateTree(const ECatFishingFailurePenalty Penalty)
{
	if (bFailureBudgetCommitted)
	{
		FCatFishingFailureResult Replay = FailureBudgetResult;
		Replay.Command.bCommitted = false;
		Replay.Command.Error = ECatDomainCommandError::AlreadyResolved;
		return Replay;
	}
	FCatFishingFailureResult Result;
	Result.Command.RequestId = FGuid::NewGuid();
	ACatCharacter* Character = FisherCharacter.Get();
	UCatEquipmentComponent* Equipment = Character ? Character->GetEquipmentComponent() : nullptr;
	if (!HasAuthority() || !StateTreeComponent || !StateTreeComponent->IsRunning() || !Equipment)
	{
		Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	Result = Equipment->CommitFishingFailure(Result.Command.RequestId, Equipment->GetSnapshot().Revision, Penalty);
	if (Result.Command.bCommitted)
	{
		bFailureBudgetCommitted = true;
		FailureBudgetResult = Result;
	}
	return Result;
}

// 重试耗尽流程：只接受 authority、运行中且未捕获的会话；把明确合格终态交给 Collection 生成唯一剪影 Grant，成功后终止 StateTree/会话而不创建 FishInstance。
FCatDomainCommandResult ACatFishingSession::ResolveRetryExhaustedEscapeFromStateTree()
{
	FCatDomainCommandResult Result;
	Result.RequestId = Snapshot.FishingSessionId;
	UCatRunImprintService* ImprintService = GetWorld() ? GetWorld()->GetSubsystem<UCatRunImprintService>() : nullptr;
	if (!HasAuthority() || bCaptureResolved || Snapshot.Phase == ECatFishingPhase::Terminated
		|| !StateTreeComponent || !StateTreeComponent->IsRunning() || !ImprintService || !FishDefinition)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	const FGuid GrantId = ImprintService->RecordRetryExhaustedSilhouette(
		Snapshot.FishingSessionId, FishDefinition->FishDefinitionId, FisherStableNetId);
	if (!GrantId.IsValid())
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	TerminateSession(ECatFishingOutcome::Escaped, TEXT("Retry budget exhausted"));
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	Result.Revision = Snapshot.Revision;
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_silhouette_committed SessionId=%s GrantId=%s Revision=%lld"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
		*GrantId.ToString(EGuidFormats::DigitsWithHyphens), Snapshot.Revision);
	return Result;
}

// 抢抄流程：先按身份/RequestId 重放，再用统一参战能力谓词、NearShore/Revision、服务器目标与 reach 拒绝不合法命令；随后重验巨鱼 HookedFight 参与者，只把仍 Active、未倒地且力量/体力为正的人与抄手去重后放入可选 Candidate。Items Compare-and-Commit 是实物唯一不可逆点，首个合法抄手独占鱼；FishRecorded 始终独立归档，只有鱼定义配置正式事件才提交 Candidate。批量计划接口先为全部参与者建齐并索引 Planned 记录，确认全量事实后才逐条投递，所以同步 RPC 回入不能留下部分计划，离线未投递仍按原 ID 重试；归档失败只记录且绝不回滚或复制实物鱼。最后写 Resolved、停树并启动有界复制窗口。
FCatScoopResult ACatFishingSession::RequestScoop(AController* ScoopingController, const FCatScoopCommand& Command)
{
	FCatScoopResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	const FString StableNetId = ResolveStableNetId(ScoopingController);
	FString ValidatedScooperId;
	ACatCharacter* ScoopingCharacter = nullptr;
	double ScooperFishingStrength = 0.0;
	double ScooperFightStamina = 0.0;
	const bool bScooperFightCapable = UCatFishingService::TryGetFightCapability(ScoopingController,
		ValidatedScooperId, ScoopingCharacter, ScooperFishingStrength, ScooperFightStamina)
		&& ValidatedScooperId == StableNetId;
	double ScoopReachCentimeters = 0.0;
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	const FString CacheKey = FString::Printf(TEXT("%s|%s"), *StableNetId,
		*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	if (const FCatScoopResult* Cached = ScoopTerminalCache.Find(CacheKey))
	{
		Result = *Cached;
		Result.Command.bCommitted = false;
		Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	if (bCaptureResolved)
	{
		Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
	}
	else if (!Command.Context.RequestId.IsValid() || StableNetId.IsEmpty() || !Command.TargetGuardContainerId.IsValid()
		|| Command.ScoopWorldLocation.ContainsNaN())
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (Snapshot.Phase != ECatFishingPhase::NearShore)
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPhase;
	}
	else if (Command.Context.ExpectedRevision != Snapshot.Revision)
	{
		Result.Command.Error = ECatDomainCommandError::RevisionConflict;
	}
	else if (!bScooperFightCapable || !ScoopingCharacter || !bHasNearShoreTarget || !Settings
		|| !Settings->TryGetScoopReach(ScoopReachCentimeters)
		|| FVector::DistSquared(ScoopingCharacter->GetActorLocation(), NearShoreTargetWorldLocation)
			> FMath::Square(ScoopReachCentimeters))
	{
		Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
	}
	else if (!ItemsService.IsValid() || !FishDefinition)
	{
		Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else
	{
		FCatContainerSnapshot GuardSnapshot;
		if (!ItemsService->TryGetContainerSnapshot(Command.TargetGuardContainerId, GuardSnapshot))
		{
			Result.Command.Error = ECatDomainCommandError::NotFound;
		}
		else
		{
			UCatRunImprintService* ImprintService = GetWorld() ? GetWorld()->GetSubsystem<UCatRunImprintService>() : nullptr;
			const ACatfishingGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
			const FGuid ProposedFishInstanceId = FGuid::NewGuid();
			const bool bHasCaptureImprintEvent = !FishDefinition->CaptureImprintEventId.IsNone();
			FCatImprintCandidate CaptureCandidate;
			CaptureCandidate.CandidateId = ProposedFishInstanceId;
			CaptureCandidate.RunId = GameState ? GameState->GetRunPublicState().Phase.RunId : FGuid();
			CaptureCandidate.EventType = FishDefinition->CaptureImprintEventId;
			CaptureCandidate.SubjectId = ProposedFishInstanceId;
			CaptureCandidate.FishDefinitionId = FishDefinition->FishDefinitionId;
			CaptureCandidate.bAllActivePlayersPresent = false;
			TSet<FString> CaptureParticipants;
			if (Snapshot.bGiant)
			{
				for (const TPair<FString, TWeakObjectPtr<ACatCharacter>>& Pair : FightParticipantCharacters)
				{
					ACatCharacter* ParticipantCharacter = Pair.Value.Get();
					FString ParticipantStableNetId;
					ACatCharacter* ValidatedCharacter = nullptr;
					double FishingStrength = 0.0;
					double FightStamina = 0.0;
					if (ParticipantCharacter && UCatFishingService::TryGetFightCapability(
						ParticipantCharacter->GetController(), ParticipantStableNetId, ValidatedCharacter,
						FishingStrength, FightStamina) && ParticipantStableNetId == Pair.Key
						&& ValidatedCharacter == ParticipantCharacter)
					{
						CaptureParticipants.Add(ParticipantStableNetId);
					}
				}
			}
			CaptureParticipants.Add(StableNetId);
			CaptureCandidate.ParticipantStableNetIds = CaptureParticipants.Array();
			CaptureCandidate.ParticipantStableNetIds.Sort();
			CaptureCandidate.ParticipantCount = CaptureCandidate.ParticipantStableNetIds.Num();
			// FishRecorded 是实物捕获的必需永久事实；CapturePlan 只在鱼定义提供正式事件时预检，None 不得反向关闭捕获链。
			if (!ImprintService || !ImprintService->CanRecordCommittedCapture()
				|| (bHasCaptureImprintEvent
					&& (!GameState || !ImprintService->CanAcceptImprintCandidate(CaptureCandidate))))
			{
				Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
				Result.Command.Revision = Snapshot.Revision;
				ScoopTerminalCache.Add(CacheKey, Result);
				return Result;
			}
			FCatCaptureCommitCommand CaptureCommand;
			CaptureCommand.Context = Command.Context;
			CaptureCommand.Context.StableNetId = StableNetId;
			CaptureCommand.Context.ExpectedRevision = GuardSnapshot.Revision;
			CaptureCommand.FishingSessionId = Snapshot.FishingSessionId;
			CaptureCommand.FishInstanceId = ProposedFishInstanceId;
			CaptureCommand.FishDefinitionId = FishDefinition->FishDefinitionId;
			CaptureCommand.TargetContainerId = Command.TargetGuardContainerId;
			CaptureCommand.WeightKilograms = FishWeightKilograms;
			CaptureCommand.SacrificeContribution = FishDefinition->SacrificeContribution;
			const FCatCaptureCommitResult CaptureResult = ItemsService->CommitCapture(CaptureCommand);
			Result.Command = CaptureResult.Command;
			Result.Capture = CaptureResult.Committed;
			const bool bHasCommittedCapture = CaptureResult.Command.bCommitted
				|| CaptureResult.Command.Error == ECatDomainCommandError::AlreadyResolved;
			if (bHasCommittedCapture && IsCommittedCaptureForCurrentSession(CaptureResult.Committed))
			{
				FCatImprintCandidate CommittedCaptureCandidate = CaptureCandidate;
				CommittedCaptureCandidate.CandidateId = CaptureResult.Committed.FishInstance.FishInstanceId;
				CommittedCaptureCandidate.SubjectId = CaptureResult.Committed.FishInstance.FishInstanceId;
				CommittedCaptureCandidate.FishDefinitionId = CaptureResult.Committed.FishInstance.FishDefinitionId;
				TSet<FString> CommittedCaptureParticipants;
				if (Snapshot.bGiant)
				{
					for (const TPair<FString, TWeakObjectPtr<ACatCharacter>>& Pair : FightParticipantCharacters)
					{
						ACatCharacter* ParticipantCharacter = Pair.Value.Get();
						FString ParticipantStableNetId;
						ACatCharacter* ValidatedCharacter = nullptr;
						double FishingStrength = 0.0;
						double FightStamina = 0.0;
						if (ParticipantCharacter && UCatFishingService::TryGetFightCapability(
							ParticipantCharacter->GetController(), ParticipantStableNetId, ValidatedCharacter,
							FishingStrength, FightStamina) && ParticipantStableNetId == Pair.Key
							&& ValidatedCharacter == ParticipantCharacter)
						{
							CommittedCaptureParticipants.Add(ParticipantStableNetId);
						}
					}
				}
				CommittedCaptureParticipants.Add(CaptureResult.Committed.FishInstance.OwnerStableNetId);
				CommittedCaptureCandidate.ParticipantStableNetIds = CommittedCaptureParticipants.Array();
				CommittedCaptureCandidate.ParticipantStableNetIds.Sort();
				CommittedCaptureCandidate.ParticipantCount = CommittedCaptureCandidate.ParticipantStableNetIds.Num();
				FCatCaptureConditionSnapshot Condition;
				Condition.RegionId = WaterRegionSnapshot.RegionId;
				const FGuid FishRecordedGrantId = ImprintService->RecordCommittedCapture(
					CaptureResult.Committed, CaptureResult.Committed.FishInstance.OwnerStableNetId, Condition);
				bool bOptionalPlanCommitted = true;
				if (bHasCaptureImprintEvent)
				{
					// 巨鱼候选包含 HookedFight 的合法钓手/协作者以及最终抄手；实物归属仍只来自 Items 的首个近岸 Compare-and-Commit。
					bOptionalPlanCommitted = ImprintService->SubmitImprintCandidate(CommittedCaptureCandidate);
					TArray<FCatCapturePlan> CapturePlans;
					bOptionalPlanCommitted = bOptionalPlanCommitted
						&& ImprintService->CreateCapturePlansForParticipants(CommittedCaptureCandidate.CandidateId,
							CommittedCaptureCandidate.ParticipantStableNetIds, false, CapturePlans);
				}
				if (!FishRecordedGrantId.IsValid() || !bOptionalPlanCommitted)
				{
					UE_LOG(LogCatFishing, Error,
						TEXT("Event=fishing_capture_archive_commit_failed SessionId=%s RequestId=%s OptionalPlanValid=%s FishRecordedGrantValid=%s"),
						*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
						*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
						bOptionalPlanCommitted ? TEXT("true") : TEXT("false"),
						FishRecordedGrantId.IsValid() ? TEXT("true") : TEXT("false"));
				}
				if (ReconcileCommittedCapture(CaptureResult.Committed))
				{
					Result.Command.Revision = Snapshot.Revision;
				}
			}
		}
	}
	Result.Command.Revision = Snapshot.Revision;
	ScoopTerminalCache.Add(CacheKey, Result);
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_scoop_terminal SessionId=%s RequestId=%s Committed=%s Error=%s Revision=%lld"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
		*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		Result.Command.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Command.Error), Snapshot.Revision);
	return Result;
}

// 会话终止流程：非 authority、已 Resolved 或已 Terminated 直接幂等返回，避免覆盖捕获终态。首次中断只写一次 Terminated/Revision 并发布快照，再停止 StateTree、释放钓手之外的参与弱引用且不触碰 Items；最后启动配置的有界复制窗口，让客户端看见终态后销毁 Actor，服务据此释放钓手的单活跃槽位。
void ACatFishingSession::TerminateSession(const ECatFishingOutcome Outcome, const TCHAR* DiagnosticReason)
{
	switch (Outcome)
	{
	case ECatFishingOutcome::EmptyHook:
	case ECatFishingOutcome::HookWindowExpired:
	case ECatFishingOutcome::Escaped:
	case ECatFishingOutcome::RodBroken:
	case ECatFishingOutcome::CatInWater:
	case ECatFishingOutcome::Cancelled:
	case ECatFishingOutcome::Invalidated:
		FinalizeSession(ECatFishingPhase::Terminated, Outcome, DiagnosticReason);
		return;
	default:
		return;
	}
}

void ACatFishingSession::FinalizeSession(const ECatFishingPhase FinalPhase, const ECatFishingOutcome FinalOutcome,
	const TCHAR* DiagnosticReason)
{
	if (!HasAuthority() || IsTerminal())
	{
		return;
	}
	Snapshot.Phase = FinalPhase;
	Snapshot.Outcome = FinalOutcome;
	Snapshot.PhaseStartedServerTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	PublishSnapshot(ECatFishingSnapshotMutation::PhaseChange);
	if (StateTreeComponent && StateTreeComponent->IsRunning())
	{
		StateTreeComponent->StopLogic(FString(DiagnosticReason));
	}
	for (const TWeakObjectPtr<ACatCharacter>& WeakParticipant : StaminaParticipantsTouched)
	{
		if (ACatCharacter* Participant = WeakParticipant.Get())
		{
			if (UCatAbilitySystemComponent* AbilitySystem = Participant->GetCatAbilitySystemComponent())
			{
				AbilitySystem->RequestFishingStaminaReset();
			}
		}
	}
	StaminaParticipantsTouched.Reset();
	FightParticipantIds.Reset();
	FightParticipantCharacters.Reset();
	FisherCharacter.Reset();
	ScheduleTerminalDestroy();
	if (FinalPhase == ECatFishingPhase::Resolved && FinalOutcome == ECatFishingOutcome::Caught)
	{
		UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_session_resolved SessionId=%s Outcome=%s Reason=%s Revision=%lld"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *UEnum::GetValueAsString(FinalOutcome),
			DiagnosticReason ? DiagnosticReason : TEXT("None"), Snapshot.Revision);
	}
	else
	{
		UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_session_terminated SessionId=%s Outcome=%s Reason=%s Revision=%lld"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *UEnum::GetValueAsString(FinalOutcome),
			DiagnosticReason ? DiagnosticReason : TEXT("None"), Snapshot.Revision);
	}
}

// Character 关联查询流程：比较初始钓手和协作者弱引用；不以名字或网络地址猜测。
bool ACatFishingSession::InvolvesCharacter(const ACatCharacter* Character) const
{
	if (!Character)
	{
		return false;
	}
	if (FisherCharacter.Get() == Character)
	{
		return true;
	}
	for (const TPair<FString, TWeakObjectPtr<ACatCharacter>>& Pair : FightParticipantCharacters)
	{
		if (Pair.Value.Get() == Character)
		{
			return true;
		}
	}
	return false;
}

// Snapshot 读取流程：返回本机当前只读事实，不暴露身份或服务引用。
const FCatFishingSessionSnapshot& ACatFishingSession::GetSnapshot() const
{
	return Snapshot;
}

// 终态读取流程：只读取公开阶段，不停止 StateTree 或销毁 Actor；服务用它移除单活跃索引，终态 Actor 仍可完成最后一次复制。
bool ACatFishingSession::IsTerminal() const
{
	return Snapshot.Phase == ECatFishingPhase::Resolved || Snapshot.Phase == ECatFishingPhase::Terminated;
}

// World 清理流程：停止仍在运行的 StateTree，并为仍可达参与者登记/应用 stamina 恢复后清私有弱引用；不补发捕获事务。
void ACatFishingSession::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (StateTreeComponent && StateTreeComponent->IsRunning())
	{
		StateTreeComponent->StopLogic(TEXT("FishingSession EndPlay"));
	}
	if (HasAuthority())
	{
		for (const TWeakObjectPtr<ACatCharacter>& WeakParticipant : StaminaParticipantsTouched)
		{
			if (ACatCharacter* Participant = WeakParticipant.Get())
			{
				if (UCatAbilitySystemComponent* AbilitySystem = Participant->GetCatAbilitySystemComponent())
				{
					AbilitySystem->RequestFishingStaminaReset();
				}
			}
		}
		StaminaParticipantsTouched.Reset();
	}
	ItemsService.Reset();
	FisherCharacter.Reset();
	FightParticipantCharacters.Reset();
	Super::EndPlay(EndPlayReason);
}

// 身份解析流程：只读取当前 Controller PlayerState 的继承 UniqueId；无效时返回空并让命令 fail-closed。
FString ACatFishingSession::ResolveStableNetId(const AController* Controller)
{
	const APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
	return PlayerState && PlayerState->GetUniqueId().IsValid() ? PlayerState->GetUniqueId()->ToString() : FString();
}

// 发布流程：仅 authority 请求即时网络更新；Snapshot 本身由单一 Replicated 属性发送。
void ACatFishingSession::OnRep_Snapshot()
{
	NotifySnapshotChanged();
}

void ACatFishingSession::NotifySnapshotChanged()
{
	OnSnapshotChanged.Broadcast();
}

void ACatFishingSession::PublishSnapshot(const ECatFishingSnapshotMutation Mutation)
{
	if (HasAuthority())
	{
		Snapshot.AdvanceVersion(Mutation);
		ForceNetUpdate();
		NotifySnapshotChanged();
	}
}

// 搏斗摘要流程：清零三项聚合后，对每个弱 Character 重用 FishingService 的 Active/未倒地/正能力谓词；只累加身份与弱引用仍一致的当前参与者。
bool ACatFishingSession::IsCommittedCaptureForCurrentSession(const FCatCaptureCommittedResult& Committed) const
{
	return Committed.CaptureRequestId.IsValid() && Committed.FishingSessionId == Snapshot.FishingSessionId
		&& Committed.FishInstance.FishInstanceId.IsValid() && !Committed.FishInstance.FishDefinitionId.IsNone()
		&& Committed.FishInstance.FishDefinitionId == Snapshot.FishDefinitionId
		&& Committed.FishInstance.SourceFishingSessionId == Snapshot.FishingSessionId
		&& !Committed.FishInstance.OwnerStableNetId.IsEmpty() && Committed.ContainerId.IsValid()
		&& Committed.ContainerRevision > 0 && FMath::IsFinite(Committed.FishInstance.WeightKilograms)
		&& Committed.FishInstance.WeightKilograms > 0.0 && Committed.FishInstance.SacrificeContribution > 0;
}

bool ACatFishingSession::ReconcileCommittedCapture(const FCatCaptureCommittedResult& Committed)
{
	if (!HasAuthority() || !IsCommittedCaptureForCurrentSession(Committed) || IsTerminal())
	{
		return false;
	}
	bCaptureResolved = true;
	FinalizeSession(ECatFishingPhase::Resolved, ECatFishingOutcome::Caught, TEXT("Capture reconciled"));
	return IsTerminal() && Snapshot.Phase == ECatFishingPhase::Resolved && Snapshot.Outcome == ECatFishingOutcome::Caught;
}

bool ACatFishingSession::RefreshFightSummary()
{
	const int32 PreviousParticipantCount = Snapshot.FightParticipantCount;
	const double PreviousCombinedFishingStrength = Snapshot.CombinedFishingStrength;
	const double PreviousCombinedFightStamina = Snapshot.CombinedFightStamina;
	Snapshot.FightParticipantCount = 0;
	Snapshot.CombinedFishingStrength = 0.0;
	Snapshot.CombinedFightStamina = 0.0;
	for (const TPair<FString, TWeakObjectPtr<ACatCharacter>>& Pair : FightParticipantCharacters)
	{
		ACatCharacter* Character = Pair.Value.Get();
		FString StableNetId;
		ACatCharacter* ValidatedCharacter = nullptr;
		double Strength = 0.0;
		double FightStamina = 0.0;
		if (Character && UCatFishingService::TryGetFightCapability(Character->GetController(), StableNetId,
			ValidatedCharacter, Strength, FightStamina) && StableNetId == Pair.Key
			&& ValidatedCharacter == Character)
		{
			++Snapshot.FightParticipantCount;
			Snapshot.CombinedFishingStrength += Strength;
			Snapshot.CombinedFightStamina += FightStamina;
		}
	}
	return Snapshot.FightParticipantCount != PreviousParticipantCount
		|| Snapshot.CombinedFishingStrength != PreviousCombinedFishingStrength
		|| Snapshot.CombinedFightStamina != PreviousCombinedFightStamina;
}

void ACatFishingSession::PublishRefreshedFightSummaryIfChanged(const bool bSummaryChanged)
{
	if (bSummaryChanged)
	{
		PublishSnapshot(ECatFishingSnapshotMutation::HighFrequency);
	}
}

// 终态销毁流程：读取与会话启动共用的显式正复制窗口，成功时交给 Actor lifespan 延迟销毁；若运行中配置突然失效，则下一帧销毁而不无界泄漏。
void ACatFishingSession::ScheduleTerminalDestroy()
{
	if (!HasAuthority() || !IsTerminal())
	{
		return;
	}
	double WindowSeconds = 0.0;
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	SetLifeSpan(Settings && Settings->TryGetTerminalReplicationWindow(WindowSeconds)
		? static_cast<float>(WindowSeconds) : KINDA_SMALL_NUMBER);
}
