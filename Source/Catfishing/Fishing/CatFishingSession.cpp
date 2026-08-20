#include "Fishing/CatFishingSession.h"

#include "Framework/Core/CatStableNetId.h"
#include "Character/CatCharacter.h"
#include "Framework/Game/CatfishingGameState.h"
#include "Logging/CatLog.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/CatSurvivalAttributeSet.h"
#include "Collection/CatRunImprintService.h"
#include "Data/CatFishDefinition.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/CatFishingStateTreeEvents.h"
#include "Integration/Fishing/CatFishingBoundarySubsystem.h"
#include "AbilitySystem/CatAbilitySettings.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
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

// 会话初始化流程：只接受 authority、显式 runtime gate、完整鱼定义、鱼护与 Boundary 冻结 EncounterSpec；再从钓手装备冻
// 结鱼竿强度与 L_max（没有可用的竿就没法进搏斗，这里直接拒绝建会话）；全部就绪后设置资产并启动 StateTree，失败销毁由
// 服务负责。
bool ACatFishingSession::InitializeSession(AController* FisherController, ACatCharacter* InFisherCharacter,
	UCatFishDefinition* InFishDefinition, const FGuid InFisherGuardContainerId,
	const FCatFishingEncounterSpec& EncounterSpec)
{
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	UStateTree* StateTreeAsset = Settings ? Settings->FishingSessionStateTree.LoadSynchronous() : nullptr;
	const FString StableNetId = CatResolveStableNetId(FisherController);
	UCatItemsService* Items = GetWorld() ? GetWorld()->GetSubsystem<UCatItemsService>() : nullptr;
	if (!HasAuthority() || !Settings || !Settings->IsRuntimeReady() || !StateTreeAsset || !StateTreeComponent
		|| !InFisherCharacter || !InFishDefinition || !InFishDefinition->IsRuntimeDefinitionReady()
		|| InFishDefinition->FishDefinitionId != EncounterSpec.FishDefinitionId
		|| !EncounterSpec.AttemptId.Value.IsValid()
		|| EncounterSpec.DataSchemaVersion <= 0 || EncounterSpec.DataRevision <= 0 || EncounterSpec.ContentHashHex.IsEmpty()
		|| StableNetId.IsEmpty() || !InFisherGuardContainerId.IsValid()
		|| !FMath::IsFinite(EncounterSpec.FishWeightKilograms) || EncounterSpec.FishWeightKilograms <= 0.0
		|| EncounterSpec.WaterRegion.RegionId.IsNone() || EncounterSpec.WaterRegion.RegionRevision <= 0
		|| !FMath::IsFinite(EncounterSpec.FishFightStamina) || EncounterSpec.FishFightStamina <= 0.0
		|| !FMath::IsFinite(EncounterSpec.InitialFishDistanceMeters) || EncounterSpec.InitialFishDistanceMeters <= 0.0
		|| !Items)
	{
		return false;
	}

	Snapshot.FishingSessionId = FGuid::NewGuid();
	Snapshot.Revision = 1;
	Snapshot.Phase = ECatFishingPhase::Created;
	Snapshot.FishDefinitionId = EncounterSpec.FishDefinitionId;
	Snapshot.bGiant = EncounterSpec.bGiant;
	Snapshot.FightParticipantCount = FMath::Max(EncounterSpec.FightParticipantCount, 1);
	Snapshot.CombinedFishingStrength = EncounterSpec.CombinedFishingStrength;
	Snapshot.CombinedFightStamina = EncounterSpec.CombinedFightStamina;
	Snapshot.FishFightStaminaRemaining = EncounterSpec.FishFightStamina;
	FishDefinition = InFishDefinition;
	FisherCharacter = InFisherCharacter;
	FisherStableNetId = StableNetId;
	FisherGuardContainerId = InFisherGuardContainerId;
	FishWeightKilograms = EncounterSpec.FishWeightKilograms;
	WaterRegionSnapshot = EncounterSpec.WaterRegion;
	BiteIntervalSeconds = EncounterSpec.BiteIntervalSeconds;
	InitialFishDistanceMeters = EncounterSpec.InitialFishDistanceMeters;
	FightParticipantIds.Add(StableNetId);
	FightParticipantCharacters.Add(StableNetId, InFisherCharacter);
	ItemsService = Items;
	FightRandom.Initialize(EncounterSpec.SelectionSeed);
	if (!TryFreezeFisherRod())
	{
		return false;
	}
	StateTreeComponent->SetStateTree(StateTreeAsset);
	bStartupInProgress = true;
	StateTreeComponent->StartLogic();
	bStartupInProgress = false;
	if (!StateTreeComponent->IsRunning() && Snapshot.Phase == ECatFishingPhase::Created)
	{
		return false;
	}
	PublishSnapshot();
	return true;
}
// 阶段进入流程：先验证 authority、唯一 StateTree 生命周期和未结算状态；TrueBiteWindow 记下服务器进入时刻（完美中鱼的
// 1 秒窗从这里起算）；HookedFight 先冻结搏斗参数并开局，参数缺失（钓手身体/ASC/装备没了）就拒绝进入让资产走失败边；
// NearShore 由会话用钓手当前位置和冻结水域算出近岸目标，再按水域包围盒复核后冻结该位置，算不出或不在水域内就拒绝进
// 入；其他阶段清除目标。HookedFight 与 NearShore 保留钓手/协作者供搏斗和巨鱼候选使用，其余阶段把参与集合收回为钓手；
// 随后刷新协作摘要、递增一次 Revision 并复制快照，若资产进入终态则启动有界销毁。C++ 只应用资产已选阶段，不维护转移拓
// 扑。
FCatFishingPhaseResult ACatFishingSession::EnterPhaseFromStateTree(const ECatFishingPhase NewPhase)
{
	FCatFishingPhaseResult Result;
	Result.PreviousPhase = Snapshot.Phase;
	Result.CurrentPhase = Snapshot.Phase;
	Result.Revision = Snapshot.Revision;
	if (!HasAuthority() || !StateTreeComponent || (!StateTreeComponent->IsRunning() && !bStartupInProgress))
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	if (bCaptureResolved || NewPhase == ECatFishingPhase::Resolved)
	{
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	if (NewPhase == ECatFishingPhase::TrueBiteWindow)
	{
		TrueBiteEnteredServerSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0;
		bHookSetRequested = false;
	}
	// 猫力每帧推进前都会按 Snapshot 当前合计重读，所以开局放在 RefreshFightSummary 之前没问题；这里只要求钓手本体、ASC、装备和鱼定义齐全。
	if (NewPhase == ECatFishingPhase::HookedFight && !BeginFightOnPhaseEntry())
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Result;
	}
	if (NewPhase == ECatFishingPhase::NearShore)
	{
		// 近岸目标只能来自服务器自己的事实：钓手 Character 的当前位置 + 会话创建时冻结的水域快照。
		// 资产节点和客户端命令都没有输入口，所以谁也没法把一个伪造的位置送进来；钓手已经失效时不进 NearShore，让资产的失败边去终止会话。
		const ACatCharacter* Fisher = FisherCharacter.Get();
		const FBox WaterBounds = FBox::BuildAABB(WaterRegionSnapshot.WorldCenter, WaterRegionSnapshot.HalfExtent);
		FVector ComputedTarget = FVector::ZeroVector;
		if (!Fisher || !TryComputeNearShoreTargetV0(Fisher->GetActorLocation(), WaterRegionSnapshot, ComputedTarget)
			|| !WaterBounds.IsInsideOrOn(ComputedTarget))
		{
			Result.Error = ECatDomainCommandError::PolicyUndecided;
			return Result;
		}
		NearShoreTargetWorldLocation = ComputedTarget;
		bHasNearShoreTarget = true;
		UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_near_shore_target_bound SessionId=%s Target=%s FisherDistanceCm=%.1f"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *ComputedTarget.ToString(),
			FVector::Dist(Fisher->GetActorLocation(), ComputedTarget));
	}
	else
	{
		NearShoreTargetWorldLocation = FVector::ZeroVector;
		bHasNearShoreTarget = false;
	}
	Snapshot.Phase = NewPhase;
	// 巨鱼离开 HookedFight 后仍需把合法协作者带入成像候选；NearShore 只冻结参与事实，不再接受新的 assist。
	if (NewPhase != ECatFishingPhase::HookedFight && NewPhase != ECatFishingPhase::NearShore)
	{
		FightParticipantIds.Reset();
		FightParticipantCharacters.Reset();
		FightParticipantIds.Add(FisherStableNetId);
		FightParticipantCharacters.Add(FisherStableNetId, FisherCharacter);
	}
	RefreshFightSummary();

	++Snapshot.Revision;
	PublishSnapshot();
	if (IsTerminal())
	{
		ScheduleTerminalDestroy();
	}
	Result.bApplied = true;
	Result.CurrentPhase = NewPhase;
	Result.Error = ECatDomainCommandError::None;
	Result.Revision = Snapshot.Revision;
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_phase_entered SessionId=%s Phase=%s Revision=%lld"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *UEnum::GetValueAsString(NewPhase), Snapshot.Revision);
	return Result;
}

