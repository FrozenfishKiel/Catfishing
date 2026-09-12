#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Character/Physics/Tests/CatPhysicalTestWorld.h"
#include "Character/CatCharacterMovementComponent.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Condition/CatConditionComponent.h"
#include "Condition/CatConditionPresentationComponent.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "Character/Physics/CatPhysicsPrototypeVisualComponent.h"
#include "GameFramework/PlayerController.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatUprightCMCWorldTest,
	"Catfishing.CMC.Runtime.UprightCapsuleKeepsGroundJumpAndExternalTraction", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatUprightCMCWorldTest::RunTest(const FString& Parameters)
{
	for (bool bFormal : {false,true}) for (int32 Rate : {60,120})
	{
		CatPhysicalTest::FScene Scene;
		if (!Scene.Initialize(this)) return false;
		FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		UClass* Type=bFormal ? LoadClass<ACatCharacter>(nullptr,TEXT("/Game/Character/BP_CatCharacter.BP_CatCharacter_C")) : ACatCharacter::StaticClass();
		auto* Cat=Scene.World.GetTestWorld()->SpawnActor<ACatCharacter>(Type,FVector(0,0,bFormal?40:20),FRotator::ZeroRotator,Spawn);
		if (!TestNotNull(TEXT("actual character"),Cat)) return false;
		auto* Body=Cat->GetPhysicalBodyComponent();
		Scene.Step(Rate,Rate);
		TestTrue(TEXT("CMC owns the actual collision capsule"), Body->UsesCharacterMovement() && Cat->GetRootComponent()==Cat->GetCapsuleComponent());
		TestFalse(TEXT("body no longer simulates a freely rotating rigid body"),Body->GetBody()->IsSimulatingPhysics());
		TestTrue(TEXT("CMC provides real floor contact"),Body->IsGrounded());
		const double StartZ=Cat->GetActorLocation().Z;
		Body->RequestJump();
		double Peak=StartZ;
		for(int32 I=0;I<Rate*2;++I) { Scene.Step(1,Rate); Peak=FMath::Max(Peak,Cat->GetActorLocation().Z); }
		TestTrue(TEXT("original 420 cm/s jump and gravity remain"),Peak-StartZ>75 && Peak-StartZ<100 && Body->IsGrounded());
		const FVector BeforePull=Cat->GetActorLocation();
		Body->SetExternalForceFromAuthority(Scene.Floor,FVector(6000,1500,100000));
		Scene.Step(Rate/3,Rate);
		TestTrue(TEXT("horizontal traction actually moves the capsule"),Cat->GetActorLocation().X>BeforePull.X+10);
		TestTrue(TEXT("unqualified prop force cannot lift or overturn the capsule"),Cat->GetActorUpVector().Z>.99999 && FMath::Abs(Cat->GetActorLocation().Z-StartZ)<.5);
		Body->ClearExternalForce(Scene.Floor);
		Scene.Step(Rate,Rate);
		// 2026-09-12：这段测的是「移动被显式关掉」这个通用开关本身，不再叫 Downed——
		// 倒地已经不关移动了（倒地者可缓慢爬行），关移动的是搬运、被抓等外力接管场景。
		Body->SetLocomotionEnabledFromAuthority(false,TEXT("CMCLocomotionDisabledContract"));
		const double DisabledZ=Cat->GetActorLocation().Z;
		Body->SetMoveIntent(FVector::ForwardVector);
		Scene.Step(Rate,Rate);
		TestTrue(TEXT("disabled locomotion retains floor support and upright collision"),Body->IsGrounded() && FMath::Abs(Cat->GetActorLocation().Z-DisabledZ)<.5 && Cat->GetActorUpVector().Z>.99999);
		TestTrue(TEXT("disabled locomotion rejects voluntary movement"),Body->GetMoveIntent().IsNearlyZero());
		Body->SetLocomotionEnabledFromAuthority(true,TEXT("CMCLocomotionRestoredContract"));
		Body->SetMoveIntent(FVector::RightVector);
		Scene.Step(Rate,Rate);
		TestTrue(TEXT("recovered character walks in the requested direction"),Body->GetVelocity().Y>95 && Cat->GetActorForwardVector().Y>.98);
		AddInfo(FString::Printf(TEXT("Event=cmc_upright_verified Formal=%d Hz=%d RiseCm=%.3f HeightCm=%.3f BodyUpZ=%.6f"),bFormal,Rate,Peak-StartZ,StartZ,Cat->GetActorUpVector().Z));
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatUprightCMCPushTest,
	"Catfishing.CMC.Runtime.PeerContactPushesWithoutOverturning", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatUprightCMCPushTest::RunTest(const FString& Parameters)
{
	CatPhysicalTest::FScene Scene;
	if (!Scene.Initialize(this)) return false;
	auto* A=Scene.SpawnCat(FVector(0,0,20)); auto* B=Scene.SpawnCat(FVector(28,0,20));
	if (!A || !B) return false;
	Scene.Step(60);
	B->GetCatAbilitySystemComponent()->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(),10);
	const FVector Start=B->GetActorLocation();
	A->GetPhysicalBodyComponent()->SetMoveIntent(FVector::ForwardVector);
	Scene.Step(120);
	TestTrue(TEXT("walking into a friend pushes them along the floor"),B->GetActorLocation().X>Start.X+25);
	TestTrue(TEXT("both contact participants remain upright"),A->GetActorUpVector().Z>.99999 && B->GetActorUpVector().Z>.99999);
	AddInfo(FString::Printf(TEXT("Event=cmc_peer_push_verified TravelCm=%.3f"),B->GetActorLocation().X-Start.X));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatUprightCMCNoHangingTest,
	"Catfishing.CMC.Runtime.AFixedGripCannotSuspendACharacter", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatUprightCMCNoHangingTest::RunTest(const FString& Parameters)
{
	CatPhysicalTest::FScene Scene;
	if (!Scene.Initialize(this)) return false;
	auto* Cat=Scene.SpawnCat(FVector(0,0,20));
	if (!Cat) return false;
	Scene.Step(60);
	auto* Body=Cat->GetPhysicalBodyComponent(); auto* Grab=Body->GetGrab();
	const FVector Shoulder=Grab->GetShoulderWorldLocation(true);
	auto* Anchor=Scene.AddBox(Shoulder+FVector(8,0,12),FVector(4));
	const FVector Contact=Shoulder+FVector(4,0,12);
	Body->GetHand(true)->SetWorldLocation(Contact);
	Body->SetViewIntent((Contact-Shoulder).Rotation());
	if (!TestTrue(TEXT("hand really grips an anchored surface"),Grab->GripFromAuthority(true,Anchor->GetStaticMeshComponent(),Contact))) return false;
	Scene.Floor->Destroy();
	Cat->GetCharacterMovement()->bForceNextFloorCheck=true;
	Scene.Step(60);
	TestTrue(TEXT("gravity continues when a gripped surface is above the character"),Cat->GetActorLocation().Z < -100);
	TestFalse(TEXT("unreachable anchor is released through the normal grip lifecycle"),Grab->IsGripping(true));
	TestTrue(TEXT("falling capsule stays upright"),Cat->GetActorUpVector().Z>.99999);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatUprightCMCConditionPoseTest,
	"Catfishing.CMC.Runtime.FormalConditionAnimatesDownAndRecoveryWithoutDroppingCapsule", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatUprightCMCConditionPoseTest::RunTest(const FString& Parameters)
{
	CatPhysicalTest::FScene Scene;
	if (!Scene.Initialize(this)) return false;
	FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Cat=Scene.World.GetTestWorld()->SpawnActor<ACatCharacter>(LoadClass<ACatCharacter>(nullptr,TEXT("/Game/Character/BP_CatCharacter.BP_CatCharacter_C")),FVector(0,0,40),FRotator::ZeroRotator,Spawn);
	auto* Controller=Scene.World.GetTestWorld()->SpawnActor<APlayerController>();
	if (!Cat || !Controller) return false;
	Controller->Possess(Cat);
	Scene.Step(120);
	auto* Visual=Cat->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>();
	auto* Presentation=Cat->FindComponentByClass<UCatConditionPresentationComponent>();
	if (!Visual || !Visual->GetVisualMesh() || !Presentation) return false;
	const double StandingHead=Visual->GetVisualMesh()->GetBoneLocationByName(TEXT("RigHead"),EBoneSpaces::WorldSpace).Z;
	const double CapsuleHeight=Cat->GetActorLocation().Z;
	AddExpectedMessage(TEXT("Event=character_downed"),ELogVerbosity::Warning);
	// 2026-09-12：倒地不再由「Poison 累加到阈值」推出，而是单条重毒鱼的结论直接裁决，所以这里走唯一的倒地入口。
	TestTrue(TEXT("severe toxicity downs the cat through the only downed entry"),Cat->GetConditionComponent()->ApplySevereToxicityFromAuthority());
	Scene.Step(600);
	TestTrue(TEXT("authoritative condition remains downed"),Cat->GetConditionComponent()->GetSnapshot().bDowned);
	TestTrue(TEXT("a downed cat can still crawl: locomotion stays on, only slowed and jump-locked"),
		Cat->GetPhysicalBodyComponent()->IsLocomotionEnabled() && Cat->GetPhysicalBodyComponent()->IsCrawlOnly()
		&& Cat->GetPhysicalBodyComponent()->GetEffectiveMaxMovementSpeedCmS()<Cat->GetPhysicalBodyComponent()->MaxMovementSpeedCmS);
	TestEqual(TEXT("authored transition reaches its lying pose"),Presentation->GetObservedPosePhase(),FName(TEXT("DownedPose")));
	const double LyingHead=Visual->GetVisualMesh()->GetBoneLocationByName(TEXT("RigHead"),EBoneSpaces::WorldSpace).Z;
	TestTrue(TEXT("actual formal mesh lies down while the collision stays supported"),LyingHead<StandingHead-3 && FMath::Abs(Cat->GetActorLocation().Z-CapsuleHeight)<.5);
	TestTrue(TEXT("real recovery command clears downed state"),CatIsAcceptedDomainCommandResult(Cat->GetConditionComponent()->RequestFieldSelfRecovery(Controller,FGuid::NewGuid())));
	Scene.Step(600);
	TestEqual(TEXT("authored get-up returns to locomotion"),Presentation->GetObservedPosePhase(),FName(TEXT("Locomotion")));
	TestTrue(TEXT("recovery restores full-speed walking without a physical flip"),Cat->GetPhysicalBodyComponent()->IsLocomotionEnabled()
		&& !Cat->GetPhysicalBodyComponent()->IsCrawlOnly() && Cat->GetActorUpVector().Z>.99999);
	AddInfo(FString::Printf(TEXT("Event=cmc_condition_pose_verified StandingHeadZ=%.3f LyingHeadZ=%.3f CapsuleZ=%.3f"),StandingHead,LyingHead,Cat->GetActorLocation().Z));
	return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatCMCTractionSubstepTest,
    "Catfishing.CMC.Runtime.TractionPredictionMatchesRealMovementAndHitchImpulse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatCMCTractionSubstepTest::RunTest(const FString& Parameters)
{
    for (const float Dt : {1.f/120,1.f/60,.05f,.12f})
    {
        CatPhysicalTest::FScene Scene;
        if (!Scene.Initialize(this)) return false;
        auto* Cat=Scene.SpawnCat(FVector(0,0,20));
        Scene.Step(30);
        auto* Body=Cat->GetPhysicalBodyComponent();
        auto* Movement=CastChecked<UCatCharacterMovementComponent>(Cat->GetCharacterMovement());
        Body->SetFishingMotorBudget(Scene.Floor,0,100);
        auto Predicted=Movement->CaptureMotionPrediction();
        const float OldStep=Movement->MaxSimulationTimeStep;
        const int32 OldIterations=Movement->MaxSimulationIterations;
        UCatCharacterMovementComponent::AdvanceMotionPrediction(Predicted,FVector(10,0,0),Dt);
        Body->AddExternalImpulseFromAuthority(FVector(1000*Dt,0,0));
        Scene.World.TickTestWorld(Dt);
        const double Error=FVector::Distance(Predicted.Position,Cat->GetActorLocation());
        AddInfo(FString::Printf(TEXT("Event=cmc_substep_prediction_verified Seconds=%.6f PositionErrorCm=%.6f SpeedCmS=%.6f"),Dt,Error,Body->GetVelocity().X));
        TestTrue(TEXT("read-only finite force predicts actual grounded CMC including a 120ms frame"),Error<.05
            && FMath::Abs(Predicted.Velocity.X-Body->GetVelocity().X)<.01);
        TestTrue(TEXT("traction overrides restore ordinary CMC settings after each movement"),Movement->MaxSimulationTimeStep==OldStep && Movement->MaxSimulationIterations==OldIterations);
        const double LoadedSpeed=Body->GetVelocity().X;
        Scene.World.TickTestWorld(Dt);
        TestTrue(TEXT("a zero-load sample consumes no duplicate impulse and does not enter ordinary walking brake"),
            Body->GetVelocity().X<=LoadedSpeed && Body->GetVelocity().X>LoadedSpeed*.8 && Body->HasFishingMotor());
        auto* Wall=Scene.AddBox(Cat->GetActorLocation()+FVector(60,0,20),FVector(5,100,100));
        const FTransform Before=Cat->GetActorTransform();
        const double Limit=Movement->GetExternalTractionTravelLimit(FVector::ForwardVector,100);
        TestTrue(TEXT("capsule query limits traction at a real wall without moving the character"),Wall && Limit>30 && Limit<50 && Before.Equals(Cat->GetActorTransform()));
        TestEqual(TEXT("retreating away from the wall retains requested travel"),Movement->GetExternalTractionTravelLimit(-FVector::ForwardVector,100),100.0);
        Wall->Destroy();
        Body->SetFishingMotorBudget(Scene.Floor,5000,100);
        Body->AddExternalImpulseFromAuthority(FVector(300,0,0));
        for (int32 I=0;I<60;++I)
        {
            Scene.World.TickTestWorld(1.f/60);
            TestTrue(TEXT("finite support decelerates to zero without reversing toward an old stance"),Body->GetVelocity().X>=-1.e-4);
        }
        TestTrue(TEXT("stance settles exactly without a spring tail"),Body->GetVelocity().Size2D()<.01);
        Body->ClearFishingMotorBudget(Scene.Floor);
    }
    return !HasAnyErrors();
}

#endif
