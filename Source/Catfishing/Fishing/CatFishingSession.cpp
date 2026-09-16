#include "Fishing/CatFishingSession.h"
#include "Growth/CatGrowthComponent.h"
#include "AbilitySystem/Effects/CatFishingScoopCooldownEffect.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Fishing/Integration/CatFishingResolutionSubsystem.h"
#include "Condition/CatConditionComponent.h"
#include "Collection/CatRunFishCollectionComponent.h"
#include "Inventory/CatInventorySettings.h"
#include "Equipment/Fragments/CatEquipmentFragment_Rod.h"
#include "Equipment/Fragments/CatEquipmentFragment_Bait.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Equipment/Fragments/CatEquipmentFragment_Float.h"
#include "Fishing/Simulation/CatFishingBiteTimingModel.h"
#include "Fishing/Simulation/CatFishBehaviorProfile.h"

#include "Character/CatCharacter.h"
#include "Collection/CatRunImprintService.h"
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
#include "FishContainers/CatFishContainerService.h"
#include "FishContainers/CatFishPickupSettings.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "FishContainers/CatFishGuardActor.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Social/CatSocialService.h"
#include "Net/UnrealNetwork.h"
#include "StateTree.h"
#include "TimerManager.h"

// 墓碑（2026-09-13）：玩法常量移至 UCatFishingSettings 与 DefaultGame.ini；默认值及服务器裁决顺序不变。

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
		// 09-11 裁决④：进搏斗不再把主控体力补满，搏斗体力是跨竿资源，只按当前余额开打。
		// 这里保留 fail-closed 的 ASC 在场校验与体力域归属登记，但不写任何体力值。
		if (!FisherCharacter.IsValid() || !FisherCharacter->GetCatAbilitySystemComponent())
		{
			Result.Error = ECatDomainCommandError::DependencyUnavailable;
			return Result;
		}
		bFightStaminaInitialized = true;
		StaminaOwner = FisherCharacter; // 本场只登记主控自己的体力域归属。
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
	if (NewPhase == ECatFishingPhase::ExhaustedReel)
	{
		// 鱼一翻肚就开始算苏醒时限；拖动中计时照走（§5.3:260），只有真的上岸才不再苏醒。
		ScheduleExhaustedRevivalTimerFromAuthority();
	}
	Result.bApplied = true;
	Result.CurrentPhase = NewPhase;
	Result.Error = ECatDomainCommandError::None;
	Result.Revision = Snapshot.Revision;
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_phase_entered SessionId=%s Phase=%s Revision=%lld"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *UEnum::GetValueAsString(NewPhase), Snapshot.Revision);
	return Result;
}

// 当前显式主控接管同一会话；物理抓握本身不会调用此入口或转让会话。
bool ACatFishingSession::ResumePrimaryControlFromAuthority(AController* NewFisherController)
{
	ClearCancelHoldFromAuthority();
	const FString NewStableNetId = ResolveStableNetId(NewFisherController);
	ACatCharacter* NewCharacter = NewFisherController ? Cast<ACatCharacter>(NewFisherController->GetPawn()) : nullptr;
	UCatAbilitySystemComponent* NewASC = NewCharacter ? NewCharacter->GetCatAbilitySystemComponent() : nullptr;
	const ACatfishingGameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	double NewStrength = NewASC ? NewASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFishingStrengthAttribute()) : 0.0;
	double NewStamina = NewASC ? NewASC->GetTotalFightStamina() : 0.0;
	// 墓碑（2026-09-14）：不再只读绿段。Knowledge/Design/设计修改记录.md 2026-09-13 裁决②④⑥：
	// 绿＋黄提供体力，双段归零也可接管；控制资格与实际出力分离。
	const bool bCapable = NewASC && GameMode && GameMode->CanAcceptGameplayCommand(NewFisherController)
		&& UCatFishingService::CanControllerStartFishingAction(NewFisherController)
		&& FMath::IsFinite(NewStrength) && NewStrength >= 0.0
		&& FMath::IsFinite(NewStamina) && NewStamina >= 0.0;
	const bool bFightTakeover = Snapshot.Phase == ECatFishingPhase::HookedFight
		|| Snapshot.Phase == ECatFishingPhase::ExhaustedReel;
	// 姿态与会话阶段正交；当前主控在允许阶段可以接管同一会话。
	const bool bTransferablePhase = Snapshot.Phase == ECatFishingPhase::CastFlight
		|| Snapshot.Phase == ECatFishingPhase::Waiting || Snapshot.Phase == ECatFishingPhase::Probe
		|| Snapshot.Phase == ECatFishingPhase::TrueBiteWindow || bFightTakeover
		|| Snapshot.Phase == ECatFishingPhase::NearShore || Snapshot.Phase == ECatFishingPhase::AutoHauling
		|| Snapshot.Phase == ECatFishingPhase::ExhaustedReel;
	if (!HasAuthority() || IsTerminal() || !bTransferablePhase || NewStableNetId.IsEmpty() || !bCapable
		|| !NewCharacter || !NewFisherController->PlayerState || !Snapshot.RodActor
        || !Snapshot.RodActor->IsPrimaryOperator(NewFisherController->PlayerState))
	{
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fishing_primary_resume_rejected SessionId=%s Phase=%s Transferable=%s FightCapable=%s NewStableIdValid=%s %s"),
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
			? NewAbilitySystem->GetTotalFightStaminaCapacity() : 0.0;
		const bool bStaminaAttributeReady = FMath::IsFinite(NewStaminaMaximum) && NewStaminaMaximum > 0.0;
		if (!FightRunner || !FightRunner->IsRunning() || !NewAbilitySystem || !bStaminaAttributeReady)
		{
			UE_LOG(LogCatFishing, Warning,
				TEXT("Event=fishing_primary_resume_rejected SessionId=%s Reason=StaminaOrRunnerUnavailable Runner=%s StaminaAttribute=%s %s"),
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
		NewStamina = NewAbilitySystem->GetTotalFightStamina();
		if (!FightRunner->ResumePrimaryFromAuthority(NewFisherController->PlayerState,
			NewAbilitySystem, NewStrength,
			NewStaminaMaximum, NewStamina, InitialInputSequence, false, false))
		{
			UE_LOG(LogCatFishing, Warning,
				TEXT("Event=fishing_primary_resume_rejected SessionId=%s Reason=RunnerRebindFailed Strength=%.3f Stamina=%.3f StaminaMaximum=%.3f InputSequence=%lld %s"),
				*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), NewStrength,
				NewStamina, static_cast<double>(NewStaminaMaximum), InitialInputSequence,
				*CatLogContext::BuildControllerFields(NewFisherController));
			return false;
		}

		// 旧操作手离开后保留当下体力；同时解除他在本会话的体力域归属，
		// 防止他去另一根竿后仍被旧会话的收尾当成自己人记账。
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
	// 当前提竿者的完美窗跟随操作位；不重新开始普通响应计时，也不延长其截止时间。
	RefreshGrowthFromAuthority(NewCharacter, ECatGrowthOptionId::PerfectWindow, 0.0);
	Snapshot.FisherPlayerState = NewFisherController->PlayerState;
	// 主控一换，上一块「谁来接一下」的牌子就作废：那是上一任主钓手挂的，新主控没表过态。
	// 这里直接清字段而不调 ClearHandoffRequestFromAuthority，是因为下面统一发布一次快照，不重复发两遍。
	Snapshot.HandoffRequestedByPlayerState = nullptr;

	RefreshFightSummary();
	PublishSnapshot(ECatFishingSnapshotMutation::Discrete);
	// 换主后只重查鱼竿承载门槛；鱼与猫的位移继续由原 Runner 求解。
	if (bFightTakeover && Snapshot.Phase == ECatFishingPhase::HookedFight
		&& EvaluateRodStrengthFromAuthority(TEXT("PrimaryHandover")))
	{
		return true;
	}
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_primary_resumed SessionId=%s Phase=%s Mode=%s OldFisher=%s NewStrength=%.3f NewFightStamina=%.3f %s"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
		*UEnum::GetValueAsString(Snapshot.Phase),
		bFightTakeover ? TEXT("FightRunnerRebind") : TEXT("WaitingIdentityTransfer"),
		*OldFisherLogValue, NewStrength, NewStamina,
		*CatLogContext::BuildControllerFields(NewFisherController));
	return true;
}

// 换人请求流程（多人钓鱼附篇 §2.4）：只有当前主控能挂牌，再按一次就摘牌。
// 请求无时限挂起、没有超时出口，也不给这一竿附加任何状态：体力照扣、归零照走持竿者落水。
// 谁能接、接的时候查什么（体力 50% 门槛），归 Service 的接手入口，本函数不预判替补。
bool ACatFishingSession::ToggleHandoffRequestFromAuthority(AController* PrimaryController)
{
	const FString RequesterId = ResolveStableNetId(PrimaryController);
	if (!HasAuthority() || IsTerminal() || RequesterId.IsEmpty() || RequesterId != FisherStableNetId
		|| !PrimaryController->PlayerState)
	{
		return false;
	}
	const bool bAlreadyRequested = Snapshot.HandoffRequestedByPlayerState != nullptr;
	Snapshot.HandoffRequestedByPlayerState = bAlreadyRequested ? nullptr : PrimaryController->PlayerState;
	PublishSnapshot(ECatFishingSnapshotMutation::Discrete);
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_handoff_request_%s SessionId=%s Phase=%s %s"),
		bAlreadyRequested ? TEXT("cancelled") : TEXT("raised"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
		*UEnum::GetValueAsString(Snapshot.Phase), *CatLogContext::BuildControllerFields(PrimaryController));
	return true;
}

// 换人请求清理流程：幂等。会话终止、本竿结束、主控换人都走到这里；没挂牌时什么也不做，不空发快照。
void ACatFishingSession::ClearHandoffRequestFromAuthority(const TCHAR* Reason)
{
	if (!HasAuthority() || Snapshot.HandoffRequestedByPlayerState == nullptr)
	{
		return;
	}
	Snapshot.HandoffRequestedByPlayerState = nullptr;
	PublishSnapshot(ECatFishingSnapshotMutation::Discrete);
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_handoff_request_cleared SessionId=%s Reason=%s"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), Reason);
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
	// 抄网范围优先绑定统一 Use 指定的本人背包实例；旧入口没有实例时才退回服务器装备投影里的已选抄网定义。
	const UCatInventoryComponent* ScooperInventory = ScoopingCharacter ? ScoopingCharacter->GetInventoryComponent() : nullptr;
	const FCatInventoryEntry* RequestedScoopEntry = Command.RequestedScoopItemInstanceId.IsValid() && ScooperInventory
		? ScooperInventory->GetInventoryEntryAtSlot(ScooperInventory->FindInventorySlotIndexFromInstanceId(Command.RequestedScoopItemInstanceId)) : nullptr;
	const UCatEquipmentDefinition* RequestedScoopDefinition = RequestedScoopEntry && RequestedScoopEntry->Instance
		&& RequestedScoopEntry->Instance->GetItemInstanceId() == Command.RequestedScoopItemInstanceId
		? Cast<UCatEquipmentDefinition>(RequestedScoopEntry->Instance->GetItemDefinition()) : nullptr;
	const bool bScoopReachReady = Command.RequestedScoopItemInstanceId.IsValid()
		? UCatFishingAimLibrary::TryResolveScoopReach(RequestedScoopDefinition, ScoopReachCentimeters)
		: UCatFishingAimLibrary::TryResolveScoopReach(ScooperEquipment, ScoopReachCentimeters);
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
			Encounter->GetFishingCollisionCenter(), Settings->ScoopTraceChannel, SightParams);
	}
	const FVector GroundQueryLocation = GroundHit.bBlockingHit ? GroundHit.ImpactPoint : BodyFootLocation;
	const FCatWaterSpatialResult GroundSpatial = Water && ScoopingCharacter && AttemptSnapshot.WaterRegion.IsValid()
		? Water->QueryShoreRelation(GroundQueryLocation, AttemptSnapshot.WaterRegion)
		: FCatWaterSpatialResult{};
	const double FishRadius = FishDefinition ? FishDefinition->ScoopTargetRadiusCentimeters : 0.0;
	const FVector FishLocation = Encounter ? Encounter->GetFishingCollisionCenter() : FVector::ZeroVector;
	// 抄鱼与拾取共用一嘴一鱼；背包中的鱼护不占嘴部槽位（道具:64）。
	const bool bMouthFree = ScoopingCharacter && ScoopingCharacter->GetMouthCarriedActor() == nullptr;
	// 高差单独算一遍：DoesScoopRayReachFish 内部也会因为高差返回 false，光看它分不清「没对准」还是「站太高」。
	// 拒绝原因要拆开给玩家提示（钓鱼规则 §5.5:273），所以这里把垂直约束提成独立谓词，判定口径仍是同一个上限。
	const bool bVerticalDeltaWithinLimit = Settings && Encounter
		&& (Settings->MaximumScoopVerticalDeltaCentimeters <= 0.0
			|| FMath::Abs(FishLocation.Z - ScooperLocation.Z) <= Settings->MaximumScoopVerticalDeltaCentimeters);
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
		// 更早的阶段（Waiting/Probe/TrueBiteWindow）不开放：尚未有效提钩，没有可抄的鱼实体，
		// 抄它会绕过整个提竿机制。
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
		// 原因拆成六项交给玩家提示（钓鱼规则 §5.5:273）：前四项几何类玩家看到「没够着」，
		// 嘴里有鱼与不在岸上各自有话说。判序按「玩家最可能先改的动作」排：先让他放下嘴里的鱼、再站上岸、
		// 再站平、再看清、再走近或别站高，最后才是对准。配置未裁（抄网射程/可捞圆半径为 0）仍留 None，
		// 那不是玩家能改的事，走原来的几何失败兜底。
		if (!bMouthFree)
		{
			Result.RejectReason = ECatScoopRejectReason::MouthOccupied;
		}
		else if (!ScooperSpatial.bSucceeded || ScooperSpatial.Containment != ECatWaterContainment::Outside)
		{
			Result.RejectReason = ECatScoopRejectReason::NotOnShore;
		}
		else if (!bValidGround)
		{
			Result.RejectReason = ECatScoopRejectReason::GroundTooSteep;
		}
		else if (!bHasLineOfSight)
		{
			Result.RejectReason = ECatScoopRejectReason::LineOfSightBlocked;
		}
		else if (!bVerticalDeltaWithinLimit)
		{
			Result.RejectReason = ECatScoopRejectReason::VerticalDeltaTooLarge;
		}
		else if (!bRayReachesFish && bScoopReachReady && FishRadius > 0.0)
		{
			Result.RejectReason = ECatScoopRejectReason::OutOfReach;
		}
		// 逐项列出失败谓词：抢抄拒绝原因众多且此前完全静默，排查成本太高。
		// 额外打出水平距离与高度差的实测值：RayReachesFish=0 时光看谓词分不清是"没对准"、"太远"还是"站太高"。
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=scoop_rejected SessionId=%s RequestId=%s Phase=%s ExpectedRevision=%lld ActualRevision=%lld "
				"FightCapable=%d Character=%d MouthFree=%d ScoopReachReady=%d "
				"FishRadiusSet=%d ScooperOnLand=%d RayReachesFish=%d LineOfSight=%d ValidGround=%d "
				"VerticalWithinLimit=%d RejectReason=%s "
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
			bVerticalDeltaWithinLimit ? 1 : 0, *UEnum::GetValueAsString(Result.RejectReason),
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
		// 渔获收口：确认消耗饵料并按 §4.4（:203）另扣 1 点竿基础磨损；失败必须终止，避免世界鱼与装备事实分叉。
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
		|| !Attempt.RodActor->IsPrimaryOperator(FisherController->PlayerState))
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
	// 鱼身份留空到咬钩计时到点：那一刻抽鱼并进入试探期；实体留到有效提钩时生成。
	FisherCharacter = InFisherCharacter;
	CastEquipment = InFisherCharacter->GetEquipmentComponent(); // 绑定扣饵来源/会话协调器；它已记录真实竿宿主，物理抓握不重新绑定。
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