// v0 近岸目标流程：先拒绝含 NaN 的钓手位置和任一轴半尺寸非正的水域（那是未配置的区域快照），再用 AABB 的最近点函数取离钓手最近的水域点。
// 钓手站在岸上时这个点就在他面前的水里，已经站在水里时就是他脚下，所以 RequestScoop 那条"离目标 ≤ ScoopReach"的检查对两种站位都成立。
// 这是工程暂定近似：真实规则是鱼被遛到离岸不足 1 米才可抄，但鱼的 D/L 距离运行态还没实现；正式接入后这里应改为读鱼位置，并删掉"钓手位置"这个输入。
bool ACatFishingSession::TryComputeNearShoreTargetV0(const FVector& FisherWorldLocation,
	const FCatWaterRegionSnapshot& WaterRegion, FVector& OutTargetWorldLocation)
{
	if (FisherWorldLocation.ContainsNaN() || WaterRegion.WorldCenter.ContainsNaN() || WaterRegion.HalfExtent.ContainsNaN()
		|| WaterRegion.HalfExtent.X <= 0.0 || WaterRegion.HalfExtent.Y <= 0.0 || WaterRegion.HalfExtent.Z <= 0.0)
	{
		return false;
	}
	OutTargetWorldLocation = FBox::BuildAABB(WaterRegion.WorldCenter, WaterRegion.HalfExtent).GetClosestPointTo(FisherWorldLocation);
	return true;
}

