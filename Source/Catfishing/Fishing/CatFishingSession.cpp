#include "Fishing/CatFishingSession.h"

#include "Character/CatCharacter.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "Logging/CatLog.h"
#include "Logging/CatLogContext.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Config/CatAbilitySettings.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "Collection/CatRunImprintService.h"
#include "Data/CatFishDefinition.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishPersonalityDefinition.h"
#include "Environment/CatChumFieldSubsystem.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Fishing/Actors/CatFishEncounterActor.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingGameplayTags.h"
#include "Fishing/CatFishingStateTreeEvents.h"
#include "Fishing/Config/CatFishingFightBalanceDefinition.h"
#include "Fishing/Presentation/CatFishingPresentationSettings.h"
#include "Fishing/Presentation/CatFishPresentationDefinition.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/Integration/CatFishingAimLibrary.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Fishing/Simulation/CatFishFightMotionSolver.h"
#include "Fishing/Simulation/CatFishingFightRunner.h"
#include "Fishing/Actors/CatFishingHookActor.h"
#include "Components/CapsuleComponent.h"
#include "Components/StateTreeComponent.h"
#include "GameFramework/PlayerState.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Items/CatItemsService.h"
#include "Items/CatWorldItemSettings.h"
#include "Items/World/CatFishPickupActor.h"
#include "Net/UnrealNetwork.h"
#include "StateTree.h"
#include "TimerManager.h"

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

// 阶段进入流程：先验证 authority、唯一 StateTree 生命周期和未结算状态；NearShore 只接受水域包围盒内的服务器目标并冻结该位置，其他阶段清除目标。HookedFight 与 NearShore 保留钓手/协作者供搏斗和巨鱼候选使用，其余阶段把参与集合收回为钓手；随后刷新协作摘要、递增一次 Revision 并复制快照，若资产进入终态则启动有界销毁。C++ 只应用资产已选阶段，不维护转移拓扑。
bool ACatFishingSession::TryReadNearShoreFishSpatial(FCatWaterSpatialResult& OutSpatial) const
{
	OutSpatial = FCatWaterSpatialResult{}; // 先清空输出，任何提前返回都不会带出脏数据。
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	const UCatWaterQuerySubsystem* Water = GetWorld() ? GetWorld()->GetSubsystem<UCatWaterQuerySubsystem>() : nullptr;
	const ACatFishEncounterActor* Encounter = Snapshot.FishEncounterActor;
	if (!HasAuthority() || !Settings || !Water || !Encounter || !AttemptSnapshot.WaterRegion.IsValid()
		|| !FMath::IsFinite(Settings->NearShoreWidthCentimeters) || Settings->NearShoreWidthCentimeters <= 0.0)
	{
		return false;
	}
	// 用鱼当前权威位置查询它与水域岸线的空间关系（在水内/水外、离岸带符号距离）。
	OutSpatial = Water->QueryShoreRelation(Encounter->GetActorLocation(), AttemptSnapshot.WaterRegion);
	// 只有鱼仍在水域内部、且离岸距离落在 (0, NearShoreWidthCentimeters] 这个近岸带内才算合法近岸目标；
	// 距离为 0 或负数意味着已经越过岸线，不属于"近岸"。
	return OutSpatial.bSucceeded && OutSpatial.Containment == ECatWaterContainment::Inside
		&& OutSpatial.SignedDistanceToShoreCm > 0.0
		&& OutSpatial.SignedDistanceToShoreCm <= Settings->NearShoreWidthCentimeters;
}

FCatFishingPhaseResult ACatFishingSession::EnterPhaseFromStateTree(const ECatFishingPhase NewPhase)
{
	FCatFishingPhaseResult Result;
	Result.PreviousPhase = Snapshot.Phase;
	Result.CurrentPhase = Snapshot.Phase; // 默认失败时保持在原阶段。
	Result.Revision = Snapshot.Revision;
	if (!HasAuthority())
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	// 已经进入终态、捕获已提交，或者资产试图直接把 Resolved/Terminated 当作普通阶段进入，
	// 都视为"已经结束"，拒绝再次写阶段（终态只能走 FinalizeSession 这一条路）。
	if (IsTerminal() || bCaptureResolved || NewPhase == ECatFishingPhase::Resolved || NewPhase == ECatFishingPhase::Terminated)
	{
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	// StateTree 必须存在且要么正在运行、要么处于 StartLogic 同步进入首状态的短暂窗口内，否则拒绝写入。
	if (!StateTreeComponent || (!StateTreeComponent->IsRunning() && !bStartupInProgress))
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	if (NewPhase == ECatFishingPhase::NearShore)
	{
		// 进入近岸阶段前必须先能读出一个合法的服务器权威近岸目标（鱼确实在水域内的近岸带里），
		// 否则拒绝进入，避免在没有合法抢抄目标的情况下开放 RequestScoop。
		FCatWaterSpatialResult FishSpatial;
		if (!TryReadNearShoreFishSpatial(FishSpatial))
		{
			Result.Error = ECatDomainCommandError::PolicyUndecided;
			return Result;
		}
	}
	if (NewPhase == ECatFishingPhase::HookedFight && !bFightStaminaInitialized)
	{
		// 首次进入搏斗时才初始化钓手的搏斗体力池（幂等标记 bFightStaminaInitialized 防止重复阶段事件补满体力）。
		UCatAbilitySystemComponent* AbilitySystem = FisherCharacter.IsValid()
			? FisherCharacter->GetCatAbilitySystemComponent() : nullptr;
		if (!AbilitySystem || !AbilitySystem->InitializeFishingStaminaForSession())
		{
			Result.Error = ECatDomainCommandError::DependencyUnavailable;
			return Result;
		}
		bFightStaminaInitialized = true;
		StaminaParticipantsTouched.Add(FisherCharacter); // 记入"需要在会话结束时恢复体力"的名单。
	}
	if (NewPhase == ECatFishingPhase::ExhaustedReel
		&& (!FightRunner || !FightRunner->SetFishExhaustedFromAuthority()))
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	Snapshot.Phase = NewPhase;
	Snapshot.PhaseStartedServerTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (Snapshot.HookActor)
	{
		switch (NewPhase)
		{
		case ECatFishingPhase::Waiting:
			Snapshot.HookActor->SetBobberPresentationModeFromAuthority(ECatFishingBobberPresentationMode::Calm);
			break;
		case ECatFishingPhase::Probe:
			Snapshot.HookActor->SetBobberPresentationModeFromAuthority(ECatFishingBobberPresentationMode::BiteWarning);
			break;
		case ECatFishingPhase::TrueBiteWindow:
			Snapshot.HookActor->SetBobberPresentationModeFromAuthority(ECatFishingBobberPresentationMode::Sunk);
			break;
		case ECatFishingPhase::HookedFight:
		case ECatFishingPhase::NearShore:
		case ECatFishingPhase::ExhaustedReel:
			Snapshot.HookActor->SetBobberPresentationModeFromAuthority(ECatFishingBobberPresentationMode::None);
			break;
		default:
			break;
		}
	}
	// 巨鱼离开 HookedFight 后仍需把合法协作者带入成像候选；NearShore 只冻结参与事实，不再接受新的 assist。
	if (NewPhase != ECatFishingPhase::HookedFight && NewPhase != ECatFishingPhase::NearShore
		&& NewPhase != ECatFishingPhase::ExhaustedReel)
	{
		// 其余阶段（Probe/TrueBiteWindow/Waiting 等）参与集合收敛回"只有钓手"，
		// 避免上一轮搏斗留下的协作者身份污染新一轮的判定。
		FightParticipantIds.Reset();
		FightParticipantCharacters.Reset();
		FightParticipantIds.Add(FisherStableNetId);
		FightParticipantCharacters.Add(FisherStableNetId, FisherCharacter);
	}
	RefreshFightSummary(); // 按最新参与集合重新聚合力量/体力，保证阶段切换那一刻的快照就是准确的。
	PublishSnapshot(ECatFishingSnapshotMutation::PhaseChange); // 阶段变化必须递增 PhaseEpoch，拒绝上一阶段的延迟事件。
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
	// 从 Controller 重新解析服务器私有身份，绝不信任客户端携带的身份字段。
	const FString StableNetId = ResolveStableNetId(AssistingController);
	// 终态缓存键=身份+RequestId：同一玩家对同一个协作请求重复提交（比如 RPC 重发）必须幂等重放，不重复修改参与集合。
	const FString CacheKey = FString::Printf(TEXT("%s|%s"), *StableNetId, *RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	if (const FCatDomainCommandResult* Cached = AssistTerminalCache.Find(CacheKey))
	{
		// 命中缓存：直接回放第一次的终态，但把 bCommitted 强制置 false、Error 置 AlreadyResolved，
		// 表明"这不是一次新的提交"，调用方不应把它当作再次成功处理。
		Result = *Cached;
		Result.bCommitted = false;
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	if (!RequestId.IsValid() || StableNetId.IsEmpty())
	{
		// RequestId 非法或身份解析失败（Controller/PlayerState 无效）都不可信，直接拒绝。
		Result.Error = ECatDomainCommandError::InvalidIdentity;
	}
	else if (!Snapshot.bGiant || Snapshot.Phase != ECatFishingPhase::HookedFight || bCaptureResolved)
	{
		// 协作只对巨鱼在 HookedFight 阶段开放；普通鱼、非搏斗阶段或已提交捕获一律拒绝。
		Result.Error = ECatDomainCommandError::InvalidPhase;
	}
	else if (ExpectedRevision != Snapshot.Revision)
	{
		// 客户端提交时携带的 ExpectedRevision 必须与当前 Session Revision 一致，
		// 防止基于过期快照发起的协作请求被误判为针对当前状态生效。
		Result.Error = ECatDomainCommandError::RevisionConflict;
	}
	else
	{
		FString ValidatedStableNetId;
		ACatCharacter* Character = nullptr;
		double FishingStrength = 0.0;
		double FightStamina = 0.0;
		// 复用 FishingService 的统一参战能力谓词：必须 Active、未倒地、且力量/体力均为正才具备协作资格；
		// 并且谓词内部重新解析出的身份要和外部传入的 StableNetId 完全一致，防止身份/Character 被偷换。
		if (!UCatFishingService::TryGetFightCapability(AssistingController, ValidatedStableNetId, Character,
			FishingStrength, FightStamina) || ValidatedStableNetId != StableNetId)
		{
			Result.Error = ECatDomainCommandError::InvalidPhase;
		}
		else
		{
			// 判断是否是本会话第一次见到这个身份（避免同一玩家重复加入时反复刷新/复制）。
			const bool bNewParticipant = !FightParticipantIds.Contains(StableNetId);
			FightParticipantIds.Add(StableNetId);
			FightParticipantCharacters.Add(StableNetId, Character);
			if (bNewParticipant)
			{
				// 只有真正新增参与者时才重新聚合力量/体力并推一次离散复制；
				// 重复请求（已在集合中）保持集合不变，不产生多余的网络更新。
				RefreshFightSummary();
				PublishSnapshot(ECatFishingSnapshotMutation::Discrete);
			}
			Result.bCommitted = true;
			Result.Error = ECatDomainCommandError::None;
		}
	}
	Result.Revision = Snapshot.Revision;
	AssistTerminalCache.Add(CacheKey, Result); // 无论成功失败都写入终态缓存，保证后续重放幂等。
	return Result;
}

// 搏斗交换流程：只接受运行中的 HookedFight、显式正消耗和足够参与人数；重读所有 ASC，力量达标且每只猫体力足够时一次扣除参与者体力与鱼体力并发布 Revision。
FCatDomainCommandResult ACatFishingSession::ResolveFightExchangeFromStateTree(const double FishStaminaCost,
	const double ParticipantStaminaCost)
{
	FCatDomainCommandResult Result;
	if (FightRunner && FightRunner->IsRunning())
	{
		// 常规搏斗（非巨鱼协作战）已经由 FightRunner 的固定步长模拟接管体力消耗，
		// StateTree 的这条交换节点在 Runner 运行期间不应该再重复扣体力，直接拒绝。
		Result.Error = ECatDomainCommandError::InvalidPhase;
		return Result;
	}
	Result.RequestId = FGuid::NewGuid(); // 本次交换事务自身没有客户端 RequestId，服务器生成一个用于日志/追踪。
	if (!HasAuthority() || !StateTreeComponent || !StateTreeComponent->IsRunning()
		|| Snapshot.Phase != ECatFishingPhase::HookedFight || !FishDefinition
		|| !FMath::IsFinite(FishStaminaCost) || FishStaminaCost <= 0.0
		|| !FMath::IsFinite(ParticipantStaminaCost) || ParticipantStaminaCost <= 0.0)
	{
		// 必须在运行中的 HookedFight 阶段，且两项消耗都必须是合法正数——0 或负消耗没有意义，直接拒绝。
		Result.Error = ECatDomainCommandError::InvalidPhase;
		return Result;
	}
	// 交换前先重新聚合一次参战集合的力量/体力（集合可能因掉线/倒地而发生变化）。
	const bool bSummaryChanged = RefreshFightSummary();
	if (Snapshot.FightParticipantCount < FishDefinition->MinimumFightParticipants
		|| Snapshot.CombinedFishingStrength < Snapshot.FishStrength
		|| Snapshot.CombinedFightStamina < ParticipantStaminaCost * Snapshot.FightParticipantCount)
	{
		// 人数不足、合计力量压不过鱼、或合计体力不够支付这一轮全员消耗，都视为条件不满足，拒绝本次交换。
		PublishRefreshedFightSummaryIfChanged(bSummaryChanged);
		Result.Error = ECatDomainCommandError::InvalidPhase;
		Result.Revision = Snapshot.Revision;
		return Result;
	}
	// 第一遍：逐个重验每位参与者的资格与体力是否够扣本次消耗，全部通过才收集其 AbilitySystemComponent；
	// 任何一人不合格就整体失败退出——要么全员一起扣体力，要么一个都不扣，避免部分扣款的不一致状态。
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
	// 第二遍：真正原子地扣除每个人的体力（第一遍已确认全员合格，这里理论上不会再失败，
	// 但仍防御式检查 ApplyFishingStaminaDelta 的返回值，一旦失败立即报错退出）。
	for (UCatAbilitySystemComponent* ASC : ParticipantSystems)
	{
		if (!ASC->ApplyFishingStaminaDelta(-static_cast<float>(ParticipantStaminaCost)))
		{
			Result.Error = ECatDomainCommandError::DependencyUnavailable;
			Result.Revision = Snapshot.Revision;
			return Result;
		}
		// 记入"本会话触碰过体力池"的名单，供会话结束时统一恢复。
		StaminaParticipantsTouched.Add(Cast<ACatCharacter>(ASC->GetAvatarActor()));
	}
	// 扣完参与者体力后再扣鱼的体力，并同步重算归一化体力比例供 UI 使用。
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
		// 本会话失败惩罚只能提交一次（丢饵/伤竿互斥）：重放缓存的终态，并显式标记本次不是新提交。
		FCatFishingFailureResult Replay = FailureBudgetResult;
		Replay.Command.bCommitted = false;
		Replay.Command.Error = ECatDomainCommandError::AlreadyResolved;
		return Replay;
	}
	FCatFishingFailureResult Result;
	Result.Command.RequestId = FGuid::NewGuid();
	UCatEquipmentComponent* Equipment = CastEquipment.Get(); // 失败惩罚（丢饵/伤竿）结算到抛竿者装备。
	if (!HasAuthority() || !StateTreeComponent || !StateTreeComponent->IsRunning() || !Equipment)
	{
		Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	// 真正的惩罚事务委托给装备组件，按其当前 Revision 提交（丢特殊饵或伤竿由 Penalty 参数决定）。
	Result = Equipment->CommitFishingFailure(Result.Command.RequestId, Equipment->GetSnapshot().Revision, Penalty);
	if (Result.Command.bCommitted)
	{
		// 只在真正提交成功时才关闭"第二刀"，失败允许调用方在其他条件满足后重试。
		bFailureBudgetCommitted = true;
		FailureBudgetResult = Result;
	}
	return Result;
}

// 重试耗尽流程：只接受 authority、运行中且未捕获的会话；把明确合格终态交给 Collection 生成唯一剪影 Grant，成功后终止 StateTree/会话而不创建 FishInstance。
FCatDomainCommandResult ACatFishingSession::ResolveRetryExhaustedEscapeFromStateTree()
{
	FCatDomainCommandResult Result;
	Result.RequestId = Snapshot.FishingSessionId; // 该终局唯一对应本会话，直接用 SessionId 作为事务标识。
	UCatRunImprintService* ImprintService = GetWorld() ? GetWorld()->GetSubsystem<UCatRunImprintService>() : nullptr;
	if (!HasAuthority() || bCaptureResolved || Snapshot.Phase == ECatFishingPhase::Terminated
		|| !StateTreeComponent || !StateTreeComponent->IsRunning() || !ImprintService || !FishDefinition)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	// 不生成实物鱼，只登记一条"剪影"图鉴记录，让玩家知道曾经遇到过这条鱼但重试耗尽未能捕获。
	const FGuid GrantId = ImprintService->RecordRetryExhaustedSilhouette(
		Snapshot.FishingSessionId, FishDefinition->FishDefinitionId, FisherStableNetId);
	if (!GrantId.IsValid())
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	// 剪影记录成功后才终止会话，避免归档失败却已经结束会话导致这条鱼彻底遗失。
	TerminateSession(ECatFishingOutcome::Escaped, TEXT("Retry budget exhausted"));
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	Result.Revision = Snapshot.Revision;
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_silhouette_committed SessionId=%s GrantId=%s Revision=%lld"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
		*GrantId.ToString(EGuidFormats::DigitsWithHyphens), Snapshot.Revision);
	return Result;
}

// 钓手接力转移流程：等口阶段迁移身份，HookedFight 额外迁移 Runner 的 ASC、力量、体力与输入序号域。
// 原始抛竿者的 CastEquipment 始终不变；捕获物最终是落地世界鱼，接力不读取或冻结任何鱼护。
bool ACatFishingSession::TransferFisherFromAuthority(AController* NewFisherController)
{
	const FString NewStableNetId = ResolveStableNetId(NewFisherController);
	FString ValidatedId;
	ACatCharacter* NewCharacter = nullptr;
	double NewStrength = 0.0;
	double NewStamina = 0.0;
	const bool bCapable = UCatFishingService::TryGetFightCapability(NewFisherController, ValidatedId, NewCharacter,
		NewStrength, NewStamina) && ValidatedId == NewStableNetId;
	const bool bFightTakeover = Snapshot.Phase == ECatFishingPhase::HookedFight
		|| Snapshot.Phase == ECatFishingPhase::ExhaustedReel;
	// 姿态与会话阶段正交：只要尚未终局，地上的同一根竿都允许新主操作手接管。
	const bool bTransferablePhase = Snapshot.Phase == ECatFishingPhase::CastFlight
		|| Snapshot.Phase == ECatFishingPhase::Waiting || Snapshot.Phase == ECatFishingPhase::Probe
		|| Snapshot.Phase == ECatFishingPhase::TrueBiteWindow || bFightTakeover
		|| Snapshot.Phase == ECatFishingPhase::NearShore || Snapshot.Phase == ECatFishingPhase::AutoHauling
		|| Snapshot.Phase == ECatFishingPhase::ExhaustedReel;
	if (!HasAuthority() || IsTerminal() || !bTransferablePhase || NewStableNetId.IsEmpty() || !bCapable
		|| !NewCharacter || !NewFisherController->PlayerState)
	{
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fishing_fisher_transfer_rejected SessionId=%s Phase=%s Transferable=%s FightCapable=%s NewStableIdValid=%s %s"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			*UEnum::GetValueAsString(Snapshot.Phase), bTransferablePhase ? TEXT("true") : TEXT("false"),
			bCapable ? TEXT("true") : TEXT("false"), NewStableNetId.IsEmpty() ? TEXT("false") : TEXT("true"),
			*CatLogContext::BuildControllerFields(NewFisherController));
		return false;
	}
	if (NewStableNetId == FisherStableNetId)
	{
		return true; // 同一钓手重复接管：幂等成功。
	}

	APlayerState* OldFisherPlayerState = Snapshot.FisherPlayerState;
	ACatCharacter* OldFisherCharacter = FisherCharacter.Get();
	const FString OldFisherLogValue = CatLogContext::BuildStableNetIdValue(OldFisherPlayerState);
	if (bFightTakeover)
	{
		UCatAbilitySystemComponent* NewAbilitySystem = NewCharacter->GetCatAbilitySystemComponent();
		float NewStaminaMaximum = 0.0f;
		const bool bStaminaConfigReady = GetDefault<UCatAbilitySettings>()->TryGetFightStaminaBaselineForCharacter(
			NewCharacter->GetCatDefinitionId(), NewStaminaMaximum)
			&& FMath::IsFinite(NewStaminaMaximum) && NewStaminaMaximum > 0.0f;
		if (!FightRunner || !FightRunner->IsRunning() || !NewAbilitySystem || !bStaminaConfigReady)
		{
			UE_LOG(LogCatFishing, Warning,
				TEXT("Event=fishing_fight_takeover_rejected SessionId=%s Reason=StaminaOrRunnerUnavailable Runner=%s StaminaConfig=%s %s"),
				*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
				FightRunner && FightRunner->IsRunning() ? TEXT("Running") : TEXT("Unavailable"),
				bStaminaConfigReady ? TEXT("Ready") : TEXT("Invalid"),
				*CatLogContext::BuildControllerFields(NewFisherController));
			return false;
		}

		bool bInitialPullHeld = false;
		bool bInitialSlackHeld = false;
		int64 InitialInputSequence = 0;
		if (const ACatfishingPlayerController* NewPlayerController = Cast<ACatfishingPlayerController>(NewFisherController))
		{
			if (const UCatFishingCommandComponent* Commands = NewPlayerController->GetFishingCommandComponent())
			{
				Commands->TryGetHeldFightInputStateFromAuthority(
					bInitialPullHeld, bInitialSlackHeld, InitialInputSequence);
			}
		}
		NewStamina = NewAbilitySystem->GetNumericAttribute(
			UCatSurvivalAttributeSet::GetFightStaminaAttribute());
		if (!FightRunner->TransferOperatorFromAuthority(NewFisherController->PlayerState,
			NewAbilitySystem, NewStrength,
			NewStaminaMaximum, NewStamina, InitialInputSequence, bInitialPullHeld, bInitialSlackHeld))
		{
			UE_LOG(LogCatFishing, Warning,
				TEXT("Event=fishing_fight_takeover_rejected SessionId=%s Reason=RunnerRebindFailed Strength=%.3f Stamina=%.3f StaminaMaximum=%.3f InputSequence=%lld %s"),
				*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), NewStrength,
				NewStamina, static_cast<double>(NewStaminaMaximum), InitialInputSequence,
				*CatLogContext::BuildControllerFields(NewFisherController));
			return false;
		}

		// 旧操作手离开后保留当下体力，不再瞬间补满；同时从本会话终态恢复名单移除，
		// 防止他去另一根竿后被旧会话的收尾错误覆盖。
		if (OldFisherCharacter && OldFisherCharacter != NewCharacter)
		{
			StaminaParticipantsTouched.Remove(OldFisherCharacter);
		}
		StaminaParticipantsTouched.Add(NewCharacter);
		Snapshot.bReeling = FightRunner->GetCatAction() == ECatFightCatAction::Pull;
		Snapshot.bSlacking = FightRunner->GetCatAction() == ECatFightCatAction::Slack;
	}
	FightParticipantIds.Remove(FisherStableNetId);
	FightParticipantCharacters.Remove(FisherStableNetId);
	FisherStableNetId = NewStableNetId;
	FisherCharacter = NewCharacter;
	Snapshot.FisherPlayerState = NewFisherController->PlayerState;
	LastSuspendedFisherPlayerState = nullptr;
	FightParticipantIds.Add(NewStableNetId);
	FightParticipantCharacters.Add(NewStableNetId, NewCharacter);
	RefreshFightSummary();
	PublishSnapshot(ECatFishingSnapshotMutation::Discrete);
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_fisher_transferred SessionId=%s Phase=%s Mode=%s OldFisher=%s NewStrength=%.3f NewFightStamina=%.3f %s"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
		*UEnum::GetValueAsString(Snapshot.Phase),
		bFightTakeover ? TEXT("FightRunnerRebind") : TEXT("WaitingIdentityTransfer"),
		*OldFisherLogValue, NewStrength, NewStamina,
		*CatLogContext::BuildControllerFields(NewFisherController));
	return true;
}