// 咬钩可用性刷新流程：入夜/翻天遮罩把「还没真咬的竿」按各自阶段收口。
// Waiting：清掉尚未到点的等待计时，浮漂回平静，竿留在场上等天亮。
// Probe：按空竿收回（钓鱼规则 §7 入夜行:337「入夜瞬间处于试探期的竿按空竿收回，不损饵、已抽库存不回」）。
//   2026-09-12 之前试探期长度是 0，这一格根本不存在；现在它有 2～4 秒，必须真的收回，
//   否则入夜后还会照常开真咬窗，与「入夜不再产生新咬钩」相反。饵没扣过，FinalizeSession 的
//   ReleaseFishingUse 会把它退回；已抽的窝点库存按设计不回。
// 真咬待响应属于「进行中」，允许打完，不在本入口收口。
void ACatFishingSession::RefreshBiteAvailabilityFromAuthority()
{
	if (!HasAuthority() || IsTerminal() || bOpeningTrueBiteWindow
		|| (Snapshot.Phase != ECatFishingPhase::Waiting && Snapshot.Phase != ECatFishingPhase::Probe)) return;
	const ACatfishingGameModeBase* Mode = GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>();
	if (Snapshot.Phase == ECatFishingPhase::Probe)
	{
		if (!Mode || !Mode->CanGenerateNewFishingBites())
		{
			GetWorldTimerManager().ClearTimer(ProbeStayTimerHandle);
			UE_LOG(LogCatFishing, Log,
				TEXT("Event=fishing_probe_recalled SessionId=%s Opportunity=%u Fish=%s Result=NightfallEmptyHook"),
				*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), BiteOpportunitySequence,
				*Snapshot.FishDefinitionId.ToString());
			FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::EmptyHook,
				TEXT("Nightfall recalled the probing rod as an empty hook"));
		}
		return;
	}
	if (Snapshot.Phase != ECatFishingPhase::Waiting) return;
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
	GetWorldTimerManager().ClearTimer(ProbeStayTimerHandle);
	if (Snapshot.HookActor)
		Snapshot.HookActor->SetBobberPresentationModeFromAuthority(ECatFishingBobberPresentationMode::Calm);
	if (bHadTimers)
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_bite_wait_cleared SessionId=%s CastAttemptId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s Result=WaitingWithoutNewBites"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *Snapshot.CastAttemptId.ToString(), *GetNameSafe(GetWorld()),
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
	// Waiting 由首次抛竿或入夜撤销未真咬的 Probe 进入；超时是终局，不再循环。
	// 已有实体或选鱼事实时拒绝重新抽取。
	if (Snapshot.FishEncounterActor || FishDefinition || SelectionResolution == ECatFishSelectionResolution::Selected)
	{
		return RejectSchedule(TEXT("FishAlreadySelected"));
	}
	GetWorldTimerManager().ClearTimer(TrueBiteTimerHandle);
	GetWorldTimerManager().ClearTimer(ProbeStayTimerHandle);
	bTrueBiteWindowAcceptingHook = false;
	SelectionResolution = ECatFishSelectionResolution::None;
	FrozenSelectionContext = FCatFishSelectionContext{};
	FrozenSelectionResult = FCatFishSelectionResult{};
	FishWeightKilograms = 0.0;
	FishVisualScale = 1.0;
	Snapshot.WindowEndsServerTime = 0.0;
	Snapshot.PerfectWindowEndsServerTime = 0.0;
	Snapshot.FishDefinitionId = NAME_None;
	Snapshot.FishWeightKilograms = 0.0;
	Snapshot.FishStrength = 0.0;
	Snapshot.bGiant = false;
	Snapshot.FishFightStaminaRemaining = 0.0;
	FishFightStaminaInitial = 0.0;
	Snapshot.NormalizedFishStamina = 0.0;
	Snapshot.bPerfectHook = false;
	Snapshot.FishMotionIntent = ECatFishMotionIntent::None;
	Snapshot.FishLineAlignment = 0.0f;
	Snapshot.NormalizedLineLoad = 0.0f;
	Snapshot.BiteSignalStability = 0.0f; // 回到等待期就不再有咬钩信号，下一次真咬重新发布。
	Snapshot.bStrongConfrontation = false;
	// 夜间抛竿或撤销试探后正常进入 Waiting，但不创建新的咬钩机会或消费随机数。
	const ACatfishingGameModeBase* Mode = GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>();
	if (!Mode || !Mode->CanGenerateNewFishingBites())
	{
		if (!EnterPhaseFromStateTree(ECatFishingPhase::Waiting).bApplied) return false;
		RefreshBiteAvailabilityFromAuthority();
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_bite_schedule_suppressed SessionId=%s CastAttemptId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s Result=WaitingWithoutNewBites"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *Snapshot.CastAttemptId.ToString(), *GetNameSafe(GetWorld()),
			GetNetMode(), HasAuthority(), int32(GetLocalRole()), *GetName());
		return true;
	}

	// 每个咬钩机会使用可重放的独立随机流，入夜撤销后次日重新采样。
	++BiteOpportunitySequence;
	if (BiteOpportunitySequence == 0) ++BiteOpportunitySequence; // 极端溢出时仍保留 0 作为“尚未初始化”。
	// 每个咬钩机会一个稳定键；剪影 Grant 按它去重，入夜收回后重排的下一轮是另一次「碰上」。
	CurrentBiteEncounterId = FGuid::NewGuid();
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
	const FName WaitingBaitDefinitionId = CastEquipment.IsValid()
		? CastEquipment->GetCurrentFishingBaitDefinitionId(Snapshot.FishingSessionId) : NAME_None;
	if (const UCatEquipmentDefinition* Bait = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentDefinition>(WaitingBaitDefinitionId))
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
	WaitSeconds *= 1.0 + GetFisherGrowthMagnitude(ECatGrowthOptionId::BiteInterval);
	const FCatFishingCastTrajectory& Flight = Snapshot.HookActor->GetPresentationState().CastTrajectory;
	const double RemainingFlightSeconds = FMath::Max(0.0,
		Flight.StartedServerTime + Flight.DurationSeconds - GetWorld()->GetTimeSeconds());
	const double WarningDelay = RemainingFlightSeconds + FMath::Max(0.0, WaitSeconds - Distribution.WarningSeconds);
	const double Delay = RemainingFlightSeconds + WaitSeconds;
	GetWorldTimerManager().ClearTimer(BiteWarningTimerHandle);
	GetWorldTimerManager().ClearTimer(ProbeTimerHandle);
	GetWorldTimerManager().ClearTimer(ProbeStayTimerHandle);
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
		TEXT("Event=fishing_bite_scheduled Model=ChumMeanAnchors SessionId=%s CastAttemptId=%s Opportunity=%u Seed=%llu World=%s WorldNetMode=%d Authority=%d LocalRole=%d Actor=%s Hook=%s Region=%s Landing=%s SampleServerTime=%.3f ChumFields=%d ChumFishy=%.6f ChumFragrant=%.6f ChumFermented=%.6f TotalChum=%.6f NeutralMeanSeconds=%.6f ExpectedMeanSeconds=%.6f RatePerSecond=%.9f Bait=%s BaitRateMultiplier=%.3f BaitMinimumMultiplier=%.3f MinimumCalmSeconds=%.3f WarningSeconds=%.3f MaximumWaitSeconds=%.3f WaitSeconds=%.6f RemainingFlightSeconds=%.6f WarningAtServerTime=%.6f ProbeAtServerTime=%.6f %s"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *Snapshot.CastAttemptId.ToString(EGuidFormats::DigitsWithHyphens), BiteOpportunitySequence,
		CurrentBiteRandomSeed, *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), static_cast<int32>(GetLocalRole()),
		*GetName(), *GetNameSafe(Snapshot.HookActor), *AttemptSnapshot.WaterRegion.RegionId.ToString(),
		*AttemptSnapshot.ServerCorrectedLandingWorldPoint.ToString(), ChumSample.SampleServerTime,
		ChumSample.ContributingFieldCount, ChumSample.EffectiveChumVector.Fishy,
		ChumSample.EffectiveChumVector.Fragrant, ChumSample.EffectiveChumVector.Fermented, TotalChum,
		Distribution.NeutralMeanSeconds, Distribution.ExpectedMeanSeconds, Distribution.RatePerSecond,
		*WaitingBaitDefinitionId.ToString(), BaitRateMultiplier, BaitMinimumDelayMultiplier,
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

// 墓碑（2026-09-14，T10；钓鱼规则 §3.4）：Bite 模板不再拥有逐鱼试探期。
// 2026-09-14 暂定正式值：资产留 0 先取目录档位默认，两层均缺配才随机兜底；错误值不能伪装成未填。
bool ACatFishingSession::TryResolveProbeDurationSeconds(double& OutProbeSeconds, const TCHAR** OutSource) const
{
	OutProbeSeconds = 0.0;
	if (OutSource) *OutSource = TEXT("Unavailable");
	if (!FishDefinition) return false;
	const double Resolved = GetDefault<UCatFishCatalogSettings>()->ResolveBiteTiming(*FishDefinition).ProbeDurationSeconds;
	if (!FMath::IsFinite(Resolved) || Resolved < 0.0) return false;
	if (Resolved > 0.0)
	{
		OutProbeSeconds = Resolved;
		if (OutSource)
		{
			const auto* Override = GetDefault<UCatFishCatalogSettings>()->BiteTimingOverridesByFishDefinitionId.Find(FishDefinition->FishDefinitionId);
			*OutSource = FishDefinition->ProbeDurationSeconds != 0.0 ? TEXT("Asset")
				: Override && Override->ProbeDurationSeconds != 0.0 ? TEXT("FishOverride") : TEXT("RarityDefault");
		}
		return true;
	}
	const FVector2D& Range = GetDefault<UCatFishingSettings>()->ProbeDurationRangeSeconds;
	if (!FMath::IsFinite(Range.X) || !FMath::IsFinite(Range.Y) || Range.X <= 0.0 || Range.Y < Range.X) return false;
	FRandomStream ProbeRandom(static_cast<int32>(CurrentBiteRandomSeed ^ 0x50726F62ull));
	OutProbeSeconds = ProbeRandom.FRandRange(Range.X, Range.Y);
	if (OutSource) *OutSource = TEXT("LegacyFallback");
	return true;
}

bool ACatFishingSession::TryResolveTrueBiteWindowSeconds(double& OutSeconds, const TCHAR** OutSource) const
{
	OutSeconds = 0.0;
	if (OutSource) *OutSource = TEXT("Unavailable");
	if (!FishDefinition) return false;
	const double Resolved = GetDefault<UCatFishCatalogSettings>()->ResolveBiteTiming(*FishDefinition).TrueBiteWindowSeconds;
	if (!FMath::IsFinite(Resolved) || Resolved < 0.0) return false;
	if (Resolved > 0.0)
	{
		if (Resolved < 8.0 || Resolved > 15.0) return false; // 钓鱼规则 §3.4：资产和档位默认使用同一设计区间。
		OutSeconds = Resolved;
		if (OutSource)
		{
			const auto* Override = GetDefault<UCatFishCatalogSettings>()->BiteTimingOverridesByFishDefinitionId.Find(FishDefinition->FishDefinitionId);
			*OutSource = FishDefinition->TrueBiteWindowSeconds != 0.0 ? TEXT("Asset")
				: Override && Override->TrueBiteWindowSeconds != 0.0 ? TEXT("FishOverride") : TEXT("RarityDefault");
		}
		return true;
	}
	const double Fallback = GetDefault<UCatFishingSettings>()->TrueBiteWindowSeconds;
	if (!FMath::IsFinite(Fallback) || Fallback <= 0.0) return false;
	OutSeconds = Fallback;
	if (OutSource) *OutSource = TEXT("LegacyFallback");
	return true;
}

double ACatFishingSession::GetFisherGrowthMagnitude(const ECatGrowthOptionId OptionId) const
{
	const bool bCastOwned = OptionId == ECatGrowthOptionId::BiteInterval || OptionId == ECatGrowthOptionId::CatchWeight;
	const AActor* OwnerActor = bCastOwned
		? (CastEquipment.IsValid() ? CastEquipment->GetOwner() : nullptr) : FisherCharacter.Get();
	const UCatGrowthComponent* Growth = OwnerActor ? OwnerActor->FindComponentByClass<UCatGrowthComponent>() : nullptr;
	return Growth ? Growth->GetTotalMagnitude(OptionId) : 0.0;
}

