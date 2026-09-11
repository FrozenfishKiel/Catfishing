#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Character/Animation/CatForceReactionComponent.h"
#include "Character/Physics/Tests/CatPhysicalTestWorld.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatForceGateTest,"Catfishing.ForceReaction.Contract.OnceUntilStableUnload",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCatForceGateTest::RunTest(const FString& Parameters)
{
    for (const int32 Hz:{30,60,120})
    {
        FCatForceReactionGate Gate;
        int32 Events=0; const double Dt=1.0/Hz;
        for (int32 I=0;I<Hz*5;++I) Events+=Gate.Step(10,Dt,5,2,.2);
        TestEqual(TEXT("sustained force outlasts montage without retrigger"),Events,1);
        for (int32 I=0;I<Hz*2;++I) Events+=Gate.Step(I%2 ? 10 : 0,Dt,5,2,.2);
        TestEqual(TEXT("one-frame force gaps never rearm"),Events,1);
        for (int32 I=0;I<Hz;++I) Events+=Gate.Step(3,Dt,5,2,.2);
        TestTrue(TEXT("hysteresis band remains loaded"),Gate.bLoaded);
        for (int32 I=0;I<Hz;++I) Events+=Gate.Step(0,Dt,5,2,.2);
        TestFalse(TEXT("stable release rearms"),Gate.bLoaded);
        Events+=Gate.Step(5,Dt,5,2,.2);
        TestEqual(TEXT("next onset plays exactly once"),Events,2);
    }
    for (const double Yaw:{0.,45.,90.,180.,270.})
    {
        const FQuat Q=FRotator(0,Yaw,0).Quaternion(); const FVector F=Q.GetForwardVector();
        TestEqual(TEXT("pull from front reacts forward"),FCatForceReactionGate::Direction(F,F),ECatForceReactionDirection::Forward);
        TestEqual(TEXT("push from front reacts backward"),FCatForceReactionGate::Direction(F,-F),ECatForceReactionDirection::Backward);
        TestEqual(TEXT("right force"),FCatForceReactionGate::Direction(F,Q.GetRightVector()),ECatForceReactionDirection::Right);
        TestEqual(TEXT("left force"),FCatForceReactionGate::Direction(F,-Q.GetRightVector()),ECatForceReactionDirection::Left);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatForceSourcesTest,"Catfishing.ForceReaction.Runtime.CharacterForceSourcesAndCleanup",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCatForceSourcesTest::RunTest(const FString& Parameters)
{
    CatPhysicalTest::FScene Scene; if (!Scene.Initialize(this)) return false;
    auto* A=Scene.SpawnCat(FVector(0,0,20)); auto* B=Scene.SpawnCat(FVector(32,0,20)); Scene.Step(30);
    auto* AB=A->GetPhysicalBodyComponent(); auto* BB=B->GetPhysicalBodyComponent(); FVector D;
    AB->SetExternalForceFromAuthority(Scene.Floor,FVector(10000,0,0));
    TestEqual(TEXT("unrelated external loads do not play peer reaction"),AB->GetCharacterInteractionLoadFromAuthority(D),0.);
    AB->ClearExternalForce(Scene.Floor);
    AB->SetExternalForceFromAuthority(Scene.Floor,FVector(1000,0,0),false,false,true);
    AB->SetExternalForceFromAuthority(B,FVector(-1000,0,0),false,false,true);
    TestEqual(TEXT("cancelled forces retain 10 N load"),AB->GetCharacterInteractionLoadFromAuthority(D),10.);
    TestTrue(TEXT("cancelled force has a deterministic nonzero fallback direction"),D.Size()>0);
    AB->ClearExternalForce(Scene.Floor); AB->ClearExternalForce(B);
    AB->SetExternalForceFromAuthority(B,FVector(0,0,1000),true,false,true);
    TestEqual(TEXT("vertical lift does not select arbitrary horizontal montage"),AB->GetCharacterInteractionLoadFromAuthority(D),0.);
    AB->ClearExternalForce(B);
    auto* Grab=AB->GetGrab(); auto* Box=BB->GetBody();
    const FVector Shoulder=Grab->GetShoulderWorldLocation(true);
    const FVector Contact(Box->GetComponentLocation().X-Box->GetScaledBoxExtent().X,Shoulder.Y,Shoulder.Z);
    AB->SetViewIntent(FRotator::ZeroRotator); AB->GetHand(true)->SetWorldLocation(Contact);
    if (!TestTrue(TEXT("real production grip established"),Grab->GripFromAuthority(true,Box,Contact))) return false;
    AB->SetMoveIntent(-FVector::ForwardVector);
    double Peak=0;
    for (int32 I=0;I<30;++I)
    {
        Scene.Step(1); FVector OtherD;
        Peak=FMath::Max(Peak,BB->GetCharacterInteractionLoadFromAuthority(D));
        AB->GetCharacterInteractionLoadFromAuthority(OtherD);
        if (!D.IsNearlyZero()) TestTrue(TEXT("real grip supplies opposite force directions"),FVector::DotProduct(D,OtherD)<0);
    }
    TestTrue(TEXT("real ApplyTraction reaches reaction sample"),Peak>5);
    Grab->ReleaseAllFromAuthority(TEXT("ReactionTest"));
    TestEqual(TEXT("release clears target reaction source"),BB->GetCharacterInteractionLoadFromAuthority(D),0.);
    return !HasAnyErrors();
}
#endif
