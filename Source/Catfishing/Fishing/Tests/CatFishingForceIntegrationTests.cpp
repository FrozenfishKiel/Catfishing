#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Character/CatCharacter.h"
#include "Character/CatCharacterMovementComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/PlayerState.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/Simulation/CatFishingFightSimulator.h"
#include "Fishing/Simulation/CatFishingRodResistanceModel.h"

namespace
{
	FCatFightSimulationConfig ForceConfig()
	{
		FCatFightSimulationConfig C;
		C.FixedStepSeconds = 0.05;
		C.PrimaryOperatorCatStrength = 50.0;
		C.PrimaryOperatorMassKilograms = 5.0;
		C.FishMassKilograms = 3.0;
		C.FishStrength = 75.0;
		C.CatStaminaMaximum = 100.0;
		C.ReelSpeedCentimetersPerSecond = 80.0;
		C.FishCalmSpeedCentimetersPerSecond = 25.0;
		C.FishStruggleSpeedCentimetersPerSecond = 75.0;
		C.MaximumLineLengthCentimeters = 1000.0;
		C.RodDurability = 1000.0;
		return C;
	}
	FCatFightSimulationState ForceState()
	{
		FCatFightSimulationState S;
		S.CatStamina = S.FishStamina = 100.0;
		S.FishWorldPosition = FVector(500.0, 0.0, 0.0);
		S.LineLengthCentimeters = 500.0;
		S.MotionIntent = ECatFishMotionIntent::StrugglingOutward;
		S.CatAction = ECatFightCatAction::Pull;
		return S;
	}
	void AcceptStep(FCatFightSimulationState& S, const FCatFightStepResult& R, double Dt)
	{
		S.FishVelocityCentimetersPerSecond = (R.ProposedFishWorldPosition - S.FishWorldPosition) / Dt;
		S.FishWorldPosition = R.ProposedFishWorldPosition;
		S.LineLengthCentimeters = R.LineLengthCentimeters;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingCommonForceTest,
	"Catfishing.Unit.Fishing.Simulation.CommonForceLimitsReelAndDrivesBothLoads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingCommonForceTest::RunTest(const FString& Parameters)
{
	auto C = ForceConfig();
	const auto S = ForceState();
	FCatFightRodConstraintInput Rod;
	Rod.bRodHeld = true;
	const auto Strong = FCatFishingFightSimulator::Step(C, S, Rod, FVector::ForwardVector);
	TestTrue(TEXT("strong fish solves"), Strong.bSucceeded);
	TestTrue(TEXT("reel request remains visible"), Strong.RequestedReelDistanceCentimeters > 0.0);
	TestEqual(TEXT("reel cannot shorten against greater sustained thrust"), Strong.ActualReelDistanceCentimeters, 0.0);
	TestEqual(TEXT("stalled reel does no positive work"), Strong.CatReelStaminaDrain, 0.0);
	TestTrue(TEXT("stalled support still costs stamina"), Strong.CatHoldStaminaDrain > 0.0);
	TestEqual(TEXT("stationary taut line balances fish thrust in newtons"), Strong.LineTensionNewtons, 75.0, 1e-6);
	TestEqual(TEXT("cat acceleration uses that tension minus support divided by mass"),
		Strong.CarrierPullAccelerationCentimetersPerSecondSquared, 500.0, 1e-6);
	FCatFishingRodResistanceInput Rotation;
	Rotation.CatStrength = C.PrimaryOperatorCatStrength;
	Rotation.LineTensionNewtons = Strong.LineTensionNewtons;
	Rotation.RodPhysicsLengthCentimeters = 200.0;
	Rotation.RodLineAlignment = 0.0;
	TestEqual(TEXT("rod consumes the same line force without another fish-direction multiplier"),
		FCatFishingRodResistanceModel::Evaluate(Rotation).MaximumFishTorqueStrengthMeters, 150.0, 1e-6);
	C.FishStrength = 5.0;
	auto WeakState = S;
	for (int32 I = 0; I < 80; ++I)
	{
		const auto Step = FCatFishingFightSimulator::Step(C, WeakState, Rod, FVector::ForwardVector);
		if (!TestTrue(TEXT("continuous weak-fish reel solves"), Step.bSucceeded)) return false;
		TestTrue(TEXT("reel never exceeds capacity"), Step.LineTensionNewtons <= 50.0 + 1e-5);
		AcceptStep(WeakState, Step, C.FixedStepSeconds);
	}
	TestTrue(TEXT("finite reel really retrieves weak fish over time"), WeakState.FishWorldPosition.X < 300.0);
	auto FinalState = S;
	C.FishStrength = 5.0;
	auto Final = FCatFishingFightSimulator::Step(C, FinalState, Rod, FVector::ForwardVector);
	Final.LineLengthCentimeters = FinalState.LineLengthCentimeters;
	Final.ProposedFishWorldPosition = FVector(490.0, 0.0, 0.0);
	Final.LineTensionNewtons = Final.NormalizedTension = 0.0;
	TestTrue(TEXT("surface result can finalize work without committing resources"), FCatFishingFightSimulator::FinalizeResolvedStep(C, FinalState, Rod, Final));
	TestEqual(TEXT("canceled reel cannot charge requested work"), Final.CatReelStaminaDrain, 0.0);
	TestEqual(TEXT("resolved slack clears support cost"), Final.CatHoldStaminaDrain, 0.0);
	TestEqual(TEXT("resolved slack clears fish resistance cost"), Final.FishStaminaDrain, 0.0);
	const double Cost = Final.CatStaminaDrain;
	FCatFishingFightSimulator::FinalizeResolvedStep(C, FinalState, Rod, Final);
	TestEqual(TEXT("finalizing twice does not accumulate cost"), Final.CatStaminaDrain, Cost);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingActualEndpointTest,
	"Catfishing.Unit.Fishing.Simulation.BlockedCarrierIntentCannotCreatePhantomEndpointMotion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingActualEndpointTest::RunTest(const FString& Parameters)
{
	const auto C = ForceConfig();
	auto S = ForceState();
	S.CatAction = ECatFightCatAction::None;
	FCatFightRodConstraintInput Rod;
	Rod.bRodHeld = true;
	const auto Still = FCatFishingFightSimulator::Step(C, S, Rod, FVector::ForwardVector);
	Rod.CarrierDesiredVelocityCentimetersPerSecond = FVector(-600.0, 0.0, 0.0);
	for (int32 I = 0; I < 200; ++I)
	{
		const auto Blocked = FCatFishingFightSimulator::Step(C, S, Rod, FVector::ForwardVector);
		if (!TestTrue(TEXT("wall-constrained fight remains valid"), Blocked.bSucceeded)) return false;
		TestTrue(TEXT("desired movement does not change the physical endpoint"), Blocked.ProposedFishWorldPosition.Equals(Still.ProposedFishWorldPosition, 1e-6));
		TestEqual(TEXT("blocked movement does no movement work"), Blocked.CatMovementStaminaDrain, 0.0);
		TestTrue(TEXT("line cannot grow indefinitely while cat is blocked"), Blocked.StraightLineDistanceCentimeters <= S.LineLengthCentimeters + 1e-6);
		AcceptStep(S, Blocked, C.FixedStepSeconds);
	}
	Rod.RodTipWorldPosition.X = -20.0;
	Rod.CarrierVelocityCentimetersPerSecond = FVector(-400.0, 0.0, 0.0);
	const auto Moved = FCatFishingFightSimulator::Step(C, S, Rod, FVector::ForwardVector);
	TestTrue(TEXT("real movement changes the constraint"), Moved.ProposedFishWorldPosition.X < S.FishWorldPosition.X);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingFishInertiaTest,
	"Catfishing.Unit.Fishing.Simulation.FishVelocityPersistsAndFreeSwimConvergesAcrossSteps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingFishInertiaTest::RunTest(const FString& Parameters)
{
	auto RestConfig = ForceConfig(); RestConfig.FishCalmSpeedCentimetersPerSecond = 0.0;
	auto RestState = ForceState(); RestState.CatAction = ECatFightCatAction::Slack;
	RestState.MotionIntent = ECatFishMotionIntent::CalmOrInward;
	const auto Rest = FCatFishingFightSimulator::Step(RestConfig, RestState, FVector::ZeroVector, FVector::ForwardVector);
	TestTrue(TEXT("zero calm speed is a valid resting configuration"), Rest.bSucceeded);
	TestTrue(TEXT("resting fish cannot acquire propulsion from the drag denominator safeguard"), Rest.ProposedFishWorldPosition.Equals(RestState.FishWorldPosition));
	for (double Dt : {0.025, 0.05, 0.1})
	{
		auto C = ForceConfig(); C.FixedStepSeconds = Dt; C.FishStrength = 3.0; C.MaximumLineLengthCentimeters = 10000.0;
		auto S = ForceState(); S.CatAction = ECatFightCatAction::Slack;
		FCatFightRodConstraintInput Rod; Rod.bRodHeld = true;
		for (int32 I = 0; I < FMath::RoundToInt(8.0 / Dt); ++I)
		{
			const auto Step = FCatFishingFightSimulator::Step(C, S, Rod, FVector::ForwardVector);
			TestEqual(TEXT("free spool has no line reaction"), Step.LineTensionNewtons, 0.0);
			AcceptStep(S, Step, Dt);
		}
		TestEqual(TEXT("weak fish approaches its configured free speed"), S.FishVelocityCentimetersPerSecond.X, 75.0, 0.01);
		const auto Reversed = FCatFishingFightSimulator::Step(C, S, Rod, -FVector::ForwardVector);
		TestTrue(TEXT("changing intent cannot instantly reverse stored momentum"), Reversed.ProposedFishWorldPosition.X > S.FishWorldPosition.X);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingMovementReplayTest,
	"Catfishing.Unit.Fishing.Runtime.TractionUsesCollisionAndReplaysSavedForceWithoutLiveOverwrite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingMovementReplayTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Wrapper.GetTestWorld();
	ACatCharacter* Cat = World->SpawnActor<ACatCharacter>();
	UCatCharacterMovementComponent* Movement = Cat ? Cast<UCatCharacterMovementComponent>(Cat->GetCharacterMovement()) : nullptr;
	if (!TestNotNull(TEXT("production character owns the replay-capable movement component"), Movement)) return false;
	Movement->bRunPhysicsWithNoController = true;
	Movement->SetMovementMode(MOVE_Flying);
	Movement->BrakingDecelerationFlying = 0.0f;
	FCatExternalTractionInput Input;
	Input.SourceId = FGuid::NewGuid(); Input.Direction = FVector::ForwardVector;
	Input.AccelerationCentimetersPerSecondSquared = 100.0; Input.SpeedLimitCentimetersPerSecond = 100.0; Input.bActive = true;
	Movement->SetExternalTraction(Cat, Input);
	FCatSavedMove Saved;
	auto* Prediction = static_cast<FNetworkPredictionData_Client_Character*>(Movement->GetPredictionData_Client());
	Saved.SetMoveFor(Cat, 0.05f, FVector::ZeroVector, *Prediction);
	Movement->PerformMovement(0.05f);
	const FVector FirstPosition = Cat->GetActorLocation();
	const FVector FirstVelocity = Movement->Velocity;
	TestTrue(TEXT("traction really moves the capsule"), FirstPosition.X > 0.0);
	Cat->SetActorLocation(FVector::ZeroVector); Movement->Velocity = FVector::ZeroVector;
	Input.Direction = -FVector::ForwardVector;
	Movement->SetExternalTraction(Cat, Input);
	Saved.PrepMoveFor(Cat);
	Movement->PerformMovement(0.05f);
	TestTrue(TEXT("replay retains the original force despite newer opposite force"), Cat->GetActorLocation().Equals(FirstPosition, 1e-6));
	TestTrue(TEXT("replay produces original velocity"), Movement->Velocity.Equals(FirstVelocity, 1e-6));
	TestTrue(TEXT("replay does not overwrite current replicated input"), Movement->GetExternalTraction().Direction.X < 0.0);
	Movement->ClearExternalTraction(Cat);
	TestFalse(TEXT("leaving the source clears the live force"), Movement->GetExternalTraction().bActive);

	AStaticMeshActor* Wall = World->SpawnActor<AStaticMeshActor>();
	Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Wall->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Wall->GetStaticMeshComponent()->SetCollisionResponseToAllChannels(ECR_Block);
	Wall->SetActorTransform(FTransform(FRotator::ZeroRotator, FVector(150.0, 0.0, 0.0), FVector(0.2, 10.0, 10.0)));
	Cat->SetActorLocation(FVector::ZeroVector); Movement->Velocity = FVector::ZeroVector;
	Input.Direction = FVector::ForwardVector; Movement->SetExternalTraction(Cat, Input);
	for (int32 I = 0; I < 100; ++I) Movement->PerformMovement(0.05f);
	TestTrue(TEXT("real collision blocks traction without teleporting through the wall"), Cat->GetActorLocation().X > 50.0 && Cat->GetActorLocation().X < 140.0);
	const FVector BlockedPosition = Cat->GetActorLocation();
	for (int32 I = 0; I < 100; ++I) Movement->PerformMovement(0.05f);
	TestTrue(TEXT("blocked traction does not accumulate phantom translation"), Cat->GetActorLocation().Equals(BlockedPosition, 0.1));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingCarrierHandoffTest,
	"Catfishing.Unit.Fishing.Runtime.CarrierHandoffRejectsOldHolderConstraintAndClearsImmediately",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingCarrierHandoffTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Wrapper.GetTestWorld();
	auto* First = World->SpawnActor<APlayerState>();
	auto* Second = World->SpawnActor<APlayerState>();
	auto* FirstCat = World->SpawnActor<ACatCharacter>();
	auto* SecondCat = World->SpawnActor<ACatCharacter>();
	auto* Rod = World->SpawnActor<ACatFishingRodActor>();
	FirstCat->SetPlayerState(First); SecondCat->SetPlayerState(Second);
	auto* FirstMovement = CastChecked<UCatCharacterMovementComponent>(FirstCat->GetCharacterMovement());
	auto* SecondMovement = CastChecked<UCatCharacterMovementComponent>(SecondCat->GetCharacterMovement());
	if (!TestTrue(TEXT("initialize rod before BeginPlay"), Rod->InitializeAuthoritativeIdentity(FGuid::NewGuid(), FGuid::NewGuid(),
		TEXT("HandoffRod"), TEXT("Skin"), First, First, true, false))) return false;
	TestTrue(TEXT("publish first holder force"), Rod->SetCarrierConstraintFromAuthority(FVector::ForwardVector, 100.0, 100.0, 1.0, 1.0));
	TestTrue(TEXT("first character receives actual movement input"), FirstMovement->GetExternalTraction().bActive);
	const auto OldConstraint = Rod->CarrierConstraintState;
	TestTrue(TEXT("handoff succeeds"), Rod->SetOperatorFromAuthority(Second, Rod->GetPresentationState().RodActorRevision));
	TestFalse(TEXT("old holder releases immediately even before presentation BeginPlay"), FirstMovement->GetExternalTraction().bActive);
	Rod->CarrierConstraintState = OldConstraint;
	Rod->OnRep_CarrierConstraintState();
	TestFalse(TEXT("out-of-order old force cannot attach to the new holder"), SecondMovement->GetExternalTraction().bActive);
	TestTrue(TEXT("new holder can receive a new authority solve"), Rod->SetCarrierConstraintFromAuthority(FVector::ForwardVector, 200.0, 100.0, 1.0, 1.0));
	TestEqual(TEXT("new character receives new force"), SecondMovement->GetExternalTraction().AccelerationCentimetersPerSecondSquared, 200.0);
	TestTrue(TEXT("last holder leaves"), Rod->SetOperatorFromAuthority(nullptr, Rod->GetPresentationState().RodActorRevision));
	TestFalse(TEXT("leaving clears force before ticking is disabled"), SecondMovement->GetExternalTraction().bActive);
	return !HasAnyErrors();
}

#endif