// 试探期进入流程（钓鱼规则 §3.4:141 演出时序）：
// ①咬钩计时到点冻结上下文并抽鱼，只保存鱼种、重量与体型数据，不生成实体；
// ②给这一竿的钓手揭开图鉴剪影层，永不撤销；
// ③浮漂保持轻点（EnterPhase 里 Probe 对应 BiteWarning 表现），依次取逐鱼值、档位默认或旧区间兜底秒数；
// ④停留到点才由 HandleProbeStayTimer 把浮漂转猛沉、打开真咬响应窗。
// 实体鱼由有效提钩事务创建；水下黑影预告应独立表现，不能借实体鱼提前出场。
// 饵的数量不在这里扣：设计把「种类权重在抽鱼时消费、数量在真咬成立时扣」分得很清（钓鱼规则 §2.3:74），
// 试探期提竿必空竿且不损饵（§3.3:133、§3.4:141），所以扣饵挪到了 OpenTrueBiteWindowFromAuthority。
bool ACatFishingSession::BeginProbeFromStateTree()
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || IsTerminal() || Snapshot.Phase != ECatFishingPhase::Probe || !World
		|| !Snapshot.HookActor || SelectionResolution != ECatFishSelectionResolution::None
		|| Snapshot.FishEncounterActor || FishDefinition)
	{
		return false;
	}

	// 白天排队的回调若到夜晚才消费，按现行试探期空竿收口；不扣饵、不回到另一条旧等待路径。
	const ACatfishingGameModeBase* Mode = World->GetAuthGameMode<ACatfishingGameModeBase>();
	if (!Mode || !Mode->CanGenerateNewFishingBites())
	{
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::EmptyHook,
			TEXT("Nightfall recalled the probing rod before true bite"));
		return false;
	}

	// 抽鱼要用的鱼情在这一刻冻结：选鱼按它过滤候选，图鉴的「首次遇上的条件」也回显这同一份
	// （图鉴 §3.1.5:132 首次条件回显；此前只写了 RegionId，时段与天气两轴一直是空的）。
	const ACatfishingGameState* GameState = World->GetGameState<ACatfishingGameState>();
	BiteTimeOfDay = GameState ? GameState->GetRunPublicState().Environment.TimeOfDay : ECatEnvironmentTimeOfDay::Unknown;
	BiteWeather = GameState ? GameState->GetRunPublicState().Environment.Weather : ECatEnvironmentWeather::Unknown;

	const FCatFishSelectionCommitResult Selection = ResolveHookSelectionFromAuthority();
	if (Selection.Resolution != ECatFishSelectionResolution::Selected)
	{
		// NoEligibleFish 已在选择事务里写成空军终局，依赖失败也已收敛为 Invalidated；
		// 这里只把失败交回资产的失败边，不重复写终态。
		return false;
	}

	double ProbeSeconds = 0.0;
	const TCHAR* ProbeSource = TEXT("Unavailable");
	if (!TryResolveProbeDurationSeconds(ProbeSeconds, &ProbeSource))
	{
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fishing_bite_timing_rejected SessionId=%s Fish=%s RarityTierId=%s Window=Probe Result=InvalidDuration World=%s NetMode=%d Authority=1 LocalRole=%d"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *FishDefinition->FishDefinitionId.ToString(), *FishDefinition->RarityTierId.ToString(),
			*GetNameSafe(World), int32(GetNetMode()), int32(GetLocalRole()));
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Invalidated,
			TEXT("Probe duration invalid or unavailable"));
		return false;
	}

	// 图鉴剪影层在咬钩成立这一刻揭开、永不撤销（钓鱼规则 §5.6:285、图鉴 §3.1.3:98）。
	// 放在这里而不是各个失败出口，正是因为「试探期空竿同揭」「真咬超时同揭」「断竿放弃也不回滚」
	// 说的都是同一件事：你碰到过这条鱼。揭给这一竿的钓手，围观看见了不算。
	if (UCatRunImprintService* Imprint = World->GetSubsystem<UCatRunImprintService>())
	{
		Imprint->RecordFishEncounterSilhouette(Snapshot.FishDefinitionId, CatchFisherStableNetId,
			CurrentBiteEncounterId);
	}
	if (IsTerminal() || Snapshot.Phase != ECatFishingPhase::Probe || !IsValid(FishDefinition)) return false;

	GetWorldTimerManager().ClearTimer(ProbeStayTimerHandle);
	UE_CLOG(FCString::Strcmp(ProbeSource, TEXT("LegacyFallback")) == 0, LogCatFishing, Warning,
		TEXT("Event=fish_probe_duration_unconfigured SessionId=%s Fish=%s RarityTierId=%s FallbackSeconds=%.3f Reason=MissingAssetAndRarityDefault World=%s NetMode=%d Authority=1 LocalRole=%d"),
		*Snapshot.FishingSessionId.ToString(), *FishDefinition->FishDefinitionId.ToString(), *FishDefinition->RarityTierId.ToString(),
		ProbeSeconds, *GetNameSafe(World), int32(GetNetMode()), int32(GetLocalRole()));
	GetWorldTimerManager().SetTimer(ProbeStayTimerHandle, this, &ThisClass::HandleProbeStayTimer, ProbeSeconds, false);
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_probe_started SessionId=%s Opportunity=%u Fish=%s WeightKg=%.3f VisualScale=%.3f ")
		TEXT("ProbeSeconds=%.3f TimeOfDay=%s Weather=%s RarityTierId=%s ProbeSource=%s World=%s NetMode=%d Authority=1 LocalRole=%d"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), BiteOpportunitySequence,
		*Snapshot.FishDefinitionId.ToString(), FishWeightKilograms, FishVisualScale, ProbeSeconds,
		*UEnum::GetValueAsString(BiteTimeOfDay), *UEnum::GetValueAsString(BiteWeather),
		*FishDefinition->RarityTierId.ToString(), ProbeSource, *GetNameSafe(World), int32(GetNetMode()), int32(GetLocalRole()));
	// 先建立完整计时事实，再发布。观察者可以同步取消或入夜，退出路径会清除计时器；此后不再重建。
	PublishSnapshot(ECatFishingSnapshotMutation::Discrete);
	return !IsTerminal();
}

// 试探期停留到点：浮漂由轻点转猛沉，进入真咬响应窗。阶段已经变化（提前空竿、取消、入夜）时直接忽略。
void ACatFishingSession::HandleProbeStayTimer()
{
	if (!HasAuthority() || IsTerminal() || Snapshot.Phase != ECatFishingPhase::Probe)
	{
		return;
	}
	// 入夜与本计时器同刻到点时的兜底：与入夜同刻的浮漂不再进真咬（钓鱼规则 §7 入夜行:337）。
	const ACatfishingGameModeBase* Mode = GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>();
	if (!Mode || !Mode->CanGenerateNewFishingBites())
	{
		RefreshBiteAvailabilityFromAuthority();
		return;
	}
	if (!OpenTrueBiteWindowFromAuthority() && !IsTerminal())
	{
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Invalidated,
			TEXT("True bite window open failed"));
	}
}

bool ACatFishingSession::OpenTrueBiteWindowFromAuthority()
{
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	UWorld* World = GetWorld();
	if (!HasAuthority() || IsTerminal() || bOpeningTrueBiteWindow || Snapshot.Phase != ECatFishingPhase::Probe || !Settings || !World
		|| !Snapshot.HookActor || SelectionResolution != ECatFishSelectionResolution::Selected
		|| !FishDefinition)
	{
		return false;
	}

	double ResponseSeconds = 0.0;
	const TCHAR* ResponseSource = TEXT("Unavailable");
	if (!TryResolveTrueBiteWindowSeconds(ResponseSeconds, &ResponseSource))
	{
		UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_bite_timing_rejected SessionId=%s Fish=%s Window=TrueBite Reason=InvalidDuration World=%s NetMode=%d Authority=1 LocalRole=%d"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *Snapshot.FishDefinitionId.ToString(), *GetNameSafe(World), int32(GetNetMode()), int32(GetLocalRole()));
		return false;
	}
	TGuardValue<bool> OpeningGuard(bOpeningTrueBiteWindow, true);
	// 防止白天计时器已排队、StateTree 到夜晚才消费的竞态；复用 Probe -> Waiting 事件边。
	const ACatfishingGameModeBase* Mode = World->GetAuthGameMode<ACatfishingGameModeBase>();
	if (!Mode || !Mode->CanGenerateNewFishingBites())
	{
		if (!StateTreeComponent) return false;
		StateTreeComponent->SendStateTreeEvent(CatFishingGameplayTags::WindowExpired, FConstStructView(), TEXT("CatFishing"));
		return true;
	}

	// 真咬成立时才消费当前饵；预警、试探和抛竿准入均不扣数量。
	// 先采样该时点，库存通知中的移动不能改变 D0。
	const FVector BiteOrigin = FisherCharacter.IsValid() ? FisherCharacter->GetActorLocation()
		: Snapshot.RodActor ? Snapshot.RodActor->GetGripWorldTransform().GetLocation()
		: AttemptSnapshot.ServerCorrectedLandingWorldPoint;
	TrueBiteDistanceCentimeters = FVector::Distance(BiteOrigin, AttemptSnapshot.ServerCorrectedLandingWorldPoint);
	UCatEquipmentComponent* Equipment = CastEquipment.Get();
	const FCatFishingUseOperationResult BaitCommit = Equipment
		? Equipment->CommitFishingBaitDeferred(Snapshot.FishingSessionId) : FCatFishingUseOperationResult{};
	if (IsTerminal()) return false;
	// 关闭记录后的通知可把协调器迁移到托管，后续只能重读当前绑定。
	Equipment = CastEquipment.Get();
	if (!Equipment || !Equipment->IsFishingBaitCommitted(Snapshot.FishingSessionId)
		|| (!BaitCommit.bApplied && BaitCommit.Error != ECatDomainCommandError::AlreadyResolved))
	{
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Invalidated, TEXT("Bite bait commit failed"));
		return false;
	}
	// 扣饵通知可能同步结束/迁移会话；不得用已经失效的资源继续打开窗口。
	if (IsTerminal() || Snapshot.Phase != ECatFishingPhase::Probe || !IsValid(Snapshot.HookActor)) return false;
	const UCatEquipmentDefinition* BiteRod = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentDefinition>(AttemptSnapshot.RodDefinitionId);
	const UCatEquipmentFragment_Rod* RodFragment = BiteRod ? BiteRod->FindFragment<UCatEquipmentFragment_Rod>() : nullptr;
	if (!RodFragment || !FMath::IsFinite(TrueBiteDistanceCentimeters) || TrueBiteDistanceCentimeters < 0.0
		|| !FMath::IsFinite(RodFragment->MaximumLineLengthCentimeters) || RodFragment->MaximumLineLengthCentimeters <= 0.0)
	{
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Invalidated, TEXT("True bite distance unavailable"));
		return false;
	}
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_true_bite_distance SessionId=%s D0Cm=%.3f LmaxCm=%.3f FishPoint=FrozenLanding World=%s NetMode=%d Authority=1 LocalRole=%d Fisher=%s"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), TrueBiteDistanceCentimeters, RodFragment->MaximumLineLengthCentimeters,
		*GetNameSafe(World), int32(GetNetMode()), int32(GetLocalRole()), *GetNameSafe(FisherCharacter.Get()));
	if (TrueBiteDistanceCentimeters > RodFragment->MaximumLineLengthCentimeters)
	{
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Escaped, TEXT("True bite D0 exceeds Lmax"));
		return false;
	}
	// WindowEnds 必须在 EnterPhase 发布快照前写好，客户端第一次看到 TrueBiteWindow 时截止时间就是完整的。
	const double PreviousWindowEnd = Snapshot.WindowEndsServerTime;
	Snapshot.WindowEndsServerTime = World->GetTimeSeconds() + ResponseSeconds;
	Snapshot.PerfectWindowEndsServerTime = World->GetTimeSeconds() + FMath::Min(ResponseSeconds,
		1.0 + GetFisherGrowthMagnitude(ECatGrowthOptionId::PerfectWindow));
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_windows_resolved SessionId=%s Fish=%s ResponseSeconds=%.3f PerfectSeconds=%.3f ResponseSource=%s World=%s NetMode=%d Authority=1 LocalRole=%d"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *Snapshot.FishDefinitionId.ToString(), ResponseSeconds,
		Snapshot.PerfectWindowEndsServerTime - World->GetTimeSeconds(), ResponseSource, *GetNameSafe(World), int32(GetNetMode()), int32(GetLocalRole()));
	UE_CLOG(FCString::Strcmp(ResponseSource, TEXT("LegacyFallback")) == 0, LogCatFishing, Warning,
		TEXT("Event=fish_response_window_unconfigured SessionId=%s Fish=%s FallbackSeconds=%.3f World=%s NetMode=%d Authority=1 LocalRole=%d"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *Snapshot.FishDefinitionId.ToString(), ResponseSeconds, *GetNameSafe(World), int32(GetNetMode()), int32(GetLocalRole()));
	if (!EnterPhaseFromStateTree(ECatFishingPhase::TrueBiteWindow).bApplied)
	{
		Snapshot.WindowEndsServerTime = PreviousWindowEnd;
		Snapshot.PerfectWindowEndsServerTime = 0.0;
		return false;
	}
	if (IsTerminal() || Snapshot.Phase != ECatFishingPhase::TrueBiteWindow) return false;
	bTrueBiteWindowAcceptingHook = true;
	GetWorldTimerManager().ClearTimer(TrueBiteTimerHandle);
	GetWorldTimerManager().SetTimer(TrueBiteTimerHandle, this, &ThisClass::HandleTrueBiteWindowExpired,
		ResponseSeconds, false);
	return true;
}

