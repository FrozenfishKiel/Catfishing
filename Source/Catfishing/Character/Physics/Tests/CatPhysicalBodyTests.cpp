#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Tests/AutomationCommon.h"
#include "Character/Physics/CatPhysicsPrototypePawn.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"

namespace CatPhysicalBodyTest
{
struct FScene
{
	FTestWorldWrapper World;
	AStaticMeshActor* Floor = nullptr;
	AStaticMeshActor* AddBox(const FVector& Position, const FVector& HalfExtents)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		const FTransform Transform(FRotator::ZeroRotator, Position, HalfExtents / 50.0);
		AStaticMeshActor* Box = World.GetTestWorld()->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform);
		if (!Cube || !Box) return nullptr;
		Box->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		if (!Box->GetStaticMeshComponent()->SetStaticMesh(Cube)) return nullptr;
		Box->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Box->FinishSpawning(Transform);
		Box->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
		return Box;
	}
	bool Initialize(FAutomationTestBase* Test)
	{
		if (!World.CreateTestWorld(EWorldType::Game)) return false;
		World.ForwardErrorMessages(Test);
		World.GetTestWorld()->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
		Floor = AddBox(FVector(0, 0, -10), FVector(1000, 1000, 10));
		return Floor && World.BeginPlayInTestWorld();
	}
	ACatPhysicsPrototypePawn* Spawn(const FVector& Position, const FRotator& Rotation = FRotator::ZeroRotator)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World.GetTestWorld()->SpawnActor<ACatPhysicsPrototypePawn>(Position, Rotation, Params);
	}
};
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPhysicalMotorBudgetTest,
	"Catfishing.PhysicalBody.Runtime.StationarySupportInputAndZeroBudgetShareOneForceLimit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatPhysicalMotorBudgetTest::RunTest(const FString& Parameters)
{
	for (int32 Frequency : {60,120})
	{
		CatPhysicalBodyTest::FScene Scene;
		if (!TestTrue(TEXT("real physical world"),Scene.Initialize(this))) return false;
		auto* Cat=Scene.Spawn(FVector(0,0,20));
		if (!TestNotNull(TEXT("physical host"),Cat)) return false;
		auto* Body=Cat->FindComponentByClass<UCatPhysicalBodyComponent>();
		if (!TestNotNull(TEXT("shared physical implementation"),Body)) return false;
		auto Step=[&](int32 Frames) { for(int32 I=0;I<Frames;++I) Scene.World.TickTestWorld(1.0f/Frequency); };
		Step(Frequency);
		Body->SetFishingMotorBudget(Scene.Floor,5000,100); // 50 N, one voluntary-force budget.
		Body->SetExternalForceFromAuthority(Scene.Floor,FVector(1000,0,0)); // 10 N constant external load.
		const double InitialX=Cat->GetActorLocation().X;
		Step(Frequency*3);
		const double FirstX=Cat->GetActorLocation().X;
		Step(Frequency*2);
		const double HeldX=Cat->GetActorLocation().X;
		TestTrue(TEXT("weak constant load produces bounded give, not perpetual sliding"),FMath::Abs(HeldX-InitialX)<6);
		TestTrue(TEXT("stationary support converges under the same force budget"),FMath::Abs(HeldX-FirstX)<0.5);
		Body->SetMoveIntent(FVector(-1,0,0)); Step(Frequency);
		TestTrue(TEXT("direction input releases the hold point and can pull against the load"),Cat->GetActorLocation().X<HeldX-20);
		Body->SetMoveIntent(FVector::ZeroVector);
		Body->SetFishingMotorBudget(Scene.Floor,0,100);
		const double ExhaustedX=Cat->GetActorLocation().X;
		Step(Frequency*2);
		TestTrue(TEXT("zero stamina budget cannot retain its hold point against the external load"),Cat->GetActorLocation().X>ExhaustedX+30);
		AddInfo(FString::Printf(TEXT("Event=physical_body_motor_budget_verified Hz=%d HeldOffsetCm=%.3f LastTwoSecondsDriftCm=%.3f ExhaustedTravelCm=%.3f"),Frequency,HeldX-InitialX,HeldX-FirstX,Cat->GetActorLocation().X-ExhaustedX));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPhysicalSupportYieldTest,
	"Catfishing.PhysicalBody.Runtime.OverpoweredStanceSettlesLocallyWithoutReturningToOldFootPoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatPhysicalSupportYieldTest::RunTest(const FString& Parameters)
{
	for (const int32 Rate : {60, 120})
	{
		CatPhysicalBodyTest::FScene Scene;
		if (!TestTrue(TEXT("creates the physical stance world"), Scene.Initialize(this))) return false;
		auto* Cat = Scene.Spawn(FVector(0, 0, 20));
		auto* Body = Cat ? Cat->FindComponentByClass<UCatPhysicalBodyComponent>() : nullptr;
		if (!TestNotNull(TEXT("uses the shared physical body"), Body)) return false;
		const auto Step = [&](int32 Frames)
		{
			for (int32 I = 0; I < Frames; ++I) Scene.World.TickTestWorld(1.0f / Rate);
		};
		Step(Rate);
		Body->SetFishingMotorBudget(Scene.Floor, 5000, 100);
		Step(Rate / 2);
		const double OriginalX = Cat->GetActorLocation().X;
		Body->SetExternalForceFromAuthority(Scene.Floor, FVector(8000, 0, 0)); // 80 N exceeds the same 50 N stance budget.
		Step(Rate * 4 / 5);
		const double ReleaseX = Cat->GetActorLocation().X;
		TestTrue(TEXT("a stronger external force actually drags the body away from its planted point"), ReleaseX > OriginalX + 80);
		Body->ClearExternalForce(Scene.Floor);
		double FarthestX = ReleaseX;
		double MaximumReturnSpeed = 0;
		for (int32 Frame = 0; Frame < Rate * 3; ++Frame)
		{
			Step(1);
			FarthestX = FMath::Max(FarthestX, double(Cat->GetActorLocation().X));
			MaximumReturnSpeed = FMath::Max(MaximumReturnSpeed, -double(Body->GetVelocity().X));
		}
		const double FinalX = Cat->GetActorLocation().X;
		TestTrue(TEXT("removing the load never drives a high speed return to the old foot point"), MaximumReturnSpeed < 150);
		TestTrue(TEXT("the body settles near the new stopping point after its real momentum is braked"), FarthestX - FinalX < 30);
		TestTrue(TEXT("a yielded stance does not rewind the physical drag"), FinalX > OriginalX + 80);
		TestTrue(TEXT("the uncommanded body comes to rest"), Body->GetVelocity().Size() < 3);
		AddInfo(FString::Printf(TEXT("Event=physical_body_support_yield_verified Hz=%d DragDistanceCm=%.3f FinalDistanceCm=%.3f ReturnDistanceCm=%.3f MaximumReturnSpeedCmS=%.3f FinalSpeedCmS=%.3f"),
			Rate, ReleaseX - OriginalX, FinalX - OriginalX, FarthestX - FinalX, MaximumReturnSpeed, Body->GetVelocity().Size()));
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPhysicalJumpAndDisabledBodyTest,
	"Catfishing.PhysicalBody.Runtime.SharedLaunchMovesAllBodiesAndDisabledMotorStillFalls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatPhysicalJumpAndDisabledBodyTest::RunTest(const FString& Parameters)
{
	CatPhysicalBodyTest::FScene Scene;
	if (!TestTrue(TEXT("real physical world"),Scene.Initialize(this))) return false;
	auto* Cat=Scene.Spawn(FVector(0,0,20));
	if (!TestNotNull(TEXT("physical cat"),Cat)) return false;
	auto* Body=Cat->FindComponentByClass<UCatPhysicalBodyComponent>();
	for (int32 I=0;I<120;++I) Scene.World.TickTestWorld(1.0f/120);
	const double StartZ=Cat->GetActorLocation().Z;
	Body->RequestJump();
	double MaximumZ=StartZ;
	for (int32 I=0;I<120;++I)
	{
		Scene.World.TickTestWorld(1.0f/120);
		MaximumZ=FMath::Max(MaximumZ,Cat->GetActorLocation().Z);
	}
	TestTrue(TEXT("formal 420 cm/s launch reaches about 90 cm without isolated body mass loss"),MaximumZ-StartZ>75 && MaximumZ-StartZ<100);
	TestTrue(TEXT("normal support returns after landing"),Body->IsGrounded());
	Body->SetLocomotionEnabledFromAuthority(false,TEXT("TestDowned"));
	Body->SetMoveIntent(FVector(1,0,0));
	AddExpectedMessage(TEXT("Event=physics_body_jump_rejected"), ELogVerbosity::Warning);
	Body->RequestJump();
	const double DisabledZ=Cat->GetActorLocation().Z;
	for (int32 I=0;I<60;++I) Scene.World.TickTestWorld(1.0f/120);
	TestTrue(TEXT("disabled locomotion keeps real gravity and cannot jump"),Cat->GetActorLocation().Z<DisabledZ-5);
	TestTrue(TEXT("disabled body discards voluntary input"),Body->GetMoveIntent().IsNearlyZero());
	TestTrue(TEXT("a disabled belly-down body does not report standing feet below the floor"), Body->GetSupportFootPointWorld().Z > -1.0);
	AddInfo(FString::Printf(TEXT("Event=physical_body_launch_verified RiseCm=%.3f DisabledFallCm=%.3f"),MaximumZ-StartZ,DisabledZ-Cat->GetActorLocation().Z));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPhysicalAuthorityGripContactTest,
	"Catfishing.PhysicalBody.Runtime.AuthorityGripRequiresContactWithItsActualTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatPhysicalAuthorityGripContactTest::RunTest(const FString& Parameters)
{
	CatPhysicalBodyTest::FScene Scene;
	if (!Scene.Initialize(this)) return false;
	auto* Cat = Scene.Spawn(FVector(0, 0, 20));
	auto* Target = Scene.AddBox(FVector(150, 0, 20), FVector(5, 5, 5));
	if (!Cat || !Target) return false;
	for (int32 Frame = 0; Frame < 30; ++Frame) Scene.World.TickTestWorld(1.0f / 60);
	AddExpectedMessage(TEXT("Event=physics_grip_rejected"), ELogVerbosity::Warning);
	TestFalse(TEXT("a point near the hand cannot create a joint to a distant target"),
		Cat->GetGrabComponent()->GripFromAuthority(true, Target->GetStaticMeshComponent(), Cat->GetLeftHand()->GetComponentLocation()));
	for (int32 Frame = 0; Frame < 30; ++Frame) Scene.World.TickTestWorld(1.0f / 60);
	TestFalse(TEXT("the rejected authority call never creates a physical grip"), Cat->GetGrabComponent()->IsGripping(true));
	TestTrue(TEXT("the rejected distant anchor cannot pull or teleport the body"), FMath::Abs(Cat->GetActorLocation().X) < 1.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPhysicalNearGripDriveTest,
	"Catfishing.PhysicalBody.Runtime.NearGripPreservesContactReachAndClearsItsDomain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatPhysicalNearGripDriveTest::RunTest(const FString& Parameters)
{
	for (const int32 Frequency : {60, 120})
	{
		CatPhysicalBodyTest::FScene Scene;
		if (!TestTrue(TEXT("real near-grip physics scene"), Scene.Initialize(this))) return false;
		auto* Cat = Scene.Spawn(FVector(0, 0, 20));
		if (!Cat) return false;
		auto* Body = Cat->FindComponentByClass<UCatPhysicalBodyComponent>();
		auto* Grab = Cat->GetGrabComponent();
		UPhysicsConstraintComponent* Arm = nullptr;
		TInlineComponentArray<UPhysicsConstraintComponent*> Constraints(Cat);
		for (auto* Constraint : Constraints) if (Constraint->GetFName() == TEXT("LeftShoulder")) Arm = Constraint;
		if (!TestNotNull(TEXT("uses the real shoulder drive"), Arm)) return false;
		auto Step = [&](int32 Frames)
		{
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				Body->SetViewIntent(FRotator::ZeroRotator);
				Body->SetMoveIntent(FVector::ZeroVector);
				Scene.World.TickTestWorld(1.0f / Frequency);
			}
		};
		Step(Frequency);
		AStaticMeshActor* Target = Scene.AddBox(Grab->GetShoulderWorldLocation(true) + FVector(14, 0, 0), FVector(1, 5, 5));
		if (!Target) return false;
		Grab->SetGrabInput(true, true);
		for (int32 Frame = 0; Frame < Frequency * 3 && !Grab->IsGripping(true); ++Frame) Step(1);
		if (!TestTrue(TEXT("nearby target is reached and gripped through real contact"), Grab->IsGripping(true)
			&& Grab->GetGripTarget(true) == Target)) return false;
		const FGuid FirstGripId = Grab->GetGripState(true).GripId;
		const double FirstHeldReach = Grab->GetGripState(true).HeldReachDistanceCm;
		const FVector ContactDriveTarget = Arm->ConstraintInstance.GetLinearPositionTarget();
		const FVector PositionAtContact = Cat->GetActorLocation();
		Step(1);
		const FVector FirstHeldDriveTarget = Arm->ConstraintInstance.GetLinearPositionTarget();
		TestTrue(TEXT("gripping does not jump from the contact target to full arm extension"),
			FMath::Abs(FirstHeldDriveTarget.Size() - ContactDriveTarget.Size()) < 0.1);
		TestTrue(TEXT("the regression actually grips inside full reach"), FirstHeldReach < Grab->GetReachLengthCm() - 3.0);
		Step(Frequency * 3);
		const double StillDrift = FVector::Dist2D(PositionAtContact, Cat->GetActorLocation());
		const double StillSpeed = Cat->GetVelocity().Size2D();
		TestTrue(TEXT("unchanged aim does not continuously push the cat away from its near contact"), StillDrift < 2.0 && StillSpeed < 3.0);
		TestTrue(TEXT("the physical contact survives stationary support"), Grab->IsGripping(true) && Body->IsGrounded());
		Grab->SetGrabInput(true, false);
		TestEqual(TEXT("release clears the previous contact reach"), Grab->GetGripState(true).HeldReachDistanceCm, 0.0);
		Target->Destroy();
		Step(Frequency);
		Target = Scene.AddBox(Grab->GetShoulderWorldLocation(true) + FVector(18, 0, 0), FVector(1, 5, 5));
		if (!Target) return false;
		Grab->SetGrabInput(true, true);
		for (int32 Frame = 0; Frame < Frequency * 3 && !Grab->IsGripping(true); ++Frame) Step(1);
		if (!TestTrue(TEXT("a different target creates a new real contact"), Grab->IsGripping(true)
			&& Grab->GetGripTarget(true) == Target && Grab->GetGripState(true).GripId != FirstGripId)) return false;
		TestTrue(TEXT("the new target captures its own reach instead of the previous grip's distance"),
			Grab->GetGripState(true).HeldReachDistanceCm > FirstHeldReach + 2.0);
		const FGuid SecondGripId = Grab->GetGripState(true).GripId;
		AddExpectedMessage(TEXT("Event=physics_grip_rejected"), ELogVerbosity::Warning);
		TestFalse(TEXT("an explicit hold cannot adopt a different target"), Grab->RetainGripFromAuthority(true, Scene.Floor->GetStaticMeshComponent()));
		TestFalse(TEXT("rejected source transfer does not change the held source"), Grab->GetGripState(true).bExplicitHold);
		if (!TestTrue(TEXT("authority adopts the existing contact"), Grab->RetainGripFromAuthority(true, Target->GetStaticMeshComponent()))) return false;
		Grab->SetGrabInput(true, false);
		Step(Frequency / 2);
		TestTrue(TEXT("the old mouse release cannot remove the same explicit grip after physical steps"),
			Grab->IsGripping(true) && Grab->GetGripState(true).bExplicitHold && Grab->GetGripState(true).GripId == SecondGripId);
		Grab->ReleaseHandFromAuthority(true, TEXT("ExplicitHoldReleased"));
		TestFalse(TEXT("authority release clears explicit ownership and the contact"), Grab->IsGripping(true) || Grab->GetGripState(true).bExplicitHold);
		TestFalse(TEXT("an explicitly released hand retracts"), Grab->IsReaching(true));
		Grab->SetGrabInput(true, true);
		for (int32 Frame = 0; Frame < Frequency * 3 && !Grab->IsGripping(true); ++Frame) Step(1);
		if (!TestTrue(TEXT("a later input creates a fresh contact instead of reviving the explicit grip"),
			Grab->IsGripping(true) && Grab->GetGripState(true).GripId != SecondGripId)) return false;
		if (!TestTrue(TEXT("the fresh grip can be explicitly held"), Grab->RetainGripFromAuthority(true, Target->GetStaticMeshComponent()))) return false;
		if (!TestTrue(TEXT("teleport uses the normal three-body reset"),
			Body->TeleportBodyFromAuthority(FTransform(FVector(100, 0, 20)), TEXT("NearGripReset")))) return false;
		TestFalse(TEXT("teleport clears the physical contact"), Grab->IsGripping(true));
		TestFalse(TEXT("teleport clears explicit ownership without regrabbing"), Grab->GetGripState(true).bExplicitHold || Grab->IsReaching(true));
		TestEqual(TEXT("teleport clears the contact reach domain"), Grab->GetGripState(true).HeldReachDistanceCm, 0.0);
		AddInfo(FString::Printf(TEXT("Event=physical_near_grip_drive_verified Hz=%d ContactTarget=%s HeldTarget=%s HeldReachCm=%.3f StationaryDriftCm=%.3f StationarySpeedCmS=%.3f"),
			Frequency, *ContactDriveTarget.ToCompactString(), *FirstHeldDriveTarget.ToCompactString(), FirstHeldReach, StillDrift, StillSpeed));
	}
	return !HasAnyErrors();
}
#endif
