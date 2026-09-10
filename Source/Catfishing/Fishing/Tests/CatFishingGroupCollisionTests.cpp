#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Character/Physics/Tests/CatPhysicalTestWorld.h"
#include "Components/BoxComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingPhysicalPeerCollisionTest,
	"Catfishing.Unit.Fishing.Runtime.PhysicalBodiesPushPeersRespectWallsAndStepOnLowGround",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingPhysicalPeerCollisionTest::RunTest(const FString& Parameters)
{
	for (int32 Scenario = 0; Scenario < 3; ++Scenario)
	{
		CatPhysicalTest::FScene Scene;
		if (!TestTrue(TEXT("independent real collision world"), Scene.Initialize(this))) return false;
		ACatCharacter* First = Scene.SpawnCat(FVector(0, 0, 20));
		if (!First) return false;
		UCatPhysicalBodyComponent* Body = First->GetPhysicalBodyComponent();
		if (Scenario == 0)
		{
			ACatCharacter* Second = Scene.SpawnCat(FVector(32, 0, 20));
			if (!Second) return false;
			Scene.Step(60);
			const double Before = Second->GetActorLocation().X;
			Body->SetMoveIntent(FVector::ForwardVector);
			Scene.Step(90);
			TestTrue(TEXT("ordinary walking contact actually pushes a standing peer"), Second->GetActorLocation().X > Before + 20);
			TestTrue(TEXT("the pusher cannot pass through the peer"), First->GetActorLocation().X < Second->GetActorLocation().X);
			TestEqual(TEXT("physics bodies keep peer blocking instead of group collision ignores"), Body->GetBody()->GetCollisionResponseToChannel(ECC_PhysicsBody), ECR_Block);
		}
		else if (Scenario == 1)
		{
			ACatCharacter* FreeCat = Scene.SpawnCat(FVector(0, 80, 20));
			if (!FreeCat || !Scene.AddBox(FVector(90, 0, 60), FVector(3, 22, 60))) return false;
			Scene.Step(60);
			Body->SetMoveIntent(FVector::ForwardVector);
			FreeCat->GetPhysicalBodyComponent()->SetMoveIntent(FVector::ForwardVector);
			Scene.Step(120);
			TestTrue(TEXT("upright capsule collision stops the character before the wall"), First->GetActorLocation().X > 40 && First->GetActorLocation().X < 87);
			TestTrue(TEXT("a wall cannot freeze an unrelated unconnected cat in the free lane"), FreeCat->GetActorLocation().X > 120);
		}
		else
		{
			if (!Scene.AddBox(FVector(100, 0, 2.5), FVector(50, 40, 2.5))) return false;
			Scene.Step(60);
			Body->SetMoveIntent(FVector::ForwardVector);
			double HighestOnStep = 0.0;
			for (int32 Frame = 0; Frame < 150; ++Frame)
			{
				Scene.Step(1);
				if (First->GetActorLocation().X > 60 && First->GetActorLocation().X < 140)
					HighestOnStep = FMath::Max(HighestOnStep, First->GetActorLocation().Z);
			}
			TestTrue(TEXT("CMC crosses a low real step without teleport"), First->GetActorLocation().X > 120 && HighestOnStep > 22.5);
			TestEqual(TEXT("low-step travel does not reset the body"), Body->GetResetEpoch(), uint32(0));
		}
		AddInfo(FString::Printf(TEXT("Event=fishing_physical_collision_verified Scenario=%d Position=%s"), Scenario, *First->GetActorLocation().ToCompactString()));
	}
	return !HasAnyErrors();
}
#endif