// 咬钩计时到点冻结选择上下文及鱼种、重量等数据，幂等返回缓存结果。
// 抽鱼不生成实体；RequestHookFromAuthority 接受真咬窗口内的提钩后才创建 Encounter。
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
	// 选鱼发生在试探期开始那一刻（咬钩计时到点），不再等玩家左键；阶段门随之从 TrueBiteWindow 改为 Probe。
	if (!HasAuthority() || IsTerminal() || Snapshot.Phase != ECatFishingPhase::Probe
		|| !AttemptSnapshot.WaterRegion.IsValid() || !Snapshot.HookActor || !Snapshot.RodActor)
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
	UCatEquipmentComponent* Equipment = CastEquipment.Get(); // 会话协调器可能已托管；鱼饵来源仍由其记录指向原抛竿者。
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
	FrozenSelectionContext.ChumSample = Chum->SampleChumAtPoint(AttemptSnapshot.ServerCorrectedLandingWorldPoint,
		AttemptSnapshot.WaterRegion, World->GetTimeSeconds());
	FrozenSelectionContext.TimeOfDay = BiteTimeOfDay;
	FrozenSelectionContext.Weather = BiteWeather;
	FrozenSelectionContext.BaitDefinitionId = Equipment->GetCurrentFishingBaitDefinitionId(Snapshot.FishingSessionId);
	FrozenSelectionContext.ActivePlayerCount = PlayerCount;
	FrozenSelectionContext.CombinedFishingStrength = FishingStrength;
	FrozenSelectionContext.CombinedFightStamina = FightStamina;
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	const UCatFishingFightBalanceDefinition* FightBalance = Settings
		? Settings->LoadFightBalanceDefinition() : nullptr;
	FrozenSelectionContext.StrengthPerKilogram = FightBalance
		? FightBalance->StrengthPerKilogram : 0.0;
	FrozenSelectionContext.CatchWeightBonus = GetFisherGrowthMagnitude(ECatGrowthOptionId::CatchWeight);
	FrozenSelectionContext.RandomSeed = static_cast<int32>(CurrentBiteRandomSeed);
	const UCatFishCatalogSettings* Catalog = GetDefault<UCatFishCatalogSettings>();
	// 按冻结上下文从鱼类图鉴中选出本次的鱼种（含权重/稀有度/条件判定，具体算法在 Catalog 内部）。
	FrozenSelectionResult = Catalog->SelectRuntimeDefinition(FrozenSelectionContext);
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_fish_selection_resolved SessionId=%s Selected=%s FishId=%s FightBalanceId=%s WeightKg=%.3f BaseFishStrength=%.3f CatConversionPerKg=%.3f EligibleCandidates=%d PositiveWeightCandidates=%d NormalizedProbability=%.6f ChumClass=%d ClassProbability=%.6f TimeFilter=%s WeatherFilter=%s TimeOfDay=%s Weather=%s ActivePlayers=%d ChumFields=%d FromBasePool=%d RandomSeed=%d Region=%s World=%s NetMode=%d Authority=1 LocalRole=%d"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphensLower),
		FrozenSelectionResult.bSelected ? TEXT("true") : TEXT("false"),
		*FrozenSelectionResult.FishDefinitionId.ToString(),
		FightBalance ? *FightBalance->BalanceDefinitionId.ToString() : TEXT("None"),
		FrozenSelectionResult.WeightKilograms, FrozenSelectionResult.BaseFishStrength,
		FrozenSelectionContext.StrengthPerKilogram, FrozenSelectionResult.EligibleCandidateCount,
		FrozenSelectionResult.PositiveWeightCandidateCount,
		FrozenSelectionResult.SelectedNormalizedProbability,
		FrozenSelectionResult.SelectedChumClass, FrozenSelectionResult.SelectedChumClassProbability,
		Catalog->bEnableTimeOfDayEligibilityFilter ? TEXT("Enabled") : TEXT("Bypassed"),
		Catalog->bEnableWeatherEligibilityFilter ? TEXT("Enabled") : TEXT("Bypassed"),
		*UEnum::GetValueAsString(FrozenSelectionContext.TimeOfDay),
		*UEnum::GetValueAsString(FrozenSelectionContext.Weather), FrozenSelectionContext.ActivePlayerCount,
		FrozenSelectionContext.ChumSample.ContributingFieldCount, FrozenSelectionResult.bFromBasePool,
		FrozenSelectionContext.RandomSeed, *FrozenSelectionContext.WaterRegion.RegionId.ToString(), *GetNameSafe(World),
		int32(GetNetMode()), int32(GetLocalRole()));
	UCatFishDefinition* SelectedDefinition = FrozenSelectionResult.bSelected
		? Catalog->FindRuntimeDefinition(FrozenSelectionResult.FishDefinitionId) : nullptr;
	// T10：鱼种时间字段/明确兜底接替 Bite 模板，不再以模板存在性阻断生产。
	const UCatFightPersonalityDefinition* Fight = SelectedDefinition && Settings
		? Settings->FindFightPersonality(SelectedDefinition->FightPersonalityId) : nullptr;
	if (!SelectedDefinition || !Fight)
	{
		// 没选出鱼、或性格模板缺失：判为"当前条件下没有合格鱼"，走空军终局而不是异常终止。
		SelectionResolution = ECatFishSelectionResolution::NoEligibleFish;
		Equipment->ReleaseFishingUse(Snapshot.FishingSessionId);
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::EmptyHook, TEXT("No eligible fish"));
		Result.Resolution = SelectionResolution;
		Result.Error = ECatDomainCommandError::None;
		return Result;
	}
	const UCatFishPresentationDefinition* FishPresentation = SelectedDefinition->LoadRuntimePresentationDefinition();
	const double SelectedVisualScale = FishPresentation
		? FishPresentation->ComputeUniformVisualScale(FrozenSelectionResult.WeightKilograms) : 1.0;
	FishDefinition = SelectedDefinition;
	FishWeightKilograms = FrozenSelectionResult.WeightKilograms;
	FishVisualScale = SelectedVisualScale;
	Snapshot.FishDefinitionId = SelectedDefinition->FishDefinitionId;
	Snapshot.FishWeightKilograms = FrozenSelectionResult.WeightKilograms;
	Snapshot.FishStrength = FrozenSelectionResult.BaseFishStrength;
	Snapshot.bGiant = SelectedDefinition->BodyClass == ECatFishBodyClass::Giant;
	// 钓鱼规则 §4.1（:160）：鱼体力初始值 ＝ 鱼表「体力系数」× 实际重量，不再是每鱼种一份定额。
	// 重量刚在本事务里冻结，系数读鱼定义上的同一列（09-08 v1.5 把该列由「体力」改写为「体力系数」）。
	FishFightStaminaInitial = FMath::Max(0.0,
		SelectedDefinition->ResolveInitialFightStamina(FrozenSelectionResult.WeightKilograms));
	Snapshot.FishFightStaminaRemaining = FishFightStaminaInitial;
	Snapshot.NormalizedFishStamina = FishFightStaminaInitial > 0.0 ? 1.0 : 0.0;
	SelectionResolution = ECatFishSelectionResolution::Selected;
	Result.Resolution = SelectionResolution;
	Result.FishDefinitionId = SelectedDefinition->FishDefinitionId;
	Result.Error = ECatDomainCommandError::None;
	return Result;
}

bool ACatFishingSession::SpawnHookedFishFromAuthority(const FGuid RequestId)
{
	UWorld* World = GetWorld();
	const auto Reject = [&](const TCHAR* Reason)
	{
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fishing_hook_fish_spawn_rejected RequestId=%s SessionId=%s CastAttemptId=%s Fish=%s Reason=%s World=%s NetMode=%d Authority=%d LocalRole=%d %s"),
			*RequestId.ToString(), *Snapshot.FishingSessionId.ToString(), *Snapshot.CastAttemptId.ToString(),
			*Snapshot.FishDefinitionId.ToString(), Reason, *GetNameSafe(World), int32(GetNetMode()), HasAuthority(), int32(GetLocalRole()),
			*CatLogContext::BuildControllerFields(FisherCharacter.IsValid() ? FisherCharacter->GetController() : nullptr));
		return false;
	};
	if (!RequestId.IsValid() || !HasAuthority() || IsTerminal() || !World
		|| Snapshot.Phase != ECatFishingPhase::TrueBiteWindow || bTrueBiteWindowAcceptingHook
		|| SelectionResolution != ECatFishSelectionResolution::Selected || !IsValid(FishDefinition)
		|| !FisherCharacter.IsValid() || !IsValid(Snapshot.HookActor) || Snapshot.FishEncounterActor)
	{
		return Reject(TEXT("InvalidHookTransaction"));
	}
	const UCatFishingPresentationSettings* Presentation = GetDefault<UCatFishingPresentationSettings>();
	UClass* FishClass = Presentation ? Presentation->FishEncounterActorClass.LoadSynchronous() : nullptr;
	const FVector FishLocation = AttemptSnapshot.ServerCorrectedLandingWorldPoint;
	ACatFishEncounterActor* Encounter = FishClass && FishClass->IsChildOf(ACatFishEncounterActor::StaticClass())
		? World->SpawnActorDeferred<ACatFishEncounterActor>(FishClass, FTransform(FishLocation), this,
			FisherCharacter.Get(), ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
	if (!Encounter) return Reject(TEXT("FishPresentationUnavailable"));
	// 身份先于构造写入；首次表现事件等搏斗初始化后再放行。
	Encounter->DeferInitialPresentationFromAuthority();
	if (!Encounter->InitializeAuthoritativeIdentity(Snapshot.FishingSessionId, Snapshot.CastAttemptId,
		Snapshot.FishDefinitionId, 0.0, FishVisualScale))
	{
		Encounter->Destroy();
		return Reject(TEXT("FishIdentityFailed"));
	}
	Encounter->FinishSpawning(FTransform(FishLocation));
	if (!IsValid(Encounter)) return Reject(TEXT("FishDestroyedDuringConstruction"));
	const FCatFishEncounterPresentationState& State = Encounter->GetPresentationState();
	if (IsTerminal() || Snapshot.Phase != ECatFishingPhase::TrueBiteWindow
		|| State.FishingSessionId != Snapshot.FishingSessionId || State.CastAttemptId != Snapshot.CastAttemptId
		|| State.FishDefinitionId != Snapshot.FishDefinitionId || !FMath::IsNearlyEqual(State.VisualScale, FishVisualScale)
		|| !Encounter->GetActorLocation().Equals(FishLocation, 1.0))
	{
		Encounter->Destroy();
		return Reject(TEXT("FishConstructionChangedAuthorityState"));
	}
	Snapshot.FishEncounterActor = Encounter;
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_hook_fish_spawned RequestId=%s SessionId=%s CastAttemptId=%s Fish=%s FishActor=%s WeightKg=%.3f VisualScale=%.3f World=%s NetMode=%d Authority=1 LocalRole=%d %s"),
		*RequestId.ToString(), *Snapshot.FishingSessionId.ToString(), *Snapshot.CastAttemptId.ToString(),
		*Snapshot.FishDefinitionId.ToString(), *GetNameSafe(Encounter), FishWeightKilograms, FishVisualScale,
		*GetNameSafe(World), int32(GetNetMode()), int32(GetLocalRole()),
		*CatLogContext::BuildControllerFields(FisherCharacter->GetController()));
	return true;
}

