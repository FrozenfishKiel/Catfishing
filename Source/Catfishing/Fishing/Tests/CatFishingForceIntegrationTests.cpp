#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/Tests/CatPhysicalTestWorld.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/Simulation/CatFishingFightSimulator.h"
#include "Fishing/Simulation/CatFishingOperatorWorkModel.h"
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
	TestTrue(TEXT("line reaction stops outward fish velocity at the sampled endpoint"),
		Strong.ResolvedFishVelocityCentimetersPerSecond.IsNearlyZero(1e-6));
	TestTrue(TEXT("the line solver does not integrate another cat or rod endpoint"),
		Strong.Trace.ConstraintRodEndWorldPosition.Equals(Rod.RodTipWorldPosition, 1e-6));
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
	"Catfishing.Unit.Fishing.Simulation.SampledRodEndpointAndPersonalProgressHaveSeparateAuthority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingActualEndpointTest::RunTest(const FString& Parameters)
{
	const auto C = ForceConfig();
	auto S = ForceState();
	S.CatAction = ECatFightCatAction::None;
	FCatFightRodConstraintInput Rod;
	Rod.bRodHeld = true;
	Rod.bPhysicalRodEndpoint = true;
	const auto Still = FCatFishingFightSimulator::Step(C, S, Rod, FVector::ForwardVector);
	if (!TestTrue(TEXT("stationary sampled rod produces a valid fish constraint"), Still.bSucceeded)) return false;
	for (int32 I = 0; I < 200; ++I)
	{
		// A moving torso is not an extrapolated rod tip: shoulder and hand constraints can
		// absorb that motion. Only the next actual rod sample may move the fish endpoint.
		Rod.CarrierVelocityCentimetersPerSecond = FVector(I % 2 == 0 ? -600.0 : 600.0, 0.0, 0.0);
		const auto Observed = FCatFishingFightSimulator::Step(C, S, Rod, FVector::ForwardVector);
		if (!TestTrue(TEXT("body velocity observation leaves the sampled line constraint valid"), Observed.bSucceeded)) return false;
		TestTrue(TEXT("body velocity cannot create phantom rod endpoint motion"),
			Observed.Trace.ConstraintRodEndWorldPosition.Equals(Rod.RodTipWorldPosition, 1e-6));
		TestTrue(TEXT("unchanged real rod tip preserves the fish constraint"),
			Observed.ProposedFishWorldPosition.Equals(Still.ProposedFishWorldPosition, 1e-6));
		TestEqual(TEXT("body observation cannot add a second fish pull"), Observed.LineTensionNewtons, Still.LineTensionNewtons, 1e-6);
		TestTrue(TEXT("line cannot grow indefinitely while the rod tip stays blocked"),
			Observed.StraightLineDistanceCentimeters <= S.LineLengthCentimeters + 1e-6);
		AcceptStep(S, Observed);
	}
	Rod.RodTipWorldPosition.X = -20.0;
	const auto Moved = FCatFishingFightSimulator::Step(C, S, Rod, FVector::ForwardVector);
	TestTrue(TEXT("an actual rod sample changes the fish constraint"), Moved.bSucceeded
		&& Moved.ProposedFishWorldPosition.X < S.FishWorldPosition.X
		&& Moved.Trace.ConstraintRodEndWorldPosition.Equals(Rod.RodTipWorldPosition, 1e-6));
	FCatFightOperatorMovementCostInput Personal;
	Personal.MoveIntentWorld = -FVector::ForwardVector;
	Personal.MaximumMoveSpeedCentimetersPerSecond = 100.0;
	Personal.FixedStepSeconds = C.FixedStepSeconds;
	Personal.ActiveStrength = C.PrimaryOperatorCatStrength;
	FCatFightOperatorMovementCostResult BlockedCost;
	if (!TestTrue(TEXT("personal billing accepts a blocked physical body"),
		FCatFishingOperatorWorkModel::ComputeMovementStaminaDrain(Personal, BlockedCost))) return false;
	TestEqual(TEXT("blocked body has no positive movement work"), BlockedCost.ActualProgressCentimeters, 0.0);
	TestTrue(TEXT("blocked voluntary effort pays the unfulfilled intention"), BlockedCost.StaminaDrain > 0.0);
	Personal.ActualDisplacementCentimeters = FVector(-5.0, 0.0, 0.0);
	FCatFightOperatorMovementCostResult ProgressCost;
	if (!TestTrue(TEXT("personal billing accepts actual progress"),
		FCatFishingOperatorWorkModel::ComputeMovementStaminaDrain(Personal, ProgressCost))) return false;
	TestTrue(TEXT("actual forward progress reduces the same directional deficit"), ProgressCost.StaminaDrain < BlockedCost.StaminaDrain);
	Personal.MoveIntentWorld = FVector::ZeroVector;
	FCatFightOperatorMovementCostResult PassiveCost;
	if (!TestTrue(TEXT("personal billing accepts passive drag"),
		FCatFishingOperatorWorkModel::ComputeMovementStaminaDrain(Personal, PassiveCost))) return false;
	TestEqual(TEXT("passive physical displacement cannot spend voluntary movement stamina"), PassiveCost.StaminaDrain, 0.0);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingPhysicalForceSourcesTest,
	"Catfishing.Unit.Fishing.Runtime.PhysicalForceSourceReplacesClearsAndConvergesAcrossFrameRates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingPhysicalForceSourcesTest::RunTest(const FString& Parameters)
{
	TArray<double> TravelSamples;
	for (int32 Frequency : {30, 60, 120})
	{
		CatPhysicalTest::FScene Scene;
		if (!TestTrue(TEXT("real force receiver world"), Scene.Initialize(this))) return false;
		ACatCharacter* Cat = Scene.SpawnCat(FVector(0, 0, 20));
		AActor* Source = Scene.World.GetTestWorld()->SpawnActor<AActor>();
		if (!Cat || !Source) return false;
		UCatPhysicalBodyComponent* Body = Cat->GetPhysicalBodyComponent();
		// Exhausted feet can stand, but have no voluntary horizontal force to hide a duplicate load.
		Body->SetFishingMotorBudget(Scene.Floor, 0.0, 100.0);
		Scene.Step(Frequency, Frequency);
		const double StartX = Cat->GetActorLocation().X;
		for (int32 Frame = 0; Frame < Frequency; ++Frame)
		{
			Body->SetExternalForceFromAuthority(Source, FVector(1000, 0, 0)); // 10 N, replace this source.
			Scene.Step(1, Frequency);
		}
		const double Travel = Cat->GetActorLocation().X - StartX;
		TravelSamples.Add(Travel);
		TestTrue(TEXT("one constant 10 N source produces bounded real acceleration"), Body->GetVelocity().X > 120 && Body->GetVelocity().X < 300);
		Body->SetExternalForceFromAuthority(Source, FVector(-1000, 0, 0));
		Scene.Step(Frequency * 2, Frequency);
		TestTrue(TEXT("replacing the source reverses the load instead of accumulating both directions"), Body->GetVelocity().X < -100);
		Body->ClearExternalForce(Source);
		const double BeforeClearSpeed = FMath::Abs(Body->GetVelocity().X);
		Scene.Step(Frequency / 2, Frequency);
		TestTrue(TEXT("cleared source cannot keep accelerating the cat"), FMath::Abs(Body->GetVelocity().X) < BeforeClearSpeed + 5);
		Body->SetExternalForceFromAuthority(Source, FVector(-2000, 0, 0));
		Source->Destroy();
		const double BeforeDestroySpeed = FMath::Abs(Body->GetVelocity().X);
		Scene.Step(Frequency / 2, Frequency);
		TestTrue(TEXT("destroyed force owner is removed before the next force application"), FMath::Abs(Body->GetVelocity().X) < BeforeDestroySpeed + 5);
		AddInfo(FString::Printf(TEXT("Event=fishing_physical_force_source_verified Hz=%d OneSecondTravelCm=%.3f FinalVelocity=%s"),
			Frequency, Travel, *Body->GetVelocity().ToCompactString()));
	}
	TestTrue(TEXT("force APIs integrate time once at 30, 60 and 120 Hz"),
		FMath::Max3(TravelSamples[0], TravelSamples[1], TravelSamples[2])
		- FMath::Min3(TravelSamples[0], TravelSamples[1], TravelSamples[2]) < 15.0);
	return !HasAnyErrors();
}
#endif
