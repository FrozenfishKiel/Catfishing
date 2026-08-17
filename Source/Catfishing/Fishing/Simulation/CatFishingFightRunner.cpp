#include "Fishing/Simulation/CatFishingFightRunner.h"

#include "AbilitySystem/CatAbilitySystemComponent.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Fishing/Actors/CatFishEncounterActor.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Simulation/CatFishFightMotionSolver.h"
#include "TimerManager.h"

bool UCatFishingFightRunner::InitializeFromAuthority(const FCatFishingFightRunnerInit& Init)
{
	ACatFishingSession* SessionActor = Init.Session.Get();
	if (bInitialized || !SessionActor || !SessionActor->HasAuthority() || !Init.FishActor.IsValid()
		|| !Init.RodActor.IsValid() || !Init.AbilitySystem.IsValid() || !Init.Equipment.IsValid()
		|| !Init.FishingSessionId.IsValid() || !Init.WaterRegion.IsValid() || !Init.FrozenWaterBounds.IsValid
		|| !Init.Config.IsValid() || Init.RandomSeed == 0
		|| Init.CalmDurationRangeSeconds.X <= 0.0 || Init.CalmDurationRangeSeconds.Y < Init.CalmDurationRangeSeconds.X
		|| Init.StruggleDurationRangeSeconds.X <= 0.0 || Init.StruggleDurationRangeSeconds.Y < Init.StruggleDurationRangeSeconds.X)
	{
		return false;
	}
	Session = Init.Session;
	FishActor = Init.FishActor;
	RodActor = Init.RodActor;
	AbilitySystem = Init.AbilitySystem;
	Equipment = Init.Equipment;
	FishingSessionId = Init.FishingSessionId;
	WaterRegion = Init.WaterRegion;
	FrozenWaterBounds = Init.FrozenWaterBounds;
	Config = Init.Config;
	State = Init.InitialState;
	CalmDurationRangeSeconds = Init.CalmDurationRangeSeconds;
	StruggleDurationRangeSeconds = Init.StruggleDurationRangeSeconds;
	Random.Initialize(static_cast<int32>(Init.RandomSeed));
	State.MotionIntent = ECatFishMotionIntent::CalmOrInward;
	MotionSecondsRemaining = Random.FRandRange(CalmDurationRangeSeconds.X, CalmDurationRangeSeconds.Y);
	bInitialized = true;
	return true;
}

bool UCatFishingFightRunner::Start()
{
	ACatFishingSession* SessionActor = Session.Get();
	UWorld* World = SessionActor ? SessionActor->GetWorld() : nullptr;
	if (!bInitialized || bRunning || !World || !SessionActor->HasAuthority()) return false;
	bRunning = true;
	World->GetTimerManager().SetTimer(FixedStepTimer, this, &ThisClass::HandleFixedStep,
		static_cast<float>(Config.FixedStepSeconds), true);
	return true;
}

void UCatFishingFightRunner::Stop()
{
	if (ACatFishingSession* SessionActor = Session.Get())
	{
		if (UWorld* World = SessionActor->GetWorld()) World->GetTimerManager().ClearTimer(FixedStepTimer);
	}
	bRunning = false;
}

bool UCatFishingFightRunner::SetReeling(const int64 InputSequence, const bool bInReeling)
{
	if (!bInitialized || !bRunning || InputSequence <= LastInputSequence) return false;
	LastInputSequence = InputSequence;
	State.bReeling = bInReeling;
	return true;
}

void UCatFishingFightRunner::SelectNextMotionIntent()
{
	if (State.MotionIntent == ECatFishMotionIntent::StrugglingOutward)
	{
		State.MotionIntent = ECatFishMotionIntent::CalmOrInward;
		MotionSecondsRemaining = Random.FRandRange(CalmDurationRangeSeconds.X, CalmDurationRangeSeconds.Y);
	}
	else
	{
		State.MotionIntent = ECatFishMotionIntent::StrugglingOutward;
		MotionSecondsRemaining = Random.FRandRange(StruggleDurationRangeSeconds.X, StruggleDurationRangeSeconds.Y);
	}
}