void ACatFishingSession::HandleTrueBiteWindowExpired()
{
	if (HasAuthority() && !IsTerminal() && Snapshot.Phase == ECatFishingPhase::TrueBiteWindow)
	{
		bTrueBiteWindowAcceptingHook = false;
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::HookWindowExpired, TEXT("True bite response timed out"));
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
	UCatEquipmentComponent* Equipment = CastEquipment.Get(); // 钓鱼使用记录及扣饵来源始终绑定原始抛竿者，物理抓握不改变结算对象。
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

	// 已配置的逐鱼节拍和游速覆盖测试模板；未明确的横切等参数仍由同一模板提供。
	FCatFishResolvedBehavior ResolvedBehavior;
	if (!Settings->TryResolveFishBehavior(*FishDefinition, ResolvedBehavior)
		|| !ResolvedBehavior.SteeringConfig.IsValid())
	{
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fishing_fight_start_rejected SessionId=%s Reason=FishBehaviorUnresolved Fish=%s %s"),
			*Snapshot.FishingSessionId.ToString(), *FishDefinition->FishDefinitionId.ToString(),
			*CatLogContext::BuildControllerFields(FisherCharacter->GetController()));
		return false;
	}

	// 完美中鱼（钓鱼规则 §3.4:145,149）：鱼力量、鱼体力、初始线长三项在入场直接乘到本场实际值，
	// 此后运动求解、负载、消耗全用削后值。倍率按鱼册稀有度档取
	// （09-09 晚裁「四套 Bite_* 性格模板只是测试用，正式口径走鱼册」），不再读那三个已弃用的过渡字段。
	const FCatPerfectHookReduction PerfectReduction = Snapshot.bPerfectHook
		? GetDefault<UCatFishCatalogSettings>()->ResolvePerfectHookReduction(*FishDefinition)
		: FCatPerfectHookReduction{};
	const bool bPerfect = Snapshot.bPerfectHook;
	const double FishStrengthScale = PerfectReduction.FishStrengthMultiplier;
	const double FishStaminaScale = PerfectReduction.FishStaminaMultiplier;
	const double LineLengthScale = PerfectReduction.InitialLineLengthMultiplier;
	// 身体上限由 ASC 播种并复制；Fishing 只消费该运行时属性，不再从角色配置建立另一份上限。
	const double CatStaminaMaximumFromAttributes = AbilitySystem->GetTotalFightStaminaCapacity();
	if (!FMath::IsFinite(CatStaminaMaximumFromAttributes) || CatStaminaMaximumFromAttributes <= 0.0)
	{
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fishing_fight_start_rejected SessionId=%s Reason=StaminaMaximumAttributeInvalid MaxFightStamina=%.3f %s"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), CatStaminaMaximumFromAttributes,
			*CatLogContext::BuildControllerFields(FisherCharacter->GetController()));
		return false;
	}
	// 09-11 裁决④：这里曾经调用 InitializeFishingStaminaForSession 把主控体力补满，
	// 该行为已删除、那个入口也已全仓移除（grep 不到是预期的，不是漏改）。
	// 开打就按 ASC 当前余额算，体力是跨竿资源；上限已在上面做过 fail-closed 校验。

	// 双方力量、实际鱼重与猫的等效系统质量在此冻结；Runner 每步只刷新参与者输入、力量和接入约束的猫数。
	// 鱼力量包含完美中鱼折减；这里先保存主位基础力量供初始化校验，
	// Runner启动后只读取主控ASC与实际身体样本；旁人助力通过物理竿端点进入求解。
	// 将服务器设置、鱼竿/鱼定义和逐鱼解析结果冻结进模拟配置，未裁定参数继续来自已绑定模板。
	FCatFightSimulationConfig Config;
	Config.FixedStepSeconds = Settings->FixedFightStepSeconds; // 固定步长模拟，保证服务器权威结果确定可复现。
	Config.PrimaryOperatorCatStrength = AbilitySystem->GetNumericAttribute(
		UCatSurvivalAttributeSet::GetFishingStrengthAttribute());
	// Runner 每个固定步刷新主控力量；抓猫队友只通过真实身体位移和竿端点参与。

	// 猫系统质量独立于力量成长；CharacterMovement 的推挤 Mass 不作为搏斗质量来源。
	const UCatPhysicalBodyComponent* PhysicalBody = FisherCharacter->GetPhysicalBodyComponent();
	if (!PhysicalBody || !PhysicalBody->GetBody()) return false;
	Config.PrimaryOperatorMassKilograms = PhysicalBody->GetBody()->GetMass();

	Config.FishMassKilograms = FishWeightKilograms;
	Config.FishBody.Geometry = FishDefinition->FightBodyGeometry.Scaled(Encounter->GetPresentationState().VisualScale);
	Config.FishBody.MaximumSwimTurnRateDegreesPerSecond = ResolvedBehavior.SteeringConfig.MaximumTurnRateDegreesPerSecond;
	Config.FishBody.MaximumBodyTurnRateDegreesPerSecond = FMath::Max(240.0, Config.FishBody.MaximumSwimTurnRateDegreesPerSecond);
	if (!Config.FishBody.Geometry.HasMouthLever())
	{
		UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_fight_start_rejected SessionId=%s FishDefinitionId=%s Reason=FishBodyGeometryMissing %s"),
			*Snapshot.FishingSessionId.ToString(), *FishDefinition->FishDefinitionId.ToString(),
			*CatLogContext::BuildControllerFields(FisherCharacter->GetController()));
		return false;
	}
	Config.FishStrength = FrozenSelectionResult.BaseFishStrength * FishStrengthScale;
	Config.StrengthPerKilogram = FightBalance->StrengthPerKilogram;
	Config.ForcePerStrengthNewtons = FightBalance->ForcePerStrengthNewtons;

	Config.ExhaustedReelForceNewtons = FightBalance->ExhaustedReelForceNewtons;
	Config.ExhaustedCatTowAccelerationCentimetersPerSecondSquared = FightBalance->ExhaustedCatTowAccelerationCentimetersPerSecondSquared;
	Snapshot.FishStrength = Config.FishStrength; // 本场鱼力此刻成为定值（已乘完美削减）。
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
	Config.FishFullEffortSpeedCentimetersPerSecond = ResolvedBehavior.FullEffortSpeedCentimetersPerSecond;
	Config.ExhaustedCatEscapeSpeedMultiplier = FightBalance->ExhaustedCatEscapeSpeedMultiplier;
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

	// 记录本场参与者贡献窗口；竿强和配置校验通过后统一启动物理搏斗。
	FightStartedServerTimeSeconds = GetWorld()->GetTimeSeconds();
	// 本场鱼力已含完美削减，鱼竿耐久与模拟配置也已完成校验。
	if (EvaluateRodStrengthFromAuthority(TEXT("FightStart")))
	{
		return false;
	}

	// 组装搏斗模拟的初始状态：猫当前体力为 ASC 绿＋黄，容量为绿上限＋黄，鱼体力/初始线长按完美中鱼折减系数缩放。
	FCatFightSimulationState InitialState;
	InitialState.CatStamina = AbilitySystem->GetTotalFightStamina();
	InitialState.FishStamina = Snapshot.FishFightStaminaRemaining * FishStaminaScale;
	const FVector RodTipWorldPosition = Rod->GetRodTipWorldTransform().GetLocation();
	const double RequestedInitialLineLength = TrueBiteDistanceCentimeters * LineLengthScale;
	const double MinimumPhysicalLineLength = FMath::Abs(
		Encounter->GetMouthWorldLocation().Z - RodTipWorldPosition.Z);
	// 完美提竿会缩短初始线长，但“账面线长”绝不能直接变得比鱼嘴到竿尖的真实距离还短。
	// 先把请求值限制在竿尖到当前水面的最短物理长度内，下面再用同一长度真正投影鱼的位置。
	if (!FMath::IsFinite(RequestedInitialLineLength) || TrueBiteDistanceCentimeters < 0.0
		|| TrueBiteDistanceCentimeters > Config.MaximumLineLengthCentimeters
		|| RequestedInitialLineLength > Config.MaximumLineLengthCentimeters
		|| MinimumPhysicalLineLength > Config.MaximumLineLengthCentimeters)
	{
		// 入场没有补过体力，也就没有体力要回滚（09-11 裁决④）。
		return false;
	}
	InitialState.LineLengthCentimeters = FMath::Max(RequestedInitialLineLength, MinimumPhysicalLineLength);
	InitialState.FishBody.Heading = (Encounter->GetMouthWorldLocation() - RodTipWorldPosition).GetSafeNormal2D(UE_DOUBLE_SMALL_NUMBER, FVector::ForwardVector);
	InitialState.FishWorldPosition = Encounter->GetActorLocation();
	InitialState.MotionIntent = ECatFishMotionIntent::StrugglingOutward; // 刚上钩默认视为鱼在向外挣扎。
	InitialState.CatAction = ECatFightCatAction::None;
	Snapshot.FishFightStaminaRemaining = InitialState.FishStamina; // 把折减后的体力写回公开快照。
	// 完美削减之后才是本场真正的鱼体力初值；归一化分母跟着一起改，和 Runner 的 InitialFishStamina 同源，
	// 否则完美提竿会让体力条一上来就不满。
	FishFightStaminaInitial = InitialState.FishStamina;
	Snapshot.RodDurabilityRemaining = Config.RodDurability;
	// 只为初始上钩点建立投影范围；搏斗拖行不再受初始落点包围盒限制，运行时由线长与真实表面负责。
	const FVector Landing = AttemptSnapshot.ServerCorrectedLandingWorldPoint;
	const FVector HalfExtent(Config.MaximumLineLengthCentimeters, Config.MaximumLineLengthCentimeters,
		FMath::Max(500.0, Config.MaximumLineLengthCentimeters * 0.25));
	const FBox FrozenBounds = FBox::BuildAABB(Landing, HalfExtent);
	// 用运动求解器把鱼的初始位置投影到合法范围内（尊重最大线长、水域边界），得到一个几何上自洽的起始点。
	FCatFishMotionSolveInput ProjectionInput;
	const FVector InitialMouthOffset = FCatFishBodyModel::RotateLocal(Config.FishBody.Geometry.MouthLocalPositionCentimeters, InitialState.FishBody.Heading);
	ProjectionInput.RodTipWorldPosition = RodTipWorldPosition - InitialMouthOffset;
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
		? FVector::Distance(RodTipWorldPosition, Exact.WaterSurfaceWorldPoint + InitialMouthOffset)
		: TNumericLimits<double>::Max();
	const double ReconciledInitialLineLength = FMath::Min(Config.MaximumLineLengthCentimeters,
		FMath::Max(InitialState.LineLengthCentimeters, ResolvedInitialDistance));
	if (!Projected.bSucceeded || !Exact.bSucceeded || !FMath::IsFinite(ResolvedInitialDistance)
		|| ResolvedInitialDistance > Config.MaximumLineLengthCentimeters + 0.01
		|| !Encounter->ApplyFightStepFromAuthority(ECatFishMotionIntent::StrugglingOutward,
			ReconciledInitialLineLength,
			Exact.WaterSurfaceWorldPoint, 0.0f, 0.0f, 0.0f,
			static_cast<float>(Config.FishFullEffortSpeedCentimetersPerSecond), false, false, FVector::UpVector,
			InitialState.FishBody.Heading,
			ECatFishBehavior::OutwardRush, static_cast<float>(InitialState.FishEffortRatio)))
	{
		// 求解/吸附/表现应用任一环节失败：保留原余额，不进入搏斗。
		return false;
	}
	// 真实水面校正可能把候选点沿岸轻微挪动；最终以鱼嘴到竿尖的真实距离抬高线长，
	// 保证 Runner 从第一步起始终满足 D <= L_paid，同时尽可能保留完美提竿的缩线收益。
	InitialState.LineLengthCentimeters = ReconciledInitialLineLength;
	InitialState.FishWorldPosition = Encounter->GetActorLocation(); // 用刚落位的实际权威位置覆盖，作为 Runner 的真正起点。

	// 嘴点是同一个静态局部点；附着复制让客户端钩和鱼共享一份移动快照。
	if (Snapshot.HookActor)
	{
		Snapshot.HookActor->SetActorLocation(Encounter->GetMouthWorldLocation());
		if (!Snapshot.HookActor->AttachToActor(Encounter, FAttachmentTransformRules::KeepWorldTransform))
		{
			UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_fight_start_rejected SessionId=%s CastAttemptId=%s HookActor=%s FishActor=%s Reason=FishMouthAttachmentFailed World=%s NetMode=%d Authority=1 LocalRole=%d"),
				*Snapshot.FishingSessionId.ToString(), *AttemptSnapshot.CastAttemptId.ToString(), *GetNameSafe(Snapshot.HookActor), *GetNameSafe(Encounter),
				*GetNameSafe(GetWorld()), int32(GetNetMode()), int32(GetLocalRole()));
			return false;
		}
	}
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_fish_body_bound SessionId=%s FishActor=%s FishDefinitionId=%s MouthLocalCm=%s CenterLocalCm=%s YawRadiusCm=%.4f World=%s NetMode=%d Authority=1 LocalRole=%d"),
		*Snapshot.FishingSessionId.ToString(), *GetNameSafe(Encounter), *FishDefinition->FishDefinitionId.ToString(),
		*Config.FishBody.Geometry.MouthLocalPositionCentimeters.ToCompactString(), *Config.FishBody.Geometry.CenterOfMassLocalPositionCentimeters.ToCompactString(),
		Config.FishBody.Geometry.YawRadiusOfGyrationCentimeters, *GetNameSafe(GetWorld()), int32(GetNetMode()), int32(GetLocalRole()));

	// 组装 FightRunner 的初始化参数：把 Session/Actor 引用、模拟配置/初始状态、逐鱼解析后的节奏参数、
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
	Init.SteeringConfig = ResolvedBehavior.SteeringConfig;
	Init.BehaviorStateTree = FishBehaviorStateTree;
	// 使用玩家实际点击确认的那一轮咬钩机会种子；入夜收回后的下一轮鱼种与搏斗节奏都能变化，同时服务器仍可复现。
	Init.RandomSeed = CurrentBiteRandomSeed != 0
		? CurrentBiteRandomSeed : (AttemptSnapshot.ServerRandomSeed != 0
			? AttemptSnapshot.ServerRandomSeed : static_cast<uint64>(GetTypeHash(Snapshot.FishingSessionId)));
	FightRunner = NewObject<UCatFishingFightRunner>(this);
	if (!FightRunner || !FightRunner->InitializeFromAuthority(Init) || !FightRunner->Start())
	{
		// Runner 创建/初始化/启动任一步失败：清空引用并保留体力，不留下半启动的 Runner。
		FightRunner = nullptr;
		return false;
	}
	Snapshot.bReeling = FightRunner->GetCatAction() == ECatFightCatAction::Pull;
	Snapshot.bSlacking = FightRunner->GetCatAction() == ECatFightCatAction::Slack;
	bFightStaminaInitialized = true;
	StaminaOwner = FisherCharacter;
	if (!EnterPhaseFromStateTree(ECatFishingPhase::HookedFight).bApplied)
	{
		// 阶段写入被拒绝（比如并发终止）：必须把已经启动的 Runner 与体力域归属登记一起回滚，
		// 否则会出现"Runner 在跑但阶段还停在 TrueBiteWindow"的不一致状态。体力数值本身没被入场改写。
		FightRunner->Stop();
		FightRunner = nullptr;
		Snapshot.bReeling = false;
		Snapshot.bSlacking = false;
		StaminaOwner.Reset();
		bFightStaminaInitialized = false;
		return false;
	}
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_physical_fight_started SessionId=%s FishActor=%s FishStrength=%.3f PrimaryStrength=%.3f Result=RunnerStarted %s"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(Encounter),
		Snapshot.FishStrength, Config.PrimaryOperatorCatStrength,
		*CatLogContext::BuildControllerFields(FisherCharacter.IsValid() ? FisherCharacter->GetController() : nullptr));
	// 巨物全体提示（多人钓鱼附篇 §3.3、交互册「求助与震动」）：求助一律手动喊人，**只有巨物**由系统发全场提示。
	// 接线点选在这里，不选抽中鱼种那一刻：试探期只冻结鱼种数据，玩家还没提竿，
	// 竿强不够会在下面的检查序里当场瞬断、这一竿根本不成立——那时候把全队喊过来，来了也没有可合力的对象。
	// 搏斗真的开起来了才喊，喊来的人能做的事（合力拉竿、拽尾救援、抢抄）此刻全部成立。
	if (Snapshot.bGiant)
	{
		if (UCatSocialService* Social = GetWorld() ? GetWorld()->GetSubsystem<UCatSocialService>() : nullptr)
		{
			Social->BroadcastGiantFishingPrompt(FisherCharacter.IsValid() ? FisherCharacter->GetController() : nullptr,
				Snapshot.FishingSessionId);
		}
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
	// 钩在静态嘴点：权威位置由同一步鱼身姿态推导，附着复制保持客户端钩嘴同位。
	if (Snapshot.HookActor && Snapshot.FishEncounterActor)
	{
		Snapshot.HookActor->SetActorLocation(Snapshot.FishEncounterActor->GetMouthWorldLocation());
		if (!Snapshot.HookActor->SetFishingLinePresentationFromAuthority(
			Step.LineLengthCentimeters, Step.StraightLineDistanceCentimeters,
			Step.SlackLineLengthCentimeters, static_cast<float>(Step.NormalizedTension), Step.bLineTaut,
			Step.LineTensionNewtons))
		{
			HandleFightRunnerFailureFromAuthority(TEXT("HookLinePresentation"));
			return;
		}
	}
	// 分母用本场冻结的初值（体力系数 × 实际重量，再乘完美削减），不是鱼种定额：
	// 定额那一套在 §4.1（:160）已经退役，拿它当分母会让重鱼的体力条永远读不到满格。
	Snapshot.NormalizedFishStamina = FishFightStaminaInitial > 0.0
		? FMath::Clamp(Snapshot.FishFightStaminaRemaining / FishFightStaminaInitial, 0.0, 1.0) : 0.0;
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
	else if (Step.bFishBeached && FightRunner->IsFishBeachedForAuthority())
	{
		if (!SpawnExhaustedFishPickupFromAuthority(Snapshot.FishEncounterActor->GetActorLocation()))
			HandleFightRunnerFailureFromAuthority(TEXT("ShoreContactPickupSpawn"));
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
		// 墓碑（2026-09-14，T17；钓鱼规则 §5.3）：删去距竿尖 75cm 的二次门，触岸处立即交付。
		if (!SpawnExhaustedFishPickupFromAuthority(Encounter->GetActorLocation()))
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
	if (!bResolvingWater)
	{
		if (bWaterResolutionPending) return;
		if (UCatFishingResolutionSubsystem* Queue = GetWorld()->GetSubsystem<UCatFishingResolutionSubsystem>())
		{
			bWaterResolutionPending = true;
			Queue->Enqueue(ECatFishingResolution::Water, Character->GetController(), Snapshot.FishingSessionId,
				[WeakThis = TWeakObjectPtr<ThisClass>(this), WeakCharacter = TWeakObjectPtr<ACatCharacter>(Character), ImmersionDepthCentimeters]()
				{
					if (!WeakThis.IsValid() || !WeakCharacter.IsValid()) return;
					WeakThis->bWaterResolutionPending = false;
					if (UCatConditionComponent* Condition = WeakCharacter->GetConditionComponent()) Condition->SetWetFromAuthority(true);
					if (WeakThis->IsTerminal())
					{
						// 同刻鱼已经进嘴，落水只补后果，不撤销实物／捕获登记。
						WeakCharacter->Multicast_PlayCosmeticEvent(ResolveTerminalFisherPresentationTag(ECatFishingOutcome::CatInWater));
						return;
					}
					TGuardValue<bool> Guard(WeakThis->bResolvingWater, true);
					WeakThis->HandleCatEnteredDangerousWaterFromAuthority(ImmersionDepthCentimeters, WeakCharacter.Get());
				});
			return;
		}
	}
	UE_LOG(LogCatFishing, Warning,
		TEXT("Event=fishing_cat_entered_dangerous_water SessionId=%s DepthCm=%.2f Phase=%s Result=TerminalCatInWater World=%s Authority=%d LocalRole=%d %s"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), ImmersionDepthCentimeters,
		*UEnum::GetValueAsString(Snapshot.Phase),
		*GetNameSafe(GetWorld()), HasAuthority(), int32(GetLocalRole()),
		*CatLogContext::BuildControllerFields(Character->GetController()));
	// 钓鱼规则 §4.6（:222,228）：落水／猫体力归零都是本竿的结局——鱼逃、饵已扣（咬钩成立时就扣了）、
	// 湿身演出，嘴里原有的鱼保留（本入口不碰 MouthCarry）。09-09 八问③把它正式写进设计。
	// 落水**不再**转入无人值守放线：那条出口只留给主位主动离竿（§4.6:230 逐字"落水与猫体力归零不走这条出口"）。
	// 先写终局再释放竿位，Service 那边的 FindActiveSessionByRod 会跳过终态会话，
	// SuspendOperatorFromAuthority 因此既不会被调用、被调用也会在终态门禁处直接返回。
	ACatCharacter* PreviousFisher = FisherCharacter.Get();
	TerminateSession(ECatFishingOutcome::CatInWater, TEXT("Primary operator entered dangerous water"));
	// 终局已由 FinalizeSession 给会话登记的钓手播过一次湿身表现；只有落水的是另一具身体时才补播，避免重复。
	if (Character != PreviousFisher)
	{
		const FGameplayTag Presentation = ResolveTerminalFisherPresentationTag(ECatFishingOutcome::CatInWater);
		if (Presentation.IsValid()) Character->Multicast_PlayCosmeticEvent(Presentation);
	}
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
	if (!Equipment->IsFishingBaitCommitted(Snapshot.FishingSessionId)) return false;
	const FCatFishingUseOperationResult Bait = Equipment->CommitFishingBaitDeferred(Snapshot.FishingSessionId);
	if (!Bait.bApplied && Bait.Error != ECatDomainCommandError::AlreadyResolved)
	{
		return false;
	}
	// 钓鱼规则 §4.4（:203）："搏斗以渔获结束时，竿另扣基础磨损 1 点"。
	// 它和 Runner 的逐步磨损不是同一笔账——逐步磨损按每秒线力计价，这一点只在真的拿到鱼时扣一次，
	// 所以此处**必须**再扣，原先"捕获仅收口饵料，不能再重复扣耐久"的口径是错的。
	// 磨损接口按累计绝对值写回，序号必须严格 +1：直接用刚拿到的记录值做基准，不另存第二份账。
	const FCatFishingUseOperationResult Wear = Equipment->ApplyFishingRodWear(Snapshot.FishingSessionId,
		Bait.WearSequence + 1, Bait.AbsoluteRodWear + GetDefault<UCatFishingSettings>()->GetCatchCompletionRodWearPoints()
		* (1.0 + GetFisherGrowthMagnitude(ECatGrowthOptionId::RodWear)));
	if (!Wear.bApplied)
	{
		UE_LOG(LogCatFishing, Error,
			TEXT("Event=fishing_catch_rod_wear_failed SessionId=%s RodItemInstanceId=%s WearSequence=%lld Error=%s"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			*AttemptSnapshot.RodItemInstanceId.ToString(), Bait.WearSequence + 1,
			*UEnum::GetValueAsString(Wear.Error));
		return false;
	}
	RodWearSequence = Wear.WearSequence;
	Snapshot.RodDurabilityRemaining = Wear.RemainingRodDurability;
	// 这一点磨损可能正好把竿扣断。鱼已经到手，结局不改判成断竿，但竿必须真的断在世界里。
	if (Wear.bRodBroken)
	{
		ACatFishingRodActor* Rod = Snapshot.RodActor;
		if (Rod && !Rod->SetBrokenFromAuthority(true, Rod->GetPresentationState().RodActorRevision))
		{
			UE_LOG(LogCatFishing, Error,
				TEXT("Event=fishing_rod_broken_projection_failed SessionId=%s RodItemInstanceId=%s Cause=CatchCompletionWear"),
				*Snapshot.FishingSessionId.ToString(), *AttemptSnapshot.RodItemInstanceId.ToString());
		}
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fishing_rod_broken SessionId=%s RodItemInstanceId=%s Cause=CatchCompletionWear RodDurability=%.3f AbsoluteRodWear=%.3f"),
			*Snapshot.FishingSessionId.ToString(), *AttemptSnapshot.RodItemInstanceId.ToString(),
			Wear.RemainingRodDurability, Wear.AbsoluteRodWear);
	}
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_catch_rod_wear_applied SessionId=%s RodItemInstanceId=%s WearSequence=%lld WearPoints=%.3f RodDurability=%.3f Broken=%s"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
		*AttemptSnapshot.RodItemInstanceId.ToString(), Wear.WearSequence, Wear.AbsoluteRodWear - Bait.AbsoluteRodWear,
		Wear.RemainingRodDurability, Wear.bRodBroken ? TEXT("true") : TEXT("false"));
	return true;
}

