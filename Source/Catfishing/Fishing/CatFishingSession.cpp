#include "Fishing/CatFishingSession.h"

#include "Character/CatCharacter.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "Logging/CatLog.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Config/CatAbilitySettings.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "Collection/CatRunImprintService.h"
#include "Data/CatFishDefinition.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishPersonalityDefinition.h"
#include "Environment/CatChumFieldSubsystem.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Fishing/Actors/CatFishEncounterActor.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingGameplayTags.h"
#include "Fishing/Presentation/CatFishingPresentationSettings.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/Integration/CatFishingAimLibrary.h"
#include "Fishing/Simulation/CatFishFightMotionSolver.h"
#include "Fishing/Simulation/CatFishingFightRunner.h"
#include "Fishing/Actors/CatFishingHookActor.h"
#include "Components/StateTreeComponent.h"
#include "GameFramework/PlayerState.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Items/CatItemsService.h"
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

// 会话初始化流程：只接受 authority、显式 runtime gate、完整鱼定义/重量/水域/身份/鱼护与 Items；全部就绪后设置资产并启动 StateTree，失败销毁由服务负责。
bool ACatFishingSession::InitializeSession(const FGuid InFishingSessionId, const FGuid InCastAttemptId,
	AController* FisherController, ACatCharacter* InFisherCharacter, UCatFishDefinition* InFishDefinition,
	const FGuid InFisherGuardContainerId, const double InFishWeightKilograms, const FCatWaterRegionHandle& WaterRegion)
{
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	// StateTree 资产同步加载：Initialize 只在 authority 服务器一次性调用，允许短暂同步等待换取代码简单。
	UStateTree* StateTreeAsset = Settings ? Settings->FishingSessionStateTree.LoadSynchronous() : nullptr;
	const FString StableNetId = ResolveStableNetId(FisherController);
	UCatItemsService* Items = GetWorld() ? GetWorld()->GetSubsystem<UCatItemsService>() : nullptr;
	// 一次性 fail-closed 校验所有前置依赖：任何一项缺失/非法都拒绝启动会话，不留半初始化状态。
	if (!HasAuthority() || !Settings || !Settings->IsRuntimeReady() || !StateTreeAsset || !StateTreeComponent
		|| !InFishingSessionId.IsValid() || !InCastAttemptId.IsValid() || InFishingSessionId == InCastAttemptId
		|| !InFisherCharacter || !InFishDefinition || !InFishDefinition->IsRuntimeDefinitionReady()
		|| StableNetId.IsEmpty() || !InFisherGuardContainerId.IsValid()
		|| !FMath::IsFinite(InFishWeightKilograms) || InFishWeightKilograms <= 0.0
		|| !WaterRegion.IsValid() || !Items)
	{
		return false;
	}

	// 逐字段填充公开 Snapshot 的初始事实；Revision/PhaseEpoch 从 1 起，SnapshotSequence 从 0 起，
	// 与 FCatFishingSessionSnapshot::AdvanceVersion 的自增语义保持一致。
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
	Snapshot.FightParticipantCount = 1; // 初始只有钓手一人。
	Snapshot.FishFightStaminaRemaining = InFishDefinition->FishFightStamina; // 从鱼定义冻结初始体力。
	Snapshot.NormalizedFishStamina = InFishDefinition->FishFightStamina > 0.0
		? FMath::Clamp(Snapshot.FishFightStaminaRemaining / InFishDefinition->FishFightStamina, 0.0, 1.0) : 0.0;
	FishDefinition = InFishDefinition; // 私有引用，不复制，仅服务器读取校验/捕获时使用。
	FisherCharacter = InFisherCharacter;
	CastEquipment = InFisherCharacter->GetEquipmentComponent(); // 冻结原始抛竿者装备：饵料/磨损结算口径不随接力改变。
	FisherStableNetId = StableNetId;
	FisherGuardContainerId = InFisherGuardContainerId;
	FishWeightKilograms = InFishWeightKilograms;
	AttemptSnapshot.WaterRegion = WaterRegion;
	FightParticipantIds.Add(StableNetId); // 钓手自动是首个（也是初始唯一）搏斗参与者。
	FightParticipantCharacters.Add(StableNetId, InFisherCharacter);
	RefreshFightSummary(); // 立即按当前参与集合刷新一次力量/体力聚合，避免快照与实际不一致。
	ItemsService = Items;
	PublishSnapshot(ECatFishingSnapshotMutation::HighFrequency); // 先把初始状态推给客户端，再启动 StateTree。
	StateTreeComponent->SetStateTree(StateTreeAsset);
	bStartupInProgress = true; // 打开短生命周期标记，允许 StartLogic 同步进入首状态时调用 EnterPhaseFromStateTree。
	StateTreeComponent->StartLogic();
	bStartupInProgress = false;
	if (!StateTreeComponent->IsRunning() && Snapshot.Phase == ECatFishingPhase::Created)
	{
		// StateTree 没能跑起来且仍停留在初始阶段，说明资产没有成功接管——视为初始化失败。
		return false;
	}
	return true;
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
	Snapshot.Phase = NewPhase;
	Snapshot.PhaseStartedServerTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	// 巨鱼离开 HookedFight 后仍需把合法协作者带入成像候选；NearShore 只冻结参与事实，不再接受新的 assist。
	if (NewPhase != ECatFishingPhase::HookedFight && NewPhase != ECatFishingPhase::NearShore)
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
		|| Snapshot.CombinedFishingStrength < FishDefinition->FishStrength
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

// 钓手接力转移流程：仅 authority、未终态、等待/试探/真咬阶段可转移（搏斗/近岸阶段离开＝弃战，不存在转移场景）；
// 新钓手必须通过统一参战能力谓词且有合法个人鱼护。转移只换"人"（身份/Character/鱼护/参与集合/公开 PlayerState），
// 不换"资源"（CastEquipment 冻结在原始抛竿者身上：饵料预留、竿磨损、失败惩罚仍结算给竿主）。
bool ACatFishingSession::TransferFisherFromAuthority(AController* NewFisherController)
{
	const FString NewStableNetId = ResolveStableNetId(NewFisherController);
	FString ValidatedId;
	ACatCharacter* NewCharacter = nullptr;
	double NewStrength = 0.0;
	double NewStamina = 0.0;
	const bool bCapable = UCatFishingService::TryGetFightCapability(NewFisherController, ValidatedId, NewCharacter,
		NewStrength, NewStamina) && ValidatedId == NewStableNetId;
	const bool bTransferablePhase = Snapshot.Phase == ECatFishingPhase::Waiting
		|| Snapshot.Phase == ECatFishingPhase::Probe || Snapshot.Phase == ECatFishingPhase::TrueBiteWindow;
	if (!HasAuthority() || IsTerminal() || !bTransferablePhase || NewStableNetId.IsEmpty() || !bCapable
		|| !NewCharacter || !NewFisherController->PlayerState)
	{
		return false;
	}
	if (NewStableNetId == FisherStableNetId)
	{
		return true; // 同一钓手重复接管：幂等成功。
	}
	const FGuid NewGuardContainerId = NewCharacter->GetPersonalFishGuardId();
	if (!NewGuardContainerId.IsValid())
	{
		return false;
	}
	FightParticipantIds.Remove(FisherStableNetId);
	FightParticipantCharacters.Remove(FisherStableNetId);
	FisherStableNetId = NewStableNetId;
	FisherCharacter = NewCharacter;
	FisherGuardContainerId = NewGuardContainerId;
	Snapshot.FisherPlayerState = NewFisherController->PlayerState;
	FightParticipantIds.Add(NewStableNetId);
	FightParticipantCharacters.Add(NewStableNetId, NewCharacter);
	RefreshFightSummary();
	PublishSnapshot(ECatFishingSnapshotMutation::Discrete); // 客户端 ViewBridge 按 FisherPlayerState 找会话，必须立即可见。
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_fisher_transferred SessionId=%s Phase=%s NewFisher=%s"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
		*UEnum::GetValueAsString(Snapshot.Phase), *NewStableNetId);
	return true;
}