void UCatFishingFightRunner::HandleFixedStep()
{
	ACatFishingSession* SessionActor = Session.Get();
	ACatFishEncounterActor* Encounter = FishActor.Get();
	ACatFishingRodActor* Rod = RodActor.Get();
	UCatAbilitySystemComponent* ASC = AbilitySystem.Get();
	UCatEquipmentComponent* EquipmentComponent = Equipment.Get();
	if (!bRunning || !SessionActor || !SessionActor->HasAuthority() || !Encounter || !Rod || !ASC || !EquipmentComponent)
	{
		Stop();
		if (SessionActor) SessionActor->HandleFightRunnerFailureFromAuthority();
		return;
	}

	MotionSecondsRemaining -= Config.FixedStepSeconds;
	if (MotionSecondsRemaining <= 0.0) SelectNextMotionIntent();
	State.FishWorldPosition = Encounter->GetActorLocation();
	const FVector RodTip = Rod->GetRodTipWorldTransform().GetLocation();
	FVector Outward = State.FishWorldPosition - RodTip;
	if (Outward.IsNearlyZero()) Outward = FVector::ForwardVector;
	const FCatFightStepResult Step = FCatFishingFightSimulator::Step(Config, State, RodTip, Outward);
	FCatFishMotionSolveInput MotionInput;
	MotionInput.RodTipWorldPosition = RodTip;
	MotionInput.ProposedFishWorldPosition = Step.ProposedFishWorldPosition;
	MotionInput.WaterBounds = FrozenWaterBounds;
	MotionInput.MaximumLineLengthCentimeters = Config.MaximumLineLengthCentimeters;
	FCatFishMotionSolveResult Motion = FCatFishFightMotionSolver::Solve(MotionInput);
	UCatWaterQuerySubsystem* Water = SessionActor->GetWorld()->GetSubsystem<UCatWaterQuerySubsystem>();
	const FCatWaterSpatialResult Exact = Motion.bSucceeded && Water
		? Water->ResolveCandidatePointToWater(Motion.FishWorldPosition, WaterRegion) : FCatWaterSpatialResult{};
	if (!Step.bSucceeded || !Motion.bSucceeded || !Exact.bSucceeded)
	{
		Stop();
		SessionActor->HandleFightRunnerFailureFromAuthority();
		return;
	}
	Motion.FishWorldPosition = Exact.WaterSurfaceWorldPoint;
	if (Step.CatStaminaDrain > 0.0 && !ASC->ApplyFishingStaminaDelta(static_cast<float>(-Step.CatStaminaDrain)))
	{
		Stop();
		SessionActor->HandleFightRunnerFailureFromAuthority();
		return;
	}
	const FCatFishingUseOperationResult Wear = EquipmentComponent->SetAccumulatedFishingRodWear(
		FishingSessionId, ++WearSequence, Step.AbsoluteRodWear);
	if (!Wear.bApplied || !Encounter->ApplyFightStepFromAuthority(State.MotionIntent,
		Step.LineLengthCentimeters, Motion.FishWorldPosition))
	{
		Stop();
		SessionActor->HandleFightRunnerFailureFromAuthority();
		return;
	}
	State.CatStamina = FMath::Max(0.0, State.CatStamina - Step.CatStaminaDrain);
	State.FishStamina = FMath::Max(0.0, State.FishStamina - Step.FishStaminaDrain);
	State.LineLengthCentimeters = Step.LineLengthCentimeters;
	State.AbsoluteRodWear = Step.AbsoluteRodWear;
	State.FishWorldPosition = Encounter->GetActorLocation();
	SessionActor->HandleFightRunnerStepFromAuthority(Step, State.FishStamina, State.MotionIntent);
}