bool ACatFishingSession::SpawnExhaustedFishPickupFromAuthority(const FVector& SurfaceLocation)
{
	// 力竭拖岸的专有前置：鱼必须真的被拖过岸线落在干地上，才允许交接成可拾取世界鱼。
	if (!FightRunner || !FightRunner->IsFishBeachedForAuthority())
	{
		UE_LOG(LogCatFishing, Error,
			TEXT("Event=exhausted_fish_pickup_rejected SessionId=%s Beached=%s Reason=NotBeached"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			FightRunner ? TEXT("false") : TEXT("no-runner"));
		return false;
	}
	// 使用可见 Encounter 的当前干地位置与法线完成表现交接，避免在竿尖/水面处重新投影后跳位或消失。
	return SpawnLandedFishPickupFromAuthority(SurfaceLocation,
		Snapshot.FishEncounterActor
			? Snapshot.FishEncounterActor->GetPresentationState().GroundNormal : FVector::UpVector,
		TEXT("Exhausted fish touched shore as world pickup"));
}

// 图鉴首次条件冻结流程：把咬钩成立那一刻的水域、时段、天气一并交给实物鱼（图鉴 §3.1.5:132）。
// 「只在首次写、之后不覆盖」由 Profile 的合并规则保证，这里只负责把三轴都填满——
// 2026-09-12 之前唯一的填充点只写了 RegionId，时段与天气两轴永远是 None，回显不出「什么时候遇上的」。
// 时段/天气用枚举名当稳定 ID：Environment 还没有正式时段/天气资产，未配置时枚举是 Unknown，写进去也是 Unknown，
// 不伪造一个看起来像正式 ID 的名字。
FCatCaptureConditionSnapshot ACatFishingSession::BuildFrozenCaptureCondition() const
{
	FCatCaptureConditionSnapshot Condition;
	Condition.RegionId = AttemptSnapshot.WaterRegion.RegionId;
	Condition.TimeOfDayId = FName(*UEnum::GetValueAsString(BiteTimeOfDay));
	Condition.WeatherId = FName(*UEnum::GetValueAsString(BiteWeather));
	return Condition;
}

// 演出贡献名单收集流程：把本次搏斗摸过竿的猫并进 Participants，让合力拉竿的猫也能进巨物合影
// （图鉴 §4:113,118-119，09-08 已裁）。这里只做「并入演出名单」一件事——
// 收集层归属是上钩者 CatchFisherStableNetId 一人，不登记图鉴、不刷新个人最佳重量，都不经过本函数。
// 竿与搏斗起点这两个入参只有会话知道：目标取本场绑定的同一根竿 Actor（AttemptSnapshot.RodActor，
// 不是可能已被换人清空的 Snapshot.RodActor），窗口取 FightStartedServerTimeSeconds。
// 未开打（负值）、无竿或非权威时不收集，让名单退回只有上钩者，而不是猜一份出来。
void ACatFishingSession::AppendGripContributorsToParticipants(TArray<FString>& Participants) const
{
	const AActor* RodTarget = AttemptSnapshot.RodActor;
	if (!HasAuthority() || !RodTarget || FightStartedServerTimeSeconds < 0.0)
	{
		return;
	}
	TArray<FString> GripContributors;
	UCatPhysicsGrabComponent::CollectGripContributorStableNetIds(GetWorld(), RodTarget,
		FightStartedServerTimeSeconds, GripContributors);
	for (const FString& ContributorStableNetId : GripContributors)
	{
		if (!ContributorStableNetId.IsEmpty())
		{
			Participants.AddUnique(ContributorStableNetId);
		}
	}
}

// 岸上世界鱼的唯一生成流程：真实拖岸从这里交付。先 fail-closed 校验依赖，再收口装备事务
// （扣饵 + §4.4 的渔获磨损 1 点），然后生成 Pickup、退掉水中 Encounter 的可视与碰撞，最后写 Landed 终态。
bool ACatFishingSession::SpawnLandedFishPickupFromAuthority(const FVector& SurfaceLocation,
	const FVector& GroundNormal, const TCHAR* DiagnosticReason)
{
	UWorld* World = GetWorld();
	const UCatFishPickupSettings* Settings = GetDefault<UCatFishPickupSettings>();
	if (!HasAuthority() || IsTerminal() || !World || !Settings || !FishDefinition
		|| !AttemptSnapshot.WaterRegion.IsValid() || !Snapshot.FishEncounterActor
		|| SurfaceLocation.ContainsNaN())
	{
		UE_LOG(LogCatFishing, Error,
			TEXT("Event=landed_fish_pickup_rejected SessionId=%s Authority=%s Terminal=%s World=%s Settings=%s "
				"FishDefinition=%s WaterRegion=%s Encounter=%s Location=%s Reason=Dependency"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			HasAuthority() ? TEXT("true") : TEXT("false"), IsTerminal() ? TEXT("true") : TEXT("false"),
			World ? TEXT("valid") : TEXT("null"),
			Settings ? TEXT("valid") : TEXT("null"), FishDefinition ? TEXT("valid") : TEXT("null"),
			AttemptSnapshot.WaterRegion.IsValid() ? TEXT("valid") : TEXT("invalid"),
			*GetNameSafe(Snapshot.FishEncounterActor), *SurfaceLocation.ToCompactString());
		return false;
	}
	if (!CommitCatchEquipmentFromAuthority())
	{
		UE_LOG(LogCatFishing, Error,
			TEXT("Event=landed_fish_pickup_rejected SessionId=%s Reason=EquipmentCommit"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens));
		return false;
	}
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
	// 上钩者先进名单（收集层归属只认他），再并入本次搏斗摸过竿的合力猫，仅供演出贡献名单。
	TArray<FString> Participants;
	if (!CatchFisherStableNetId.IsEmpty())
	{
		Participants.Add(CatchFisherStableNetId);
	}
	AppendGripContributorsToParticipants(Participants);
	if (!Pickup || !Pickup->InitializeFromAuthority(Snapshot.FishingSessionId, FGuid::NewGuid(),
		FishDefinition, FishWeightKilograms, FishVisualScale, BuildFrozenCaptureCondition(),
		CatchFisherStableNetId, Participants,
		GroundNormal.IsNormalized() ? GroundNormal : FVector::UpVector))
	{
		UE_LOG(LogCatFishing, Error,
			TEXT("Event=landed_fish_pickup_rejected SessionId=%s Reason=%s Location=%s"),
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
		TEXT("Event=landed_fish_pickup_spawned SessionId=%s Pickup=%s Location=%s Rotation=%s "
			"Reason=\"%s\" PickupState=Available WorldNetMode=%d Authority=true %s"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(Pickup),
		*Pickup->GetActorLocation().ToCompactString(), *Pickup->GetActorRotation().ToCompactString(),
		DiagnosticReason ? DiagnosticReason : TEXT("None"),
		static_cast<int32>(World->GetNetMode()),
		*CatLogContext::BuildControllerFields(FisherCharacter.IsValid() ? FisherCharacter->GetController() : nullptr));
	RecordRunCollectionCaptureFromAuthority(*Pickup);
	FinalizeSession(ECatFishingPhase::Resolved, ECatFishingOutcome::Landed, DiagnosticReason);
	return true;
}

// 阈值只读持竿猫当前力量；协助者的物理牵引不进入这项属性，
// 且"当前力量不随体力衰减，体力低不降力量"。所以这里读 ASC 的 FishingStrength 属性，
// 而不是 Runner 每步算出的出力值（后者在体力归零时会被压成 0，那是出力不是力量）。
bool ACatFishingSession::TryResolvePrimaryStrength(double& OutPrimaryStrength) const
{
	OutPrimaryStrength = 0.0;
	const ACatCharacter* Fisher = FisherCharacter.Get();
	const UCatAbilitySystemComponent* AbilitySystem = Fisher ? Fisher->GetCatAbilitySystemComponent() : nullptr;
	if (!AbilitySystem)
	{
		return false;
	}
	const double Strength = AbilitySystem->GetNumericAttribute(
		UCatSurvivalAttributeSet::GetFishingStrengthAttribute());
	if (!FMath::IsFinite(Strength) || Strength < 0.0)
	{
		return false;
	}
	OutPrimaryStrength = Strength;
	return true;
}

// 咬钩信号发布流程：
// 1. 按本竿冻结的鱼漂定义读 BiteSignalStability，写进公开快照——这是这个字段第一个运行消费者。
// 2. 稳定度达到全场门槛的那一款（铃铛漂）另走一次 GameState 全场广播：GameState 对所有客户端恒相关，
//    湖对岸的猫也一定收得到，这就是「不受距离衰减」在网络这一层的含义；音效自己的衰减配置归表现层。
// 3. 无论过不过门槛都记一行诊断，带上鱼漂、稳定度和门槛——一局游戏就能核出铃铛漂资产的落值对不对得上。
void ACatFishingSession::PublishBiteSignalFromAuthority()
{
	if (!HasAuthority())
	{
		return;
	}
	const UCatEquipmentDefinition* FloatDefinition = GetDefault<UCatInventorySettings>()
		->FindRuntimeDefinition<UCatEquipmentDefinition>(AttemptSnapshot.FloatDefinitionId);
	const UCatEquipmentFragment_Float* FloatFragment = FloatDefinition
		? FloatDefinition->FindFragment<UCatEquipmentFragment_Float>() : nullptr;
	if (FloatFragment == nullptr || !FloatFragment->IsRuntimeReady())
	{
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_bite_signal SessionId=%s Float=%s Result=FloatFragmentUnavailable"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			*AttemptSnapshot.FloatDefinitionId.ToString());
		return;
	}
	const double Stability = FMath::Clamp(FloatFragment->BiteSignalStability, 0.0, 1.0);
	Snapshot.BiteSignalStability = static_cast<float>(Stability);

	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	const double Threshold = Settings ? Settings->WorldwideBiteSignalStabilityThreshold : 0.0;
	const bool bWorldwide = Settings != nullptr && FMath::IsFinite(Threshold) && Threshold <= 1.0
		&& Stability >= Threshold;
	if (bWorldwide)
	{
		if (ACatfishingGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr)
		{
			const FVector SignalLocation = Snapshot.HookActor
				? Snapshot.HookActor->GetActorLocation() : GetActorLocation();
			GameState->Multicast_PlayWorldwideFishingSignal(
				CatFishingAbilityTags::Cosmetic_Fishing_BiteBell, SignalLocation);
		}
	}
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_bite_signal SessionId=%s Float=%s Stability=%.3f Threshold=%.3f Worldwide=%s"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
		*AttemptSnapshot.FloatDefinitionId.ToString(), Stability, Threshold,
		bWorldwide ? TEXT("true") : TEXT("false"));
}

// 竿强度读取流程：静态配置，三档 25／60／210（钓鱼规则 §4.1:162）。0 或读不到都表示"未裁"，
// 调用方一律不得据此瞬断——没落数据的鱼竿不能一中鱼就断。
// 承载字段沿用历史名 UCatEquipmentFragment_Rod::FishingStrength（2026-09-04 停用、本轮按 D-21 恢复），
// 它是鱼竿资产的静态配置，与猫的 GAS 属性 UCatSurvivalAttributeSet::FishingStrength（猫力）不是同一个来源。
bool ACatFishingSession::TryResolveRodStrength(double& OutRodStrength) const
{
	OutRodStrength = 0.0;
	const UCatEquipmentDefinition* RodDefinition = GetDefault<UCatInventorySettings>()
		->FindRuntimeDefinition<UCatEquipmentDefinition>(AttemptSnapshot.RodDefinitionId);
	const UCatEquipmentFragment_Rod* RodFragment = RodDefinition
		? RodDefinition->FindFragment<UCatEquipmentFragment_Rod>() : nullptr;
	if (!RodFragment || !FMath::IsFinite(RodFragment->FishingStrength) || RodFragment->FishingStrength <= 0.0)
	{
		return false;
	}
	OutRodStrength = RodFragment->FishingStrength;
	return true;
}

// 鱼竿承载检查：只判断器材能否承受本场负载，不依据猫鱼力量比直接收鱼。
// 瞬时判定，只在搏斗开始与显式换主时各跑一次；抓猫、松手和属性变化不另开终局裁决。
bool ACatFishingSession::EvaluateRodStrengthFromAuthority(const TCHAR* Trigger)
{
	if (!HasAuthority() || IsTerminal() || SelectionResolution != ECatFishSelectionResolution::Selected)
	{
		return false;
	}
	// 仅采样本次持竿猫的属性，不缓存合力、不按固定步重查。
	double PrimaryStrength = 0.0;
	const double FishStrength = Snapshot.FishStrength; // 已含完美削减，§3.4（:149）要求此后一律用削后值。
	if (!TryResolvePrimaryStrength(PrimaryStrength)
		|| !FMath::IsFinite(FishStrength) || FishStrength <= 0.0)
	{
		// 读不到任一边的力量就不裁瞬断；依赖缺失由各自的 fail-closed 门禁处理。
		return false;
	}
	double RodStrength = 0.0;
	const bool bRodStrengthConfigured = TryResolveRodStrength(RodStrength);
	// ① 竿强瞬断：竿强度不超过「持竿猫力量与鱼力中较小的那个」就当场断竿，张力由两端较小者决定。
	if (bRodStrengthConfigured && RodStrength <= FMath::Min(PrimaryStrength, FishStrength))
	{
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fishing_rod_strength_snapped SessionId=%s Trigger=%s RodStrength=%.3f PrimaryStrength=%.3f "
				"FishStrength=%.3f Phase=%s RodDefinition=%s %s"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), Trigger ? Trigger : TEXT("None"),
			RodStrength, PrimaryStrength, FishStrength, *UEnum::GetValueAsString(Snapshot.Phase),
			*AttemptSnapshot.RodDefinitionId.ToString(),
			*CatLogContext::BuildControllerFields(FisherCharacter.IsValid() ? FisherCharacter->GetController() : nullptr));
		// 2026-09-12 拍：瞬断和耐久归零一样**报废鱼竿**。设计对两条路用的是同一个词「断竿」、
		// 同一套表现（爪里只剩半截竿）、同一个结局类（器材失败）；不报废，强度门槛就没有牙齿——
		// 拿树枝竿一遍遍去钓巨影，每次只赔一份饵。饵同样要扣：§3.3(:133)「进入咬钩后，无论上鱼、
		// 超时、放弃、断竿、落水，都消耗 1 份饵」。
		if (UCatEquipmentComponent* Equipment = CastEquipment.Get())
		{
			const FCatFishingUseOperationResult Bait = Equipment->CommitFishingBaitDeferred(Snapshot.FishingSessionId);
			double RemainingDurability = 0.0;
			bool bAlreadyBroken = false;
			Equipment->GetFishingRodDurability(Snapshot.FishingSessionId, RemainingDurability, bAlreadyBroken);
			if (!bAlreadyBroken && FMath::IsFinite(RemainingDurability) && RemainingDurability > 0.0)
			{
				// 磨损接口按累计绝对值写回：把剩余耐久一次性加满即归零，序号严格 +1，不另存第二份账。
				const FCatFishingUseOperationResult Wear = Equipment->ApplyFishingRodWear(Snapshot.FishingSessionId,
					Bait.WearSequence + 1, Bait.AbsoluteRodWear + RemainingDurability);
				if (Wear.bApplied)
				{
					RodWearSequence = Wear.WearSequence;
					Snapshot.RodDurabilityRemaining = Wear.RemainingRodDurability;
				}
				UE_LOG(LogCatFishing, Warning,
					TEXT("Event=fishing_rod_broken SessionId=%s RodItemInstanceId=%s Cause=StrengthSnap "
						"RodDurability=%.3f Applied=%s"),
					*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
					*AttemptSnapshot.RodItemInstanceId.ToString(), Wear.RemainingRodDurability,
					Wear.bApplied ? TEXT("true") : TEXT("false"));
			}
		}
		TerminateSession(ECatFishingOutcome::LineBroken,
			TEXT("Rod strength did not exceed the smaller of primary strength and fish strength; rod destroyed"));
		return true;
	}
	// 鱼竿可承受本场负载，后续运动与收鱼交给 Runner。
	return false;
}

// 苏醒计时流程：鱼一翻肚就起算（钓鱼规则 §5.3:260"拖动中计时照走"），到点仍没上岸就苏醒逃跑。
void ACatFishingSession::ScheduleExhaustedRevivalTimerFromAuthority()
{
	if (!HasAuthority() || IsTerminal() || Snapshot.Phase != ECatFishingPhase::ExhaustedReel)
	{
		return;
	}
	if (GetWorldTimerManager().IsTimerActive(ExhaustedRevivalTimerHandle))
	{
		return; // 重复阶段事件不重新起算，否则反复进 ExhaustedReel 就能无限续命。
	}
	GetWorldTimerManager().SetTimer(ExhaustedRevivalTimerHandle, this, &ThisClass::HandleExhaustedRevivalTimer,
		GetDefault<UCatFishingSettings>()->GetExhaustedFishRevivalSeconds(), false);
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_exhausted_revival_armed SessionId=%s RevivalSeconds=%.2f FishDefinition=%s"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), GetDefault<UCatFishingSettings>()->GetExhaustedFishRevivalSeconds(),
		FishDefinition ? *FishDefinition->FishDefinitionId.ToString() : TEXT("None"));
}

