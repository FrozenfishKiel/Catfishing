#include "Fishing/CatFishingSession.h"
#include "Inventory/CatInventorySettings.h"
#include "Equipment/Fragments/CatEquipmentFragment_Rod.h"
#include "Equipment/Fragments/CatEquipmentFragment_Bait.h"
#include "Fishing/Simulation/CatFishingBiteTimingModel.h"

#include "Character/CatCharacter.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingGameState.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Logging/CatLog.h"
#include "Logging/CatLogContext.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
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
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StateTreeComponent.h"
#include "GameFramework/PlayerState.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "FishContainers/CatFishContainerService.h"
#include "FishContainers/CatFishPickupSettings.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "FishContainers/CatFishGuardActor.h"
#include "FishContainers/World/CatWorldSurfaceResolver.h"
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
		StaminaOwner = FisherCharacter; // 本场只负责主控自己的恢复域。
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
	RefreshFightSummary(); // 阶段变化只重读当前主控，物理旁人没有会话身份。

	PublishSnapshot(ECatFishingSnapshotMutation::PhaseChange); // 阶段变化必须递增 PhaseEpoch，拒绝上一阶段的延迟事件。
	Result.bApplied = true;
	Result.CurrentPhase = NewPhase;
	Result.Error = ECatDomainCommandError::None;
	Result.Revision = Snapshot.Revision;
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_phase_entered SessionId=%s Phase=%s Revision=%lld"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *UEnum::GetValueAsString(NewPhase), Snapshot.Revision);
	return Result;
}

// 旧协作和交换Task的反射类型仍可加载，但不能创建成员或绕过固定步扣款。
FCatDomainCommandResult ACatFishingSession::SubmitFightAssist(AController* AssistingController,
    const FGuid RequestId, const int64 ExpectedRevision)
{
    FCatDomainCommandResult Result;
    Result.RequestId = RequestId;
    Result.Revision = Snapshot.Revision;
    Result.Error = !HasAuthority() || !RequestId.IsValid() || ResolveStableNetId(AssistingController).IsEmpty()
        ? ECatDomainCommandError::InvalidIdentity
        : ExpectedRevision != Snapshot.Revision ? ECatDomainCommandError::RevisionConflict
        : ECatDomainCommandError::InvalidPhase;
    UE_LOG(LogCatFishing, Warning,
        TEXT("Event=fishing_legacy_assist_rejected SessionId=%s RequestId=%s Reason=PhysicalAssistanceOnly World=%s NetMode=%d Authority=%d LocalRole=%d %s"),
        *Snapshot.FishingSessionId.ToString(), *RequestId.ToString(), *GetNameSafe(GetWorld()),
        int32(GetNetMode()), HasAuthority(), int32(GetLocalRole()), *CatLogContext::BuildControllerFields(AssistingController));
    return Result;
}

FCatDomainCommandResult ACatFishingSession::ResolveFightExchangeFromStateTree(const double FishStaminaCost,
    const double ParticipantStaminaCost)
{
    FCatDomainCommandResult Result;
    Result.RequestId = Snapshot.FishingSessionId;
    Result.Revision = Snapshot.Revision;
    Result.Error = ECatDomainCommandError::InvalidPhase;
    UE_LOG(LogCatFishing, Warning,
        TEXT("Event=fishing_legacy_exchange_rejected SessionId=%s Reason=FixedStepOwnsBilling World=%s NetMode=%d Authority=%d LocalRole=%d"),
        *Snapshot.FishingSessionId.ToString(), *GetNameSafe(GetWorld()), int32(GetNetMode()), HasAuthority(), int32(GetLocalRole()));
    return Result;
}

