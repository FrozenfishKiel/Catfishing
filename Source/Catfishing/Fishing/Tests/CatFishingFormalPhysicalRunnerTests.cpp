#include "Inventory/CatInventorySettings.h"
#include "Equipment/Fragments/CatEquipmentFragment_Rod.h"
#include "Fishing/Tests/CatFishingEquipmentTestFixtures.h"
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Tests/AutomationCommon.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Components/BoxComponent.h"
#include "Data/CatFishDefinition.h"
#include "Engine/LocalPlayer.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Environment/Tests/CatWaterTestFixtures.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Fishing/Actors/CatFishEncounterActor.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Fishing/Integration/CatFishingPhysicalRodComponent.h"
#include "Fishing/Simulation/CatFishingFightRunner.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "OnlineSubsystemTypes.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "StateTree.h"
#include "TimerManager.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingFormalPhysicalRunnerTest,
	"Catfishing.Unit.Fishing.PhysicalRod.FormalBlueprintRunnerCouplesForEightSecondsAt60And120Hz",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingFormalPhysicalRunnerTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const UPhysicsSettings* PhysicsSettings = GetDefault<UPhysicsSettings>();
	TestTrue(TEXT("the production physics configuration enables substepping"), PhysicsSettings->bSubstepping);
	TestEqual(TEXT("production substeps are capped at 120 Hz"), double(PhysicsSettings->MaxSubstepDeltaTime), 1.0 / 120.0, 1e-6);
	TestEqual(TEXT("production allows all substeps of a 0.12s frame"), PhysicsSettings->MaxSubsteps, 16);
	AddInfo(FString::Printf(TEXT("Event=fishing_formal_runner_physics_config_observed Substepping=%d MaxSubstepSeconds=%.9f MaxSubsteps=%d Source=PhysicsSettingsCDO"),
		PhysicsSettings->bSubstepping, PhysicsSettings->MaxSubstepDeltaTime, PhysicsSettings->MaxSubsteps));
	UClass* CatClass = LoadClass<ACatCharacter>(nullptr,
		TEXT("/Game/Character/BP_CatCharacter.BP_CatCharacter_C"));
	UStateTree* Tree = LoadObject<UStateTree>(nullptr, TEXT("/Game/Data/StateTrees/ST_FishFight.ST_FishFight"));
	const UCatEquipmentDefinition* RodDefinition = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentDefinition>(TEXT("StarterRodT1"));
	UCatFishDefinition* FishAsset = LoadObject<UCatFishDefinition>(nullptr,
		TEXT("/Game/Catfishing/Data/Fish/Fish_RiverPattern.Fish_RiverPattern"));
	if (!TestTrue(TEXT("formal cat, rod definition, fish definition and behavior tree load"),
		CatClass && Tree && RodDefinition && FishAsset)) return false;

	TArray<double> TravelByRate;
	for (const bool bHitch : {false, true})
	for (const int32 Rate : {60, 120})
	{
		FTestWorldWrapper Wrapper;
		if (!TestTrue(TEXT("creates an independent physics world"), Wrapper.CreateTestWorld(EWorldType::Game))) return false;
		Wrapper.ForwardErrorMessages(this);
		UWorld* World = Wrapper.GetTestWorld();
		FURL URL;
		URL.AddOption(TEXT("game=/Script/Catfishing.CatfishingGameModeBase"));
		if (!TestTrue(TEXT("uses the production admission game mode"), World->SetGameMode(URL))) return false;
		AActor* Floor = World->SpawnActor<AActor>();
		UBoxComponent* FloorBody = NewObject<UBoxComponent>(Floor);
		Floor->SetRootComponent(FloorBody);
		Floor->AddInstanceComponent(FloorBody);
		FloorBody->InitBoxExtent(FVector(4000, 4000, 10));
		FloorBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		FloorBody->SetCollisionResponseToAllChannels(ECR_Block);
		FloorBody->RegisterComponent();
		Floor->SetActorLocation(FVector(0, 0, -10));
		auto* Region = World->SpawnActor<ACatWaterRegion>();
		FCatWaterGeometryBuildInput Geometry;
		Geometry.RegionId = TEXT("FormalPhysicalRunnerWater");
		Geometry.WaterPointVerticalToleranceCm = 10;
		Geometry.BankHeightToleranceCm = 20;
		Geometry.BoundaryToleranceCm = 2;
		Geometry.MaxLandingCorrectionCm = 20;
		Geometry.MinimumWaterInsetCm = 5;
		auto& Boundary = Geometry.Boundaries.AddDefaulted_GetRef();
		Boundary.BoundaryId = TEXT("Outer");
		Boundary.Vertices = {FVector2D(-5000, -5000), FVector2D(5000, -5000),
			FVector2D(5000, 5000), FVector2D(-5000, 5000)};
		const auto Baked = FCatWaterGeometry::Build(Geometry);
		if (!TestTrue(TEXT("bakes the real water query receiver"), Region && Baked.bSucceeded)) return false;
		FCatWaterRegionTestAccess::InjectBakedGeometry(*Region, Baked.Cache);
		if (!TestTrue(TEXT("begins real physics and water lifecycles"), Wrapper.BeginPlayInTestWorld())) return false;
		auto* Mode = World->GetAuthGameMode<ACatfishingGameModeBase>();
		auto* Controller = World->SpawnActor<ACatfishingPlayerController>();
		auto* Player = World->SpawnActor<ACatfishingPlayerState>();
		auto* Cat = World->SpawnActor<ACatCharacter>(CatClass, FVector(0, 0, 100), FRotator::ZeroRotator);
		if (!TestTrue(TEXT("spawns the formal Blueprint and authority hosts"), Mode && Controller && Player && Cat)) return false;
		Mode->bRunCommandsOpen = true;
		Mode->RunPublicState.Phase.Phase = ECatRunPhase::DayActive;
		Mode->RunPublicState.Phase.bFishingAllowed = true;
		Controller->PlayerState = Player;
		Cat->SetPlayerState(Player);
		TStrongObjectPtr<ULocalPlayer> LocalPlayer(NewObject<ULocalPlayer>(GEngine));
		Controller->SetPlayer(LocalPlayer.Get());
		Controller->Possess(Cat);
		Controller->SetActorTickEnabled(false);
		Controller->SetControlRotation(FRotator::ZeroRotator);
		Player->SetPlayerId(1);
		const FUniqueNetIdRef NetId = FUniqueNetIdString::Create(TEXT("FormalPhysicalRunner"), FName(TEXT("CAT_TEST")));
		Player->SetUniqueId(FUniqueNetIdRepl(NetId));
		ACatfishingGameModeBase::FAdmissionRecord Admission;
		Admission.Phase = ACatfishingGameModeBase::EAdmissionPhase::Active;
		Admission.Controller = Controller;
		Mode->AdmissionRecords.Add(ACatfishingGameModeBase::MakeStableNetIdKey(Player->GetUniqueId()), Admission);
		auto* Body = Cat->GetPhysicalBodyComponent();
		if (!TestTrue(TEXT("formal geometry and server admission are ready"), Body && Body->GetBody()
			&& Mode->CanAcceptFishingCommand(Controller))) return false;
		Body->TeleportBodyFromAuthority(FTransform(FRotator::ZeroRotator,
			FVector(0, 0, Body->GetStandRootHeightCm())), TEXT("FormalRunnerInitialPose"));
		const auto TickConnected = [&](const float DeltaSeconds)
		{
			// This test feeds the same accepted intent heartbeat as a connected controller.
			Body->SetMoveIntent(Body->GetMoveIntent());
			return Wrapper.TickTestWorld(DeltaSeconds);
		};
		for (int32 Frame = 0; Frame < Rate; ++Frame) if (!TickConnected(1.0f / Rate)) return false;
		if (!TestTrue(TEXT("the authored-size cat stands on actual floor support"), Body->IsGrounded())) return false;
		auto* ASC = Cat->GetCatAbilitySystemComponent();
		auto* Equipment = Cat->GetEquipmentComponent();
		if (!TestTrue(TEXT("formal ASC and equipment initialized"), ASC && Equipment
			&& ASC->InitializeCharacterAttributesFromDefinition(Cat->GetCatDefinitionId()))) return false;
		const double MaximumStamina = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute());
		ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 50);
		ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), static_cast<float>(MaximumStamina));
		for (const FName Id : {FName(TEXT("StarterRodT1")), FName(TEXT("FeatherFloat"))})
			if (!TestTrue(TEXT("grants the current formal equipment definition"), Equipment->GrantEquipmentFromAuthority(
				FGuid::NewGuid(), Equipment->GetSnapshot().Revision, Id).bCommitted)) return false;
		if (!TestTrue(TEXT("grants a real consumable bait"), Equipment->GrantInventoryQuantityFromAuthority(
			FGuid::NewGuid(), Equipment->GetSnapshot().Revision, TEXT("BugBait"), 1).bCommitted)) return false;
		auto* Service = World->GetSubsystem<UCatFishingService>();
		auto* Commands = Controller->GetFishingCommandComponent();
		const auto R = Commands->SubmitRodInteract();
		FCatFishingCommandResult RResult;
		if (!TestTrue(TEXT("the real R route deploys and physically holds the owned rod"),
			Commands->TryGetResult(R.RequestId, RResult) && RResult.bCommitted)) return false;
		auto* Rod = Service->FindRodOperatedBy(Player);
		if (!TestTrue(TEXT("the configured rod Blueprint has a real grip and shaft"), Rod
			&& Rod->GetClass() == RodDefinition->UseActorClass.Get() && Rod->GetPhysicalRodBody()
			&& Body->GetGrab()->GetGripTarget(true) == Rod && Body->GetGrab()->IsGripping(true))) return false;
		for (int32 Frame = 0; Frame < Rate / 2; ++Frame) if (!TickConnected(1.0f / Rate)) return false;
		const FCatEquipmentLoadoutSnapshot Loadout = Equipment->GetSnapshot();
		const FGuid SessionId = FGuid::NewGuid();
		if (!TestTrue(TEXT("reserves the actual deployed rod and bait/float instances"), Equipment->BeginFishingUse(SessionId,
			Loadout.RodItemInstanceId, Loadout.BaitItemInstanceId, Loadout.FloatItemInstanceId,
			Loadout.RodDefinitionId, Loadout.BaitDefinitionId, Loadout.FloatDefinitionId, Loadout.Revision).bBaitFrozen)
			|| !TestTrue(TEXT("commits the hooked bait through its resource transaction"), Equipment->CommitFishingBaitDeferred(SessionId).bApplied)) return false;
		ON_SCOPE_EXIT { Equipment->ReleaseFishingUse(SessionId); };
		double InitialDurability = 0;
		bool bBroken = false;
		if (!TestTrue(TEXT("reads actual instance durability instead of old authoring-script constants"),
			Equipment->GetFishingRodDurability(SessionId, InitialDurability, bBroken) && !bBroken && InitialDurability > 0)) return false;
		auto* Session = World->SpawnActor<ACatFishingSession>();
		auto* Fish = World->SpawnActor<ACatFishEncounterActor>();
		if (!TestTrue(TEXT("spawns the production session and encounter"), Session && Fish)) return false;
		// Only the already-hooked starting transaction and heavy-fish sample are seeded.
		// All subsequent timers, behavior, forces, surface queries and payments are production consumers.
		UCatFishDefinition* FishDefinition = DuplicateObject<UCatFishDefinition>(FishAsset, Session);
		FishDefinition->FishFightStamina = 1000;
		const FVector Outward = FVector(1, .4, 0).GetSafeNormal();
		FVector FishStart = Rod->GetRodTipWorldTransform().GetLocation() + Outward * 500;
		FishStart.Z = 0;
		Fish->SetActorLocation(FishStart);
		const double InitialLineLength = FVector::Distance(FishStart, Rod->GetRodTipWorldTransform().GetLocation());
		const FGuid CastAttemptId = FGuid::NewGuid();
		if (!TestTrue(TEXT("initializes the encounter through its real identity receiver"), Fish->InitializeAuthoritativeIdentity(
			SessionId, CastAttemptId, FishDefinition->FishDefinitionId, InitialLineLength, 1))) return false;
		Session->FishDefinition = FishDefinition;
		Session->FishWeightKilograms = 30;
		Session->Snapshot.FishingSessionId = SessionId;
		Session->Snapshot.CastAttemptId = CastAttemptId;
		Session->Snapshot.FishDefinitionId = FishDefinition->FishDefinitionId;
		Session->Snapshot.FishWeightKilograms = 30;
		Session->Snapshot.Phase = ECatFishingPhase::HookedFight;
		Session->Snapshot.RodActor = Rod;
		Session->Snapshot.FisherPlayerState = Player;
		Session->Snapshot.FishEncounterActor = Fish;
		Session->Snapshot.FishFightStaminaRemaining = FishDefinition->FishFightStamina;
		Session->AttemptSnapshot.RodItemInstanceId = Loadout.RodItemInstanceId;
		Session->AttemptSnapshot.CastAttemptId = CastAttemptId;
		Session->AttemptSnapshot.WaterRegion = Region->GetWaterRegionHandle();
		Session->CastEquipment = Equipment;
		Session->FisherCharacter = Cat;
		Session->StaminaOwner = Cat;
		Service->Sessions.Add(SessionId, Session);
		auto* Runner = NewObject<UCatFishingFightRunner>(Session);
		Session->FightRunner = Runner;
		ON_SCOPE_EXIT
		{
			Runner->Stop();
			Service->Sessions.Remove(SessionId);
			Body->ClearControlIntent(TEXT("FormalRunnerTestFinished"));
		};
		FCatFishingFightRunnerInit Init;
		Init.Session = Session; Init.RodActor = Rod; Init.FishActor = Fish;
		Init.PrimaryPlayerState = Player; Init.AbilitySystem = ASC;
		Init.WaterRegion = Region->GetWaterRegionHandle(); Init.BehaviorStateTree = Tree;
		Init.RandomSeed = 2003; Init.bInitialPullHeld = true;
		Init.Config.FixedStepSeconds = .05;
		Init.Config.PrimaryOperatorCatStrength = 50;
		Init.Config.PrimaryOperatorMassKilograms = Body->GetBody()->GetMass();
		Init.Config.FishMassKilograms = 30; Init.Config.FishStrength = 300;
		Init.Config.CatStaminaMaximum = MaximumStamina;
		Init.Config.FishFullEffortSpeedCentimetersPerSecond = 75;
		Init.Config.ReelSpeedCentimetersPerSecond = 80;
		Init.Config.MaximumLineLengthCentimeters = CatFishingTest::Fragment<UCatEquipmentFragment_Rod>(RodDefinition)->MaximumLineLengthCentimeters;
		Init.Config.RodDurability = InitialDurability;
		Init.Config.RodPhysicsLengthCentimeters = CatFishingTest::Fragment<UCatEquipmentFragment_Rod>(RodDefinition)->RodPhysicsLengthCentimeters;
		Init.Config.FishFullEffortRodWearPerSecond = CatFishingTest::Fragment<UCatEquipmentFragment_Rod>(RodDefinition)->BaseDurabilityWearPerSecond;
		Init.Config.TautRodWearMultiplier = CatFishingTest::Fragment<UCatEquipmentFragment_Rod>(RodDefinition)->HighTensionWearMultiplier;
		Init.InitialState.CatStamina = MaximumStamina; Init.InitialState.FishStamina = 1000;
		Init.InitialState.FishWorldPosition = FishStart; Init.InitialState.LineLengthCentimeters = InitialLineLength;
		Init.SteeringConfig.OutwardEffortRange = FVector2D(.8, .8);
		Init.SteeringConfig.OutwardAngularSpreadDegrees = 0;
		if (!TestTrue(TEXT("initializes and starts the real fish tree and prephysics fixed-step scheduler"),
			Runner->InitializeFromAuthority(Init) && Runner->Start())) return false;
		int32 StaminaWrites = 0;
		uint64 LastWriteStep = MAX_uint64;
		bool bDuplicatePayment = false;
		const FDelegateHandle PaymentHandle = ASC->GetGameplayAttributeValueChangeDelegate(
			UCatSurvivalAttributeSet::GetFightStaminaAttribute()).AddLambda([&](const FOnAttributeChangeData&)
			{
				++StaminaWrites;
				bDuplicatePayment |= LastWriteStep == Runner->DiagnosticFixedStepSequence;
				LastWriteStep = Runner->DiagnosticFixedStepSequence;
			});
		ON_SCOPE_EXIT { ASC->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFightStaminaAttribute()).Remove(PaymentHandle); };
		auto* Receiver = Rod->GetPhysicalRodComponent();
		const FVector BodyStart = Body->GetBody()->GetComponentLocation();
		double MaximumForce = 0, MaximumJump = 0, PreviousForce = 0, MaximumLineError = 0, MaximumSpeed = 0, MaximumTipReadError = 0;
		double PeakSpeedSeconds = 0, PeakAngularSpeed = 0, PeakBodySpeed = 0;
		double MaximumImpulseLedgerError = 0, MaximumQueuedSeconds = 0;
		double SimulatedSeconds = 0, NextHitchSeconds = 1;
		const double WorldStartSeconds = World->GetTimeSeconds();
		uint64 LastLoadStep = 0;
		int32 CompletedFrames = 0, HitchCount = 0;
		bool bGripSurvived = true, bMovementStarted = false;
		while (SimulatedSeconds < 8.0 - 1e-6)
		{
			if (!bMovementStarted && SimulatedSeconds >= 4.0 - 1e-6)
			{
				Body->SetMoveIntent(FVector(-1, 1, 0).GetSafeNormal());
				bMovementStarted = true;
			}
			double RequestedDeltaSeconds = 1.0 / Rate;
			if (bHitch && SimulatedSeconds >= NextHitchSeconds - 1e-6)
			{
				RequestedDeltaSeconds = .12;
				NextHitchSeconds += 1;
				++HitchCount;
			}
			const float DeltaSeconds = static_cast<float>(FMath::Min(RequestedDeltaSeconds, 8.0 - SimulatedSeconds));
			if (!TickConnected(DeltaSeconds)) return false;
			SimulatedSeconds += DeltaSeconds;
			++CompletedFrames;
			const FVector ImpulseRemainder = Receiver->GetSubmittedLineImpulseNewtonSecondsForDiagnostics()
				- Receiver->GetAppliedLineImpulseNewtonSecondsForDiagnostics()
				- Receiver->GetQueuedLineImpulseNewtonSecondsForDiagnostics()
				- Receiver->GetDiscardedLineImpulseNewtonSecondsForDiagnostics();
			MaximumImpulseLedgerError = FMath::Max(MaximumImpulseLedgerError, ImpulseRemainder.Size());
			MaximumQueuedSeconds = FMath::Max(MaximumQueuedSeconds, Receiver->GetQueuedLineSecondsForDiagnostics());
			const FVector ActualTip = (CatFishingTest::Fragment<UCatEquipmentFragment_Rod>(RodDefinition)->RodTipLocalTransform * Receiver->GetObservedActorTransform()).GetLocation();
			MaximumTipReadError = FMath::Max(MaximumTipReadError,
				FVector::Distance(ActualTip, Rod->GetRodTipWorldTransform().GetLocation()));
			MaximumLineError = FMath::Max(MaximumLineError,
				FMath::Max(0.0, FVector::Distance(Fish->GetActorLocation(), ActualTip) - Runner->State.LineLengthCentimeters));
			const double RodSpeed = Rod->GetPhysicalRodComponent()->GetPointVelocity(Rod->GetPhysicalRodBody()->GetComponentLocation()).Size();
			if (RodSpeed > MaximumSpeed)
			{
				MaximumSpeed = RodSpeed;
				PeakSpeedSeconds = SimulatedSeconds;
				PeakAngularSpeed = Rod->GetPhysicalRodComponent()->GetAngularVelocityRadiansPerSecond().Size();
				PeakBodySpeed = Body->GetVelocity().Size();
			}
			if (Receiver->GetLineLoadStepForDiagnostics() != LastLoadStep)
			{
				LastLoadStep = Receiver->GetLineLoadStepForDiagnostics();
				const double Force = Receiver->GetLineForceNewtonsForDiagnostics().Size();
				MaximumForce = FMath::Max(MaximumForce, Force);
				MaximumJump = FMath::Max(MaximumJump, FMath::Abs(Force - PreviousForce));
				PreviousForce = Force;
			}
			bGripSurvived &= Body->GetGrab()->IsGripping(true) && Body->GetGrab()->GetGripTarget(true) == Rod;
			if (!Runner->IsRunning() || Session->IsTerminal() || !bGripSurvived) break;
		}
		const double Travel = FVector::Dist2D(BodyStart, Body->GetBody()->GetComponentLocation());
		AddInfo(FString::Printf(TEXT("Event=fishing_formal_runner_coupling_observed Hz=%d Hitch120ms=%d HitchCount=%d Frames=%d Seconds=%.3f WorldSeconds=%.6f FixedStepSeconds=%.6f CatClass=%s RodClass=%s GeometryScale=%.3f InitialInstanceDurability=%.3f RemainingDurability=%.3f Phase=%s Outcome=%s Steps=%llu ASCWrites=%d Grip=%d MaximumForceN=%.3f MaximumForceJumpN=%.3f MaximumLineErrorCm=%.3f MaximumRodSpeedCmS=%.3f PeakSpeedSeconds=%.3f PeakAngularSpeedRadS=%.3f PeakBodySpeedCmS=%.3f MaximumTipReadErrorCm=%.3f BodyTravelCm=%.3f Authority=true NetMode=%d"),
			Rate, bHitch, HitchCount, CompletedFrames, SimulatedSeconds, World->GetTimeSeconds() - WorldStartSeconds,
			Runner->DiagnosticFixedStepSequence * Init.Config.FixedStepSeconds, *Cat->GetClass()->GetPathName(), *Rod->GetClass()->GetPathName(),
			Body->GetGeometryScale(), InitialDurability, Session->GetSnapshot().RodDurabilityRemaining,
			*UEnum::GetValueAsString(Session->GetSnapshot().Phase), *UEnum::GetValueAsString(Session->GetSnapshot().Outcome),
			Runner->DiagnosticFixedStepSequence, StaminaWrites, bGripSurvived, MaximumForce, MaximumJump,
			MaximumLineError, MaximumSpeed, PeakSpeedSeconds, PeakAngularSpeed, PeakBodySpeed,
			MaximumTipReadError, Travel, int32(World->GetNetMode())));
		TestEqual(TEXT("the real production prephysics scheduler runs for the full eight seconds"), SimulatedSeconds, 8.0, 1e-5);
		TestEqual(TEXT("world time retains the complete eight seconds including hitches"),
			World->GetTimeSeconds() - WorldStartSeconds, 8.0, .001);
		if (bHitch) TestEqual(TEXT("all seven scheduled hitches were actual 0.12s World ticks"), HitchCount, 7);
		TestTrue(TEXT("eight seconds produce the expected real fixed-step count"), Runner->DiagnosticFixedStepSequence >= 159 && Runner->DiagnosticFixedStepSequence <= 161);
		TestTrue(TEXT("formal body/rod constraints survive sustained heavy-fish and diagonal movement"), bGripSurvived && Runner->IsRunning() && !Session->IsTerminal());
		TestTrue(TEXT("actual line load travels from the rod to the formal cat"), MaximumForce > 1 && Travel > 10);
		TestTrue(TEXT("real endpoint stretch remains bounded"), MaximumLineError < 25);
		TestTrue(TEXT("the production tip reader observes the current physics pose"), MaximumTipReadError < .01);
		TestTrue(TEXT("real rod tension never develops explosive oscillation"), MaximumForce < 1200 && MaximumJump < 900);
		TestTrue(TEXT("the formal shaft has bounded actual speed"), MaximumSpeed < 1000);
		TestTrue(TEXT("resource payment uses at most one ASC write per real fixed step"), StaminaWrites > 0 && !bDuplicatePayment);
		const FVector SubmittedImpulse = Receiver->GetSubmittedLineImpulseNewtonSecondsForDiagnostics();
		const FVector AppliedImpulse = Receiver->GetAppliedLineImpulseNewtonSecondsForDiagnostics();
		const FVector QueuedImpulse = Receiver->GetQueuedLineImpulseNewtonSecondsForDiagnostics();
		AddInfo(FString::Printf(TEXT("Event=fishing_formal_runner_impulse_observed Hz=%d Hitch120ms=%d SubmittedNs=%s AppliedAfterPhysicsNs=%s QueuedNs=%s DiscardedNs=%s MaxLedgerErrorNs=%.9f MaxQueuedSeconds=%.6f"),
			Rate,bHitch,*SubmittedImpulse.ToString(),*AppliedImpulse.ToString(),*QueuedImpulse.ToString(),
			*Receiver->GetDiscardedLineImpulseNewtonSecondsForDiagnostics().ToString(),MaximumImpulseLedgerError,MaximumQueuedSeconds));
		TestTrue(TEXT("each submitted force interval is accounted for by actual physics, queue or explicit teardown"), MaximumImpulseLedgerError < 1e-5);
		TestTrue(TEXT("real PostPhysics observations confirm nonzero applied line impulse"), AppliedImpulse.Size() > 1);
		TestTrue(TEXT("normal operation never discards queued simulation impulse"),Receiver->GetDiscardedLineImpulseNewtonSecondsForDiagnostics().IsNearlyZero(1e-5));
		TestEqual(TEXT("the runner balance mirrors the actually paid ASC value"), Runner->State.CatStamina,
			double(ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute())), 1e-4);
		TestEqual(TEXT("the session publishes the actual fish balance without another charge"), Session->GetSnapshot().FishFightStaminaRemaining, Runner->State.FishStamina, 1e-8);
		TestTrue(TEXT("the real reserved rod retains its paid durability"), Equipment->GetFishingRodDurability(SessionId, InitialDurability, bBroken)
			&& !bBroken && InitialDurability == Session->GetSnapshot().RodDurabilityRemaining);
		const double PaidDurability = InitialDurability;
		if (!bHitch) TravelByRate.Add(Travel);
		else if (TravelByRate.Num() == 2)
		{
			const double Reference = TravelByRate[Rate == 60 ? 0 : 1];
			TestTrue(TEXT("periodic hitches preserve comparable production motion"),
				FMath::Abs(Travel - Reference) < FMath::Max(35.0, .3 * FMath::Max(Travel, Reference)));
		}
		if (bHitch && Rate == 120)
		{
			// The active scheduler belongs to the real rod. Losing it must publish the existing failure terminal,
			// not leave a live session waiting for a component tick which can no longer occur.
			const uint64 DestroyedRodStep = Runner->DiagnosticFixedStepSequence;
			AddExpectedErrorPlain(TEXT("Event=fishing_session_terminated"), EAutomationExpectedErrorFlags::Contains, 1);
			TestTrue(TEXT("destroys the actual rod that has run the full physical fight"), Rod->Destroy());
			TestTrue(TEXT("receiver destruction finalizes its active session through the existing failure entry"),
				Session->IsTerminal() && Session->GetSnapshot().Outcome == ECatFishingOutcome::Invalidated);
			TestFalse(TEXT("destroyed scheduler host cannot leave a running fight"), Runner->IsRunning());
			TestFalse(TEXT("receiver destruction unbinds the prephysics scheduler"), Runner->PhysicsFrameHandle.IsValid());
			TestFalse(TEXT("receiver destruction unbinds its availability notification"), Runner->PhysicsReceiverUnavailableHandle.IsValid());
			TestFalse(TEXT("destroyed shaft releases its actual grip"), Body->GetGrab()->IsGripping(true));
			TestFalse(TEXT("receiver destruction releases the original resource reservation"), Equipment->IsFishingUseActive(SessionId));
			Body->SetMoveIntent(FVector::ZeroVector);
			if (!TickConnected(1.0f / Rate)) return false;
			TestEqual(TEXT("a real physics frame after rod destruction cannot advance the stopped fight"), Runner->DiagnosticFixedStepSequence, DestroyedRodStep);
			TestTrue(TEXT("destroying the rod preserves its already-paid instance wear"),
				Equipment->GetFishingRodDurability(SessionId, InitialDurability, bBroken) && InitialDurability == PaidDurability);
			AddInfo(FString::Printf(TEXT("Event=fishing_formal_runner_host_destroyed_observed SessionId=%s Steps=%llu Running=%d Terminal=%d ResourceActive=%d"),
				*SessionId.ToString(), Runner->DiagnosticFixedStepSequence, Runner->IsRunning(), Session->IsTerminal(), Equipment->IsFishingUseActive(SessionId)));
			continue;
		}
		const uint64 LastAcceptedLoadStep = Receiver->GetLineLoadStepForDiagnostics();
		if (!Session->IsTerminal())
		{
			AddExpectedErrorPlain(TEXT("Event=fishing_session_terminated"), EAutomationExpectedErrorFlags::Contains, 1);
			TestTrue(TEXT("cancel finalizes the same live session"), Session->CancelFromAuthority(FGuid::NewGuid()).bCommitted);
		}
		TestFalse(TEXT("finalization unbinds the actual prephysics scheduler"), Runner->PhysicsFrameHandle.IsValid());
		const uint64 StoppedStep = Runner->DiagnosticFixedStepSequence;
		TestTrue(TEXT("finalization removes the physical receiver's sustained line load"),
			Receiver->GetLineForceNewtonsForDiagnostics().IsNearlyZero() && !Rod->GetCarrierConstraintState().bFightActive);
		TestTrue(TEXT("finalization accounts for canceled queued impulses without applying them"),
			(Receiver->GetSubmittedLineImpulseNewtonSecondsForDiagnostics()
				- Receiver->GetAppliedLineImpulseNewtonSecondsForDiagnostics()
				- Receiver->GetQueuedLineImpulseNewtonSecondsForDiagnostics()
				- Receiver->GetDiscardedLineImpulseNewtonSecondsForDiagnostics()).IsNearlyZero(1e-5));
		TestEqual(TEXT("finalization empties unconsumed force time"),Receiver->GetQueuedLineSecondsForDiagnostics(),0.0,1e-8);
		TestTrue(TEXT("clearing force preserves the session/step fence against stale loads"),
			Receiver->GetLineLoadSessionIdForDiagnostics() == SessionId && Receiver->GetLineLoadStepForDiagnostics() == LastAcceptedLoadStep);
		AddExpectedErrorPlain(TEXT("Event=fishing_physical_line_load_rejected"), EAutomationExpectedErrorFlags::Contains, 1);
		Receiver->SetLineLoad(SessionId, LastAcceptedLoadStep, FVector(100, 0, 0), .05, .15);
		Body->SetMoveIntent(FVector::ZeroVector);
		if (!TickConnected(1.0f / Rate)) return false;
		TestEqual(TEXT("an actual physics frame after finalization cannot advance the stopped runner"), Runner->DiagnosticFixedStepSequence, StoppedStep);
		TestTrue(TEXT("late old force remains rejected after a real physics frame"),
			Receiver->GetLineForceNewtonsForDiagnostics().IsNearlyZero()
			&& Receiver->GetLineLoadStepForDiagnostics() == LastAcceptedLoadStep);
		TestFalse(TEXT("finalization releases the real resource reservation"), Equipment->IsFishingUseActive(SessionId));
		TestTrue(TEXT("releasing the reservation preserves the already-paid instance wear"),
			Equipment->GetFishingRodDurability(SessionId, InitialDurability, bBroken) && InitialDurability == PaidDurability);
	}
	if (TravelByRate.Num() == 2) TestTrue(TEXT("formal production travel is comparable at 60 and 120 Hz"),
		FMath::Abs(TravelByRate[0] - TravelByRate[1]) < FMath::Max(35.0, .3 * FMath::Max(TravelByRate[0], TravelByRate[1])));
	return !HasAnyErrors();
}

#endif