// 抢抄流程：先按身份/RequestId 重放，再用统一参战能力谓词、NearShore/Revision、服务器目标与 reach 拒绝不合法命令；随后重验巨鱼 HookedFight 参与者，只把仍 Active、未倒地且力量/体力为正的人与抄手去重后放入可选 Candidate。Items Compare-and-Commit 是实物唯一不可逆点，首个合法抄手独占鱼；FishRecorded 始终独立归档，只有鱼定义配置正式事件才提交 Candidate。批量计划接口先为全部参与者建齐并索引 Planned 记录，确认全量事实后才逐条投递，所以同步 RPC 回入不能留下部分计划，离线未投递仍按原 ID 重试；归档失败只记录且绝不回滚或复制实物鱼。最后写 Resolved、停树并启动有界复制窗口。
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
	double ScoopReachCentimeters = 0.0;
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	UCatWaterQuerySubsystem* Water = GetWorld() ? GetWorld()->GetSubsystem<UCatWaterQuerySubsystem>() : nullptr;
	ACatFishEncounterActor* Encounter = Snapshot.FishEncounterActor;
	UCatEquipmentComponent* ScooperEquipment = ScoopingCharacter ? ScoopingCharacter->GetEquipmentComponent() : nullptr;
	const FCatEquipmentLoadoutSnapshot* ScooperLoadout = ScooperEquipment ? &ScooperEquipment->GetSnapshot() : nullptr;
	// 抄手当前装备的抄网定义：必须是服务器权威的装备快照里查出来的，不接受客户端指定装备类型。
	const UCatEquipmentDefinition* ScoopDefinition = ScooperLoadout
		? GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(ScooperLoadout->ScoopNetDefinitionId) : nullptr;
	// 这里不再要求"鱼处于近岸带内"：射线∩圆本身就是唯一的范围判定，再叠一层离岸距离等于两套口径，
	// 会出现"圈画成绿色（够得着）但服务器因为鱼离岸 3.1 米而拒绝"这种表现与判定打架的情况。
	// 几何上也已经蕴含：抄手必须站在岸上，射线长度有限，所以能被抄到的鱼必然离岸不远。
	// 抄手自己相对岸线的空间关系：抢抄要求抄手站在岸上（Outside 水域），不能站在水里抄。
	const FCatWaterSpatialResult ScooperSpatial = Water && ScoopingCharacter && AttemptSnapshot.WaterRegion.IsValid()
		? Water->QueryShoreRelation(ScoopingCharacter->GetActorLocation(), AttemptSnapshot.WaterRegion)
		: FCatWaterSpatialResult{};
	// 抄网范围口径（与 debug 绘制同源）：抄手向正前方水平发射一条线段，与挂在鱼身上的圆相交即够得着。
	// 圆心随鱼移动、半径由鱼定义给（这条鱼有多好捞），线段长度由抄网装备给（网杆多长），两者互不耦合。
	const FVector ScooperFacing = ScoopingController
		? FVector(ScoopingController->GetControlRotation().Vector().X,
			ScoopingController->GetControlRotation().Vector().Y, 0.0).GetSafeNormal()
		: FVector::ZeroVector;
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
	// 终态缓存键=身份+RequestId：同一玩家对同一次抢抄请求的重复提交必须幂等重放，绝不重复执行 Compare-and-Commit。
	const FString CacheKey = FString::Printf(TEXT("%s|%s"), *StableNetId,
		*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	if (const FCatScoopResult* Cached = ScoopTerminalCache.Find(CacheKey))
	{
		// 命中缓存直接回放，且显式标记本次不是新提交，避免调用方误以为又成功抢到了一次。
		Result = *Cached;
		Result.Command.bCommitted = false;
		Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	if (bCaptureResolved)
	{
		// 捕获已经被某个更早的合法请求不可逆提交：这条鱼已经归属别人，本次直接拒绝。
		Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
	}
	else if (!Command.Context.RequestId.IsValid() || StableNetId.IsEmpty() || !Command.TargetGuardContainerId.IsValid())
	{
		// 请求 ID、身份或目标鱼护容器 ID 任一缺失都是非法负载。
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (Snapshot.Phase != ECatFishingPhase::HookedFight && Snapshot.Phase != ECatFishingPhase::NearShore)
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
	else if (!ItemsService.IsValid() || !FishDefinition || !Water || !Encounter || !AttemptSnapshot.WaterRegion.IsValid())
	{
		// 核心依赖缺失（Items 服务/鱼定义/水域子系统/鱼 Actor/水域句柄）是不可恢复的系统性故障，
		// 不只是拒绝这次请求，而是直接把整个会话判为失效并终止，避免留下无法继续推进的僵死会话。
		Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Invalidated,
			TEXT("Scoop system dependency unavailable"));
	}
	else if (!bScooperFightCapable || !ScoopingCharacter || !Settings
		|| !Settings->TryGetScoopReach(ScoopReachCentimeters)
		|| !ScoopDefinition || ScoopDefinition->Kind != ECatEquipmentKind::ScoopNet
		|| !ScoopDefinition->IsRuntimeDefinitionReady() || ScoopDefinition->ScoopReachCentimeters <= 0.0
		|| FishDefinition->ScoopTargetRadiusCentimeters <= 0.0
		|| !ScooperSpatial.bSucceeded || ScooperSpatial.Containment != ECatWaterContainment::Outside
		|| !UCatFishingAimLibrary::DoesScoopRayReachFish(ScoopingCharacter->GetActorLocation(), ScooperFacing,
			static_cast<float>(FMath::Min(ScoopReachCentimeters, ScoopDefinition->ScoopReachCentimeters)),
			Encounter->GetActorLocation(), static_cast<float>(FishDefinition->ScoopTargetRadiusCentimeters),
			static_cast<float>(Settings->MaximumScoopVerticalDeltaCentimeters))
		|| !bHasLineOfSight || !bValidGround)
	{
		// 汇总校验：抄手战斗能力/角色有效性/抢抄距离配置/抄网定义/鱼的可捞半径已裁/抄手在岸上/
		// 射线够到鱼圈/视线通畅/地面合法 —— 任一条件不满足都统一判为 PolicyUndecided（策略未满足）拒绝。
		Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
		// 逐项列出失败谓词：抢抄拒绝原因众多且此前完全静默，排查成本太高。
		// 额外打出水平距离与高度差的实测值：RayReachesFish=0 时光看谓词分不清是"没对准"、"太远"还是"站太高"。
		const bool bScoopDefReady = ScoopDefinition && ScoopDefinition->Kind == ECatEquipmentKind::ScoopNet
			&& ScoopDefinition->IsRuntimeDefinitionReady() && ScoopDefinition->ScoopReachCentimeters > 0.0;
		const double FishRadius = FishDefinition ? FishDefinition->ScoopTargetRadiusCentimeters : 0.0;
		const FVector FishLocation = Encounter ? Encounter->GetActorLocation() : FVector::ZeroVector;
		const FVector ScooperLocation = ScoopingCharacter ? ScoopingCharacter->GetActorLocation() : FVector::ZeroVector;
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=scoop_rejected SessionId=%s Phase=%s FightCapable=%d Character=%d ScoopDef=%d "
				"FishRadiusSet=%d ScooperOnLand=%d RayReachesFish=%d LineOfSight=%d ValidGround=%d "
				"HorizontalDistanceCm=%.1f VerticalDeltaCm=%.1f ReachCm=%.1f RadiusCm=%.1f"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			*UEnum::GetValueAsString(Snapshot.Phase),
			bScooperFightCapable ? 1 : 0, ScoopingCharacter ? 1 : 0, bScoopDefReady ? 1 : 0,
			FishRadius > 0.0 ? 1 : 0,
			ScooperSpatial.bSucceeded && ScooperSpatial.Containment == ECatWaterContainment::Outside ? 1 : 0,
			bScoopDefReady && ScoopingCharacter && Settings
				&& UCatFishingAimLibrary::DoesScoopRayReachFish(ScooperLocation, ScooperFacing,
					static_cast<float>(FMath::Min(ScoopReachCentimeters, ScoopDefinition->ScoopReachCentimeters)),
					FishLocation, static_cast<float>(FishRadius),
					static_cast<float>(Settings->MaximumScoopVerticalDeltaCentimeters)) ? 1 : 0,
			bHasLineOfSight ? 1 : 0, bValidGround ? 1 : 0,
			FVector::Dist2D(ScooperLocation, FishLocation), FMath::Abs(FishLocation.Z - ScooperLocation.Z),
			bScoopDefReady ? FMath::Min(ScoopReachCentimeters, ScoopDefinition->ScoopReachCentimeters) : 0.0,
			FishRadius);
	}
	else
	{
		// 所有前置校验都通过：进入真正的抢抄事务流程。
		FCatContainerSnapshot GuardSnapshot;
		if (!ItemsService->TryGetContainerSnapshot(Command.TargetGuardContainerId, GuardSnapshot))
		{
			// 客户端提交的目标鱼护容器 ID 在 Items 服务里查不到，拒绝。
			Result.Command.Error = ECatDomainCommandError::NotFound;
		}
		else
		{
			UCatRunImprintService* ImprintService = GetWorld() ? GetWorld()->GetSubsystem<UCatRunImprintService>() : nullptr;
			const ACatfishingGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
			// 先拟定一个鱼实例 ID（尚未提交）；真正的 FishInstanceId 会在 Items 提交成功后从结果里取。
			const FGuid ProposedFishInstanceId = FGuid::NewGuid();
			const bool bHasCaptureImprintEvent = !FishDefinition->CaptureImprintEventId.IsNone();
			// 预检用的图鉴候选：只用于在真正提交前判断"这次捕获是否满足图鉴事件的前置条件"（如全员在场），
			// 不代表最终归档内容——提交成功后会用真实的 FishInstance 数据重新构造一份。
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
				// 巨鱼：把仍然合法（Active/未倒地/力量体力为正）的 HookedFight 协作者都计入图鉴候选参与者，
				// 即便实物最终只归抄手一人所有。
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
			CaptureParticipants.Add(StableNetId); // 抄手本人必定在参与者集合内。
			CaptureCandidate.ParticipantStableNetIds = CaptureParticipants.Array();
			CaptureCandidate.ParticipantStableNetIds.Sort(); // 排序保证参与者数组内容与顺序确定，便于比较/去重。
			CaptureCandidate.ParticipantCount = CaptureCandidate.ParticipantStableNetIds.Num();
			// FishRecorded 是实物捕获的必需永久事实；CapturePlan 只在鱼定义提供正式事件时预检，None 不得反向关闭捕获链。
			if (!ImprintService || !ImprintService->CanRecordCommittedCapture()
				|| (bHasCaptureImprintEvent
					&& (!GameState || !ImprintService->CanAcceptImprintCandidate(CaptureCandidate))))
			{
				// 归档依赖不可用：宁可整个会话失效终止，也不允许在无法记录永久事实的情况下发放实物鱼。
				Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
				FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Invalidated,
					TEXT("Capture run or imprint dependency unavailable"));
				Result.Command.Revision = Snapshot.Revision;
				ScoopTerminalCache.Add(CacheKey, Result);
				return Result;
			}
			// 组装提交给 Items 服务的捕获命令：容器 Revision 用刚读到的 GuardSnapshot.Revision（Compare-and-Commit 的比较基准）。
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
			UCatEquipmentComponent* FisherEquipment = CastEquipment.Get(); // 饵料/磨损终结算走抛竿者装备（接力后不变）。
			// 在真正提交捕获前，先把这场钓鱼此前"延迟"记账的饵料消耗和鱼竿磨损最终结算掉
			// （之前阶段只是预留/暂记，这里才是不可逆的最终扣除）；AlreadyResolved 也算作已经最终化。
			const FCatFishingUseOperationResult BaitFinal = FisherEquipment
				? FisherEquipment->CommitFishingBaitDeferred(Snapshot.FishingSessionId) : FCatFishingUseOperationResult{};
			const bool bBaitFinal = BaitFinal.bApplied || BaitFinal.Error == ECatDomainCommandError::AlreadyResolved;
			const FCatFishingUseOperationResult WearFinal = FisherEquipment
				? FisherEquipment->CommitFishingRodWear(Snapshot.FishingSessionId) : FCatFishingUseOperationResult{};
			const bool bWearFinal = WearFinal.bApplied || WearFinal.Error == ECatDomainCommandError::AlreadyResolved;
			if (!bBaitFinal || !bWearFinal)
			{
				// 装备结算失败：同样属于不可恢复的一致性问题，直接判定会话失效。
				Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
				FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Invalidated,
					TEXT("Capture equipment finalization failed"));
				Result.Command.Revision = Snapshot.Revision;
				ScoopTerminalCache.Add(CacheKey, Result);
				return Result;
			}
			// 唯一不可逆的实物提交点：Items 服务内部用 Compare-and-Commit 保证并发抢抄时只有第一个到达的请求真正生效。
			const FCatCaptureCommitResult CaptureResult = ItemsService->CommitCapture(CaptureCommand);
			Result.Command = CaptureResult.Command;
			Result.Capture = CaptureResult.Committed;
			// "已提交"既包括本次真正提交成功，也包括命中 Items 侧自己的幂等重放（AlreadyResolved）。
			const bool bHasCommittedCapture = CaptureResult.Command.bCommitted
				|| CaptureResult.Command.Error == ECatDomainCommandError::AlreadyResolved;
			if (bHasCommittedCapture && IsCommittedCaptureForCurrentSession(CaptureResult.Committed))
			{
				// 提交结果确实归属本会话（防御 Items 侧异常返回了别的会话的数据）：走成功归档+会话收尾流程。
				// 用 Items 真正返回的 FishInstance 数据重建一份"已提交"图鉴候选，
				// 取代之前用 ProposedFishInstanceId 拟定的预检版本。
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
				// 用 Items 提交结果里真正的 Owner（即抢抄成功的抄手）替代候选参与者集合中的占位身份。
				CommittedCaptureParticipants.Add(CaptureResult.Committed.FishInstance.OwnerStableNetId);
				CommittedCaptureCandidate.ParticipantStableNetIds = CommittedCaptureParticipants.Array();
				CommittedCaptureCandidate.ParticipantStableNetIds.Sort();
				CommittedCaptureCandidate.ParticipantCount = CommittedCaptureCandidate.ParticipantStableNetIds.Num();
				FCatCaptureConditionSnapshot Condition;
				Condition.RegionId = AttemptSnapshot.WaterRegion.RegionId;
				// FishRecorded：无条件记录一条永久归档事实（谁在哪个水域钓到了哪条鱼），与图鉴事件是否配置无关。
				const FGuid FishRecordedGrantId = ImprintService->RecordCommittedCapture(
					CaptureResult.Committed, CaptureResult.Committed.FishInstance.OwnerStableNetId, Condition);
				bool bOptionalPlanCommitted = true;
				if (bHasCaptureImprintEvent)
				{
					// 巨鱼候选包含 HookedFight 的合法钓手/协作者以及最终抄手；实物归属仍只来自 Items 的首个近岸 Compare-and-Commit。
					// CapturePlan（图鉴成像计划）是可选的：只有鱼定义配置了正式图鉴事件时才需要先为全体参与者建齐 Planned 记录。
					bOptionalPlanCommitted = ImprintService->SubmitImprintCandidate(CommittedCaptureCandidate);
					TArray<FCatCapturePlan> CapturePlans;
					bOptionalPlanCommitted = bOptionalPlanCommitted
						&& ImprintService->CreateCapturePlansForParticipants(CommittedCaptureCandidate.CandidateId,
							CommittedCaptureCandidate.ParticipantStableNetIds, false, CapturePlans);
				}
				if (!FishRecordedGrantId.IsValid() || !bOptionalPlanCommitted)
				{
					// 归档失败只记录日志，绝不回滚实物或阻止会话收尾——实物鱼已经不可逆地进了鱼护，
					// 归档只是"锦上添花"的记录，不能反过来卡住已经成立的捕获事实。
					UE_LOG(LogCatFishing, Error,
						TEXT("Event=fishing_capture_archive_commit_failed SessionId=%s RequestId=%s OptionalPlanValid=%s FishRecordedGrantValid=%s"),
						*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
						*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
						bOptionalPlanCommitted ? TEXT("true") : TEXT("false"),
						FishRecordedGrantId.IsValid() ? TEXT("true") : TEXT("false"));
				}
				// 无论归档是否成功，只要实物捕获已提交，就把会话收敛为 Resolved/Caught 终态。
				if (ReconcileCommittedCapture(CaptureResult.Committed))
				{
					Result.Command.Revision = Snapshot.Revision;
				}
			}
			else if (bHasCommittedCapture)
			{
				// Items 说"已提交"，但提交结果的会话/鱼种/身份等字段对不上本会话——出现了不该出现的不一致，
				// 判定为系统性异常，终止会话而不是继续信任这份数据。
				FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Invalidated,
					TEXT("Committed capture did not match session"));
			}
			else if (CaptureResult.Command.Error != ECatDomainCommandError::CapacityExceeded
				&& CaptureResult.Command.Error != ECatDomainCommandError::RevisionConflict
				&& CaptureResult.Command.Error != ECatDomainCommandError::NotFound)
			{
				// 容量已满/Revision 冲突/容器找不到属于"正常的抢抄失败"（比如慢了一步、鱼护满了），
				// 允许后续重试；除此之外的提交失败视为异常情况，直接 fail-closed 终止会话。
				FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Invalidated,
					TEXT("Capture commit failed closed"));
			}
		}
	}
	Result.Command.Revision = Snapshot.Revision;
	ScoopTerminalCache.Add(CacheKey, Result); // 无论成功失败都写入终态缓存，保证后续重放幂等。
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_scoop_terminal SessionId=%s RequestId=%s Committed=%s Error=%s Revision=%lld"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
		*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		Result.Command.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Command.Error), Snapshot.Revision);
	return Result;
}