// 抄网流程：服务器重建抄手身份和装备事实，鱼上钩后不再读取鱼体力；只要抄手、岸边站位、视线与
// “抄网线段 ∩ 鱼目标圆”都合法，就把水中 Encounter 交接成世界鱼并立即附到抄手嘴上。鱼仍未进入
// Items 容器，之后必须像原来的 E 拾鱼一样，对具体地面鱼护交互才发生唯一容器提交。
FCatScoopResult ACatFishingSession::RequestScoop(AController* ScoopingController, const FCatScoopCommand& Command)
{
	FCatScoopResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	// 从 Controller 重新解析服务器私有身份，客户端提交的任何身份字段都不可信。
	const FString StableNetId = ResolveStableNetId(ScoopingController);
	FString ValidatedScooperId;
	ACatCharacter* ScoopingCharacter = nullptr;
	double ScooperFishingStrength = 0.0;
	double ScooperFightStamina = 0.0;
	// 抄手本人也必须满足统一的参战能力谓词（Active、未倒地、力量/体力为正），且谓词内部解析出的身份要与外部一致。
	const bool bScooperFightCapable = UCatFishingService::TryGetFightCapability(ScoopingController,
		ValidatedScooperId, ScoopingCharacter, ScooperFishingStrength, ScooperFightStamina)
		&& ValidatedScooperId == StableNetId;
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	UCatWaterQuerySubsystem* Water = GetWorld() ? GetWorld()->GetSubsystem<UCatWaterQuerySubsystem>() : nullptr;
	ACatFishEncounterActor* Encounter = Snapshot.FishEncounterActor;
	UCatEquipmentComponent* ScooperEquipment = ScoopingCharacter ? ScoopingCharacter->GetEquipmentComponent() : nullptr;
	double ScoopReachCentimeters = 0.0;
	// 全局设置和服务器当前装备快照中的已选抄网 DA 共同给出有效距离；当前流程不提供默认获取，未装备时解析保持失败。
	const bool bScoopReachReady = UCatFishingAimLibrary::TryResolveScoopReach(
		ScooperEquipment, ScoopReachCentimeters);
	// 这里不再要求"鱼处于近岸带内"：射线∩圆本身就是唯一的范围判定，再叠一层离岸距离等于两套口径，
	// 会出现"圈画成绿色（够得着）但服务器因为鱼离岸 3.1 米而拒绝"这种表现与判定打架的情况。
	// 几何上也已经蕴含：抄手必须站在岸上，射线长度有限，所以能被抄到的鱼必然离岸不远。
	// 抄手自己相对岸线的空间关系：抢抄要求抄手站在岸上（Outside 水域），不能站在水里抄。
	const FVector ScooperLocation = ScoopingCharacter ? ScoopingCharacter->GetActorLocation() : FVector::ZeroVector;
	const float CapsuleHalfHeight = ScoopingCharacter && ScoopingCharacter->GetCapsuleComponent()
		? ScoopingCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.0f;
	const FVector CapsuleFootLocation = ScooperLocation - FVector(0.0, 0.0, CapsuleHalfHeight);
	const FCatWaterSpatialResult ScooperSpatial = Water && ScoopingCharacter && AttemptSnapshot.WaterRegion.IsValid()
		? Water->QueryShoreRelation(ScooperLocation, AttemptSnapshot.WaterRegion)
		: FCatWaterSpatialResult{};
	// 足底与地面点只用于诊断，不参与当前抄网策略。它们能直接区分“角色中心高度超差”与“脚下实际位于水域内”。
	const FCatWaterSpatialResult CapsuleFootSpatial = Water && ScoopingCharacter && AttemptSnapshot.WaterRegion.IsValid()
		? Water->QueryShoreRelation(CapsuleFootLocation, AttemptSnapshot.WaterRegion)
		: FCatWaterSpatialResult{};
	// 抄网范围口径（与 debug 绘制同源）：沿抄手 Character 的正前方水平发射一条线段，与挂在鱼身上的圆相交即够得着。
	// 圆心随鱼移动、半径由鱼定义给（这条鱼有多好捞），线段长度由统一有效距离给出，两者互不耦合。
	// 这是身体动作而非镜头瞄准动作；自由转动 Camera/Controller 不得改变抄网判定方向。
	const FVector ScooperFacing = UCatFishingAimLibrary::ResolveScoopFacingHorizontal(ScoopingCharacter);
	FHitResult GroundHit;
	bool bValidGround = false;
	bool bHasLineOfSight = false;
	if (Settings && ScoopingCharacter && Encounter && GetWorld())
	{
		// 地面校验：从抄手头顶向下打一条射线，命中点法线倾角必须不超过配置的最大坡度，
		// 防止在陡坡/悬崖边缘的非法站位也能完成抢抄。
		FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(CatScoopGround), false, ScoopingCharacter);
		bValidGround = GetWorld()->LineTraceSingleByChannel(GroundHit,
			ScoopingCharacter->GetActorLocation() + FVector(0, 0, 75),
			ScoopingCharacter->GetActorLocation() - FVector(0, 0, 250), Settings->ScoopTraceChannel, GroundParams)
			&& GroundHit.ImpactNormal.Z >= FMath::Cos(FMath::DegreesToRadians(Settings->MaximumScoopGroundSlopeDegrees));
		// 视线校验：抄手视点到鱼的连线中间不能被遮挡（忽略鱼自身），防止隔墙/隔物抢抄。
		FCollisionQueryParams SightParams(SCENE_QUERY_STAT(CatScoopLineOfSight), true, ScoopingCharacter);
		SightParams.AddIgnoredActor(Encounter);
		bHasLineOfSight = !GetWorld()->LineTraceTestByChannel(ScoopingCharacter->GetPawnViewLocation(),
			Encounter->GetActorLocation(), Settings->ScoopTraceChannel, SightParams);
	}
	const FVector GroundQueryLocation = GroundHit.bBlockingHit ? GroundHit.ImpactPoint : CapsuleFootLocation;
	const FCatWaterSpatialResult GroundSpatial = Water && ScoopingCharacter && AttemptSnapshot.WaterRegion.IsValid()
		? Water->QueryShoreRelation(GroundQueryLocation, AttemptSnapshot.WaterRegion)
		: FCatWaterSpatialResult{};
	const double FishRadius = FishDefinition ? FishDefinition->ScoopTargetRadiusCentimeters : 0.0;
	const FVector FishLocation = Encounter ? Encounter->GetActorLocation() : FVector::ZeroVector;
	const bool bMouthFree = ScoopingCharacter && !ACatFishPickupActor::FindCarriedFish(ScoopingCharacter);
	const bool bRayReachesFish = bScoopReachReady && ScoopingCharacter && Settings && Encounter && FishRadius > 0.0
		&& UCatFishingAimLibrary::DoesScoopRayReachFish(ScooperLocation, ScooperFacing,
			static_cast<float>(ScoopReachCentimeters), FishLocation, static_cast<float>(FishRadius),
			static_cast<float>(Settings->MaximumScoopVerticalDeltaCentimeters));
	// 终态缓存键=身份+RequestId：同一玩家的网络重放绝不生成第二条世界鱼。
	const FString CacheKey = FString::Printf(TEXT("%s|%s"), *StableNetId,
		*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	if (const FCatScoopResult* Cached = ScoopTerminalCache.Find(CacheKey))
	{
		// 命中缓存直接回放，且显式标记本次不是新提交，避免调用方误以为又成功抢到了一次。
		Result = *Cached;
		Result.Command.bCommitted = false;
		Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_scoop_replay SessionId=%s RequestId=%s CachedCommitted=%s CachedError=%s %s"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			Cached->Command.bCommitted ? TEXT("true") : TEXT("false"),
			*UEnum::GetValueAsString(Cached->Command.Error),
			*CatLogContext::BuildControllerFields(ScoopingController));
		return Result;
	}
	if (bCaptureResolved || IsTerminal())
	{
		// Encounter 已经交接给某个世界鱼，或会话已由其他终局收敛。
		Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
	}
	else if (!Command.Context.RequestId.IsValid() || StableNetId.IsEmpty())
	{
		// 抄网不接收容器目标；请求 ID 与服务器身份是唯一载荷身份要求。
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (Snapshot.Phase != ECatFishingPhase::HookedFight && Snapshot.Phase != ECatFishingPhase::NearShore
		&& Snapshot.Phase != ECatFishingPhase::ExhaustedReel)
	{
		// 抢抄对 HookedFight 与 NearShore 两个阶段都开放：鱼身上的可捞圆圈一直存在，不是"体力清零才能抄"。
		// 搏斗中只要把鱼收到射线够得着的位置就能直接抄上来——这是高风险高回报的主动选择（提前结束搏斗、
		// 也给多人抢抄留出更长的窗口），而不是等待鱼翻肚后的收尾操作。
		// 更早的阶段（Waiting/Probe/TrueBiteWindow）不开放：那时鱼还没被提上钩，抄它会绕过整个提竿机制。
		Result.Command.Error = ECatDomainCommandError::InvalidPhase;
	}
	else if (Command.Context.ExpectedRevision != Snapshot.Revision)
	{
		// 客户端携带的 ExpectedRevision 必须匹配当前 Session Revision，拒绝基于过期快照的抢抄。
		Result.Command.Error = ECatDomainCommandError::RevisionConflict;
	}
	else if (!FishDefinition || !Water || !Encounter || !AttemptSnapshot.WaterRegion.IsValid())
	{
		// 核心依赖缺失（鱼定义/水域子系统/鱼 Actor/水域句柄）是不可恢复的系统性故障，
		// 不只是拒绝这次请求，而是直接把整个会话判为失效并终止，避免留下无法继续推进的僵死会话。
		Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Invalidated,
			TEXT("Scoop system dependency unavailable"));
	}
	else if (!bScooperFightCapable || !ScoopingCharacter || !Settings
		|| !ScoopingController->PlayerState || !bMouthFree
		|| !bScoopReachReady
		|| FishDefinition->ScoopTargetRadiusCentimeters <= 0.0
		|| !ScooperSpatial.bSucceeded || ScooperSpatial.Containment != ECatWaterContainment::Outside
		|| !bRayReachesFish
		|| !bHasLineOfSight || !bValidGround)
	{
		// 汇总校验：抄手战斗能力/角色有效性/嘴上无鱼/基础抄网距离/鱼的可捞半径已裁/抄手在岸上/
		// 射线够到鱼圈/视线通畅/地面合法 —— 任一条件不满足都统一判为 PolicyUndecided（策略未满足）拒绝。
		Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
		// 逐项列出失败谓词：抢抄拒绝原因众多且此前完全静默，排查成本太高。
		// 额外打出水平距离与高度差的实测值：RayReachesFish=0 时光看谓词分不清是"没对准"、"太远"还是"站太高"。
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=scoop_rejected SessionId=%s RequestId=%s Phase=%s ExpectedRevision=%lld ActualRevision=%lld "
				"FightCapable=%d Character=%d MouthFree=%d ScoopReachReady=%d "
				"FishRadiusSet=%d ScooperOnLand=%d RayReachesFish=%d LineOfSight=%d ValidGround=%d "
				"GroundTraceHit=%s GroundImpact=%s GroundNormal=%s ScoopFacingSource=CharacterActorForward ScooperFacing=%s FishLocation=%s "
				"HorizontalDistanceCm=%.1f VerticalDeltaCm=%.1f ReachCm=%.1f RadiusCm=%.1f %s %s %s %s"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			*UEnum::GetValueAsString(Snapshot.Phase),
			Command.Context.ExpectedRevision, Snapshot.Revision,
			bScooperFightCapable ? 1 : 0, ScoopingCharacter ? 1 : 0,
			bMouthFree ? 1 : 0,
			bScoopReachReady ? 1 : 0,
			FishRadius > 0.0 ? 1 : 0,
			ScooperSpatial.bSucceeded && ScooperSpatial.Containment == ECatWaterContainment::Outside ? 1 : 0,
			bRayReachesFish ? 1 : 0,
			bHasLineOfSight ? 1 : 0, bValidGround ? 1 : 0,
			GroundHit.bBlockingHit ? TEXT("true") : TEXT("false"),
			*GroundHit.ImpactPoint.ToCompactString(), *GroundHit.ImpactNormal.ToCompactString(),
			*ScooperFacing.ToCompactString(), *FishLocation.ToCompactString(),
			FVector::Dist2D(ScooperLocation, FishLocation), FMath::Abs(FishLocation.Z - ScooperLocation.Z),
			bScoopReachReady ? ScoopReachCentimeters : 0.0,
			FishRadius,
			*CatLogContext::BuildControllerFields(ScoopingController),
			*CatLogContext::BuildWaterSpatialFields(TEXT("CenterWater"), ScooperLocation, ScooperSpatial),
			*CatLogContext::BuildWaterSpatialFields(TEXT("FootWater"), CapsuleFootLocation, CapsuleFootSpatial),
			*CatLogContext::BuildWaterSpatialFields(TEXT("GroundWater"), GroundQueryLocation, GroundSpatial));
	}
	else
	{
		// 饵料只在鱼即将离开水中会话时确认消耗；失败必须终止，避免世界鱼与装备预留事实分叉。
		if (!CommitCatchEquipmentFromAuthority())
		{
			Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
			FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Invalidated,
				TEXT("Scoop equipment finalization failed"));
		}
		else if (!SpawnScoopedFishPickupFromAuthority(
			ScoopingCharacter, ScoopingController->PlayerState, StableNetId))
		{
			Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
			FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Invalidated,
				TEXT("Scoop world-fish handoff failed"));
		}
		else
		{
			Result.Command.bCommitted = true;
			Result.Command.Error = ECatDomainCommandError::None;
		}
	}
	Result.Command.Revision = Snapshot.Revision;
	ScoopTerminalCache.Add(CacheKey, Result); // 无论成功失败都写入终态缓存，保证后续重放幂等。
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_scoop_terminal SessionId=%s RequestId=%s Committed=%s Error=%s Revision=%lld "
			"Phase=%s FishLocation=%s ScoopFacingSource=CharacterActorForward ScooperFacing=%s GroundTraceHit=%s ValidGround=%s LineOfSight=%s "
			"RayReachesFish=%s %s %s %s %s"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
		*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		Result.Command.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Command.Error),
		Snapshot.Revision, *UEnum::GetValueAsString(Snapshot.Phase), *FishLocation.ToCompactString(),
		*ScooperFacing.ToCompactString(), GroundHit.bBlockingHit ? TEXT("true") : TEXT("false"),
		bValidGround ? TEXT("true") : TEXT("false"), bHasLineOfSight ? TEXT("true") : TEXT("false"),
		bRayReachesFish ? TEXT("true") : TEXT("false"), *CatLogContext::BuildControllerFields(ScoopingController),
		*CatLogContext::BuildWaterSpatialFields(TEXT("CenterWater"), ScooperLocation, ScooperSpatial),
		*CatLogContext::BuildWaterSpatialFields(TEXT("FootWater"), CapsuleFootLocation, CapsuleFootSpatial),
		*CatLogContext::BuildWaterSpatialFields(TEXT("GroundWater"), GroundQueryLocation, GroundSpatial));
	return Result;
}

