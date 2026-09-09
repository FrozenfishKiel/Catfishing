#if WITH_DEV_AUTOMATION_TESTS

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
			Controller->SetControlRotation(FRotator(0.0, 120.0, 0.0));
			for (int32 Frame = 0; Frame < 360; ++Frame)
			{
				if (!Rod->RefreshHeldTransformFromAuthority(1.0 / 60.0))
				{
					Test.AddError(TEXT("loaded production rod pose must integrate"));
					return false;
				}
			}
			return Test.TestTrue(TEXT("fish holds actual rod far short of the old controller target"),
				FMath::Abs(FMath::FindDeltaAngleDegrees(Rod->GetGripWorldTransform().Rotator().Yaw, 120.0)) > 60.0);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingSlackAimSampleOrderTest,
	"Catfishing.Unit.Fishing.SlackAim.CumulativeSamplesSurviveReorderingAndLostFinalInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingSlackAimSampleOrderTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingSlackAimTest;
	FCatFishingRodAimState InOrder;
	FCatFishingRodAimState FutureFirst;
	const auto Press = Sample(10, 700.0, 100.0);
	const auto NewLook = Sample(12, 705.0, 102.0);
	const FRotator Actual(10.0, 20.0, 0.0);
	TestTrue(TEXT("press rebases ordered stream to actual rod"), InOrder.Rebase(Press, Actual, -60.0, 60.0));
	TestTrue(TEXT("new look arrives after press"), InOrder.AcceptSample(NewLook, -60.0, 60.0));
	TestTrue(TEXT("future look can arrive ahead of reliable press"), FutureFirst.AcceptSample(NewLook, -60.0, 60.0));
	TestFalse(TEXT("pre-press samples alone never switch control mode"), FutureFirst.IsRebased());
	TestTrue(TEXT("late press retains genuinely post-press look"), FutureFirst.Rebase(Press, Actual, -60.0, 60.0));
	TestTrue(TEXT("both network arrival orders produce the same target"),
		FutureFirst.GetRequestedAim().Equals(InOrder.GetRequestedAim(), 1e-8));
	TestEqual(TEXT("only five new yaw degrees survive the press"), FutureFirst.GetRequestedAim().Yaw, 25.0);
	TestEqual(TEXT("only two new pitch degrees survive the press"), FutureFirst.GetRequestedAim().Pitch, 12.0);
	TestFalse(TEXT("an older movement packet cannot restore pre-press aim"),
		FutureFirst.AcceptSample(Sample(9, 690.0, 90.0), -60.0, 60.0));
	TestFalse(TEXT("same movement packet cannot count twice"), FutureFirst.AcceptSample(NewLook, -60.0, 60.0));
	TestFalse(TEXT("replayed press cannot discard newer input"), FutureFirst.Rebase(Press, Actual, -60.0, 60.0));
	TestEqual(TEXT("rejections leave requested yaw intact"), FutureFirst.GetRequestedAim().Yaw, 25.0);
	// Sequence 13 carried the final +7 degrees but was lost; the stationary heartbeat includes it.
	TestTrue(TEXT("stationary cumulative resend repairs a lost final movement packet"),
		FutureFirst.AcceptSample(Sample(14, 712.0, 102.0), -60.0, 60.0));
	TestEqual(TEXT("lost movement is recovered exactly once"), FutureFirst.GetRequestedAim().Yaw, 32.0);
	TestTrue(TEXT("next stationary heartbeat is accepted"), FutureFirst.AcceptSample(Sample(15, 712.0, 102.0), -60.0, 60.0));
	TestEqual(TEXT("stationary heartbeat cannot keep rotating the target"), FutureFirst.GetRequestedAim().Yaw, 32.0);
	TestTrue(TEXT("next physical press discards this fight's remaining target error"),
		FutureFirst.Rebase(Sample(16, 712.0, 102.0), FRotator(8.0, 22.0, 0.0), -60.0, 60.0));
	TestEqual(TEXT("next press uses its current actual angle"), FutureFirst.GetRequestedAim().Yaw, 22.0);
	FutureFirst.Reset();
	TestFalse(TEXT("lifecycle reset removes rebased input mode"), FutureFirst.IsRebased());
	TestEqual(TEXT("lifecycle reset discards old packet sequence"), FutureFirst.GetLastSequence(), int64{0});
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingSlackAimAngularBoundaryTest,
	"Catfishing.Unit.Fishing.SlackAim.YawWrapAndPitchLimitAllowImmediateReverseInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingSlackAimAngularBoundaryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingSlackAimTest;
	FCatFishingRodAimState State;
	TestTrue(TEXT("rebase near positive yaw and pitch limits"),
		State.Rebase(Sample(1, 1000.0, 2000.0), FRotator(59.0, 179.0, 23.0), -60.0, 60.0));
	TestTrue(TEXT("look crosses positive yaw boundary and reaches pitch limit"),
		State.AcceptSample(Sample(2, 1005.0, 2001.0), -60.0, 60.0));
	TestEqual(TEXT("yaw crosses plus 180 without a full turn"), State.GetRequestedAim().Yaw, -176.0);
	TestEqual(TEXT("pitch is clamped at the existing body limit"), State.GetRequestedAim().Pitch, 60.0);
	TestTrue(TEXT("reverse look is accepted immediately"), State.AcceptSample(Sample(3, 1000.0, 2000.0), -60.0, 60.0));
	TestEqual(TEXT("reverse yaw crosses minus 180 continuously"), State.GetRequestedAim().Yaw, 179.0);
	TestEqual(TEXT("one degree reverse escapes upper limit without paying off hidden input"), State.GetRequestedAim().Pitch, 59.0);
	// The sender filters excess raw pitch per frame; snapshots contain only the effective movement.
	TestTrue(TEXT("effective movement reaches lower limit"), State.AcceptSample(Sample(4, 1000.0, 1881.0), -60.0, 60.0));
	TestEqual(TEXT("lower pitch clamps"), State.GetRequestedAim().Pitch, -60.0);
	TestTrue(TEXT("reverse upwards immediately"), State.AcceptSample(Sample(5, 1000.0, 1882.0), -60.0, 60.0));
	TestEqual(TEXT("one degree reverse escapes lower limit"), State.GetRequestedAim().Pitch, -59.0);
	TestEqual(TEXT("input never introduces roll"), State.GetRequestedAim().Roll, 0.0);
	FCatFishingRodAimState EveryPacket;
	FCatFishingRodAimState Coalesced;
	EveryPacket.Rebase(Sample(1, 0.0, 0.0), FRotator(65.0, 0.0, 0.0), -35.0, 70.0);
	Coalesced.Rebase(Sample(1, 0.0, 0.0), FRotator(65.0, 0.0, 0.0), -35.0, 70.0);
	EveryPacket.AcceptSample(Sample(2, 0.0, 5.0), -35.0, 70.0);
	EveryPacket.AcceptSample(Sample(3, 0.0, -5.0), -35.0, 70.0);
	Coalesced.AcceptSample(Sample(3, 0.0, -5.0), -35.0, 70.0);
	TestEqual(TEXT("filtered plus ten/minus ten raw look retains the reverse after clipping"), EveryPacket.GetRequestedAim().Pitch, 60.0);
	TestTrue(TEXT("dropping the pitch-limit packet cannot change the final requested aim"),
		Coalesced.GetRequestedAim().Equals(EveryPacket.GetRequestedAim(), 1e-8));
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
	Fixture.Controller->SetControlRotation(FRotator(0.0, 0.0, 0.0));
	if (!TestTrue(TEXT("loaded rod starts turning back before the right-button edge"),
		Rod->RefreshHeldTransformFromAuthority(1.0 / 60.0))) return false;
	FCatFishingRodRotationPrediction Before;
	if (!TestTrue(TEXT("read production loaded rotation input"), Rod->GetRotationPredictionFromAuthority(0.0, Before))) return false;
	TestTrue(TEXT("rebase fixture has actual angular velocity to preserve"),
		!Before.Input.PreviousAngularVelocityRadiansPerSecond.IsNearlyZero());
	const FTransform PoseBefore = Rod->GetActorTransform();
	const auto EffortBefore = Rod->GetAuthoritativeRotationEffortSnapshot();
	const auto Press = Sample(10, 120.0, 0.0, Rod);
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
	TestEqual(TEXT("press cannot erase already accumulated effort"),
		Rod->GetAuthoritativeRotationEffortSnapshot().ExertionSquaredSeconds, EffortBefore.ExertionSquaredSeconds);
	// CMC/control packets may keep writing the old hidden target after the reliable right-click RPC.
	Fixture.Controller->SetControlRotation(FRotator(45.0, 150.0, 0.0));
	FCatFishingRodRotationPrediction AfterOldControl;
	Rod->GetRotationPredictionFromAuthority(0.0, AfterOldControl);
	TestTrue(TEXT("late old ControlRotation cannot reinstate hidden pitch or yaw"),
		AfterOldControl.Input.RequestedAim.Equals(Rebased.Input.RequestedAim, 1e-8));
	TestTrue(TEXT("genuinely new mouse input is accepted"), Rod->AcceptHeldAimSampleFromAuthority(Fixture.Player, Sample(11, 125.0, 0.0, Rod)));
	FCatFishingRodRotationPrediction NewInput;
	Rod->GetRotationPredictionFromAuthority(0.0, NewInput);
	TestEqual(TEXT("new five-degree mouse input changes production target by five degrees"),
		FMath::FindDeltaAngleDegrees(Rebased.Input.RequestedAim.Yaw, NewInput.Input.RequestedAim.Yaw), 5.0, 1e-7);
	TestFalse(TEXT("delayed pre-press packet is discarded"), Rod->AcceptHeldAimSampleFromAuthority(Fixture.Player, Sample(9, 90.0, 0.0, Rod)));
	TestTrue(TEXT("unloading retains the same fight"), Rod->SetCarrierConstraintFromAuthority(
		FVector::ForwardVector, 0.0, 0.0, 0.0, 0.0, true, 0.0, 50.0));
	FCatFishingRodRotationPrediction Unloaded;
	Rod->GetRotationPredictionFromAuthority(0.0, Unloaded);
	TestTrue(TEXT("unload publication does not erase smoothed residual force"),
		Unloaded.Input.PreviousSmoothedFishPullStrengthMeters.Equals(Before.Input.PreviousSmoothedFishPullStrengthMeters, 1e-8));
	TestTrue(TEXT("load publication preserves angular velocity until real integration"),
		Unloaded.Input.PreviousAngularVelocityRadiansPerSecond.Equals(Before.Input.PreviousAngularVelocityRadiansPerSecond, 1e-8));
	for (int32 Frame = 0; Frame < 240; ++Frame)
	{
		if (!Rod->RefreshHeldTransformFromAuthority(1.0 / 60.0)) return false;
	}
	TestEqual(TEXT("actual production grip eventually follows only the new five-degree target"),
		Rod->GetGripWorldTransform().Rotator().Yaw, NewInput.Input.RequestedAim.Yaw, 0.03);
	TestTrue(TEXT("new movement continues recording real rotation work"),
		Rod->GetAuthoritativeRotationEffortSnapshot().PositiveWorkRadians > EffortBefore.PositiveWorkRadians);
	const auto OldFightSample = Sample(100, 200.0, 0.0, Rod);
	Fixture.Controller->SetControlRotation(FRotator(0.0, 70.0, 0.0));
	Rod->ClearCarrierConstraintFromAuthority();
	TestTrue(TEXT("exit restores ordinary controller-based aim"), Rod->RefreshHeldTransformFromAuthority());
	TestEqual(TEXT("non-fight grip follows current control yaw"), Rod->GetGripWorldTransform().Rotator().Yaw, 70.0, 1e-7);
	TestTrue(TEXT("next fight starts"), Rod->SetCarrierConstraintFromAuthority(
		FVector::ForwardVector, 0.0, 0.0, 1.0, 0.0, true, 100.0, 50.0));
	TestTrue(TEXT("new fight gets a new aim input epoch"), Rod->GetCarrierConstraintState().AimInputEpoch != Press.InputEpoch);
	TestFalse(TEXT("previous fight's late movement cannot target the new fight"), Rod->AcceptHeldAimSampleFromAuthority(Fixture.Player, OldFightSample));
	TestFalse(TEXT("previous fight's late press cannot rebase the new fight"), Rod->CanRebaseHeldAimFromAuthority(Fixture.Player, OldFightSample));
	TestTrue(TEXT("new fight accepts samples in its own epoch"), Rod->AcceptHeldAimSampleFromAuthority(Fixture.Player, Sample(101, 200.0, 0.0, Rod)));
	APlayerState* NextPlayer = Fixture.World->SpawnActor<APlayerState>();
	if (!TestNotNull(TEXT("next holder identity"), NextPlayer)) return false;
	TestTrue(TEXT("holder transfer clears old aim ownership"), Rod->SetOperatorFromAuthority(NextPlayer, Rod->GetPresentationState().RodActorRevision));
	TestFalse(TEXT("previous holder cannot submit aim after transfer"),
		Rod->AcceptHeldAimSampleFromAuthority(Fixture.Player, Sample(200, 300.0, 0.0, Rod)));
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
	if (!Fixture.Create(*this) || !Fixture.LoadAgainstOldIntent(*this)) return false;
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
	TestTrue(TEXT("stationary production heartbeat cannot repeat the previous frame delta"), Stationary.Input.RequestedAim.Equals(SameFrame.Input.RequestedAim, 1e-8));

	const auto RepeatedPress = Commands->SubmitSlackPressed();
	TestTrue(TEXT("higher-sequence repeated right press is a held-state update"),
		Commands->TryGetResult(RepeatedPress.RequestId, Result) && Result.bCommitted
		&& RepeatedPress.InputSequence > SlackPress.InputSequence);
	FCatFishingRodRotationPrediction AfterRepeat;
	Fixture.Rod->GetRotationPredictionFromAuthority(0.0, AfterRepeat);
	TestTrue(TEXT("a repeated pressed notification cannot erase newer aim"), AfterRepeat.Input.RequestedAim.Equals(SameFrame.Input.RequestedAim, 1e-8));
	const auto Release = Commands->SubmitSlackReleased();
	TestTrue(TEXT("right release commits"), Commands->TryGetResult(Release.RequestId, Result) && Result.bCommitted);
	TestEqual(TEXT("right release resumes physically held left button"), Runner->GetCatAction(), ECatFightCatAction::Pull);
	TestTrue(TEXT("session publishes resumed reel"), Session->GetSnapshot().bReeling && !Session->GetSnapshot().bSlacking);
	FCatFishingRodRotationPrediction AfterRelease;
	Fixture.Rod->GetRotationPredictionFromAuthority(0.0, AfterRelease);
	TestTrue(TEXT("right release never restores the old hidden aim"), AfterRelease.Input.RequestedAim.Equals(SameFrame.Input.RequestedAim, 1e-8));

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
	TestTrue(TEXT("rejection cannot modify rod aim"), Rejected.Input.RequestedAim.Equals(SameFrame.Input.RequestedAim, 1e-8));
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
	TestTrue(TEXT("helper rejection cannot erase primary aim"), Rejected.Input.RequestedAim.Equals(SameFrame.Input.RequestedAim, 1e-8));
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
	TestEqual(TEXT("domain binding preserves the existing filtered pitch target"),
		Commands->LocalRequestedRodPitch, Settings->HeldRodMaximumPitchDegrees - 1.0, 1e-7);
	Commands->UpdateLocalRodAimInput(1.0 / 60.0, FRotator(-1.0, 0.0, 0.0));
	TestEqual(TEXT("first reverse after binding still moves immediately by one degree"),
		Commands->LocalRequestedRodPitch, Settings->HeldRodMaximumPitchDegrees - 2.0, 1e-7);
	TestEqual(TEXT("first bound reverse does not repay pre-binding excess pitch"),
		Commands->CumulativeRodLookDegrees.Y - BeforeDomainBinding.Y, -1.0, 1e-7);
	Runner->Stop();
	Fixture.Fishing->Sessions.Remove(Session->Snapshot.FishingSessionId);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
