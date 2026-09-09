#if WITH_DEV_AUTOMATION_TESTS

#include <limits>

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"

#include "Character/CatCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Fishing/Integration/CatFishingRodAimState.h"
#include "Fishing/Simulation/CatFishingFightRunner.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "GameFramework/PlayerState.h"
#include "OnlineSubsystemTypes.h"

namespace CatFishingSlackAimTest
{
	static FCatFishingRodAimSample Sample(const int64 Sequence, const double Yaw, const double Pitch = 0.0,
		const ACatFishingRodActor* Rod = nullptr)
	{
		FCatFishingRodAimSample Result;
		Result.Sequence = Sequence;
		Result.CumulativeLookDegrees = FVector2D(Yaw, Pitch);
		if (Rod)
		{
			Result.RodActorId = Rod->GetPresentationState().RodActorId;
			Result.InputEpoch = Rod->GetCarrierConstraintState().AimInputEpoch;
		}
		return Result;
	}

	static FCatFishingRodAimSample MotionSample(const int64 Sequence, const int64 Stroke,
		const bool bActive, const double CumulativeYaw, const double StrokeStartYaw, const ACatFishingRodActor* Rod)
	{
		auto Result = Sample(Sequence, CumulativeYaw, 0.0, Rod);
		Result.bMouseActive = bActive;
		Result.MouseStrokeSequence = Stroke;
		Result.MouseStrokeStartLookDegrees = FVector2D(StrokeStartYaw, 0.0);
		return Result;
	}

	struct FHeldRodFixture
	{
		FTestWorldWrapper WorldWrapper;
		UWorld* World = nullptr;
		ACatfishingPlayerController* Controller = nullptr;
		ACatfishingPlayerState* Player = nullptr;
		ACatCharacter* Character = nullptr;
		ACatFishingRodActor* Rod = nullptr;
		UCatFishingService* Fishing = nullptr;