// 会话终止流程：非 authority、已 Resolved 或已 Terminated 直接幂等返回，避免覆盖捕获终态。首次中断只写一次 Terminated/Revision 并发布快照，再停止 StateTree、释放钓手之外的参与弱引用且不触碰 Items；最后启动配置的有界复制窗口，让客户端看见终态后销毁 Actor，服务据此清理会话弱索引。
void ACatFishingSession::TerminateSession(const ECatFishingOutcome Outcome, const TCHAR* DiagnosticReason)
{
	// 只白名单允许"非捕获类"终止结果才真正写终态；Caught/None 等结果不属于这条路径
	// （捕获成功走世界鱼嘴叼交接 -> FinalizeSession(Resolved, Caught) 那条独立路径），
	// 防止调用方误用本函数覆盖掉已经成立的捕获终态。
	switch (Outcome)
	{
	case ECatFishingOutcome::EmptyHook:
	case ECatFishingOutcome::HookWindowExpired:
	case ECatFishingOutcome::Escaped:
	case ECatFishingOutcome::RodBroken:
	case ECatFishingOutcome::LineBroken:
	case ECatFishingOutcome::LineCut:
	case ECatFishingOutcome::CatInWater:
	case ECatFishingOutcome::Cancelled:
	case ECatFishingOutcome::Invalidated:
		FinalizeSession(ECatFishingPhase::Terminated, Outcome, DiagnosticReason);
		return;
	default:
		return;
	}
}

bool ACatFishingSession::PrepareSessionFromAuthority(const FCatFishingAttemptSnapshot& Attempt,
	AController* FisherController, ACatCharacter* InFisherCharacter, ACatFishingHookActor* HookActor)
{
	const FString StableNetId = ResolveStableNetId(FisherController);
	// bPrepared 只允许设置一次：本函数是两阶段提交（Prepare -> Start/Publish 或 Abort）事务的第一阶段，
	// 重复调用或任何一项前置校验失败都直接拒绝，不留半准备状态。
	if (!HasAuthority() || bPrepared || !Attempt.RequestId.IsValid() || !Attempt.FishingSessionId.IsValid()
		|| !Attempt.CastAttemptId.IsValid() || Attempt.FishingSessionId == Attempt.CastAttemptId
		|| !Attempt.WaterRegion.IsValid() || !Attempt.RodActor || !HookActor || !InFisherCharacter
		|| !FisherController || !FisherController->PlayerState || StableNetId.IsEmpty())
	{
		return false;
	}
	Snapshot = FCatFishingSessionSnapshot{}; // 重置为默认值，确保没有上一次失败准备遗留的脏字段。
	Snapshot.FishingSessionId = Attempt.FishingSessionId;
	Snapshot.CastAttemptId = Attempt.CastAttemptId;
	Snapshot.Revision = 1;
	Snapshot.PhaseEpoch = 1;
	Snapshot.Phase = ECatFishingPhase::Created;
	Snapshot.FisherPlayerState = FisherController->PlayerState;
	Snapshot.RodActor = Attempt.RodActor;
	Snapshot.HookActor = HookActor;
	AttemptSnapshot = Attempt;
	// Fish identity remains deliberately empty until a valid left-click commits the hook inside TrueBiteWindow.
	FisherCharacter = InFisherCharacter;
	CastEquipment = InFisherCharacter->GetEquipmentComponent(); // 冻结原始抛竿者装备：饵料/磨损结算口径不随接力改变。
	FisherStableNetId = StableNetId;
	FightParticipantIds.Add(StableNetId);
	FightParticipantCharacters.Add(StableNetId, InFisherCharacter);
	ItemsService = GetWorld() ? GetWorld()->GetSubsystem<UCatItemsService>() : nullptr;
	// 捕获物会在力竭回收后生成世界鱼；抛竿准备只要求 Items 服务存在，不绑定或搜索任何鱼护。
	bPrepared = ItemsService.IsValid();
	return bPrepared;
}

bool ACatFishingSession::ScheduleWaitingProbeFromStateTree()
{
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	double BiteWarningSeconds = 0.0;
	if (!HasAuthority() || !bPrepared || IsTerminal() || !Settings
		|| !FMath::IsFinite(Settings->BaseBiteRatePerSecond) || Settings->BaseBiteRatePerSecond <= 0.0
		|| !FMath::IsFinite(Settings->MinimumBiteDelaySeconds) || Settings->MinimumBiteDelaySeconds < 0.0
		|| !FMath::IsFinite(Settings->MaximumBiteDelaySeconds)
		|| Settings->MaximumBiteDelaySeconds < Settings->MinimumBiteDelaySeconds
		|| !Settings->TryGetBiteWarning(BiteWarningSeconds))
	{
		return false;
	}
	// Waiting 可以由“首次抛竿”或“上一轮真咬窗口漏按”进入。漏按不会释放鱼竿/鱼线/饵料预约，
	// 这里只清理尚未确认的咬钩机会；若已有鱼 Actor，说明错误地试图把已确认搏斗倒回 Waiting，拒绝重入。
	if (Snapshot.FishEncounterActor || FishDefinition || SelectionResolution == ECatFishSelectionResolution::Selected)
	{
		return false;
	}
	GetWorldTimerManager().ClearTimer(TrueBiteTimerHandle);
	bTrueBiteWindowAcceptingHook = false;
	SelectionResolution = ECatFishSelectionResolution::None;
	FrozenSelectionContext = FCatFishSelectionContext{};
	FrozenSelectionResult = FCatFishSelectionResult{};
	FishWeightKilograms = 0.0;
	FishVisualScale = 1.0;
	Snapshot.WindowEndsServerTime = 0.0;
	Snapshot.FishDefinitionId = NAME_None;
	Snapshot.FishWeightKilograms = 0.0;
	Snapshot.FishStrength = 0.0;
	Snapshot.bGiant = false;
	Snapshot.FishFightStaminaRemaining = 0.0;
	Snapshot.NormalizedFishStamina = 0.0;
	Snapshot.bPerfectHook = false;
	Snapshot.FishMotionIntent = ECatFishMotionIntent::None;
	Snapshot.FishLineAlignment = 0.0f;
	Snapshot.NormalizedLineLoad = 0.0f;
	Snapshot.bStrongConfrontation = false;

	// 每个咬钩机会使用独立、但可由服务器抛竿种子重放的随机流。否则窗口漏按后，
	// 下一轮会重复完全相同的等待时长，并在最终点击时固定抽到同一条鱼。
	++BiteOpportunitySequence;
	if (BiteOpportunitySequence == 0) ++BiteOpportunitySequence; // 极端溢出时仍保留 0 作为“尚未初始化”。
	const uint32 BaseSeed = GetTypeHash(AttemptSnapshot.ServerRandomSeed != 0
		? AttemptSnapshot.ServerRandomSeed : static_cast<uint64>(GetTypeHash(Snapshot.FishingSessionId)));
	uint32 DerivedSeed = HashCombineFast(BaseSeed, BiteOpportunitySequence);
	if (DerivedSeed == 0) DerivedSeed = BiteOpportunitySequence;
	CurrentBiteRandomSeed = static_cast<uint64>(DerivedSeed);
	if (Snapshot.Phase != ECatFishingPhase::Waiting)
	{
		// 只在尚未进入 Waiting 时才写一次阶段；重复调度（如 StateTree 重入）不重复写阶段事件。
		if (!EnterPhaseFromStateTree(ECatFishingPhase::Waiting).bApplied) return false;
	}
	double BiteRate = Settings->BaseBiteRatePerSecond;
	double MinimumDelay = Settings->MinimumBiteDelaySeconds;
	// 鱼饵按其配置的倍率修正基础上钩率与最小延迟。
	if (const UCatEquipmentDefinition* Bait = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(AttemptSnapshot.BaitDefinitionId))
	{
		BiteRate *= Bait->BiteRateMultiplier;
		MinimumDelay *= Bait->MinimumBiteDelayMultiplier;
	}
	// 打窝效果在此处生效：采样钩子当前位置的窝料浓度（鱼腥/香/发酵三个维度求和），
	// 浓度越高上钩率提升越多，但用 1-e^-x 做饱和曲线，避免无限堆窝料导致上钩率失控。
	if (UCatChumFieldSubsystem* Chum = GetWorld()->GetSubsystem<UCatChumFieldSubsystem>())
	{
		const FCatChumSample Sample = Chum->SampleChumAtPoint(Snapshot.HookActor->GetActorLocation(),
			AttemptSnapshot.WaterRegion, GetWorld()->GetTimeSeconds());
		const double TotalChum = Sample.EffectiveChumVector.Fishy + Sample.EffectiveChumVector.Fragrant
			+ Sample.EffectiveChumVector.Fermented;
		if (Sample.bSucceeded && FMath::IsFinite(TotalChum) && TotalChum > 0.0)
		{
			BiteRate *= 1.0 + (1.0 - FMath::Exp(-TotalChum));
		}
	}
	if (!FMath::IsFinite(BiteRate) || BiteRate <= 0.0 || MinimumDelay > Settings->MaximumBiteDelaySeconds) return false;
	// 用服务器种子生成确定性随机数，再按泊松过程的逆变换采样法算出额外的安静等待时间。
	// MinimumDelay 是浮漂必须慢浮的下限，MaximumBiteDelaySeconds 仍是从落水到真咬的总时间上限。
	FRandomStream Random(static_cast<int32>(CurrentBiteRandomSeed));
	const double Unit = FMath::Clamp(static_cast<double>(Random.FRand()), UE_DOUBLE_SMALL_NUMBER, 1.0 - UE_DOUBLE_SMALL_NUMBER);
	const double SampledAdditionalCalmDelay = -FMath::Loge(1.0 - Unit) / BiteRate;
	const double MaximumAdditionalCalmDelay = Settings->MaximumBiteDelaySeconds - BiteWarningSeconds - MinimumDelay;
	if (!FMath::IsFinite(MaximumAdditionalCalmDelay) || MaximumAdditionalCalmDelay < 0.0) return false;
	const double WarningDelay = MinimumDelay + FMath::Min(SampledAdditionalCalmDelay, MaximumAdditionalCalmDelay);
	const double Delay = WarningDelay + BiteWarningSeconds;
	GetWorldTimerManager().ClearTimer(BiteWarningTimerHandle);
	GetWorldTimerManager().ClearTimer(ProbeTimerHandle);
	if (WarningDelay <= UE_DOUBLE_SMALL_NUMBER)
	{
		HandleBiteWarningTimer();
	}
	else
	{
		GetWorldTimerManager().SetTimer(BiteWarningTimerHandle, this,
			&ThisClass::HandleBiteWarningTimer, WarningDelay, false);
	}
	GetWorldTimerManager().SetTimer(ProbeTimerHandle, this, &ThisClass::HandleProbeTimer, Delay, false);
	return true;
}

void ACatFishingSession::HandleBiteWarningTimer()
{
	// 预警只改变 Hook 的复制表现模式；公开阶段仍是 Waiting，提前提竿继续按空钩裁决。
	if (!HasAuthority() || IsTerminal() || Snapshot.Phase != ECatFishingPhase::Waiting || !Snapshot.HookActor)
	{
		return;
	}
	Snapshot.HookActor->SetBobberPresentationModeFromAuthority(ECatFishingBobberPresentationMode::BiteWarning);
}

void ACatFishingSession::HandleProbeTimer()
{
	// 计时器到期：只有仍处于 Waiting 阶段才把"试探触发"事件送进 StateTree，
	// 阶段已经变化（比如提前被取消/提竿）则说明这次触发已经过期，直接忽略。
	if (!HasAuthority() || IsTerminal() || Snapshot.Phase != ECatFishingPhase::Waiting || !StateTreeComponent) return;
	StateTreeComponent->SendStateTreeEvent(CatFishingGameplayTags::ProbeTriggered, FConstStructView(), TEXT("CatFishing"));
}