// 会话终止流程：非 authority、已 Resolved 或已 Terminated 直接幂等返回，避免覆盖捕获终态。首次中断只写一次 Terminated/Revision 并发布快照，再停止 StateTree、释放钓手之外的参与弱引用且不触碰 Items；最后启动配置的有界复制窗口，让客户端看见终态后销毁 Actor，服务据此释放钓手的单活跃槽位。
void ACatFishingSession::TerminateSession(const ECatFishingOutcome Outcome, const TCHAR* DiagnosticReason)
{
	// 只白名单允许"非捕获类"终止结果才真正写终态；Caught/None 等结果不属于这条路径
	// （捕获成功走 ReconcileCommittedCapture -> FinalizeSession(Resolved, Caught) 那条独立路径），
	// 防止调用方误用本函数覆盖掉已经成立的捕获终态。
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
	// Fish identity remains deliberately empty until Probe/TrueBite commits selection.
	FisherCharacter = InFisherCharacter;
	CastEquipment = InFisherCharacter->GetEquipmentComponent(); // 冻结原始抛竿者装备：饵料/磨损结算口径不随接力改变。
	FisherStableNetId = StableNetId;
	FisherGuardContainerId = InFisherCharacter->GetPersonalFishGuardId();
	FightParticipantIds.Add(StableNetId);
	FightParticipantCharacters.Add(StableNetId, InFisherCharacter);
	ItemsService = GetWorld() ? GetWorld()->GetSubsystem<UCatItemsService>() : nullptr;
	// 只有 Items 服务与钓手个人鱼护 ID 都齐备才算真正"准备就绪"，否则整体视为失败。
	bPrepared = ItemsService.IsValid() && FisherGuardContainerId.IsValid();
	return bPrepared;
}