// 仅允许原竿拥有者明确取回控制；物理抓握本身不会调用此入口或转让会话。
bool ACatFishingSession::ResumeOwnerControlFromAuthority(AController* NewFisherController)
{
	const FString NewStableNetId = ResolveStableNetId(NewFisherController);
	ACatCharacter* NewCharacter = NewFisherController ? Cast<ACatCharacter>(NewFisherController->GetPawn()) : nullptr;
	UCatAbilitySystemComponent* NewASC = NewCharacter ? NewCharacter->GetCatAbilitySystemComponent() : nullptr;
	const ACatfishingGameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	double NewStrength = NewASC ? NewASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFishingStrengthAttribute()) : 0.0;
	double NewStamina = NewASC ? NewASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()) : 0.0;
	// 控制资格与出力分离：原主控零体力仍能持有线杯控制权。
	const bool bCapable = NewASC && GameMode && GameMode->CanAcceptGameplayCommand(NewFisherController)
		&& UCatFishingService::CanControllerStartFishingAction(NewFisherController)
		&& FMath::IsFinite(NewStrength) && NewStrength >= 0.0
		&& FMath::IsFinite(NewStamina) && NewStamina >= 0.0;
	const bool bFightTakeover = Snapshot.Phase == ECatFishingPhase::HookedFight
		|| Snapshot.Phase == ECatFishingPhase::ExhaustedReel;
	// 姿态与会话阶段正交；原拥有者在允许阶段可以恢复同一会话的控制。
	const bool bTransferablePhase = Snapshot.Phase == ECatFishingPhase::CastFlight
		|| Snapshot.Phase == ECatFishingPhase::Waiting || Snapshot.Phase == ECatFishingPhase::Probe
		|| Snapshot.Phase == ECatFishingPhase::TrueBiteWindow || bFightTakeover
		|| Snapshot.Phase == ECatFishingPhase::NearShore || Snapshot.Phase == ECatFishingPhase::AutoHauling
		|| Snapshot.Phase == ECatFishingPhase::ExhaustedReel;
	if (!HasAuthority() || IsTerminal() || !bTransferablePhase || NewStableNetId.IsEmpty() || !bCapable
		|| !NewCharacter || !NewFisherController->PlayerState || !Snapshot.RodActor
        || Snapshot.RodActor->GetPresentationState().OwnerPlayerState != NewFisherController->PlayerState
        || !Snapshot.RodActor->IsPrimaryOperator(NewFisherController->PlayerState))
	{
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fishing_owner_resume_rejected SessionId=%s Phase=%s Transferable=%s FightCapable=%s NewStableIdValid=%s %s"),
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
		// 恢复只读取本人当前属性，不补满体力或重建本场Runner。
		const double NewStaminaMaximum = NewAbilitySystem
			? NewAbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute()) : 0.0;
		const bool bStaminaAttributeReady = FMath::IsFinite(NewStaminaMaximum) && NewStaminaMaximum > 0.0;
		if (!FightRunner || !FightRunner->IsRunning() || !NewAbilitySystem || !bStaminaAttributeReady)
		{
			UE_LOG(LogCatFishing, Warning,
				TEXT("Event=fishing_owner_resume_rejected SessionId=%s Reason=StaminaOrRunnerUnavailable Runner=%s StaminaAttribute=%s %s"),
				*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
				FightRunner && FightRunner->IsRunning() ? TEXT("Running") : TEXT("Unavailable"),
				bStaminaAttributeReady ? TEXT("Ready") : TEXT("Invalid"),
				*CatLogContext::BuildControllerFields(NewFisherController));
			return false;
		}

		int64 InitialInputSequence = 0;
		if (const ACatfishingPlayerController* NewPlayerController = Cast<ACatfishingPlayerController>(NewFisherController))
		{
			if (UCatFishingCommandComponent* Commands = NewPlayerController->GetFishingCommandComponent())
			{
				bool bIgnoredPull = false;
				bool bIgnoredSlack = false;
				Commands->ClearHeldFightInputForControlTransferFromAuthority();
				Commands->TryGetHeldFightInputStateFromAuthority(
					bIgnoredPull, bIgnoredSlack, InitialInputSequence);
			}
		}
		NewStamina = NewAbilitySystem->GetNumericAttribute(
			UCatSurvivalAttributeSet::GetFightStaminaAttribute());
		if (!FightRunner->ResumeOwnerFromAuthority(NewFisherController->PlayerState,
			NewAbilitySystem, NewStrength,
			NewStaminaMaximum, NewStamina, InitialInputSequence, false, false))
		{
			UE_LOG(LogCatFishing, Warning,
				TEXT("Event=fishing_owner_resume_rejected SessionId=%s Reason=RunnerRebindFailed Strength=%.3f Stamina=%.3f StaminaMaximum=%.3f InputSequence=%lld %s"),
				*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), NewStrength,
				NewStamina, static_cast<double>(NewStaminaMaximum), InitialInputSequence,
				*CatLogContext::BuildControllerFields(NewFisherController));
			return false;
		}

		// 旧操作手离开后保留当下体力，不再瞬间补满；同时从本会话终态恢复名单移除，
		// 防止他去另一根竿后被旧会话的收尾错误覆盖。
		if (OldFisherCharacter && OldFisherCharacter != NewCharacter)
		{
			StaminaOwner.Reset();
		}
		StaminaOwner = NewCharacter;
		Snapshot.bReeling = FightRunner->GetCatAction() == ECatFightCatAction::Pull;
		Snapshot.bSlacking = FightRunner->GetCatAction() == ECatFightCatAction::Slack;
	}

	FisherStableNetId = NewStableNetId;
	if (bFightStaminaInitialized) StaminaOwner = NewCharacter;
	FisherCharacter = NewCharacter;
	Snapshot.FisherPlayerState = NewFisherController->PlayerState;
	LastSuspendedFisherPlayerState = nullptr;

	RefreshFightSummary();
	PublishSnapshot(ECatFishingSnapshotMutation::Discrete);
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_owner_resumed SessionId=%s Phase=%s Mode=%s OldFisher=%s NewStrength=%.3f NewFightStamina=%.3f %s"),
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
	// 全局设置和服务器当前装备快照中的已选抄网 DA 共同给出有效距离；临时测试发放只负责入库和选择，此处仍要求真实已装备抄网。
	const bool bScoopReachReady = UCatFishingAimLibrary::TryResolveScoopReach(
		ScooperEquipment, ScoopReachCentimeters);
	// 这里不再要求"鱼处于近岸带内"：射线∩圆本身就是唯一的范围判定，再叠一层离岸距离等于两套口径，
	// 会出现"圈画成绿色（够得着）但服务器因为鱼离岸 3.1 米而拒绝"这种表现与判定打架的情况。
	// 几何上也已经蕴含：抄手必须站在岸上，射线长度有限，所以能被抄到的鱼必然离岸不远。
	// 抄手自己相对岸线的空间关系：抢抄要求抄手站在岸上（Outside 水域），不能站在水里抄。
	const FVector ScooperLocation = ScoopingCharacter ? ScoopingCharacter->GetActorLocation() : FVector::ZeroVector;
	const FVector BodyFootLocation = ScoopingCharacter ? ScoopingCharacter->GetBodyFootPointWorld() : FVector::ZeroVector;
	const FCatWaterSpatialResult ScooperSpatial = Water && ScoopingCharacter && AttemptSnapshot.WaterRegion.IsValid()
		? Water->QueryShoreRelation(ScooperLocation, AttemptSnapshot.WaterRegion)
		: FCatWaterSpatialResult{};
	// 足底与地面点只用于诊断，不参与当前抄网策略。它们能直接区分“角色中心高度超差”与“脚下实际位于水域内”。
	const FCatWaterSpatialResult BodyFootSpatial = Water && ScoopingCharacter && AttemptSnapshot.WaterRegion.IsValid()
		? Water->QueryShoreRelation(BodyFootLocation, AttemptSnapshot.WaterRegion)
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
	const FVector GroundQueryLocation = GroundHit.bBlockingHit ? GroundHit.ImpactPoint : BodyFootLocation;
	const FCatWaterSpatialResult GroundSpatial = Water && ScoopingCharacter && AttemptSnapshot.WaterRegion.IsValid()
		? Water->QueryShoreRelation(GroundQueryLocation, AttemptSnapshot.WaterRegion)
		: FCatWaterSpatialResult{};
	const double FishRadius = FishDefinition ? FishDefinition->ScoopTargetRadiusCentimeters : 0.0;
	const FVector FishLocation = Encounter ? Encounter->GetActorLocation() : FVector::ZeroVector;
	// 抄鱼与拾取共用单嘴约束；鱼护虽在背包中，其可见嘴部载体仍占用这一位置。
	const bool bMouthFree = ScoopingCharacter && !ACatFishPickupActor::FindCarriedFish(ScoopingCharacter)
		&& !ACatFishGuardActor::FindCarriedGuard(ScoopingCharacter);
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
			*CatLogContext::BuildWaterSpatialFields(TEXT("FootWater"), BodyFootLocation, BodyFootSpatial),
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
		*CatLogContext::BuildWaterSpatialFields(TEXT("FootWater"), BodyFootLocation, BodyFootSpatial),
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
		|| !FisherController || !FisherController->PlayerState || StableNetId.IsEmpty()
		|| !Attempt.RodActor->IsPrimaryOperator(FisherController->PlayerState)
		|| Attempt.RodActor->GetPresentationState().OwnerPlayerState != FisherController->PlayerState)
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
	CastEquipment = InFisherCharacter->GetEquipmentComponent(); // 冻结饵料/会话协调器；它已记录真实竿宿主，物理抓握不重新绑定。
	bool bRodBroken = false;
	if (!CastEquipment.IsValid() || !CastEquipment->GetFishingRodDurability(
		Attempt.FishingSessionId, Snapshot.RodDurabilityRemaining, bRodBroken) || bRodBroken)
	{
		return false;
	}
	RodWearSequence = 0;
	FisherStableNetId = StableNetId;
	CatchFisherStableNetId = StableNetId;

	ItemsService = GetWorld() ? GetWorld()->GetSubsystem<UCatFishContainerService>() : nullptr;
	// 捕获物会在力竭回收后生成世界鱼；抛竿准备只要求 Items 服务存在，不绑定或搜索任何鱼护。
	bPrepared = ItemsService.IsValid();
	return bPrepared;
}

void ACatFishingSession::RefreshBiteAvailabilityFromAuthority()
{
	if (!HasAuthority() || IsTerminal() || Snapshot.Phase != ECatFishingPhase::Waiting) return;
	const ACatfishingGameModeBase* Mode = GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>();
	if (Mode && Mode->CanGenerateNewFishingBites())
	{
		if (!GetWorldTimerManager().IsTimerActive(ProbeTimerHandle)
			&& !ScheduleWaitingProbeFromStateTree())
			TerminateSession(ECatFishingOutcome::Invalidated, TEXT("Daytime bite reschedule failed"));
		return;
	}
	const bool bHadTimers = GetWorldTimerManager().IsTimerActive(ProbeTimerHandle)
		|| GetWorldTimerManager().IsTimerActive(BiteWarningTimerHandle);
	GetWorldTimerManager().ClearTimer(BiteWarningTimerHandle);
	GetWorldTimerManager().ClearTimer(ProbeTimerHandle);
	if (Snapshot.HookActor)
		Snapshot.HookActor->SetBobberPresentationModeFromAuthority(ECatFishingBobberPresentationMode::Calm);
	if (bHadTimers)
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_bite_wait_cleared SessionId=%s CastAttemptId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s Result=WaitingWithoutNewBites"),
			*Snapshot.FishingSessionId.ToString(), *Snapshot.CastAttemptId.ToString(), *GetNameSafe(GetWorld()),
			GetNetMode(), HasAuthority(), int32(GetLocalRole()), *GetName());
}

