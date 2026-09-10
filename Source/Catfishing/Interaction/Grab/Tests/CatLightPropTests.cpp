#if WITH_DEV_AUTOMATION_TESTS

#include "Character/Physics/Tests/CatPhysicalTestWorld.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Interaction/Grab/CatLightPropComponent.h"
#include "Interaction/Grab/CatLightPropSubsystem.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Interaction/Grab/CatPhysicsGrabProp.h"

namespace CatLightPropTest
{
	ACatPhysicsGrabProp* Prop(CatPhysicalTest::FScene& Scene, FVector At, FVector Size, float Mass = .35f)
	{
		const FTransform Pose(At);
		auto* Actor = Scene.World.GetTestWorld()->SpawnActorDeferred<ACatPhysicsGrabProp>(ACatPhysicsGrabProp::StaticClass(), Pose);
		if (!Actor || !Actor->ConfigureFromAuthority(Size, true, Mass, FLinearColor::Green)) return nullptr;
		Actor->FinishSpawning(Pose);
		return Actor;
	}
	bool GripAt(ACatCharacter* Cat, bool Left, UPrimitiveComponent* Target, FVector Point)
	{
		auto* Physical = Cat->GetPhysicalBodyComponent();
		if (!Physical->TeleportBodyFromAuthority(FTransform(Cat->GetActorRotation(),
			Physical->GetBody()->GetComponentLocation() + Point - Physical->GetHand(Left)->GetComponentLocation()), TEXT("LightPropContactFixture"))) return false;
		return Physical->GetGrab()->GripFromAuthority(Left, Target, Point);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatLightPropCollisionTest,
	"Catfishing.PhysicalGrab.Runtime.LightProps.CollisionsYieldWithoutCatImpulse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatLightPropCollisionTest::RunTest(const FString& Parameters)
{
	for (const bool HitHand : {false, true}) for (const bool Horizontal : {false, true})
	{
		CatPhysicalTest::FScene Scene;
		if (!Scene.Initialize(this)) return false;
		Scene.World.GetTestWorld()->GetWorldSettings()->bGlobalGravitySet = true;
		Scene.World.GetTestWorld()->GetWorldSettings()->GlobalGravityZ = 0;
		auto* Cat = Scene.SpawnCat(FVector(0, 0, 200));
		auto* Physical = Cat->GetPhysicalBodyComponent();
		Physical->SetLocomotionEnabledFromAuthority(false, TEXT("CollisionOnlyFixture"));
		Scene.Step(120);
		const FVector Direction = Horizontal ? FVector::RightVector : FVector::UpVector;
		UPrimitiveComponent* Target = HitHand ? static_cast<UPrimitiveComponent*>(Physical->GetHand(true)) : Physical->GetBody();
		auto* Prop = CatLightPropTest::Prop(Scene, Target->GetComponentLocation() + Direction * 40, FVector(8), 10);
		if (!Prop) return false;
		// Exercise the registration update as well as the initial BeginPlay particle.
		Prop->GetPhysicsMesh()->RecreatePhysicsState();
		const FVector Before = Physical->GetBody()->GetComponentLocation();
		const FQuat Rotation = Physical->GetBody()->GetComponentQuat();
		const FVector HandBefore = Physical->GetHand(true)->GetComponentLocation();
		Prop->GetPhysicsMesh()->SetPhysicsLinearVelocity(-Direction * 400);
		Scene.Step(40, 120);
		const auto* Policy = Scene.World.GetTestWorld()->GetSubsystem<UCatLightPropSubsystem>();
		const double Drift = FVector::Distance(Before, Physical->GetBody()->GetComponentLocation());
		const double Turn = FMath::RadiansToDegrees(Rotation.AngularDistance(Physical->GetBody()->GetComponentQuat()));
		AddInfo(FString::Printf(TEXT("Event=light_prop_collision_verified Hand=%d Horizontal=%d Contacts=%llu CatDriftCm=%.6f CatTurnDeg=%.6f PropSpeedCmS=%.3f"),
			HitHand, Horizontal, Policy->GetModifiedContactCount(), Drift, Turn, Prop->GetPhysicsMesh()->GetPhysicsLinearVelocity().Size()));
		TestTrue(TEXT("actual Chaos contacts executed the one-way policy"), Policy->GetModifiedContactCount() > 0);
		TestTrue(TEXT("ten-kilogram prop cannot displace the freely floating cat"), Drift < .1);
		TestTrue(TEXT("prop cannot rotate the cat"), Turn < .1);
		TestTrue(TEXT("prop collision cannot knock its hand away"), FVector::Distance(HandBefore, Physical->GetHand(true)->GetComponentLocation()) < .1);
		TestTrue(TEXT("solid prop remains on the impact side instead of passing through the cat"),
			FVector::DotProduct(Prop->GetActorLocation() - Target->GetComponentLocation(), Direction) > 4);
		TestTrue(TEXT("contact stops or rebounds the incoming prop"), FVector::DotProduct(Prop->GetPhysicsMesh()->GetPhysicsLinearVelocity(), Direction) >= -.1);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatLightPropGripLifecycleTest,
	"Catfishing.PhysicalGrab.Runtime.LightProps.AllHandsOwnGravityAndCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatLightPropGripLifecycleTest::RunTest(const FString& Parameters)
{
	CatPhysicalTest::FScene Scene;
	if (!Scene.Initialize(this)) return false;
	auto* Prop = CatLightPropTest::Prop(Scene, FVector(0, 0, 100), FVector(20, 60, 4));
	if (!Prop) return false;
	auto* Light = Prop->FindComponentByClass<UCatLightPropComponent>();
	auto* First = Scene.SpawnCat(FVector(0, -60, 100));
	auto* Second = Scene.SpawnCat(FVector(0, 60, 100));
	if (!TestTrue(TEXT("first cat acquires actual contact"), CatLightPropTest::GripAt(First, true, Prop->GetPhysicsMesh(), FVector(0, -25, 102)))
		|| !TestTrue(TEXT("second cat independently acquires actual contact"), CatLightPropTest::GripAt(Second, true, Prop->GetPhysicsMesh(), FVector(0, 25, 102)))) return false;
	TestEqual(TEXT("all players count, not only the owner"), Light->GetState().GripCount, 2);
	TestEqual(TEXT("held prop has no self gravity"), Light->GetState().Mode, ECatLightPropMode::Held);
	TestFalse(TEXT("engine gravity is not also applied"), Prop->GetPhysicsMesh()->IsGravityEnabled());
	TestEqual(TEXT("finite mass retained"), Prop->GetPhysicsMesh()->GetMass(), .35f, .001f);
	const FGuid RemainingGrip = Second->GetPhysicalBodyComponent()->GetGrab()->GetGripState(true).GripId;
	First->GetPhysicalBodyComponent()->GetGrab()->ReleaseAllFromAuthority(TEXT("FirstHandReleased"));
	TestEqual(TEXT("one release leaves other hand holding"), Light->GetState().GripCount, 1);
	TestEqual(TEXT("remaining hand keeps no self gravity"), Light->GetState().Mode, ECatLightPropMode::Held);
	TestEqual(TEXT("remaining constraint identity unchanged"), Second->GetPhysicalBodyComponent()->GetGrab()->GetGripState(true).GripId, RemainingGrip);
	Second->Destroy();
	TestEqual(TEXT("character exit clears last grip through normal cleanup"), Light->GetState().GripCount, 0);
	TestEqual(TEXT("last release enters gentle fall"), Light->GetState().Mode, ECatLightPropMode::Falling);
	TestEqual(TEXT("released damping"), Prop->GetPhysicsMesh()->GetLinearDamping(), UCatLightPropComponent::FallingLinearDamping);
	First->Destroy();
	const double Z = Prop->GetActorLocation().Z;
	Scene.Step(12);
	const double Drop = Z - Prop->GetActorLocation().Z;
	AddInfo(FString::Printf(TEXT("Event=light_prop_gentle_fall_verified Seconds=.2 DropCm=%.3f VelocityZ=%.3f"), Drop, Prop->GetPhysicsMesh()->GetPhysicsLinearVelocity().Z));
	TestTrue(TEXT("released prop falls gently under 25 percent gravity"), Drop > 2 && Drop < 7);
	Light->SetExternalLoadFromAuthority(true);
	TestEqual(TEXT("fish load suspends the extra landing damping"), Prop->GetPhysicsMesh()->GetLinearDamping(), .25f);
	Prop->GetPhysicsMesh()->SetPhysicsLinearVelocity(FVector(1000, 0, 0));
	Scene.Step(6);
	TestTrue(TEXT("loaded velocity is never capped"), Prop->GetPhysicsMesh()->GetPhysicsLinearVelocity().X > 900);
	Light->SetExternalLoadFromAuthority(false);
	TestEqual(TEXT("clear load restores fall policy"), Light->GetState().Mode, ECatLightPropMode::Falling);
	Prop->GetPhysicsMesh()->RecreatePhysicsState();
	Scene.Step(2);
	TestTrue(TEXT("recreated prop keeps same policy and finite mass"), UCatLightPropComponent::FindFor(Prop->GetPhysicsMesh()) == Light && Prop->GetPhysicsMesh()->GetMass() > .3f);
	TestTrue(TEXT("dynamic-to-fixed configuration is supported"), Prop->ConfigureFromAuthority(FVector(20), false, 1, FLinearColor::Green));
	TestNull(TEXT("fixed scene geometry does not retain the lightweight policy"), UCatLightPropComponent::FindFor(Prop->GetPhysicsMesh()));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatLightPropSupportTest,
	"Catfishing.PhysicalGrab.Runtime.LightProps.SolidGrabbableGeometryIsNotFootSupport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatLightPropSupportTest::RunTest(const FString& Parameters)
{
	CatPhysicalTest::FScene Scene;
	if (!Scene.Initialize(this)) return false;
	auto* Prop = CatLightPropTest::Prop(Scene, FVector(0, 0, 8), FVector(100, 100, 4));
	auto* Cat = Scene.SpawnCat(FVector(0, 0, 20));
	if (!Prop || !Cat) return false;
	auto* Physical = Cat->GetPhysicalBodyComponent();
	FCollisionQueryParams Ordinary(SCENE_QUERY_STAT(LightPropGrippable), false, Cat);
	FHitResult Hit;
	const FVector Origin(0, 0, 20), End(0, 0, -2);
	TestTrue(TEXT("ordinary grab/collision query still hits the prop"), Scene.World.GetTestWorld()->LineTraceSingleByChannel(Hit, Origin, End, ECC_PhysicsBody, Ordinary) && Hit.GetActor() == Prop);
	Physical->AppendSupportQueryIgnores(Ordinary);
	TestTrue(TEXT("support query sees the actual floor beneath the prop"), Scene.World.GetTestWorld()->LineTraceSingleByChannel(Hit, Origin, End, ECC_PhysicsBody, Ordinary) && Hit.GetActor() == Scene.Floor);
	Scene.Step(120);
	TestTrue(TEXT("solid prop under cat does not become a suspension spring"), FMath::Abs(Physical->GetBody()->GetComponentLocation().Z - Physical->GetStandRootHeightCm()) < 2);
	TestTrue(TEXT("world floor remains valid support"), Physical->IsGrounded());
	Prop->ConfigureFromAuthority(FVector(100, 100, 4), false, .35f, FLinearColor::Green);
	Prop->SetActorLocation(FVector(0, 0, 8));
	FCollisionQueryParams Fixed(SCENE_QUERY_STAT(LightPropFixedSupport), false, Cat);
	Physical->AppendSupportQueryIgnores(Fixed);
	TestTrue(TEXT("fixed platform regains original support contract"), Scene.World.GetTestWorld()->LineTraceSingleByChannel(Hit, Origin, End, ECC_PhysicsBody, Fixed) && Hit.GetActor() == Prop);
	return !HasAnyErrors();
}

#endif