bool ACatFishingSession::ScheduleWaitingProbeFromStateTree()
{
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	if (!HasAuthority() || !bPrepared || IsTerminal() || !Settings
		|| !FMath::IsFinite(Settings->BaseBiteRatePerSecond) || Settings->BaseBiteRatePerSecond <= 0.0
		|| !FMath::IsFinite(Settings->MinimumBiteDelaySeconds) || Settings->MinimumBiteDelaySeconds < 0.0
		|| !FMath::IsFinite(Settings->MaximumBiteDelaySeconds)
		|| Settings->MaximumBiteDelaySeconds < Settings->MinimumBiteDelaySeconds)
	{
		return false;
	}
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
	// 用服务器种子生成确定性随机数，再按泊松过程的逆变换采样法算出下一次咬钩的延迟，
	// 最后夹在 [MinimumDelay, MaximumBiteDelaySeconds] 之间，避免极端值导致体验异常。
	FRandomStream Random(static_cast<int32>(AttemptSnapshot.ServerRandomSeed));
	const double Unit = FMath::Clamp(static_cast<double>(Random.FRand()), UE_DOUBLE_SMALL_NUMBER, 1.0 - UE_DOUBLE_SMALL_NUMBER);
	const double PoissonDelay = -FMath::Loge(1.0 - Unit) / BiteRate;
	const double Delay = FMath::Clamp(PoissonDelay, MinimumDelay, Settings->MaximumBiteDelaySeconds);
	GetWorldTimerManager().SetTimer(ProbeTimerHandle, this, &ThisClass::HandleProbeTimer, Delay, false);
	return true;
}