bool ACatFishingSession::ScheduleWaitingProbeFromStateTree()
{
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	const auto RejectSchedule = [this](const TCHAR* Reason)
	{
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fishing_bite_schedule_rejected SessionId=%s CastAttemptId=%s Opportunity=%u Reason=%s World=%s WorldNetMode=%d Authority=%d LocalRole=%d Actor=%s Hook=%s"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *Snapshot.CastAttemptId.ToString(EGuidFormats::DigitsWithHyphens), BiteOpportunitySequence, Reason,
			*GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), static_cast<int32>(GetLocalRole()),
			*GetName(), *GetNameSafe(Snapshot.HookActor));
		return false;
	};
	FCatFishingBiteTimingParameters TimingParameters;
	if (!HasAuthority() || !bPrepared || IsTerminal() || !Settings
		|| !Snapshot.HookActor || !Settings->TryGetBiteTimingParameters(TimingParameters))
	{
		return RejectSchedule(TEXT("SessionOrTimingConfigurationUnavailable"));
	}
	// Waiting 可以由“首次抛竿”或“上一轮真咬窗口漏按”进入。漏按不会释放鱼竿/鱼线/饵料预约，
	// 这里只清理尚未确认的咬钩机会；若已有鱼 Actor，说明错误地试图把已确认搏斗倒回 Waiting，拒绝重入。
	if (Snapshot.FishEncounterActor || FishDefinition || SelectionResolution == ECatFishSelectionResolution::Selected)
	{
		return RejectSchedule(TEXT("FishAlreadySelected"));
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
	// 夜间抛竿/漏过真咬都正常进入 Waiting，但不创建新的咬钩机会或消费随机数。
	const ACatfishingGameModeBase* Mode = GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>();
	if (!Mode || !Mode->CanGenerateNewFishingBites())
	{
		if (!EnterPhaseFromStateTree(ECatFishingPhase::Waiting).bApplied) return false;
		RefreshBiteAvailabilityFromAuthority();
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_bite_schedule_suppressed SessionId=%s CastAttemptId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s Result=WaitingWithoutNewBites"),
			*Snapshot.FishingSessionId.ToString(), *Snapshot.CastAttemptId.ToString(), *GetNameSafe(GetWorld()),
			GetNetMode(), HasAuthority(), int32(GetLocalRole()), *GetName());
		return true;
	}

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
		if (!EnterPhaseFromStateTree(ECatFishingPhase::Waiting).bApplied)
			return RejectSchedule(TEXT("WaitingPhaseRejected"));
	}
	double BaitRateMultiplier = 1.0;
	double BaitMinimumDelayMultiplier = 1.0;
	// 鱼饵按其配置的倍率修正基础上钩率与最小延迟。
	if (const UCatEquipmentDefinition* Bait = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentDefinition>(AttemptSnapshot.BaitDefinitionId))
	{
		BaitRateMultiplier = Bait->FindFragment<UCatEquipmentFragment_Bait>()->BiteRateMultiplier;
		BaitMinimumDelayMultiplier = Bait->FindFragment<UCatEquipmentFragment_Bait>()->MinimumBiteDelayMultiplier;
	}
	// 初次调度时钩子还在飞行；窝料必须采样服务器冻结的水面落点。
	FCatChumSample ChumSample;
	if (UCatChumFieldSubsystem* Chum = GetWorld()->GetSubsystem<UCatChumFieldSubsystem>())
	{
		ChumSample = Chum->SampleChumAtPoint(AttemptSnapshot.ServerCorrectedLandingWorldPoint,
			AttemptSnapshot.WaterRegion, GetWorld()->GetTimeSeconds());
	}
	if (!ChumSample.bSucceeded)
	{
		// 保留采样失败时按无窝调度的既有契约，但不再把失败静默伪装成有效零浓度。
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fishing_bite_chum_sample_failed SessionId=%s CastAttemptId=%s Opportunity=%u Error=%s Result=UnchummedFallback World=%s WorldNetMode=%d Authority=%d LocalRole=%d Actor=%s"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *Snapshot.CastAttemptId.ToString(EGuidFormats::DigitsWithHyphens), BiteOpportunitySequence,
			*UEnum::GetValueAsString(ChumSample.Error), *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(),
			static_cast<int32>(GetLocalRole()), *GetName());
	}
	const double TotalChum = ChumSample.bSucceeded ? ChumSample.EffectiveChumVector.Fishy
		+ ChumSample.EffectiveChumVector.Fragrant + ChumSample.EffectiveChumVector.Fermented : 0.0;
	FCatFishingBiteTimingDistribution Distribution;
	if (!FCatFishingBiteTimingModel::BuildDistribution(TimingParameters, TotalChum,
		BaitRateMultiplier, BaitMinimumDelayMultiplier, Distribution))
		return RejectSchedule(TEXT("InvalidContributionOrBaitTiming"));
	// 每轮仍只消费原随机流的第一个随机数，保留鱼种抽样与机会种子的既有关系。
	FRandomStream Random(static_cast<int32>(CurrentBiteRandomSeed));
	double WaitSeconds = 0.0;
	if (!Distribution.TrySample(static_cast<double>(Random.FRand()), WaitSeconds))
		return RejectSchedule(TEXT("InvalidWaitSample"));
	const FCatFishingCastTrajectory& Flight = Snapshot.HookActor->GetPresentationState().CastTrajectory;
	const double RemainingFlightSeconds = FMath::Max(0.0,
		Flight.StartedServerTime + Flight.DurationSeconds - GetWorld()->GetTimeSeconds());
	const double WarningDelay = RemainingFlightSeconds + WaitSeconds - Distribution.WarningSeconds;
	const double Delay = RemainingFlightSeconds + WaitSeconds;
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
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_bite_scheduled Model=ChumMeanAnchors SessionId=%s CastAttemptId=%s Opportunity=%u Seed=%llu World=%s WorldNetMode=%d Authority=%d LocalRole=%d Actor=%s Hook=%s Region=%s Landing=%s SampleServerTime=%.3f ChumFields=%d ChumFishy=%.6f ChumFragrant=%.6f ChumFermented=%.6f TotalChum=%.6f NeutralMeanSeconds=%.6f ExpectedMeanSeconds=%.6f RatePerSecond=%.9f Bait=%s BaitRateMultiplier=%.3f BaitMinimumMultiplier=%.3f MinimumCalmSeconds=%.3f WarningSeconds=%.3f MaximumWaitSeconds=%.3f WaitSeconds=%.6f RemainingFlightSeconds=%.6f WarningAtServerTime=%.6f BiteAtServerTime=%.6f %s"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *Snapshot.CastAttemptId.ToString(EGuidFormats::DigitsWithHyphens), BiteOpportunitySequence,
		CurrentBiteRandomSeed, *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), static_cast<int32>(GetLocalRole()),
		*GetName(), *GetNameSafe(Snapshot.HookActor), *AttemptSnapshot.WaterRegion.RegionId.ToString(),
		*AttemptSnapshot.ServerCorrectedLandingWorldPoint.ToString(), ChumSample.SampleServerTime,
		ChumSample.ContributingFieldCount, ChumSample.EffectiveChumVector.Fishy,
		ChumSample.EffectiveChumVector.Fragrant, ChumSample.EffectiveChumVector.Fermented, TotalChum,
		Distribution.NeutralMeanSeconds, Distribution.ExpectedMeanSeconds, Distribution.RatePerSecond,
		*AttemptSnapshot.BaitDefinitionId.ToString(), BaitRateMultiplier, BaitMinimumDelayMultiplier,
		Distribution.MinimumCalmSeconds, Distribution.WarningSeconds, Distribution.MaximumWaitSeconds,
		WaitSeconds, RemainingFlightSeconds, GetWorld()->GetTimeSeconds() + WarningDelay,
		GetWorld()->GetTimeSeconds() + Delay,
		*CatLogContext::BuildControllerFields(FisherCharacter.IsValid() ? FisherCharacter->GetController() : nullptr));
	return true;
}