void ACatFishingSession::HandleExhaustedRevivalTimer()
{
	if (!HasAuthority() || IsTerminal() || Snapshot.Phase != ECatFishingPhase::ExhaustedReel)
	{
		return;
	}
	if (!bResolvingRevival)
	{
		if (UCatFishingResolutionSubsystem* Queue = GetWorld()->GetSubsystem<UCatFishingResolutionSubsystem>())
		{
			Queue->Enqueue(ECatFishingResolution::Revival, nullptr, Snapshot.FishingSessionId,
				[WeakThis = TWeakObjectPtr<ThisClass>(this)]()
				{
					if (!WeakThis.IsValid()) return;
					TGuardValue<bool> Guard(WeakThis->bResolvingRevival, true);
					WeakThis->HandleExhaustedRevivalTimer();
				});
			return;
		}
	}
	// "实际拖上岸后不再苏醒"：鱼已经越过岸线就让收尾继续，不再重排计时。
	if (FightRunner && FightRunner->IsFishBeachedForAuthority())
	{
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_exhausted_revival_skipped SessionId=%s Reason=AlreadyBeached"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens));
		return;
	}
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_exhausted_fish_revived SessionId=%s RevivalSeconds=%.2f FishDefinition=%s Result=Escaped %s"),
		*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), GetDefault<UCatFishingSettings>()->GetExhaustedFishRevivalSeconds(),
		FishDefinition ? *FishDefinition->FishDefinitionId.ToString() : TEXT("None"),
		*CatLogContext::BuildControllerFields(FisherCharacter.IsValid() ? FisherCharacter->GetController() : nullptr));
	TerminateSession(ECatFishingOutcome::Escaped, TEXT("Exhausted fish revived before reaching the shore"));
}

void ACatFishingSession::SuspendOperatorFromAuthority()
{
	ClearCancelHoldFromAuthority();
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
		// 放下鱼竿只解除操作关系；体力始终归身体所有，保留离开瞬间的余额。
	}
	StaminaOwner.Reset(); // 所有允许放下的阶段都解除体力域归属，离开的猫带走当时剩下的体力（§4.6:230）。
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
		|| ScoopingCharacter->GetMouthCarriedActor() != nullptr)
	{
		return false;
	}

	// 上钩者 + 抄网命中者 + 本次搏斗摸过竿的合力猫，三方都只进演出贡献名单；
	// 抄网命中同样不登记收集、不刷新个人最佳重量，归属仍是 CatchFisherStableNetId（图鉴 §4:118-119）。
	TArray<FString> Participants;
	if (!CatchFisherStableNetId.IsEmpty())
	{
		Participants.Add(CatchFisherStableNetId);
	}
	Participants.AddUnique(ScooperStableNetId);
	AppendGripContributorsToParticipants(Participants);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = nullptr;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FRotator SpawnRotation = Encounter->GetActorRotation();
	SpawnRotation.Pitch = 0.0;
	SpawnRotation.Roll = 0.0;
	ACatFishPickupActor* Pickup = World->SpawnActor<ACatFishPickupActor>(ACatFishPickupActor::StaticClass(),
		Encounter->GetActorLocation(), SpawnRotation, SpawnParams);
	// 抄网命中者只进演出贡献名单；图鉴收集层仍归上钩者，所以 HookerStableNetId 传 CatchFisherStableNetId
	// 而不是抄手（钓鱼规则 §5.6:285「实物被队友抢走不取消登记」）。
	if (!Pickup || !Pickup->InitializeFromAuthority(Snapshot.FishingSessionId, FGuid::NewGuid(), FishDefinition,
		FishWeightKilograms, FishVisualScale, BuildFrozenCaptureCondition(), CatchFisherStableNetId, Participants)
		|| !Pickup->BeginMouthCarryFromAuthority(ScoopingCharacter, ScoopingPlayerState))
	{
		if (Pickup)
		{
			Pickup->Destroy();
		}
		return false;
	}

	bCaptureResolved = true;
	RecordRunCollectionCaptureFromAuthority(*Pickup);
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

