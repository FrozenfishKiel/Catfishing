#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
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

namespace CatPhysicsRecoveryTest
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPhysicsPrototypeGroundRecoveryTest,
	"Catfishing.PhysicsGrabPrototype.Runtime.InvertedAndSideLyingBodiesRecoverThroughGroundContact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatPhysicsPrototypeGroundRecoveryTest::RunTest(const FString& Parameters)
{
	for (const int32 Frequency : {60, 120})
	{
		CatPhysicsRecoveryTest::FScene Scene;
		if (!TestTrue(TEXT("creates a real Chaos recovery world"), Scene.Initialize(this))) return false;
		TArray<ACatPhysicsPrototypePawn*> Cats;
		TArray<FVector> LastPositions;
		TArray<uint32> ResetEpochs;
		const double Rolls[] = {90.0, -90.0, 180.0};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Rolls); ++Index)
		{
			const double InitialHeight = Index == 2 ? 7.2 : 5.2;
			ACatPhysicsPrototypePawn* Cat = Scene.Spawn(FVector(0, Index * 150.0, InitialHeight), FRotator(0, 0, Rolls[Index]));
			if (!TestNotNull(TEXT("spawns an actually rotated physical cat"), Cat)) return false;
			TestTrue(TEXT("sideways or upside-down pose begins outside normal foot support eligibility"), Cat->GetActorUpVector().Z < 0.01);
			Cats.Add(Cat);
			LastPositions.Add(Cat->GetActorLocation());
			ResetEpochs.Add(Cat->GetPrototypeResetEpoch());
		}
		double MaximumFrameTravelCm = 0.0;
		double MaximumAngularSpeed = 0.0;
		for (int32 Frame = 0; Frame < Frequency * 6; ++Frame)
		{
			Scene.World.TickTestWorld(1.0f / Frequency);
			for (int32 Index = 0; Index < Cats.Num(); ++Index)
			{
				ACatPhysicsPrototypePawn* Cat = Cats[Index];
				const FVector Position = Cat->GetActorLocation();
				const FVector AngularVelocity = Cat->GetPhysicsBody()->GetPhysicsAngularVelocityInRadians();
				if (!TestFalse(TEXT("recovery remains finite while actual collisions and arms are solved"),
					Position.ContainsNaN() || Cat->GetVelocity().ContainsNaN() || AngularVelocity.ContainsNaN())) return false;
				MaximumFrameTravelCm = FMath::Max(MaximumFrameTravelCm, FVector::Distance(Position, LastPositions[Index]));
				MaximumAngularSpeed = FMath::Max(MaximumAngularSpeed, AngularVelocity.Size());
				LastPositions[Index] = Position;
			}
		}
		for (int32 Index = 0; Index < Cats.Num(); ++Index)
		{
			ACatPhysicsPrototypePawn* Cat = Cats[Index];
			TestTrue(TEXT("all initial roll directions finish upright"), Cat->GetActorUpVector().Z > 0.90);
			TestTrue(TEXT("normal foot support resumes after the body rolls upright"), Cat->IsPrototypeGrounded());
			TestTrue(TEXT("body settles at the normal standing height"), Cat->GetActorLocation().Z > 16.0 && Cat->GetActorLocation().Z < 25.0);
			TestTrue(TEXT("standing body has stopped tumbling"), Cat->GetPhysicsBody()->GetPhysicsAngularVelocityInRadians().Size() < 0.5);
			TestEqual(TEXT("recovery never uses the teleport/reset path"), Cat->GetPrototypeResetEpoch(), ResetEpochs[Index]);
			AddInfo(FString::Printf(TEXT("Event=physics_prototype_ground_recovery_verified Hz=%d InitialRoll=%.0f UpZ=%.5f HeightCm=%.3f AngularSpeed=%.4f ResetEpoch=%u"),
				Frequency, Rolls[Index], Cat->GetActorUpVector().Z, Cat->GetActorLocation().Z,
				Cat->GetPhysicsBody()->GetPhysicsAngularVelocityInRadians().Size(), Cat->GetPrototypeResetEpoch()));
		}
		TestTrue(TEXT("continuous physics recovery does not jump position between frames"), MaximumFrameTravelCm < 12.0);
		TestTrue(TEXT("ground recovery does not create explosive spin"), MaximumAngularSpeed < 18.0);
		// A reported upright pose is insufficient: exercise the production movement and jump gates.
		ACatPhysicsPrototypePawn* RecoveredCat = Cats[0];
		const FVector BeforeMove = RecoveredCat->GetActorLocation();
		for (int32 Frame = 0; Frame < Frequency; ++Frame)
		{
			RecoveredCat->SetPrototypeInput(FVector2D(1.0, 0.0), FRotator::ZeroRotator);
			Scene.World.TickTestWorld(1.0f / Frequency);
		}
		TestTrue(TEXT("recovered cat actually walks using the ordinary grounded movement gate"),
			RecoveredCat->GetActorLocation().X > BeforeMove.X + 30.0);
		RecoveredCat->SetPrototypeInput(FVector2D::ZeroVector, FRotator::ZeroRotator);
		for (int32 Frame = 0; Frame < Frequency; ++Frame) Scene.World.TickTestWorld(1.0f / Frequency);
		if (!TestTrue(TEXT("recovered cat has real foot support before jumping"), RecoveredCat->IsPrototypeGrounded())) return false;
		const double BeforeJumpZ = RecoveredCat->GetActorLocation().Z;
		RecoveredCat->RequestJump();
		double HighestJumpZ = BeforeJumpZ, HighestJumpVelocityZ = 0.0;
		bool bObservedAirborne = false;
		for (int32 Frame = 0; Frame < Frequency / 2; ++Frame)
		{
			Scene.World.TickTestWorld(1.0f / Frequency);
			HighestJumpZ = FMath::Max(HighestJumpZ, RecoveredCat->GetActorLocation().Z);
			HighestJumpVelocityZ = FMath::Max(HighestJumpVelocityZ, RecoveredCat->GetVelocity().Z);
			bObservedAirborne |= !RecoveredCat->IsPrototypeGrounded();
		}
		TestTrue(TEXT("jump after recovery produces actual upward physics motion"),
			HighestJumpZ > BeforeJumpZ + 10.0 && HighestJumpVelocityZ > 50.0 && bObservedAirborne);
		TestEqual(TEXT("walking and jumping after recovery still use no reset"), RecoveredCat->GetPrototypeResetEpoch(), ResetEpochs[0]);
		AddInfo(FString::Printf(TEXT("Event=physics_prototype_recovered_controls_verified Hz=%d WalkDistanceCm=%.3f JumpRiseCm=%.3f JumpSpeedCmS=%.3f"),
			Frequency, RecoveredCat->GetActorLocation().X - BeforeMove.X, HighestJumpZ - BeforeJumpZ, HighestJumpVelocityZ));
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPhysicsPrototypeHangingRecoveryBoundaryTest,
	"Catfishing.PhysicsGrabPrototype.Runtime.WallSuspensionDoesNotBecomeGroundedRecoveryOrJump",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatPhysicsPrototypeHangingRecoveryBoundaryTest::RunTest(const FString& Parameters)
{
	CatPhysicsRecoveryTest::FScene Scene;
	if (!Scene.Initialize(this)) return false;
	AStaticMeshActor* Wall = Scene.AddBox(FVector(30, 0, 45), FVector(3, 20, 45));
	ACatPhysicsPrototypePawn* Cat = Scene.Spawn(FVector(0, 0, 20));
	if (!Wall || !Cat) return false;
	for (int32 Frame = 0; Frame < 30; ++Frame) Scene.World.TickTestWorld(1.0f / 60.0f);
	Cat->SetPrototypeInput(FVector2D::ZeroVector, FRotator::ZeroRotator);
	Cat->SetGrabInput(true, true);
	for (int32 Frame = 0; Frame < 60; ++Frame) Scene.World.TickTestWorld(1.0f / 60.0f);
	if (!TestTrue(TEXT("hand really grips the static wall before ground removal"),
		Cat->GetGrabComponent()->IsGripping(true) && Cat->GetGrabComponent()->GetGripTarget(true) == Wall)) return false;
	Scene.Floor->Destroy();
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Scene.World.TickTestWorld(1.0f / 60.0f);
		TestFalse(TEXT("a wall-held cat has no downward floor support"), Cat->IsPrototypeGrounded());
	}
	// Measure the settled pendulum before requesting a jump, then observe actual physics consumption
	// of any queued impulse. Reading velocity immediately after RequestJump would miss a delayed AddImpulse.
	double BaselineHighestZ = Cat->GetActorLocation().Z;
	double BaselineHighestUpSpeed = FMath::Max(0.0, Cat->GetVelocity().Z);
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Scene.World.TickTestWorld(1.0f / 60.0f);
		BaselineHighestZ = FMath::Max(BaselineHighestZ, Cat->GetActorLocation().Z);
		BaselineHighestUpSpeed = FMath::Max(BaselineHighestUpSpeed, Cat->GetVelocity().Z);
	}
	const uint32 ResetEpoch = Cat->GetPrototypeResetEpoch();
	AddExpectedMessage(TEXT("Event=physics_body_jump_rejected"), ELogVerbosity::Warning);
	Cat->RequestJump();
	double HighestAfterJumpZ = Cat->GetActorLocation().Z;
	double HighestAfterJumpUpSpeed = FMath::Max(0.0, Cat->GetVelocity().Z);
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Scene.World.TickTestWorld(1.0f / 60.0f);
		HighestAfterJumpZ = FMath::Max(HighestAfterJumpZ, Cat->GetActorLocation().Z);
		HighestAfterJumpUpSpeed = FMath::Max(HighestAfterJumpUpSpeed, Cat->GetVelocity().Z);
		TestFalse(TEXT("the rejected jump never creates a ground support fact"), Cat->IsPrototypeGrounded());
	}
	TestTrue(TEXT("after physics consumes pending forces, rejected jump adds no upward speed to the settled suspension"),
		HighestAfterJumpUpSpeed <= BaselineHighestUpSpeed + 12.0);
	TestTrue(TEXT("after physics steps, rejected jump never lifts the suspended body like a normal jump"),
		HighestAfterJumpZ <= BaselineHighestZ + 3.0);
	TestTrue(TEXT("real grip remains load bearing while the body is suspended"), Cat->GetGrabComponent()->IsGripping(true));
	TestTrue(TEXT("hanging cat remains near its actual wall contact"), Cat->GetActorLocation().Z > -40.0);
	Cat->SetGrabInput(true, false);
	const double BeforeReleaseZ = Cat->GetActorLocation().Z;
	for (int32 Frame = 0; Frame < 20; ++Frame) Scene.World.TickTestWorld(1.0f / 60.0f);
	TestTrue(TEXT("without the hand constraint gravity still drops the cat"), Cat->GetActorLocation().Z < BeforeReleaseZ - 20.0);
	TestEqual(TEXT("suspension and release never reset the body"), Cat->GetPrototypeResetEpoch(), ResetEpoch);
	AddInfo(FString::Printf(TEXT("Event=physics_prototype_recovery_boundary_verified BaselineMaxZ=%.3f AfterJumpMaxZ=%.3f BaselineUpSpeed=%.3f AfterJumpUpSpeed=%.3f Result=WallGripNotGroundNoAirJump"),
		BaselineHighestZ, HighestAfterJumpZ, BaselineHighestUpSpeed, HighestAfterJumpUpSpeed));
	return !HasAnyErrors();
}

#endif