void ACatFishingSession::HandleBiteWarningTimer()
{
	// 预警只改变 Hook 的复制表现模式；公开阶段仍是 Waiting，提前提竿继续按空钩裁决。
	if (!HasAuthority() || IsTerminal() || Snapshot.Phase != ECatFishingPhase::Waiting || !Snapshot.HookActor)
	{
		return;
	}
	const ACatfishingGameModeBase* Mode = GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>();
	if (!Mode || !Mode->CanGenerateNewFishingBites())
	{
		RefreshBiteAvailabilityFromAuthority();
		return;
	}
	Snapshot.HookActor->SetBobberPresentationModeFromAuthority(ECatFishingBobberPresentationMode::BiteWarning);
}

void ACatFishingSession::HandleProbeTimer()
{
	// 计时器到期：只有仍处于 Waiting 阶段才把"试探触发"事件送进 StateTree，
	// 阶段已经变化（比如提前被取消/提竿）则说明这次触发已经过期，直接忽略。
	if (!HasAuthority() || IsTerminal() || Snapshot.Phase != ECatFishingPhase::Waiting || !StateTreeComponent) return;
	const ACatfishingGameModeBase* Mode = GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>();
	if (!Mode || !Mode->CanGenerateNewFishingBites())
	{
		RefreshBiteAvailabilityFromAuthority();
		return;
	}
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

	// 防止白天计时器已排队、StateTree 到夜晚才消费的竞态；复用 Probe -> Waiting 事件边。
	const ACatfishingGameModeBase* Mode = World->GetAuthGameMode<ACatfishingGameModeBase>();
	if (!Mode || !Mode->CanGenerateNewFishingBites())
	{
		if (!StateTreeComponent) return false;
		StateTreeComponent->SendStateTreeEvent(CatFishingGameplayTags::WindowExpired, FConstStructView(), TEXT("CatFishing"));
		return true;
	}

	// 此刻只发布“真咬信号”：鱼仍未被选择、未生成，饵料也仍处于抛竿时建立的预约状态。
	// WindowEnds 必须在 EnterPhase 发布快照前写好，客户端第一次看到 TrueBiteWindow 时截止时间就是完整的。
	const double PreviousWindowEnd = Snapshot.WindowEndsServerTime;
	const ACatfishingGameState* GameState = World->GetGameState<ACatfishingGameState>();
	BiteTimeOfDay = GameState ? GameState->GetRunPublicState().Environment.TimeOfDay : ECatEnvironmentTimeOfDay::Unknown;
	BiteWeather = GameState ? GameState->GetRunPublicState().Environment.Weather : ECatEnvironmentWeather::Unknown;
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
	FrozenSelectionContext.TimeOfDay = BiteTimeOfDay;
	FrozenSelectionContext.Weather = BiteWeather;
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
	const UCatEquipmentDefinition* RodDefinition = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentDefinition>(
		AttemptSnapshot.RodDefinitionId);
	UCatEquipmentComponent* Equipment = CastEquipment.Get(); // 钓鱼用途/饵料预留始终属于原始抛竿者，物理抓握不改变结算对象。
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
	// 身体上限由 ASC 播种并复制；Fishing 只消费该运行时属性，不再从角色配置建立另一份上限。
	const double CatStaminaMaximumFromAttributes = AbilitySystem->GetNumericAttribute(
		UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute());
	if (!FMath::IsFinite(CatStaminaMaximumFromAttributes) || CatStaminaMaximumFromAttributes <= 0.0)
	{
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fishing_fight_start_rejected SessionId=%s Reason=StaminaMaximumAttributeInvalid MaxFightStamina=%.3f %s"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), CatStaminaMaximumFromAttributes,
			*CatLogContext::BuildControllerFields(FisherCharacter->GetController()));
		return false;
	}
	if (!AbilitySystem->InitializeFishingStaminaForSession())
	{
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fishing_fight_start_rejected SessionId=%s Reason=StaminaInitializationRejected MaxFightStamina=%.3f %s"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), CatStaminaMaximumFromAttributes,
			*CatLogContext::BuildControllerFields(FisherCharacter->GetController()));
		return false;
	}

	// 双方力量、实际鱼重与猫的等效系统质量在此冻结；Runner 每步只刷新参与者输入、力量和接入约束的猫数。
	// 鱼力量包含完美中鱼折减；这里先保存主位基础力量供初始化校验，
	// Runner启动后只读取主控ASC与实际身体样本；旁人助力通过物理竿端点进入求解。
	// 下面把服务器设置、鱼竿/鱼定义、性格模板的各项参数一次性打包进模拟配置结构体，交给 FightRunner/Simulator 使用。
	FCatFightSimulationConfig Config;
	Config.FixedStepSeconds = Settings->FixedFightStepSeconds; // 固定步长模拟，保证服务器权威结果确定可复现。
	Config.PrimaryOperatorCatStrength = AbilitySystem->GetNumericAttribute(
		UCatSurvivalAttributeSet::GetFishingStrengthAttribute());
	// 辅助位合力不能在会话启动瞬间静态冻结；Runner 每个固定步从鱼竿操作位重建并覆盖此合计。

	// 猫系统质量独立于力量成长；CharacterMovement 的推挤 Mass 不作为搏斗质量来源。
	const UCatPhysicalBodyComponent* PhysicalBody = FisherCharacter->GetPhysicalBodyComponent();
	if (!PhysicalBody || !PhysicalBody->GetBody()) return false;
	Config.PrimaryOperatorMassKilograms = PhysicalBody->GetBody()->GetMass();

	Config.FishMassKilograms = FishWeightKilograms;
	Config.FishStrength = FrozenSelectionResult.BaseFishStrength * FishStrengthScale;
	Config.StrengthPerKilogram = FightBalance->StrengthPerKilogram;
	Config.ForcePerStrengthNewtons = FightBalance->ForcePerStrengthNewtons;

	Config.ExhaustedReelForceNewtons = FightBalance->ExhaustedReelForceNewtons;
	Config.ExhaustedCatTowAccelerationCentimetersPerSecondSquared = FightBalance->ExhaustedCatTowAccelerationCentimetersPerSecondSquared;
	Snapshot.FishStrength = Config.FishStrength;
	Config.RodPhysicsLengthCentimeters = RodDefinition->FindFragment<UCatEquipmentFragment_Rod>()->RodPhysicsLengthCentimeters;
	Config.CatStaminaMaximum = CatStaminaMaximumFromAttributes;
	Config.CatStaminaCostPerStrengthCentimeter = FightBalance->CatStaminaCostPerStrengthCentimeter;
	Config.CatRodStaminaCostPerStrengthRadian = FightBalance->CatRodStaminaCostPerStrengthRadian;
	Config.CatUnloadedWorkMultiplier = FightBalance->CatUnloadedWorkMultiplier;
	Config.CatSupportStaminaPerSecond = FightBalance->CatSupportStaminaPerSecond;
	Config.FishStaminaPerUnfulfilledMeter = FightBalance->FishStaminaPerUnfulfilledMeter;
	Config.CatMovementStaminaMultiplier = FightBalance->CatMovementStaminaMultiplier;
	Config.CatReelStaminaMultiplier = FightBalance->CatReelStaminaMultiplier;
	Config.CatRodStaminaMultiplier = FightBalance->CatRodStaminaMultiplier;
	Config.CatHoldStaminaMultiplier = FightBalance->CatHoldStaminaMultiplier;
	Config.CatLoadStaminaMultiplier = FightBalance->CatLoadStaminaMultiplier;
	Config.StalemateRodWearPerFishStrength = FightBalance->StalemateRodWearPerFishStrength;
	Config.SlackStaminaRegenPerSecond = FightBalance->SlackStaminaRegenPerSecond;
	Config.ReelSpeedCentimetersPerSecond = FightBalance->ReelSpeedCentimetersPerSecond;
	Config.FishFullEffortSpeedCentimetersPerSecond = Personality->FullEffortMovementSpeedCentimetersPerSecond;
	Config.ExhaustedCatEscapeSpeedMultiplier = FightBalance->ExhaustedCatEscapeSpeedMultiplier;
	Config.FishExhaustionThreshold = FightBalance->FishExhaustionThreshold;
	Config.StrongConfrontationAlignmentThreshold = Personality->StrongConfrontationAlignmentThreshold;
	Config.StrongConfrontationConfirmationSeconds = Personality->StrongConfrontationConfirmationSeconds;
	Config.AngleStrengthExponent = Personality->AngleStrengthExponent;
	Config.DisplayTensionNewtons = FightBalance->DisplayTensionNewtons;
	Config.MinimumRodLeverageMultiplier = FightBalance->HeldRodMinimumLeverageMultiplier;
	Config.MaximumFishConstraintCorrectionSpeedCentimetersPerSecond =
		FightBalance->MaximumFishConstraintCorrectionSpeedCentimetersPerSecond;
	Config.MaximumLineLengthCentimeters = RodDefinition->FindFragment<UCatEquipmentFragment_Rod>()->MaximumLineLengthCentimeters;
	// 只读取本场绑定的同一根鱼竿实例；定义上限仅用于新购和维修，开场不能补耐久。
	bool bRodBroken = false;
	if (!Equipment->GetFishingRodDurability(Snapshot.FishingSessionId, Config.RodDurability, bRodBroken)
		|| bRodBroken || Config.RodDurability <= 0.0)
	{
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fishing_rod_durability_rejected SessionId=%s RodItemInstanceId=%s Reason=UnavailableOrBroken %s"),
			*Snapshot.FishingSessionId.ToString(), *AttemptSnapshot.RodItemInstanceId.ToString(),
			*CatLogContext::BuildControllerFields(FisherCharacter.IsValid() ? FisherCharacter->GetController() : nullptr));
		return false;
	}
	Config.FishFullEffortRodWearPerSecond = RodDefinition->FindFragment<UCatEquipmentFragment_Rod>()->BaseDurabilityWearPerSecond;
	Config.TautRodWearMultiplier = FMath::Max(1.0, RodDefinition->FindFragment<UCatEquipmentFragment_Rod>()->HighTensionWearMultiplier);
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
			static_cast<float>(Config.FishFullEffortSpeedCentimetersPerSecond), false, false, FVector::UpVector,
			(Exact.WaterSurfaceWorldPoint - RodTipWorldPosition).GetSafeNormal2D(UE_DOUBLE_SMALL_NUMBER, FVector::ForwardVector),
			ECatFishBehavior::OutwardRush, static_cast<float>(InitialState.FishEffortRatio)))
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
	// 连续出力/反馈参数与本场随机种子一并交给它，随后驱动固定步长的搏斗推进。
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
	// 这里在 Runner 启动前原子快照；两键同时按住时由 Runner 统一裁决为右键优先。
	if (const ACatfishingPlayerController* FisherController = FisherCharacter.IsValid()
		? Cast<ACatfishingPlayerController>(FisherCharacter->GetController()) : nullptr)
	{
		if (const UCatFishingCommandComponent* Commands = FisherController->GetFishingCommandComponent())
		{
			Commands->TryGetHeldFightInputStateFromAuthority(Init.bInitialPullHeld,
				Init.bInitialSlackHeld, Init.InitialInputSequence);
		}
	}
	Init.SteeringConfig = Personality->AdaptiveSteeringConfig;
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
	StaminaOwner = FisherCharacter;
	if (!EnterPhaseFromStateTree(ECatFishingPhase::HookedFight).bApplied)
	{
		// 阶段写入被拒绝（比如并发终止）：必须把已经启动的 Runner 和已初始化的体力状态全部回滚，
		// 否则会出现"Runner 在跑但阶段还停在 TrueBiteWindow"的不一致状态。
		FightRunner->Stop();
		FightRunner = nullptr;
		Snapshot.bReeling = false;
		Snapshot.bSlacking = false;
		StaminaOwner.Reset();
		bFightStaminaInitialized = false;
		AbilitySystem->RequestFishingStaminaReset();
		return false;
	}
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_fight_started SessionId=%s FightBalanceId=%s FishDefinition=%s RodDefinition=%s PerfectHook=%s PrimaryStrength=%.2f OperatorBodyMassKg=%.2f FishMassKg=%.2f MassMode=IndependentCatBodyMass FishStrengthBase=%.2f FishStrengthEffective=%.2f StrengthPerKg=%.2f ForcePerStrengthN=%.2f ExhaustedReelForceN=%.2f CatStamina=%.2f FishStamina=%.2f RodDurability=%.2f RodPhysicsLengthCm=%.2f InitialLineLengthCm=%.2f MaximumLineLengthCm=%.2f RodPose=%s CatLinearWorkCost=%.5f FishStaminaPerUnfulfilledMeter=%.5f FishFullEffortSpeedCmPerSec=%.2f FixedStepSeconds=%.3f MinimumLeverage=%.3f MaximumEndpointCorrectionSpeed=%.2f StrengthResolution=CommonLineForce StrongConfrontationRole=PresentationOnly RodFailure=DurabilityDepleted World=%s NetMode=%d Authority=%s LocalRole=%d RodActor=%s"),
		*Snapshot.FishingSessionId.ToString(),
		*FightBalance->BalanceDefinitionId.ToString(),
		*FishDefinition->FishDefinitionId.ToString(),
		*RodDefinition->EquipmentDefinitionId.ToString(),
		bPerfect ? TEXT("true") : TEXT("false"),
		Config.PrimaryOperatorCatStrength,
		Config.PrimaryOperatorMassKilograms,
		Config.FishMassKilograms,
		FrozenSelectionResult.BaseFishStrength,
		Config.FishStrength,
		Config.StrengthPerKilogram,
		Config.ForcePerStrengthNewtons,
		Config.ExhaustedReelForceNewtons,
		InitialState.CatStamina,
		InitialState.FishStamina,
		Config.RodDurability,
		Config.RodPhysicsLengthCentimeters,
		InitialState.LineLengthCentimeters,
		Config.MaximumLineLengthCentimeters,
		Snapshot.RodActor && Snapshot.RodActor->GetPresentationState().PoseMode == ECatFishingRodPoseMode::Held
			? TEXT("Held") : TEXT("Grounded"),
		Config.CatStaminaCostPerStrengthCentimeter,
		Config.FishStaminaPerUnfulfilledMeter,
		Config.FishFullEffortSpeedCentimetersPerSecond,
		Config.FixedStepSeconds,
		Config.MinimumRodLeverageMultiplier,
		Config.MaximumFishConstraintCorrectionSpeedCentimetersPerSecond,
		*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), HasAuthority() ? TEXT("true") : TEXT("false"),
		static_cast<int32>(GetLocalRole()), *GetNameSafe(Snapshot.RodActor));
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_effort_configured SessionId=%s FightBalanceId=%s Model=CatActualWorkAndTimedSupport CatPhaseMultiplier=1 "
			"FishDrainMode=UnfulfilledIntentDistance FishIntentSpeed=ActualEffortTimesFullEffortSpeed "
			"SlackRecoveryMode=RightButtonWithLineCapacityExceptExhaustedDrag SlackStaminaCost=WaivedOnlyWithLineCapacity LineLimitMode=NormalLockedContest SlackRegenPerSecond=%.3f "
			"MovementMultiplier=%.3f ReelMultiplier=%.3f RodMultiplier=%.3f HoldMultiplier=%.3f "
			"CatLoadMultiplier=%.3f "
			"CatLinearWorkCost=%.6f FishStaminaPerUnfulfilledMeter=%.6f CatRodWorkCostPerRadian=%.6f CatUnloadedWorkMultiplier=%.3f CatSupportPerSecond=%.3f "
			"ExhaustedCatEscapeSpeedMultiplier=%.3f %s"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *FightBalance->BalanceDefinitionId.ToString(),
		Config.SlackStaminaRegenPerSecond,
		Config.CatMovementStaminaMultiplier, Config.CatReelStaminaMultiplier,
		Config.CatRodStaminaMultiplier, Config.CatHoldStaminaMultiplier,
		Config.CatLoadStaminaMultiplier,
		Config.CatStaminaCostPerStrengthCentimeter, Config.FishStaminaPerUnfulfilledMeter,
		Config.CatRodStaminaCostPerStrengthRadian, Config.CatUnloadedWorkMultiplier, Config.CatSupportStaminaPerSecond,
		Config.ExhaustedCatEscapeSpeedMultiplier,
		*CatLogContext::BuildControllerFields(FisherCharacter->GetController()));
	return true;
}