bool ACatFishingSession::OpenTrueBiteWindowFromStateTree()
{
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	UWorld* World = GetWorld();
	if (!HasAuthority() || IsTerminal() || Snapshot.Phase != ECatFishingPhase::Probe || !Settings || !World
		|| !Snapshot.HookActor || SelectionResolution != ECatFishSelectionResolution::None
		|| Snapshot.FishEncounterActor || FishDefinition
		|| !FMath::IsFinite(Settings->TrueBiteWindowSeconds) || Settings->TrueBiteWindowSeconds <= 0.0)
	{
		return false;
	}

	// 此刻只发布“真咬信号”：鱼仍未被选择、未生成，饵料也仍处于抛竿时建立的预约状态。
	// WindowEnds 必须在 EnterPhase 发布快照前写好，客户端第一次看到 TrueBiteWindow 时截止时间就是完整的。
	const double PreviousWindowEnd = Snapshot.WindowEndsServerTime;
	Snapshot.WindowEndsServerTime = World->GetTimeSeconds() + Settings->TrueBiteWindowSeconds;
	if (!EnterPhaseFromStateTree(ECatFishingPhase::TrueBiteWindow).bApplied)
	{
		Snapshot.WindowEndsServerTime = PreviousWindowEnd;
		return false;
	}
	bTrueBiteWindowAcceptingHook = true;
	GetWorldTimerManager().ClearTimer(TrueBiteTimerHandle);
	GetWorldTimerManager().SetTimer(TrueBiteTimerHandle, this, &ThisClass::HandleTrueBiteWindowExpired,
		Settings->TrueBiteWindowSeconds, false);
	return true;
}

FCatFishSelectionCommitResult ACatFishingSession::ResolveHookSelectionFromAuthority()
{
	FCatFishSelectionCommitResult Result;
	Result.Resolution = SelectionResolution;
	Result.FishDefinitionId = FrozenSelectionResult.FishDefinitionId;
	if (SelectionResolution == ECatFishSelectionResolution::Selected)
	{
		// 已经选出鱼种：直接幂等返回缓存的结果，不重新选择。
		Result.Error = ECatDomainCommandError::None;
		return Result;
	}
	if (SelectionResolution == ECatFishSelectionResolution::NoEligibleFish
		|| SelectionResolution == ECatFishSelectionResolution::Failed
		|| SelectionResolution == ECatFishSelectionResolution::InProgress)
	{
		// 已经处于失败/无合格鱼/进行中这几个终态或过渡态，同样直接返回，不重复触发选择流程。
		return Result;
	}
	if (!HasAuthority() || IsTerminal() || Snapshot.Phase != ECatFishingPhase::TrueBiteWindow
		|| !AttemptSnapshot.WaterRegion.IsValid() || !Snapshot.HookActor || !FisherCharacter.IsValid())
	{
		SelectionResolution = ECatFishSelectionResolution::Failed;
		Result.Resolution = SelectionResolution;
		return Result;
	}
	SelectionResolution = ECatFishSelectionResolution::InProgress; // 标记进行中，防止同一帧内被并发重入。
	UWorld* World = GetWorld();
	UCatChumFieldSubsystem* Chum = World ? World->GetSubsystem<UCatChumFieldSubsystem>() : nullptr;
	const ACatfishingGameState* GameState = World ? World->GetGameState<ACatfishingGameState>() : nullptr;
	UCatFishingService* Service = World ? World->GetSubsystem<UCatFishingService>() : nullptr;
	UCatEquipmentComponent* Equipment = CastEquipment.Get(); // 饵料预留在抛竿者装备上，选鱼/消耗确认必须用同一组件。
	if (!Chum || !GameState || !Service || !Equipment)
	{
		SelectionResolution = ECatFishSelectionResolution::Failed;
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Invalidated, TEXT("Selection dependency unavailable"));
		Result.Resolution = SelectionResolution;
		return Result;
	}
	int32 PlayerCount = 0;
	double FishingStrength = 0.0;
	double FightStamina = 0.0;
	Service->BuildFightCapabilitySnapshot(PlayerCount, FishingStrength, FightStamina);
	// 冻结本次选择所依据的全部上下文（水域、窝料采样、时间/天气、饵料、在场玩家战力）：
	// 一旦选出鱼种就不再受这些外部条件后续变化影响，保证结果确定且可复现。
	FrozenSelectionContext = FCatFishSelectionContext{};
	FrozenSelectionContext.WaterRegion = AttemptSnapshot.WaterRegion;
	// 再次采样打窝浓度（与上钩率采样同源），用于影响鱼种选择的品质/稀有度权重。
	FrozenSelectionContext.ChumSample = Chum->SampleChumAtPoint(Snapshot.HookActor->GetActorLocation(),
		AttemptSnapshot.WaterRegion, World->GetTimeSeconds());
	FrozenSelectionContext.TimeOfDay = GameState->GetRunPublicState().Environment.TimeOfDay;
	FrozenSelectionContext.Weather = GameState->GetRunPublicState().Environment.Weather;
	FrozenSelectionContext.BaitDefinitionId = AttemptSnapshot.BaitDefinitionId;
	FrozenSelectionContext.ActivePlayerCount = PlayerCount;
	FrozenSelectionContext.CombinedFishingStrength = FishingStrength;
	FrozenSelectionContext.CombinedFightStamina = FightStamina;
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	const UCatFishingFightBalanceDefinition* FightBalance = Settings
		? Settings->LoadFightBalanceDefinition() : nullptr;
	FrozenSelectionContext.StrengthPerKilogram = FightBalance
		? FightBalance->StrengthPerKilogram : 0.0;
	FrozenSelectionContext.RandomSeed = static_cast<int32>(CurrentBiteRandomSeed);
	const UCatFishCatalogSettings* Catalog = GetDefault<UCatFishCatalogSettings>();
	// 按冻结上下文从鱼类图鉴中选出本次的鱼种（含权重/稀有度/条件判定，具体算法在 Catalog 内部）。
	FrozenSelectionResult = Catalog->SelectRuntimeDefinition(FrozenSelectionContext);
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_fish_selection_resolved SessionId=%s Selected=%s FishId=%s FightBalanceId=%s WeightKg=%.3f BaseFishStrength=%.3f StrengthPerKg=%.3f EligibleCandidates=%d SelectedBandCandidates=%d NormalizedProbability=%.6f TimeFilter=%s WeatherFilter=%s TimeOfDay=%s Weather=%s ActivePlayers=%d ChumFields=%d"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphensLower),
		FrozenSelectionResult.bSelected ? TEXT("true") : TEXT("false"),
		*FrozenSelectionResult.FishDefinitionId.ToString(),
		FightBalance ? *FightBalance->BalanceDefinitionId.ToString() : TEXT("None"),
		FrozenSelectionResult.WeightKilograms, FrozenSelectionResult.BaseFishStrength,
		FrozenSelectionContext.StrengthPerKilogram, FrozenSelectionResult.EligibleCandidateCount,
		FrozenSelectionResult.SelectedBandCandidateCount,
		FrozenSelectionResult.SelectedNormalizedProbability,
		Catalog->bEnableTimeOfDayEligibilityFilter ? TEXT("Enabled") : TEXT("Bypassed"),
		Catalog->bEnableWeatherEligibilityFilter ? TEXT("Enabled") : TEXT("Bypassed"),
		*UEnum::GetValueAsString(FrozenSelectionContext.TimeOfDay),
		*UEnum::GetValueAsString(FrozenSelectionContext.Weather), FrozenSelectionContext.ActivePlayerCount,
		FrozenSelectionContext.ChumSample.ContributingFieldCount);
	UCatFishDefinition* SelectedDefinition = FrozenSelectionResult.bSelected
		? Catalog->FindRuntimeDefinition(FrozenSelectionResult.FishDefinitionId) : nullptr;
	const UCatBitePersonalityDefinition* Bite = SelectedDefinition && Settings
		? Settings->FindBitePersonality(SelectedDefinition->BitePersonalityId) : nullptr;
	const UCatFightPersonalityDefinition* Fight = SelectedDefinition && Settings
		? Settings->FindFightPersonality(SelectedDefinition->FightPersonalityId) : nullptr;
	if (!SelectedDefinition || !Bite || !Fight)
	{
		// 没选出鱼、或性格模板缺失：判为"当前条件下没有合格鱼"，走空军终局而不是异常终止。
		SelectionResolution = ECatFishSelectionResolution::NoEligibleFish;
		Equipment->ReleaseFishingUse(Snapshot.FishingSessionId);
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::EmptyHook, TEXT("No eligible fish"));
		Result.Resolution = SelectionResolution;
		Result.Error = ECatDomainCommandError::None;
		return Result;
	}
	const UCatFishingPresentationSettings* Presentation = GetDefault<UCatFishingPresentationSettings>();
	const UCatFishPresentationDefinition* FishPresentation =
		SelectedDefinition->LoadRuntimePresentationDefinition();
	const double SelectedVisualScale = FishPresentation
		? FishPresentation->ComputeUniformVisualScale(FrozenSelectionResult.WeightKilograms) : 1.0;
	UClass* FishClass = Presentation ? Presentation->FishEncounterActorClass.LoadSynchronous() : nullptr;
	const FVector FishLocation = Snapshot.HookActor->GetActorLocation(); // 鱼在钩子所在位置咬钩。
	// 用 SpawnActorDeferred 而非直接 SpawnActor：需要先设置好初始 Transform/Owner，
	// 再等下面显式调用 InitializeAuthoritativeIdentity 写好权威身份后才 FinishSpawning，
	// 避免构造期蓝图逻辑读到一个身份尚未就绪的鱼 Actor。
	ACatFishEncounterActor* Encounter = FishClass && FishClass->IsChildOf(ACatFishEncounterActor::StaticClass())
		? World->SpawnActorDeferred<ACatFishEncounterActor>(FishClass, FTransform(FishLocation), this,
			FisherCharacter.Get(), ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
	const double InitialLineLength = AttemptSnapshot.RodActor
		? FVector::Distance(AttemptSnapshot.RodActor->GetRodTipWorldTransform().GetLocation(), FishLocation) : 0.0;
	if (!Encounter)
	{
		SelectionResolution = ECatFishSelectionResolution::Failed;
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Invalidated, TEXT("Fish presentation unavailable"));
		Result.Resolution = SelectionResolution;
		return Result;
	}
	// 延迟首次表现通知，直到 PublishInitialPresentationFromAuthority 显式放行（避免构造期蓝图事件过早触发）。
	Encounter->DeferInitialPresentationFromAuthority();
	if (!Encounter->InitializeAuthoritativeIdentity(Snapshot.FishingSessionId, Snapshot.CastAttemptId,
		SelectedDefinition->FishDefinitionId, InitialLineLength, SelectedVisualScale))
	{
		Encounter->Destroy();
		SelectionResolution = ECatFishSelectionResolution::Failed;
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Invalidated, TEXT("Fish identity failed"));
		Result.Resolution = SelectionResolution;
		return Result;
	}
	Encounter->FinishSpawning(FTransform(FishLocation));
	const FCatFishEncounterPresentationState& EncounterState = Encounter->GetPresentationState();
	// FinishSpawning 之后再校验一遍身份/位置是否仍与预期一致：防止构造期蓝图逻辑（BeginPlay 等）
	// 篡改了权威状态，一旦发现不一致就整体判失败，绝不带着被污染的鱼 Actor 继续往下走。
	if (!IsValid(Encounter) || EncounterState.FishingSessionId != Snapshot.FishingSessionId
		|| EncounterState.CastAttemptId != Snapshot.CastAttemptId
		|| EncounterState.FishDefinitionId != SelectedDefinition->FishDefinitionId
		|| !FMath::IsNearlyEqual(EncounterState.VisualScale, SelectedVisualScale)
		|| !Encounter->GetActorLocation().Equals(FishLocation, 1.0))
	{
		if (IsValid(Encounter)) Encounter->Destroy();
		SelectionResolution = ECatFishSelectionResolution::Failed;
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Invalidated, TEXT("Fish construction changed authority state"));
		Result.Resolution = SelectionResolution;
		return Result;
	}
	// 到这里才确认消耗 Begin 已经暂存的饵料；失败就销毁鱼并终止，不留下"鱼已生成但饵未结算"的不一致状态。
	const FCatFishingUseOperationResult BaitCommit = Equipment->CommitFishingBaitDeferred(Snapshot.FishingSessionId);
	if (!BaitCommit.bApplied)
	{
		Encounter->Destroy();
		SelectionResolution = ECatFishSelectionResolution::Failed;
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Invalidated, TEXT("Bait commit failed"));
		Result.Resolution = SelectionResolution;
		return Result;
	}
	FishDefinition = SelectedDefinition;
	FishWeightKilograms = FrozenSelectionResult.WeightKilograms;
	FishVisualScale = SelectedVisualScale;
	Snapshot.FishDefinitionId = SelectedDefinition->FishDefinitionId;
	Snapshot.FishWeightKilograms = FrozenSelectionResult.WeightKilograms;
	Snapshot.FishStrength = FrozenSelectionResult.BaseFishStrength;
	Snapshot.bGiant = SelectedDefinition->BodyClass == ECatFishBodyClass::Giant;
	Snapshot.FishFightStaminaRemaining = SelectedDefinition->FishFightStamina;
	Snapshot.NormalizedFishStamina = 1.0;
	Snapshot.FishEncounterActor = Encounter;
	SelectionResolution = ECatFishSelectionResolution::Selected;
	Result.Resolution = SelectionResolution;
	Result.FishDefinitionId = SelectedDefinition->FishDefinitionId;
	Result.Error = ECatDomainCommandError::None;
	return Result;
}

void ACatFishingSession::HandleTrueBiteWindowExpired()
{
	// 漏按只结束当前“咬钩机会”，不结束整次架杆会话。StateTree 收到事件后从 Probe 叶子回到 Waiting，
	// Waiting 的调度 Task 会清空窗口表现、派生下一轮随机种子并重新开始慢浮/预警计时。
	if (HasAuthority() && !IsTerminal() && Snapshot.Phase == ECatFishingPhase::TrueBiteWindow)
	{
		bTrueBiteWindowAcceptingHook = false;
		if (StateTreeComponent) StateTreeComponent->SendStateTreeEvent(CatFishingGameplayTags::WindowExpired,
			FConstStructView(), TEXT("CatFishing"));
		UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_bite_opportunity_expired SessionId=%s Opportunity=%u"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), BiteOpportunitySequence);
	}
}

