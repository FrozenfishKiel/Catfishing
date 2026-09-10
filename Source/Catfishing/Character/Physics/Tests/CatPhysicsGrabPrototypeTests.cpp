#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Character/Physics/CatPhysicsPrototypePawn.h"
#include "Character/Physics/CatPhysicsPrototypeVisualComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Framework/Game/PhysicsPrototype/CatPhysicsPrototypePlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Interaction/Grab/CatPhysicsGrabProp.h"

namespace CatPhysicsGrabTest
{

struct FScene
{
	FTestWorldWrapper World;
	FAutomationTestBase* Test = nullptr;
	AStaticMeshActor* Floor = nullptr;
	bool Initialize(FAutomationTestBase* InTest)
	{
		Test = InTest;
		if (!World.CreateTestWorld(EWorldType::Game)) return false;
		World.ForwardErrorMessages(Test);
		World.GetTestWorld()->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
		Floor = AddBox(FVector(0, 0, -10), FVector(1000, 1000, 10));
		if (!Floor) return false;
		return World.BeginPlayInTestWorld();
	}
	AStaticMeshActor* AddBox(FVector Position, FVector HalfExtents)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		const FTransform Transform(FRotator::ZeroRotator, Position, HalfExtents / 50.0);
		AStaticMeshActor* Actor = World.GetTestWorld()->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform);
		if (!Cube || !Actor) return nullptr;
		// SetStaticMesh checks the WORLD's begun-play flag even for a deferred actor.
		Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		if (!Actor->GetStaticMeshComponent()->SetStaticMesh(Cube)) return nullptr;
		Actor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Actor->FinishSpawning(Transform);
		Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
		return Actor;
	}
	ACatPhysicsPrototypePawn* Spawn(FVector Position)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World.GetTestWorld()->SpawnActor<ACatPhysicsPrototypePawn>(Position, FRotator::ZeroRotator, Params);
	}
	void Step(int32 Frames, ACatPhysicsPrototypePawn* InputPawn = nullptr,
		FVector2D Move = FVector2D::ZeroVector, FRotator Aim = FRotator::ZeroRotator)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			if (InputPawn) InputPawn->SetPrototypeInput(Move, Aim);
			World.TickTestWorld(1.0f / 60.0f);
		}
	}
};

UAnimationAsset* CurrentAnimation(ACatPhysicsPrototypePawn* Pawn)
{
	auto* Visual = Pawn ? Pawn->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>() : nullptr;
	auto* Source = Visual ? Visual->GetAnimationSource() : nullptr;
	auto* Instance = Source ? Source->GetSingleNodeInstance() : nullptr;
	return Instance ? Instance->GetCurrentAsset() : nullptr;
}