bool ACatFishingSession::SetReelingFromAuthority(APlayerState* InputPlayerState,
	const int64 InputSequence, const bool bReeling)
{
	// HookedFight 与鱼力竭回收共用同一个 Runner 和输入序号域。
	if (!HasAuthority() || (Snapshot.Phase != ECatFishingPhase::HookedFight
		&& Snapshot.Phase != ECatFishingPhase::ExhaustedReel) || !FightRunner
		|| !Snapshot.RodActor || !Snapshot.RodActor->IsPrimaryOperator(InputPlayerState)
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
	const int64 InputSequence, const bool bSlacking, const FCatFishingRodAimSample* AimRebaseSample, const FGuid RequestId)
{
	if (!HasAuthority() || (Snapshot.Phase != ECatFishingPhase::HookedFight
		&& Snapshot.Phase != ECatFishingPhase::ExhaustedReel) || !FightRunner
		|| InputPlayerState != Snapshot.FisherPlayerState)
	{
		return false;
	}
	const bool bRebaseAim = AimRebaseSample && !FightRunner->IsSlackInputHeldForAuthority(InputPlayerState);
	if (bRebaseAim && (!bSlacking || !Snapshot.RodActor
		|| !Snapshot.RodActor->CanRebaseHeldAimFromAuthority(InputPlayerState, *AimRebaseSample)))
	{
		const TCHAR* Reason = !bSlacking ? TEXT("NotPress") : !Snapshot.RodActor ? TEXT("NoRod")
			: !AimRebaseSample->IsValid() ? TEXT("InvalidAimSample")
			: AimRebaseSample->RodActorId.IsValid() && AimRebaseSample->RodActorId != Snapshot.RodActor->GetPresentationState().RodActorId
				? TEXT("RodMismatch")
			: AimRebaseSample->InputEpoch != 0 && AimRebaseSample->InputEpoch != Snapshot.RodActor->GetCarrierConstraintState().AimInputEpoch
				? TEXT("AimEpochMismatch") : TEXT("StaleAimOrHeldPoseNotReady");
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fishing_rod_aim_rebase_rejected RequestId=%s InputSequence=%lld SessionId=%s RodActorId=%s "
				"AimInputEpoch=%u CurrentAimInputEpoch=%u AimSequence=%lld Reason=%s PlayerId=%d "
				"World=%s NetMode=%d Authority=true LocalRole=%d"),
			*RequestId.ToString(), InputSequence, *Snapshot.FishingSessionId.ToString(), *AimRebaseSample->RodActorId.ToString(),
			AimRebaseSample->InputEpoch, Snapshot.RodActor ? Snapshot.RodActor->GetCarrierConstraintState().AimInputEpoch : 0,
			AimRebaseSample->Sequence, Reason, InputPlayerState ? InputPlayerState->GetPlayerId() : INDEX_NONE,
			*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()));
		return false;
	}
	if (!FightRunner->SetSlacking(InputPlayerState, InputSequence, bSlacking)) return false;
	// 先验证完整转换，再改 Runner 和输入基准，最后才发布可能触发消费者的快照。
	if (bRebaseAim)
	{
		Snapshot.RodActor->RebaseHeldAimFromAuthority(InputPlayerState, *AimRebaseSample, RequestId, InputSequence);
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
	const double FishStaminaRemaining, const ECatFishMotionIntent MotionIntent)
{
	if (!HasAuthority() || IsTerminal() || (Snapshot.Phase != ECatFishingPhase::HookedFight
		&& Snapshot.Phase != ECatFishingPhase::ExhaustedReel) || !FightRunner) return;
	Snapshot.FishFightStaminaRemaining = FMath::Max(0.0, FishStaminaRemaining); // 钳制非负，防止浮点误差产生负值。
	// 磨损先写回绑定实例，公开快照只镜像写回结果；取消、切线和换人均不能退还已消耗耐久。
	bool bRodBroken = false;
	UCatEquipmentComponent* Equipment = CastEquipment.Get();
	if (Step.RodWearDelta > 0.0)
	{
		const FCatFishingUseOperationResult Wear = Equipment
			? Equipment->ApplyFishingRodWear(Snapshot.FishingSessionId, RodWearSequence + 1, Step.AbsoluteRodWear)
			: FCatFishingUseOperationResult{};
		if (!Wear.bApplied)
		{
			UE_LOG(LogCatFishing, Error,
				TEXT("Event=fishing_rod_wear_commit_failed SessionId=%s RodItemInstanceId=%s WearSequence=%lld Error=%s %s"),
				*Snapshot.FishingSessionId.ToString(), *AttemptSnapshot.RodItemInstanceId.ToString(), RodWearSequence + 1,
				*UEnum::GetValueAsString(Wear.Error),
				*CatLogContext::BuildControllerFields(FisherCharacter.IsValid() ? FisherCharacter->GetController() : nullptr));
			FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Invalidated, TEXT("Rod wear commit failed"));
			return;
		}
		RodWearSequence = Wear.WearSequence;
		Snapshot.RodDurabilityRemaining = Wear.RemainingRodDurability;
		bRodBroken = Wear.bRodBroken;
	}
	else if (!Equipment || !Equipment->GetFishingRodDurability(
		Snapshot.FishingSessionId, Snapshot.RodDurabilityRemaining, bRodBroken))
	{
		UE_LOG(LogCatFishing, Error,
			TEXT("Event=fishing_rod_durability_rejected SessionId=%s RodItemInstanceId=%s Reason=BoundInstanceUnavailable %s"),
			*Snapshot.FishingSessionId.ToString(), *AttemptSnapshot.RodItemInstanceId.ToString(),
			*CatLogContext::BuildControllerFields(FisherCharacter.IsValid() ? FisherCharacter->GetController() : nullptr));
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Invalidated, TEXT("Bound rod unavailable"));
		return;
	}
	if (bRodBroken)
	{
		ACatFishingRodActor* Rod = Snapshot.RodActor;
		if (Rod && !Rod->SetBrokenFromAuthority(true, Rod->GetPresentationState().RodActorRevision))
		{
			UE_LOG(LogCatFishing, Error, TEXT("Event=fishing_rod_broken_projection_failed SessionId=%s RodItemInstanceId=%s"),
				*Snapshot.FishingSessionId.ToString(), *AttemptSnapshot.RodItemInstanceId.ToString());
		}
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fishing_rod_broken SessionId=%s RodItemInstanceId=%s Cause=DurabilityDepleted RodDurability=%.3f AbsoluteRodWear=%.3f %s"),
			*Snapshot.FishingSessionId.ToString(), *AttemptSnapshot.RodItemInstanceId.ToString(),
			Snapshot.RodDurabilityRemaining, Step.AbsoluteRodWear,
			*CatLogContext::BuildControllerFields(FisherCharacter.IsValid() ? FisherCharacter->GetController() : nullptr));
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::RodBroken, TEXT("Rod durability depleted"));
		if (UCatFishingService* Service = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr)
		{
			Service->ReleaseRodOperators(Rod);
		}
		return;
	}
	// 钩在鱼嘴里：搏斗期间钩 Actor 跟随鱼的权威位置（含近岸/贴岸吸附后的落点），复制到所有端。
	if (Snapshot.HookActor && Snapshot.FishEncounterActor)
	{
		Snapshot.HookActor->SetActorLocation(Snapshot.FishEncounterActor->GetActorLocation());
		if (!Snapshot.HookActor->SetFishingLinePresentationFromAuthority(
			Step.LineLengthCentimeters, Step.StraightLineDistanceCentimeters,
			Step.SlackLineLengthCentimeters, static_cast<float>(Step.NormalizedTension), Step.bLineTaut,
			Step.LineTensionNewtons))
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
	Snapshot.ActiveCombinedFishingStrength = FMath::Max(0.0, Step.OperatorCatStrength);
	Snapshot.ActiveHelperCount = 0;
	Snapshot.bReeling = FightRunner->GetCatAction() == ECatFightCatAction::Pull;
	Snapshot.bSlacking = FightRunner->GetCatAction() == ECatFightCatAction::Slack;
	Snapshot.CarrierPullAccelerationCentimetersPerSecondSquared = 0.0f;
	Snapshot.CarrierAwaySpeedMultiplier = 1.0f; // 旧序列化观察字段；真实运动仅由物理身体和约束推进。
	Snapshot.ConstraintErrorCentimeters = static_cast<float>(Step.ConstraintErrorCentimeters);
	Snapshot.FishConstraintCorrectionCentimeters =
		static_cast<float>(Step.FishConstraintCorrectionCentimeters);
	RefreshFightSummary(); // 每步都重新校验参与者是否仍然合法在场（掉线/倒地会即时反映）。
	PublishSnapshot(ECatFishingSnapshotMutation::HighFrequency); // 搏斗数值每步都要尽快同步给客户端表现层。
	if (Step.Outcome == ECatFightStepOutcome::Escaped)
	{
		// 鱼距超过最大线长与逃脱余量后直接逃脱，无需先进入 NearShore。
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
		const UCatFishPickupSettings* ItemSettings = GetDefault<UCatFishPickupSettings>();
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
	const double ImmersionDepthCentimeters, ACatCharacter* AffectedCharacter)
{
	if (!HasAuthority() || IsTerminal() || (Snapshot.Phase != ECatFishingPhase::HookedFight
		&& Snapshot.Phase != ECatFishingPhase::ExhaustedReel))
	{
		return;
	}
	ACatCharacter* Character = AffectedCharacter ? AffectedCharacter : FisherCharacter.Get();
	if (!Character || !Snapshot.RodActor
		|| !Snapshot.RodActor->IsPrimaryOperator(Character->GetPlayerState())) return;
	UE_LOG(LogCatFishing, Warning,
		TEXT("Event=fishing_cat_entered_dangerous_water SessionId=%s DepthCm=%.2f Phase=%s Result=RemovalRequested World=%s Authority=%d LocalRole=%d %s"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), ImmersionDepthCentimeters,
		*UEnum::GetValueAsString(Snapshot.Phase),
		*GetNameSafe(GetWorld()), HasAuthority(), int32(GetLocalRole()),
		*CatLogContext::BuildControllerFields(Character->GetController()));
	const FGameplayTag Presentation = ResolveTerminalFisherPresentationTag(ECatFishingOutcome::CatInWater);
	if (Presentation.IsValid()) Character->Multicast_PlayCosmeticEvent(Presentation);
	if (UCatFishingService* Service = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr)
		Service->ReleaseFishingOperatorForCharacter(Character);
}

bool ACatFishingSession::CommitCatchEquipmentFromAuthority()
{
	UCatEquipmentComponent* Equipment = CastEquipment.Get();
	if (!Equipment)
	{
		return false;
	}
	const FCatFishingUseOperationResult Bait = Equipment->CommitFishingBaitDeferred(Snapshot.FishingSessionId);
	// 每步磨损已经写回同一鱼竿实例；捕获仅收口饵料，不能再重复扣耐久。
	return Bait.bApplied || Bait.Error == ECatDomainCommandError::AlreadyResolved;
}

bool ACatFishingSession::SpawnExhaustedFishPickupFromAuthority(const FVector& SurfaceLocation)
{
	UWorld* World = GetWorld();
	const UCatFishPickupSettings* Settings = GetDefault<UCatFishPickupSettings>();
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
	// 可拾取鱼沿用水中最后的水平朝向，并清掉可能的俯仰和侧翻；Actor 根保持直立，避免与 Pickup 自己应用的网格侧翻叠加。
	// 旋转写在服务器生成的 Actor 上而不是只转客户端 Mesh，ReplicatedMovement 会让所有玩家看到同一结果。
	FRotator LandedRotation = Snapshot.FishEncounterActor
		? Snapshot.FishEncounterActor->GetActorRotation() : FRotator::ZeroRotator;
	LandedRotation.Pitch = 0.0;
	// 世界鱼自己恢复侧躺网格和盒形碰撞；生成入口只提供水平朝向与地面位置。
	LandedRotation.Roll = 0.0;
	ACatFishPickupActor* Pickup = World->SpawnActor<ACatFishPickupActor>(
		ACatFishPickupActor::StaticClass(), SpawnLocation, LandedRotation, SpawnParams);
	TArray<FString> Participants;
	if (!CatchFisherStableNetId.IsEmpty())
	{
		Participants.Add(CatchFisherStableNetId);
	}
	if (!Pickup || !Pickup->InitializeFromAuthority(Snapshot.FishingSessionId, FGuid::NewGuid(),
		FishDefinition, FishWeightKilograms, FishVisualScale, AttemptSnapshot.WaterRegion.RegionId, Participants,
		Snapshot.FishEncounterActor->GetPresentationState().GroundNormal))
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
	}
	StaminaOwner.Reset(); // 所有允许放下的阶段都解除恢复域，不能重置本人之后另一场的体力。
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

	FisherStableNetId.Reset();
	FisherCharacter.Reset();
	Snapshot.FisherPlayerState = nullptr;
	PublishPrimarySummaryFromAuthority(0.0, 0.0, 0.0, false);
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
		|| ACatFishPickupActor::FindCarriedFish(ScoopingCharacter) || ACatFishGuardActor::FindCarriedGuard(ScoopingCharacter))
	{
		return false;
	}

	TArray<FString> Participants;
	if (!CatchFisherStableNetId.IsEmpty())
	{
		Participants.Add(CatchFisherStableNetId);
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
		const double RodDurabilityBefore = Snapshot.RodDurabilityRemaining;
		const double NormalizedLoadBefore = Snapshot.NormalizedLineLoad;
		// 切线直接抢占终态写口；Finalize 会先写入 LineCut，再停止 Runner/StateTree，避免 Interrupted
		// 同步回调在同一帧抢先写成另一种终态，保证“第一个终态提交者获胜”的结果可重放。
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::LineCut,
			TEXT("Fishing line cut by operator"));
		Result.bCommitted = true;
		Result.Error = ECatFishingCommandError::None;
		UE_LOG(LogCatFishing, Display,
			TEXT("Event=fishing_line_cut_committed SessionId=%s RequestId=%s RodActorId=%s PhaseBefore=%s "
				"Revision=%lld RodDurabilityBefore=%.3f RodDurabilityAfter=%.3f NormalizedLoadBefore=%.3f %s"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			*Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			Snapshot.RodActor
				? *Snapshot.RodActor->GetPresentationState().RodActorId.ToString(EGuidFormats::DigitsWithHyphens)
				: TEXT("None"),
			*UEnum::GetValueAsString(PhaseBefore), Snapshot.Revision,
			RodDurabilityBefore, Snapshot.RodDurabilityRemaining, NormalizedLoadBefore,
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
	// 沿既有终局恢复规则，只通知仍归本会话负责的主控体力池。
	if (ACatCharacter* Participant = StaminaOwner.Get())
	{
		if (UCatAbilitySystemComponent* AbilitySystem = Participant->GetCatAbilitySystemComponent())
		{
			AbilitySystem->RequestFishingStaminaReset();
		}
	}
	StaminaOwner.Reset();

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

// World 清理流程：先停 FightRunner 并清 Bite/Probe/TrueBite 计时器，再停止仍在运行的 StateTree。
// authority 随后释放本会话的 Equipment use、请求仍可达参与者重置钓鱼体力并清弱引用；最后重置 ItemsService 和钓手引用后交给 Super，不补发捕获事务。
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
		if (UCatEquipmentComponent* Equipment = CastEquipment.Get(); Equipment
			&& Equipment->IsFishingUseActive(Snapshot.FishingSessionId))
		{
			Equipment->ReleaseFishingUse(Snapshot.FishingSessionId);
		}
		if (ACatCharacter* Participant = StaminaOwner.Get())
		{
			if (UCatAbilitySystemComponent* AbilitySystem = Participant->GetCatAbilitySystemComponent())
			{
				AbilitySystem->RequestFishingStaminaReset();
			}
		}
		StaminaOwner.Reset();
	}
	ItemsService.Reset();
	FisherCharacter.Reset();

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
	const UWorld* World = GetWorld();
	const int32 RodDurabilityBand = FMath::FloorToInt(Snapshot.RodDurabilityRemaining / 5.0);
	if (World && Snapshot.RodActor && (RodDurabilityBand != LastReceivedRodDurabilityBand
		|| (IsTerminal() && !bReceivedRodTerminal)))
	{
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_rod_durability_received SessionId=%s RodItemInstanceId=%s RodDurability=%.3f Phase=%s Outcome=%s World=%s NetMode=%d Authority=%s LocalRole=%d Result=Replicated"),
			*Snapshot.FishingSessionId.ToString(), *Snapshot.RodActor->GetPresentationState().ItemInstanceId.ToString(),
			Snapshot.RodDurabilityRemaining, *UEnum::GetValueAsString(Snapshot.Phase), *UEnum::GetValueAsString(Snapshot.Outcome),
			*GetNameSafe(World), static_cast<int32>(World->GetNetMode()), HasAuthority() ? TEXT("true") : TEXT("false"),
			static_cast<int32>(GetLocalRole()));
		LastReceivedRodDurabilityBand = RodDurabilityBand;
		bReceivedRodTerminal = IsTerminal();
	}
	const bool bFightPhase = Snapshot.Phase == ECatFishingPhase::HookedFight
		|| Snapshot.Phase == ECatFishingPhase::ExhaustedReel;
	if (World && bFightPhase && World->GetTimeSeconds() >= NextStaminaReceivedDiagnosticSeconds)
	{
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_fish_stamina_received SessionId=%s FishStamina=%.4f Phase=%s Slacking=%s Reeling=%s "
				"RodActor=%s World=%s NetMode=%d Authority=%s LocalRole=%d Result=Replicated"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), Snapshot.FishFightStaminaRemaining,
			*UEnum::GetValueAsString(Snapshot.Phase), Snapshot.bSlacking ? TEXT("true") : TEXT("false"),
			Snapshot.bReeling ? TEXT("true") : TEXT("false"), *GetNameSafe(Snapshot.RodActor), *GetNameSafe(World),
			static_cast<int32>(World->GetNetMode()), HasAuthority() ? TEXT("true") : TEXT("false"),
			static_cast<int32>(GetLocalRole()));
		NextStaminaReceivedDiagnosticSeconds = World->GetTimeSeconds() + 1.0;
	}
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