// 搏斗协作流程：先按服务器 Controller 身份和 RequestId 固定 ExpectedRevision 载荷，再要求 Giant、HookedFight 与匹配
// Revision；合法新参与者才刷新协作摘要、递增 Revision 并复制，同请求换 Revision 会被拒绝而不是重放。
FCatDomainCommandResult ACatFishingSession::SubmitFightAssist(AController* AssistingController, const FGuid RequestId,
	const int64 ExpectedRevision)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString StableNetId = CatResolveStableNetId(AssistingController);
	const FString CacheKey = FString::Printf(TEXT("%s|%s"), *StableNetId, *RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	const FString PayloadSignature = FString::Printf(TEXT("ExpectedRevision=%lld"), ExpectedRevision);
	switch (CatQueryTerminalReplay(AssistTerminalCache, AssistPayloadByKey, CacheKey, PayloadSignature, Result, MarkCommandReplayed))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		// 拒绝也要带回当前 Revision，让调用方能重读最新会话快照。
		Result.Error = ECatDomainCommandError::InvalidPayload;
		Result.Revision = Snapshot.Revision;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
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
		if (!UCatFishingBoundarySubsystem::TryGetFightCapability(AssistingController, ValidatedStableNetId, Character,
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
				++Snapshot.Revision;
				PublishSnapshot();
			}
			Result.bCommitted = true;
			Result.Error = ECatDomainCommandError::None;
		}
	}
	Result.Revision = Snapshot.Revision;
	AssistTerminalCache.Add(CacheKey, Result);
	AssistPayloadByKey.Add(CacheKey, PayloadSignature);
	return Result;
}

// 搏斗推进流程：
// 1. 只接受 authority、树在跑、阶段为 HookedFight 且已开局的帧；已有终局时幂等返回同一终局，不再推进。
// 2. 钓手身体/ASC/装备任一丢失返回 DependencyUnavailable，让节点 Failed、树进 Terminated（旧半场不恢复）。
// 3. 猫力按 Snapshot 当前合法参与者合计重读（巨鱼协作者中途加入会抬高它），读出猫当前体力和"装备耐久 − 尚未提交的磨损"交给纯模型 Step。
// 4. 把模型给的猫体力增量写回 ASC（夹在 [0, 上限]），竿磨损先累计、每满 1 秒或终局时整批提交给 Equipment；断竿终局把
// 剩余耐久全部磨光，使 Equipment 把 RodBroken 写成 true。
// 5. 把公开字段拷进 Snapshot；终局时递增 Revision 并强制网络更新，并打一行结构化日志说明哪条出口。
FCatFishingFightStepResult ACatFishingSession::ApplyFightExchangeFromStateTree(const double DeltaSeconds)
{
	FCatFishingFightStepResult Result;
	if (!HasAuthority() || !StateTreeComponent || !StateTreeComponent->IsRunning()
		|| Snapshot.Phase != ECatFishingPhase::HookedFight || !bFightStarted
		|| !FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.0)
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
		return Result;
	}
	Result.Outcome = FightState.Outcome;
	if (FightState.Outcome != ECatFishingFightOutcome::None)
	{
		Result.Error = ECatDomainCommandError::None;
		return Result;
	}
	ACatCharacter* Fisher = FisherCharacter.Get();
	UAbilitySystemComponent* ASC = Fisher ? Fisher->GetAbilitySystemComponent() : nullptr;
	const UCatEquipmentComponent* Equipment = Fisher ? Fisher->GetEquipmentComponent() : nullptr;
	if (!ASC || !Equipment)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}

	FightParams.CatStrength = Snapshot.CombinedFishingStrength;
	FCatFishingFightResources Resources;
	Resources.CatStamina = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	Resources.RodDurability = Equipment->GetSnapshot().RodDurability - PendingRodWearCost;
	FCatFishingFightStepDelta Delta;
	CatFishingFightModel::Step(FightState, FightParams, FisherIntent, DeltaSeconds, Resources, FightRandom, Delta);

	if (!FMath::IsNearlyZero(Delta.CatStaminaDelta))
	{
		const double NewStamina = FMath::Clamp(Resources.CatStamina + Delta.CatStaminaDelta, 0.0, FightParams.CatStaminaMax);
		ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), static_cast<float>(NewStamina));
	}
	PendingRodWearCost += Delta.RodDurabilityCost;
	RodWearFlushAccumulatorSeconds += DeltaSeconds;

	Result.Error = ECatDomainCommandError::None;
	Result.Outcome = FightState.Outcome;
	if (FightState.Outcome != ECatFishingFightOutcome::None)
	{
		FlushPendingRodWear(FightState.Outcome == ECatFishingFightOutcome::RodBroken);
		CopyFightStateToSnapshot();
		++Snapshot.Revision;
		PublishSnapshot();
		UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_fight_resolved SessionId=%s Outcome=%s DistanceM=%.2f LineM=%.2f FishStamina=%.2f/%.2f CatStamina=%.2f RodDurability=%.2f PerfectHook=%s Revision=%lld"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *UEnum::GetValueAsString(FightState.Outcome),
			FightState.DistanceMeters, FightState.LineMeters, FightState.FishStamina, FightState.FishStaminaMax,
			ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), Equipment->GetSnapshot().RodDurability,
			Snapshot.bPerfectHook ? TEXT("true") : TEXT("false"), Snapshot.Revision);
		return Result;
	}
	if (RodWearFlushAccumulatorSeconds >= 1.0)
	{
		FlushPendingRodWear(false);
	}
	CopyFightStateToSnapshot();
	return Result;
}