bool ACatFishingSession::TryEnterHookedFightFromAuthority()
{
	// Runner 已在跑：幂等返回"当前是否确实处于 HookedFight"，不重复初始化搏斗。
	if (FightRunner && FightRunner->IsRunning()) return Snapshot.Phase == ECatFishingPhase::HookedFight;
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	const UCatFishingFightBalanceDefinition* FightBalance = Settings
		? Settings->LoadFightBalanceDefinition() : nullptr;
	const UCatFightPersonalityDefinition* Personality = FishDefinition && Settings
		? Settings->FindFightPersonality(FishDefinition->FightPersonalityId) : nullptr;
	UStateTree* FishBehaviorStateTree = Settings ? Settings->FishBehaviorStateTree.LoadSynchronous() : nullptr;
	const UCatEquipmentDefinition* RodDefinition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(
		AttemptSnapshot.RodDefinitionId);
	UCatEquipmentComponent* Equipment = CastEquipment.Get(); // 钓鱼用途/饵料预留始终属于原始抛竿者，接力不改变结算对象。
	UCatAbilitySystemComponent* AbilitySystem = FisherCharacter.IsValid()
		? FisherCharacter->GetCatAbilitySystemComponent() : nullptr;
	ACatFishEncounterActor* Encounter = Snapshot.FishEncounterActor;
	ACatFishingRodActor* Rod = AttemptSnapshot.RodActor;
	UCatWaterQuerySubsystem* Water = GetWorld() ? GetWorld()->GetSubsystem<UCatWaterQuerySubsystem>() : nullptr;
	// 一次性 fail-closed 校验所有搏斗启动前置依赖：阶段必须是 TrueBiteWindow、鱼种已选定、
	// 性格/鱼竿定义齐全且就绪、钓鱼用途处于激活态、ASC/鱼/竿/水域子系统全部有效。
	if (!HasAuthority() || IsTerminal() || Snapshot.Phase != ECatFishingPhase::TrueBiteWindow
		|| SelectionResolution != ECatFishSelectionResolution::Selected || !Settings || !FightBalance || !Personality
		|| !FishBehaviorStateTree
		|| !Personality->IsRuntimeDefinitionReady() || !RodDefinition || !RodDefinition->IsRuntimeDefinitionReady()
		|| !Equipment || !Equipment->IsFishingUseActive(Snapshot.FishingSessionId) || !AbilitySystem
		|| !Encounter || !Rod || !Water || !AttemptSnapshot.WaterRegion.IsValid()
		|| !FMath::IsNearlyEqual(FrozenSelectionContext.StrengthPerKilogram,
			FightBalance->StrengthPerKilogram))
	{
		return false;
	}

	// 完美中鱼（规格 4.1）：鱼力量/体力/初始线长按性格模板折减；Bite 模板缺失时视为普通提竿。
	const UCatBitePersonalityDefinition* Bite = Settings->FindBitePersonality(FishDefinition->BitePersonalityId);
	const bool bPerfect = Snapshot.bPerfectHook && Bite && Bite->IsRuntimeDefinitionReady();
	const double FishStrengthScale = bPerfect ? Bite->PerfectFishStrengthMultiplier : 1.0;
	const double FishStaminaScale = bPerfect ? Bite->PerfectFishStaminaMultiplier : 1.0;
	const double LineLengthScale = bPerfect ? Bite->PerfectInitialLineLengthMultiplier : 1.0;
	if (!AbilitySystem->InitializeFishingStaminaForSession()) return false; // 初始化搏斗体力属性（非幂等重复调用是安全的）。
	float CatStaminaBaseline = 0.0f;
	// 猫的体力上限按其角色定义查表，而不是写死常量。
	GetDefault<UCatAbilitySettings>()->TryGetFightStaminaBaselineForCharacter(
		FisherCharacter->GetCatDefinitionId(), CatStaminaBaseline);

	// 三方力量、实际鱼重与猫的等效系统质量在此冻结；Runner 每步只刷新参与者输入、力量和接入约束的猫数。
	// （含完美折减）；钓组承载 = 鱼竿定义 FishingStrength（静态）。这里先保存主位基础力量供初始化校验，
	// Runner 启动后会从 OperatorPlayerStates 逐步建立每位参与者的实时贡献。
	// 下面把服务器设置、鱼竿/鱼定义、性格模板的各项参数一次性打包进模拟配置结构体，交给 FightRunner/Simulator 使用。
	FCatFightSimulationConfig Config;
	Config.FixedStepSeconds = Settings->FixedFightStepSeconds; // 固定步长模拟，保证服务器权威结果确定可复现。
	Config.PrimaryOperatorCatStrength = AbilitySystem->GetNumericAttribute(
		UCatSurvivalAttributeSet::GetFishingStrengthAttribute());
	// 辅助位合力不能在会话启动瞬间静态冻结；Runner 每个固定步从鱼竿操作位重建并覆盖此合计。
	Config.SecondCatStrength = 0.0;
	// 猫和鱼共用“质量 × 系数 = 基础力量”口径；CharacterMovement 的 Mass 只属于引擎推挤，不进入玩法公式。
	Config.PrimaryOperatorMassKilograms = Config.PrimaryOperatorCatStrength
		/ FightBalance->StrengthPerKilogram;
	Config.HelperMassKilograms = 0.0;
	Config.FishMassKilograms = FishWeightKilograms;
	Config.FishStrength = FrozenSelectionResult.BaseFishStrength * FishStrengthScale;
	Config.StrengthPerKilogram = FightBalance->StrengthPerKilogram;
	Config.AccelerationPerStrength = FightBalance->AccelerationPerStrength;
	Config.DriveResponseSeconds = FightBalance->DriveResponseSeconds;
	Snapshot.FishStrength = Config.FishStrength;
	Config.RodStrength = RodDefinition->FishingStrength;
	Config.RodPhysicsLengthCentimeters = RodDefinition->RodPhysicsLengthCentimeters;
	Config.CatStaminaMaximum = CatStaminaBaseline;
	Config.CatStaminaCostPerStrengthCentimeter = FightBalance->CatStaminaCostPerStrengthCentimeter;
	Config.FishStaminaCostPerStrengthCentimeter = FightBalance->FishStaminaCostPerStrengthCentimeter;
	Config.IsometricEffortMultiplier = FightBalance->IsometricEffortMultiplier;
	Config.BaseDrainMultiplier = Personality->BaseDrainMultiplier;
	Config.StruggleDrainMultiplier = Personality->StruggleDrainMultiplier;
	Config.StalemateRodWearPerFishStrength = FightBalance->StalemateRodWearPerFishStrength;
	Config.SlackStaminaRegenPerSecond = FightBalance->SlackStaminaRegenPerSecond;
	Config.ReelSpeedCentimetersPerSecond = FightBalance->ReelSpeedCentimetersPerSecond;
	Config.FishCalmSpeedCentimetersPerSecond = Personality->CalmMovementSpeedCentimetersPerSecond;
	Config.FishStruggleSpeedCentimetersPerSecond = Personality->StruggleMovementSpeedCentimetersPerSecond;
	Config.FishExhaustionThreshold = FightBalance->FishExhaustionThreshold;
	Config.StrongConfrontationAlignmentThreshold = Personality->StrongConfrontationAlignmentThreshold;
	Config.StrongConfrontationConfirmationSeconds = Personality->StrongConfrontationConfirmationSeconds;
	Config.AngleStrengthExponent = Personality->AngleStrengthExponent;
	Config.TensionResponseRangeCentimeters = FightBalance->TensionResponseRangeCentimeters;
	Config.MinimumRodLeverageMultiplier = FightBalance->HeldRodMinimumLeverageMultiplier;
	Config.MaximumFishConstraintCorrectionSpeedCentimetersPerSecond =
		FightBalance->MaximumFishConstraintCorrectionSpeedCentimetersPerSecond;
	Config.MinimumCarrierAwaySpeedMultiplier = FightBalance->MinimumCarrierAwaySpeedMultiplier;
	Config.MaximumLineLengthCentimeters = RodDefinition->MaximumLineLengthCentimeters;
	// 当前资产字段仍叫 MaximumRodDurability，但玩法语义是“本场鱼线耐久”：每次新会话重置，不损坏装备鱼竿。
	Config.RodDurability = RodDefinition->MaximumRodDurability;
	Config.StruggleHoldRodWearPerSecond = RodDefinition->BaseDurabilityWearPerSecond;
	Config.TautRodWearMultiplier = FMath::Max(1.0, RodDefinition->HighTensionWearMultiplier);
	Config.EscapeSlackCentimeters = FightBalance->EscapeSlackCentimeters;
	if (!Config.IsValid()) return false; // 配置自检（如任何数值非有限/非法组合）未通过则拒绝启动搏斗。

	// 组装搏斗模拟的初始状态：猫当前体力从 ASC 读，鱼体力/初始线长按完美中鱼折减系数缩放。
	FCatFightSimulationState InitialState;
	InitialState.CatStamina = AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	InitialState.FishStamina = Snapshot.FishFightStaminaRemaining * FishStaminaScale;
	const FVector RodTipWorldPosition = Rod->GetRodTipWorldTransform().GetLocation();
	const double RequestedInitialLineLength = Encounter->GetPresentationState().CurrentLineLength * LineLengthScale;
	const double MinimumPhysicalLineLength = FMath::Abs(
		Encounter->GetActorLocation().Z - RodTipWorldPosition.Z);
	// 完美提竿会缩短初始线长，但“账面线长”绝不能直接变得比 Actor 的真实距离还短。
	// 先把请求值限制在竿尖到当前水面的最短物理长度内，下面再用同一长度真正投影鱼的位置。
	if (!FMath::IsFinite(RequestedInitialLineLength)
		|| MinimumPhysicalLineLength > Config.MaximumLineLengthCentimeters)
	{
		AbilitySystem->RequestFishingStaminaReset();
		return false;
	}
	InitialState.LineLengthCentimeters = FMath::Clamp(RequestedInitialLineLength,
		MinimumPhysicalLineLength, Config.MaximumLineLengthCentimeters);
	InitialState.FishWorldPosition = Encounter->GetActorLocation();
	InitialState.MotionIntent = ECatFishMotionIntent::StrugglingOutward; // 刚上钩默认视为鱼在向外挣扎。
	InitialState.CatAction = ECatFightCatAction::None;
	Snapshot.FishFightStaminaRemaining = InitialState.FishStamina; // 把折减后的体力写回公开快照。
	Snapshot.RodDurabilityRemaining = Config.RodDurability;
	// 只为初始上钩点建立投影范围；搏斗拖行不再受初始落点包围盒限制，运行时由线长与真实表面负责。
	const FVector Landing = AttemptSnapshot.ServerCorrectedLandingWorldPoint;
	const FVector HalfExtent(Config.MaximumLineLengthCentimeters, Config.MaximumLineLengthCentimeters,
		FMath::Max(500.0, Config.MaximumLineLengthCentimeters * 0.25));
	const FBox FrozenBounds = FBox::BuildAABB(Landing, HalfExtent);
	// 用运动求解器把鱼的初始位置投影到合法范围内（尊重最大线长、水域边界），得到一个几何上自洽的起始点。
	FCatFishMotionSolveInput ProjectionInput;
	ProjectionInput.RodTipWorldPosition = RodTipWorldPosition;
	ProjectionInput.ProposedFishWorldPosition = InitialState.FishWorldPosition;
	ProjectionInput.WaterBounds = FrozenBounds;
	// 关键约束：这里必须使用本次（可能被完美提竿缩短的）初始线长，而不是整根鱼线的最大长度。
	// 否则 Actor 仍留在原钩点，Runner 第一固定步会发现“鱼在线长球外”并按坏状态终止会话。
	ProjectionInput.MaximumLineLengthCentimeters = InitialState.LineLengthCentimeters;
	const FCatFishMotionSolveResult Projected = FCatFishFightMotionSolver::ProjectInitialFishToWater(ProjectionInput);
	// 再用水域子系统把投影点精确吸附到水面上，得到最终的权威落点。
	const FCatWaterSpatialResult Exact = Projected.bSucceeded
		? Water->ResolveCandidatePointToWater(Projected.FishWorldPosition, AttemptSnapshot.WaterRegion)
		: FCatWaterSpatialResult{};
	const double ResolvedInitialDistance = Exact.bSucceeded
		? FVector::Distance(RodTipWorldPosition, Exact.WaterSurfaceWorldPoint)
		: TNumericLimits<double>::Max();
	const double ReconciledInitialLineLength = FMath::Min(Config.MaximumLineLengthCentimeters,
		FMath::Max(InitialState.LineLengthCentimeters, ResolvedInitialDistance));
	if (!Projected.bSucceeded || !Exact.bSucceeded || !FMath::IsFinite(ResolvedInitialDistance)
		|| ResolvedInitialDistance > Config.MaximumLineLengthCentimeters + 0.01
		|| !Encounter->ApplyFightStepFromAuthority(ECatFishMotionIntent::StrugglingOutward,
			ReconciledInitialLineLength,
			Exact.WaterSurfaceWorldPoint, 0.0f, 0.0f, 0.0f,
			static_cast<float>(Config.FishStruggleSpeedCentimetersPerSecond), false))
	{
		// 求解/吸附/表现应用任一环节失败：回滚已初始化的体力属性，不进入搏斗。
		AbilitySystem->RequestFishingStaminaReset();
		return false;
	}
	// 真实水面校正可能把候选点沿岸轻微挪动；最终以 Actor 到竿尖的真实距离抬高线长，
	// 保证 Runner 从第一步起始终满足 D <= L_paid，同时尽可能保留完美提竿的缩线收益。
	InitialState.LineLengthCentimeters = ReconciledInitialLineLength;
	InitialState.FishWorldPosition = Encounter->GetActorLocation(); // 用刚落位的实际权威位置覆盖，作为 Runner 的真正起点。

	// 组装 FightRunner 的初始化参数：把 Session/Actor 引用、模拟配置/初始状态、性格模板节奏参数、
	// 低体力歇息倍率、以及本场搏斗用的随机种子一并交给它，随后驱动固定步长的搏斗推进。
	FCatFishingFightRunnerInit Init;
	Init.Session = this;
	Init.FishActor = Encounter;
	Init.RodActor = Rod;
	Init.AbilitySystem = AbilitySystem;
	Init.PrimaryPlayerState = Snapshot.FisherPlayerState;
	Init.WaterRegion = AttemptSnapshot.WaterRegion;
	Init.Config = Config;
	Init.InitialState = InitialState;
	// 按键按住状态挂在玩家 CommandComponent 上，不随上一场断线终止而丢失。
	// 这里在 Runner 启动前原子快照；两键同时按住时仍由 Runner 保持“收线优先”。
	if (const ACatfishingPlayerController* FisherController = FisherCharacter.IsValid()
		? Cast<ACatfishingPlayerController>(FisherCharacter->GetController()) : nullptr)
	{
		if (const UCatFishingCommandComponent* Commands = FisherController->GetFishingCommandComponent())
		{
			Commands->TryGetHeldFightInputStateFromAuthority(Init.bInitialPullHeld,
				Init.bInitialSlackHeld, Init.InitialInputSequence);
		}
	}
	Init.CalmDurationRangeSeconds = Personality->CalmDurationRangeSeconds;
	Init.StruggleDurationRangeSeconds = Personality->StruggleDurationRangeSeconds;
	Init.LowStaminaRestThreshold = FightBalance->LowStaminaRestThreshold;
	Init.LowStaminaRestMultiplier = FightBalance->LowStaminaRestMultiplier;
	Init.SteeringConfig.RetargetDurationRangeSeconds = Personality->DirectionRetargetDurationRangeSeconds;
	Init.SteeringConfig.MaximumTurnRateDegreesPerSecond = Personality->MaximumTurnRateDegreesPerSecond;
	Init.SteeringConfig.StruggleOutwardBias = Personality->StruggleOutwardDirectionBias;
	Init.SteeringConfig.CalmInwardBias = Personality->CalmInwardDirectionBias;
	Init.SteeringConfig.LateralMovementBias = Personality->LateralMovementBias;
	Init.SteeringConfig.FeintProbability = Personality->FeintProbability;
	Init.SteeringConfig.FullStaminaInwardProbability = Personality->FullStaminaInwardProbability;
	Init.SteeringConfig.ExhaustedInwardProbability = Personality->ExhaustedInwardProbability;
	Init.SteeringConfig.InwardProbabilityExponent = Personality->InwardProbabilityExponent;
	Init.SteeringConfig.InwardConeHalfAngleDegrees = Personality->InwardConeHalfAngleDegrees;
	Init.BehaviorStateTree = FishBehaviorStateTree;
	// 使用玩家实际点击确认的那一轮咬钩机会种子；漏按后下一轮鱼种与搏斗节奏都能变化，同时服务器仍可复现。
	Init.RandomSeed = CurrentBiteRandomSeed != 0
		? CurrentBiteRandomSeed : (AttemptSnapshot.ServerRandomSeed != 0
			? AttemptSnapshot.ServerRandomSeed : static_cast<uint64>(GetTypeHash(Snapshot.FishingSessionId)));
	FightRunner = NewObject<UCatFishingFightRunner>(this);
	if (!FightRunner || !FightRunner->InitializeFromAuthority(Init) || !FightRunner->Start())
	{
		// Runner 创建/初始化/启动任一步失败：清空引用并回滚体力，不留下半启动的 Runner。
		FightRunner = nullptr;
		AbilitySystem->RequestFishingStaminaReset();
		return false;
	}
	Snapshot.bReeling = FightRunner->GetCatAction() == ECatFightCatAction::Pull;
	Snapshot.bSlacking = FightRunner->GetCatAction() == ECatFightCatAction::Slack;
	bFightStaminaInitialized = true;
	StaminaParticipantsTouched.Add(FisherCharacter);
	if (!EnterPhaseFromStateTree(ECatFishingPhase::HookedFight).bApplied)
	{
		// 阶段写入被拒绝（比如并发终止）：必须把已经启动的 Runner 和已初始化的体力状态全部回滚，
		// 否则会出现"Runner 在跑但阶段还停在 TrueBiteWindow"的不一致状态。
		FightRunner->Stop();
		FightRunner = nullptr;
		Snapshot.bReeling = false;
		Snapshot.bSlacking = false;
		StaminaParticipantsTouched.Remove(FisherCharacter);
		bFightStaminaInitialized = false;
		AbilitySystem->RequestFishingStaminaReset();
		return false;
	}
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_fight_started SessionId=%s FightBalanceId=%s FishDefinition=%s RodDefinition=%s PerfectHook=%s PrimaryStrength=%.2f InitialHelperStrength=%.2f InitialCombinedStrength=%.2f CatSystemMassKg=%.2f FishMassKg=%.2f MassMode=StrengthDerived FishStrengthBase=%.2f FishStrengthEffective=%.2f StrengthPerKg=%.2f AccelerationPerStrength=%.2f DriveResponseSeconds=%.2f CatStamina=%.2f FishStamina=%.2f LineStrength=%.2f LineDurability=%.2f RodPhysicsLengthCm=%.2f InitialLineLengthCm=%.2f MaximumLineLengthCm=%.2f RodPose=%s CatWorkCost=%.5f FishWorkCost=%.5f IsometricMultiplier=%.3f BaseDrainMultiplier=%.3f StruggleDrainMultiplier=%.3f FishCalmSpeedCmPerSec=%.2f FishStruggleSpeedCmPerSec=%.2f FixedStepSeconds=%.3f MinimumLeverage=%.3f MaximumEndpointCorrectionSpeed=%.2f MinimumCarrierAwaySpeedMultiplier=%.3f"),
		*Snapshot.FishingSessionId.ToString(),
		*FightBalance->BalanceDefinitionId.ToString(),
		*FishDefinition->FishDefinitionId.ToString(),
		*RodDefinition->EquipmentDefinitionId.ToString(),
		bPerfect ? TEXT("true") : TEXT("false"),
		Config.PrimaryOperatorCatStrength,
		Config.SecondCatStrength,
		Config.GetCombinedCatStrength(),
		Config.PrimaryOperatorMassKilograms,
		Config.FishMassKilograms,
		FrozenSelectionResult.BaseFishStrength,
		Config.FishStrength,
		Config.StrengthPerKilogram,
		Config.AccelerationPerStrength,
		Config.DriveResponseSeconds,
		InitialState.CatStamina,
		InitialState.FishStamina,
		Config.RodStrength,
		Config.RodDurability,
		Config.RodPhysicsLengthCentimeters,
		InitialState.LineLengthCentimeters,
		Config.MaximumLineLengthCentimeters,
		Snapshot.RodActor && Snapshot.RodActor->GetPresentationState().PoseMode == ECatFishingRodPoseMode::Held
			? TEXT("Held") : TEXT("Grounded"),
		Config.CatStaminaCostPerStrengthCentimeter,
		Config.FishStaminaCostPerStrengthCentimeter,
		Config.IsometricEffortMultiplier,
		Config.BaseDrainMultiplier,
		Config.StruggleDrainMultiplier,
		Config.FishCalmSpeedCentimetersPerSecond,
		Config.FishStruggleSpeedCentimetersPerSecond,
		Config.FixedStepSeconds,
		Config.MinimumRodLeverageMultiplier,
		Config.MaximumFishConstraintCorrectionSpeedCentimetersPerSecond,
		Config.MinimumCarrierAwaySpeedMultiplier);
	return true;
}