// 收鱼成功后立即上公共板子，既不等待个人 Profile ACK，也不等待把鱼送进鱼护；账号缺失时只报错，不猜拾取者。
void ACatFishingSession::RecordRunCollectionCaptureFromAuthority(const ACatFishPickupActor& Pickup) const
{
	const auto& Fish = Pickup.GetPresentationState();
	const ACatfishingGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
	UCatRunFishCollectionComponent* Collection = GameState ? GameState->GetRunFishCollection() : nullptr;
	if (!Collection || !Collection->RecordCaptureFromAuthority(Fish.FishInstanceId, Fish.FishDefinitionId, CatchFisherStableNetId))
	{
		UE_LOG(LogCatRun, Warning,
			TEXT("Event=run_collection_capture_delivery_failed World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s SessionId=%s FishInstanceId=%s HasCollection=%d HasHooker=%d"),
			*GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole(), *GetNameSafe(this),
			*Snapshot.FishingSessionId.ToString(), *Fish.FishInstanceId.ToString(), Collection != nullptr, !CatchFisherStableNetId.IsEmpty());
	}
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
		// 试探期空竿的图鉴剪影不在这里补——它在咬钩成立那一刻就已经揭开且永不撤销（钓鱼规则 §5.6:285），
		// 这条出口只负责收口本竿：饵由 FinalizeSession 的 ReleaseFishingUse 退回（试探期提竿不损饵，§3.4:141）。
		if (StateTreeComponent) StateTreeComponent->SendStateTreeEvent(CatFishingGameplayTags::EarlyHook,
			FConstStructView(), TEXT("CatFishing"));
		Result.bCommitted = true;
		Result.Error = ECatFishingCommandError::None;
		FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::EmptyHook, TEXT("Early hook"));
	}
	else if (Snapshot.Phase == ECatFishingPhase::TrueBiteWindow
		&& SelectionResolution == ECatFishSelectionResolution::Selected && bTrueBiteWindowAcceptingHook)
	{
		// 先冻结服务器收到左键时的响应时间，再停止计时；已在 Probe 选鱼，此处只裁决提竿。
		const double SinceBite = GetWorld()
			? GetWorld()->GetTimeSeconds() - Snapshot.PhaseStartedServerTime : TNumericLimits<double>::Max();
		bTrueBiteWindowAcceptingHook = false;
		GetWorldTimerManager().ClearTimer(TrueBiteTimerHandle);
		// 请求与计时器同帧到达时仍按服务器截止时间裁决，不能利用计时器尚未执行的间隙提竿。
		if (GetWorld()->GetTimeSeconds() > Snapshot.WindowEndsServerTime)
		{
			HandleTrueBiteWindowExpired();
			Result.Error = ECatFishingCommandError::InvalidPhase;
			Result.Revision = Snapshot.Revision;
			HookTerminalByRequest.Add(RequestId, Result);
			return Result;
		}
		Snapshot.bPerfectHook = FMath::IsFinite(SinceBite) && SinceBite >= 0.0
			&& GetWorld()->GetTimeSeconds() <= Snapshot.PerfectWindowEndsServerTime;
		const bool bSpawned = SpawnHookedFishFromAuthority(RequestId);
		Result.bCommitted = bSpawned && (TryEnterHookedFightFromAuthority() || (IsTerminal() && Snapshot.Outcome == ECatFishingOutcome::LineBroken));
		if (Result.bCommitted && !IsTerminal())
		{
			if (IsValid(Snapshot.FishEncounterActor))
				Snapshot.FishEncounterActor->PublishInitialPresentationFromAuthority();
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

// 墓碑（2026-09-14，T16；钓鱼规则 §4.7、参数页）：玩家搏斗收竿由服务器计满 1.5 秒；旧立即终止只供权威内部清理。
FCatFishingCommandResult ACatFishingSession::SetCancelHeldFromAuthority(AController* Controller, bool bHeld, FGuid RequestId)
{
	FCatFishingCommandResult Result;
	Result.CommandType = ECatFishingCommandType::CancelFishing;
	Result.RequestId = RequestId;
	Result.FishingSessionId = Snapshot.FishingSessionId;
	Result.CastAttemptId = Snapshot.CastAttemptId;
	if (!HasAuthority() || !Controller || !RequestId.IsValid()) return Result;
	if (!bHeld)
	{
		if (CancelHoldController == Controller) ClearCancelHoldFromAuthority();
		Result.bCommitted = true; Result.Error = ECatFishingCommandError::None;
		return Result;
	}
	if (IsTerminal() || !Snapshot.RodActor || !Snapshot.RodActor->IsPrimaryOperator(Controller->PlayerState)
		|| UCatGE_FishingScoopCooldown::IsOperationBlocked(Controller->GetPawn()))
	{
		Result.Error = ECatFishingCommandError::NotFisher;
		return Result;
	}
	if (!GetWorldTimerManager().IsTimerActive(CancelHoldTimer))
	{
		CancelHoldController = Controller;
		CancelHoldControlEpoch = Snapshot.RodActor->GetControlEpoch();
		CancelHoldRequestId = RequestId;
		Snapshot.CancelHoldStartedServerTime = GetWorld()->GetTimeSeconds();
		Snapshot.CancelHoldEndsServerTime = Snapshot.CancelHoldStartedServerTime + 1.5;
		GetWorldTimerManager().SetTimer(CancelHoldTimer, this, &ThisClass::CompleteCancelHoldFromAuthority, 1.5f, false);
		PublishSnapshot(ECatFishingSnapshotMutation::HighFrequency);
		UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_cancel_hold_started RequestId=%s SessionId=%s EndsAt=%.3f World=%s NetMode=%d Authority=1 LocalRole=%d"),
			*RequestId.ToString(), *Snapshot.FishingSessionId.ToString(), Snapshot.CancelHoldEndsServerTime, *GetNameSafe(GetWorld()), int32(GetNetMode()), int32(GetLocalRole()));
	}
	Result.bCommitted = true; Result.Error = ECatFishingCommandError::None; // 只确认开始保持，终局另发回执。
	return Result;
}
void ACatFishingSession::ClearCancelHoldFromAuthority()
{
	if (!HasAuthority()) return;
	GetWorldTimerManager().ClearTimer(CancelHoldTimer);
	if (Snapshot.CancelHoldEndsServerTime > 0.0)
	{
		UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_cancel_hold_cleared RequestId=%s SessionId=%s World=%s NetMode=%d Authority=1 LocalRole=%d"),
			*CancelHoldRequestId.ToString(), *Snapshot.FishingSessionId.ToString(), *GetNameSafe(GetWorld()), int32(GetNetMode()), int32(GetLocalRole()));
		Snapshot.CancelHoldStartedServerTime = Snapshot.CancelHoldEndsServerTime = 0.0;
		PublishSnapshot(ECatFishingSnapshotMutation::HighFrequency);
	}
	CancelHoldController.Reset();
	CancelHoldRequestId.Invalidate();
	CancelHoldControlEpoch = 0;
}
void ACatFishingSession::CompleteCancelHoldFromAuthority()
{
	AController* Controller = CancelHoldController.Get();
	if (!Controller || IsTerminal() || !Snapshot.RodActor
		|| !Snapshot.RodActor->IsPrimaryOperator(Controller->PlayerState)
		|| Snapshot.RodActor->GetControlEpoch() != CancelHoldControlEpoch
		|| UCatGE_FishingScoopCooldown::IsOperationBlocked(Controller->GetPawn()))
	{
		ClearCancelHoldFromAuthority(); return;
	}
	const double Remaining = Snapshot.CancelHoldEndsServerTime - GetWorld()->GetTimeSeconds();
	if (Remaining > 0.0)
	{
		// TimerManager 与 World 浮点时间可相差一个舍入尾数；不足 1.5 秒只补等，不误取消保持。
		GetWorldTimerManager().SetTimer(CancelHoldTimer, this, &ThisClass::CompleteCancelHoldFromAuthority,
			float(FMath::Max(Remaining, 0.0001)), false);
		return;
	}
	FCatFishingSessionCommandContext Context;
	Context.RequestId = CancelHoldRequestId;
	Context.FishingSessionId = Snapshot.FishingSessionId;
	Context.CastAttemptId = Snapshot.CastAttemptId;
	Context.ExpectedRevision = Snapshot.Revision;
	const FCatFishingCommandResult Result = CutLineFromAuthority(Controller, Context);
	if (UCatFishingCommandComponent* Commands = Controller->FindComponentByClass<UCatFishingCommandComponent>()) Commands->DeliverResultFromAuthority(Result);
	ClearCancelHoldFromAuthority();
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
	const bool bBeforeFight = PhaseBefore == ECatFishingPhase::CastFlight
		|| PhaseBefore == ECatFishingPhase::Waiting || PhaseBefore == ECatFishingPhase::Probe
		|| PhaseBefore == ECatFishingPhase::TrueBiteWindow;
	const bool bCuttablePhase = PhaseBefore == ECatFishingPhase::HookedFight
		|| PhaseBefore == ECatFishingPhase::NearShore
		|| PhaseBefore == ECatFishingPhase::ExhaustedReel
		|| PhaseBefore == ECatFishingPhase::AutoHauling || bBeforeFight;
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
		const bool bNearRod = RequestingPawn && Rod && FVector::DistSquared(
			RequestingPawn->GetActorLocation(), Rod->GetActorLocation()) <= FMath::Square(250.0);
		return !(bUnattendedGroundRod && bNearRod);
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
		FinalizeSession(ECatFishingPhase::Terminated, bBeforeFight ? ECatFishingOutcome::Cancelled : ECatFishingOutcome::LineCut,
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
	ClearCancelHoldFromAuthority();
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
	// 本竿结束，挂着的换人请求随之失效（多人钓鱼附篇 §2.4：本竿结束自然失效，没有超时出口）。
	// 这里直接清字段而不调 ClearHandoffRequestFromAuthority：终态下面统一发布一次快照。
	Snapshot.HandoffRequestedByPlayerState = nullptr;
	Snapshot.PhaseStartedServerTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	Snapshot.FishLineAlignment = 0.0f;
	Snapshot.NormalizedLineLoad = 0.0f;
	Snapshot.BiteSignalStability = 0.0f;
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
	GetWorldTimerManager().ClearTimer(ProbeStayTimerHandle);
	GetWorldTimerManager().ClearTimer(TrueBiteTimerHandle);
	GetWorldTimerManager().ClearTimer(ExhaustedRevivalTimerHandle);
	// 释放原始抛竿者装备上属于本 Session 的钓具预留；其他鱼竿的并行预留保持不变。
	if (UCatEquipmentComponent* Equipment = CastEquipment.Get())
	{
		// 道具册「鱼竿断裂后直接消失」：耐久归零与竿强瞬断共用同一结局，断掉的那根不回背包、不占格。
		// 位置定在 ReleaseFishingUse 之前：使用记录此时还在，报废入口靠它才找得到这一竿绑定的是哪根实例。
		// 判据只问「绑定实例现在是不是断的」，不问是哪条路断的——两条路本来就是同一个结局（2026-09-12 拍）。
		double RemainingRodDurability = 0.0;
		bool bBoundRodBroken = false;
		if (Equipment->GetFishingRodDurability(Snapshot.FishingSessionId, RemainingRodDurability, bBoundRodBroken)
			&& bBoundRodBroken)
		{
			const bool bRetired = Equipment->RetireBrokenFishingRodFromAuthority(Snapshot.FishingSessionId);
			// 库存实例和世界里那根竿是两件东西，同一次断竿要两边都收：实例报废掉，场上那根半截竿也不留。
			ACatFishingRodActor* BrokenRod = Snapshot.RodActor;
			bool bRodActorDestroyed = false;
			if (bRetired && BrokenRod)
			{
				if (UCatFishingService* RodService = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr)
				{
					RodService->RetireDeployedRodActorFromAuthority(BrokenRod);
					bRodActorDestroyed = true;
					// 终态快照在本函数开头就已经发布过了；这里把引用清空只是不让复制结构继续指着一个已销毁的 Actor。
					Snapshot.RodActor = nullptr;
				}
			}
			UE_LOG(LogCatFishing, Warning,
				TEXT("Event=fishing_broken_rod_discarded SessionId=%s RodItemInstanceId=%s ItemRetired=%s RodActorDestroyed=%s"),
				*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
				*AttemptSnapshot.RodItemInstanceId.ToString(), bRetired ? TEXT("true") : TEXT("false"),
				bRodActorDestroyed ? TEXT("true") : TEXT("false"));
		}
		// 墓碑（2026-09-13）：上鱼不再要求返还已提交鱼饵；所有终局共用未提交才返还的结算入口。
		Equipment->ReleaseFishingUse(Snapshot.FishingSessionId);
	}
	if (StateTreeComponent && StateTreeComponent->IsRunning())
	{
		StateTreeComponent->StopLogic(FString(DiagnosticReason));
	}
	// 09-11 裁决④：终局不再把主控体力回满。搏斗体力是跨竿资源，剩多少就带走多少，
	// 「搏斗外 5 点/秒」的恢复由属性侧负责，这里只解除体力域归属并留一行诊断。
	if (const ACatCharacter* Participant = StaminaOwner.Get())
	{
		const UCatAbilitySystemComponent* AbilitySystem = Participant->GetCatAbilitySystemComponent();
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_stamina_carried_over SessionId=%s Owner=%s TotalFightStamina=%.3f TotalCapacity=%.3f Result=NotRefilled"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(Participant),
			AbilitySystem ? AbilitySystem->GetTotalFightStamina() : 0.0,
			AbilitySystem ? AbilitySystem->GetTotalFightStaminaCapacity() : 0.0);
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

// World 清理流程：先停 FightRunner 并清 Bite/Probe/试探停留/TrueBite/苏醒计时器，再停止仍在运行的 StateTree。
// authority 随后释放本会话的 Equipment use 并清弱引用；最后重置 ItemsService 和钓手引用后交给 Super，
// 既不补发捕获事务，也不回满任何人的搏斗体力。
void ACatFishingSession::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(CancelHoldTimer);
	if (FightRunner) FightRunner->Stop();
	GetWorldTimerManager().ClearTimer(BiteWarningTimerHandle);
	GetWorldTimerManager().ClearTimer(ProbeTimerHandle);
	GetWorldTimerManager().ClearTimer(ProbeStayTimerHandle);
	GetWorldTimerManager().ClearTimer(TrueBiteTimerHandle);
	GetWorldTimerManager().ClearTimer(ExhaustedRevivalTimerHandle);
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
		// 09-11 裁决④：World teardown 同样不补体力，只解除本会话的体力域归属。
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
	if (World && (Snapshot.Phase == ECatFishingPhase::Probe || Snapshot.Phase == ECatFishingPhase::TrueBiteWindow)
		&& LastReceivedWindowPhaseEpoch != Snapshot.PhaseEpoch)
	{
		LastReceivedWindowPhaseEpoch = Snapshot.PhaseEpoch;
		UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_window_received SessionId=%s Fish=%s Phase=%s ResponseEnds=%.3f PerfectEnds=%.3f World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s"),
			*Snapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *Snapshot.FishDefinitionId.ToString(), *UEnum::GetValueAsString(Snapshot.Phase),
			Snapshot.WindowEndsServerTime, Snapshot.PerfectWindowEndsServerTime, *GetNameSafe(World), int32(GetNetMode()), HasAuthority(), int32(GetLocalRole()), *GetName());
	}

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
        Stamina = ASC->GetTotalFightStamina();
        Strength = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFishingStrengthAttribute());
        Maximum = ASC->GetTotalFightStaminaCapacity();
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

void ACatFishingSession::RefreshGrowthFromAuthority(const ACatCharacter* Character,
	const ECatGrowthOptionId OptionId, const double AppliedDelta)
{
	const AActor* GrowthOwner = OptionId == ECatGrowthOptionId::BiteInterval
		? (CastEquipment.IsValid() ? CastEquipment->GetOwner() : nullptr) : FisherCharacter.Get();
	if (!HasAuthority() || IsTerminal() || GrowthOwner != Character) return;
	if (OptionId == ECatGrowthOptionId::PerfectWindow && Snapshot.Phase == ECatFishingPhase::TrueBiteWindow)
	{
		Snapshot.PerfectWindowEndsServerTime = FMath::Min(Snapshot.WindowEndsServerTime,
			Snapshot.PhaseStartedServerTime + 1.0 + GetFisherGrowthMagnitude(OptionId));
		PublishSnapshot(ECatFishingSnapshotMutation::Discrete);
	}
	if (OptionId == ECatGrowthOptionId::BiteInterval && Snapshot.Phase == ECatFishingPhase::Waiting
		&& GetWorldTimerManager().IsTimerActive(ProbeTimerHandle))
	{
		const double NewScale = 1.0 + GetFisherGrowthMagnitude(OptionId);
		const double OldScale = NewScale - AppliedDelta;
		const auto* Hook = Snapshot.HookActor.Get();
		const double FlightRemaining = Hook ? FMath::Max(0.0, Hook->GetPresentationState().CastTrajectory.StartedServerTime
			+ Hook->GetPresentationState().CastTrajectory.DurationSeconds - GetWorld()->GetTimeSeconds()) : 0.0;
		const double Remaining = GetWorldTimerManager().GetTimerRemaining(ProbeTimerHandle);
		const double Delay = FMath::Max(UE_DOUBLE_KINDA_SMALL_NUMBER, FlightRemaining
			+ FMath::Max(0.0, Remaining - FlightRemaining) * NewScale / OldScale);
		// 保留已发出的预警；尚未发出的预警按与咬钩相同的提前量重排。
		if (GetWorldTimerManager().IsTimerActive(BiteWarningTimerHandle))
		{
			const double Lead = Remaining - GetWorldTimerManager().GetTimerRemaining(BiteWarningTimerHandle);
			GetWorldTimerManager().SetTimer(BiteWarningTimerHandle, this, &ThisClass::HandleBiteWarningTimer,
				FMath::Max(UE_DOUBLE_KINDA_SMALL_NUMBER, Delay - Lead), false);
		}
		GetWorldTimerManager().SetTimer(ProbeTimerHandle, this, &ThisClass::HandleProbeTimer, Delay, false);
		UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_growth_wait_rescheduled SessionId=%s OldRemainingSeconds=%.3f NewRemainingSeconds=%.3f Scale=%.3f World=%s NetMode=%d Authority=1 LocalRole=%d"),
			*Snapshot.FishingSessionId.ToString(), Remaining, Delay, NewScale, *GetNameSafe(GetWorld()), GetNetMode(), GetLocalRole());
	}
}