// 遛鱼意图流程：先按服务器 Controller 身份确认是初始钓手且会话未结算；写入当前意图并拷进 Snapshot；
// 若正处于 TrueBiteWindow 且这是本次真咬第一次"拖"，按 World 时间戳判断完美窗（≤1 秒），记下 bPerfectHook 并向
// StateTree 发 HookSet 事件，资产据此转进 HookedFight（鱼力量/体力削减在开局时应用）。
FCatDomainCommandResult ACatFishingSession::SubmitFightIntent(AController* Controller, const ECatFishingFightIntent Intent)
{
	FCatDomainCommandResult Result;
	Result.Revision = Snapshot.Revision;
	const FString StableNetId = CatResolveStableNetId(Controller);
	if (!HasAuthority() || StableNetId.IsEmpty() || StableNetId != FisherStableNetId)
	{
		Result.Error = ECatDomainCommandError::InvalidIdentity;
		return Result;
	}
	if (IsTerminal() || bCaptureResolved || !StateTreeComponent || !StateTreeComponent->IsRunning())
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
		return Result;
	}
	FisherIntent = Intent;
	Snapshot.FisherIntent = Intent;
	if (Snapshot.Phase == ECatFishingPhase::TrueBiteWindow && Intent == ECatFishingFightIntent::Pull && !bHookSetRequested)
	{
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0;
		const double Elapsed = (Now >= 0.0 && TrueBiteEnteredServerSeconds >= 0.0) ? Now - TrueBiteEnteredServerSeconds : -1.0;
		// 完美窗 1 秒：飞书钓鱼规则 §4（2026-08-18 拍定），服务器时间戳判定，客户端时间不参与。
		Snapshot.bPerfectHook = Elapsed >= 0.0 && Elapsed <= 1.0;
		bHookSetRequested = true;
		StateTreeComponent->SendStateTreeEvent(CatFishingStateTreeEvents::HookSet.GetTag(), FConstStructView(), FName(TEXT("CatFishing")));
		UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_hook_set SessionId=%s ElapsedSinceTrueBite=%.3f PerfectHook=%s"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), Elapsed, Snapshot.bPerfectHook ? TEXT("true") : TEXT("false"));
	}
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	return Result;
}

// 失败预算流程：先重放本会话唯一终态，再验证 StateTree/钓手 Equipment；首次把随机 RequestId 和当前 Equipment
// Revision 交给互斥惩罚事务，成功即关闭第二刀。
FCatFishingFailureResult ACatFishingSession::ApplyFailureBudgetFromStateTree(const ECatFishingFailurePenalty Penalty)
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