bool ACatFishingSession::SetReelingFromAuthority(APlayerState* InputPlayerState,
	const int64 InputSequence, const bool bReeling)
{
	// HookedFight 与鱼力竭回收共用同一个 Runner 和输入序号域。
	if (!HasAuthority() || (Snapshot.Phase != ECatFishingPhase::HookedFight
		&& Snapshot.Phase != ECatFishingPhase::ExhaustedReel) || !FightRunner
		|| !Snapshot.RodActor || Snapshot.RodActor->GetOperatorSlotIndex(InputPlayerState) == INDEX_NONE
		|| !FightRunner->SetReeling(InputPlayerState, InputSequence, bReeling))
	{
		return false;
	}
	Snapshot.bReeling = FightRunner->GetCatAction() == ECatFightCatAction::Pull;
	Snapshot.bSlacking = FightRunner->GetCatAction() == ECatFightCatAction::Slack;
	PublishSnapshot(ECatFishingSnapshotMutation::HighFrequency); // 高频输入不推进离散 Revision，只更新 SnapshotSequence。
	return true;
}

bool ACatFishingSession::SetSlackingFromAuthority(APlayerState* InputPlayerState,
	const int64 InputSequence, const bool bSlacking)
{
	if (!HasAuthority() || (Snapshot.Phase != ECatFishingPhase::HookedFight
		&& Snapshot.Phase != ECatFishingPhase::ExhaustedReel) || !FightRunner
		|| InputPlayerState != Snapshot.FisherPlayerState
		|| !FightRunner->SetSlacking(InputPlayerState, InputSequence, bSlacking))
	{
		return false;
	}
	Snapshot.bReeling = FightRunner->GetCatAction() == ECatFightCatAction::Pull;
	Snapshot.bSlacking = FightRunner->GetCatAction() == ECatFightCatAction::Slack;
	PublishSnapshot(ECatFishingSnapshotMutation::HighFrequency);
	return true;
}

bool ACatFishingSession::IsFightRunnerRunning() const
{
	return FightRunner && FightRunner->IsRunning();
}

// FightRunner 每完成一步固定步长模拟就回调一次本函数：把模拟结果写回公开快照，并根据步结果决定是否收尾搏斗。
void ACatFishingSession::HandleFightRunnerStepFromAuthority(const FCatFightStepResult& Step,
	const double FishStaminaRemaining, const ECatFishMotionIntent MotionIntent, const double RodDurabilityRemaining)
{
	if (!HasAuthority() || IsTerminal() || (Snapshot.Phase != ECatFishingPhase::HookedFight
		&& Snapshot.Phase != ECatFishingPhase::ExhaustedReel) || !FightRunner) return;
	Snapshot.FishFightStaminaRemaining = FMath::Max(0.0, FishStaminaRemaining); // 钳制非负，防止浮点误差产生负值。
	Snapshot.RodDurabilityRemaining = FMath::Max(0.0, RodDurabilityRemaining);
	// 钩在鱼嘴里：搏斗期间钩 Actor 跟随鱼的权威位置（含近岸/贴岸吸附后的落点），复制到所有端。
	if (Snapshot.HookActor && Snapshot.FishEncounterActor)
	{
		Snapshot.HookActor->SetActorLocation(Snapshot.FishEncounterActor->GetActorLocation());
		if (!Snapshot.HookActor->SetFishingLinePresentationFromAuthority(
			Step.LineLengthCentimeters, Step.StraightLineDistanceCentimeters,
			Step.SlackLineLengthCentimeters, static_cast<float>(Step.NormalizedTension), Step.bLineTaut))
		{
			HandleFightRunnerFailureFromAuthority(TEXT("HookLinePresentation"));
			return;
		}
	}
	Snapshot.NormalizedFishStamina = FishDefinition && FishDefinition->FishFightStamina > 0.0
		? FMath::Clamp(Snapshot.FishFightStaminaRemaining / FishDefinition->FishFightStamina, 0.0, 1.0) : 0.0;
	Snapshot.FishMotionIntent = MotionIntent;
	Snapshot.FishLineAlignment = static_cast<float>(Step.FishLineAlignment);
	Snapshot.NormalizedLineLoad = static_cast<float>(Step.NormalizedLineLoad);
	Snapshot.bStrongConfrontation = Step.bStrongConfrontation;
	Snapshot.RodLeverageMultiplier = static_cast<float>(Step.RodLeverageMultiplier);
	Snapshot.CarrierMovementAlpha = 0.0f;
	// 兼容现有 HUD 资产：字段不再表示蓄力，只表示主位当前是否提交收线意图。
	Snapshot.PrimaryPowerAlpha = FightRunner->GetCatAction() == ECatFightCatAction::Pull ? 1.0f : 0.0f;
	Snapshot.ActiveCombinedFishingStrength = FMath::Max(0.0, Step.CombinedCatStrength);
	Snapshot.ActiveHelperCount = FMath::Max(0, Step.ActiveHelperCount);
	Snapshot.bReeling = FightRunner->GetCatAction() == ECatFightCatAction::Pull;
	Snapshot.bSlacking = FightRunner->GetCatAction() == ECatFightCatAction::Slack;
	Snapshot.CarrierPullAccelerationCentimetersPerSecondSquared =
		static_cast<float>(Step.CarrierPullAccelerationCentimetersPerSecondSquared);
	Snapshot.CarrierAwaySpeedMultiplier = static_cast<float>(Step.CarrierAwaySpeedMultiplier);
	Snapshot.ConstraintErrorCentimeters = static_cast<float>(Step.ConstraintErrorCentimeters);
	Snapshot.FishConstraintCorrectionCentimeters =
		static_cast<float>(Step.FishConstraintCorrectionCentimeters);
	RefreshFightSummary(); // 每步都重新校验参与者是否仍然合法在场（掉线/倒地会即时反映）。
	PublishSnapshot(ECatFishingSnapshotMutation::HighFrequency); // 搏斗数值每步都要尽快同步给客户端表现层。
	if (Step.Outcome == ECatFightStepOutcome::LineBroken)
	{
		const TCHAR* LineBreakCause = TEXT("None");
		switch (Step.LineBreakCause)
		{
		case ECatFightLineBreakCause::StrengthOverload:
			LineBreakCause = TEXT("StrengthOverload");
			break;
		case ECatFightLineBreakCause::DurabilityDepleted:
			LineBreakCause = TEXT("DurabilityDepleted");
			break;
		default:
			break;
		}
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fishing_line_broken SessionId=%s FishDefinition=%s Cause=%s RemainingLineDurability=%.2f "
				"AccumulatedLineWear=%.2f LastLineWearDelta=%.3f Tension=%.3f LineLoad=%.3f Alignment=%.3f StrongConfrontation=%s "
				"RodLeverage=%.3f CarrierMovementAlpha=%.3f CarrierPullAcceleration=%.2f RodPose=%s "
				"FishLocation=%s Rod=%s RodTip=%s Hook=%s %s"),
			*Snapshot.FishingSessionId.ToString(),
			FishDefinition ? *FishDefinition->FishDefinitionId.ToString() : TEXT("None"),
			LineBreakCause,
			Snapshot.RodDurabilityRemaining,
			Step.AbsoluteRodWear,
			Step.LineWearDelta,
			Step.NormalizedTension,
			Step.NormalizedLineLoad,
			Step.FishLineAlignment,
			Step.bStrongConfrontation ? TEXT("true") : TEXT("false"),
			Step.RodLeverageMultiplier,
			0.0,
			Step.CarrierPullAccelerationCentimetersPerSecondSquared,
			Snapshot.RodActor && Snapshot.RodActor->GetPresentationState().PoseMode == ECatFishingRodPoseMode::Held
				? TEXT("Held") : TEXT("Grounded"),
			Snapshot.FishEncounterActor ? *Snapshot.FishEncounterActor->GetActorLocation().ToCompactString() : TEXT("None"),
			*GetNameSafe(Snapshot.RodActor),
			Snapshot.RodActor ? *Snapshot.RodActor->GetRodTipWorldTransform().GetLocation().ToCompactString() : TEXT("None"),
			*GetNameSafe(Snapshot.HookActor),
			*CatLogContext::BuildControllerFields(FisherCharacter.IsValid() ? FisherCharacter->GetController() : nullptr));
		// 断线只终止当前会话。FinalizeSession 会释放 FishingUse；部署中的鱼竿及其操作槽保持原样，可立即重新抛竿。
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::LineBroken, TEXT("Fishing line broken"));
	}
	else if (Step.Outcome == ECatFightStepOutcome::Escaped)
	{
		// 线放尽/张力超限等判定为鱼直接逃脱，无需先进入 NearShore。
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Escaped, TEXT("Fish escaped"));
	}
	else if (Step.Outcome == ECatFightStepOutcome::FishExhausted
		&& Snapshot.Phase == ECatFishingPhase::HookedFight)
	{
		if (!StateTreeComponent || !FightRunner->SetFishExhaustedFromAuthority())
		{
			FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Invalidated,
				TEXT("Fish exhausted transition unavailable"));
			return;
		}
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_fish_exhausted SessionId=%s Cause=%s FishStaminaRemaining=%.3f "
				"Beached=%s Result=StateTreeEventSent RunnerContinues=true"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			Step.bFishBeached ? TEXT("ShoreLanding") : TEXT("StaminaDepleted"),
			FishStaminaRemaining, Step.bFishBeached ? TEXT("true") : TEXT("false"));
		Snapshot.FishMotionIntent = ECatFishMotionIntent::AutoHauling;
		Snapshot.bStrongConfrontation = false;
		PublishSnapshot(ECatFishingSnapshotMutation::HighFrequency);
		StateTreeComponent->SendStateTreeEvent(CatFishingStateTreeEvents::FishExhausted,
			FConstStructView(), TEXT("CatFishing"));
	}
	else if (Snapshot.Phase == ECatFishingPhase::ExhaustedReel)
	{
		const ACatFishEncounterActor* Encounter = Snapshot.FishEncounterActor;
		const ACatFishingRodActor* Rod = Snapshot.RodActor;
		const UCatWorldItemSettings* ItemSettings = GetDefault<UCatWorldItemSettings>();
		if (!Encounter || !Rod || !FightRunner)
		{
			HandleFightRunnerFailureFromAuthority(TEXT("ExhaustedReelDependency"));
			return;
		}
		// Pickup 只允许从已经落在真实干地上的 Encounter 交接。鱼仍在水里时继续收线，
		// 不隐藏旧鱼，也不在竿尖可能悬于水面的 XY 生成不可拾取对象。
		if (!FightRunner->IsFishBeachedForAuthority())
		{
			return;
		}
		const double ConfiguredReachTolerance = ItemSettings
			? ItemSettings->LandingCompletionDistanceToRodCentimeters : 0.0;
		const double ReachTolerance = FMath::IsFinite(ConfiguredReachTolerance)
			&& ConfiguredReachTolerance > 0.0
			? FMath::Max(5.0, ConfiguredReachTolerance) : 5.0;
		// 收线约束端点是竿尖，不是相隔一段杆长的握把。已到达干地的鱼也不应因刚松开左键而卡住交接。
		const FVector PickupTarget = Rod->GetRodTipWorldTransform().GetLocation();
		if (FVector::Dist2D(Encounter->GetActorLocation(), PickupTarget) <= ReachTolerance
			&& !SpawnExhaustedFishPickupFromAuthority(Encounter->GetActorLocation()))
		{
			HandleFightRunnerFailureFromAuthority(TEXT("ExhaustedFishPickupSpawn"));
		}
	}
}

void ACatFishingSession::HandleCatEnteredDangerousWaterFromAuthority(
	const double ImmersionDepthCentimeters)
{
	if (!HasAuthority() || IsTerminal() || (Snapshot.Phase != ECatFishingPhase::HookedFight
		&& Snapshot.Phase != ECatFishingPhase::ExhaustedReel))
	{
		return;
	}
	UE_LOG(LogCatFishing, Warning,
		TEXT("Event=fishing_cat_entered_dangerous_water SessionId=%s DepthCm=%.2f Phase=%s Result=CatInWater %s"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), ImmersionDepthCentimeters,
		*UEnum::GetValueAsString(Snapshot.Phase),
		*CatLogContext::BuildControllerFields(FisherCharacter.IsValid() ? FisherCharacter->GetController() : nullptr));
	FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::CatInWater,
		TEXT("Condition confirmed dangerous foot immersion"));
}

bool ACatFishingSession::CommitCatchEquipmentFromAuthority()
{
	UCatEquipmentComponent* Equipment = CastEquipment.Get();
	if (!Equipment)
	{
		return false;
	}
	const FCatFishingUseOperationResult Bait = Equipment->CommitFishingBaitDeferred(Snapshot.FishingSessionId);
	// Runner 中的负载耐久代表本场鱼线，新一轮钓鱼会重置；这里不提交鱼竿永久磨损。
	return Bait.bApplied || Bait.Error == ECatDomainCommandError::AlreadyResolved;
}

