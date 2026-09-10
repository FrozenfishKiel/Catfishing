#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Character/Physics/Tests/CatPhysicalTestWorld.h"
#include "Character/CatCharacterMovementComponent.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"

namespace CatGrabJumpTest
{
bool Grip(ACatCharacter* Holder, ACatCharacter* Target, bool Left)
{
    auto* Body = Holder->GetPhysicalBodyComponent();
    auto* Box = Target->GetPhysicalBodyComponent()->GetBody();
    const FVector Shoulder = Body->GetGrab()->GetShoulderWorldLocation(Left);
    const double Direction = Target->GetActorLocation().X>Holder->GetActorLocation().X ? 1 : -1;
    const FVector Contact(Box->GetComponentLocation().X-Direction*Box->GetScaledBoxExtent().X,Shoulder.Y,Shoulder.Z);
    Body->SetViewIntent(FRotator(0,Direction>0 ? 0 : 180,0));
    Body->GetHand(Left)->SetWorldLocation(Contact);
    return Body->GetGrab()->GripFromAuthority(Left,Box,Contact);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatGrabJumpWorldTest,
    "Catfishing.CMC.Runtime.GrabJumpBrieflyLiftsFriendAndRetainsBackwardPull",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatGrabJumpWorldTest::RunTest(const FString& Parameters)
{
    double ReferenceRise = -1;
    for (int32 Rate : {120,60}) for (bool Hitch : {false,true}) for (int32 HandCount : {1,2,4})
    {
        const bool TwoHands=HandCount>1, Mutual=HandCount==4;
        CatPhysicalTest::FScene Scene;
        if (!Scene.Initialize(this)) return false;
        auto* A = Scene.SpawnCat(FVector(0,0,20)); auto* B = Scene.SpawnCat(FVector(32,0,20));
        if (!A || !B) return false;
        auto* AB = A->GetPhysicalBodyComponent(); auto* BB = B->GetPhysicalBodyComponent();
        if (Mutual) BB->TeleportBodyFromAuthority(FTransform(FRotator(0,180,0),B->GetActorLocation()),TEXT("MutualJumpFixture"));
        Scene.Step(Rate,Rate);
        if (!TestTrue(TEXT("real left hand latches the friend's surface"),CatGrabJumpTest::Grip(A,B,true))) return false;
        if (TwoHands && !TestTrue(TEXT("second hand independently latches the same friend"),CatGrabJumpTest::Grip(A,B,false))) return false;
        if (Mutual && !TestTrue(TEXT("both friends can hold each other with four real grips"),CatGrabJumpTest::Grip(B,A,true) && CatGrabJumpTest::Grip(B,A,false))) return false;
        const FGuid KeptGrip = AB->GetGrab()->GetGripState(TwoHands ? false : true).GripId;
        const double AZ = A->GetActorLocation().Z, BZ = B->GetActorLocation().Z;
        AB->RequestJump();
        double PeakA = AZ, PeakB = BZ, MaximumReciprocalError = 0;
        bool LiftedWithoutOwnJump = false, ReleasedFirst = false;
        for (int32 Frame=0;Frame<Rate*2;++Frame)
        {
            Scene.World.TickTestWorld(Hitch && Frame==1 ? .12f : 1.f/Rate);
            PeakA=FMath::Max(PeakA,A->GetActorLocation().Z); PeakB=FMath::Max(PeakB,B->GetActorLocation().Z);
            MaximumReciprocalError=FMath::Max(MaximumReciprocalError,FMath::Abs(AB->GetVerticalGripForceFromAuthority()+BB->GetVerticalGripForceFromAuthority()));
            if (B->GetActorLocation().Z>BZ+2 && !BB->IsGrounded()) LiftedWithoutOwnJump=true;
            TestTrue(TEXT("external lift never grants a new jump window"),BB->GetJumpTractionWeight()==0);
            if (TwoHands && !ReleasedFirst && Frame>Rate*.06)
            {
                AB->GetGrab()->ReleaseHandFromAuthority(true,TEXT("JumpFirstHandReleased"));
                TestEqual(TEXT("first hand release keeps the second grip identity"),AB->GetGrab()->GetGripState(false).GripId,KeptGrip);
                ReleasedFirst=true;
            }
        }
        AddInfo(FString::Printf(TEXT("Event=cmc_grab_jump_verified Hz=%d Hitch=%d Hands=%d JumperRiseCm=%.3f FriendRiseCm=%.3f ReciprocalErrorUE=%.6f GroundedA=%d GroundedB=%d GripRetained=%d"),
            Rate,Hitch,HandCount,PeakA-AZ,PeakB-BZ,MaximumReciprocalError,AB->IsGrounded(),BB->IsGrounded(),AB->GetGrab()->IsGripping(!TwoHands)));
        TestTrue(TEXT("real grip force briefly lifts the friend and restrains the original jump"),LiftedWithoutOwnJump && PeakA-AZ>5 && PeakA-AZ<80 && PeakB-BZ<60);
        TestTrue(TEXT("grip forces are reciprocal, both return to ground without hanging or overturning"),MaximumReciprocalError<.01 && AB->IsGrounded() && BB->IsGrounded() && A->GetActorUpVector().Z>.99999 && B->GetActorUpVector().Z>.99999);
        TestTrue(TEXT("window ends and the remaining exact grip survives the jump"),AB->GetJumpTractionWeight()==0 && FMath::Abs(BB->GetVerticalGripForceFromAuthority())<.01 && AB->GetGrab()->IsGripping(!TwoHands) && AB->GetGrab()->GetGripState(!TwoHands).GripId==KeptGrip);
        if (!Hitch && !TwoHands)
        {
            if (ReferenceRise<0) ReferenceRise=PeakB-BZ;
            else TestTrue(TEXT("60 and 120 Hz deliver comparable lift"),FMath::Abs(ReferenceRise-(PeakB-BZ))<8);
        }
        const double BeforePull=B->GetActorLocation().X;
        AB->SetMoveIntent(-FVector::ForwardVector);
        Scene.Step(Rate,Rate);
        TestTrue(TEXT("the same retained hand still drags the friend when walking backward"),B->GetActorLocation().X<BeforePull-10 && AB->GetGrab()->IsGripping(!TwoHands));
        AB->GetGrab()->ReleaseAllFromAuthority(TEXT("JumpFinalRelease"));
        if (Mutual) BB->GetGrab()->ReleaseAllFromAuthority(TEXT("MutualJumpFinalRelease"));
        TestTrue(TEXT("final release immediately clears both endpoints' grip forces"),AB->GetVerticalGripForceFromAuthority()==0 && BB->GetVerticalGripForceFromAuthority()==0);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatGrabJumpCleanupTest,
    "Catfishing.CMC.Runtime.GrabJumpReleaseTeleportAndTargetExitClearLift",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatGrabJumpCleanupTest::RunTest(const FString& Parameters)
{
    for (int32 Exit=0;Exit<3;++Exit)
    {
        CatPhysicalTest::FScene Scene;
        if (!Scene.Initialize(this)) return false;
        auto* A=Scene.SpawnCat(FVector(0,0,20)); auto* B=Scene.SpawnCat(FVector(32,0,20));
        Scene.Step(30);
        auto* AB=A->GetPhysicalBodyComponent(); auto* BB=B->GetPhysicalBodyComponent();
        if (!TestTrue(TEXT("cleanup fixture has a real grip"),CatGrabJumpTest::Grip(A,B,true))) return false;
        AB->RequestJump(); Scene.Step(1);
        TestTrue(TEXT("fixture really has a live vertical force"),FMath::Abs(BB->GetVerticalGripForceFromAuthority())>1);
        if (Exit==0) AB->GetGrab()->ReleaseAllFromAuthority(TEXT("AirRelease"));
        if (Exit==1) AB->TeleportBodyFromAuthority(FTransform(FVector(-100,0,20)),TEXT("AirTeleport"));
        if (Exit==2) B->Destroy();
        TestTrue(TEXT("exit removes the original force source immediately"),AB->GetVerticalGripForceFromAuthority()==0);
        if (Exit!=2) TestTrue(TEXT("receiver also loses the original force immediately"),BB->GetVerticalGripForceFromAuthority()==0);
        if (Exit==1) TestTrue(TEXT("teleport clears the voluntary jump window"),AB->GetJumpTractionWeight()==0);
        Scene.Step(120);
        TestTrue(TEXT("no stale force or support remains after exit"),AB->IsGrounded() && AB->GetVerticalGripForceFromAuthority()==0);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatGrabJumpPredictionTest,
    "Catfishing.CMC.Runtime.VerticalGripPredictionMatchesGroundDepartureAndAirLineForce",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatGrabJumpPredictionTest::RunTest(const FString& Parameters)
{
    for (float Dt : {1.f/120,1.f/60,.12f}) for (bool InAir : {false,true})
    {
        CatPhysicalTest::FScene Scene;
        if (!Scene.Initialize(this)) return false;
        auto* Cat=Scene.SpawnCat(FVector(0,0,20)); Scene.Step(30);
        auto* Body=Cat->GetPhysicalBodyComponent();
        auto* Movement=CastChecked<UCatCharacterMovementComponent>(Cat->GetCharacterMovement());
        if (InAir) { Body->RequestJump(); Scene.Step(3); }
        Body->SetExternalForceFromAuthority(Scene.Floor,FVector(500,0,8000),true);
        auto Predicted=Movement->CaptureMotionPrediction();
        const FVector LineForce(2,0,-12);
        UCatCharacterMovementComponent::AdvanceMotionPrediction(Predicted,LineForce,Dt);
        Body->AddExternalImpulseFromAuthority(LineForce*(100*Dt));
        Scene.World.TickTestWorld(Dt);
        const double Error=FVector::Distance(Predicted.Position,Cat->GetActorLocation());
        AddInfo(FString::Printf(TEXT("Event=cmc_vertical_prediction_verified Dt=%.6f StartedInAir=%d PositionErrorCm=%.6f VelocityErrorCmS=%.6f"),Dt,InAir,Error,FVector::Distance(Predicted.Velocity,Body->GetVelocity())));
        TestTrue(TEXT("same frozen force predicts actual CMC leaving ground and airborne fish impulse once"),Error<.1 && FVector::Distance(Predicted.Velocity,Body->GetVelocity())<.05 && !Body->IsGrounded());
        Body->ClearExternalForce(Scene.Floor);
    }
    return !HasAnyErrors();
}
#endif