// 抢抄流程：先按服务器身份和 RequestId 固定目标鱼护、ExpectedRevision 与表现命中位置；缓存命中必须同载荷才重放，首次
// 合法请求仍由 Items Compare-and-Commit 独占实物鱼。
FCatScoopResult ACatFishingSession::RequestScoop(AController* ScoopingController, const FCatScoopCommand& Command)
{
	FCatScoopResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	const FString StableNetId = CatResolveStableNetId(ScoopingController);
	if (!Command.Context.RequestId.IsValid() || StableNetId.IsEmpty())
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		Result.Command.Revision = Snapshot.Revision;
		return Result;
	}
	const FString CacheKey = FString::Printf(TEXT("%s|%s"), *StableNetId,
		*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	// 签名只收进真正参与授权的两项。命中位置**不能**进签名：它按字段定义只用于诊断，授权比的是服务器 Character
	// 与 StateTree 绑定的权威近岸目标；而客户端填进来的是角色当前坐标，逐帧都在漂。把它算进签名等于要求
	// "重试时人必须站在原地"，同一个意图的第二次提交会被判成载荷冲突、拿到永久 InvalidPayload，
	// 而幂等机制本来就是为重试存在的。签名过覆盖和签名漏字段一样是 bug，只是坏的方向相反。
	const FString PayloadSignature = FString::Printf(
		TEXT("ExpectedRevision=%lld|TargetGuard=%s"),
		Command.Context.ExpectedRevision, *Command.TargetGuardContainerId.ToString(EGuidFormats::DigitsWithHyphens));
	switch (CatQueryTerminalReplay(ScoopTerminalCache, ScoopPayloadByKey, CacheKey, PayloadSignature, Result,
		[](FCatScoopResult& R){ MarkCommandReplayed(R.Command); }))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		// 载荷冲突时不能沿用缓存里的 Revision：那是首次那条命令看到的会话版本，对这次换了鱼护或换了版本前提的请求没有意义。
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		Result.Command.Revision = Snapshot.Revision;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}
	FString ValidatedScooperId;
	ACatCharacter* ScoopingCharacter = nullptr;
	double ScooperFishingStrength = 0.0;
	double ScooperFightStamina = 0.0;
	const bool bScooperFightCapable = UCatFishingBoundarySubsystem::TryGetFightCapability(ScoopingController,
		ValidatedScooperId, ScoopingCharacter, ScooperFishingStrength, ScooperFightStamina)
		&& ValidatedScooperId == StableNetId;
	double ScoopReachCentimeters = 0.0;
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
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
					if (ParticipantCharacter && UCatFishingBoundarySubsystem::TryGetFightCapability(
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
			// 钓起轨与抄获轨都是实物捕获的必需永久事实，因此钓手身份必须在不可逆的 Items 提交之前就确认可用，
			// 否则捕获落盘后才发现无法归档钓起轨，只能留下一条谁都拿不到首钓的鱼。CapturePlan 仍只在鱼定义提供正式事
			// 件时预检，None 不得反向关闭捕获链。
			if (!ImprintService || !ImprintService->CanRecordCommittedCapture() || FisherStableNetId.IsEmpty()
				|| (bHasCaptureImprintEvent
					&& (!GameState || !ImprintService->CanAcceptImprintCandidate(CaptureCandidate))))
			{
				Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
				Result.Command.Revision = Snapshot.Revision;
				ScoopTerminalCache.Add(CacheKey, Result);
				ScoopPayloadByKey.Add(CacheKey, PayloadSignature);
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
			if (CaptureResult.Command.bCommitted)
			{
				FCatCaptureConditionSnapshot Condition;
				Condition.RegionId = WaterRegionSnapshot.RegionId;
				// 记录两轨制：钓起轨（首钓与个人最佳）归上钩成立时的钓手 FisherStableNetId，抄获轨归本次抢抄者 StableNetId。
				// 两轨在 Collection 用各自的幂等索引，所以自己钓自己抄时这里会产出两条 Grant，不去重成一条；实物鱼仍只跟 StableNetId 走。
				const FGuid FishRecordedGrantId = ImprintService->RecordCommittedCapture(
					CaptureResult.Committed, FisherStableNetId, Condition);
				const FGuid FishScoopedGrantId = ImprintService->RecordCommittedScoop(
					CaptureResult.Committed, StableNetId);
				bool bOptionalPlanCommitted = true;
				if (bHasCaptureImprintEvent)
				{
					// 巨鱼候选包含 HookedFight 的合法钓手/协作者以及最终抄手；实物归属仍只来自 Items 的首个近岸 Compare-and-Commit。
					bOptionalPlanCommitted = ImprintService->SubmitImprintCandidate(CaptureCandidate);
					TArray<FCatCapturePlan> CapturePlans;
					bOptionalPlanCommitted = bOptionalPlanCommitted
						&& ImprintService->CreateCapturePlansForParticipants(CaptureCandidate.CandidateId,
							CaptureCandidate.ParticipantStableNetIds, false, CapturePlans);
				}
				if (!FishRecordedGrantId.IsValid() || !FishScoopedGrantId.IsValid() || !bOptionalPlanCommitted)
				{
					UE_LOG(LogCatFishing, Error,
						TEXT("Event=fishing_capture_archive_commit_failed SessionId=%s RequestId=%s OptionalPlanValid=%s FishRecordedGrantValid=%s FishScoopedGrantValid=%s"),
						*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
						*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
						bOptionalPlanCommitted ? TEXT("true") : TEXT("false"),
						FishRecordedGrantId.IsValid() ? TEXT("true") : TEXT("false"),
						FishScoopedGrantId.IsValid() ? TEXT("true") : TEXT("false"));
				}
				// 飞书钓鱼规则 4.4：每钓上一条鱼，钓手的竿固定 -1 耐久，和僵持磨损并行；扣在上钩成立时的钓手身上而不是抄手身上。
				// 钓手身体已经没了就记一行日志放弃这 1 点，不让它阻塞已经提交的捕获。
				if (ACatCharacter* RodOwner = FisherCharacter.Get())
				{
					UCatEquipmentComponent* RodEquipment = RodOwner->GetEquipmentComponent();
					// 不记终态：这个 RequestId 是这一行当场铸的，抄鱼流程自己已经按 CaptureRequestId 幂等，
					// 没有人会拿这个号重试这一笔磨损，记进去只是往装备组件的重放表里塞一条永远命不中的条目。
					const FCatDomainCommandResult WearResult = RodEquipment
						? RodEquipment->CommitFightRodDurabilityFromAuthority(FGuid::NewGuid(),
							RodEquipment->GetSnapshot().Revision, 1.0, /*bRecordTerminalResult*/ false)
						: FCatDomainCommandResult();
					UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_capture_rod_wear SessionId=%s Committed=%s Error=%s RodDurability=%.2f RodBroken=%s"),
						*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
						WearResult.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(WearResult.Error),
						RodEquipment ? RodEquipment->GetSnapshot().RodDurability : 0.0,
						RodEquipment && RodEquipment->GetSnapshot().bRodBroken ? TEXT("true") : TEXT("false"));
				}
				bCaptureResolved = true;
				Snapshot.Phase = ECatFishingPhase::Resolved;
				++Snapshot.Revision;
				PublishSnapshot();
				if (StateTreeComponent && StateTreeComponent->IsRunning())
				{
					StateTreeComponent->StopLogic(TEXT("Capture committed"));
				}
				ScheduleTerminalDestroy();
				Result.Command.Revision = Snapshot.Revision;
			}
		}
	}
	Result.Command.Revision = Snapshot.Revision;
	ScoopTerminalCache.Add(CacheKey, Result);
	ScoopPayloadByKey.Add(CacheKey, PayloadSignature);
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_scoop_terminal SessionId=%s RequestId=%s Committed=%s Error=%s Revision=%lld"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
		*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		Result.Command.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Command.Error), Snapshot.Revision);
	return Result;
}