void ACatFishingSession::HandleProbeTimer()
{
	// 计时器到期：只有仍处于 Waiting 阶段才把"试探触发"事件送进 StateTree，
	// 阶段已经变化（比如提前被取消/提竿）则说明这次触发已经过期，直接忽略。
	if (!HasAuthority() || IsTerminal() || Snapshot.Phase != ECatFishingPhase::Waiting || !StateTreeComponent) return;
	StateTreeComponent->SendStateTreeEvent(CatFishingGameplayTags::ProbeTriggered, FConstStructView(), TEXT("CatFishing"));
}

FCatFishSelectionCommitResult ACatFishingSession::ResolveProbeSelectionFromStateTree()
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
	if (!HasAuthority() || IsTerminal() || Snapshot.Phase != ECatFishingPhase::Probe
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
	UCatEquipmentComponent* Equipment = CastEquipment.Get(); // 饵料预留在抛竿者装备上，选鱼/扣饵必须用同一组件。
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
	FrozenSelectionContext.RandomSeed = static_cast<int32>(AttemptSnapshot.ServerRandomSeed);
	const UCatFishCatalogSettings* Catalog = GetDefault<UCatFishCatalogSettings>();
	// 按冻结上下文从鱼类图鉴中选出本次的鱼种（含权重/稀有度/条件判定，具体算法在 Catalog 内部）。
	FrozenSelectionResult = Catalog->SelectRuntimeDefinition(FrozenSelectionContext);
	UCatFishDefinition* SelectedDefinition = FrozenSelectionResult.bSelected
		? Catalog->FindRuntimeDefinition(FrozenSelectionResult.FishDefinitionId) : nullptr;
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
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
		SelectedDefinition->FishDefinitionId, InitialLineLength))
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
		|| !Encounter->GetActorLocation().Equals(FishLocation, 1.0))
	{
		if (IsValid(Encounter)) Encounter->Destroy();
		SelectionResolution = ECatFishSelectionResolution::Failed;
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Invalidated, TEXT("Fish construction changed authority state"));
		Result.Resolution = SelectionResolution;
		return Result;
	}
	// 到这里才真正扣除（延迟结算的）饵料消耗；扣除失败就销毁鱼并终止，不留下"鱼已生成但饵未扣"的不一致状态。
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
	Snapshot.FishDefinitionId = SelectedDefinition->FishDefinitionId;
	Snapshot.bGiant = SelectedDefinition->BodyClass == ECatFishBodyClass::Giant;
	Snapshot.FishFightStaminaRemaining = SelectedDefinition->FishFightStamina;
	Snapshot.NormalizedFishStamina = 1.0;
	Snapshot.FishEncounterActor = Encounter;
	Snapshot.Phase = ECatFishingPhase::TrueBiteWindow;
	Snapshot.PhaseStartedServerTime = World->GetTimeSeconds();
	Snapshot.WindowEndsServerTime = Snapshot.PhaseStartedServerTime + Bite->TrueBiteWindowSeconds;
	SelectionResolution = ECatFishSelectionResolution::Selected;
	Equipment->PublishDeferredFishingBait(Snapshot.FishingSessionId); // 饵料扣除结果对外发布。
	Encounter->PublishInitialPresentationFromAuthority(); // 放行此前延迟的鱼表现首次通知。
	PublishSnapshot(ECatFishingSnapshotMutation::PhaseChange);
	// 真咬窗口倒计时：窗口内没有 RequestHookFromAuthority 就会走 HandleTrueBiteWindowExpired 判空军。
	GetWorldTimerManager().SetTimer(TrueBiteTimerHandle, this, &ThisClass::HandleTrueBiteWindowExpired,
		Bite->TrueBiteWindowSeconds, false);
	Result.Resolution = SelectionResolution;
	Result.FishDefinitionId = SelectedDefinition->FishDefinitionId;
	Result.Error = ECatDomainCommandError::None;
	return Result;
}

void ACatFishingSession::HandleTrueBiteWindowExpired()
{
	// 计时器到期时仍处于 TrueBiteWindow 才判定为"窗口过期未提竿"；
	// 阶段已变化（提前提竿/取消）说明这次到期通知已经过期，忽略。
	if (HasAuthority() && !IsTerminal() && Snapshot.Phase == ECatFishingPhase::TrueBiteWindow)
	{
		if (StateTreeComponent) StateTreeComponent->SendStateTreeEvent(CatFishingGameplayTags::WindowExpired,
			FConstStructView(), TEXT("CatFishing"));
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::HookWindowExpired, TEXT("True bite window expired"));
	}
}

