#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Integration/CatFishingPhysicalRodComponent.h"
#include "Fishing/Simulation/CatFishingFightSimulator.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "OnlineSubsystemTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingPhysicalEndpointTest,
	"Catfishing.Unit.Fishing.PhysicalRod.ObservedEndpointHasOneFishLoadAndNoVirtualCatTravel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatFishingPhysicalEndpointTest::RunTest(const FString& Parameters)
{
	FCatFightSimulationConfig Config;
	Config.FixedStepSeconds = 0.05;
	Config.PrimaryOperatorCatStrength = 100;
	Config.PrimaryOperatorMassKilograms = 4;
	Config.FishMassKilograms = 3;
	Config.FishStrength = 40;
	Config.CatStaminaMaximum = 60;
	Config.ReelSpeedCentimetersPerSecond = 80;
	Config.FishFullEffortSpeedCentimetersPerSecond = 75;
	Config.MaximumLineLengthCentimeters = 1000;
	Config.RodDurability = 1000;
	FCatFightSimulationState State;
	State.CatStamina = 60;
	State.FishStamina = 100;
	State.LineLengthCentimeters = 500;
	State.FishWorldPosition = FVector(500, 0, 0);
	State.MotionIntent = ECatFishMotionIntent::StrugglingOutward;
	FCatFightRodConstraintInput Endpoint;
	Endpoint.bPhysicalRodEndpoint = true;
	Endpoint.bRodHeld = true;
	const auto Stationary = FCatFishingFightSimulator::Step(Config, State, Endpoint, FVector::ForwardVector);
	if (!TestTrue(TEXT("physical receiver retains the fish/line solver"), Stationary.bSucceeded)) return false;
	TestTrue(TEXT("taut line produces an actual load for the rod"), Stationary.LineTensionNewtons > 0);
	Endpoint.CarrierVelocityCentimetersPerSecond = FVector(800, -800, 0);
	const auto MovingIntent = FCatFishingFightSimulator::Step(Config, State, Endpoint, FVector::ForwardVector);
	TestTrue(TEXT("a velocity observation does not write a hypothetical endpoint pose"), MovingIntent.bSucceeded
		&& MovingIntent.Trace.ConstraintRodEndWorldPosition.Equals(Endpoint.RodTipWorldPosition, 1.e-6));
	TestTrue(TEXT("one sampled endpoint has the same fish solve regardless of hypothetical cat displacement"),
		MovingIntent.ProposedFishWorldPosition.Equals(Stationary.ProposedFishWorldPosition, 1.e-6));
	TestEqual(TEXT("line tension is not pre-subtracted by another cat force receiver"), MovingIntent.LineTensionNewtons, Stationary.LineTensionNewtons, 1.e-6);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingPhysicalGripGraphTest,
	"Catfishing.Unit.Fishing.PhysicalRod.RealConstraintsDoNotGrantControlAndIsolateSessionLoads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatFishingPhysicalGripGraphTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Wrapper;
	if (!TestTrue(TEXT("authority physics world"), Wrapper.CreateTestWorld(EWorldType::Game))) return false;
	Wrapper.ForwardErrorMessages(this);
	UWorld* World = Wrapper.GetTestWorld();
	FURL URL;
	URL.AddOption(TEXT("game=/Script/Catfishing.CatfishingGameModeBase"));
	if (!World->SetGameMode(URL) || !Wrapper.BeginPlayInTestWorld()) return false;
	auto* GameMode = World->GetAuthGameMode<ACatfishingGameModeBase>();
	GameMode->bRunCommandsOpen = true;
	GameMode->RunPublicState.Phase.Phase = ECatRunPhase::DayActive;
	GameMode->RunPublicState.Phase.bNewFishingBitesAllowed = true;
	TArray<ACatCharacter*> Cats;
	TArray<ACatfishingPlayerState*> Players;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (int32 Index = 0; Index < 6; ++Index)
	{
		auto* Controller = World->SpawnActor<ACatfishingPlayerController>();
		auto* Player = World->SpawnActor<ACatfishingPlayerState>();
		auto* Cat = World->SpawnActor<ACatCharacter>(FVector(0, Index * 40, 200), FRotator::ZeroRotator, Spawn);
		if (!Controller || !Player || !Cat) return false;
		Controller->PlayerState = Player;
		Cat->SetPlayerState(Player);
		Controller->Possess(Cat);
		Player->SetPlayerId(Index + 1);
		const FUniqueNetIdRef UniqueId = FUniqueNetIdString::Create(
			FString::Printf(TEXT("PhysicalGripGraph%d"), Index), FName(TEXT("CAT_TEST")));
		Player->SetUniqueId(FUniqueNetIdRepl(UniqueId));
		ACatfishingGameModeBase::FAdmissionRecord Admission;
		Admission.Phase = ACatfishingGameModeBase::EAdmissionPhase::Active;
		Admission.Controller = Controller;
		GameMode->AdmissionRecords.Add(ACatfishingGameModeBase::MakeStableNetIdKey(Player->GetUniqueId()), Admission);
		Cats.Add(Cat); Players.Add(Player);
	}
	const auto SpawnRod = [World](APlayerState* Owner)
	{
		const FTransform Pose(FRotator::ZeroRotator, FVector(0, 0, 200));
		auto* Rod = World->SpawnActorDeferred<ACatFishingRodActor>(ACatFishingRodActor::StaticClass(), Pose);
		if (!Rod || !Rod->ConfigureCanonicalAnchorsFromAuthority(FTransform(FVector(50, 0, 0)), FTransform::Identity, FTransform::Identity)
			|| !Rod->InitializeAuthoritativeIdentity(FGuid::NewGuid(), FGuid::NewGuid(), TEXT("PhysicalTestRod"), NAME_None, Owner, nullptr, true, false)) return static_cast<ACatFishingRodActor*>(nullptr);
		Rod->FinishSpawning(Pose);
		return Rod;
	};
	auto* Rod = SpawnRod(Players[0]);
	auto* OtherRod = SpawnRod(Players[5]);
	if (!Rod || !OtherRod || !Rod->BeginPhysicalHoldFromAuthority(Players[0], true)
		|| !OtherRod->BeginPhysicalHoldFromAuthority(Players[5], true)) return false;
	if (!TestTrue(TEXT("owner explicitly controls first rod"), Rod->SetPrimaryOperatorFromAuthority(Players[0], Rod->GetPresentationState().RodActorRevision))
		|| !TestTrue(TEXT("other owner explicitly controls second rod"), OtherRod->SetPrimaryOperatorFromAuthority(Players[5], OtherRod->GetPresentationState().RodActorRevision))) return false;
	Rod->GetPhysicalRodComponent()->CommitPrimaryHold(Players[0]);
	OtherRod->GetPhysicalRodComponent()->CommitPrimaryHold(Players[5]);
	Rod->RefreshPrimaryControlFromAuthority();
	const auto Grip = [&Cats](const int32 Index, const bool bLeft, UPrimitiveComponent* Target)
	{
		auto* Physical = Cats[Index]->GetPhysicalBodyComponent();
		if (!Physical->GetGrab()->IsGripping(true) && !Physical->GetGrab()->IsGripping(false))
		{
			FVector Contact = Target->GetComponentLocation();
			if (const auto* Box = Cast<UBoxComponent>(Target))
				Contact = Box->GetComponentTransform().TransformPosition(FVector(0,
					(bLeft ? -1.0 : 1.0) * FMath::Min(3.4, double(Box->GetUnscaledBoxExtent().Y)), Box->GetUnscaledBoxExtent().Z));
			// Only the fixture positions an entire free cat; the submitted point is on real target geometry.
			Physical->TeleportBodyFromAuthority(FTransform(Cats[Index]->GetActorRotation(), Physical->GetBody()->GetComponentLocation()
				+ Contact - Physical->GetHand(bLeft)->GetComponentLocation()), TEXT("GripGraphContactFixture"));
		}
		return Physical->GetGrab()->GripFromAuthority(bLeft, Target, Physical->GetHand(bLeft)->GetComponentLocation());
	};
	if (!TestTrue(TEXT("helper first hand creates a real cat constraint"), Grip(1, true, Cats[0]->GetPhysicalBodyComponent()->GetBody()))
		|| !TestTrue(TEXT("helper second hand creates a second real constraint"), Grip(1, false, Cats[0]->GetPhysicalBodyComponent()->GetBody()))
		|| !Grip(2, true, Cats[1]->GetPhysicalBodyComponent()->GetHand(true))
		|| !Grip(2, false, Cats[0]->GetPhysicalBodyComponent()->GetBody())
		|| !Grip(3, true, Cats[2]->GetPhysicalBodyComponent()->GetBody())) return false;
	Rod->RefreshPrimaryControlFromAuthority();
	TestEqual(TEXT("two hands and a cycle never register helper operators"), Rod->GetOperatorCount(), 1);
	TestTrue(TEXT("a fifth connected cat has no fishing capacity gate"), Grip(4, true, Cats[3]->GetPhysicalBodyComponent()->GetBody()));
	for (int32 Index = 1; Index < 5; ++Index)
		TestFalse(TEXT("physical assistants never obtain rod control"), Rod->IsPrimaryOperator(Players[Index]));
	for (int32 Index = 1; Index < 5; ++Index)
		Cats[Index]->GetPhysicalBodyComponent()->GetGrab()->ReleaseAllFromAuthority(TEXT("TestReleaseChain"));
	if (!TestTrue(TEXT("an assistant may directly grip the actual rod"), Grip(1, true, Rod->GetPhysicalRodBody()))) return false;
	Rod->RefreshPrimaryControlFromAuthority();
	TestEqual(TEXT("direct assistant grip keeps the explicit owner"), Rod->GetPresentationState().HolderPlayerState.Get(), static_cast<APlayerState*>(Players[0]));
	Cats[0]->GetPhysicalBodyComponent()->GetGrab()->ReleaseAllFromAuthority(TEXT("TestOwnerRelease"));
	Rod->RefreshPrimaryControlFromAuthority();
	TestEqual(TEXT("owner release leaves the rod unattended without promotion"), Rod->GetOperatorCount(), 0);
	TestTrue(TEXT("an unattended physical rod accepts passive line observations without opening a control domain"),
		Rod->SetFightConstraintObservationFromAuthority(FVector::ForwardVector, 0.5, 2, false)
		&& !Rod->GetCarrierConstraintState().bFightActive && Rod->GetOperatorCount() == 0);
	TestFalse(TEXT("parking releases the assistant rod grip"), Cats[1]->GetPhysicalBodyComponent()->GetGrab()->IsGripping(true));
	TestTrue(TEXT("physical assistance never changes the other rod's owner control"), OtherRod->IsPrimaryOperator(Players[5]));
	FCatFightRodConstraintInput DynamicResponse;
	Rod->GetPhysicalRodComponent()->PopulateEndpointResponse(DynamicResponse);
	TestTrue(TEXT("parked rod uses fixed endpoint response"), !DynamicResponse.bPhysicalRodEndpoint && DynamicResponse.RodPointInverseMassX.IsZero());
	TestFalse(TEXT("a parked rod rejects an otherwise valid touching hand"), Grip(1, true, Rod->GetPhysicalRodBody()));
	auto* Anchor = World->SpawnActor<AActor>();
	auto* AnchorBox = NewObject<UBoxComponent>(Anchor);
	Anchor->SetRootComponent(AnchorBox); Anchor->AddInstanceComponent(AnchorBox);
	AnchorBox->SetBoxExtent(FVector(5)); AnchorBox->SetCollisionProfileName(TEXT("BlockAll")); AnchorBox->RegisterComponent();
	const FVector AnchorContact = Cats[1]->GetPhysicalBodyComponent()->GetHand(false)->GetComponentLocation();
	AnchorBox->SetWorldLocation(AnchorContact);
	if (!TestTrue(TEXT("the free second hand can grip actual static geometry"), Cats[1]->GetPhysicalBodyComponent()->GetGrab()->GripFromAuthority(false, AnchorBox, AnchorContact))) return false;
	FCatFightRodConstraintInput AnchoredResponse;
	Rod->GetPhysicalRodComponent()->PopulateEndpointResponse(AnchoredResponse);
	TestTrue(TEXT("a static grip across limited shoulders does not weld the rod endpoint or grant control"),
		AnchoredResponse.RodPointInverseMassX.Equals(DynamicResponse.RodPointInverseMassX, 1.e-6)
		&& AnchoredResponse.RodPointInverseMassY.Equals(DynamicResponse.RodPointInverseMassY, 1.e-6)
		&& AnchoredResponse.RodPointInverseMassZ.Equals(DynamicResponse.RodPointInverseMassZ, 1.e-6)
		&& Rod->GetOperatorCount() == 0);
	Cats[1]->GetPhysicalBodyComponent()->GetGrab()->ReleaseHandFromAuthority(false, TEXT("TestStaticRelease"));
	Rod->GetPhysicalRodComponent()->PopulateEndpointResponse(DynamicResponse);
	TestTrue(TEXT("static release does not mobilize a parked rod"), DynamicResponse.RodPointInverseMassX.IsZero()
		&& DynamicResponse.RodTipAccelerationCentimetersPerSecondSquared.IsZero());
	Anchor->Destroy();
	Rod->GetPhysicalRodComponent()->ReleaseAllConnections(TEXT("TestReceiverIsolation"));
	for (ACatCharacter* Cat : Cats) Cat->Destroy();
	OtherRod->Destroy();
	// The receiver accepts only the rod's current Session and monotonic fixed-step publication.
	auto* Service = World->GetSubsystem<UCatFishingService>();
	auto* Session = World->SpawnActor<ACatFishingSession>();
	const FGuid FirstSessionId = FGuid::NewGuid();
	Session->Snapshot.FishingSessionId = FirstSessionId;
	Session->Snapshot.RodActor = Rod;
	Service->Sessions.Add(FirstSessionId, Session);
	auto* Receiver = Rod->GetPhysicalRodComponent();
	Receiver->SetLineLoad(FirstSessionId, 10, FVector(10, 0, 0), .1, 1.0);
	TestEqual(TEXT("one authoritative tension enters the receiver in N"), Receiver->LineForceNewtons.X, 10.0);
	AddExpectedErrorPlain(TEXT("Event=fishing_physical_line_load_rejected"), EAutomationExpectedErrorFlags::Contains, 2);
	Receiver->SetLineLoad(FirstSessionId, 9, FVector(999, 0, 0), .1, 1.0);
	TestEqual(TEXT("older fixed step cannot overwrite the load"), Receiver->LineForceNewtons.X, 10.0);
	const FGuid SecondSessionId = FGuid::NewGuid();
	Service->Sessions.Remove(FirstSessionId);
	Session->Snapshot.FishingSessionId = SecondSessionId;
	Service->Sessions.Add(SecondSessionId, Session);
	Receiver->SetLineLoad(SecondSessionId, 1, FVector(20, 0, 0), .1, 1.0);
	Receiver->SetLineLoad(FirstSessionId, 999, FVector(999, 0, 0), .1, 1.0);
	Receiver->ClearLineLoad(FirstSessionId);
	AddExpectedErrorPlain(TEXT("Event=fishing_fight_constraint_clear_rejected"), EAutomationExpectedErrorFlags::Contains, 1);
	Rod->ClearFightConstraintAndLoadFromAuthority(FirstSessionId);
	TestEqual(TEXT("old Session writes and cleanup cannot change a new Session load"), Receiver->LineForceNewtons.X, 20.0);
	Receiver->ClearLineLoad(SecondSessionId);
	TestTrue(TEXT("current Session cleanup removes its force"), Receiver->LineForceNewtons.IsZero());
	UBoxComponent* RodBody = Rod->GetPhysicalRodBody();
	RodBody->SetWorldLocation(FVector(1000, 1000, 1000), false, nullptr, ETeleportType::ResetPhysics);
	const FTransform SceneProxyBeforeRead = Rod->GetActorTransform();
	RodBody->SetWorldRotation(FRotator(15, 25, 5), false, nullptr, ETeleportType::ResetPhysics);
	const FTransform PhysicalPoseBeforeRead = RodBody->GetComponentTransform();
	const FTransform ObservedActorPose = Receiver->GetObservedActorTransform();
	const FVector ActualTip = (FTransform(FVector(50, 0, 0)) * ObservedActorPose).GetLocation();
	TestTrue(TEXT("authority reads current physical anchors before the scene proxy is refreshed"),
		Rod->GetRodTipWorldTransform().GetLocation().Equals(ActualTip, 1.e-5)
		&& Rod->GetGripWorldTransform().Equals(ObservedActorPose, 1.e-5));
	TestTrue(TEXT("the tip velocity is sampled at that same current physical point"),
		Rod->GetAuthoritativeRodTipVelocity().IsZero());
	TestTrue(TEXT("anchor observation changes neither the rigid body nor its scene proxy"),
		RodBody->GetComponentTransform().Equals(PhysicalPoseBeforeRead, 1.e-5)
		&& Rod->GetActorTransform().Equals(SceneProxyBeforeRead, 1.e-5));
	RodBody->SetWorldRotation(FRotator::ZeroRotator, false, nullptr, ETeleportType::ResetPhysics);
	const FTransform SupportedPose = RodBody->GetComponentTransform();
	Receiver->SetLineLoad(SecondSessionId, 2, FVector(20, 0, 0), .1, 1.0);
	for (int32 Frame = 0; Frame < 12; ++Frame) Wrapper.TickTestWorld(1.0f / 120);
	TestTrue(TEXT("fixed support absorbs fish load without rod motion"), RodBody->GetComponentTransform().Equals(SupportedPose, 1.e-5));
	TestTrue(TEXT("parked receiver accounts for every accepted impulse exactly once"),
		Receiver->GetSubmittedLineImpulseNewtonSecondsForDiagnostics().Equals(
			Receiver->GetAppliedLineImpulseNewtonSecondsForDiagnostics() + Receiver->GetDiscardedLineImpulseNewtonSecondsForDiagnostics()
			+ Receiver->GetQueuedLineImpulseNewtonSecondsForDiagnostics(), 1.e-6));
	TestTrue(TEXT("parked receiver consumes the current force segment"), Receiver->GetAppliedLineImpulseNewtonSecondsForDiagnostics().Equals(FVector(2, 0, 0), 1.e-6));
	Receiver->ClearLineLoad(SecondSessionId);
	Service->Sessions.Reset();
	Session->Snapshot.Phase = ECatFishingPhase::Terminated;
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingPhysicalCouplingTest,
	"Catfishing.Unit.Fishing.PhysicalRod.OwnerMouseTorqueAndHeavyFishCoupleAt60And120Hz",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatFishingPhysicalCouplingTest::RunTest(const FString& Parameters)
{
	TArray<double> FinalDistances;
	TArray<double> TinyTowDistances;
	for (const int32 Rate : {60, 120})
	{
		FTestWorldWrapper Wrapper;
		if (!TestTrue(FString::Printf(TEXT("%d Hz coupling world creation"), Rate), Wrapper.CreateTestWorld(EWorldType::Game))) return false;
		Wrapper.ForwardErrorMessages(this);
		UWorld* World = Wrapper.GetTestWorld();
		FURL URL;
		URL.AddOption(TEXT("game=/Script/Catfishing.CatfishingGameModeBase"));
		if (!TestTrue(TEXT("coupling authority game mode"), World->SetGameMode(URL))) return false;
		auto* Floor = World->SpawnActor<AStaticMeshActor>();
		Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Floor->SetActorLocation(FVector(0, 0, -10));
		Floor->SetActorScale3D(FVector(80, 80, 0.2));
		Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
		if (!TestTrue(TEXT("coupling world begins physics"), Wrapper.BeginPlayInTestWorld())) return false;
		auto* GameMode = World->GetAuthGameMode<ACatfishingGameModeBase>();
		GameMode->bRunCommandsOpen = true;
		GameMode->RunPublicState.Phase.Phase = ECatRunPhase::DayActive;
		GameMode->RunPublicState.Phase.bNewFishingBitesAllowed = true;
		TArray<ACatCharacter*> Cats;
		TArray<ACatfishingPlayerState*> Players;
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		for (int32 Index = 0; Index < 2; ++Index)
		{
			auto* Controller = World->SpawnActor<ACatfishingPlayerController>();
			auto* Player = World->SpawnActor<ACatfishingPlayerState>();
			auto* Cat = World->SpawnActor<ACatCharacter>(FVector(Index * 40, 0, 20), FRotator::ZeroRotator, Spawn);
			if (!TestTrue(TEXT("coupling participant spawns controller, player state and body"), Controller && Player && Cat)) return false;
			Controller->PlayerState = Player; Cat->SetPlayerState(Player); Controller->Possess(Cat);
			Player->SetPlayerId(Index + 1);
			const FUniqueNetIdRef NetId = FUniqueNetIdString::Create(FString::Printf(TEXT("PhysicalCoupling%d"), Index), FName(TEXT("CAT_TEST")));
			Player->SetUniqueId(FUniqueNetIdRepl(NetId));
			ACatfishingGameModeBase::FAdmissionRecord Admission;
			Admission.Phase = ACatfishingGameModeBase::EAdmissionPhase::Active; Admission.Controller = Controller;
			GameMode->AdmissionRecords.Add(ACatfishingGameModeBase::MakeStableNetIdKey(Player->GetUniqueId()), Admission);
			auto* ASC = Cat->GetCatAbilitySystemComponent();
			ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(), 100);
			ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 100);
			ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 50);
			Cats.Add(Cat); Players.Add(Player);
		}
		const auto SpawnHeldRod = [&]() -> ACatFishingRodActor*
		{
			auto* Result = World->SpawnActorDeferred<ACatFishingRodActor>(ACatFishingRodActor::StaticClass(), FTransform::Identity);
			if (!Result || !Result->ConfigureCanonicalAnchorsFromAuthority(FTransform(FVector(60, 0, 0)), FTransform::Identity, FTransform::Identity)
				|| !Result->InitializeAuthoritativeIdentity(FGuid::NewGuid(), FGuid::NewGuid(), TEXT("CoupledRod"), NAME_None, Players[0], nullptr, true, false)) return nullptr;
			Result->FinishSpawning(FTransform::Identity);
			return Result->BeginPhysicalHoldFromAuthority(Players[0], true) && Result->SetPrimaryOperatorFromAuthority(Players[0], Result->GetPresentationState().RodActorRevision)
				&& Result->GetPhysicalRodComponent()->CommitPrimaryHold(Players[0]) ? Result : nullptr;
		};
		// These authority controllers have no LocalPlayer. Keep their accepted input alive exactly as
		// a connected client does; otherwise the production 0.5 s disconnect watchdog must release them.
		const auto TickConnectedWorld = [&](float DeltaSeconds)
		{
			for (ACatCharacter* Cat : Cats)
				if (IsValid(Cat) && !Cat->IsActorBeingDestroyed())
					Cat->GetPhysicalBodyComponent()->SetMoveIntent(Cat->GetPhysicalBodyComponent()->GetMoveIntent());
			Wrapper.TickTestWorld(DeltaSeconds);
		};
		auto* Rod = SpawnHeldRod();
		if (!TestNotNull(TEXT("primary hand holds an actual rigid rod"), Rod)) return false;
		auto* HelperBody = Cats[1]->GetPhysicalBodyComponent();
		const FVector HelperContact = Rod->GetPhysicalRodBody()->GetComponentTransform().TransformPosition(FVector(10, 0, .8));
		HelperBody->TeleportBodyFromAuthority(FTransform(FRotator::ZeroRotator, HelperBody->GetBody()->GetComponentLocation()
			+ HelperContact - HelperBody->GetHand(true)->GetComponentLocation()), TEXT("TorqueContactFixture"));
		FVector ClosestContact;
		const double ClosestDistance = Rod->GetPhysicalRodBody()->GetClosestPointOnCollision(HelperContact, ClosestContact);
		AddInfo(FString::Printf(TEXT("Event=controlled_rod_helper_contact Point=%s Closest=%s Distance=%.4f RodPose=%s"), *HelperContact.ToCompactString(), *ClosestContact.ToCompactString(), ClosestDistance, *Rod->GetPhysicalRodBody()->GetComponentTransform().ToHumanReadableString()));
		if (!TestTrue(TEXT("helper actually holds the same rod"), HelperBody->GetGrab()->GripFromAuthority(true, Rod->GetPhysicalRodBody(), HelperContact))) return false;
		Rod->RefreshPrimaryControlFromAuthority();
		if (!TestEqual(TEXT("two real grips retain only the explicit owner"), Rod->GetOperatorCount(), 1)) return false;
		Rod->SetFightConstraintObservationFromAuthority(FVector::ForwardVector, 0, 0, true, 0, 50);

		const FRotator InitialAim = Rod->GetGripWorldTransform().Rotator();
		const auto SendMouse = [&](int64 Sequence, bool bActive, double Yaw)
		{
			FCatFishingRodAimSample Sample;
			Sample.RodActorId = Rod->GetPresentationState().RodActorId;
			Sample.InputEpoch = Rod->GetCarrierConstraintState().AimInputEpoch;
			Sample.Sequence = Sequence; Sample.MouseStrokeSequence = 1;
			Sample.bMouseActive = bActive; Sample.CumulativeLookDegrees = FVector2D(Yaw, 0);
			return Rod->AcceptHeldAimSampleFromAuthority(Players[0], Sample);
		};
		for (int32 Frame = 0; Frame < Rate; ++Frame)
		{
			if (!TestTrue(FString::Printf(TEXT("%d Hz owner mouse sample %d"), Rate, Frame), SendMouse(Frame + 1, true, (Frame + 1) * 60.0 / Rate))) return false;
			TickConnectedWorld(1.0f / Rate);
		}
		const double Turn = FMath::Abs(FMath::FindDeltaAngleDegrees(InitialAim.Yaw, Rod->GetGripWorldTransform().Rotator().Yaw));
		TestTrue(TEXT("the owner actually turns the physical rod with its own strength"), Turn > 5);
		TestTrue(TEXT("the active owner motor records real effort"), Rod->GetAuthoritativeRotationEffortSnapshot().ExertionSquaredSeconds > 0);
		Cats[0]->GetCatAbilitySystemComponent()->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 0);
		const auto EffortBeforeZero = Rod->GetAuthoritativeRotationEffortSnapshot();
		for (int32 Frame = 0; Frame < Rate / 4; ++Frame)
		{
			if (!TestTrue(FString::Printf(TEXT("%d Hz exhausted mouse sample %d"), Rate, Frame), SendMouse(Rate + Frame + 1, true, 65.0 + Frame))) return false;
			TickConnectedWorld(1.0f / Rate);
		}
		TestEqual(TEXT("an exhausted owner cannot borrow mouse torque from a healthy physical assistant"),
			Rod->GetAuthoritativeRotationEffortSnapshot().ExertionSquaredSeconds, EffortBeforeZero.ExertionSquaredSeconds);
		Rod->Destroy(); Cats[1]->Destroy();
		Cats.RemoveAt(1); // The later single-cat stages can cross GC; do not retain the destroyed helper.
		auto* CatBody = Cats[0]->GetPhysicalBodyComponent();
		CatBody->TeleportBodyFromAuthority(FTransform(FRotator::ZeroRotator, FVector(0, 0, 20)), TEXT("CouplingInitialPose"));
		Cats[0]->GetCatAbilitySystemComponent()->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 100);
		Rod = SpawnHeldRod();
		if (!TestNotNull(TEXT("heavy-fish stage creates a new physically held rod"), Rod)) return false;
		Rod->SetFightConstraintObservationFromAuthority(FVector::ForwardVector, 1, 0, true);
		for (int32 Frame = 0; Frame < Rate / 2; ++Frame) TickConnectedWorld(1.0f / Rate);
		if (!TestTrue(TEXT("cat settles on actual floor support"), CatBody->IsGrounded())) return false;
		auto* Service = World->GetSubsystem<UCatFishingService>();
		auto* Session = World->SpawnActor<ACatFishingSession>();
		const FGuid SessionId = FGuid::NewGuid();
		Session->Snapshot.FishingSessionId = SessionId; Session->Snapshot.RodActor = Rod;
		Service->Sessions.Add(SessionId, Session);
		FCatFightSimulationConfig Config;
		Config.FixedStepSeconds = 0.05; Config.PrimaryOperatorCatStrength = 50;
		Config.PrimaryOperatorMassKilograms = CatBody->GetBody()->GetMass();
		Config.FishMassKilograms = 30; Config.FishStrength = 300; Config.CatStaminaMaximum = 100;
		Config.ReelSpeedCentimetersPerSecond = 80; Config.FishFullEffortSpeedCentimetersPerSecond = 75;
		Config.MaximumLineLengthCentimeters = 1000; Config.RodDurability = 100000;
		FCatFightSimulationState State;
		State.CatStamina = 100; State.FishStamina = 1000; State.LineLengthCentimeters = 500;
		const FVector Outward = FVector(1, 0.4, 0).GetSafeNormal();
		State.FishWorldPosition = Rod->GetRodTipWorldTransform().GetLocation() + Outward * 500;
		State.MotionIntent = ECatFishMotionIntent::StrugglingOutward;
		double MaximumTension = 0, MaximumLineError = 0, MaximumSpeed = 0, PreviousTension = 0, MaximumTensionJump = 0;
		double PeakSpeedSeconds = 0, PeakStretchSeconds = 0;
		FVector PeakTipVelocity = FVector::ZeroVector, PeakTipAcceleration = FVector::ZeroVector;
		FCatFightRodConstraintInput LastEndpoint;
		double Accumulator = 0;
		uint64 StepNumber = 0;
		const FVector Start = CatBody->GetBody()->GetComponentLocation();
		for (int32 Frame = 0; Frame < Rate * 8; ++Frame)
		{
			// The physics receiver gets one held force per fish fixed step; the actual body and shaft feed the next sample.
			Accumulator += 1.0 / Rate;
			while (Accumulator + 1.e-8 >= Config.FixedStepSeconds)
			{
				FCatFightRodConstraintInput Endpoint;
				Endpoint.bPhysicalRodEndpoint = Endpoint.bRodHeld = true;
				Endpoint.RodTipWorldPosition = Rod->GetRodTipWorldTransform().GetLocation();
				Endpoint.RodForwardWorld = Rod->GetAuthoritativeRodForwardVector();
				Endpoint.RodTipVelocityCentimetersPerSecond = Rod->GetAuthoritativeRodTipVelocity();
				Endpoint.CarrierVelocityCentimetersPerSecond = CatBody->GetVelocity();
				Rod->GetPhysicalRodComponent()->PopulateEndpointResponse(Endpoint);
				LastEndpoint = Endpoint;
				const auto Result = FCatFishingFightSimulator::Step(Config, State, Endpoint, Outward);
				if (!TestTrue(TEXT("heavy-fish coupled solve remains finite and accepted"), Result.bSucceeded)) return false;
				const double SampleSeconds = double(Frame + 1) / Rate;
				if (SampleSeconds >= 2.8 && SampleSeconds <= 3.6)
				{
					const FVector Shoulder = CatBody->GetGrab()->GetShoulderWorldLocation(true);
					const FVector Hand = CatBody->GetHand(true)->GetComponentLocation();
					AddInfo(FString::Printf(TEXT("Event=fishing_native_coupling_sample Hz=%d Seconds=%.6f TensionN=%.6f Tip=%s Fish=%s TipVelocity=%s TipAcceleration=%s PreviousForce=%s InverseMassX=%s ShoulderToHandCm=%.6f HeldReachCm=%.6f BodySpeedCmS=%.6f RodOmegaRadS=%.6f"),
						Rate, SampleSeconds, Result.LineTensionNewtons, *Endpoint.RodTipWorldPosition.ToCompactString(), *State.FishWorldPosition.ToCompactString(),
						*Endpoint.RodTipVelocityCentimetersPerSecond.ToCompactString(), *Endpoint.RodTipAccelerationCentimetersPerSecondSquared.ToCompactString(),
						*Endpoint.PreviousLineForceNewtons.ToCompactString(), *Endpoint.RodPointInverseMassX.ToCompactString(),
						FVector::Distance(Shoulder, Hand), CatBody->GetGrab()->GetGripState(true).HeldReachDistanceCm,
						CatBody->GetVelocity().Size(), Rod->GetPhysicalRodComponent()->GetAngularVelocityRadiansPerSecond().Size()));
				}
				State.FishWorldPosition = Result.ProposedFishWorldPosition;
				State.FishVelocityCentimetersPerSecond = Result.ResolvedFishVelocityCentimetersPerSecond;
				State.LineLengthCentimeters = Result.LineLengthCentimeters;
				MaximumTension = FMath::Max(MaximumTension, Result.LineTensionNewtons);
				MaximumTensionJump = FMath::Max(MaximumTensionJump, FMath::Abs(Result.LineTensionNewtons - PreviousTension));
				PreviousTension = Result.LineTensionNewtons;
				Rod->GetPhysicalRodComponent()->SetLineLoad(SessionId, ++StepNumber,
					Result.RodLineForceNewtons, Config.FixedStepSeconds, 0.15);
				Accumulator -= Config.FixedStepSeconds;
			}
			TickConnectedWorld(1.0f / Rate);
			const double Stretch = FMath::Max(0.0, FVector::Distance(State.FishWorldPosition, Rod->GetRodTipWorldTransform().GetLocation()) - State.LineLengthCentimeters);
			if (Stretch > MaximumLineError) { MaximumLineError = Stretch; PeakStretchSeconds = double(Frame + 1) / Rate; }
			const double Speed = Rod->GetPhysicalRodComponent()->GetPointVelocity(Rod->GetPhysicalRodBody()->GetComponentLocation()).Size();
			if (Speed > MaximumSpeed)
			{
				MaximumSpeed = Speed; PeakSpeedSeconds = double(Frame + 1) / Rate;
				PeakTipVelocity = LastEndpoint.RodTipVelocityCentimetersPerSecond;
				PeakTipAcceleration = LastEndpoint.RodTipAccelerationCentimetersPerSecondSquared;
			}
			if (Frame == Rate * 4) CatBody->SetMoveIntent(FVector(-1, 1, 0).GetSafeNormal());
		}
		const double Travel = FVector::Dist2D(Start, CatBody->GetBody()->GetComponentLocation());
		FinalDistances.Add(Travel);
		TestTrue(TEXT("heavy line load reaches the cat through the actual hand constraint"), Travel > 10);
		TestTrue(TEXT("the continuous line has bounded stretch"), MaximumLineError < 25);
		TestTrue(TEXT("heavy fish never causes explosive alternating tension"), MaximumTension < 1200 && MaximumTensionJump < 900);
		TestTrue(TEXT("the actual shaft never flies off at an unbounded speed"), MaximumSpeed < 1000);
		TestTrue(TEXT("continuous force preserves the actual primary hold"), CatBody->GetGrab()->IsGripping(true) && Rod->GetOperatorCount() == 1);
		AddInfo(FString::Printf(TEXT("Event=fishing_physical_coupling_observed Hz=%d Seconds=8 FishMassKg=30 OwnerActiveTurnDegrees=%.3f MaximumTensionN=%.3f MaximumTensionJumpN=%.3f MaximumLineErrorCm=%.3f MaximumRodSpeedCmS=%.3f BodyTravelCm=%.3f"),
			Rate, Turn, MaximumTension, MaximumTensionJump, MaximumLineError, MaximumSpeed, Travel));
		AddInfo(FString::Printf(TEXT("Event=fishing_physical_coupling_peak_observed Hz=%d PeakSpeedSeconds=%.6f PeakStretchSeconds=%.6f LastTipVelocityCmS=%s LastTipAccelerationCmS2=%s"),
			Rate, PeakSpeedSeconds, PeakStretchSeconds, *PeakTipVelocity.ToCompactString(), *PeakTipAcceleration.ToCompactString()));
		Rod->GetPhysicalRodComponent()->ClearLineLoad(SessionId);
		// Existing zero-stamina gameplay adds finite fish thrust; the real line and hand must tow the cat.
		double WithTowTravel = 0, WithTowEndSpeed = 0;
		for (int32 TowCase = 0; TowCase < 3; ++TowCase)
		{
			const bool bTowEnabled = TowCase != 1;
			const bool bStaticAnchor = TowCase == 2;
			Rod->Destroy();
			CatBody->TeleportBodyFromAuthority(FTransform(FRotator::ZeroRotator, FVector(0, 0, 20)), TEXT("TinyFishInitialPose"));
			CatBody->SetMoveIntent(FVector::ZeroVector);
			Cats[0]->GetCatAbilitySystemComponent()->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 0);
			Rod = SpawnHeldRod();
			if (!TestNotNull(TEXT("tiny fish stage starts with an actual owner hand constraint"), Rod)) return false;
			Rod->SetFightConstraintObservationFromAuthority(FVector::ForwardVector, 1, 0, true);
			Session->Snapshot.RodActor = Rod;
			for (int32 Frame = 0; Frame < Rate / 2; ++Frame) TickConnectedWorld(1.0f / Rate);
			AActor* TowAnchor = nullptr;
			if (bStaticAnchor)
			{
				TowAnchor = World->SpawnActor<AActor>();
				auto* AnchorBody = NewObject<UBoxComponent>(TowAnchor);
				TowAnchor->SetRootComponent(AnchorBody); TowAnchor->AddInstanceComponent(AnchorBody);
				AnchorBody->SetBoxExtent(FVector(5)); AnchorBody->SetCollisionProfileName(TEXT("BlockAll")); AnchorBody->RegisterComponent();
				const FVector Contact = CatBody->GetHand(false)->GetComponentLocation();
				AnchorBody->SetWorldLocation(Contact);
				if (!TestTrue(TEXT("zero-stamina owner holds both the real rod and a real static anchor before load begins"),
					CatBody->GetGrab()->GripFromAuthority(false, AnchorBody, Contact) && CatBody->GetGrab()->IsGripping(true))) return false;
			}
			Config.PrimaryOperatorCatStrength = 0;
			Config.FishMassKilograms = 0.04; Config.FishStrength = 0.4;
			Config.FishFullEffortSpeedCentimetersPerSecond = 180;
			Config.MaximumLineLengthCentimeters = 10000;
			Config.ExhaustedCatTowAccelerationCentimetersPerSecondSquared = bTowEnabled ? 300 : 0;
			State = {};
			State.CatStamina = 0; State.FishStamina = 1000; State.LineLengthCentimeters = 500;
			State.FishWorldPosition = Rod->GetRodTipWorldTransform().GetLocation() + FVector::ForwardVector * 500;
			State.MotionIntent = ECatFishMotionIntent::StrugglingOutward;
			const FVector TinyStart = CatBody->GetBody()->GetComponentLocation();
			double TinyMaximumTension = 0, TinyMaximumStretch = 0, TinyMaximumSpeed = 0;
			int32 TinyLoadedSteps = 0, TinyTotalSteps = 0;
			Accumulator = 0; StepNumber = 0;
			for (int32 Frame = 0; Frame < Rate * 10; ++Frame)
			{
				Accumulator += 1.0 / Rate;
				while (Accumulator + 1.e-8 >= Config.FixedStepSeconds)
				{
					FCatFightRodConstraintInput Endpoint;
					Endpoint.bPhysicalRodEndpoint = Endpoint.bRodHeld = true;
					Endpoint.RodTipWorldPosition = Rod->GetRodTipWorldTransform().GetLocation();
					Endpoint.RodForwardWorld = Rod->GetAuthoritativeRodForwardVector();
					Endpoint.RodTipVelocityCentimetersPerSecond = Rod->GetAuthoritativeRodTipVelocity();
					Endpoint.CarrierVelocityCentimetersPerSecond = CatBody->GetVelocity();
					Rod->GetPhysicalRodComponent()->PopulateEndpointResponse(Endpoint);
					const auto Result = FCatFishingFightSimulator::Step(Config, State, Endpoint, FVector::ForwardVector);
					if (!TestTrue(TEXT("tiny fish coupled solve preserves finite forced escape"), Result.bSucceeded && Result.bExhaustedCatEscape)) return false;
					State.FishWorldPosition = Result.ProposedFishWorldPosition;
					State.FishVelocityCentimetersPerSecond = Result.ResolvedFishVelocityCentimetersPerSecond;
					State.LineLengthCentimeters = Result.LineLengthCentimeters;
					TinyMaximumTension = FMath::Max(TinyMaximumTension, Result.LineTensionNewtons);
					++TinyTotalSteps; if (Result.LineTensionNewtons > 0.01) ++TinyLoadedSteps;
					Rod->GetPhysicalRodComponent()->SetLineLoad(SessionId, ++StepNumber,
						Result.RodLineForceNewtons, Config.FixedStepSeconds, 0.15);
					Accumulator -= Config.FixedStepSeconds;
				}
				TickConnectedWorld(1.0f / Rate);
				TinyMaximumStretch = FMath::Max(TinyMaximumStretch, FMath::Max(0.0,
					FVector::Distance(State.FishWorldPosition, Rod->GetRodTipWorldTransform().GetLocation()) - State.LineLengthCentimeters));
				TinyMaximumSpeed = FMath::Max(TinyMaximumSpeed, CatBody->GetVelocity().Size2D());
			}
			const double TinyTravel = FVector::Dist2D(TinyStart, CatBody->GetBody()->GetComponentLocation());
			const double TinyEndSpeed = CatBody->GetVelocity().Size2D();
			AddInfo(FString::Printf(TEXT("Event=fishing_tiny_fish_physical_tow_observed Hz=%d Seconds=10 FishMassKg=0.04 FishStrength=0.4 OperatorStamina=0 TowAccelerationCmS2=%.0f StaticGrip=%d BodyTravelCm=%.3f EndSpeedCmS=%.3f MaximumSpeedCmS=%.3f MaximumTensionN=%.3f MaximumStretchCm=%.3f LoadedSteps=%d TotalSteps=%d Grip=%d"),
				Rate, Config.ExhaustedCatTowAccelerationCentimetersPerSecondSquared, bStaticAnchor, TinyTravel, TinyEndSpeed, TinyMaximumSpeed, TinyMaximumTension, TinyMaximumStretch, TinyLoadedSteps, TinyTotalSteps, CatBody->GetGrab()->IsGripping(true)));
			if (bTowEnabled && !bStaticAnchor)
			{
				WithTowTravel = TinyTravel; WithTowEndSpeed = TinyEndSpeed; TinyTowDistances.Add(TinyTravel);
				TestEqual(TEXT("finite extra thrust keeps the tiny fish loaded for every fixed step"), TinyLoadedSteps, TinyTotalSteps);
				TestTrue(TEXT("exhausted-cat tow remains faster than the ordinary full-effort swim reference despite physical losses"), TinyEndSpeed > Config.FishFullEffortSpeedCentimetersPerSecond);
			}
			else if (!bStaticAnchor)
			{
				TestTrue(TEXT("the existing finite extra thrust, rather than passive drift, causes fast continuous towing"),
					WithTowTravel > 10.0 * FMath::Max(1.0, TinyTravel) && WithTowEndSpeed > TinyEndSpeed * 2.0);
				AddInfo(FString::Printf(TEXT("Event=fishing_tiny_tow_control_comparison Hz=%d EnabledTravelCm=%.3f DisabledTravelCm=%.3f EnabledSpeedCmS=%.3f DisabledSpeedCmS=%.3f SameMassDampingAndFriction=true"),
					Rate, WithTowTravel, TinyTravel, WithTowEndSpeed, TinyEndSpeed));
			}
			if (bStaticAnchor)
			{
				TestTrue(TEXT("a real static hand constraint resists the same finite exhausted-cat towing force"),
					TinyTravel < WithTowTravel * 0.1 && TinyEndSpeed < WithTowEndSpeed * 0.25
					&& TinyLoadedSteps > TinyTotalSteps / 2 && CatBody->GetGrab()->IsGripping(false));
				AddInfo(FString::Printf(TEXT("Event=fishing_tiny_tow_static_constraint_observed Hz=%d FreeTravelCm=%.3f AnchoredTravelCm=%.3f FreeSpeedCmS=%.3f AnchoredSpeedCmS=%.3f AnchorGrip=%d"),
					Rate, WithTowTravel, TinyTravel, WithTowEndSpeed, TinyEndSpeed, CatBody->GetGrab()->IsGripping(false)));
			}
			TestTrue(TEXT("tiny fish retains finite physical travel and line error"), TinyMaximumSpeed < 500 && TinyMaximumStretch < 25);
			TestTrue(TEXT("tiny fish force passes through a surviving actual hand constraint"), CatBody->GetGrab()->IsGripping(true) && Rod->GetOperatorCount() == 1);
			Rod->GetPhysicalRodComponent()->ClearLineLoad(SessionId);
			if (TowAnchor) { CatBody->GetGrab()->ReleaseHandFromAuthority(false, TEXT("TinyTowAnchorCleanup")); TowAnchor->Destroy(); }
		}
		Service->Sessions.Reset(); Session->Snapshot.Phase = ECatFishingPhase::Terminated;
	}
	if (FinalDistances.Num() == 2) TestTrue(TEXT("60 and 120 Hz retain comparable real body travel"),
		FMath::Abs(FinalDistances[0] - FinalDistances[1]) < FMath::Max(35.0, 0.3 * FMath::Max(FinalDistances[0], FinalDistances[1])));
	if (TinyTowDistances.Num() == 2) TestTrue(TEXT("tiny-fish physical tow is consistent across 60 and 120 Hz"),
		FMath::Abs(TinyTowDistances[0] - TinyTowDistances[1]) < 0.05 * FMath::Max(TinyTowDistances[0], TinyTowDistances[1]));
	return !HasAnyErrors();
}

#endif