// 会话终止流程：非 authority、已 Resolved 或已 Terminated 直接幂等返回，避免覆盖捕获终态。首次中断先把搏斗里累计未提
// 交的竿磨损结清（钓手身体还在才扣得到），只写一次 Terminated/Revision 并发布快照，再停止 StateTree、释放钓手之外的参
// 与弱引用且不触碰 Items；最后启动配置的有界复制窗口，让客户端看见终态后销毁 Actor，服务据此释放钓手的单活跃槽位。
void ACatFishingSession::TerminateSession(const TCHAR* Reason)
{
	if (!HasAuthority() || bCaptureResolved || Snapshot.Phase == ECatFishingPhase::Terminated)
	{
		return;
	}
	FlushPendingRodWear(false);
	Snapshot.Phase = ECatFishingPhase::Terminated;
	++Snapshot.Revision;
	PublishSnapshot();
	if (StateTreeComponent && StateTreeComponent->IsRunning())
	{
		StateTreeComponent->StopLogic(FString(Reason));
	}
	FightParticipantIds.Reset();
	FightParticipantCharacters.Reset();
	ScheduleTerminalDestroy();
	UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_session_terminated SessionId=%s Reason=%s Revision=%lld"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), Reason ? Reason : TEXT("None"), Snapshot.Revision);
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

// World 清理流程：停止仍在运行的 StateTree并清私有弱引用；不从 EndPlay 恢复或补发捕获事务。
void ACatFishingSession::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (StateTreeComponent && StateTreeComponent->IsRunning())
	{
		StateTreeComponent->StopLogic(TEXT("FishingSession EndPlay"));
	}
	ItemsService.Reset();
	FisherCharacter.Reset();
	FightParticipantCharacters.Reset();
	Super::EndPlay(EndPlayReason);
}