void ACatFishingSession::RefreshPrimaryControlFromAuthority()
{
	if (!HasAuthority() || IsTerminal()) return;
	RefreshFightSummary();
	PublishSnapshot(ECatFishingSnapshotMutation::Discrete);
}

void ACatFishingSession::EndFixedStepMutationBoundary()
{
	bFixedStepMutationBoundary = false;
	if (UCatFishingService* Service = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr)
		Service->FlushDeferredOperatorRemovalsFromAuthority();
}

void ACatFishingSession::PublishPrimarySummaryFromAuthority(const double Strength,
    const double Stamina, const double StaminaMaximum, const bool bOperatorPresent)
{
    if (!HasAuthority() || IsTerminal() || !FMath::IsFinite(Strength) || Strength < 0.0
        || !FMath::IsFinite(Stamina) || Stamina < 0.0
        || !FMath::IsFinite(StaminaMaximum) || StaminaMaximum < Stamina) return;
    // 旧反射摘要只投影0或1人，供尚未完全审计的二进制消费者加载；不是成员写入口。
    Snapshot.FightParticipantCount = bOperatorPresent ? 1 : 0;
    Snapshot.CombinedFishingStrength = bOperatorPresent ? Strength : 0.0;
    Snapshot.CombinedFightStamina = bOperatorPresent ? Stamina : 0.0;
    Snapshot.CombinedFightStaminaMaximum = bOperatorPresent ? StaminaMaximum : 0.0;
    // 随本固定步HandleFightRunnerStep发布，不暴露中间结算。
}

