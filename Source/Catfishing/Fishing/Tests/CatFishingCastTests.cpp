#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Fishing/Actors/CatFishingHookActor.h"
#include "Fishing/Integration/CatFishingAimLibrary.h"
#include "Fishing/Presentation/CatFishingPresentationSettings.h"
#include "Fishing/Simulation/CatFishingCastTrajectory.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingCastTrajectoryTest,
	"Catfishing.Unit.Fishing.Cast.TrajectoryArrivesWithoutSnap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingCastTrajectoryTest::RunTest(const FString& Parameters)
{
	for (const FVector Target : { FVector(100.0, 0.0, -100.0), FVector(1000.0, 250.0, -100.0),
		FVector(2000.0, -500.0, -100.0), FVector(800.0, 0.0, 250.0) })
	{
		FCatFishingCastTrajectory Flight;
		const FVector Origin(0.0, 0.0, 100.0);
		TestTrue(TEXT("valid near/far/higher water flight"), Flight.Initialize(Origin, Target, -980.0, 10.0));
		TestTrue(TEXT("launch rises"), Flight.InitialVelocity.Z > 0.0);
		TestTrue(TEXT("arrives descending"), Flight.InitialVelocity.Z + Flight.GravityZ * Flight.DurationSeconds < 0.0);
		TestTrue(TEXT("starts at rod"), Flight.Evaluate(10.0).Equals(Origin));
		const double EndTime = 10.0 + Flight.DurationSeconds;
		TestTrue(TEXT("unclamped ballistic solution reaches water"),
			(Origin + Flight.InitialVelocity * Flight.DurationSeconds
				+ FVector(0, 0, 0.5 * Flight.GravityZ * FMath::Square(Flight.DurationSeconds))).Equals(Target, 0.001));
		TestTrue(TEXT("no final teleport"), FVector::Dist(Flight.Evaluate(EndTime - 0.0001), Target) < 1.0);
		TestTrue(TEXT("long frame cannot overshoot"), Flight.Evaluate(EndTime + 0.5).Equals(Target));
		TestTrue(TEXT("pre-start clock does not move behind the rod"), Flight.Evaluate(9.0).Equals(Origin));
	}
	FCatFishingCastTrajectory Invalid;
	TestFalse(TEXT("invalid gravity rejected"), Invalid.Initialize(FVector::ZeroVector, FVector(100, 0, 0), 0.0, 0.0));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingCastViewRayTest,
	"Catfishing.Unit.Fishing.Cast.CameraRayPreservesDistanceAndRejectsInvalidInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingCastViewRayTest::RunTest(const FString& Parameters)
{
	const FVector Eyes(0, 0, 100), Camera(-400, 0, 400), Target(1000, 0, 0);
	const FVector Direction = (Target - Camera).GetSafeNormal();
	TestTrue(TEXT("third person camera ray accepted"), UCatFishingAimLibrary::IsCastViewRayValid(Camera, Direction, Eyes, Direction));
	const FVector CameraHit = Camera + Direction * (-Camera.Z / Direction.Z);
	const FVector OldEyeHit = Eyes + Direction * (-Eyes.Z / Direction.Z);
	TestTrue(TEXT("camera hits intended ten metre target"), CameraHit.Equals(Target));
	TestTrue(TEXT("old eye ray falls much nearer"), OldEyeHit.X < Target.X * 0.5);
	TestFalse(TEXT("forged distant camera rejected"), UCatFishingAimLibrary::IsCastViewRayValid(FVector(5000, 0, 400), Direction, Eyes, Direction));
	TestFalse(TEXT("backward ray rejected"), UCatFishingAimLibrary::IsCastViewRayValid(Camera, -Direction, Eyes, Direction));
	TestFalse(TEXT("zero ray rejected"), UCatFishingAimLibrary::IsCastViewRayValid(Camera, FVector::ZeroVector, Eyes, Direction));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingCastActorTest,
	"Catfishing.Unit.Fishing.Cast.HookFinishesFlightAndStopsMoving",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingCastActorTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Wrapper;
	if (!TestTrue(TEXT("create flight world"), Wrapper.CreateTestWorld(EWorldType::Game))) return false;
	if (!TestTrue(TEXT("begin play for real timer/frame lifecycle"), Wrapper.BeginPlayInTestWorld())) return false;
	Wrapper.ForwardErrorMessages(this);
	UWorld* World = Wrapper.GetTestWorld();
	UClass* HookClass = GetDefault<UCatFishingPresentationSettings>()->HookActorClass.LoadSynchronous();
	if (!TestNotNull(TEXT("formal hook Blueprint loads"), HookClass)) return false;
	ACatFishingHookActor* Hook = World->SpawnActor<ACatFishingHookActor>(HookClass);
	if (!TestNotNull(TEXT("hook spawned"), Hook)) return false;
	TestNull(TEXT("formal Blueprint has no obsolete projectile driver"), Hook->GetDefaultSubobjectByName(TEXT("ProjectileMovement")));
	Hook->SetActorLocation(FVector(0, 0, -50)); // 竿尖低于水面也不能立即定住。
	TestTrue(TEXT("identity initialized"), Hook->InitializeAuthoritativeIdentity(FGuid::NewGuid(), FGuid::NewGuid()));
	const FVector Landing(1000, 0, 0);
	TestTrue(TEXT("flight starts"), Hook->BeginAuthoritativeFlight(Landing));
	TestFalse(TEXT("flight does not compete with movement replication"), Hook->IsReplicatingMovement());
	TestEqual(TEXT("not landed prematurely"), Hook->GetPresentationState().Phase, ECatFishingHookPresentationPhase::CastFlight);
	for (int32 Frame = 0; Frame < 100; ++Frame) Wrapper.TickTestWorld(1.0f / 30.0f);
	TestEqual(TEXT("authoritative landing reached"), Hook->GetPresentationState().Phase, ECatFishingHookPresentationPhase::Landed);
	TestTrue(TEXT("exact landing"), Hook->GetActorLocation().Equals(Landing, 0.01));
	TestTrue(TEXT("landed hook resumes movement replication for fight"), Hook->IsReplicatingMovement());
	Wrapper.TickTestWorld(0.25f);
	TestTrue(TEXT("no drift after landing"), Hook->GetActorLocation().Equals(Landing, 0.01));
	TestFalse(TEXT("landing remains once only"), Hook->FinalizeAuthoritativeLandingOnce(true, FVector(2000, 0, 0)));
	return !HasAnyErrors();
}

#endif