// 发布流程：仅 authority 请求即时网络更新；Snapshot 本身由单一 Replicated 属性发送。
void ACatFishingSession::PublishSnapshot()
{
	if (HasAuthority())
	{
		ForceNetUpdate();
	}
}

// 搏斗摘要流程：清零三项聚合后，对每个弱 Character 重用 Boundary 的 Active/未倒地/正能力谓词；只累加身份与弱引用仍一致的当前参与者。
void ACatFishingSession::RefreshFightSummary()
{
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
		if (Character && UCatFishingBoundarySubsystem::TryGetFightCapability(Character->GetController(), StableNetId,
			ValidatedCharacter, Strength, FightStamina) && StableNetId == Pair.Key
			&& ValidatedCharacter == Character)
		{
			++Snapshot.FightParticipantCount;
			Snapshot.CombinedFishingStrength += Strength;
			Snapshot.CombinedFightStamina += FightStamina;
		}
	}
}

// 鱼竿冻结流程：从钓手 EquipmentComponent 读当前竿定义 ID，经运行目录解析为 Rod 定义；竿已断、没装竿、目录解析不到、强度或 L_max 非正都返回 false。
// 强度与 L_max 冻结在会话里，进搏斗后不再重读装备，这样中途换竿不会让正在打的一场规则变化；耐久不冻结，每帧按 Equipment 当前值读。
bool ACatFishingSession::TryFreezeFisherRod()
{
	const ACatCharacter* Fisher = FisherCharacter.Get();
	const UCatEquipmentComponent* Equipment = Fisher ? Fisher->GetEquipmentComponent() : nullptr;
	if (!Equipment)
	{
		return false;
	}
	const FCatEquipmentLoadoutSnapshot& Loadout = Equipment->GetSnapshot();
	const UCatEquipmentDefinition* Rod = Loadout.RodDefinitionId.IsNone() || Loadout.bRodBroken
		? nullptr : GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(Loadout.RodDefinitionId);
	if (!Rod || Rod->Kind != ECatEquipmentKind::Rod
		|| !FMath::IsFinite(Rod->RodStrength) || Rod->RodStrength <= 0.0
		|| !FMath::IsFinite(Rod->MaximumLineLengthMeters) || Rod->MaximumLineLengthMeters <= 0.0)
	{
		UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_session_rod_unavailable RodDefinitionId=%s RodBroken=%s"),
			*Loadout.RodDefinitionId.ToString(), Loadout.bRodBroken ? TEXT("true") : TEXT("false"));
		return false;
	}
	RodStrength = Rod->RodStrength;
	LineMaxMeters = Rod->MaximumLineLengthMeters;
	return true;
}

// 搏斗开局流程：
// 1. 钓手身体、ASC、装备、鱼定义、Fishing/Ability 配置任一缺失返回 false。
// 2. 鱼基础力量 = 本条鱼实际重量 × K，K 由鱼定义 FishStrength（= 最大重量 × K）反推；鱼体力起点取 EncounterSpec 冻结
// 值；完美中鱼先按普通鱼系数削减两者。
// 3. 冻结猫力（当前合计）、竿强度与 L_max、按重量档的游速系数、猫体力上限（CatAbilitySettings 初值）、近岸距离（
// ScoopReach 折成米）、P_base（鱼表无食性列，工程暂定杂食 0.5，决策记录 D-16）、巨影标记。
// 4. 用 Cast 冻结的 D₀（猫到浮漂落点的真实距离，由当前鱼漂的射程与精准度决定）开局（第一段强制向外），
// 把鱼体力/上限/D/L/L_max 等拷进 Snapshot，标记已开局并清空磨损累计。
bool ACatFishingSession::BeginFightOnPhaseEntry()
{
	const ACatCharacter* Fisher = FisherCharacter.Get();
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	float InitialPoison = 0.0f;
	float InitialFishingStrength = 0.0f;
	float CatStaminaMax = 0.0f;
	if (!Fisher || !Fisher->GetAbilitySystemComponent() || !Fisher->GetEquipmentComponent() || !FishDefinition
		|| !Settings || !Settings->IsRuntimeReady()
		|| !GetDefault<UCatAbilitySettings>()->TryGetInitialAttributes(InitialPoison, InitialFishingStrength, CatStaminaMax)
		|| !FMath::IsFinite(FishDefinition->MaximumWeightKilograms) || FishDefinition->MaximumWeightKilograms <= 0.0)
	{
		return false;
	}
	double FishStrength = FishDefinition->FishStrength / FishDefinition->MaximumWeightKilograms * FishWeightKilograms;
	double FishStamina = Snapshot.FishFightStaminaRemaining;
	if (Snapshot.bPerfectHook)
	{
		CatFishingFightModel::ApplyPerfectHookReduction(FishStrength, FishStamina);
	}
	FightParams = FCatFishingFightParams();
	FightParams.CatStrength = Snapshot.CombinedFishingStrength;
	FightParams.FishStrengthBase = FishStrength;
	FightParams.RodStrength = RodStrength;
	FightParams.LineMaxMeters = LineMaxMeters;
	FightParams.FishSpeedCoefficient = CatFishingFightModel::ComputeFishSpeedCoefficient(FishWeightKilograms);
	FightParams.CatStaminaMax = CatStaminaMax;
	FightParams.NearShoreDistanceMeters = Settings->ScoopReachCentimeters / 100.0;
	FightParams.OutwardProbabilityBase = 0.5;
	FightParams.bGiant = Snapshot.bGiant;
	CatFishingFightModel::BeginFight(FightState, FightParams, InitialFishDistanceMeters, FishStamina, FightRandom);
	Snapshot.LineLengthMaxMeters = LineMaxMeters;
	Snapshot.FishFightStaminaMax = FishStamina;
	PendingRodWearCost = 0.0;
	RodWearFlushAccumulatorSeconds = 0.0;
	bFightStarted = true;
	CopyFightStateToSnapshot();
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_fight_started SessionId=%s CatStrength=%.2f FishStrength=%.2f RodStrength=%.2f LineMaxM=%.1f SpeedCoef=%.2f FishStamina=%.2f InitialDistanceM=%.2f PerfectHook=%s Giant=%s"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), FightParams.CatStrength, FishStrength, RodStrength,
		LineMaxMeters, FightParams.FishSpeedCoefficient, FishStamina, InitialFishDistanceMeters,
		Snapshot.bPerfectHook ? TEXT("true") : TEXT("false"), Snapshot.bGiant ? TEXT("true") : TEXT("false"));
	return true;
}