UAnimSequence* JumpAsset(const TCHAR* Name)
{
	return LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/Animalia/Cat/Animations/InPlace/%s.%s"), Name, Name));
}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPhysicsPrototypeBodyContactTest,
	"Catfishing.PhysicsGrabPrototype.Runtime.RealBodyFallsPushesAndStopsAtWall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatPhysicsPrototypeBodyContactTest::RunTest(const FString& Parameters)
{
	CatPhysicsGrabTest::FScene Scene;
	if (!TestTrue(TEXT("creates and begins a real physics world"), Scene.Initialize(this))) return false;
	ACatPhysicsPrototypePawn* Falling = Scene.Spawn(FVector(-300, 0, 120));
	if (!TestNotNull(TEXT("spawns native physical cat"), Falling)) return false;
	TestTrue(TEXT("authority body actually simulates Chaos physics"), Falling->GetPhysicsBody()->IsSimulatingPhysics());
	TestTrue(TEXT("both hands are real simulated bodies"), Falling->GetLeftHand()->IsSimulatingPhysics()
		&& Falling->GetRightHand()->IsSimulatingPhysics());
	Scene.Step(120);
	TestTrue(TEXT("gravity moves the body from its airborne initial position onto the floor"),
		Falling->GetActorLocation().Z < 60.0 && Falling->GetActorLocation().Z > 5.0);
	TestTrue(TEXT("floor support is observed by the production body controller"), Falling->IsPrototypeGrounded());
	Falling->Destroy();

	ACatPhysicsPrototypePawn* Pusher = Scene.Spawn(FVector(-40, 0, 20));
	ACatPhysicsPrototypePawn* Target = Scene.Spawn(FVector(0, 0, 20));
	if (!Pusher || !Target || !Scene.AddBox(FVector(160, 0, 75), FVector(10, 200, 75))) return false;
	Scene.Step(30);
	const FVector Start = Target->GetActorLocation();
	Scene.Step(300, Pusher, FVector2D(1, 0));
	TestTrue(TEXT("walking body transfers contact force to the other real cat"), Target->GetActorLocation().X > Start.X + 10.0);
	TestTrue(TEXT("both bodies remain outside the blocking wall"),
		Target->GetActorLocation().X < 150.0 && Pusher->GetActorLocation().X < 150.0);
	TestTrue(TEXT("the forward body actually reaches the wall during the sustained push"), Target->GetActorLocation().X > 130.0);
	TestTrue(TEXT("contact does not pass the rear cat through the front cat"),
		Pusher->GetActorLocation().X < Target->GetActorLocation().X);
	TestTrue(TEXT("body velocity remains finite after sustained wall contact"),
		!Pusher->GetVelocity().ContainsNaN() && !Target->GetVelocity().ContainsNaN());
	AddInfo(FString::Printf(TEXT("Event=physics_prototype_contact_verified TargetTravelCm=%.3f PusherX=%.3f TargetX=%.3f"),
		Target->GetActorLocation().X - Start.X, Pusher->GetActorLocation().X, Target->GetActorLocation().X));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPhysicsPrototypeGrabLifecycleTest,
	"Catfishing.PhysicsGrabPrototype.Runtime.RealGripTransfersForceAndCleansUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatPhysicsPrototypeGrabLifecycleTest::RunTest(const FString& Parameters)
{
	CatPhysicsGrabTest::FScene Scene;
	if (!TestTrue(TEXT("creates actual grab physics world"), Scene.Initialize(this))) return false;
	ACatPhysicsPrototypePawn* Puller = Scene.Spawn(FVector(0, 0, 20));
	ACatPhysicsPrototypePawn* Target = Scene.Spawn(FVector(39, 0, 20));
	if (!Puller || !Target) return false;
	Scene.Step(30);
	Puller->SetPrototypeInput(FVector2D::ZeroVector, FRotator::ZeroRotator);
	Puller->SetGrabInput(true, true);
	Scene.Step(60, Puller);
	UCatPhysicsGrabComponent* Grab = Puller->GetGrabComponent();
	if (!TestTrue(TEXT("reaching creates a real persistent contact with the second cat"),
		Grab->IsGripping(true) && Grab->GetGripTarget(true) == Target)) return false;
	const uint32 GripRevision = Grab->GetGripRevision(true);
	const FVector PullerStart = Puller->GetActorLocation();
	const FVector TargetStart = Target->GetActorLocation();
	Scene.Step(60, Puller, FVector2D(-1, 0));
	TestTrue(TEXT("backward movement pulls the second real physics body"), Target->GetActorLocation().X < TargetStart.X - 2.0);
	TestTrue(TEXT("puller also moves under the shared physical connection"), Puller->GetActorLocation().X < PullerStart.X - 2.0);
	TestTrue(TEXT("connection survives ordinary pulling without stretching across the arena"),
		Grab->IsGripping(true) && FVector::Distance(Puller->GetActorLocation(), Target->GetActorLocation()) < 100.0);
	Puller->SetGrabInput(true, false);
	Scene.Step(2, Puller);
	TestFalse(TEXT("release removes the grip"), Grab->IsGripping(true));
	TestFalse(TEXT("release stops reaching"), Grab->IsReaching(true));
	TestNull(TEXT("release clears the old target"), Grab->GetGripTarget(true));
	TestTrue(TEXT("release publishes a newer observed state"), Grab->GetGripRevision(true) > GripRevision);

	// A fresh scene position exercises target destruction using the same public reach/contact path.
	Puller->RequestReset();
	Target->RequestReset();
	Scene.Step(30);
	Puller->SetGrabInput(true, true);
	Scene.Step(60, Puller);
	if (!TestTrue(TEXT("a new contact is established after release and reset"),
		Grab->IsGripping(true) && Grab->GetGripTarget(true) == Target)) return false;
	Target->Destroy();
	Scene.Step(3, Puller);
	TestFalse(TEXT("destroying a target removes its physical connection"), Grab->IsGripping(true));
	TestNull(TEXT("destroying a target clears the replicated target reference"), Grab->GetGripTarget(true));
	Puller->SetGrabInput(true, false);
	TestFalse(TEXT("cleanup remains safe when release arrives after destruction"), Grab->IsReaching(true));
	AddInfo(TEXT("Event=physics_prototype_grip_lifecycle_verified Result=ForceTransferReleaseDestroy"));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPhysicsPrototypeVisualConsumerTest,
	"Catfishing.PhysicsGrabPrototype.Runtime.ExistingCatPoseFollowsHandWithoutStretchingBones",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatPhysicsPrototypeVisualConsumerTest::RunTest(const FString& Parameters)
{
	CatPhysicsGrabTest::FScene Scene;
	if (!Scene.Initialize(this)) return false;
	ACatPhysicsPrototypePawn* Pawn = Scene.Spawn(FVector(0, 0, 20));
	if (!Pawn) return false;
	Scene.Step(30);
	auto* Visual = Pawn->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>();
	if (!TestNotNull(TEXT("real pawn installs the prototype visual consumer"), Visual)) return false;
	auto* Mesh = Visual->GetVisualMesh();
	auto* Animation = Visual->GetAnimationSource();
	if (!TestNotNull(TEXT("visible cat mesh exists"), Mesh) || !TestNotNull(TEXT("animation source exists"), Animation)) return false;
	TestEqual(TEXT("prototype uses the existing 105-bone cat"), Mesh->GetNumBones(), 105);
	TestTrue(TEXT("existing asset is shared by visible mesh and animation source"),
		Mesh->GetSkinnedAsset() == Animation->GetSkinnedAsset());
	TestEqual(TEXT("visible mesh does not introduce a second physics collider"), Mesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	const FName Bones[] = {TEXT("RigLFLeg1"), TEXT("RigLFLeg2"), TEXT("RigLFLeg3"), TEXT("RigLFLegAnkle")};
	double Lengths[3];
	for (int32 Link = 0; Link < 3; ++Link)
		Lengths[Link] = FVector::Distance(Mesh->GetBoneLocationByName(Bones[Link], EBoneSpaces::WorldSpace),
			Mesh->GetBoneLocationByName(Bones[Link + 1], EBoneSpaces::WorldSpace));
	Pawn->SetGrabInput(true, true);
	Scene.Step(60, Pawn);
	for (int32 Link = 0; Link < 3; ++Link)
		TestEqual(TEXT("reaching preserves each authored upper/lower/paw bone segment length"),
			FVector::Distance(Mesh->GetBoneLocationByName(Bones[Link], EBoneSpaces::WorldSpace),
				Mesh->GetBoneLocationByName(Bones[Link + 1], EBoneSpaces::WorldSpace)), Lengths[Link], 0.1);
	const double HandError = FVector::Distance(Visual->GetVisualHandWorldLocation(true), Pawn->GetLeftHand()->GetComponentLocation());
	TestTrue(TEXT("visible reaching paw tracks its physical hand contact within three centimeters"), HandError < 3.0);
	TestTrue(TEXT("source gait continues to have an active animation"), Animation->IsPlaying());
	Pawn->SetGrabInput(true, false);
	Scene.Step(20, Pawn);
	TestFalse(TEXT("visual exercise does not leave gameplay reaching active"), Pawn->GetGrabComponent()->IsReaching(true));
	AddInfo(FString::Printf(TEXT("Event=physics_prototype_visual_consumer_verified HandErrorCm=%.3f BoneSegments=3 Result=PoseContractOnly"), HandError));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPhysicsPrototypeStaticSupportTest,
	"Catfishing.PhysicsGrabPrototype.Runtime.GroundGripAndStaticContactBearBodyWeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatPhysicsPrototypeStaticSupportTest::RunTest(const FString& Parameters)
{
	CatPhysicsGrabTest::FScene Scene;
	if (!Scene.Initialize(this)) return false;
	ACatPhysicsPrototypePawn* GroundCat = Scene.Spawn(FVector(-100, 0, 20));
	if (!GroundCat) return false;
	Scene.Step(30);
	GroundCat->SetPrototypeInput(FVector2D::ZeroVector, FRotator(-80, 0, 0));
	GroundCat->SetGrabInput(true, true);
	Scene.Step(60, GroundCat, FVector2D::ZeroVector, FRotator(-80, 0, 0));
	if (!TestTrue(TEXT("looking down and reaching grips the actual ground collision"),
		GroundCat->GetGrabComponent()->IsGripping(true) && GroundCat->GetGrabComponent()->GetGripTarget(true) == Scene.Floor)) return false;
	GroundCat->SetGrabInput(true, false);
	TestFalse(TEXT("ground grip releases through the ordinary input path"), GroundCat->GetGrabComponent()->IsGripping(true));
	GroundCat->Destroy();

	AStaticMeshActor* Wall = Scene.AddBox(FVector(30, 0, 45), FVector(3, 20, 45));
	ACatPhysicsPrototypePawn* Hanging = Scene.Spawn(FVector(0, 0, 20));
	if (!Wall || !Hanging) return false;
	Scene.Step(30);
	Hanging->SetPrototypeInput(FVector2D::ZeroVector, FRotator::ZeroRotator);
	Hanging->SetGrabInput(true, true);
	Scene.Step(60, Hanging);
	if (!TestTrue(TEXT("physical hand establishes a persistent static support contact"),
		Hanging->GetGrabComponent()->IsGripping(true) && Hanging->GetGrabComponent()->GetGripTarget(true) == Wall)) return false;
	Scene.Floor->Destroy();
	Scene.Step(60, Hanging);
	const double SupportedHeight = Hanging->GetActorLocation().Z;
	TestFalse(TEXT("suspended body no longer has foot support"), Hanging->IsPrototypeGrounded());
	TestTrue(TEXT("the hand constraint still carries the body against gravity"),
		Hanging->GetGrabComponent()->IsGripping(true) && SupportedHeight > -40.0);
	Hanging->SetGrabInput(true, false);
	Scene.Step(20, Hanging);
	TestTrue(TEXT("releasing the load-bearing hand makes the body fall"), Hanging->GetActorLocation().Z < SupportedHeight - 20.0);
	AddInfo(FString::Printf(TEXT("Event=physics_prototype_static_support_verified SupportedZ=%.3f ReleasedZ=%.3f"),
		SupportedHeight, Hanging->GetActorLocation().Z));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPhysicsPrototypeDynamicRodTest,
	"Catfishing.PhysicsGrabPrototype.Runtime.ReachingGripsAndMovesThePhysicalRod",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatPhysicsPrototypeDynamicRodTest::RunTest(const FString& Parameters)
{
	CatPhysicsGrabTest::FScene Scene;
	if (!Scene.Initialize(this)) return false;
	ACatPhysicsPrototypePawn* Pawn = Scene.Spawn(FVector(0, 0, 20));
	ACatPhysicsGrabProp* Rod = Scene.World.GetTestWorld()->SpawnActor<ACatPhysicsGrabProp>(FVector(30, -3.4, 3), FRotator(90, 0, 0));
	if (!Pawn || !Rod || !Rod->ConfigureFromAuthority(FVector(2, 2, 90), true, 0.45f, FLinearColor::Yellow, true)) return false;
	Scene.Step(30);
	TestTrue(TEXT("prototype rod is an actual simulated rigid body"), Rod->GetPhysicsMesh()->IsSimulatingPhysics());
	const FRotator Aim(-70, 0, 0);
	Pawn->SetPrototypeInput(FVector2D::ZeroVector, Aim);
	Pawn->SetGrabInput(true, true);
	Scene.Step(90, Pawn, FVector2D::ZeroVector, Aim);
	if (!TestTrue(TEXT("downward reach latches the physical rod rather than the floor"),
		Pawn->GetGrabComponent()->IsGripping(true) && Pawn->GetGrabComponent()->GetGripTarget(true) == Rod)) return false;
	const FVector Before = Rod->GetActorLocation();
	Scene.Step(60, Pawn, FVector2D(-1, 0), Aim);
	TestTrue(TEXT("walking transfers force to the same rod body"), Rod->GetActorLocation().X < Before.X - 5.0);
	Pawn->SetGrabInput(true, false);
	TestFalse(TEXT("releasing the rod removes its contact"), Pawn->GetGrabComponent()->IsGripping(true));
	TestTrue(TEXT("released rod retains independent physical simulation"), Rod->GetPhysicsMesh()->IsSimulatingPhysics());
	AddInfo(FString::Printf(TEXT("Event=physics_prototype_dynamic_rod_verified TravelCm=%.3f"), Before.X - Rod->GetActorLocation().X));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPhysicsPrototypeFocusCleanupTest,
	"Catfishing.PhysicsGrabPrototype.Runtime.FocusLossReleasesBothHands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatPhysicsPrototypeFocusCleanupTest::RunTest(const FString& Parameters)
{
	CatPhysicsGrabTest::FScene Scene;
	if (!Scene.Initialize(this)) return false;
	ACatPhysicsPrototypePawn* Pawn = Scene.Spawn(FVector(0, 0, 20));
	auto* Controller = Scene.World.GetTestWorld()->SpawnActor<ACatPhysicsPrototypePlayerController>();
	if (!Pawn || !Controller) return false;
	Controller->Possess(Pawn);
	Controller->SetActorTickEnabled(false);
	Pawn->SetPrototypeInput(FVector2D::ZeroVector, FRotator(-80, 0, 0));
	Pawn->SetGrabInput(true, true);
	Pawn->SetGrabInput(false, true);
	Scene.Step(60, Pawn, FVector2D::ZeroVector, FRotator(-80, 0, 0));
	if (!TestTrue(TEXT("both held inputs are active before the viewport loses focus"),
		Pawn->GetGrabComponent()->IsReaching(true) && Pawn->GetGrabComponent()->IsReaching(false))) return false;
	Controller->FlushPressedKeys();
	for (bool bLeft : {true, false})
	{
		TestFalse(TEXT("focus loss clears each reaching state without a mouse-up event"), Pawn->GetGrabComponent()->IsReaching(bLeft));
		TestFalse(TEXT("focus loss releases any physical contact"), Pawn->GetGrabComponent()->IsGripping(bLeft));
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPhysicsPrototypeTwoHandResetTest,
	"Catfishing.PhysicsGrabPrototype.Runtime.RightHandSupportsAfterLeftReleaseAndResetClearsIncomingGrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatPhysicsPrototypeTwoHandResetTest::RunTest(const FString& Parameters)
{
	CatPhysicsGrabTest::FScene Scene;
	if (!Scene.Initialize(this)) return false;
	AStaticMeshActor* Wall = Scene.AddBox(FVector(30, 0, 45), FVector(3, 20, 45));
	ACatPhysicsPrototypePawn* MainCat = Scene.Spawn(FVector(0, 0, 20));
	if (!Wall || !MainCat) return false;
	Scene.Step(30);
	MainCat->SetPrototypeInput(FVector2D::ZeroVector, FRotator::ZeroRotator);
	MainCat->SetGrabInput(true, true);
	MainCat->SetGrabInput(false, true);
	Scene.Step(60, MainCat);
	auto* Grab = MainCat->GetGrabComponent();
	if (!TestTrue(TEXT("both real hands simultaneously constrain the same static support"),
		Grab->IsGripping(true) && Grab->IsGripping(false)
		&& Grab->GetGripTarget(true) == Wall && Grab->GetGripTarget(false) == Wall)) return false;
	const uint32 RightRevision = Grab->GetGripRevision(false);
	Scene.Floor->Destroy();
	Scene.Step(30, MainCat);
	MainCat->SetGrabInput(true, false);
	Scene.Step(30, MainCat);
	const double RightOnlyHeight = MainCat->GetActorLocation().Z;
	TestFalse(TEXT("left release clears only the left contact"), Grab->IsGripping(true));
	TestTrue(TEXT("right hand continues bearing the suspended body alone"),
		Grab->IsGripping(false) && !MainCat->IsPrototypeGrounded() && RightOnlyHeight > -40.0);
	TestEqual(TEXT("left release does not replace the right grip"), Grab->GetGripRevision(false), RightRevision);

	// Reset has an independent physical scene, so rebuilding a floor cannot embed the hanging cat.
	CatPhysicsGrabTest::FScene ResetScene;
	if (!ResetScene.Initialize(this)) return false;
	if (!ResetScene.AddBox(FVector(30, 0, 45), FVector(3, 20, 45))) return false;
	ACatPhysicsPrototypePawn* ResetCat = ResetScene.Spawn(FVector(0, 0, 20));
	ACatPhysicsPrototypePawn* Incoming = ResetScene.Spawn(FVector(-39, 0, 20));
	if (!ResetCat || !Incoming) return false;
	ResetScene.Step(30);
	ResetCat->SetPrototypeInput(FVector2D::ZeroVector, FRotator::ZeroRotator);
	ResetCat->SetGrabInput(true, true);
	ResetCat->SetGrabInput(false, true);
	ResetScene.Step(60, ResetCat);
	Incoming->SetPrototypeInput(FVector2D::ZeroVector, FRotator::ZeroRotator);
	Incoming->SetGrabInput(true, true);
	ResetScene.Step(60, Incoming);
	auto* ResetGrab = ResetCat->GetGrabComponent();
	AddInfo(FString::Printf(TEXT("Event=physics_prototype_incoming_grip_setup MainPosition=%s IncomingPosition=%s IncomingTarget=%s RightStillGripped=%d"),
		*ResetCat->GetActorLocation().ToCompactString(), *Incoming->GetActorLocation().ToCompactString(),
		*GetNameSafe(Incoming->GetGrabComponent()->GetGripTarget(true)), ResetGrab->IsGripping(false)));
	if (!TestTrue(TEXT("another real cat grips the still-holding cat before reset"),
		Incoming->GetGrabComponent()->IsGripping(true) && Incoming->GetGrabComponent()->GetGripTarget(true) == ResetCat
		&& ResetGrab->IsGripping(true) && ResetGrab->IsGripping(false))) return false;
	ResetCat->RequestReset();
	ResetScene.Step(3);
	TestFalse(TEXT("reset clears the left outgoing contact"), ResetGrab->IsGripping(true));
	TestFalse(TEXT("reset clears the right outgoing contact"), ResetGrab->IsGripping(false));
	TestFalse(TEXT("reset also clears the other cat's incoming contact"), Incoming->GetGrabComponent()->IsGripping(true));
	TestNull(TEXT("incoming grip no longer retains the reset target"), Incoming->GetGrabComponent()->GetGripTarget(true));
	TestFalse(TEXT("incoming reaching is stopped until a fresh input after reset"), Incoming->GetGrabComponent()->IsReaching(true));
	AddInfo(FString::Printf(TEXT("Event=physics_prototype_two_hand_reset_verified RightOnlyZ=%.3f Result=RightSupportsAndAllDirectionsReleased"), RightOnlyHeight));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPhysicsPrototypeJumpAnimationTest,
	"Catfishing.PhysicsGrabPrototype.Runtime.PhysicalJumpPlaysAuthoredPhasesAndKeepsPawIK",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatPhysicsPrototypeJumpAnimationTest::RunTest(const FString& Parameters)
{
	using namespace CatPhysicsGrabTest;
	CatPhysicsGrabTest::FScene Scene;
	if (!Scene.Initialize(this)) return false;
	auto* Pawn = Scene.Spawn(FVector(0, 0, 20));
	auto* Start = JumpAsset(TEXT("JumpX_Start-IP"));
	auto* Loop = JumpAsset(TEXT("JumpX_Loop-IP"));
	auto* End = JumpAsset(TEXT("JumpX_End-IP"));
	if (!Pawn || !Start || !Loop || !End) return false;
	Scene.Step(60);
	if (!TestTrue(TEXT("jump begins from actual sampled floor support"), Pawn->HasPrototypeMovementSample() && Pawn->IsPrototypeGrounded())) return false;
	auto* Visual = Pawn->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>();
	if (!Visual || !Visual->GetVisualMesh() || !Visual->GetAnimationSource()) return false;
	const FTransform ReferenceRoot = Visual->GetAnimationSource()->GetSkeletalMeshAsset()->GetRefSkeleton().GetRefBonePose()[0];
	for (auto* Asset : {Start, Loop, End})
	{
		TestEqual(TEXT("jump animation shares the existing Cat skeleton"), Asset->GetSkeleton(), Visual->GetAnimationSource()->GetSkeletalMeshAsset()->GetSkeleton());
		TestFalse(TEXT("visual jump clips do not supply a competing root-motion writer"), Asset->bEnableRootMotion);
	}
	Pawn->SetGrabInput(true, true);
	Scene.Step(20, Pawn);
	const double GroundZ = Pawn->GetActorLocation().Z;
	Pawn->RequestJump();
	const FName Bones[] = {TEXT("RigLFLeg1"), TEXT("RigLFLeg2"), TEXT("RigLFLeg3"), TEXT("RigLFLegAnkle")};
	bool bSawStart = false, bSawLoop = false, bSawEnd = false, bSawAir = false, bRejectedRepeat = false;
	double MaximumHeight = GroundZ, MaximumAirborneIKImprovement = 0.0;
	for (int32 Frame = 0; Frame < 150; ++Frame)
	{
		Scene.Step(1, Pawn);
		auto* Current = CurrentAnimation(Pawn);
		if (!TestTrue(TEXT("visible jump root remains at the skeleton reference so only the body moves the cat through space"),
			Visual->GetVisualMesh()->BoneSpaceTransforms[0].Equals(ReferenceRoot, 0.001f))) return false;
		bSawStart |= Current == Start;
		bSawLoop |= Current == Loop;
		if (Current == End)
		{
			TestTrue(TEXT("landing animation follows a real airborne interval"), bSawAir);
			TestTrue(TEXT("landing animation is driven by regained physical support"), Pawn->IsPrototypeGrounded());
			bSawEnd = true;
		}
		MaximumHeight = FMath::Max(MaximumHeight, Pawn->GetActorLocation().Z);
		if (!Pawn->IsPrototypeGrounded())
		{
			bSawAir = true;
			if (!bRejectedRepeat && Frame > 3)
			{
				const FVector Before = Pawn->GetVelocity();
				AddExpectedMessage(TEXT("Event=physics_body_jump_rejected"), ELogVerbosity::Warning);
				Pawn->RequestJump();
				// Chaos consumes AddImpulse on the next physics step, so an immediate velocity read cannot prove rejection.
				Scene.Step(1, Pawn);
				TestTrue(TEXT("an airborne repeat cannot inject another jump impulse on the next physics step"), Pawn->GetVelocity().Z < Before.Z + 50.0);
				bRejectedRepeat = true;
			}
			const FVector Target = Pawn->GetLeftHand()->GetComponentLocation();
			auto* Source = Visual->GetAnimationSource();
			const FVector AuthoredHandComponent = Source->GetComponentTransform().InverseTransformPosition(Source->GetBoneLocation(Bones[3]));
			const FVector AuthoredHandAtReferenceRoot = Source->GetComponentTransform().TransformPosition(
				ReferenceRoot.TransformPosition(Source->GetBoneSpaceTransforms()[0].InverseTransformPosition(AuthoredHandComponent)));
			const double AuthoredError = FVector::Distance(AuthoredHandAtReferenceRoot, Target);
			const double VisibleError = FVector::Distance(Visual->GetVisualHandWorldLocation(true), Target);
			MaximumAirborneIKImprovement = FMath::Max(MaximumAirborneIKImprovement, AuthoredError - VisibleError);
			for (int32 Link = 0; Link < 3; ++Link)
			{
				const double AuthoredLength = FVector::Distance(Visual->GetAnimationSource()->GetBoneLocation(Bones[Link]), Visual->GetAnimationSource()->GetBoneLocation(Bones[Link + 1]));
				const double VisibleLength = FVector::Distance(Visual->GetVisualMesh()->GetBoneLocationByName(Bones[Link], EBoneSpaces::WorldSpace), Visual->GetVisualMesh()->GetBoneLocationByName(Bones[Link + 1], EBoneSpaces::WorldSpace));
				if (!TestEqual(TEXT("airborne reaching preserves authored leg segment lengths"), VisibleLength, AuthoredLength, 0.1)) return false;
			}
		}
	}
	TestTrue(TEXT("actual impulse visibly lifts the physical body"), MaximumHeight > GroundZ + 8.0);
	TestTrue(TEXT("actual animation assets cover takeoff, flight and landing"), bSawStart && bSawLoop && bSawEnd);
	TestTrue(TEXT("airborne paw IK improves contact tracking after the jump pose is copied"), MaximumAirborneIKImprovement > 2.0);
	TestTrue(TEXT("settled body returns from landing to a ground gait"), Pawn->IsPrototypeGrounded() && CurrentAnimation(Pawn) != Start && CurrentAnimation(Pawn) != Loop && CurrentAnimation(Pawn) != End);
	Pawn->SetGrabInput(true, false);
	AddInfo(FString::Printf(TEXT("Event=physics_prototype_jump_animation_verified HeightGainCm=%.3f AirborneIKImprovementCm=%.3f Result=StartLoopLandAndReach"), MaximumHeight - GroundZ, MaximumAirborneIKImprovement));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPhysicsPrototypeJumpResetAnimationTest,
	"Catfishing.PhysicsGrabPrototype.Runtime.JumpLandingCanBeInterruptedAndResetDoesNotFakeLanding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatPhysicsPrototypeJumpResetAnimationTest::RunTest(const FString& Parameters)
{
	using namespace CatPhysicsGrabTest;
	CatPhysicsGrabTest::FScene Scene;
	if (!Scene.Initialize(this)) return false;
	auto* Pawn = Scene.Spawn(FVector(0, 0, 20));
	auto* Start = JumpAsset(TEXT("JumpX_Start-IP"));
	auto* Loop = JumpAsset(TEXT("JumpX_Loop-IP"));
	auto* End = JumpAsset(TEXT("JumpX_End-IP"));
	if (!Pawn || !Start || !Loop || !End) return false;
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		Scene.Step(1);
		if (!TestTrue(TEXT("initial ground acquisition does not fabricate a landing"), CurrentAnimation(Pawn) != End)) return false;
	}
	Pawn->RequestJump();
	bool bSawLanding = false;
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Scene.Step(1, Pawn);
		if (CurrentAnimation(Pawn) == End) { bSawLanding = true; break; }
	}
	if (!TestTrue(TEXT("first physical jump reaches its landing clip"), bSawLanding)) return false;
	Pawn->RequestJump();
	Scene.Step(2, Pawn);
	TestFalse(TEXT("new grounded jump leaves support again"), Pawn->IsPrototypeGrounded());
	TestTrue(TEXT("new takeoff interrupts landing before its clip finishes"), CurrentAnimation(Pawn) == Start);
	const uint32 BeforeReset = Pawn->GetPrototypeResetEpoch();
	Pawn->RequestReset();
	TestTrue(TEXT("reset advances the actual authority reset epoch"), Pawn->GetPrototypeResetEpoch() > BeforeReset);
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		Scene.Step(1, Pawn);
		if (!TestTrue(TEXT("reset clears airborne history without playing a false landing"), CurrentAnimation(Pawn) != End)) return false;
	}
	TestTrue(TEXT("reset settles into a real supported ground gait"), Pawn->IsPrototypeGrounded() && CurrentAnimation(Pawn) != Start && CurrentAnimation(Pawn) != Loop);
	AddInfo(TEXT("Event=physics_prototype_jump_interrupt_reset_verified Result=NewTakeoffInterruptsLandingAndResetClearsHistory"));
	return !HasAnyErrors();
}

#endif
