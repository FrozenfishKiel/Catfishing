#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/Integration/CatFishingPhysicalRodComponent.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/CatFishingService.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "OnlineSubsystemTypes.h"
#include "Fishing/Simulation/CatFishingFightRunner.h"
#include "GameFramework/PlayerState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingOperatorRunnerIntegrationTest,
	"Catfishing.Unit.Fishing.Runner.OperatorSamplesAndSingleASCSettlementRemainConservative",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingOperatorRunnerIntegrationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper Wrapper;
	if (!TestTrue(TEXT("single-operator sampler contract world"), Wrapper.CreateTestWorld(EWorldType::Game))) return false;
	Wrapper.ForwardErrorMessages(this);
	UWorld* World = Wrapper.GetTestWorld();
	FURL URL;
	URL.AddOption(TEXT("game=/Script/Catfishing.CatfishingGameModeBase"));
	if (!World->SetGameMode(URL) || !Wrapper.BeginPlayInTestWorld()) return false;
	auto* Mode = World->GetAuthGameMode<ACatfishingGameModeBase>();
	Mode->bRunCommandsOpen = true;
	Mode->RunPublicState.Phase.Phase = ECatRunPhase::DayActive;
	Mode->RunPublicState.Phase.bFishingAllowed = true;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACatCharacter* Cat = World->SpawnActor<ACatCharacter>(FVector(0, 0, 20), FRotator::ZeroRotator, Spawn);
	APlayerState* Player = World->SpawnActor<ACatfishingPlayerState>();
	auto* Controller = World->SpawnActor<ACatfishingPlayerController>();
	ACatFishingSession* Session = World->SpawnActor<ACatFishingSession>();
	ACatFishingRodActor* Rod = World->SpawnActor<ACatFishingRodActor>();
	if (!Cat || !Player || !Controller || !Session || !Rod) return false;
	Controller->PlayerState = Player;
	Controller->Possess(Cat);
	Controller->SetActorTickEnabled(false);
	Player->SetPlayerId(1);
	const FUniqueNetIdRef NetId = FUniqueNetIdString::Create(TEXT("OperatorSamplingContract"), FName(TEXT("CAT_TEST")));
	Player->SetUniqueId(FUniqueNetIdRepl(NetId));
	ACatfishingGameModeBase::FAdmissionRecord Admission;
	Admission.Phase = ACatfishingGameModeBase::EAdmissionPhase::Active;
	Admission.Controller = Controller;
	Mode->AdmissionRecords.Add(ACatfishingGameModeBase::MakeStableNetIdKey(Player->GetUniqueId()), Admission);
	UCatAbilitySystemComponent* ASC = Cat->GetCatAbilitySystemComponent();
	UCatPhysicalBodyComponent* Movement = Cat->GetPhysicalBodyComponent();
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(), 60.0f);
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 30.0f);
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 100.0f);
	if (!TestTrue(TEXT("the owner is admitted and deploys a real rod identity"), Mode->CanAcceptFishingCommand(Controller)
		&& Rod->InitializeAuthoritativeIdentity(FGuid::NewGuid(), FGuid::NewGuid(), TEXT("OperatorSamplingRod"), NAME_None, Player, nullptr, true, false)
		&& World->GetSubsystem<UCatFishingService>()->RegisterDeployedRod(Player, Rod))) return false;
	const auto HoldAndAuthorize = [&]()
	{
		return Rod->BeginPhysicalHoldFromAuthority(Player, true)
			&& Rod->SetPrimaryOperatorFromAuthority(Player, Rod->GetPresentationState().RodActorRevision) && Rod->GetPhysicalRodComponent()->CommitPrimaryHold(Player);
	};
	if (!TestTrue(TEXT("a real hand constraint precedes explicit owner control"), HoldAndAuthorize())) return false;
	UCatFishingFightRunner* Runner = NewObject<UCatFishingFightRunner>(Session);
	Runner->Session = Session;
	Runner->RodActor = Rod;
	Runner->AbilitySystem = ASC;
	Runner->bInitialized = Runner->bRunning = true;
	if (!Runner->BindPrimaryOperatorFromAuthority(Player, true, false, 0)) return false;
	Runner->Config.FixedStepSeconds = 0.05;
	Runner->Config.CatStaminaMaximum = 60.0;
	Runner->Config.FishMassKilograms = 3.0;
	Runner->Config.FishStrength = 40.0;
	Runner->Config.ReelSpeedCentimetersPerSecond = 80.0;
	Runner->Config.FishFullEffortSpeedCentimetersPerSecond = 75.0;
	Runner->Config.MaximumLineLengthCentimeters = 1000.0;
	Runner->Config.RodDurability = 1000.0;
	Runner->State.FishStamina = 100.0;
	Runner->State.LineLengthCentimeters = 500.0;
	Runner->State.FishWorldPosition = FVector(500.0, 0.0, 0.0);
	Runner->State.MotionIntent = ECatFishMotionIntent::StrugglingOutward;

	if (!TestTrue(TEXT("the production refresh reads the actual primary ASC"), Runner->UpdateOperatorIntentAndProperties())) return false;
	TestEqual(TEXT("only primary strength enters the reel model"), Runner->Config.PrimaryOperatorCatStrength, 100.0);
	TestEqual(TEXT("only primary stamina enters the session model"), Runner->State.CatStamina, 30.0);
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(), 75.0f);
	Runner->UpdateOperatorIntentAndProperties();
	TestEqual(TEXT("a changed primary maximum is sampled rather than cached at entry"), Runner->Config.CatStaminaMaximum, 75.0);
	TestEqual(TEXT("raising maximum does not refill the actual primary balance"), Runner->State.CatStamina, 30.0);
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(), 20.0f);
	Runner->UpdateOperatorIntentAndProperties();
	TestEqual(TEXT("only ASC clamps a reduced maximum"), Runner->State.CatStamina, 20.0);
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(), 60.0f);
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 30.0f);
	Runner->UpdateOperatorIntentAndProperties();
	FCatFightRodConstraintInput Constraint;
	Constraint.bRodHeld = true;
	Constraint.bPhysicalRodEndpoint = true;
	Constraint.RodForwardWorld = FVector::ForwardVector;
	const auto Step = FCatFishingFightSimulator::Step(Runner->Config, Runner->State, Constraint, FVector::ForwardVector);
	if (!TestTrue(TEXT("the live line solver generates one operation bill"), Step.bSucceeded && Step.CatStaminaDrain > 0.0)
		|| !TestTrue(TEXT("the production writer charges the primary ASC"), Runner->ApplyOperatorStaminaChanges(Step))) return false;
	const float Balance = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	TestEqual(TEXT("one operation bill is conserved in the actual ASC"), 30.0 - Balance, Step.CatStaminaDrain, 1e-5);
	TestFalse(TEXT("the frozen bill cannot be replayed"), Runner->ApplyOperatorStaminaChanges(Step));
	TestEqual(TEXT("replay rejection preserves the balance"), ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), Balance);

	// Contract evidence: these are controlled body observations and sampling times, not a
	// second physics integrator. Separate Chaos runtime tests verify actual motor travel.
	Runner->State.LineLengthCentimeters = 800.0;
	Runner->State.FishEffortRatio = 0.0;
	TArray<double> ScheduleDrains, ScheduleTravel;
	for (int32 Schedule = 0; Schedule < 2; ++Schedule)
	{
		ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 30.0f);
		Movement->TeleportBodyFromAuthority(FTransform(FVector(-1000, 0, 20)), TEXT("SettlementFixtureStart"));
		if (!TestTrue(TEXT("reset fixture reestablishes real hand contact and owner control"), HoldAndAuthorize())) return false;
		Movement->SetMovementSpeed(600.0);
		Movement->SetMoveIntent(FVector::ForwardVector);
		FCatFightOperatorRuntime& Operator = Runner->OperatorState;
		Operator.bPullHeld = Operator.bSlackHeld = false;
		Operator.LastSampledPosition = Movement->GetBody()->GetComponentLocation();
		Operator.LastBodyResetEpoch = Movement->GetResetEpoch();
		Operator.LastMovementSampleWorldSeconds = World->GetTimeSeconds();
		Operator.PendingMovementSamples.Reset();
		Operator.bHasSampledPosition = true;
		const double StartX = Movement->GetBody()->GetComponentLocation().X;
		for (int32 FixedStepIndex = 0; FixedStepIndex < 2; ++FixedStepIndex)
		{
			if (Schedule == 1 || FixedStepIndex == 0)
			{
				const float FrameSeconds = Schedule == 0 ? 0.10f : 0.05f;
				World->TimeSeconds += FrameSeconds;
				Movement->GetBody()->SetWorldLocation(Movement->GetBody()->GetComponentLocation()
					+ FVector(600.0 * FrameSeconds, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
			}
			if (!TestTrue(TEXT("each fixed step freezes the real body observation"), Runner->UpdateOperatorIntentAndProperties())) return false;
			if (Schedule == 0 && FixedStepIndex == 0)
			{
				const auto& Pending = Runner->OperatorState.PendingMovementSamples;
				TestTrue(TEXT("a 100 ms sample retains the second 50 ms and its progress"), Pending.Num() == 1
					&& FMath::IsNearlyEqual(Pending[0].DurationSeconds, 0.05, 1e-7)
					&& Pending[0].ActualDisplacementCentimeters.X > 29.9);
			}
			const auto MovementStep = FCatFishingFightSimulator::Step(Runner->Config, Runner->State, Constraint, FVector::ForwardVector);
			if (!TestTrue(TEXT("unloaded line has no extra operation bill"), MovementStep.bSucceeded && MovementStep.CatStaminaDrain == 0.0)
				|| !TestTrue(TEXT("personal movement bill reaches the sole ASC writer"), Runner->ApplyOperatorStaminaChanges(MovementStep))) return false;
		}
		ScheduleDrains.Add(30.0 - ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()));
		ScheduleTravel.Add(Movement->GetBody()->GetComponentLocation().X - StartX);
		double RemainingSeconds = 0.0;
		for (const auto& Pending : Runner->OperatorState.PendingMovementSamples) RemainingSeconds += Pending.DurationSeconds;
		TestTrue(TEXT("two fixed steps consume the complete sample exactly once"), RemainingSeconds < 1e-7);
	}
	TestTrue(TEXT("schedule comparison includes real observations and a nonzero ASC bill"), ScheduleTravel[0] > 59.9 && ScheduleDrains[0] > 0.1);
	TestEqual(TEXT("one 100 ms frame and two 50 ms frames conserve observed progress"), ScheduleTravel[0], ScheduleTravel[1], 1e-4);
	TestEqual(TEXT("catch-up timers cannot double-charge one body's movement"), ScheduleDrains[0], ScheduleDrains[1], 1e-5);

	World->TimeSeconds += 0.05;
	const uint32 BeforeReset = Movement->GetResetEpoch();
	Movement->TeleportBodyFromAuthority(FTransform(Movement->GetBody()->GetComponentLocation() + FVector(1000, 0, 0)), TEXT("SettlementCorrection"));
	// Teleport really releases the rod. Rebuild contact and explicitly authorize the same
	// deployment owner before resuming the sampler; no helper or automatic promotion exists.
	if (!TestTrue(TEXT("the sampler continues only after real owner regrip"), HoldAndAuthorize())) return false;
	Movement->SetMoveIntent(FVector::ForwardVector);
	TestTrue(TEXT("teleport changes the actual body reset epoch"), Movement->GetResetEpoch() != BeforeReset);
	if (!Runner->UpdateOperatorIntentAndProperties()) return false;
	for (const auto& Sample : Runner->FrozenOperatorMovementSamples)
		TestTrue(TEXT("teleport cannot be billed as voluntary progress"), Sample.ActualDisplacementCentimeters.IsNearlyZero());
	const auto TeleportStep = FCatFishingFightSimulator::Step(Runner->Config, Runner->State, Constraint, FVector::ForwardVector);
	TestTrue(TEXT("reset boundary has no observed duration to bill"), TeleportStep.bSucceeded && Runner->ApplyOperatorStaminaChanges(TeleportStep)
		&& Runner->LastOperatorStaminaDrain == 0.0);
	World->TimeSeconds += 0.05;
	if (!Runner->UpdateOperatorIntentAndProperties()) return false;
	const auto Blocked = FCatFishingFightSimulator::Step(Runner->Config, Runner->State, Constraint, FVector::ForwardVector);
	TestTrue(TEXT("the next full blocked interval retains the primary support bill"), Blocked.bSucceeded && Runner->ApplyOperatorStaminaChanges(Blocked)
		&& Runner->LastOperatorStaminaDrain > 0.09);
	Movement->SetMoveIntent(FVector::ZeroVector);
	World->TimeSeconds += 0.05;
	Movement->GetBody()->SetWorldLocation(Movement->GetBody()->GetComponentLocation() + FVector(10, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
	if (!Runner->UpdateOperatorIntentAndProperties()) return false;
	const auto Passive = FCatFishingFightSimulator::Step(Runner->Config, Runner->State, Constraint, FVector::ForwardVector);
	TestTrue(TEXT("passive body movement cannot create a voluntary bill"), Passive.bSucceeded && Runner->ApplyOperatorStaminaChanges(Passive)
		&& Runner->LastOperatorStaminaDrain == 0.0);
	for (int32 Phase = 0; Phase < 3; ++Phase)
	{
		Runner->State.bFishExhausted = Phase == 1;
		Runner->State.LineLengthCentimeters = Phase == 2 ? Runner->Config.MaximumLineLengthCentimeters : 800.0;
		Runner->OperatorState.bSlackHeld = true;
		World->TimeSeconds += 0.05;
		Movement->SetMoveIntent(FVector::ForwardVector);
		Movement->GetBody()->SetWorldLocation(Movement->GetBody()->GetComponentLocation() + FVector(10, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
		if (!Runner->UpdateOperatorIntentAndProperties()) return false;
		TestTrue(TEXT("free-phase checks include a nonempty voluntary body sample"), !Runner->FrozenOperatorMovementSamples.IsEmpty()
			&& Runner->FrozenOperatorMovementSamples[0].ActualDisplacementCentimeters.X > 9.9);
		const auto PhaseStep = FCatFishingFightSimulator::Step(Runner->Config, Runner->State, Constraint, FVector::ForwardVector);
		if (!TestTrue(TEXT("phase policy and movement reach one ASC writer"), PhaseStep.bSucceeded && Runner->ApplyOperatorStaminaChanges(PhaseStep))) return false;
		if (Phase < 2) TestTrue(TEXT("slack recovery and exhausted fish suppress voluntary movement charges"), Runner->LastOperatorStaminaDrain <= 0.0);
		else TestTrue(TEXT("full spool restores the real primary movement bill"), !PhaseStep.bSlackRecoveryActive && Runner->LastOperatorStaminaDrain > 0.0);
	}
	AddInfo(FString::Printf(TEXT("Event=fishing_operator_sampling_contract_verified OneFrameTravelCm=%.3f TwoFrameTravelCm=%.3f OneFrameDrain=%.6f TwoFrameDrain=%.6f Evidence=contract"),
		ScheduleTravel[0], ScheduleTravel[1], ScheduleDrains[0], ScheduleDrains[1]));
	return !HasAnyErrors();
}
#endif
