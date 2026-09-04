#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Character/CatCharacter.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/Simulation/CatFishingRodResistanceModel.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/PlayerState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingRodEffortSeparatesActiveAndPassiveTest,
	"Catfishing.Unit.Fishing.Simulation.RodEffortSeparatesActiveAndPassiveRotation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingRodEffortSeparatesActiveAndPassiveTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFishingRodRotationInput Input;
	Input.CurrentAim.Yaw = 60.0;
	Input.RequestedAim.Yaw = 120.0;
	Input.MaximumFishTorque = 100.0;
	Input.PreviousSmoothedFishPullStrengthMeters = FVector(100.0, 0.0, 0.0);
	Input.DeltaSeconds = 1.0 / 60.0;
	const auto Passive = FCatFishingRodResistanceModel::StepRotation(Input);
	TestTrue(TEXT("fish can rotate the rod with no cat torque"),
		Passive.bSucceeded && Passive.ActualAim.Yaw < Input.CurrentAim.Yaw);
	TestEqual(TEXT("passive dragging has no cat intent effort"), Passive.CatIntentArcCentimeters, 0.0);
	TestEqual(TEXT("passive dragging has no cat work"), Passive.CatActualArcCentimeters, 0.0);

	Input.CatTorqueCapacity = 50.0;
	const auto Opposed = FCatFishingRodResistanceModel::StepRotation(Input);
	TestTrue(TEXT("fish overcomes active cat torque in the opposite direction"),
		Opposed.bSucceeded && Opposed.ActualAim.Yaw < Input.CurrentAim.Yaw);
	TestTrue(TEXT("trying to turn against an overpowering fish still records effort"),
		Opposed.CatIntentArcCentimeters > 0.0);
	TestEqual(TEXT("opposite rotation does not count as successful active work"),
		Opposed.CatActualArcCentimeters, 0.0, 1e-9);

	Input.CurrentAim.Yaw = 30.0;
	const auto Holding = FCatFishingRodResistanceModel::StepRotation(Input);
	TestTrue(TEXT("holding torque equilibrium still records effort"), Holding.CatIntentArcCentimeters > 0.0);
	TestEqual(TEXT("equilibrium produces no realized work"), Holding.CatActualArcCentimeters, 0.0, 1e-7);

	Input.MaximumFishTorque = 0.0;
	Input.PreviousSmoothedFishPullStrengthMeters = FVector::ZeroVector;
	const auto Free = FCatFishingRodResistanceModel::StepRotation(Input);
	TestTrue(TEXT("active unloaded rotation completes positive work"), Free.CatActualArcCentimeters > 0.0);
	TestEqual(TEXT("unloaded effort is realized without resistance"),
		Free.CatActualArcCentimeters, Free.CatIntentArcCentimeters, 1e-7);

	Input.DeltaSeconds = 1.0;
	const auto Hitch = FCatFishingRodResistanceModel::StepRotation(Input);
	TestEqual(TEXT("effort covers only the quarter second actually integrated during a hitch"),
		Hitch.IntegratedSeconds, 0.25, 1e-9);
	Input.DeltaSeconds = 0.0;
	const auto Paused = FCatFishingRodResistanceModel::StepRotation(Input);
	TestEqual(TEXT("zero-time pose refresh cannot duplicate intent effort"), Paused.CatIntentArcCentimeters, 0.0);
	TestEqual(TEXT("zero-time pose refresh cannot duplicate actual effort"), Paused.CatActualArcCentimeters, 0.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingRodEffortFrameRateTest,
	"Catfishing.Unit.Fishing.Simulation.RodEffortIsStableAcrossFrameRates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingRodEffortFrameRateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	double ReferenceIntent = 0.0;
	double ReferenceActual = 0.0;
	for (const int32 Rate : {120, 60, 20})
	{
		FCatFishingRodRotationInput Input;
		Input.CatTorqueCapacity = 50.0;
		Input.RequestedAim = FRotator(25.0, 120.0, 0.0);
		Input.DeltaSeconds = 1.0 / Rate;
		double TotalIntent = 0.0;
		double TotalActual = 0.0;
		double TotalSeconds = 0.0;
		for (int32 Phase = 0; Phase < 6; ++Phase)
		{
			Input.MaximumFishTorque = Phase < 4 ? 100.0 : 0.0;
			Input.PullAxis = Phase < 2 ? FVector::ForwardVector : -FVector::ForwardVector;
			for (int32 Frame = 0; Frame < Rate / 2; ++Frame)
			{
				const auto Step = FCatFishingRodResistanceModel::StepRotation(Input);
				if (!TestTrue(TEXT("rotation effort integrates successfully"), Step.bSucceeded)) return false;
				Input.CurrentAim = Step.ActualAim;
				Input.PreviousSmoothedFishPullStrengthMeters = Step.SmoothedFishPullStrengthMeters;
				TotalIntent += Step.CatIntentArcCentimeters;
				TotalActual += Step.CatActualArcCentimeters;
				TotalSeconds += Step.IntegratedSeconds;
			}
		}
		if (Rate == 120)
		{
			ReferenceIntent = TotalIntent;
			ReferenceActual = TotalActual;
		}
		TestTrue(TEXT("changing line loads produces both active and blocked effort"),
			TotalIntent > TotalActual && TotalActual > 0.0);
		TestEqual(TEXT("intent totals agree across 20, 60 and 120 FPS"), TotalIntent, ReferenceIntent, 0.05);
		TestEqual(TEXT("realized totals agree across 20, 60 and 120 FPS"), TotalActual, ReferenceActual, 0.05);
		TestEqual(TEXT("all frame rates account for the same three seconds"), TotalSeconds, 3.0, 1e-8);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingRodEffortSnapshotLifecycleTest,
	"Catfishing.Unit.Fishing.Actors.RodEffortSnapshotSurvivesSamplingAndResetsWithOwnerAndFight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingRodEffortSnapshotLifecycleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("create rod effort world"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
	WorldWrapper.ForwardErrorMessages(this);
	WorldWrapper.BeginPlayInTestWorld();
	UWorld* World = WorldWrapper.GetTestWorld();
	ACatfishingPlayerController* Controller = World->SpawnActor<ACatfishingPlayerController>();
	ACatfishingPlayerState* PlayerState = World->SpawnActor<ACatfishingPlayerState>();
	ACatCharacter* Character = World->SpawnActor<ACatCharacter>();
	ACatFishingRodActor* Rod = World->SpawnActor<ACatFishingRodActor>();
	APlayerState* NextHolder = World->SpawnActor<APlayerState>();
	if (!TestNotNull(TEXT("controller"), Controller) || !TestNotNull(TEXT("player state"), PlayerState)
		|| !TestNotNull(TEXT("character"), Character) || !TestNotNull(TEXT("rod"), Rod)
		|| !TestNotNull(TEXT("next holder"), NextHolder)) return false;
	Controller->PlayerState = PlayerState;
	Character->SetPlayerState(PlayerState);
	Controller->Possess(Character);
	Controller->SetControlRotation(FRotator::ZeroRotator);
	TestTrue(TEXT("initialize held rod"), Rod->InitializeAuthoritativeIdentity(
		FGuid::NewGuid(), FGuid::NewGuid(), TEXT("EffortRod"), TEXT("Skin"), PlayerState, PlayerState, true, false));
	TestTrue(TEXT("initialize held aim"), Rod->RefreshHeldTransformFromAuthority());
	TestTrue(TEXT("start fight rotation"), Rod->SetCarrierConstraintFromAuthority(
		FVector::ForwardVector, 0.0, 0.0, 1.0, 1.0, 0.0, true, 100.0, 50.0));
	Controller->SetControlRotation(FRotator(0.0, 120.0, 0.0));
	TestTrue(TEXT("integrate active rotation"), Rod->RefreshHeldTransformFromAuthority(1.0 / 60.0));
	const auto First = Rod->GetAuthoritativeRotationEffortSnapshot();
	TestTrue(TEXT("authoritative integration accumulates effort"), First.IntentArcCentimeters > 0.0);
	const auto Repeated = Rod->GetAuthoritativeRotationEffortSnapshot();
	TestEqual(TEXT("multiple fixed-step readers see the same cumulative intent"),
		Repeated.IntentArcCentimeters, First.IntentArcCentimeters);
	TestEqual(TEXT("multiple readers do not consume or duplicate actual effort"),
		Repeated.ActualArcCentimeters, First.ActualArcCentimeters);
	TestTrue(TEXT("zero-time refresh succeeds"), Rod->RefreshHeldTransformFromAuthority());
	TestEqual(TEXT("zero-time refresh retains the same effort snapshot"),
		Rod->GetAuthoritativeRotationEffortSnapshot().IntentArcCentimeters, First.IntentArcCentimeters);
	TestTrue(TEXT("slack update retains the same fight"), Rod->SetCarrierConstraintFromAuthority(
		FVector::ForwardVector, 0.0, 0.0, 1.0, 0.0, 0.0, true, 0.0, 50.0));
	TestEqual(TEXT("load changes cannot erase unconsumed effort"),
		Rod->GetAuthoritativeRotationEffortSnapshot().Epoch, First.Epoch);
	Rod->ClearCarrierConstraintFromAuthority();
	const auto Cleared = Rod->GetAuthoritativeRotationEffortSnapshot();
	TestTrue(TEXT("fight cleanup starts a new epoch"), Cleared.Epoch > First.Epoch);
	TestEqual(TEXT("fight cleanup drops previous fight effort"), Cleared.IntentArcCentimeters, 0.0);
	TestTrue(TEXT("restart fight rotation"), Rod->SetCarrierConstraintFromAuthority(
		FVector::ForwardVector, 0.0, 0.0, 1.0, 1.0, 0.0, true, 100.0, 50.0));
	TestTrue(TEXT("new fight collects fresh effort"), Rod->RefreshHeldTransformFromAuthority(1.0 / 60.0));
	const auto Restarted = Rod->GetAuthoritativeRotationEffortSnapshot();
	TestTrue(TEXT("restart gets a new epoch and effort"),
		Restarted.Epoch > Cleared.Epoch && Restarted.IntentArcCentimeters > 0.0);
	TestTrue(TEXT("transfer holder"), Rod->SetOperatorFromAuthority(
		NextHolder, Rod->GetPresentationState().RodActorRevision));
	const auto Transferred = Rod->GetAuthoritativeRotationEffortSnapshot();
	TestTrue(TEXT("holder transfer changes epoch"), Transferred.Epoch > Restarted.Epoch);
	TestEqual(TEXT("new holder cannot inherit former holder effort"), Transferred.IntentArcCentimeters, 0.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingRodEffortFixedStepAllocationTest,
	"Catfishing.Unit.Fishing.Simulation.RodEffortSplitsSlowFramesAndDiscardsPreviousHolderBacklog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingRodEffortFixedStepAllocationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFishingRodEffortSampler Sampler;
	const FCatFishingRodRotationEffortSnapshot Baseline{7, 200.0, 100.0, 2.0};
	Sampler.Reset(Baseline);
	const auto Existing = Sampler.Consume(Baseline, 0.05);
	TestEqual(TEXT("initial baseline cannot charge effort collected before attachment"),
		Existing.IntentArcCentimeters, 0.0);
	const FCatFishingRodRotationEffortSnapshot SlowFrame{7, 210.0, 104.0, 2.1};
	const auto FirstStep = Sampler.Consume(SlowFrame, 0.05);
	const auto SecondStep = Sampler.Consume(SlowFrame, 0.05);
	for (const auto& Step : {FirstStep, SecondStep})
	{
		TestEqual(TEXT("each 50 ms catch-up step receives half the 100 ms intent"),
			Step.IntentArcCentimeters, 5.0, 1e-8);
		TestEqual(TEXT("each catch-up step receives half the realized work"),
			Step.ActualArcCentimeters, 2.0, 1e-8);
		TestEqual(TEXT("each catch-up step accounts for its own 50 ms"),
			Step.IntegratedSeconds, 0.05, 1e-8);
	}
	const auto Exhausted = Sampler.Consume(SlowFrame, 0.05);
	TestEqual(TEXT("reading an exhausted snapshot cannot fabricate more effort"),
		Exhausted.IntentArcCentimeters, 0.0);
	TestEqual(TEXT("reading an exhausted snapshot cannot fabricate elapsed time"),
		Exhausted.IntegratedSeconds, 0.0);

	const FCatFishingRodRotationEffortSnapshot MoreBacklog{7, 230.0, 112.0, 2.2};
	Sampler.Consume(MoreBacklog, 0.05);
	const FCatFishingRodRotationEffortSnapshot NewHolder{8, 3.0, 1.0, 0.03};
	const auto Handoff = Sampler.Consume(NewHolder, 0.05);
	TestEqual(TEXT("new epoch retains only the new holder's effort"), Handoff.IntentArcCentimeters, 3.0, 1e-8);
	TestEqual(TEXT("new epoch discards previous holder's remaining realized work"),
		Handoff.ActualArcCentimeters, 1.0, 1e-8);
	TestEqual(TEXT("a partially covered step never invents integration time"), Handoff.IntegratedSeconds, 0.03, 1e-8);
	TestEqual(TEXT("new holder effort is consumed exactly once"),
		Sampler.Consume(NewHolder, 0.05).IntentArcCentimeters, 0.0);

	const FCatFishingRodRotationEffortSnapshot NewBacklog{8, 13.0, 5.0, 0.13};
	Sampler.Consume(NewBacklog, 0.05);
	Sampler.Reset(NewBacklog);
	TestEqual(TEXT("explicit reset also drops any pending effort"),
		Sampler.Consume(NewBacklog, 0.05).IntentArcCentimeters, 0.0);
	return !HasAnyErrors();
}

#endif