bool ACatFishingSession::RefreshFightSummary()
{
    if (IsFightRunnerRunning()) return false;
    APlayerState* Player = Snapshot.RodActor ? Snapshot.RodActor->GetPresentationState().OperatorPlayerState.Get() : nullptr;
    ACatCharacter* Character = Player && Player == Snapshot.FisherPlayerState ? Cast<ACatCharacter>(Player->GetPawn()) : nullptr;
    UCatAbilitySystemComponent* ASC = Character ? Character->GetCatAbilitySystemComponent() : nullptr;
    bool bPresent = ASC && UCatFishingService::CanControllerStartFishingAction(Character->GetController());
    double Strength = 0.0, Stamina = 0.0, Maximum = 0.0;
    if (bPresent)
    {
        Stamina = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
        Strength = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFishingStrengthAttribute());
        Maximum = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute());
        if (!FMath::IsFinite(Maximum) || Maximum <= 0.0 || !FMath::IsFinite(Stamina)
            || Stamina < 0.0 || Stamina > Maximum || !FMath::IsFinite(Strength) || Strength < 0.0)
        {
            UE_LOG(LogCatFishing, Warning,
                TEXT("Event=fishing_primary_summary_rejected SessionId=%s PlayerId=%d Reason=AttributesOutsideBounds World=%s NetMode=%d Authority=%d LocalRole=%d"),
                *Snapshot.FishingSessionId.ToString(), Player->GetPlayerId(), *GetNameSafe(GetWorld()), int32(GetNetMode()), HasAuthority(), int32(GetLocalRole()));
            return false;
        }
        if (Stamina == 0.0) Strength = 0.0;
    }
    const bool bChanged = Snapshot.FightParticipantCount != (bPresent ? 1 : 0)
        || Snapshot.CombinedFishingStrength != Strength || Snapshot.CombinedFightStamina != Stamina
        || Snapshot.CombinedFightStaminaMaximum != Maximum;
    PublishPrimarySummaryFromAuthority(Strength, Stamina, Maximum, bPresent);
    return bChanged;
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