bool ACatFishingSession::SpawnExhaustedFishPickupFromAuthority(const FVector& SurfaceLocation)
{
	UWorld* World = GetWorld();
	const UCatWorldItemSettings* Settings = GetDefault<UCatWorldItemSettings>();
	if (!HasAuthority() || !World || !Settings || !FishDefinition || !AttemptSnapshot.WaterRegion.IsValid()
		|| !FightRunner || !FightRunner->IsFishBeachedForAuthority() || !Snapshot.FishEncounterActor)
	{
		UE_LOG(LogCatFishing, Error,
			TEXT("Event=exhausted_fish_pickup_rejected SessionId=%s Authority=%s World=%s Settings=%s "
				"FishDefinition=%s WaterRegion=%s Beached=%s Encounter=%s Reason=Dependency"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			HasAuthority() ? TEXT("true") : TEXT("false"), World ? TEXT("valid") : TEXT("null"),
			Settings ? TEXT("valid") : TEXT("null"), FishDefinition ? TEXT("valid") : TEXT("null"),
			AttemptSnapshot.WaterRegion.IsValid() ? TEXT("valid") : TEXT("invalid"),
			FightRunner && FightRunner->IsFishBeachedForAuthority() ? TEXT("true") : TEXT("false"),
			*GetNameSafe(Snapshot.FishEncounterActor));
		return false;
	}
	if (!CommitCatchEquipmentFromAuthority())
	{
		UE_LOG(LogCatFishing, Error,
			TEXT("Event=exhausted_fish_pickup_rejected SessionId=%s Reason=EquipmentCommit"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens));
		return false;
	}
	// 使用可见 Encounter 的当前干地位置完成表现交接，避免在竿尖/水面处重新投影后跳位或消失。
	const FVector SpawnLocation = SurfaceLocation;

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = nullptr;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// 可拾取鱼沿用水中最后的水平朝向，但清掉可能的俯仰并保持侧翻。
	// 旋转写在服务器生成的 Actor 上而不是只转客户端 Mesh，ReplicatedMovement 会让所有玩家看到同一结果。
	FRotator LandedRotation = Snapshot.FishEncounterActor
		? Snapshot.FishEncounterActor->GetActorRotation() : FRotator::ZeroRotator;
	LandedRotation.Pitch = 0.0;
	const UCatFishPresentationDefinition* FishPresentation =
		FishDefinition->LoadRuntimePresentationDefinition();
	LandedRotation.Roll = FishPresentation ? FishPresentation->LandedActorRollDegrees : 90.0;
	ACatFishPickupActor* Pickup = World->SpawnActor<ACatFishPickupActor>(
		ACatFishPickupActor::StaticClass(), SpawnLocation, LandedRotation, SpawnParams);
	TArray<FString> Participants;
	if (Snapshot.bGiant)
	{
		Participants = FightParticipantIds.Array();
	}
	else if (!FisherStableNetId.IsEmpty())
	{
		Participants.Add(FisherStableNetId);
	}
	if (!Pickup || !Pickup->InitializeFromAuthority(Snapshot.FishingSessionId, FGuid::NewGuid(),
		FishDefinition, FishWeightKilograms, FishVisualScale, AttemptSnapshot.WaterRegion.RegionId, Participants))
	{
		UE_LOG(LogCatFishing, Error,
			TEXT("Event=exhausted_fish_pickup_rejected SessionId=%s Reason=%s Location=%s"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			Pickup ? TEXT("Initialization") : TEXT("Spawn"), *SpawnLocation.ToCompactString());
		if (Pickup)
		{
			Pickup->Destroy();
		}
		return false;
	}
	// Pickup 已经接管鱼的世界表现；旧 Encounter 仍保留一个短复制窗口，但必须立刻退出可视与碰撞。
	// bHidden 是 AActor 的复制属性，ForceNetUpdate 会把这次表现交接尽快同步给其他客户端。
	if (ACatFishEncounterActor* Encounter = Snapshot.FishEncounterActor)
	{
		Encounter->SetActorHiddenInGame(true);
		Encounter->SetActorEnableCollision(false);
		Encounter->ForceNetUpdate();
	}
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=exhausted_fish_pickup_spawned SessionId=%s Pickup=%s Location=%s Rotation=%s "
			"LandingTarget=RodTip PickupState=Available WorldNetMode=%d Authority=true %s"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(Pickup),
		*Pickup->GetActorLocation().ToCompactString(), *Pickup->GetActorRotation().ToCompactString(),
		static_cast<int32>(World->GetNetMode()),
		*CatLogContext::BuildControllerFields(FisherCharacter.IsValid() ? FisherCharacter->GetController() : nullptr));
	FinalizeSession(ECatFishingPhase::Resolved, ECatFishingOutcome::Landed,
		TEXT("Grounded exhausted fish reached the rod tip as world pickup"));
	return true;
}

void ACatFishingSession::SuspendOperatorFromAuthority()
{
	if (!HasAuthority() || IsTerminal()) return;
	const ECatFishingPhase Phase = Snapshot.Phase;
	const bool bFightUnattended = Phase == ECatFishingPhase::HookedFight
		|| Phase == ECatFishingPhase::ExhaustedReel;
	APlayerState* OldFisherPlayerState = Snapshot.FisherPlayerState;
	ACatCharacter* OldFisherCharacter = FisherCharacter.Get();
	AController* OldController = OldFisherCharacter ? OldFisherCharacter->GetController() : nullptr;
	const FString OldFisherLogValue = CatLogContext::BuildStableNetIdValue(OldFisherPlayerState);
	bool bRunnerTransitionApplied = !bFightUnattended;

	if (bFightUnattended)
	{
		bRunnerTransitionApplied = FightRunner && FightRunner->IsRunning()
			&& FightRunner->BeginUnattendedSlackFromAuthority();
		if (!bRunnerTransitionApplied)
		{
			UE_LOG(LogCatFishing, Warning,
				TEXT("Event=fishing_operator_suspended SessionId=%s Phase=%s Mode=UnattendedSlack RunnerTransition=false OldFisher=%s %s"),
				*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
				*UEnum::GetValueAsString(Phase), *OldFisherLogValue,
				*CatLogContext::BuildControllerFields(OldController));
		}
		// 放下鱼竿只解除本会话对该体力池的所有权；角色保留离开瞬间的剩余体力，不做瞬间补满。
		StaminaParticipantsTouched.Remove(OldFisherCharacter);
	}
	Snapshot.bReeling = false;
	Snapshot.bSlacking = bFightUnattended;
	Snapshot.RodLeverageMultiplier = 1.0f;
	Snapshot.CarrierMovementAlpha = 0.0f;
	Snapshot.CarrierPullAccelerationCentimetersPerSecondSquared = 0.0f;
	Snapshot.CarrierAwaySpeedMultiplier = 1.0f;
	Snapshot.ConstraintErrorCentimeters = 0.0f;
	Snapshot.FishConstraintCorrectionCentimeters = 0.0f;
	Snapshot.PrimaryPowerAlpha = 0.0f;
	Snapshot.ActiveCombinedFishingStrength = 0.0;
	Snapshot.ActiveHelperCount = 0;
	LastSuspendedFisherPlayerState = OldFisherPlayerState;
	FightParticipantIds.Remove(FisherStableNetId);
	FightParticipantCharacters.Remove(FisherStableNetId);
	FisherStableNetId.Reset();
	FisherCharacter.Reset();
	Snapshot.FisherPlayerState = nullptr;
	RefreshFightSummary();
	PublishSnapshot(ECatFishingSnapshotMutation::Discrete);
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_operator_suspended SessionId=%s Phase=%s Mode=%s RunnerTransition=%s OldFisher=%s Rod=%s Reeling=%s Slacking=%s %s"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
		*UEnum::GetValueAsString(Phase), bFightUnattended ? TEXT("UnattendedSlack") : TEXT("InputReleased"),
		bRunnerTransitionApplied ? TEXT("true") : TEXT("false"),
		*OldFisherLogValue, *GetNameSafe(Snapshot.RodActor), Snapshot.bReeling ? TEXT("true") : TEXT("false"),
		Snapshot.bSlacking ? TEXT("true") : TEXT("false"), *CatLogContext::BuildControllerFields(OldController));
}

bool ACatFishingSession::SpawnScoopedFishPickupFromAuthority(ACatCharacter* ScoopingCharacter,
	APlayerState* ScoopingPlayerState, const FString& ScooperStableNetId)
{
	UWorld* World = GetWorld();
	ACatFishEncounterActor* Encounter = Snapshot.FishEncounterActor;
	if (!HasAuthority() || !World || !ScoopingCharacter || !ScoopingPlayerState || ScooperStableNetId.IsEmpty()
		|| !Encounter || !FishDefinition || !AttemptSnapshot.WaterRegion.IsValid()
		|| ACatFishPickupActor::FindCarriedFish(ScoopingCharacter))
	{
		return false;
	}

	TArray<FString> Participants;
	if (Snapshot.bGiant)
	{
		Participants = FightParticipantIds.Array();
	}
	else if (!FisherStableNetId.IsEmpty())
	{
		Participants.Add(FisherStableNetId);
	}
	Participants.AddUnique(ScooperStableNetId);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = nullptr;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FRotator SpawnRotation = Encounter->GetActorRotation();
	SpawnRotation.Pitch = 0.0;
	SpawnRotation.Roll = 0.0;
	ACatFishPickupActor* Pickup = World->SpawnActor<ACatFishPickupActor>(ACatFishPickupActor::StaticClass(),
		Encounter->GetActorLocation(), SpawnRotation, SpawnParams);
	if (!Pickup || !Pickup->InitializeFromAuthority(Snapshot.FishingSessionId, FGuid::NewGuid(), FishDefinition,
		FishWeightKilograms, FishVisualScale, AttemptSnapshot.WaterRegion.RegionId, Participants)
		|| !Pickup->BeginMouthCarryFromAuthority(ScoopingCharacter, ScoopingPlayerState))
	{
		if (Pickup)
		{
			Pickup->Destroy();
		}
		return false;
	}

	bCaptureResolved = true;
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=scooped_fish_mouth_carried SessionId=%s Pickup=%s ScooperPlayerState=%s "
			"ScooperStableNetId=%s ScooperPawn=%s ScooperLocation=%s FishStamina=%.3f"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(Pickup),
		*GetNameSafe(ScoopingPlayerState), *CatLogContext::BuildStableNetIdValue(ScoopingPlayerState),
		*GetNameSafe(ScoopingCharacter), *ScoopingCharacter->GetActorLocation().ToCompactString(),
		Snapshot.FishFightStaminaRemaining);
	FinalizeSession(ECatFishingPhase::Resolved, ECatFishingOutcome::Caught,
		TEXT("Scoop transferred hooked fish directly to mouth carry"));
	return IsTerminal() && Snapshot.Phase == ECatFishingPhase::Resolved
		&& Snapshot.Outcome == ECatFishingOutcome::Caught
		&& ACatFishPickupActor::FindCarriedFish(ScoopingCharacter) == Pickup;
}

void ACatFishingSession::HandleFightRunnerFailureFromAuthority(const FName FailureStage)
{
	// FightRunner 自身遇到不可恢复的依赖失败（如引用失效）时回调本函数；只要会话还没结束就直接判为失效终止。
	if (HasAuthority() && !IsTerminal())
	{
		const FString Reason = FString::Printf(TEXT("Fight runner failed at %s"),
			FailureStage.IsNone() ? TEXT("Unknown") : *FailureStage.ToString());
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Invalidated, *Reason);
	}
}

FCatFishingCommandResult ACatFishingSession::RequestHookFromAuthority(const FGuid RequestId)
{
	if (const FCatFishingCommandResult* Cached = HookTerminalByRequest.Find(RequestId)) return *Cached;
	FCatFishingCommandResult Result;
	Result.CommandType = ECatFishingCommandType::RequestHook;
	Result.RequestId = RequestId;
	Result.FishingSessionId = Snapshot.FishingSessionId;
	Result.CastAttemptId = Snapshot.CastAttemptId;
	if (!RequestId.IsValid() || !HasAuthority() || IsTerminal()) Result.Error = ECatFishingCommandError::InvalidPayload;
	else if (Snapshot.Phase == ECatFishingPhase::Waiting || Snapshot.Phase == ECatFishingPhase::Probe)
	{
		// 鱼还没有给出真咬信号就提竿：算作"空军"（EarlyHook），提竿本身仍然算命令成功提交。
		if (StateTreeComponent) StateTreeComponent->SendStateTreeEvent(CatFishingGameplayTags::EarlyHook,
			FConstStructView(), TEXT("CatFishing"));
		Result.bCommitted = true;
		Result.Error = ECatFishingCommandError::None;
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::EmptyHook, TEXT("Early hook"));
	}
	else if (Snapshot.Phase == ECatFishingPhase::TrueBiteWindow
		&& SelectionResolution == ECatFishSelectionResolution::None && bTrueBiteWindowAcceptingHook)
	{
		// 先冻结服务器收到左键时的响应时间，再停止窗口计时。选鱼/加载资产的耗时不能反过来影响完美提竿判定。
		const double SinceBite = GetWorld()
			? GetWorld()->GetTimeSeconds() - Snapshot.PhaseStartedServerTime : TNumericLimits<double>::Max();
		bTrueBiteWindowAcceptingHook = false;
		GetWorldTimerManager().ClearTimer(TrueBiteTimerHandle);
		const FCatFishSelectionCommitResult Selection = ResolveHookSelectionFromAuthority();
		if (Selection.Resolution != ECatFishSelectionResolution::Selected)
		{
			// 无合格鱼是一次已被服务器正常处理的空钩；依赖/生成失败则由选择事务收敛为 Invalidated。
			Result.bCommitted = Selection.Resolution == ECatFishSelectionResolution::NoEligibleFish;
			Result.Error = Result.bCommitted ? ECatFishingCommandError::None : ECatFishingCommandError::InvalidPhase;
			Result.Revision = Snapshot.Revision;
			HookTerminalByRequest.Add(RequestId, Result);
			return Result;
		}

		// 到这里才存在本次鱼定义与性格；也就是说鱼种选择、Actor 生成、饵料提交都严格发生在合法左键之后。
		const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
		const UCatBitePersonalityDefinition* Bite = FishDefinition && Settings
			? Settings->FindBitePersonality(FishDefinition->BitePersonalityId) : nullptr;
		// 服务器完全按自己的时间戳判定是否"完美"，不接受客户端上报的反应时间，杜绝作弊。
		Snapshot.bPerfectHook = Bite && FMath::IsFinite(SinceBite) && SinceBite >= 0.0
			&& SinceBite <= Bite->PerfectHookWindowSeconds;
		Result.bCommitted = TryEnterHookedFightFromAuthority(); // 真正的搏斗初始化在这里发生。
		if (Result.bCommitted)
		{
			if (ACatFishEncounterActor* Encounter = Snapshot.FishEncounterActor)
			{
				// 搏斗完整启动后才放行鱼的首次多人表现，客户端不会看见一个尚未成立的半初始化 Encounter。
				Encounter->PublishInitialPresentationFromAuthority();
			}
			if (StateTreeComponent) StateTreeComponent->SendStateTreeEvent(
				CatFishingGameplayTags::HookAccepted, FConstStructView(), TEXT("CatFishing"));
		}
		if (!Result.bCommitted)
		{
			// 提竿动作本身合法，但搏斗初始化失败（依赖缺失等）：视为系统性异常，终止整个会话。
			if (ACatFishEncounterActor* Encounter = Snapshot.FishEncounterActor)
			{
				Encounter->Destroy();
				Snapshot.FishEncounterActor = nullptr;
			}
			FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Invalidated,
				TEXT("Hooked fight initialization failed"));
		}
		Result.Error = Result.bCommitted ? ECatFishingCommandError::None : ECatFishingCommandError::InvalidPhase;
	}
	else Result.Error = ECatFishingCommandError::InvalidPhase; // 其余阶段（如已在搏斗中）提竿命令没有意义，拒绝。
	Result.Revision = Snapshot.Revision;
	HookTerminalByRequest.Add(RequestId, Result); // 按 RequestId 缓存终态，保证重放幂等。
	return Result;
}

FCatFishingCommandResult ACatFishingSession::CancelFromAuthority(const FGuid RequestId)
{
	if (const FCatFishingCommandResult* Cached = CancelTerminalByRequest.Find(RequestId)) return *Cached;
	FCatFishingCommandResult Result;
	Result.CommandType = ECatFishingCommandType::CancelFishing;
	Result.RequestId = RequestId;
	Result.FishingSessionId = Snapshot.FishingSessionId;
	if (RequestId.IsValid() && HasAuthority() && !IsTerminal())
	{
		// 取消在任何非终态阶段都允许：直接把 Interrupted 事件送进 StateTree 并终止为 Cancelled。
		if (StateTreeComponent) StateTreeComponent->SendStateTreeEvent(CatFishingGameplayTags::Interrupted,
			FConstStructView(), TEXT("CatFishing"));
		Result.bCommitted = true;
		Result.Error = ECatFishingCommandError::None;
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Cancelled, TEXT("Cancelled"));
	}
	else Result.Error = ECatFishingCommandError::InvalidPhase;
	Result.Revision = Snapshot.Revision;
	CancelTerminalByRequest.Add(RequestId, Result);
	return Result;
}