bool ACatFishingSession::TryEnterHookedFightFromAuthority()
{
	// Runner 已在跑：幂等返回"当前是否确实处于 HookedFight"，不重复初始化搏斗。
	if (FightRunner && FightRunner->IsRunning()) return Snapshot.Phase == ECatFishingPhase::HookedFight;
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	const UCatFightPersonalityDefinition* Personality = FishDefinition && Settings
		? Settings->FindFightPersonality(FishDefinition->FightPersonalityId) : nullptr;
	const UCatEquipmentDefinition* RodDefinition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(
		AttemptSnapshot.RodDefinitionId);
	UCatEquipmentComponent* Equipment = CastEquipment.Get(); // 钓鱼用途预留/竿磨损在抛竿者装备上（接力后仍是竿主的）。
	UCatAbilitySystemComponent* AbilitySystem = FisherCharacter.IsValid()
		? FisherCharacter->GetCatAbilitySystemComponent() : nullptr;
	ACatFishEncounterActor* Encounter = Snapshot.FishEncounterActor;
	ACatFishingRodActor* Rod = AttemptSnapshot.RodActor;
	UCatWaterQuerySubsystem* Water = GetWorld() ? GetWorld()->GetSubsystem<UCatWaterQuerySubsystem>() : nullptr;
	// 一次性 fail-closed 校验所有搏斗启动前置依赖：阶段必须是 TrueBiteWindow、鱼种已选定、
	// 性格/鱼竿定义齐全且就绪、钓鱼用途处于激活态、ASC/鱼/竿/水域子系统全部有效。
	if (!HasAuthority() || IsTerminal() || Snapshot.Phase != ECatFishingPhase::TrueBiteWindow
		|| SelectionResolution != ECatFishSelectionResolution::Selected || !Settings || !Personality
		|| !Personality->IsRuntimeDefinitionReady() || !RodDefinition || !RodDefinition->IsRuntimeDefinitionReady()
		|| !Equipment || !Equipment->IsFishingUseActive(Snapshot.FishingSessionId) || !AbilitySystem
		|| !Encounter || !Rod || !Water || !AttemptSnapshot.WaterRegion.IsValid())
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

	const FCatEquipmentLoadoutSnapshot& EquipmentSnapshot = Equipment->GetSnapshot();
	if (EquipmentSnapshot.RodDefinitionId != AttemptSnapshot.RodDefinitionId
		|| !FMath::IsFinite(EquipmentSnapshot.RodDurability) || EquipmentSnapshot.RodDurability <= 0.0)
	{
		return false;
	}

	// 规格 4.2 三方力量：猫力量 = ASC FishingStrength；鱼力量 = 鱼种 FishStrength（含完美折减）；竿强度 = 鱼竿定义 FishingStrength（静态）。
	// 下面把服务器设置、鱼竿/鱼定义、性格模板的各项参数一次性打包进模拟配置结构体，交给 FightRunner/Simulator 使用。
	FCatFightSimulationConfig Config;
	Config.FixedStepSeconds = Settings->FixedFightStepSeconds; // 固定步长模拟，保证服务器权威结果确定可复现。
	Config.CatStrength = AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetFishingStrengthAttribute());
	Config.FishStrength = FishDefinition->FishStrength * FishStrengthScale; // 完美中鱼可能折减鱼的力量。
	Config.RodStrength = RodDefinition->FishingStrength;
	Config.CatStaminaMaximum = CatStaminaBaseline;
	Config.InwardPullCatDrainPerFishStrength = Settings->InwardPullCatDrainPerFishStrength;
	Config.InwardPullFishDrainPerCatStrength = Settings->InwardPullFishDrainPerCatStrength;
	Config.StalemateRodWearPerFishStrength = Settings->StalemateRodWearPerFishStrength;
	Config.StalemateFishDrainPerCatStrength = Settings->StalemateFishDrainPerCatStrength;
	Config.StalemateCatDrainPerFishStrength = Settings->StalemateCatDrainPerFishStrength;
	Config.SlackStaminaRegenPerSecond = Settings->SlackStaminaRegenPerSecond;
	Config.OverpowerStrengthRatio = Settings->OverpowerStrengthRatio;
	Config.ReelSpeedCentimetersPerSecond = Settings->ReelSpeedCentimetersPerSecond;
	Config.FishCalmSpeedCentimetersPerSecond = Personality->CalmMovementSpeedCentimetersPerSecond;
	Config.FishStruggleSpeedCentimetersPerSecond = Personality->StruggleMovementSpeedCentimetersPerSecond;
	Config.MaximumLineLengthCentimeters = RodDefinition->MaximumLineLengthCentimeters;
	// 竿耐久以 Equipment 当前快照为权威；这里不从 DA 最大值重置，避免维修、磨损或断竿状态被下一场搏斗悄悄抹掉。
	Config.RodDurability = EquipmentSnapshot.RodDurability;
	Config.StruggleHoldRodWearPerSecond = RodDefinition->BaseDurabilityWearPerSecond;
	Config.TautRodWearMultiplier = FMath::Max(1.0, RodDefinition->HighTensionWearMultiplier);
	Config.EscapeSlackCentimeters = Settings->EscapeSlackCentimeters;
	Config.NearShoreLineLengthCentimeters = Settings->NearShoreLineLengthCentimeters;
	if (!Config.IsValid()) return false; // 配置自检（如任何数值非有限/非法组合）未通过则拒绝启动搏斗。

	// 组装搏斗模拟的初始状态：猫当前体力从 ASC 读，鱼体力/初始线长按完美中鱼折减系数缩放。
	FCatFightSimulationState InitialState;
	InitialState.CatStamina = AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	InitialState.FishStamina = Snapshot.FishFightStaminaRemaining * FishStaminaScale;
	InitialState.LineLengthCentimeters = Encounter->GetPresentationState().CurrentLineLength * LineLengthScale;
	InitialState.FishWorldPosition = Encounter->GetActorLocation();
	InitialState.MotionIntent = ECatFishMotionIntent::StrugglingOutward; // 刚上钩默认视为鱼在向外挣扎。
	InitialState.CatAction = ECatFightCatAction::None;
	Snapshot.FishFightStaminaRemaining = InitialState.FishStamina; // 把折减后的体力写回公开快照。
	Snapshot.RodDurabilityRemaining = Config.RodDurability;
	// 冻结一个以落点为中心、半径=最大线长的世界包围盒，作为本场搏斗全程鱼的运动边界（防止鱼被算出界外）。
	const FVector Landing = AttemptSnapshot.ServerCorrectedLandingWorldPoint;
	const FVector HalfExtent(Config.MaximumLineLengthCentimeters, Config.MaximumLineLengthCentimeters,
		FMath::Max(500.0, Config.MaximumLineLengthCentimeters * 0.25));
	const FBox FrozenBounds = FBox::BuildAABB(Landing, HalfExtent);
	// 用运动求解器把鱼的初始位置投影到合法范围内（尊重最大线长、水域边界），得到一个几何上自洽的起始点。
	FCatFishMotionSolveInput ProjectionInput;
	ProjectionInput.RodTipWorldPosition = Rod->GetRodTipWorldTransform().GetLocation();
	ProjectionInput.ProposedFishWorldPosition = InitialState.FishWorldPosition;
	ProjectionInput.WaterBounds = FrozenBounds;
	ProjectionInput.MaximumLineLengthCentimeters = Config.MaximumLineLengthCentimeters;
	const FCatFishMotionSolveResult Projected = FCatFishFightMotionSolver::Solve(ProjectionInput);
	// 再用水域子系统把投影点精确吸附到水面上，得到最终的权威落点。
	const FCatWaterSpatialResult Exact = Projected.bSucceeded
		? Water->ResolveCandidatePointToWater(Projected.FishWorldPosition, AttemptSnapshot.WaterRegion)
		: FCatWaterSpatialResult{};
	if (!Projected.bSucceeded || !Exact.bSucceeded
		|| !Encounter->ApplyFightStepFromAuthority(ECatFishMotionIntent::StrugglingOutward,
			InitialState.LineLengthCentimeters, Exact.WaterSurfaceWorldPoint))
	{
		// 求解/吸附/表现应用任一环节失败：回滚已初始化的体力属性，不进入搏斗。
		AbilitySystem->RequestFishingStaminaReset();
		return false;
	}
	InitialState.FishWorldPosition = Encounter->GetActorLocation(); // 用刚落位的实际权威位置覆盖，作为 Runner 的真正起点。

	// 组装 FightRunner 的初始化参数：把 Session/Actor 引用、模拟配置/初始状态、性格模板节奏参数、
	// 低体力歇息倍率、以及本场搏斗用的随机种子一并交给它，随后驱动固定步长的搏斗推进。
	FCatFishingFightRunnerInit Init;
	Init.Session = this;
	Init.FishActor = Encounter;
	Init.RodActor = Rod;
	Init.AbilitySystem = AbilitySystem;
	Init.Equipment = Equipment;
	Init.FishingSessionId = Snapshot.FishingSessionId;
	Init.WaterRegion = AttemptSnapshot.WaterRegion;
	Init.FrozenWaterBounds = FrozenBounds;
	Init.Config = Config;
	Init.InitialState = InitialState;
	Init.CalmDurationRangeSeconds = Personality->CalmDurationRangeSeconds;
	Init.StruggleDurationRangeSeconds = Personality->StruggleDurationRangeSeconds;
	Init.LowStaminaRestThreshold = Settings->LowStaminaRestThreshold;
	Init.LowStaminaRestMultiplier = Settings->LowStaminaRestMultiplier;
	// 优先用抛竿时锁定的服务器随机种子，保证同一次抛竿的搏斗结果确定可复现；缺省时退化为按会话 ID 派生一个。
	Init.RandomSeed = AttemptSnapshot.ServerRandomSeed != 0
		? AttemptSnapshot.ServerRandomSeed : static_cast<uint64>(GetTypeHash(Snapshot.FishingSessionId));
	FightRunner = NewObject<UCatFishingFightRunner>(this);
	if (!FightRunner || !FightRunner->InitializeFromAuthority(Init) || !FightRunner->Start())
	{
		// Runner 创建/初始化/启动任一步失败：清空引用并回滚体力，不留下半启动的 Runner。
		FightRunner = nullptr;
		AbilitySystem->RequestFishingStaminaReset();
		return false;
	}
	bFightStaminaInitialized = true;
	StaminaParticipantsTouched.Add(FisherCharacter);
	if (!EnterPhaseFromStateTree(ECatFishingPhase::HookedFight).bApplied)
	{
		// 阶段写入被拒绝（比如并发终止）：必须把已经启动的 Runner 和已初始化的体力状态全部回滚，
		// 否则会出现"Runner 在跑但阶段还停在 TrueBiteWindow"的不一致状态。
		FightRunner->Stop();
		FightRunner = nullptr;
		StaminaParticipantsTouched.Remove(FisherCharacter);
		bFightStaminaInitialized = false;
		AbilitySystem->RequestFishingStaminaReset();
		return false;
	}
	return true;
}

