#if WITH_DEV_AUTOMATION_TESTS

#include <limits>

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Character/CatCharacter.h"
#include "Character/CatCharacterMovementComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Equipment/CatEquipmentDefinition.h"
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
		C.FishFullEffortSpeedCentimetersPerSecond = 75.0;
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
	void AcceptStep(FCatFightSimulationState& S, const FCatFightStepResult& R)
	{
		S.FishVelocityCentimetersPerSecond = R.ResolvedFishVelocityCentimetersPerSecond;
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
		AcceptStep(WeakState, Step);
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
	TestEqual(TEXT("final displacement against 3.75 cm intent retains 13.75 cm shortfall without tension"),
		Final.FishUnfulfilledDistanceCentimeters, 13.75, 1e-6);
	TestEqual(TEXT("actual reverse progress remains billable after surface unloads the line"),
		Final.FishStaminaDrain, 0.1375 * C.FishStaminaPerUnfulfilledMeter, 1e-6);
	const double Cost = Final.CatStaminaDrain;
	const double FishCost = Final.FishStaminaDrain;
	FCatFishingFightSimulator::FinalizeResolvedStep(C, FinalState, Rod, Final);
	TestEqual(TEXT("finalizing twice does not accumulate cost"), Final.CatStaminaDrain, Cost);
	TestEqual(TEXT("finalizing twice does not accumulate fish cost"), Final.FishStaminaDrain, FishCost);
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
		AcceptStep(S, Blocked);
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
	auto RestConfig = ForceConfig();
	auto RestState = ForceState(); RestState.CatAction = ECatFightCatAction::Slack;
	RestState.FishEffortRatio = 0.0;
	RestState.MotionIntent = ECatFishMotionIntent::CalmOrInward;
	const auto Rest = FCatFishingFightSimulator::Step(RestConfig, RestState, FVector::ZeroVector, FVector::ForwardVector);
	TestTrue(TEXT("zero effort is a valid live-fish rest"), Rest.bSucceeded);
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
			AcceptStep(S, Step);
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
	// 同一个来源在张力不足时发布有限减速；必须保存并重放，不能恢复成普通行走急刹。
	Input.Direction = FVector::ForwardVector; Input.AccelerationCentimetersPerSecondSquared = 0.0;
	Input.BrakingDecelerationCentimetersPerSecondSquared = 100.0; Input.SpeedLimitCentimetersPerSecond = 0.0;
	Movement->BrakingDecelerationFlying = 2048.0f;
	Cat->SetActorLocation(FVector::ZeroVector); Movement->Velocity = FVector(100.0, 0.0, 0.0);
	Movement->SetExternalTraction(Cat, Input);
	FCatSavedMove BrakingSaved; BrakingSaved.SetMoveFor(Cat, 0.05f, FVector::ZeroVector, *Prediction);
	Movement->PerformMovement(0.05f);
	const FVector BrakingPosition = Cat->GetActorLocation();
	TestEqual(TEXT("line support brakes continuously without a zero speed-limit snap"), Movement->Velocity.X, 95.0, 0.001);
	Input.Direction = -FVector::ForwardVector; Input.BrakingDecelerationCentimetersPerSecondSquared = 900.0;
	Movement->SetExternalTraction(Cat, Input);
	Cat->SetActorLocation(FVector::ZeroVector); Movement->Velocity = FVector(100.0, 0.0, 0.0);
	BrakingSaved.PrepMoveFor(Cat); Movement->PerformMovement(0.05f);
	TestTrue(TEXT("saved braking replays the original collided movement"), Cat->GetActorLocation().Equals(BrakingPosition, 1e-5));
	TestEqual(TEXT("saved braking preserves its original force"), Movement->Velocity.X, 95.0, 0.001);
	Movement->ClearExternalTraction(Cat);
	TestFalse(TEXT("leaving the source clears the live force"), Movement->GetExternalTraction().bActive);
	Movement->PerformMovement(0.1f);
	TestTrue(TEXT("leaving restores ordinary movement braking"), Movement->Velocity.IsNearlyZero());
	Input.Direction = FVector::ForwardVector;
	Movement->SetExternalTraction(Cat, Input); Movement->PerformMovement(0.05f);
	TestTrue(TEXT("support deceleration cannot push a resting cat backwards"), Movement->Velocity.IsNearlyZero());
	Input.AccelerationCentimetersPerSecondSquared = 100.0; Input.BrakingDecelerationCentimetersPerSecondSquared = 0.0;
	Input.SpeedLimitCentimetersPerSecond = 100.0;

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
	const double ForwardTravel = Movement->GetExternalTractionTravelLimit(FVector::ForwardVector, 20.0);
	TestTrue(TEXT("wall contact removes predicted carrier travel"), ForwardTravel < 0.2);
	TestEqual(TEXT("collision query does not move the body"), Cat->GetActorLocation(), BlockedPosition);
	TestEqual(TEXT("leaving the wall is not blocked by the old contact"), Movement->GetExternalTractionTravelLimit(-FVector::ForwardVector, 20.0), 20.0);
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
	TestTrue(TEXT("publish first holder force"), Rod->SetCarrierConstraintFromAuthority(FVector::ForwardVector, 100.0, 100.0, 1.0, 1.0, true));
	TestTrue(TEXT("publish first holder group solve"), Rod->SetGroupMotionFromAuthority(FVector::ZeroVector, FVector::ZeroVector));
	TestTrue(TEXT("first character receives actual movement input"), FirstMovement->GetExternalTraction().bActive);
	const FTickPrerequisite FirstMovementTick(FirstMovement, FirstMovement->PrimaryComponentTick);
	TestTrue(TEXT("loaded rod samples its endpoint after actual movement"), Rod->PrimaryActorTick.GetPrerequisites().Contains(FirstMovementTick));
	Rod->SetCarrierConstraintFromAuthority(FVector::ForwardVector, 0.0, 0.0, 0.0, 0.0, true);
	TestTrue(TEXT("zero pull retains a complete group solve"), Rod->SetGroupMotionFromAuthority(FVector::ZeroVector, FVector::ZeroVector));
	TestTrue(TEXT("temporary zero pull must retain endpoint sampling order"), Rod->PrimaryActorTick.GetPrerequisites().Contains(FirstMovementTick));
	Rod->OnRep_CarrierConstraintState();
	TestTrue(TEXT("receiving a zero-pull snapshot must retain the same order"), Rod->PrimaryActorTick.GetPrerequisites().Contains(FirstMovementTick));
	Rod->SetCarrierConstraintFromAuthority(FVector::ForwardVector, 0.0, 0.0, 0.0, 0.0, true, 0.0, 50.0,
		FVector::ForwardVector, 200.0, true);
	TestTrue(TEXT("publish the continuous braking group solve"), Rod->SetGroupMotionFromAuthority(FVector::ZeroVector, FVector::ZeroVector));
	TestTrue(TEXT("zero pulling force can still publish a continuous braking phase"), FirstMovement->GetExternalTraction().bActive);
	TestEqual(TEXT("rod delivers the authority braking decision to actual movement"),
		FirstMovement->GetExternalTraction().BrakingDecelerationCentimetersPerSecondSquared, 200.0);
	Rod->SetCarrierConstraintFromAuthority(FVector::ForwardVector, 100.0, 100.0, 1.0, 1.0, true);
	TestTrue(TEXT("restore first holder complete force solve"), Rod->SetGroupMotionFromAuthority(FVector::ZeroVector, FVector::ZeroVector));
	const auto OldConstraint = Rod->CarrierConstraintState;
	TestTrue(TEXT("handoff succeeds"), Rod->SetOperatorFromAuthority(Second, Rod->GetPresentationState().RodActorRevision));
	TestFalse(TEXT("old holder releases immediately even before presentation BeginPlay"), FirstMovement->GetExternalTraction().bActive);
	TestFalse(TEXT("handoff releases the former movement prerequisite"), Rod->PrimaryActorTick.GetPrerequisites().Contains(FirstMovementTick));
	Rod->CarrierConstraintState = OldConstraint;
	Rod->OnRep_CarrierConstraintState();
	TestFalse(TEXT("out-of-order old force cannot attach to the new holder"), SecondMovement->GetExternalTraction().bActive);
	TestTrue(TEXT("new holder can receive a new authority solve"), Rod->SetCarrierConstraintFromAuthority(FVector::ForwardVector, 200.0, 100.0, 1.0, 1.0, true));
	TestTrue(TEXT("new holder completes the current group solve"), Rod->SetGroupMotionFromAuthority(FVector::ZeroVector, FVector::ZeroVector));
	TestEqual(TEXT("new character receives new force"), SecondMovement->GetExternalTraction().AccelerationCentimetersPerSecondSquared, 200.0);
	TestTrue(TEXT("last holder leaves"), Rod->SetOperatorFromAuthority(nullptr, Rod->GetPresentationState().RodActorRevision));
	TestFalse(TEXT("leaving clears force before ticking is disabled"), SecondMovement->GetExternalTraction().bActive);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingSteadyTractionTest,
	"Catfishing.Unit.Fishing.Runtime.ConstantFishThrustDoesNotAlternateTractionAndWalkingBrake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingSteadyTractionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	for (const int32 Rate : {120, 60, 20})
	{
		FTestWorldWrapper Wrapper;
		if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
		auto* Cat = Wrapper.GetTestWorld()->SpawnActor<ACatCharacter>();
		auto* Movement = CastChecked<UCatCharacterMovementComponent>(Cat->GetCharacterMovement());
		Movement->bRunPhysicsWithNoController = true;
		// 使用真实地面和 Walking，覆盖行走摩擦、碰撞和 CMC 的移动子步。
		auto* Floor = Wrapper.GetTestWorld()->SpawnActor<AStaticMeshActor>();
		Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Floor->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Floor->GetStaticMeshComponent()->SetCollisionResponseToAllChannels(ECR_Block);
		Floor->SetActorTransform(FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, -140.0), FVector(100.0, 100.0, 1.0)));
		Movement->SetMovementMode(MOVE_Walking);
		const float OriginalMaxStep = Movement->MaxSimulationTimeStep;
		const int32 OriginalMaxIterations = Movement->MaxSimulationIterations;
		auto C = ForceConfig();
		auto S = ForceState(); S.CatAction = ECatFightCatAction::None;
		FCatFightRodConstraintInput Rod; Rod.bRodHeld = true;
		FCatExternalTractionInput Traction; Traction.SourceId = FGuid::NewGuid(); Traction.Direction = FVector::ForwardVector;
		double MinTension = TNumericLimits<double>::Max(), MaxTension = 0.0;
		double MinSpeed = TNumericLimits<double>::Max(), MaxSpeed = 0.0;
		for (int32 Frame = 0; Frame < Rate * 20; ++Frame)
		{
			if (Frame % (Rate / 20) == 0)
			{
				Rod.RodTipWorldPosition = Cat->GetActorLocation();
				Rod.RodTipWorldPosition.Z = 0.0; // 固定握持偏移使测试鱼线保持水平。
				Rod.CarrierVelocityCentimetersPerSecond = Movement->Velocity;
				Rod.RodTipVelocityCentimetersPerSecond = Movement->Velocity;
				Rod.CarrierTravelLimitCentimeters = Movement->GetExternalTractionTravelLimit(FVector::ForwardVector, 20.0);
				const auto Step = FCatFishingFightSimulator::Step(C, S, Rod, FVector::ForwardVector);
				if (!TestTrue(TEXT("constant fish thrust solves against the actual moved endpoint"), Step.bSucceeded)) return false;
				TestTrue(TEXT("even a tiny positive continuous acceleration has a nonzero speed limit"),
					Step.CarrierPullAccelerationCentimetersPerSecondSquared == 0.0 || Step.CarrierTargetPullSpeedCentimetersPerSecond > 0.0);
				Traction.AccelerationCentimetersPerSecondSquared = Step.CarrierPullAccelerationCentimetersPerSecondSquared;
				Traction.BrakingDecelerationCentimetersPerSecondSquared = Step.CarrierBrakingDecelerationCentimetersPerSecondSquared;
				Traction.SpeedLimitCentimetersPerSecond = Step.CarrierTargetPullSpeedCentimetersPerSecond;
				Traction.bActive = Step.bUseContinuousCarrierTraction;
				Movement->SetExternalTraction(Cat, Traction);
				AcceptStep(S, Step);
				if (Frame >= Rate * 15)
				{
					MinTension = FMath::Min(MinTension, Step.LineTensionNewtons);
					MaxTension = FMath::Max(MaxTension, Step.LineTensionNewtons);
				}
			}
			Movement->PerformMovement(1.0f / Rate);
			if (!TestTrue(TEXT("traction stays on the real walking floor"), Movement->IsMovingOnGround())) return false;
			if (Frame >= Rate * 15)
			{
				MinSpeed = FMath::Min(MinSpeed, Movement->Velocity.X);
				MaxSpeed = FMath::Max(MaxSpeed, Movement->Velocity.X);
			}
		}
		AddInfo(FString::Printf(TEXT("FPS=%d TensionMinN=%.4f TensionMaxN=%.4f SpeedMinCmS=%.4f SpeedMaxCmS=%.4f"),
			Rate, MinTension, MaxTension, MinSpeed, MaxSpeed));
		TestTrue(TEXT("steady fish thrust cannot create a repeated start-stop line load"), MaxTension - MinTension < 1.0);
		TestTrue(TEXT("steady drag keeps moving instead of periodically stopping"), MinSpeed > 10.0 && MaxSpeed - MinSpeed < 3.0);
		TestEqual(TEXT("traction restores the ordinary movement step setting"), Movement->MaxSimulationTimeStep, OriginalMaxStep);
		TestEqual(TEXT("traction restores the ordinary movement iteration setting"), Movement->MaxSimulationIterations, OriginalMaxIterations);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingRodMovementOrderTest,
	"Catfishing.Unit.Fishing.Runtime.HeldRodSamplesMovedBodyDuringPullAndBrakeWorldTicks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingRodMovementOrderTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Wrapper.GetTestWorld();
	// 先创建竿，避免测试碰巧依赖 Actor 注册顺序，而未使用 CMC 前置 Tick。
	auto* Rod = World->SpawnActor<ACatFishingRodActor>();
	auto* Player = World->SpawnActor<APlayerState>();
	auto* Cat = World->SpawnActor<ACatCharacter>();
	Cat->SetPlayerState(Player);
	TestTrue(TEXT("initialize world-ticked held rod"), Rod->InitializeAuthoritativeIdentity(
		FGuid::NewGuid(), FGuid::NewGuid(), TEXT("SamplingRod"), TEXT("Skin"), Player, Player, true, false));
	Wrapper.BeginPlayInTestWorld();
	auto* Movement = CastChecked<UCatCharacterMovementComponent>(Cat->GetCharacterMovement());
	Movement->bRunPhysicsWithNoController = true;
	Movement->SetMovementMode(MOVE_Flying);
	Movement->Velocity = FVector(80.0, 0.0, 0.0);
	Rod->RefreshHeldTransformFromAuthority();
	const FVector GripOffset = Rod->GetGripWorldTransform().GetLocation() - Cat->GetActorLocation();
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		const bool bPulling = (Frame / 6) % 2 == 0;
		Rod->SetCarrierConstraintFromAuthority(FVector::ForwardVector, bPulling ? 100.0 : 0.0,
			bPulling ? 160.0 : 0.0, bPulling ? 1.0 : 0.0, 0.0, true, 0.0, 50.0,
			FVector::ForwardVector, bPulling ? 0.0 : 100.0, true);
		Wrapper.TickTestWorld(1.0f / 60.0f);
		if (!TestTrue(TEXT("grip follows the current collided body in both phases without a frame of lag"),
			Rod->GetGripWorldTransform().GetLocation().Equals(Cat->GetActorLocation() + GripOffset, 0.001))) return false;
	}
	TestTrue(TEXT("the real world loop actually dragged the character"), Cat->GetActorLocation().X > 80.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingShortLineTractionTest,
	"Catfishing.Unit.Fishing.Runtime.ShortElevatedLineKeepsLoadWhileBodyAndRodMove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingShortLineTractionTest::RunTest(const FString& Parameters)
{
	const auto* Definition = LoadObject<UCatEquipmentDefinition>(nullptr, TEXT("/Game/Catfishing/Data/Equipment/Equip_Rod_StarterT1.Equip_Rod_StarterT1"));
	if (!TestNotNull(TEXT("formal rod anchor calibration is available"), Definition)) return false;
	AddInfo(FString::Printf(TEXT("FormalTip=%s FormalGrip=%s"), *Definition->RodTipLocalTransform.ToString(), *Definition->GripLocalTransform.ToString()));
	for (const double FishMass : {5.535, 15.0})
	for (const int32 Rate : {20, 60, 120})
	{
		FTestWorldWrapper Wrapper;
		if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = Wrapper.GetTestWorld();
		auto* Cat = World->SpawnActor<ACatCharacter>();
		auto* Controller = World->SpawnActor<APlayerController>();
		Controller->Possess(Cat);
		Controller->SetControlRotation(FRotator(-25.38, 290.63, 0));
		auto* Player = World->SpawnActor<APlayerState>();
		Cat->SetPlayerState(Player);
		auto* Movement = CastChecked<UCatCharacterMovementComponent>(Cat->GetCharacterMovement());
		Movement->bRunPhysicsWithNoController = true;
		auto* Floor = World->SpawnActor<AStaticMeshActor>();
		Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Floor->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Floor->GetStaticMeshComponent()->SetCollisionResponseToAllChannels(ECR_Block);
		Floor->SetActorTransform(FTransform(FRotator::ZeroRotator, FVector(0, 0, -140), FVector(100, 100, 1)));
		Movement->SetMovementMode(MOVE_Walking);
		auto* Rod = World->SpawnActor<ACatFishingRodActor>();
		Rod->ConfigureCanonicalAnchorsFromAuthority(Definition->RodTipLocalTransform, Definition->StandLocalTransform, Definition->GripLocalTransform);
		Rod->InitializeAuthoritativeIdentity(FGuid::NewGuid(), FGuid::NewGuid(), TEXT("ShortLineRod"), TEXT("Skin"), Player, Player, true, false);
		Rod->RefreshHeldTransformFromAuthority();
		auto C = ForceConfig();
		C.FishMassKilograms = FishMass;
		C.FishStrength = FishMass * 10.0;
		C.FishFullEffortSpeedCentimetersPerSecond = 180;
		auto S = ForceState();
		S.CatAction = ECatFightCatAction::None;
		S.FishWorldPosition = Rod->GetRodTipWorldTransform().GetLocation() + FVector(-55.8, -100, -148.88);
		S.LineLengthCentimeters = FVector::Distance(S.FishWorldPosition, Rod->GetRodTipWorldTransform().GetLocation());
		double MinLoad = TNumericLimits<double>::Max(), MaxLoad = 0, MinSpeed = TNumericLimits<double>::Max(), MaxSpeed = 0;
		int32 UnloadedSteps = 0;
		for (int32 Frame = 0; Frame < Rate * 6; ++Frame)
		{
			if (Frame % (Rate / 20) == 0)
			{
				FCatFightRodConstraintInput Input;
				Input.bRodHeld = true;
				Input.bGroupDriven = true;
				Input.GroupFriction = (Movement->bUseSeparateBrakingFriction ? Movement->BrakingFriction : Movement->GroundFriction)
					* Movement->BrakingFrictionFactor;
				Input.GroupBrakingDeceleration = Movement->GetMaxBrakingDeceleration();
				Input.RodTipWorldPosition = Rod->GetRodTipWorldTransform().GetLocation();
				Input.RodForwardWorld = Rod->GetAuthoritativeRodForwardVector();
				Input.RodTipVelocityCentimetersPerSecond = Rod->GetAuthoritativeRodTipVelocity();
				Input.CarrierVelocityCentimetersPerSecond = Movement->Velocity;
				Rod->GetRotationPredictionFromAuthority(C.FixedStepSeconds, Input.RodRotationPrediction);
				Input.CarrierTravelLimitCentimeters = Movement->GetExternalTractionTravelLimit(
					(S.FishWorldPosition - Input.RodTipWorldPosition).GetSafeNormal2D(), 20.0);
				const FTransform PoseBeforePrediction = Rod->GetActorTransform();
				const auto EffortBeforePrediction = Rod->GetAuthoritativeRotationEffortSnapshot();
				const auto Step = FCatFishingFightSimulator::Step(C, S, Input, FVector(0, -1, 0));
				if (!TestTrue(TEXT("short elevated fight solves"), Step.bSucceeded)) return false;
				if (Frame == Rate * 2)
				{
					FCatFishingRodRotationPrediction After;
					TestTrue(TEXT("held rod supplies the shared rotation prediction"), Step.Trace.bRodRotationPredicted
						&& Rod->GetRotationPredictionFromAuthority(C.FixedStepSeconds, After));
					TestTrue(TEXT("candidate tensions do not mutate the live pose or smoothing history"),
						Rod->GetActorTransform().Equals(PoseBeforePrediction)
						&& After.Input.CurrentAim.Equals(Input.RodRotationPrediction.Input.CurrentAim)
						&& After.Input.PreviousSmoothedFishPullStrengthMeters.Equals(Input.RodRotationPrediction.Input.PreviousSmoothedFishPullStrengthMeters));
					const auto& EffortAfter = Rod->GetAuthoritativeRotationEffortSnapshot();
					TestTrue(TEXT("prediction cannot accumulate or consume player effort"), EffortAfter.Epoch == EffortBeforePrediction.Epoch
						&& EffortAfter.ExertionSquaredSeconds == EffortBeforePrediction.ExertionSquaredSeconds
						&& EffortAfter.PositiveWorkRadians == EffortBeforePrediction.PositiveWorkRadians
						&& EffortAfter.IntegratedSeconds == EffortBeforePrediction.IntegratedSeconds);
				}
				const FVector Axis = (Step.ProposedFishWorldPosition - Input.RodTipWorldPosition).GetSafeNormal();
				Rod->SetCarrierConstraintFromAuthority(Axis, Step.CarrierPullAccelerationCentimetersPerSecondSquared,
					Step.CarrierTargetPullSpeedCentimetersPerSecond, Step.NormalizedTension, Step.ConstraintErrorCentimeters, true,
					Step.LineTensionNewtons / C.ForcePerStrengthNewtons * C.RodPhysicsLengthCentimeters / 100,
					C.PrimaryOperatorCatStrength, Axis, Step.CarrierBrakingDecelerationCentimetersPerSecondSquared, Step.bUseContinuousCarrierTraction);
				// 单成员无主动移动，仍按 Runner 的 Carrier -> Group 顺序发布相同受力。
				if (!TestTrue(TEXT("short elevated fight publishes its complete group motion"),
					Rod->SetGroupMotionFromAuthority(FVector::ZeroVector, FVector::ZeroVector))) return false;
				AcceptStep(S, Step);
				if (Frame >= Rate * 2)
				{
					MinLoad = FMath::Min(MinLoad, Step.LineTensionNewtons);
					MaxLoad = FMath::Max(MaxLoad, Step.LineTensionNewtons);
					UnloadedSteps += Step.LineTensionNewtons < 0.01 ? 1 : 0;
				}
			}
			Movement->PerformMovement(1.0f / Rate);
			Rod->RefreshHeldTransformFromAuthority(1.0 / Rate);
			if (Frame >= Rate * 2)
			{
				MinSpeed = FMath::Min(MinSpeed, Movement->Velocity.Size2D());
				MaxSpeed = FMath::Max(MaxSpeed, Movement->Velocity.Size2D());
			}
		}
		AddInfo(FString::Printf(TEXT("FishMassKg=%.3f FPS=%d ShortLineMinN=%.3f MaxN=%.3f UnloadedSteps=%d MinSpeedCmS=%.3f MaxSpeedCmS=%.3f"),
			FishMass, Rate, MinLoad, MaxLoad, UnloadedSteps, MinSpeed, MaxSpeed));
		TestEqual(TEXT("steady outward fight has no periodic complete unloading"), UnloadedSteps, 0);
		TestTrue(TEXT("loaded body keeps moving"), MinSpeed > 5.0);
		TestTrue(TEXT("steady load and speed settle without repeated kicks"), MaxLoad - MinLoad < 5.0 && MaxSpeed - MinSpeed < 10.0);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingJointMotionContractTest,
	"Catfishing.Unit.Fishing.Simulation.JointMotionSeparatesPositionRepairAndHonorsCollisionAndSlack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingJointMotionContractTest::RunTest(const FString& Parameters)
{
	auto C = ForceConfig();
	auto S = ForceState();
	S.CatAction = ECatFightCatAction::None;
	FCatFightRodConstraintInput Rod;
	Rod.bRodHeld = true;
	Rod.CarrierTravelLimitCentimeters = 20.0;
	const auto Moving = FCatFishingFightSimulator::Step(C, S, Rod, FVector::ForwardVector);
	TestTrue(TEXT("both bodies can accelerate under a shared finite load"), Moving.bSucceeded
		&& Moving.LineTensionNewtons > 50.0 && Moving.LineTensionNewtons < 75.0
		&& Moving.ResolvedFishVelocityCentimetersPerSecond.X > 0.0 && Moving.CarrierPullAccelerationCentimetersPerSecondSquared > 0.0);
	Rod.CarrierTravelLimitCentimeters = 0.0;
	const auto Wall = FCatFishingFightSimulator::Step(C, S, Rod, FVector::ForwardVector);
	TestEqual(TEXT("a blocked body cannot lend fictitious displacement to the fish"), Wall.ProposedFishWorldPosition.X, S.FishWorldPosition.X, 1e-6);
	TestEqual(TEXT("wall support balances outward thrust"), Wall.LineTensionNewtons, 75.0, 1e-6);
	Rod.CarrierTravelLimitCentimeters = 20.0;
	S.FishWorldPosition.X += 100.0;
	const auto Repaired = FCatFishingFightSimulator::Step(C, S, Rod, FVector::ForwardVector);
	TestTrue(TEXT("past position error is repaired toward the line"), Repaired.ProposedFishWorldPosition.X < S.FishWorldPosition.X);
	TestTrue(TEXT("position repair cannot reverse outward momentum"), Repaired.ResolvedFishVelocityCentimetersPerSecond.X > 0.0);
	TestEqual(TEXT("old position error cannot become a new force spike"), Repaired.LineTensionNewtons, Moving.LineTensionNewtons, 1e-6);
	S = ForceState(); S.CatAction = ECatFightCatAction::None;
	S.LineLengthCentimeters += 100.0;
	const auto Slack = FCatFishingFightSimulator::Step(C, S, Rod, FVector::ForwardVector);
	TestEqual(TEXT("real slack has no minimum or held-over line force"), Slack.LineTensionNewtons, 0.0);
	S = ForceState(); S.CatAction = ECatFightCatAction::Slack;
	const auto Released = FCatFishingFightSimulator::Step(C, S, Rod, FVector::ForwardVector);
	TestEqual(TEXT("free spool releases the common physical load"), Released.LineTensionNewtons, 0.0);
	Rod.CarrierTravelLimitCentimeters = std::numeric_limits<double>::quiet_NaN();
	TestFalse(TEXT("invalid collision feedback is rejected"), FCatFishingFightSimulator::Step(C, S, Rod, FVector::ForwardVector).bSucceeded);
	Rod.CarrierTravelLimitCentimeters = 20.0;
	Rod.RodRotationPrediction.bValid = true;
	Rod.RodRotationPrediction.HolderWorldPosition.X = std::numeric_limits<double>::quiet_NaN();
	TestFalse(TEXT("invalid rotation geometry cannot silently release the line"), FCatFishingFightSimulator::Step(C, S, Rod, FVector::ForwardVector).bSucceeded);
	return !HasAnyErrors();
}

#endif