FCatFishingCommandResult ACatFishingSession::CutLineFromAuthority(AController* RequestingController,
	const FCatFishingSessionCommandContext& Context)
{
	if (const FCatFishingCommandResult* Cached = CutLineTerminalByRequest.Find(Context.RequestId))
	{
		return *Cached;
	}

	FCatFishingCommandResult Result;
	Result.CommandType = ECatFishingCommandType::CutLine;
	Result.RequestId = Context.RequestId;
	Result.FishingSessionId = Snapshot.FishingSessionId;
	Result.CastAttemptId = Snapshot.CastAttemptId;
	const ECatFishingPhase PhaseBefore = Snapshot.Phase;
	const bool bCuttablePhase = PhaseBefore == ECatFishingPhase::HookedFight
		|| PhaseBefore == ECatFishingPhase::NearShore
		|| PhaseBefore == ECatFishingPhase::ExhaustedReel
		|| PhaseBefore == ECatFishingPhase::AutoHauling;
	if (!Context.RequestId.IsValid() || !HasAuthority())
	{
		Result.Error = ECatFishingCommandError::InvalidPayload;
	}
	else if (IsTerminal())
	{
		Result.Error = ECatFishingCommandError::AlreadyResolved;
	}
	else if (!Context.FishingSessionId.IsValid() || Context.FishingSessionId != Snapshot.FishingSessionId
		|| (Context.CastAttemptId.IsValid() && Context.CastAttemptId != Snapshot.CastAttemptId))
	{
		Result.Error = ECatFishingCommandError::SessionNotFound;
	}
	else if (!RequestingController || !RequestingController->PlayerState)
	{
		Result.Error = ECatFishingCommandError::NotFisher;
	}
	else if ([&]()
	{
		if (Snapshot.FisherPlayerState == RequestingController->PlayerState)
		{
			return false;
		}
		const ACatFishingRodActor* Rod = Snapshot.RodActor;
		const APawn* RequestingPawn = RequestingController->GetPawn();
		const bool bUnattendedGroundRod = Rod && !Snapshot.FisherPlayerState
			&& Rod->GetPresentationState().PoseMode == ECatFishingRodPoseMode::Grounded
			&& Rod->GetOperatorCount() == 0;
		const bool bKnownOperator = Rod && (Rod->GetPresentationState().OwnerPlayerState
			== RequestingController->PlayerState
			|| LastSuspendedFisherPlayerState == RequestingController->PlayerState);
		const bool bNearRod = RequestingPawn && Rod && FVector::DistSquared(
			RequestingPawn->GetActorLocation(), Rod->GetActorLocation()) <= FMath::Square(250.0);
		return !(bUnattendedGroundRod && bKnownOperator && bNearRod);
	}())
	{
		Result.Error = ECatFishingCommandError::NotFisher;
	}
	else if (Context.ExpectedRevision != Snapshot.Revision)
	{
		Result.Error = ECatFishingCommandError::RevisionConflict;
	}
	else if (!bCuttablePhase)
	{
		Result.Error = ECatFishingCommandError::InvalidPhase;
	}
	else
	{
		const double LineDurabilityBefore = Snapshot.RodDurabilityRemaining;
		const double NormalizedLoadBefore = Snapshot.NormalizedLineLoad;
		// 切线直接抢占终态写口；Finalize 会先写入 LineCut，再停止 Runner/StateTree，避免 Interrupted
		// 同步回调在同一帧抢先写成另一种终态，保证“第一个终态提交者获胜”的结果可重放。
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::LineCut,
			TEXT("Fishing line cut by operator"));
		Result.bCommitted = true;
		Result.Error = ECatFishingCommandError::None;
		UE_LOG(LogCatFishing, Display,
			TEXT("Event=fishing_line_cut_committed SessionId=%s RequestId=%s RodActorId=%s PhaseBefore=%s "
				"Revision=%lld LineDurabilityBefore=%.3f LineDurabilityAfter=%.3f NormalizedLoadBefore=%.3f %s"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			*Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			Snapshot.RodActor
				? *Snapshot.RodActor->GetPresentationState().RodActorId.ToString(EGuidFormats::DigitsWithHyphens)
				: TEXT("None"),
			*UEnum::GetValueAsString(PhaseBefore), Snapshot.Revision,
			LineDurabilityBefore, Snapshot.RodDurabilityRemaining, NormalizedLoadBefore,
			*CatLogContext::BuildControllerFields(RequestingController));
	}
	Result.Revision = Snapshot.Revision;
	Result.SnapshotSequence = Snapshot.SnapshotSequence;
	Result.PhaseEpoch = Snapshot.PhaseEpoch;
	CutLineTerminalByRequest.Add(Context.RequestId, Result);
	return Result;
}

bool ACatFishingSession::StartPreparedSessionLogicFromAuthority()
{
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	UStateTree* StateTreeAsset = Settings ? Settings->FishingSessionStateTree.LoadSynchronous() : nullptr;
	if (!HasAuthority() || !bPrepared || bPublished || !Settings || !Settings->IsRuntimeReady()
		|| !StateTreeAsset || !StateTreeComponent)
	{
		return false;
	}
	StateTreeComponent->SetStateTree(StateTreeAsset);
	bStartupInProgress = true; // 允许 StartLogic 同步进入首状态时写入阶段。
	StateTreeComponent->StartLogic();
	bStartupInProgress = false;
	// 只要 StateTree 真正跑起来了、或者阶段已经被首状态事件推进（不再是 Created），就算启动成功。
	return StateTreeComponent->IsRunning() || Snapshot.Phase != ECatFishingPhase::Created;
}

bool ACatFishingSession::PublishPreparedSessionFromAuthority()
{
	// 两阶段提交的第二阶段：只有已 Prepare 且尚未 Publish、且 StateTree 已在运行时才允许发布。
	if (!HasAuthority() || !bPrepared || bPublished || !StateTreeComponent || !StateTreeComponent->IsRunning())
	{
		return false;
	}
	bPublished = true; // 一旦发布就不可再 Abort（AbortPreparedSessionFromAuthority 会检查这个标记）。
	PublishSnapshot(ECatFishingSnapshotMutation::HighFrequency);
	return true;
}

void ACatFishingSession::AbortPreparedSessionFromAuthority()
{
	// 只在尚未发布（bPublished=false）时才允许中止：一旦发布过，就必须走正常的终止/捕获流程收尾，
	// 不能再简单粗暴地直接 Destroy 掉 Actor（客户端可能已经看到过这个会话）。
	if (!HasAuthority() || bPublished) return;
	if (StateTreeComponent) StateTreeComponent->StopLogic(TEXT("BeginCast transaction aborted"));
	bPrepared = false;
	Destroy(); // 从未公开发布过的会话可以直接销毁，不需要走有界复制窗口。
}

// 终态的唯一写口：把当前阶段/结果写为最终值，停止一切仍在运行的子系统，释放资源与参与者引用，
// 并按结果写日志、启动有界销毁；已处于终态时直接幂等返回，不会二次覆盖已经成立的结果。
void ACatFishingSession::FinalizeSession(const ECatFishingPhase FinalPhase, const ECatFishingOutcome FinalOutcome,
	const TCHAR* DiagnosticReason)
{
	if (!HasAuthority() || IsTerminal())
	{
		return;
	}
	// 在释放参与者弱引用和终态 Actor 之前冻结诊断上下文；终态日志必须能够还原是监听主机还是远端玩家、
	// 鱼/竿/钩当时分别在哪里，而不能依赖已经被清空的运行时引用。
	const FString FisherFields = CatLogContext::BuildControllerFields(
		FisherCharacter.IsValid() ? FisherCharacter->GetController() : nullptr);
	const FString FishDefinitionValue = FishDefinition ? FishDefinition->FishDefinitionId.ToString() : TEXT("None");
	const FString EncounterValue = GetNameSafe(Snapshot.FishEncounterActor);
	const FVector FishLocation = Snapshot.FishEncounterActor
		? Snapshot.FishEncounterActor->GetActorLocation() : FVector::ZeroVector;
	const FString RodValue = GetNameSafe(Snapshot.RodActor);
	const FVector RodTipLocation = Snapshot.RodActor
		? Snapshot.RodActor->GetRodTipWorldTransform().GetLocation() : FVector::ZeroVector;
	const FString HookValue = GetNameSafe(Snapshot.HookActor);
	Snapshot.Phase = FinalPhase;
	Snapshot.Outcome = FinalOutcome;
	Snapshot.PhaseStartedServerTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	Snapshot.FishLineAlignment = 0.0f;
	Snapshot.NormalizedLineLoad = 0.0f;
	Snapshot.bStrongConfrontation = false;
	Snapshot.RodLeverageMultiplier = 1.0f;
	Snapshot.CarrierMovementAlpha = 0.0f;
	Snapshot.CarrierPullAccelerationCentimetersPerSecondSquared = 0.0f;
	Snapshot.CarrierAwaySpeedMultiplier = 1.0f;
	Snapshot.ConstraintErrorCentimeters = 0.0f;
	Snapshot.FishConstraintCorrectionCentimeters = 0.0f;
	Snapshot.PrimaryPowerAlpha = 0.0f;
	Snapshot.ActiveCombinedFishingStrength = 0.0;
	Snapshot.ActiveHelperCount = 0;
	Snapshot.bReeling = false;
	Snapshot.bSlacking = false;
	if (Snapshot.HookActor)
	{
		Snapshot.HookActor->SetBobberPresentationModeFromAuthority(ECatFishingBobberPresentationMode::None);
	}
	PublishSnapshot(ECatFishingSnapshotMutation::PhaseChange); // 终态属于阶段变化，必须递增 PhaseEpoch。
	// 终局已经成为服务器事实后才通知猫播放一次性表现；Finalize 的终态幂等门禁保证不会因重放重复播 Montage。
	if (ACatCharacter* Character = FisherCharacter.Get())
	{
		const FGameplayTag PresentationTag = ResolveTerminalFisherPresentationTag(FinalOutcome);
		if (PresentationTag.IsValid())
		{
			Character->Multicast_PlayCosmeticEvent(PresentationTag);
		}
	}
	if (FightRunner) FightRunner->Stop(); // 停止仍在跑的搏斗模拟，防止终态之后还有 Step 回调。
	GetWorldTimerManager().ClearTimer(BiteWarningTimerHandle);
	GetWorldTimerManager().ClearTimer(ProbeTimerHandle);
	GetWorldTimerManager().ClearTimer(TrueBiteTimerHandle);
	// 释放原始抛竿者装备上属于本 Session 的钓具预留；其他鱼竿的并行预留保持不变。
	if (UCatEquipmentComponent* Equipment = CastEquipment.Get())
	{
		Equipment->ReleaseFishingUse(Snapshot.FishingSessionId);
	}
	if (StateTreeComponent && StateTreeComponent->IsRunning())
	{
		StateTreeComponent->StopLogic(FString(DiagnosticReason));
	}
	// 恢复本会话实际扣过体力的所有参与者的搏斗体力池（无论最终谁赢谁输，体力都要归还）。
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
	if (FinalPhase == ECatFishingPhase::Resolved
		&& (FinalOutcome == ECatFishingOutcome::Caught || FinalOutcome == ECatFishingOutcome::Landed))
	{
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_session_resolved SessionId=%s Phase=%s Outcome=%s Reason=\"%s\" Revision=%lld "
				"SnapshotSequence=%lld PhaseEpoch=%lld FishDefinition=%s Encounter=%s FishLocation=%s "
				"Rod=%s RodTip=%s Hook=%s %s"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *UEnum::GetValueAsString(FinalPhase),
			*UEnum::GetValueAsString(FinalOutcome), DiagnosticReason ? DiagnosticReason : TEXT("None"), Snapshot.Revision,
			Snapshot.SnapshotSequence, Snapshot.PhaseEpoch, *FishDefinitionValue, *EncounterValue,
			*FishLocation.ToCompactString(), *RodValue, *RodTipLocation.ToCompactString(), *HookValue, *FisherFields);
	}
	else
	{
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fishing_session_terminated SessionId=%s Phase=%s Outcome=%s Reason=\"%s\" Revision=%lld "
				"SnapshotSequence=%lld PhaseEpoch=%lld FishDefinition=%s Encounter=%s FishLocation=%s "
				"Rod=%s RodTip=%s Hook=%s %s"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *UEnum::GetValueAsString(FinalPhase),
			*UEnum::GetValueAsString(FinalOutcome), DiagnosticReason ? DiagnosticReason : TEXT("None"), Snapshot.Revision,
			Snapshot.SnapshotSequence, Snapshot.PhaseEpoch, *FishDefinitionValue, *EncounterValue,
			*FishLocation.ToCompactString(), *RodValue, *RodTipLocation.ToCompactString(), *HookValue, *FisherFields);
	}
}

FGameplayTag ACatFishingSession::ResolveTerminalFisherPresentationTag(const ECatFishingOutcome Outcome)
{
	switch (Outcome)
	{
	case ECatFishingOutcome::LineBroken:
		return CatFishingAbilityTags::Cosmetic_Fishing_LineBroken;
	case ECatFishingOutcome::LineCut:
		return CatFishingAbilityTags::Cosmetic_Fishing_LineCut;
	case ECatFishingOutcome::CatInWater:
		return CatFishingAbilityTags::Cosmetic_Fishing_CatInWater;
	default:
		return FGameplayTag();
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

// 终态读取流程：只读取公开阶段，不停止 StateTree 或销毁 Actor；服务用它移除会话弱索引，终态 Actor 仍可完成最后一次复制。
bool ACatFishingSession::IsTerminal() const
{
	return Snapshot.Phase == ECatFishingPhase::Resolved || Snapshot.Phase == ECatFishingPhase::Terminated;
}

// World 清理流程：停止仍在运行的 StateTree，并为仍可达参与者登记/应用 stamina 恢复后清私有弱引用；不补发捕获事务。
void ACatFishingSession::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (FightRunner) FightRunner->Stop();
	GetWorldTimerManager().ClearTimer(BiteWarningTimerHandle);
	GetWorldTimerManager().ClearTimer(ProbeTimerHandle);
	GetWorldTimerManager().ClearTimer(TrueBiteTimerHandle);
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
bool ACatFishingSession::RefreshFightSummary()
{
	// 先记住刷新前的三项聚合值，函数末尾用来判断本次刷新是否真的产生了变化。
	const int32 PreviousParticipantCount = Snapshot.FightParticipantCount;
	const double PreviousCombinedFishingStrength = Snapshot.CombinedFishingStrength;
	const double PreviousCombinedFightStamina = Snapshot.CombinedFightStamina;
	// 每次都从零重新聚合，而不是增量修改，避免掉线/倒地参与者的旧贡献残留在总量里。
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
		// 复用统一参战能力谓词重新验证每个参与者：弱引用必须仍然有效，谓词内部解析出的身份/Character
		// 必须和登记时的身份/Character 完全一致，防止掉线重连后身份被冒用或角色被替换。
		if (Character && UCatFishingService::TryGetFightCapability(Character->GetController(), StableNetId,
			ValidatedCharacter, Strength, FightStamina) && StableNetId == Pair.Key
			&& ValidatedCharacter == Character)
		{
			++Snapshot.FightParticipantCount;
			Snapshot.CombinedFishingStrength += Strength;
			Snapshot.CombinedFightStamina += FightStamina;
		}
		// 校验不通过的参与者（掉线/倒地/身份不符）直接跳过，不计入本次聚合，相当于隐式退出搏斗。
	}
	// 只要三项聚合中有任意一项和刷新前不同，就返回 true，供调用方决定是否需要额外推送一次复制更新。
	return Snapshot.FightParticipantCount != PreviousParticipantCount
		|| Snapshot.CombinedFishingStrength != PreviousCombinedFishingStrength
		|| Snapshot.CombinedFightStamina != PreviousCombinedFightStamina;
}

void ACatFishingSession::RegisterFightStaminaParticipantFromAuthority(ACatCharacter* Character)
{
	if (HasAuthority() && Character && !IsTerminal())
	{
		StaminaParticipantsTouched.Add(Character);
		const FString StableNetId = ResolveStableNetId(Character->GetController());
		if (!StableNetId.IsEmpty())
		{
			FightParticipantIds.Add(StableNetId);
			FightParticipantCharacters.Add(StableNetId, Character);
		}
	}
}

void ACatFishingSession::UnregisterFightStaminaParticipantFromAuthority(ACatCharacter* Character)
{
	if (HasAuthority() && Character)
	{
		StaminaParticipantsTouched.Remove(Character);
		const FString StableNetId = ResolveStableNetId(Character->GetController());
		if (!StableNetId.IsEmpty())
		{
			FightParticipantIds.Remove(StableNetId);
			FightParticipantCharacters.Remove(StableNetId);
		}
	}
}

void ACatFishingSession::PublishRefreshedFightSummaryIfChanged(const bool bSummaryChanged)
{
	// 只有真的发生了变化才推送一次高频复制更新，避免每次调用 RefreshFightSummary 都无谓地触发网络同步。
	if (bSummaryChanged)
	{
		PublishSnapshot(ECatFishingSnapshotMutation::HighFrequency);
	}
}

// 终态销毁流程：读取与会话启动共用的显式正复制窗口，成功时交给 Actor lifespan 延迟销毁；若运行中配置突然失效，则下一帧销毁而不无界泄漏。
// 钩与鱼与会话同窗销毁：它们只在生成失败路径被显式 Destroy，正常终结若不在这里接管就会永久残留在水面/岸上；
// 复用同一复制窗，让客户端在会话终态期间仍能看到落点/侧翻收鱼的收尾表现。
void ACatFishingSession::ScheduleTerminalDestroy()
{
	if (!HasAuthority() || !IsTerminal())
	{
		return;
	}
	double WindowSeconds = 0.0;
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	const float TerminalLifeSpan = Settings && Settings->TryGetTerminalReplicationWindow(WindowSeconds)
		? static_cast<float>(WindowSeconds) : KINDA_SMALL_NUMBER;
	SetLifeSpan(TerminalLifeSpan);
	// 钩子立即销毁（收竿手感优先，延迟消失体感差）；Encounter 是否可见按具体终局裁决。
	if (ACatFishingHookActor* Hook = Snapshot.HookActor)
	{
		Hook->Destroy();
	}
	if (ACatFishEncounterActor* Encounter = Snapshot.FishEncounterActor)
	{
		// 抄鱼成功：Pickup 已在嘴上接管世界表现，水中的旧模型立即销毁。
		if (Snapshot.Outcome == ECatFishingOutcome::Caught)
		{
			Encounter->Destroy();
		}
		else
		{
			// 水面收近成功后 Pickup 已接管表现：Encounter 保留复制窗但必须隐藏，不能与 Pickup 重叠成两条鱼。
			// 其他逃走/断线终局仍保持可见，让客户端看完原 Actor 的收尾表现。
			if (Snapshot.Outcome == ECatFishingOutcome::Landed)
			{
				Encounter->SetActorHiddenInGame(true);
				Encounter->SetActorEnableCollision(false);
				Encounter->ForceNetUpdate();
			}
			Encounter->SetLifeSpan(TerminalLifeSpan);
		}
	}
}