bool ACatFishingSession::SetReelingFromAuthority(const int64 InputSequence, const bool bReeling)
{
	// 收线只在运行中的 HookedFight 生效；InputSequence 由 FightRunner 内部做单调性/去抖校验。
	if (!HasAuthority() || Snapshot.Phase != ECatFishingPhase::HookedFight || !FightRunner
		|| !FightRunner->SetReeling(InputSequence, bReeling))
	{
		return false;
	}
	Snapshot.bReeling = bReeling; // 同步到公开快照供表现层读取（如收线动画/UI 提示）。
	PublishSnapshot(ECatFishingSnapshotMutation::HighFrequency); // 高频输入不推进离散 Revision，只更新 SnapshotSequence。
	return true;
}

bool ACatFishingSession::SetSlackingFromAuthority(const int64 InputSequence, const bool bSlacking)
{
	// 放线同样只在运行中的 HookedFight 生效；拖优先于放的仲裁逻辑在 FightRunner::SetSlacking 内部处理。
	if (!HasAuthority() || Snapshot.Phase != ECatFishingPhase::HookedFight || !FightRunner
		|| !FightRunner->SetSlacking(InputSequence, bSlacking))
	{
		return false;
	}
	Snapshot.bSlacking = bSlacking;
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
	// 只在仍处于运行中的 HookedFight 阶段才处理回调；阶段已经变化说明这是一次过期的 Runner 回调，直接忽略。
	if (!HasAuthority() || IsTerminal() || Snapshot.Phase != ECatFishingPhase::HookedFight || !FightRunner) return;
	Snapshot.FishFightStaminaRemaining = FMath::Max(0.0, FishStaminaRemaining); // 钳制非负，防止浮点误差产生负值。
	Snapshot.RodDurabilityRemaining = FMath::Max(0.0, RodDurabilityRemaining);
	// 钩在鱼嘴里：搏斗期间钩 Actor 跟随鱼的权威位置（含近岸/贴岸吸附后的落点），复制到所有端。
	if (Snapshot.HookActor && Snapshot.FishEncounterActor)
	{
		Snapshot.HookActor->SetActorLocation(Snapshot.FishEncounterActor->GetActorLocation());
	}
	Snapshot.NormalizedFishStamina = FishDefinition && FishDefinition->FishFightStamina > 0.0
		? FMath::Clamp(Snapshot.FishFightStaminaRemaining / FishDefinition->FishFightStamina, 0.0, 1.0) : 0.0;
	Snapshot.FishMotionIntent = MotionIntent;
	RefreshFightSummary(); // 每步都重新校验参与者是否仍然合法在场（掉线/倒地会即时反映）。
	PublishSnapshot(ECatFishingSnapshotMutation::HighFrequency); // 搏斗数值每步都要尽快同步给客户端表现层。
	if (Step.Outcome == ECatFightStepOutcome::RodBroken)
	{
		// 断竿：先把竿耐久磨损结算为不可逆的装备事务，失败则整体判会话失效；提交对象必须是会话开始时冻结的抛竿者装备，不能因接力或 Pawn 变化转移。
		UCatEquipmentComponent* Equipment = CastEquipment.Get();
		if (!Equipment || !Equipment->CommitFishingRodBreak(Snapshot.FishingSessionId).bApplied)
		{
			FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Invalidated, TEXT("Rod break commit failed"));
			return;
		}
		if (Snapshot.RodActor)
		{
			// 同步鱼竿 Actor 的表现状态（断竿视觉），带上当前 Revision 防止过期覆盖。
			const int64 Revision = Snapshot.RodActor->GetPresentationState().RodActorRevision;
			Snapshot.RodActor->SetBrokenFromAuthority(true, Revision);
		}
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::RodBroken, TEXT("Rod broken"));
	}
	else if (Step.Outcome == ECatFightStepOutcome::CatStaminaExhausted
		|| Step.Outcome == ECatFightStepOutcome::DraggedIntoWater)
	{
		// 规格 4.3②/4.4：力量不足或体力归零都是「被拖下水，鱼逃」；救援（W3）尚未实现。
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::CatInWater,
			Step.Outcome == ECatFightStepOutcome::DraggedIntoWater ? TEXT("Fish overpowered cat") : TEXT("Cat fight stamina exhausted"));
	}
	else if (Step.Outcome == ECatFightStepOutcome::Escaped)
	{
		// 线放尽/张力超限等判定为鱼直接逃脱，无需先进入 NearShore。
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Escaped, TEXT("Fish escaped"));
	}
	else if (Step.Outcome == ECatFightStepOutcome::FishExhausted
		|| Step.Outcome == ECatFightStepOutcome::Overpowered
		|| Step.Outcome == ECatFightStepOutcome::NearShore)
	{
		// 翻肚 / 碾压 / 贴岸 三者当前都收敛到 NearShore 供抢抄；岸上态与 30s 苏醒见规格 5.3（TODO）。
		FightRunner->Stop(); // 搏斗模拟到此为止，交给 NearShore 阶段的抢抄流程接管。
		if (!EnterPhaseFromStateTree(ECatFishingPhase::NearShore).bApplied)
		{
			// 近岸阶段进入被拒绝（比如此刻鱼的权威位置其实已经不在近岸带内）：视为异常，终止会话。
			FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Invalidated,
				TEXT("Near-shore entry rejected after fight ended"));
		}
	}
}

