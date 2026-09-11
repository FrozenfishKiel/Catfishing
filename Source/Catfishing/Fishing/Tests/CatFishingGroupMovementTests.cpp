#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Character/Physics/Tests/CatPhysicalTestWorld.h"
#include "Components/BoxComponent.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingPhysicalConnectionMovementTest,
	"Catfishing.Unit.Fishing.Runtime.PhysicalConnectionPullsInAnyDirectionAndReleaseSeparatesBodies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingPhysicalConnectionMovementTest::RunTest(const FString& Parameters)
{
	CatPhysicalTest::FScene Scene;
	if (!TestTrue(TEXT("real physics world"), Scene.Initialize(this))) return false;
	ACatCharacter* Puller = Scene.SpawnCat(FVector(0, 0, 20));
	ACatCharacter* Partner = Scene.SpawnCat(FVector(35, 0, 20));
	if (!Puller || !Partner) return false;
	UCatPhysicalBodyComponent* First = Puller->GetPhysicalBodyComponent();
	UCatPhysicalBodyComponent* Second = Partner->GetPhysicalBodyComponent();
	// Outside a fishing Session, ordinary movement does not read fishing stamina or a group budget.
	Puller->GetCatAbilitySystemComponent()->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 0.0f);
	Scene.Step(60);
	First->SetViewIntent(FRotator::ZeroRotator);
	First->GetGrab()->SetGrabInput(true, true);
	Scene.Step(90);
	if (!TestTrue(TEXT("reaching hand really contacts and constrains the other cat"),
		First->GetGrab()->IsGripping(true) && First->GetGrab()->GetGripTarget(true) == Partner)) return false;
	const FVector Before = Partner->GetActorLocation();
	const uint32 ResetEpoch = Second->GetResetEpoch();
	First->SetMoveIntent(FVector(-1, -1, 0));
	Scene.Step(90);
	const FVector Pulled = Partner->GetActorLocation() - Before;
	TestEqual(TEXT("ordinary physical assistance does not create or charge fishing stamina"),
		Puller->GetCatAbilitySystemComponent()->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), 0.0f);
	TestTrue(TEXT("ordinary diagonal movement transfers both force components through the grip without fishing membership"), Pulled.X < -10 && Pulled.Y < -10);
	TestTrue(TEXT("load-bearing connection stays within physical arm reach"), FVector::Distance(Puller->GetActorLocation(), Partner->GetActorLocation()) < 70);
	TestEqual(TEXT("the partner moves by physics without a teleport epoch"), Second->GetResetEpoch(), ResetEpoch);
	First->GetGrab()->SetGrabInput(true, false);
	TestFalse(TEXT("button release removes the real connection immediately"), First->GetGrab()->IsGripping(true));
	const double ReleasedDistance = FVector::Dist2D(Puller->GetActorLocation(), Partner->GetActorLocation());
	Second->SetMoveIntent(FVector(1, 1, 0));
	Scene.Step(90);
	TestTrue(TEXT("released players can move apart without a retained shared velocity or formation"),
		FVector::Dist2D(Puller->GetActorLocation(), Partner->GetActorLocation()) > ReleasedDistance + 50);
	AddInfo(FString::Printf(TEXT("Event=fishing_physical_connection_verified PulledCm=%s FinalSeparationCm=%.3f"),
		*Pulled.ToCompactString(), FVector::Dist2D(Puller->GetActorLocation(), Partner->GetActorLocation())));
	return !HasAnyErrors();
}
#endif
