#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Character/Physics/Tests/CatPhysicalTestWorld.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "ShopEconomy/CatShopKioskActor.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatReachSurfaceTest,
	"Catfishing.PhysicsGrabPrototype.Runtime.ReachIgnoresInteractionVolumesAndFindsSolidSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatReachSurfaceTest::RunTest(const FString& Parameters)
{
	CatPhysicalTest::FScene Scene;
	if (!Scene.Initialize(this)) return false;
	auto* Cat = Scene.SpawnCat(FVector(0,0,20));
	if (!Cat) return false;
	Scene.Step(90);
	auto* Body = Cat->GetPhysicalBodyComponent();
	auto* Grab = Body->GetGrab();
	Body->SetViewIntent(FRotator::ZeroRotator);
	const FVector Shoulder = Grab->GetShoulderWorldLocation(true);
	// Use the actual kiosk interaction component, plus a box with the container query contract.
	auto* Kiosk = Scene.World.GetTestWorld()->SpawnActor<ACatShopKioskActor>(Shoulder + FVector(20,0,0), FRotator::ZeroRotator);
	auto* QueryBox = Scene.AddBox(Shoulder + FVector(6,0,0), FVector(3,15,10));
	if (!Kiosk || !QueryBox) return false;
	QueryBox->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	QueryBox->GetStaticMeshComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
	QueryBox->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
	Scene.Step(2);
	for (const bool bLeft : {true,false}) Grab->SetGrabInput(bLeft, true);
	Scene.Step(2);
	for (const bool bLeft : {true,false})
	{
		const FVector Expected = Grab->GetShoulderWorldLocation(bLeft) + FVector::ForwardVector * Grab->GetReachLengthCm();
		TestTrue(TEXT("interaction volumes do not shorten either reaching hand"), Body->GetHand(bLeft)->GetComponentLocation().Equals(Expected, .1));
		TestFalse(TEXT("interaction volume cannot become a physical grip"), Grab->IsGripping(bLeft));
	}
	FHitResult UIHit;
	TestTrue(TEXT("unchanged Visibility query still detects the interaction range"), Scene.World.GetTestWorld()->LineTraceSingleByChannel(
		UIHit, Shoulder - FVector(100,0,0), Shoulder + FVector(40,0,0), ECC_Visibility, FCollisionQueryParams(NAME_None, false, Cat)));
	TestTrue(TEXT("the UI hit belongs to a preserved query volume"), UIHit.GetActor() == Kiosk || UIHit.GetActor() == QueryBox);
	auto* Solid = Scene.AddBox(Shoulder + FVector(13,0,0), FVector(1,15,10));
	if (!Solid) return false;
	// A physical surface need not participate in crosshair interaction targeting.
	Solid->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Ignore);
	Scene.Step(5);
	for (const bool bLeft : {true,false})
	{
		TestTrue(TEXT("a solid behind the UI volume remains reachable"), Grab->IsGripping(bLeft));
		TestEqual(TEXT("the grip records the actual solid component"), Grab->GetGripTargetComponent(bLeft), static_cast<UPrimitiveComponent*>(Solid->GetStaticMeshComponent()));
		Grab->SetGrabInput(bLeft, false);
	}
	Scene.Step(2);
	TestTrue(TEXT("both hands release normally"), !Grab->IsGripping(true) && !Grab->IsGripping(false));
	return !HasAnyErrors();
}
#endif
