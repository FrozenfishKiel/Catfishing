#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Fishing/Integration/CatFishingPhysicalRodComponent.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "OnlineSubsystemTypes.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/Simulation/CatFishingRodResistanceModel.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "GameFramework/PlayerState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingRodEffortSnapshotLifecycleTest,
	"Catfishing.Unit.Fishing.Actors.RodEffortSnapshotSurvivesSamplingAndResetsWithOwnerAndFight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingRodEffortSnapshotLifecycleTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
	Wrapper.ForwardErrorMessages(this);
	UWorld* World=Wrapper.GetTestWorld();
	FURL URL; URL.AddOption(TEXT("game=/Script/Catfishing.CatfishingGameModeBase"));
	if (!World->SetGameMode(URL) || !Wrapper.BeginPlayInTestWorld()) return false;
	auto* GameMode=World->GetAuthGameMode<ACatfishingGameModeBase>();
	GameMode->bRunCommandsOpen=true; GameMode->RunPublicState.Phase.Phase=ECatRunPhase::DayActive; GameMode->RunPublicState.Phase.bFishingAllowed=true;
	auto* Floor=World->SpawnActor<AStaticMeshActor>();
	Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
	Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
	Floor->SetActorTransform(FTransform(FRotator::ZeroRotator,FVector(0,0,-10),FVector(20,20,.2)));
	Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
	TArray<ACatCharacter*> Cats; TArray<ACatfishingPlayerState*> Players;
	FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (int32 Index=0;Index<2;++Index)
	{
		auto* Controller=World->SpawnActor<ACatfishingPlayerController>();
		auto* Player=World->SpawnActor<ACatfishingPlayerState>();
		auto* Cat=World->SpawnActor<ACatCharacter>(FVector(0,Index*70,20),FRotator::ZeroRotator,Spawn);
		if (!Controller || !Player || !Cat) return false;
		Controller->PlayerState=Player; Cat->SetPlayerState(Player); Controller->Possess(Cat); Controller->SetActorTickEnabled(false);
		const FUniqueNetIdRef StableId=FUniqueNetIdString::Create(FString::Printf(TEXT("Effort%d"),Index),FName(TEXT("CAT_TEST")));
		Player->SetUniqueId(FUniqueNetIdRepl(StableId));
		ACatfishingGameModeBase::FAdmissionRecord Admission; Admission.Phase=ACatfishingGameModeBase::EAdmissionPhase::Active; Admission.Controller=Controller;
		GameMode->AdmissionRecords.Add(ACatfishingGameModeBase::MakeStableNetIdKey(Player->GetUniqueId()),Admission);
		Cats.Add(Cat); Players.Add(Player);
	}
	auto* Rod=World->SpawnActor<ACatFishingRodActor>();
	if (!Rod || !Rod->InitializeAuthoritativeIdentity(FGuid::NewGuid(),FGuid::NewGuid(),TEXT("EffortRod"),NAME_None,Players[0],nullptr,true,false)
		|| !Rod->BeginPhysicalHoldFromAuthority(Players[0],true)
		|| !Rod->SetPrimaryOperatorFromAuthority(Players[0],Rod->GetPresentationState().RodActorRevision)) return false;
	if (!TestTrue(TEXT("real primary rod constraint exists"),Cats[0]->GetPhysicalBodyComponent()->GetGrab()->IsGripping(true))) return false;
	Rod->SetFightConstraintObservationFromAuthority(FVector::ForwardVector, 1, 0, true, 100, 50);
	FCatFishingRodAimSample Mouse; Mouse.RodActorId=Rod->GetPresentationState().RodActorId; Mouse.InputEpoch=Rod->GetCarrierConstraintState().AimInputEpoch;
	Mouse.Sequence=1; Mouse.bMouseActive=true; Mouse.MouseStrokeSequence=1; Mouse.CumulativeLookDegrees.X=45;
	const auto Tick=[&]() { for (auto* Cat:Cats) Cat->GetPhysicalBodyComponent()->SetMoveIntent(FVector::ZeroVector); Wrapper.TickTestWorld(1.0f/60.0f); Rod->RefreshHeldTransformFromAuthority(); };
	for (int32 Frame=0;Frame<12;++Frame)
	{ ++Mouse.Sequence; Mouse.CumulativeLookDegrees.X+=.1; if (!Rod->AcceptHeldAimSampleFromAuthority(Players[0],Mouse)) return false; Tick(); }
	FCatFishingRodControlObservation Actual;
	if (!TestTrue(TEXT("reads authoritative rigid-body observation"),Rod->GetControlObservationFromAuthority(Actual))) return false;
	TestTrue(TEXT("observed angular velocity comes from actual Chaos body"),Actual.AngularVelocityRadiansPerSecond.Equals(Rod->GetPhysicalRodBody()->GetPhysicsAngularVelocityInRadians(),1.e-8));
	const auto First=Rod->GetAuthoritativeRotationEffortSnapshot();
	TestTrue(TEXT("real active motor accumulates support effort"),First.ExertionSquaredSeconds>0);
	TestTrue(TEXT("real rotation accumulates positive work"),First.PositiveWorkRadians>0);
	const auto Repeated=Rod->GetAuthoritativeRotationEffortSnapshot();
	TestEqual(TEXT("reading cumulative intent cannot charge twice"),Repeated.ExertionSquaredSeconds,First.ExertionSquaredSeconds);
	TestEqual(TEXT("reading cumulative work cannot consume it"),Repeated.PositiveWorkRadians,First.PositiveWorkRadians);
	const FTransform BeforeRead=Rod->GetPhysicalRodBody()->GetComponentTransform();
	Rod->RefreshHeldTransformFromAuthority(1.0);
	TestTrue(TEXT("observation refresh cannot integrate a second physical pose"),Rod->GetPhysicalRodBody()->GetComponentTransform().Equals(BeforeRead,1.e-8));
	TestEqual(TEXT("observation refresh cannot integrate a second effort step"),Rod->GetAuthoritativeRotationEffortSnapshot().ExertionSquaredSeconds,First.ExertionSquaredSeconds);
	Mouse.bMouseActive=false; ++Mouse.Sequence; Rod->AcceptHeldAimSampleFromAuthority(Players[0],Mouse);
	const auto Passive=Rod->GetAuthoritativeRotationEffortSnapshot();
	for (int32 Frame=0;Frame<12;++Frame) Tick();
	TestEqual(TEXT("passive inertia does not charge support"),Rod->GetAuthoritativeRotationEffortSnapshot().ExertionSquaredSeconds,Passive.ExertionSquaredSeconds);
	TestEqual(TEXT("passive inertia does not charge positive work"),Rod->GetAuthoritativeRotationEffortSnapshot().PositiveWorkRadians,Passive.PositiveWorkRadians);
	const FVector Momentum=Rod->GetPhysicalRodBody()->GetPhysicsAngularVelocityInRadians();
	Rod->ClearFightConstraintAndLoadFromAuthority();
	const auto Cleared=Rod->GetAuthoritativeRotationEffortSnapshot();
	TestTrue(TEXT("fight cleanup changes effort epoch"),Cleared.Epoch>First.Epoch);
	TestEqual(TEXT("fight cleanup drops prior effort"),Cleared.ExertionSquaredSeconds,0.0);
	TestTrue(TEXT("fight cleanup preserves actual angular momentum"),Rod->GetPhysicalRodBody()->GetPhysicsAngularVelocityInRadians().Equals(Momentum,1.e-8));
	Rod->SetFightConstraintObservationFromAuthority(FVector::ForwardVector, 1, 0, true, 100, 50);
	Mouse.InputEpoch=Rod->GetCarrierConstraintState().AimInputEpoch; Mouse.bMouseActive=true; ++Mouse.MouseStrokeSequence; Mouse.MouseStrokeStartLookDegrees=Mouse.CumulativeLookDegrees;
	for (int32 Frame=0;Frame<6;++Frame) { ++Mouse.Sequence; Mouse.CumulativeLookDegrees.X+=2; if (!Rod->AcceptHeldAimSampleFromAuthority(Players[0],Mouse)) return false; Tick(); }
	const auto Restarted=Rod->GetAuthoritativeRotationEffortSnapshot();
	TestTrue(TEXT("new fight collects only fresh motor effort"),Restarted.Epoch>Cleared.Epoch && Restarted.ExertionSquaredSeconds>0);
	// Position a free helper at the actual rod contact; the normal grip validation still checks geometry and eligibility.
	auto* NextBody=Cats[1]->GetPhysicalBodyComponent();
	const FVector Contact=Rod->GetGripWorldTransform().GetLocation();
	const FQuat HelperRotation=Cats[1]->GetActorQuat();
	NextBody->TeleportBodyFromAuthority(FTransform(HelperRotation,Contact-HelperRotation.RotateVector(UCatPhysicsGrabComponent::RestHandLocal(true))),TEXT("EffortHelperContact"));
	if (!TestTrue(TEXT("helper establishes a real direct rod grip"),NextBody->GetGrab()->GripFromAuthority(true,Rod->GetPhysicalRodBody(),Contact))) return false;
	Rod->GetPhysicalRodComponent()->RefreshPrimaryControl();
	TestEqual(TEXT("helper contact does not reset owner effort epoch"),Rod->GetAuthoritativeRotationEffortSnapshot().Epoch,Restarted.Epoch);
	TestEqual(TEXT("helper contact does not create another fishing operator"),Rod->GetOperatorCount(),1);
	TestTrue(TEXT("explicit owner remains the only operator"),Rod->IsPrimaryOperator(Players[0])&&!Rod->IsPrimaryOperator(Players[1]));
	TestFalse(TEXT("physically connected helper cannot turn through the owner's input protocol"),Rod->AcceptHeldAimSampleFromAuthority(Players[1],Mouse));
	const FTransform BeforeRelease=Rod->GetPhysicalRodBody()->GetComponentTransform();
	const FVector BeforeReleaseMomentum=Rod->GetPhysicalRodBody()->GetPhysicsAngularVelocityInRadians();
	Cats[0]->GetPhysicalBodyComponent()->GetGrab()->ReleaseAllFromAuthority(TEXT("EffortOwnerRelease"));
	Rod->GetPhysicalRodComponent()->RefreshPrimaryControl();
	TestEqual(TEXT("owner release leaves no fishing operator"),Rod->GetOperatorCount(),0);
	TestTrue(TEXT("helper's real grip survives owner release"),NextBody->GetGrab()->IsGripping(true)&&NextBody->GetGrab()->GetGripTarget(true)==Rod);
	TestFalse(TEXT("remaining physical helper never automatically takes control"),Rod->IsPrimaryOperator(Players[1]));
	const auto Released=Rod->GetAuthoritativeRotationEffortSnapshot();
	TestTrue(TEXT("owner release ends the former effort epoch"),Released.Epoch>Restarted.Epoch);
	TestEqual(TEXT("uncontrolled rod cannot retain a former owner bill"),Released.ExertionSquaredSeconds,0.0);
	FCatFishingRodControlObservation Observation; Rod->GetControlObservationFromAuthority(Observation);
	TestFalse(TEXT("no operator leaves no active mouse motor"),Observation.bMouseDriveActive);
	TestTrue(TEXT("owner release cannot teleport the physically held rod"),Rod->GetPhysicalRodBody()->GetComponentTransform().Equals(BeforeRelease,1.e-8));
	TestTrue(TEXT("owner release preserves real angular velocity"),Observation.AngularVelocityRadiansPerSecond.Equals(BeforeReleaseMomentum,1.e-8));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingRodEffortFixedStepAllocationTest,
	"Catfishing.Unit.Fishing.Simulation.RodEffortSplitsSlowFramesAndDiscardsPreviousHolderBacklog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingRodEffortFixedStepAllocationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFishingRodEffortSampler Sampler;
	const FCatFishingRodRotationEffortSnapshot Baseline{7, 0.2, 0.01, 2.0};
	Sampler.Reset(Baseline);
	const auto Existing = Sampler.Consume(Baseline, 0.05);
	TestEqual(TEXT("initial baseline cannot charge effort collected before attachment"),
		Existing.ExertionSquaredSeconds, 0.0);
	const FCatFishingRodRotationEffortSnapshot SlowFrame{7, 0.25, 0.03, 2.1};
	const auto FirstStep = Sampler.Consume(SlowFrame, 0.05);
	const auto SecondStep = Sampler.Consume(SlowFrame, 0.05);
	for (const auto& Step : {FirstStep, SecondStep})
	{
		TestEqual(TEXT("each 50 ms catch-up step receives half the 100 ms intent"),
			Step.ExertionSquaredSeconds, 0.025, 1e-8);
		TestEqual(TEXT("each catch-up step receives half the realized work"),
			Step.PositiveWorkRadians, 0.01, 1e-8);
		TestEqual(TEXT("each catch-up step accounts for its own 50 ms"),
			Step.IntegratedSeconds, 0.05, 1e-8);
	}
	const auto Exhausted = Sampler.Consume(SlowFrame, 0.05);
	TestEqual(TEXT("reading an exhausted snapshot cannot fabricate more effort"),
		Exhausted.ExertionSquaredSeconds, 0.0);
	TestEqual(TEXT("reading an exhausted snapshot cannot fabricate elapsed time"),
		Exhausted.IntegratedSeconds, 0.0);

	const FCatFishingRodRotationEffortSnapshot MoreBacklog{7, 0.35, 0.07, 2.2};
	Sampler.Consume(MoreBacklog, 0.05);
	const FCatFishingRodRotationEffortSnapshot NewHolder{8, 0.03, 0.01, 0.03};
	const auto Handoff = Sampler.Consume(NewHolder, 0.05);
	TestEqual(TEXT("new epoch retains only the new holder's effort"), Handoff.ExertionSquaredSeconds, 0.03, 1e-8);
	TestEqual(TEXT("new epoch discards previous holder's remaining realized work"),
		Handoff.PositiveWorkRadians, 0.01, 1e-8);
	TestEqual(TEXT("a partially covered step never invents integration time"), Handoff.IntegratedSeconds, 0.03, 1e-8);
	TestEqual(TEXT("new holder effort is consumed exactly once"),
		Sampler.Consume(NewHolder, 0.05).ExertionSquaredSeconds, 0.0);

	const FCatFishingRodRotationEffortSnapshot NewBacklog{8, 0.08, 0.05, 0.13};
	Sampler.Consume(NewBacklog, 0.05);
	Sampler.Reset(NewBacklog);
	TestEqual(TEXT("explicit reset also drops any pending effort"),
		Sampler.Consume(NewBacklog, 0.05).ExertionSquaredSeconds, 0.0);
	return !HasAnyErrors();
}

#endif