// 磨损提交流程：钓手装备还在且（累计磨损为正或要求断竿）时，用一个新 RequestId 和装备当前 Revision 提交一次；
// 断竿时成本取"累计磨损"与"当前耐久"中的较大者，保证 Equipment 把耐久写到 0 并置 RodBroken。提交结果落一行日志，失败不重试（下一次累计会再试）。
void ACatFishingSession::FlushPendingRodWear(const bool bBreakRod)
{
	ACatCharacter* Fisher = FisherCharacter.Get();
	UCatEquipmentComponent* Equipment = Fisher ? Fisher->GetEquipmentComponent() : nullptr;
	if (!Equipment)
	{
		return;
	}
	const double Cost = bBreakRod
		? FMath::Max(PendingRodWearCost, Equipment->GetSnapshot().RodDurability)
		: PendingRodWearCost;
	RodWearFlushAccumulatorSeconds = 0.0;
	if (!FMath::IsFinite(Cost) || Cost <= 0.0)
	{
		return;
	}
	// 不记终态：僵持磨损是搏斗期间按秒提交的，RequestId 每次当场铸、没有任何一方持有它，
	// 记进去的条目永远命不中，却会让装备组件的两张重放表按秒各涨一条，一场搏斗上百条、直到角色销毁才释放。
	// 这条提交的"不重复扣"由本会话的 PendingRodWearCost 归零来保证，不需要重放缓存兜底。
	const FCatDomainCommandResult Result = Equipment->CommitFightRodDurabilityFromAuthority(
		FGuid::NewGuid(), Equipment->GetSnapshot().Revision, Cost, /*bRecordTerminalResult*/ false);
	if (Result.bCommitted)
	{
		PendingRodWearCost = 0.0;
	}
	UE_LOG(LogCatFishing, Verbose, TEXT("Event=fishing_fight_rod_wear_committed SessionId=%s Cost=%.3f Committed=%s Error=%s RodDurability=%.2f RodBroken=%s"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), Cost,
		Result.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Error),
		Equipment->GetSnapshot().RodDurability, Equipment->GetSnapshot().bRodBroken ? TEXT("true") : TEXT("false"));
}

// 快照拷贝流程：只搬运公开字段，不碰 Revision，也不 ForceNetUpdate（每帧都 force 没意义，属性变化本身会随正常网络节拍复制）。
void ACatFishingSession::CopyFightStateToSnapshot()
{
	Snapshot.FishDistanceMeters = FightState.DistanceMeters;
	Snapshot.LineLengthMeters = FightState.LineMeters;
	Snapshot.FishSwimState = FightState.SwimState;
	Snapshot.FishFightStaminaRemaining = FightState.FishStamina;
	Snapshot.bFishDeathStruggle = FightState.IsDeathStruggling();
	Snapshot.FisherIntent = FisherIntent;
	Snapshot.FightOutcome = FightState.Outcome;
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