		bool Create(FAutomationTestBase& Test)
		{
			if (!Test.TestTrue(TEXT("create slack aim game world"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
			WorldWrapper.ForwardErrorMessages(&Test);
			World = WorldWrapper.GetTestWorld();
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Controller = World->SpawnActor<ACatfishingPlayerController>();
			Player = World->SpawnActor<ACatfishingPlayerState>();
			Character = World->SpawnActor<ACatCharacter>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
			Rod = World->SpawnActor<ACatFishingRodActor>();
			Fishing = World->GetSubsystem<UCatFishingService>();
			if (!Test.TestTrue(TEXT("spawn real controller, character, rod and service"),
				Controller && Player && Character && Rod && Fishing)) return false;
			Controller->PlayerState = Player;
			Character->SetPlayerState(Player);
			Controller->Possess(Character);
			Controller->SetControlRotation(FRotator::ZeroRotator);
			return Test.TestTrue(TEXT("initialize held rod identity"), Rod->InitializeAuthoritativeIdentity(
				FGuid::NewGuid(), FGuid::NewGuid(), TEXT("SlackAimRod"), TEXT("Skin"), Player, Player, true, false))
				&& Test.TestTrue(TEXT("register held rod through production lookup"), Fishing->RegisterDeployedRod(Player, Rod))
				&& Test.TestTrue(TEXT("initialize actual held pose"), Rod->RefreshHeldTransformFromAuthority())
				&& Test.TestTrue(TEXT("start loaded fight"), Rod->SetCarrierConstraintFromAuthority(
					FVector::ForwardVector, 0.0, 0.0, 1.0, 0.0, true, 100.0, 50.0));
		}

		bool LoadAgainstOldIntent(FAutomationTestBase& Test)
		{
			for (int32 Frame = 0; Frame < 360; ++Frame)
			{
				if (!Rod->AcceptHeldAimSampleFromAuthority(Player,
					MotionSample(Frame + 1, 1, true, 120.0 + Frame * 0.01, 0.0, Rod))) return false;
				if (!Rod->RefreshHeldTransformFromAuthority(1.0 / 60.0))
				{
					Test.AddError(TEXT("loaded production rod pose must integrate"));
					return false;
				}
			}
			return Test.TestTrue(TEXT("fish holds actual rod far short of the continuously moving mouse target"),
				FMath::Abs(FMath::FindDeltaAngleDegrees(Rod->GetGripWorldTransform().Rotator().Yaw, 120.0)) > 60.0);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingSlackAimSampleOrderTest,
	"Catfishing.Unit.Fishing.SlackAim.MouseStrokesStopAndExpireWithoutRevivingOldTargets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingSlackAimSampleOrderTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingSlackAimTest;
	const auto Motion = [](const int64 Sequence, const double Yaw, const double Pitch = 100.0,
		const int64 Stroke = 1, const double StartYaw = 690.0, const double StartPitch = 90.0)
	{
		auto Result = Sample(Sequence, Yaw, Pitch);
		Result.bMouseActive = true;
		Result.MouseStrokeSequence = Stroke;
		Result.MouseStrokeStartLookDegrees = FVector2D(StartYaw, StartPitch);
		return Result;
	};
	FCatFishingRodAimState InOrder;
	FCatFishingRodAimState FutureFirst;
	const auto Press = Motion(10, 700.0);
	const auto NewLook = Motion(12, 705.0, 102.0);
	const FRotator Actual(10.0, 20.0, 0.0);
	TestTrue(TEXT("press rebases ordered stream to actual rod"), InOrder.Rebase(Press, Actual, -60.0, 60.0, 1.0));
	TestTrue(TEXT("new look arrives after press"), InOrder.AcceptSample(NewLook, Actual, -60.0, 60.0, 1.01));
	TestTrue(TEXT("future look can arrive ahead of reliable press"), FutureFirst.AcceptSample(NewLook, Actual, -60.0, 60.0, 1.01));
	TestTrue(TEXT("ordinary first movement anchors an actual rod target without a right press"), FutureFirst.IsRebased());
	TestEqual(TEXT("a missing first stroke packet retains this stroke's fifteen yaw degrees"), FutureFirst.GetRequestedAim().Yaw, 35.0);
	TestTrue(TEXT("late press retains genuinely post-press look"), FutureFirst.Rebase(Press, Actual, -60.0, 60.0, 1.02));
	TestTrue(TEXT("both network arrival orders produce the same target"),
		FutureFirst.GetRequestedAim().Equals(InOrder.GetRequestedAim(), 1e-8));
	TestEqual(TEXT("only five new yaw degrees survive the press"), FutureFirst.GetRequestedAim().Yaw, 25.0);
	TestEqual(TEXT("only two new pitch degrees survive the press"), FutureFirst.GetRequestedAim().Pitch, 12.0);
	TestFalse(TEXT("an older movement packet cannot restore pre-press aim"),
		FutureFirst.AcceptSample(Motion(9, 690.0, 90.0), Actual, -60.0, 60.0, 1.02));
	TestFalse(TEXT("same movement packet cannot count twice"), FutureFirst.AcceptSample(NewLook, Actual, -60.0, 60.0, 1.02));
	TestFalse(TEXT("replayed press cannot discard newer input"), FutureFirst.Rebase(Press, Actual, -60.0, 60.0, 1.02));
	TestEqual(TEXT("rejections leave requested yaw intact"), FutureFirst.GetRequestedAim().Yaw, 25.0);
	TestTrue(TEXT("an active cumulative snapshot repairs a missing movement packet"),
		FutureFirst.AcceptSample(Motion(14, 712.0, 102.0), Actual, -60.0, 60.0, 1.03));
	TestEqual(TEXT("lost movement within an active stroke is recovered exactly once"), FutureFirst.GetRequestedAim().Yaw, 32.0);
	auto Stop = Motion(15, 720.0, 102.0);
	Stop.bMouseActive = false;
	const FRotator StoppedActual(9.0, 23.0, 0.0);
	TestTrue(TEXT("stop accepts the complete snapshot but discards its outstanding movement"),
		FutureFirst.AcceptSample(Stop, StoppedActual, -60.0, 60.0, 1.04));
	TestFalse(TEXT("stop immediately removes active mouse effort"), FutureFirst.IsMouseActive(1.04));
	TestTrue(TEXT("stopped target equals actual rod instead of replaying the lost final delta"),
		FutureFirst.GetRequestedAim().Equals(StoppedActual, 1e-8));
	TestTrue(TEXT("late right press is a valid no-op after newer stop"),
		FutureFirst.Rebase(Motion(14, 712.0, 102.0), FRotator(0.0, 50.0, 0.0), -60.0, 60.0, 1.05));
	TestFalse(TEXT("late press cannot reactivate stopped input"), FutureFirst.IsMouseActive(1.05));
	TestEqual(TEXT("late press cannot replace the stopped target"), FutureFirst.GetRequestedAim().Yaw, 23.0);

	TestTrue(TEXT("new stroke after a lost first packet starts at the current actual rod"),
		FutureFirst.AcceptSample(Motion(17, 725.0, 102.0, 2, 720.0, 102.0),
			FRotator(9.0, 24.0, 0.0), -60.0, 60.0, 1.06));
	TestEqual(TEXT("new stroke adds five degrees and never old target error"), FutureFirst.GetRequestedAim().Yaw, 29.0);
	Stop.Sequence = 16;
	TestFalse(TEXT("delayed old stop cannot cancel newer resumed movement"),
		FutureFirst.AcceptSample(Stop, StoppedActual, -60.0, 60.0, 1.07));
	TestTrue(TEXT("late press from previous stroke remains a valid no-op"),
		FutureFirst.Rebase(Motion(16, 720.0, 102.0), Actual, -60.0, 60.0, 1.07));
	TestEqual(TEXT("old stroke press cannot move the new target"), FutureFirst.GetRequestedAim().Yaw, 29.0);
	TestTrue(TEXT("a later packet continues from the fixed stroke anchor"),
		FutureFirst.AcceptSample(Motion(19, 730.0, 102.0, 2, 720.0, 102.0),
			FRotator(9.0, 25.0, 0.0), -60.0, 60.0, 1.08));
	TestEqual(TEXT("physical rod motion does not repeatedly add to an ongoing stroke"), FutureFirst.GetRequestedAim().Yaw, 34.0);
	TestTrue(TEXT("same stroke late press retains only movement after that press"),
		FutureFirst.Rebase(Motion(18, 728.0, 102.0, 2, 720.0, 102.0),
			FRotator(9.0, 26.0, 0.0), -60.0, 60.0, 1.09));
	TestEqual(TEXT("same stroke post-press movement is two degrees"), FutureFirst.GetRequestedAim().Yaw, 28.0);
	TestTrue(TEXT("input remains active immediately before the lease expires"), FutureFirst.IsMouseActive(1.229));
	TestFalse(TEXT("a right press does not extend the last movement lease"), FutureFirst.IsMouseActive(1.24));
	TestTrue(TEXT("missing stop packet is retired by the authority timeout"),
		FutureFirst.ExpireInput(1.24, FRotator(9.0, 27.0, 0.0)));
	TestEqual(TEXT("timeout discards requested angle in favor of actual"), FutureFirst.GetRequestedAim().Yaw, 27.0);
	TestFalse(TEXT("timeout transition only fires once"), FutureFirst.ExpireInput(1.25, Actual));
	TestTrue(TEXT("same stroke first packet after outage establishes a fresh baseline"),
		FutureFirst.AcceptSample(Motion(22, 750.0, 102.0, 2, 720.0, 102.0),
			FRotator(9.0, 30.0, 0.0), -60.0, 60.0, 1.26));
	TestEqual(TEXT("outage backlog does not turn into an immediate target jump"), FutureFirst.GetRequestedAim().Yaw, 30.0);
	TestTrue(TEXT("a delayed press inside the outage is consumed without replaying discarded movement"),
		FutureFirst.Rebase(Motion(21, 740.0, 102.0, 2, 720.0, 102.0), Actual, -60.0, 60.0, 1.27));
	TestEqual(TEXT("late press cannot resurrect the expired ten degrees"), FutureFirst.GetRequestedAim().Yaw, 30.0);
	TestTrue(TEXT("fresh movement after resumed baseline continues normally"),
		FutureFirst.AcceptSample(Motion(23, 753.0, 102.0, 2, 720.0, 102.0), Actual, -60.0, 60.0, 1.28));
	TestEqual(TEXT("only fresh three degrees survive resumption"), FutureFirst.GetRequestedAim().Yaw, 33.0);
	TestTrue(TEXT("new stroke after another timeout retains its own first movement"),
		FutureFirst.AcceptSample(Motion(25, 755.0, 102.0, 3, 753.0, 102.0),
			FRotator(9.0, 31.0, 0.0), -60.0, 60.0, 1.5));
	TestEqual(TEXT("new stroke does not lose its first two degrees"), FutureFirst.GetRequestedAim().Yaw, 33.0);
	TestTrue(TEXT("a newer right-press packet from an expired stroke is still a valid action"),
		FutureFirst.Rebase(Motion(26, 758.0, 102.0, 3, 753.0, 102.0),
			FRotator(9.0, 32.0, 0.0), -60.0, 60.0, 1.7));
	TestFalse(TEXT("a delayed same-stroke right press never starts active torque again"), FutureFirst.IsMouseActive(1.7));
	TestEqual(TEXT("expired right press only establishes the actual baseline"), FutureFirst.GetRequestedAim().Yaw, 32.0);
	FutureFirst.Reset();
	TestFalse(TEXT("lifecycle reset removes rebased input mode"), FutureFirst.IsRebased());
	TestFalse(TEXT("lifecycle reset removes mouse activity"), FutureFirst.IsMouseActive(1.5));
	TestEqual(TEXT("lifecycle reset discards old packet sequence"), FutureFirst.GetLastSequence(), int64{0});
	TestTrue(TEXT("idle sample with no previous mouse stroke is valid after lifecycle reset"),
		FutureFirst.AcceptSample(Sample(30, 755.0, 102.0), Actual, -60.0, 60.0, 1.6));
	TestFalse(TEXT("an idle sample never fabricates movement"), FutureFirst.IsMouseActive(1.6));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingSlackAimAngularBoundaryTest,
	"Catfishing.Unit.Fishing.SlackAim.YawWrapAndPitchLimitAllowImmediateReverseInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingSlackAimAngularBoundaryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingSlackAimTest;
	const auto Motion = [](const int64 Sequence, const double Yaw, const double Pitch,
		const double StartYaw = 1000.0, const double StartPitch = 2000.0)
	{
		auto Result = Sample(Sequence, Yaw, Pitch);
		Result.bMouseActive = true;
		Result.MouseStrokeSequence = 1;
		Result.MouseStrokeStartLookDegrees = FVector2D(StartYaw, StartPitch);
		return Result;
	};
	FCatFishingRodAimState State;
	const FRotator Actual(59.0, 179.0, 23.0);
	TestTrue(TEXT("rebase near positive yaw and pitch limits"),
		State.Rebase(Motion(1, 1000.0, 2000.0), Actual, -60.0, 60.0, 0.0));
	TestTrue(TEXT("look crosses positive yaw boundary and reaches pitch limit"),
		State.AcceptSample(Motion(2, 1005.0, 2001.0), Actual, -60.0, 60.0, 0.01));
	TestEqual(TEXT("yaw crosses plus 180 without a full turn"), State.GetRequestedAim().Yaw, -176.0);
	TestEqual(TEXT("pitch is clamped at the existing body limit"), State.GetRequestedAim().Pitch, 60.0);
	TestTrue(TEXT("reverse look is accepted immediately"), State.AcceptSample(Motion(3, 1000.0, 2000.0), Actual, -60.0, 60.0, 0.02));
	TestEqual(TEXT("reverse yaw crosses minus 180 continuously"), State.GetRequestedAim().Yaw, 179.0);
	TestEqual(TEXT("one degree reverse escapes upper limit without paying off hidden input"), State.GetRequestedAim().Pitch, 59.0);
	// The sender filters excess raw pitch per frame; snapshots contain only the effective movement.
	TestTrue(TEXT("effective movement reaches lower limit"), State.AcceptSample(Motion(4, 1000.0, 1881.0), Actual, -60.0, 60.0, 0.03));
	TestEqual(TEXT("lower pitch clamps"), State.GetRequestedAim().Pitch, -60.0);
	TestTrue(TEXT("reverse upwards immediately"), State.AcceptSample(Motion(5, 1000.0, 1882.0), Actual, -60.0, 60.0, 0.04));
	TestEqual(TEXT("one degree reverse escapes lower limit"), State.GetRequestedAim().Pitch, -59.0);
	TestEqual(TEXT("input never introduces roll"), State.GetRequestedAim().Roll, 0.0);
	FCatFishingRodAimState EveryPacket;
	FCatFishingRodAimState Coalesced;
	const FRotator HighPitchActual(65.0, 0.0, 0.0);
	EveryPacket.Rebase(Motion(1, 0.0, 0.0, 0.0, 0.0), HighPitchActual, -35.0, 70.0, 0.0);
	Coalesced.Rebase(Motion(1, 0.0, 0.0, 0.0, 0.0), HighPitchActual, -35.0, 70.0, 0.0);
	EveryPacket.AcceptSample(Motion(2, 0.0, 5.0, 0.0, 0.0), HighPitchActual, -35.0, 70.0, 0.01);
	EveryPacket.AcceptSample(Motion(3, 0.0, -5.0, 0.0, 0.0), HighPitchActual, -35.0, 70.0, 0.02);
	Coalesced.AcceptSample(Motion(3, 0.0, -5.0, 0.0, 0.0), HighPitchActual, -35.0, 70.0, 0.02);
	TestEqual(TEXT("filtered plus ten/minus ten raw look retains the reverse after clipping"), EveryPacket.GetRequestedAim().Pitch, 60.0);
	TestTrue(TEXT("dropping the pitch-limit packet cannot change the final requested aim"),
		Coalesced.GetRequestedAim().Equals(EveryPacket.GetRequestedAim(), 1e-8));
	const FRotator BeforeInvalid = State.GetRequestedAim();
	auto Invalid = Motion(6, 1001.0, 1882.0);
	Invalid.MouseStrokeSequence = 0;
	TestFalse(TEXT("active sample requires a positive stroke identifier"), State.AcceptSample(Invalid, Actual, -60.0, 60.0, 0.05));
	Invalid.MouseStrokeSequence = 1;
	Invalid.MouseStrokeStartLookDegrees.X = std::numeric_limits<double>::infinity();
	TestFalse(TEXT("nonfinite stroke baseline is rejected"), State.AcceptSample(Invalid, Actual, -60.0, 60.0, 0.05));
	TestFalse(TEXT("negative input time is rejected"), State.AcceptSample(Motion(6, 1001.0, 1882.0), Actual, -60.0, 60.0, -1.0));
	TestFalse(TEXT("backward input clock cannot extend an existing stroke"), State.AcceptSample(Motion(6, 1001.0, 1882.0), Actual, -60.0, 60.0, 0.01));
	TestFalse(TEXT("reversed pitch bounds are rejected"), State.AcceptSample(Motion(6, 1001.0, 1882.0), Actual, 60.0, -60.0, 0.05));
	TestEqual(TEXT("rejected input never consumes its sequence"), State.GetLastSequence(), int64{5});
	TestTrue(TEXT("rejected input cannot modify the existing target"), State.GetRequestedAim().Equals(BeforeInvalid, 1e-8));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingSlackAimRodContinuityTest,
	"Catfishing.Unit.Fishing.SlackAim.LoadedRodRebasesWithoutPoseLoadOrEffortReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingSlackAimRodContinuityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingSlackAimTest;
	FHeldRodFixture Fixture;
	if (!Fixture.Create(*this) || !Fixture.LoadAgainstOldIntent(*this)) return false;
	ACatFishingRodActor* Rod = Fixture.Rod;
	// Press while the real rod is moving, so clearing angular velocity cannot hide behind equilibrium.
	TestTrue(TEXT("ongoing stroke reverses its actual mouse input"),
		Rod->AcceptHeldAimSampleFromAuthority(Fixture.Player, MotionSample(1000, 1, true, 0.0, 0.0, Rod)));
	if (!TestTrue(TEXT("loaded rod starts turning back before the right-button edge"),
		Rod->RefreshHeldTransformFromAuthority(1.0 / 60.0))) return false;
	FCatFishingRodRotationPrediction Before;
	if (!TestTrue(TEXT("read production loaded rotation input"), Rod->GetRotationPredictionFromAuthority(0.0, Before))) return false;
	TestTrue(TEXT("rebase fixture has actual angular velocity to preserve"),
		!Before.Input.PreviousAngularVelocityRadiansPerSecond.IsNearlyZero());
	const FTransform PoseBefore = Rod->GetActorTransform();
	const auto EffortBefore = Rod->GetAuthoritativeRotationEffortSnapshot();
	const auto Press = MotionSample(1001, 1, false, 0.0, 0.0, Rod);
	TestTrue(TEXT("loaded effort and fish smoothing are actually present"),
		EffortBefore.ExertionSquaredSeconds > 0.0 && !Before.Input.PreviousSmoothedFishPullStrengthMeters.IsNearlyZero());
	TestTrue(TEXT("holder can rebase current fight"), Rod->CanRebaseHeldAimFromAuthority(Fixture.Player, Press));
	Rod->RebaseHeldAimFromAuthority(Fixture.Player, Press, FGuid::NewGuid(), 1);
	FCatFishingRodRotationPrediction Rebased;
	TestTrue(TEXT("rebased prediction is available"), Rod->GetRotationPredictionFromAuthority(0.0, Rebased));
	TestTrue(TEXT("rebase target equals the actual authoritative grip aim"), Rebased.Input.RequestedAim.Equals(Before.Input.CurrentAim, 1e-8));
	TestTrue(TEXT("press cannot teleport rod or grip"), Rod->GetActorTransform().Equals(PoseBefore, 1e-8));
	TestTrue(TEXT("press retains the smoothed fish load"),
		Rebased.Input.PreviousSmoothedFishPullStrengthMeters.Equals(Before.Input.PreviousSmoothedFishPullStrengthMeters, 1e-8));
	TestTrue(TEXT("right-button rebase preserves ongoing angular velocity"),
		Rebased.Input.PreviousAngularVelocityRadiansPerSecond.Equals(Before.Input.PreviousAngularVelocityRadiansPerSecond, 1e-8));
	TestEqual(TEXT("press does not lower authoritative fish torque"), Rebased.Input.MaximumFishTorque, Before.Input.MaximumFishTorque);
	TestEqual(TEXT("press retains effort epoch"), Rod->GetAuthoritativeRotationEffortSnapshot().Epoch, EffortBefore.Epoch);
	TestFalse(TEXT("right click without ongoing mouse movement does not apply cat turning torque"), Rebased.Input.bCatDriveActive);
	TestEqual(TEXT("press cannot erase already accumulated effort"),
		Rod->GetAuthoritativeRotationEffortSnapshot().ExertionSquaredSeconds, EffortBefore.ExertionSquaredSeconds);
	// CMC/control packets may keep writing the old hidden target after the reliable right-click RPC.
	Fixture.Controller->SetControlRotation(FRotator(45.0, 150.0, 0.0));
	FCatFishingRodRotationPrediction AfterOldControl;
	Rod->GetRotationPredictionFromAuthority(0.0, AfterOldControl);
	TestTrue(TEXT("late old ControlRotation cannot reinstate hidden pitch or yaw"),
		AfterOldControl.Input.RequestedAim.Equals(Rebased.Input.RequestedAim, 1e-8));
	TestTrue(TEXT("genuinely new mouse stroke is accepted"),
		Rod->AcceptHeldAimSampleFromAuthority(Fixture.Player, MotionSample(1002, 2, true, 5.0, 0.0, Rod)));
	FCatFishingRodRotationPrediction NewInput;
	Rod->GetRotationPredictionFromAuthority(0.0, NewInput);
	TestEqual(TEXT("new five-degree mouse input changes production target by five degrees"),
		FMath::FindDeltaAngleDegrees(Rebased.Input.RequestedAim.Yaw, NewInput.Input.RequestedAim.Yaw), 5.0, 1e-7);
	TestTrue(TEXT("new stroke explicitly activates cat drive"), NewInput.Input.bCatDriveActive);
	TestFalse(TEXT("delayed pre-press packet is discarded"),
		Rod->AcceptHeldAimSampleFromAuthority(Fixture.Player, MotionSample(999, 1, true, 90.0, 0.0, Rod)));
	TestTrue(TEXT("mouse stop is accepted independently of right-button state"),
		Rod->AcceptHeldAimSampleFromAuthority(Fixture.Player, MotionSample(1003, 2, false, 5.0, 0.0, Rod)));
	FCatFishingRodRotationPrediction Stopped;
	Rod->GetRotationPredictionFromAuthority(0.0, Stopped);
	TestFalse(TEXT("mouse stop immediately removes active turning torque"), Stopped.Input.bCatDriveActive);
	TestTrue(TEXT("mouse stop immediately discards unfulfilled turn target"), Stopped.Input.RequestedAim.Equals(Stopped.Input.CurrentAim, 1e-8));
	TestTrue(TEXT("mouse stop preserves existing angular velocity"),
		Stopped.Input.PreviousAngularVelocityRadiansPerSecond.Equals(NewInput.Input.PreviousAngularVelocityRadiansPerSecond, 1e-8));
	TestTrue(TEXT("unloading retains the same fight"), Rod->SetCarrierConstraintFromAuthority(
		FVector::ForwardVector, 0.0, 0.0, 0.0, 0.0, true, 0.0, 50.0));
	FCatFishingRodRotationPrediction Unloaded;
	Rod->GetRotationPredictionFromAuthority(0.0, Unloaded);
	TestTrue(TEXT("unload publication does not erase smoothed residual force"),
		Unloaded.Input.PreviousSmoothedFishPullStrengthMeters.Equals(Before.Input.PreviousSmoothedFishPullStrengthMeters, 1e-8));
	TestTrue(TEXT("load publication preserves angular velocity until real integration"),
		Unloaded.Input.PreviousAngularVelocityRadiansPerSecond.Equals(Before.Input.PreviousAngularVelocityRadiansPerSecond, 1e-8));
	const auto BeforePassive = Rod->GetAuthoritativeRotationEffortSnapshot();
	for (int32 Frame = 0; Frame < 240; ++Frame)
	{
		if (!Rod->RefreshHeldTransformFromAuthority(1.0 / 60.0)) return false;
	}
	TestEqual(TEXT("inertia and residual fish motion after stopping cannot charge cat support effort"),
		Rod->GetAuthoritativeRotationEffortSnapshot().ExertionSquaredSeconds, BeforePassive.ExertionSquaredSeconds);
	TestEqual(TEXT("inertia and residual fish motion after stopping cannot charge cat positive work"),
		Rod->GetAuthoritativeRotationEffortSnapshot().PositiveWorkRadians, BeforePassive.PositiveWorkRadians);
	FCatFishingRodRotationPrediction BeforeNextStrokePrediction;
	Rod->GetRotationPredictionFromAuthority(0.0, BeforeNextStrokePrediction);
	const FRotator BeforeNextStroke = BeforeNextStrokePrediction.Input.CurrentAim;
	TestTrue(TEXT("next mouse stroke begins after passive motion has changed actual pose"),
		Rod->AcceptHeldAimSampleFromAuthority(Fixture.Player, MotionSample(1004, 3, true, 8.0, 5.0, Rod)));
	FCatFishingRodRotationPrediction NextStroke;
	Rod->GetRotationPredictionFromAuthority(0.0, NextStroke);
	TestEqual(TEXT("next stroke applies only its three degrees from current actual pose"),
		FMath::FindDeltaAngleDegrees(BeforeNextStroke.Yaw, NextStroke.Input.RequestedAim.Yaw), 3.0, 1e-7);
	TestTrue(TEXT("next stroke collects new actual rotation work"), Rod->RefreshHeldTransformFromAuthority(1.0 / 60.0));
	TestTrue(TEXT("new active work is billed after the inactive interval"),
		Rod->GetAuthoritativeRotationEffortSnapshot().PositiveWorkRadians > BeforePassive.PositiveWorkRadians);
	// Advance only the authority clock: no Controller tick is allowed to send a normal stop for this timeout case.
	Fixture.World->TimeSeconds += 0.16;
	FCatFishingRodRotationPrediction TimedOut;
	Rod->GetRotationPredictionFromAuthority(0.0, TimedOut);
	TestFalse(TEXT("missing samples for more than 0.15 seconds remove cat drive"), TimedOut.Input.bCatDriveActive);
	TestTrue(TEXT("authority timeout exposes current pose instead of the old target"), TimedOut.Input.RequestedAim.Equals(TimedOut.Input.CurrentAim, 1e-8));
	const auto OldFightSample = MotionSample(5000, 4, true, 200.0, 8.0, Rod);
	Fixture.Controller->SetControlRotation(FRotator(0.0, 70.0, 0.0));
	Rod->ClearCarrierConstraintFromAuthority();
	TestTrue(TEXT("exit restores ordinary controller-based aim"), Rod->RefreshHeldTransformFromAuthority());
	TestEqual(TEXT("non-fight grip follows current control yaw"), Rod->GetGripWorldTransform().Rotator().Yaw, 70.0, 1e-7);
	TestTrue(TEXT("next fight starts"), Rod->SetCarrierConstraintFromAuthority(
		FVector::ForwardVector, 0.0, 0.0, 1.0, 0.0, true, 100.0, 50.0));
	TestTrue(TEXT("new fight gets a new aim input epoch"), Rod->GetCarrierConstraintState().AimInputEpoch != Press.InputEpoch);
	TestFalse(TEXT("previous fight's late movement cannot target the new fight"), Rod->AcceptHeldAimSampleFromAuthority(Fixture.Player, OldFightSample));
	TestFalse(TEXT("previous fight's late press cannot rebase the new fight"), Rod->CanRebaseHeldAimFromAuthority(Fixture.Player, OldFightSample));
	TestTrue(TEXT("new fight accepts samples in its own epoch"), Rod->AcceptHeldAimSampleFromAuthority(Fixture.Player, MotionSample(5001, 5, true, 201.0, 200.0, Rod)));
	APlayerState* NextPlayer = Fixture.World->SpawnActor<APlayerState>();
	if (!TestNotNull(TEXT("next holder identity"), NextPlayer)) return false;
	TestTrue(TEXT("holder transfer clears old aim ownership"), Rod->SetOperatorFromAuthority(NextPlayer, Rod->GetPresentationState().RodActorRevision));
	TestFalse(TEXT("previous holder cannot submit aim after transfer"),
		Rod->AcceptHeldAimSampleFromAuthority(Fixture.Player, MotionSample(6000, 6, true, 300.0, 201.0, Rod)));
	TestEqual(TEXT("transfer does not carry previous holder effort"), Rod->GetAuthoritativeRotationEffortSnapshot().ExertionSquaredSeconds, 0.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingSlackAimCommandRoutingTest,
	"Catfishing.Unit.Fishing.SlackAim.CommandSessionRunnerPreserveEdgesAndSampleOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingSlackAimCommandRoutingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingSlackAimTest;
	FHeldRodFixture Fixture;
	if (!Fixture.Create(*this)) return false;
	FURL URL;
	URL.AddOption(TEXT("game=/Script/Catfishing.CatfishingGameModeBase"));
	if (!TestTrue(TEXT("create real authority game mode"), Fixture.World->SetGameMode(URL))) return false;
	ACatfishingGameModeBase* GameMode = Fixture.World->GetAuthGameMode<ACatfishingGameModeBase>();
	if (!TestNotNull(TEXT("project game mode"), GameMode)) return false;
	const FUniqueNetIdRef UniqueId = FUniqueNetIdString::Create(TEXT("SlackAimFisher"), FName(TEXT("CAT_TEST")));
	Fixture.Player->SetUniqueId(FUniqueNetIdRepl(UniqueId));
	ACatfishingGameModeBase::FAdmissionRecord Admission;
	Admission.Phase = ACatfishingGameModeBase::EAdmissionPhase::Active;
	Admission.Controller = Fixture.Controller;
	GameMode->AdmissionRecords.Add(ACatfishingGameModeBase::MakeStableNetIdKey(Fixture.Player->GetUniqueId()), Admission);
	GameMode->bRunCommandsOpen = true;
	GameMode->RunPublicState.Phase.Phase = ECatRunPhase::DayActive;
	GameMode->RunPublicState.Phase.bFishingAllowed = true;
	TStrongObjectPtr<ULocalPlayer> LocalPlayer(NewObject<ULocalPlayer>(GEngine));
	Fixture.Controller->SetPlayer(LocalPlayer.Get());
	if (!TestTrue(TEXT("fixture passes real local and fishing gates"),
		Fixture.Controller->IsLocalController() && Fixture.Controller->CanForwardFishingCommand())) return false;

	ACatFishingSession* Session = Fixture.World->SpawnActor<ACatFishingSession>();
	if (!TestNotNull(TEXT("real session"), Session)) return false;
	UCatFishingFightRunner* Runner = NewObject<UCatFishingFightRunner>(Session);
	Session->Snapshot.FishingSessionId = FGuid::NewGuid();
	Session->Snapshot.CastAttemptId = FGuid::NewGuid();
	Session->Snapshot.Phase = ECatFishingPhase::HookedFight;
	Session->Snapshot.FisherPlayerState = Fixture.Player;
	Session->Snapshot.RodActor = Fixture.Rod;
	Session->FightRunner = Runner;
	Fixture.Fishing->Sessions.Add(Session->Snapshot.FishingSessionId, Session);
	// Only seed the running input fixture; all following transitions use production command/Session/Runner code.
	Runner->bInitialized = true;
	Runner->bRunning = true;
	Runner->State.bOperatorPresent = true;
	Runner->Config.MaximumLineLengthCentimeters = 1000.0;
	Runner->State.LineLengthCentimeters = 500.0;
	Runner->Session = Session;
	Runner->RodActor = Fixture.Rod;
	FCatFightParticipantRuntime Participant;
	Participant.PlayerState = Fixture.Player;
	Participant.Character = Fixture.Character;
	Participant.AbilitySystem = Fixture.Character->GetCatAbilitySystemComponent();
	Participant.bPrimary = true;
	Runner->Participants.Add(TWeakObjectPtr<APlayerState>(Fixture.Player), Participant);
	UCatFishingCommandComponent* Commands = Fixture.Controller->GetFishingCommandComponent();
	// Build the resisted pose through this controller's own input domain and sequence numbers.
	Commands->UpdateLocalRodAimInput(1.0 / 60.0, FRotator(0.0, 120.0, 0.0));
	for (int32 Frame = 0; Frame < 360; ++Frame)
	{
		Commands->UpdateLocalRodAimInput(1.0 / 60.0, FRotator(0.0, 0.01, 0.0));
		if (!Fixture.Rod->RefreshHeldTransformFromAuthority(1.0 / 60.0)) return false;
	}
	FCatFishingCommandResult Result;
	const auto PrimaryPress = Commands->SubmitPrimaryPressed();
	TestTrue(TEXT("left press routes to actual active session"), Commands->TryGetResult(PrimaryPress.RequestId, Result) && Result.bCommitted);
	TestEqual(TEXT("runner begins reeling"), Runner->GetCatAction(), ECatFightCatAction::Pull);
	FCatFishingRodRotationPrediction BeforePress;
	Fixture.Rod->GetRotationPredictionFromAuthority(0.0, BeforePress);
	const auto SlackPress = Commands->SubmitSlackPressed();
	TestTrue(TEXT("right press commits through real route"), Commands->TryGetResult(SlackPress.RequestId, Result) && Result.bCommitted);
	TestTrue(TEXT("press packet identifies the current rod and fight input epoch"),
		SlackPress.RodAimSample.RodActorId == Fixture.Rod->GetPresentationState().RodActorId
		&& SlackPress.RodAimSample.InputEpoch == Fixture.Rod->GetCarrierConstraintState().AimInputEpoch);
	TestEqual(TEXT("right press takes priority over held left button"), Runner->GetCatAction(), ECatFightCatAction::Slack);
	TestTrue(TEXT("session publishes slack and pauses reel"), Session->GetSnapshot().bSlacking && !Session->GetSnapshot().bReeling);
	FCatFishingRodRotationPrediction Rebased;
	Fixture.Rod->GetRotationPredictionFromAuthority(0.0, Rebased);
	TestTrue(TEXT("production right press rebases to actual angle"), Rebased.Input.RequestedAim.Equals(BeforePress.Input.CurrentAim, 1e-8));

	// Model the engine order: right-button edge in PostProcessInput, then this frame's RotationInput.
	Fixture.Controller->RotationInput = FRotator(0.0, 5.0, 0.0);
	Fixture.Controller->UpdateRotation(1.0f / 60.0f);
	Fixture.Controller->RotationInput = FRotator::ZeroRotator;
	FCatFishingRodRotationPrediction SameFrame;
	Fixture.Rod->GetRotationPredictionFromAuthority(0.0, SameFrame);
	TestEqual(TEXT("right press plus UpdateRotation count the same frame's mouse movement once"),
		FMath::FindDeltaAngleDegrees(Rebased.Input.RequestedAim.Yaw, SameFrame.Input.RequestedAim.Yaw), 5.0, 1e-7);
	Fixture.Controller->UpdateRotation(1.0f / 60.0f);
	FCatFishingRodRotationPrediction Stationary;
	Fixture.Rod->GetRotationPredictionFromAuthority(0.0, Stationary);
	TestFalse(TEXT("the first stationary input frame removes active turning torque"), Stationary.Input.bCatDriveActive);
	TestTrue(TEXT("stationary production sample clears the unfulfilled mouse target"),
		Stationary.Input.RequestedAim.Equals(Stationary.Input.CurrentAim, 1e-8));
	TestEqual(TEXT("mouse stop does not release physically held reel or slack buttons"), Runner->GetCatAction(), ECatFightCatAction::Slack);

	const auto RepeatedPress = Commands->SubmitSlackPressed();
	TestTrue(TEXT("higher-sequence repeated right press is a held-state update"),
		Commands->TryGetResult(RepeatedPress.RequestId, Result) && Result.bCommitted
		&& RepeatedPress.InputSequence > SlackPress.InputSequence);
	FCatFishingRodRotationPrediction AfterRepeat;
	Fixture.Rod->GetRotationPredictionFromAuthority(0.0, AfterRepeat);
	TestTrue(TEXT("a repeated pressed notification cannot restore stopped mouse aim"),
		!AfterRepeat.Input.bCatDriveActive && AfterRepeat.Input.RequestedAim.Equals(Stationary.Input.RequestedAim, 1e-8));
	const auto Release = Commands->SubmitSlackReleased();
	TestTrue(TEXT("right release commits"), Commands->TryGetResult(Release.RequestId, Result) && Result.bCommitted);
	TestEqual(TEXT("right release resumes physically held left button"), Runner->GetCatAction(), ECatFightCatAction::Pull);
	TestTrue(TEXT("session publishes resumed reel"), Session->GetSnapshot().bReeling && !Session->GetSnapshot().bSlacking);
	FCatFishingRodRotationPrediction AfterRelease;
	Fixture.Rod->GetRotationPredictionFromAuthority(0.0, AfterRelease);
	TestTrue(TEXT("right release resumes reeling without restoring stopped mouse aim"),
		!AfterRelease.Input.bCatDriveActive && AfterRelease.Input.RequestedAim.Equals(Stationary.Input.RequestedAim, 1e-8));

	const int64 ParticipantSequence = Runner->FindParticipant(Fixture.Player)->LastInputSequence;
	const int64 SnapshotSequence = Session->GetSnapshot().SnapshotSequence;
	FCatFishingInputEdge StaleControl = Commands->MakeDiscreteEdge();
	++StaleControl.ControlEpoch;
	AddExpectedErrorPlain(TEXT("Event=fishing_control_input_rejected"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedErrorPlain(TEXT("Error=ECatFishingCommandError::InputSequenceStale"), EAutomationExpectedErrorFlags::Contains, 1);
	Commands->HandleAbilityCommandFromAuthority(ECatFishingCommandType::PrimaryReleased, StaleControl);
	TestTrue(TEXT("old control epoch cannot release a new primary's reel"),
		Commands->TryGetResult(StaleControl.RequestId, Result) && !Result.bCommitted
		&& Result.Error == ECatFishingCommandError::InputSequenceStale);
	TestEqual(TEXT("stale control cannot consume current runner input sequence"),
		Runner->FindParticipant(Fixture.Player)->LastInputSequence, ParticipantSequence);
	TestEqual(TEXT("stale control preserves active reeling"), Runner->GetCatAction(), ECatFightCatAction::Pull);
	auto WrongEpoch = Sample(1000, 900.0, 0.0, Fixture.Rod);
	++WrongEpoch.InputEpoch;
	const FGuid RejectedSessionRequest = FGuid::NewGuid();
	AddExpectedErrorPlain(FString::Printf(TEXT("Event=fishing_rod_aim_rebase_rejected RequestId=%s"),
		*RejectedSessionRequest.ToString()), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("invalid rebase rejects the entire Session transition"),
		Session->SetSlackingFromAuthority(Fixture.Player, ParticipantSequence + 1, true, &WrongEpoch, RejectedSessionRequest));
	TestEqual(TEXT("rejection cannot consume runner input sequence"), Runner->FindParticipant(Fixture.Player)->LastInputSequence, ParticipantSequence);
	TestEqual(TEXT("rejection cannot publish a new session snapshot"), Session->GetSnapshot().SnapshotSequence, SnapshotSequence);
	TestEqual(TEXT("rejection cannot pause active reel"), Runner->GetCatAction(), ECatFightCatAction::Pull);
	FCatFishingRodRotationPrediction Rejected;
	Fixture.Rod->GetRotationPredictionFromAuthority(0.0, Rejected);
	TestTrue(TEXT("rejection cannot modify rod aim"), Rejected.Input.RequestedAim.Equals(Stationary.Input.RequestedAim, 1e-8));
	APlayerState* Helper = Fixture.World->SpawnActor<APlayerState>();
	int32 HelperSlot = INDEX_NONE;
	if (!TestNotNull(TEXT("helper identity"), Helper)
		|| !TestTrue(TEXT("helper joins the same rod"), Fixture.Rod->AddOperatorFromAuthority(
			Helper, Fixture.Rod->GetPresentationState().RodActorRevision, HelperSlot))) return false;
	const auto HelperPress = Sample(1001, 900.0, 0.0, Fixture.Rod);
	TestFalse(TEXT("helper cannot submit primary-only reeling"), Session->SetReelingFromAuthority(Helper, 1, true));
	TestFalse(TEXT("helper cannot use primary-only Session slack rebase"),
		Session->SetSlackingFromAuthority(Helper, 1, true, &HelperPress, FGuid::NewGuid()));
	TestEqual(TEXT("helper rejection preserves the primary's runner sequence"), Runner->FindParticipant(Fixture.Player)->LastInputSequence, ParticipantSequence);
	TestEqual(TEXT("helper rejection does not publish a changed snapshot"), Session->GetSnapshot().SnapshotSequence, SnapshotSequence);
	Fixture.Rod->GetRotationPredictionFromAuthority(0.0, Rejected);
	TestTrue(TEXT("helper rejection cannot alter primary aim"), Rejected.Input.RequestedAim.Equals(Stationary.Input.RequestedAim, 1e-8));
	const auto NewPhysicalPress = Commands->SubmitSlackPressed();
	TestTrue(TEXT("new physical press after release commits"), Commands->TryGetResult(NewPhysicalPress.RequestId, Result) && Result.bCommitted);
	FCatFishingRodRotationPrediction NewPress;
	Fixture.Rod->GetRotationPredictionFromAuthority(0.0, NewPress);
	TestTrue(TEXT("new physical press deliberately discards current unfulfilled input"), NewPress.Input.RequestedAim.Equals(NewPress.Input.CurrentAim, 1e-8));
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	const auto ApplyRawPitch = [&](const double Pitch)
	{
		Fixture.Controller->RotationInput = FRotator(Pitch, 0.0, 0.0);
		Fixture.Controller->UpdateRotation(1.0f / 60.0f);
		Fixture.Controller->RotationInput = FRotator::ZeroRotator;
		FCatFishingRodRotationPrediction Prediction;
		Fixture.Rod->GetRotationPredictionFromAuthority(0.0, Prediction);
		return Prediction.Input.RequestedAim.Pitch;
	};
	TestEqual(TEXT("production input clamps raw pitch at the configured upper rod limit"),
		ApplyRawPitch(200.0), Settings->HeldRodMaximumPitchDegrees, 1e-7);
	TestEqual(TEXT("one degree raw reverse immediately leaves the upper limit"),
		ApplyRawPitch(-1.0), Settings->HeldRodMaximumPitchDegrees - 1.0, 1e-7);
	TestEqual(TEXT("production input clamps raw pitch at the configured lower rod limit"),
		ApplyRawPitch(-200.0), Settings->HeldRodMinimumPitchDegrees, 1e-7);
	TestEqual(TEXT("one degree raw reverse immediately leaves the lower limit"),
		ApplyRawPitch(1.0), Settings->HeldRodMinimumPitchDegrees + 1.0, 1e-7);
	const auto BeforeRetryRelease = Commands->SubmitSlackReleased();
	TestTrue(TEXT("release before retry scenario commits"),
		Commands->TryGetResult(BeforeRetryRelease.RequestId, Result) && Result.bCommitted);
	FCatFishingRodRotationPrediction BeforeRejectedPress;
	Fixture.Rod->GetRotationPredictionFromAuthority(0.0, BeforeRejectedPress);
	TestFalse(TEXT("retry scenario has real unfulfilled aim to discard"),
		BeforeRejectedPress.Input.RequestedAim.Equals(BeforeRejectedPress.Input.CurrentAim, 1e-4));
	FCatFishingInputEdge RejectedPress = Commands->MakeDiscreteEdge();
	RejectedPress.RodAimSample = Commands->MakeRodAimSample(Fixture.Rod);
	++RejectedPress.RodAimSample.InputEpoch;
	AddExpectedErrorPlain(FString::Printf(TEXT("Event=fishing_rod_aim_rebase_rejected RequestId=%s"),
		*RejectedPress.RequestId.ToString()), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedErrorPlain(FString::Printf(
		TEXT("Event=fishing_command_result Type=ECatFishingCommandType::SlackPressed Committed=false Error=ECatFishingCommandError::InvalidPhase Request=%s"),
		*RejectedPress.RequestId.ToString(EGuidFormats::DigitsWithHyphens)), EAutomationExpectedErrorFlags::Contains, 1);
	Commands->HandleAbilityCommandFromAuthority(ECatFishingCommandType::SlackPressed, RejectedPress);
	TestTrue(TEXT("wrong-epoch press is rejected through actual command routing"),
		Commands->TryGetResult(RejectedPress.RequestId, Result) && !Result.bCommitted
		&& Result.Error == ECatFishingCommandError::InvalidPhase);
	TestEqual(TEXT("rejected routed press keeps the runner reeling"), Runner->GetCatAction(), ECatFightCatAction::Pull);
	FCatFishingRodRotationPrediction AfterRejectedPress;
	Fixture.Rod->GetRotationPredictionFromAuthority(0.0, AfterRejectedPress);
	TestTrue(TEXT("rejected routed press cannot alter the previous aim"),
		AfterRejectedPress.Input.RequestedAim.Equals(BeforeRejectedPress.Input.RequestedAim, 1e-8));
	// No intervening release: receipt of a rejected physical press must not impersonate accepted Runner state.
	const auto ValidRetryPress = Commands->SubmitSlackPressed();
	TestTrue(TEXT("valid higher-sequence press succeeds without releasing after rejection"),
		ValidRetryPress.InputSequence > RejectedPress.InputSequence
		&& Commands->TryGetResult(ValidRetryPress.RequestId, Result) && Result.bCommitted);
	TestEqual(TEXT("accepted retry transitions the real runner to slack"), Runner->GetCatAction(), ECatFightCatAction::Slack);
	FCatFishingRodRotationPrediction AfterValidRetry;
	Fixture.Rod->GetRotationPredictionFromAuthority(0.0, AfterValidRetry);
	TestTrue(TEXT("accepted retry rebases to current actual posture despite previous rejected press"),
		AfterValidRetry.Input.RequestedAim.Equals(AfterValidRetry.Input.CurrentAim, 1e-8));
	const auto BeforeReset = Commands->MakeRodAimSample(Fixture.Rod);
	Commands->ResetTransientCommandState();
	const auto AfterReset = Commands->MakeRodAimSample(Fixture.Rod);
	TestTrue(TEXT("input lifecycle reset cannot rewind aim sequence within a controller lifetime"), AfterReset.Sequence > BeforeReset.Sequence);
	TestTrue(TEXT("input lifecycle reset cannot rewind cumulative mouse movement"),
		AfterReset.CumulativeLookDegrees.Equals(BeforeReset.CumulativeLookDegrees, 1e-8));
	TestFalse(TEXT("input lifecycle reset cannot leave mouse drive active"), AfterReset.bMouseActive);
	TestTrue(TEXT("input lifecycle reset cannot rewind mouse stroke identity"), AfterReset.MouseStrokeSequence >= BeforeReset.MouseStrokeSequence);
	// Synthetic pending-domain fixture: locally hide the fight constraint, then restore it.
	// This exercises sender input filtering; it does not exercise network replication transport.
	const FCatFishingCarrierConstraintState SavedConstraint = Fixture.Rod->GetCarrierConstraintState();
	Fixture.Rod->ClearCarrierConstraintFromAuthority();
	// The previous check reset command-edge sequencing; retain this fixture's still-running Runner domain.
	Commands->NextInputSequence = Runner->FindParticipant(Fixture.Player)->LastInputSequence;
	const auto PendingDomainPress = Commands->SubmitSlackPressed();
	TestTrue(TEXT("already-held Runner accepts pending-domain press without another rebase"),
		Commands->TryGetResult(PendingDomainPress.RequestId, Result) && Result.bCommitted);
	TestEqual(TEXT("pending-domain press has no observed fight epoch"), PendingDomainPress.RodAimSample.InputEpoch, uint32{0});
	TestEqual(TEXT("local pitch filter remains unbound before the constraint is visible"), Commands->LocalPitchAimEpoch, uint32{0});
	TestTrue(TEXT("pending-domain press initializes local pitch filtering immediately"), Commands->bLocalPitchAimInitialized);
	const double PendingPitchBaseline = Commands->LocalRequestedRodPitch;
	TestEqual(TEXT("pending-domain pitch starts from the visible held grip"), PendingPitchBaseline,
		FMath::Clamp(FRotator::NormalizeAxis(Fixture.Rod->GetGripWorldTransform().Rotator().Pitch),
			Settings->HeldRodMinimumPitchDegrees, Settings->HeldRodMaximumPitchDegrees), 1e-7);
	const FVector2D PendingCumulativeBaseline = Commands->CumulativeRodLookDegrees;
	Commands->UpdateLocalRodAimInput(1.0 / 60.0, FRotator(200.0, 0.0, 0.0));
	TestEqual(TEXT("pending-domain raw pitch clips at upper limit immediately"),
		Commands->LocalRequestedRodPitch, Settings->HeldRodMaximumPitchDegrees, 1e-7);
	TestEqual(TEXT("pending-domain cumulative pitch contains only effective input"),
		Commands->CumulativeRodLookDegrees.Y - PendingCumulativeBaseline.Y,
		Settings->HeldRodMaximumPitchDegrees - PendingPitchBaseline, 1e-7);
	Commands->UpdateLocalRodAimInput(1.0 / 60.0, FRotator(-1.0, 0.0, 0.0));
	TestEqual(TEXT("pending-domain reverse leaves pitch limit without hidden excess"),
		Commands->LocalRequestedRodPitch, Settings->HeldRodMaximumPitchDegrees - 1.0, 1e-7);
	TestEqual(TEXT("pending-domain reverse contributes exactly one negative degree"),
		Commands->CumulativeRodLookDegrees.Y - PendingCumulativeBaseline.Y,
		Settings->HeldRodMaximumPitchDegrees - PendingPitchBaseline - 1.0, 1e-7);
	const FVector2D BeforeDomainBinding = Commands->CumulativeRodLookDegrees;
	TestTrue(TEXT("restore visible fight constraint for pending-domain fixture"), Fixture.Rod->SetCarrierConstraintFromAuthority(
		SavedConstraint.PullDirection, SavedConstraint.PullAccelerationCentimetersPerSecondSquared,
		SavedConstraint.TargetPullSpeedCentimetersPerSecond, SavedConstraint.NormalizedTension,
		SavedConstraint.ConstraintErrorCentimeters, SavedConstraint.bFightActive,
		SavedConstraint.MaximumFishTorqueStrengthMeters, SavedConstraint.CatTorqueCapacityStrengthMeters,
		SavedConstraint.RodPullAxis, SavedConstraint.PullBrakingDecelerationCentimetersPerSecondSquared,
		SavedConstraint.bUseContinuousTraction));
	Commands->UpdateLocalRodAimInput(1.0 / 60.0, FRotator::ZeroRotator);
	TestTrue(TEXT("pending filter binds the observed nonzero fight epoch"), Commands->LocalPitchAimEpoch != 0
		&& Commands->LocalPitchAimEpoch == Fixture.Rod->GetCarrierConstraintState().AimInputEpoch
		&& Commands->LocalPitchAimRod.Get() == Fixture.Rod && Commands->bLocalPitchAimInitialized);
	TestTrue(TEXT("domain binding preserves all accumulated effective look"),
		Commands->CumulativeRodLookDegrees.Equals(BeforeDomainBinding, 1e-8));
	const double BoundPitchBaseline = FMath::Clamp(FRotator::NormalizeAxis(Fixture.Rod->GetGripWorldTransform().Rotator().Pitch),
		Settings->HeldRodMinimumPitchDegrees, Settings->HeldRodMaximumPitchDegrees);
	TestEqual(TEXT("new fight domain starts from visible actual pitch without carrying pending input debt"),
		Commands->LocalRequestedRodPitch, BoundPitchBaseline, 1e-7);
	FCatFishingRodRotationPrediction BoundIdle;
	Fixture.Rod->GetRotationPredictionFromAuthority(0.0, BoundIdle);
	TestFalse(TEXT("binding a stationary domain cannot activate mouse drive"), BoundIdle.Input.bCatDriveActive);
	Commands->UpdateLocalRodAimInput(1.0 / 60.0, FRotator(-1.0, 0.0, 0.0));
	TestEqual(TEXT("first reverse after binding still moves immediately by one degree"),
		Commands->LocalRequestedRodPitch, FMath::Clamp(BoundPitchBaseline - 1.0,
			Settings->HeldRodMinimumPitchDegrees, Settings->HeldRodMaximumPitchDegrees), 1e-7);
	TestEqual(TEXT("first bound reverse does not repay pre-binding excess pitch"),
		Commands->CumulativeRodLookDegrees.Y - BeforeDomainBinding.Y, -1.0, 1e-7);
	Runner->Stop();
	Fixture.Fishing->Sessions.Remove(Session->Snapshot.FishingSessionId);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