void ACatFishingSession::HandleFightRunnerFailureFromAuthority()
{
	// FightRunner 自身遇到不可恢复的依赖失败（如引用失效）时回调本函数；只要会话还没结束就直接判为失效终止。
	if (HasAuthority() && !IsTerminal())
	{
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Invalidated, TEXT("Fight runner dependency failed"));
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
	else if (Snapshot.Phase == ECatFishingPhase::TrueBiteWindow && SelectionResolution == ECatFishSelectionResolution::Selected)
	{
		GetWorldTimerManager().ClearTimer(TrueBiteTimerHandle); // 提竿了，窗口过期计时器不再需要。
		// 完美中鱼（规格 4.1）：以服务器时间戳判定，真咬开始后 PerfectHookWindowSeconds 内提竿。
		const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
		const UCatBitePersonalityDefinition* Bite = FishDefinition && Settings
			? Settings->FindBitePersonality(FishDefinition->BitePersonalityId) : nullptr;
		const double SinceBite = GetWorld() ? GetWorld()->GetTimeSeconds() - Snapshot.PhaseStartedServerTime : TNumericLimits<double>::Max();
		// 服务器完全按自己的时间戳判定是否"完美"，不接受客户端上报的反应时间，杜绝作弊。
		Snapshot.bPerfectHook = Bite && FMath::IsFinite(SinceBite) && SinceBite >= 0.0
			&& SinceBite <= Bite->PerfectHookWindowSeconds;
		Result.bCommitted = TryEnterHookedFightFromAuthority(); // 真正的搏斗初始化在这里发生。
		if (Result.bCommitted && StateTreeComponent) StateTreeComponent->SendStateTreeEvent(
			CatFishingGameplayTags::HookAccepted, FConstStructView(), TEXT("CatFishing"));
		if (!Result.bCommitted)
		{
			// 提竿动作本身合法，但搏斗初始化失败（依赖缺失等）：视为系统性异常，终止整个会话。
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
	Snapshot.Phase = FinalPhase;
	Snapshot.Outcome = FinalOutcome;
	Snapshot.PhaseStartedServerTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	PublishSnapshot(ECatFishingSnapshotMutation::PhaseChange); // 终态属于阶段变化，必须递增 PhaseEpoch。
	if (FightRunner) FightRunner->Stop(); // 停止仍在跑的搏斗模拟，防止终态之后还有 Step 回调。
	GetWorldTimerManager().ClearTimer(ProbeTimerHandle);
	GetWorldTimerManager().ClearTimer(TrueBiteTimerHandle);
	// 释放抛竿者装备上的"正在使用钓具"占用标记（接力后仍是竿主的装备），允许其重新开始新的一轮钓鱼。
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
	if (FightRunner) FightRunner->Stop();
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

// 判断 Items 服务回传的"已提交捕获" DTO 是否真的对应当前这个会话——逐项核对请求 ID、会话 ID、鱼种、
// 归属身份、容器信息与重量/贡献值，任何一项缺失或不匹配都不可信任，防止把别的会话/别的鱼的提交结果误接进来。
bool ACatFishingSession::IsCommittedCaptureForCurrentSession(const FCatCaptureCommittedResult& Committed) const
{
	return Committed.CaptureRequestId.IsValid() && Committed.FishingSessionId == Snapshot.FishingSessionId // 捕获请求 ID 合法且属于本会话
		&& Committed.FishInstance.FishInstanceId.IsValid() && !Committed.FishInstance.FishDefinitionId.IsNone() // 鱼实例 ID 和鱼种定义都已生成
		&& Committed.FishInstance.FishDefinitionId == Snapshot.FishDefinitionId // 鱼种必须和本会话冻结的鱼种一致
		&& Committed.FishInstance.SourceFishingSessionId == Snapshot.FishingSessionId // 鱼实例记录的来源会话也必须是本会话（双重校验）
		&& !Committed.FishInstance.OwnerStableNetId.IsEmpty() && Committed.ContainerId.IsValid() // 归属身份和目标容器都已确定
		&& Committed.ContainerRevision > 0 && FMath::IsFinite(Committed.FishInstance.WeightKilograms) // 容器 Revision 有效、重量是合法数值
		&& Committed.FishInstance.WeightKilograms > 0.0 && Committed.FishInstance.SacrificeContribution > 0; // 重量和贡献值都必须为正
}

bool ACatFishingSession::ReconcileCommittedCapture(const FCatCaptureCommittedResult& Committed)
{
	// 非权威、DTO 校验不通过、或会话已经是终态，都不接受这次捕获对账。
	if (!HasAuthority() || !IsCommittedCaptureForCurrentSession(Committed) || IsTerminal())
	{
		return false;
	}
	bCaptureResolved = true; // 标记捕获已不可逆提交，后续所有抢抄请求都会被拒绝为 AlreadyResolved
	FinalizeSession(ECatFishingPhase::Resolved, ECatFishingOutcome::Caught, TEXT("Capture reconciled"));
	// 收尾校验：确认 FinalizeSession 真的把会话写成了预期的"已解决+捕获成功"终态，而不是被其他分支打断。
	return IsTerminal() && Snapshot.Phase == ECatFishingPhase::Resolved && Snapshot.Outcome == ECatFishingOutcome::Caught;
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
// 复用同一复制窗，让客户端在会话终态期间仍能看到落点/上岸的收尾表现。
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
	// 钩子立即销毁（收竿手感优先，延迟消失体感差）；鱼保留复制窗让客户端看完上岸/逃走的收尾表现。
	if (ACatFishingHookActor* Hook = Snapshot.HookActor)
	{
		Hook->Destroy();
	}
	if (ACatFishEncounterActor* Encounter = Snapshot.FishEncounterActor)
	{
		// 抄鱼成功：实物已进鱼护，模型立即消失（以后有捕获动画时改为播完再销毁）；
		// 逃走/断竿等终局保留复制窗，让客户端看到收尾表现。
		if (Snapshot.Outcome == ECatFishingOutcome::Caught)
		{
			Encounter->Destroy();
		}
		else
		{
			Encounter->SetLifeSpan(TerminalLifeSpan);
		}
	}
}
