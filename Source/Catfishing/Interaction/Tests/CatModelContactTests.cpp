#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Character/Physics/Tests/CatPhysicalTestWorld.h"
#include "Character/Physics/CatPhysicsPrototypeVisualComponent.h"
#include "Character/CatCharacterMovementComponent.h"
#include "Interaction/CatModelContactComponent.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SphereComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "Engine/SkeletalMesh.h"
#include "Physics/Experimental/PhysInterface_Chaos.h"
#include "Physics/PhysicsInterfaceTypes.h"
#include "Chaos/ChaosEngineInterface.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"

namespace CatModelContactTest
{
ACatCharacter* Spawn(CatPhysicalTest::FScene& Scene, const TCHAR* Path, FVector Position)
{
    auto* Type = LoadClass<ACatCharacter>(nullptr, Path);
    if (!Type) return nullptr;
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    return Scene.World.GetTestWorld()->SpawnActor<ACatCharacter>(Type, Position, FRotator::ZeroRotator, Params);
}
// Measure actual shape penetration independently of the upright solver's estimated
// horizontal correction travel (which is longer than the MTD on tilted surfaces).
double ShapePenetration(UCatModelContactComponent* First, UCatModelContactComponent* Second)
{
    double Maximum = 0;
    for (UCatModelContactBody* A : First->GetBodies()) for (UCatModelContactBody* B : Second->GetBodies())
    {
        if (!A->Bounds.GetBox().Intersect(B->Bounds.GetBox())) continue;
        auto* BI = A->GetBodyInstance(); auto* OtherBI = B->GetBodyInstance();
        FPhysicsCommand::ExecuteRead(BI->GetPhysicsActor(), OtherBI->GetPhysicsActor(),
            [&](const FPhysicsActorHandle&, const FPhysicsActorHandle& ActorB)
        {
            PhysicsInterfaceTypes::FInlineShapeArray Shapes;
            FPhysicsInterface::GetAllShapes_AssumedLocked(ActorB, Shapes);
            for (const auto& Shape : Shapes)
            {
                if (!Shape.GetGeometry().IsConvex()) continue;
                FMTDResult MTD;
                if (FPhysicsInterface::Overlap_Geom(BI, FPhysicsInterface::GetGeometryCollection(Shape),
                    FTransform(B->GetComponentQuat(), B->GetComponentLocation()), &MTD))
                    Maximum = FMath::Max(Maximum, double(MTD.Distance));
            }
        });
    }
    return Maximum;
}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatModelContactAssetsTest,
    "Catfishing.ModelContacts.Runtime.AuthoredBodiesFollowFinalPoseAndKeepTerrainSupport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatModelContactAssetsTest::RunTest(const FString& Parameters)
{
    for (const TCHAR* Path : {TEXT("/Game/Character/BP_CatCharacter.BP_CatCharacter_C"),
        TEXT("/Game/Character/BP_CuteCatCharacter.BP_CuteCatCharacter_C")})
    {
        CatPhysicalTest::FScene Scene;
        if (!Scene.Initialize(this)) return false;
        auto* Cat = CatModelContactTest::Spawn(Scene, Path, FVector(0,0,40));
        if (!TestNotNull(TEXT("formal blueprint"), Cat)) return false;
        Scene.Step(90);
        for (UActorComponent* Component : Cat->GetComponents())
            if (const auto* Primitive = Cast<UPrimitiveComponent>(Component); Primitive && Primitive->IsCollisionEnabled())
                AddInfo(FString::Printf(TEXT("Event=model_contact_bounds_audit Blueprint=%s Component=%s Type=%s Scale=%s Origin=%s ExtentCm=%s"),
                    Path, *Primitive->GetName(), *Primitive->GetClass()->GetName(), *Primitive->GetComponentScale().ToString(),
                    *Primitive->Bounds.Origin.ToString(), *Primitive->Bounds.BoxExtent.ToString()));
        auto* Model = Cat->FindComponentByClass<UCatModelContactComponent>();
        auto* Visual = Cat->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>()->GetVisualMesh();
        if (!TestTrue(TEXT("actual model asset creates multiple contact bodies"), Model && Model->GetBodies().Num()>2)) return false;
        int32 Shapes = 0;
        for (UCatModelContactBody* Contact : Model->GetBodies())
        {
            TestTrue(TEXT("the authored body creates a real query shape"), Contact->GetBodyInstance()->IsValidBodyInstance());
            TestTrue(TEXT("contact follows final displayed bone including scale"), Contact->GetComponentTransform().Equals(
                Visual->GetBoneTransformByName(Contact->GetBoneName(), EBoneSpaces::WorldSpace), .001));
            TestEqual(TEXT("model contacts cannot become CMC terrain"), Contact->GetCollisionResponseToChannel(ECC_Pawn), ECR_Ignore);
            TestFalse(TEXT("contacts do not simulate or overturn the cat"), Contact->IsSimulatingPhysics());
            Shapes += Contact->GetBodySetup()->AggGeom.GetElementCount();
            TestTrue(TEXT("each contact fits within the character's physical scale, including imported bone units"),
                Contact->Bounds.BoxExtent.GetMax() < Cat->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()*1.5);
            const auto& Geometry = Contact->GetBodySetup()->AggGeom;
            AddInfo(FString::Printf(TEXT("Event=model_contact_geometry_audit Bone=%s Spheres=%d Boxes=%d Capsules=%d Convex=%d LocalExtent=%s MeshExtent=%s"),
                *Contact->GetBoneName().ToString(), Geometry.SphereElems.Num(), Geometry.BoxElems.Num(), Geometry.SphylElems.Num(), Geometry.ConvexElems.Num(),
                *Geometry.CalcAABB(FTransform::Identity).GetExtent().ToString(), *Visual->Bounds.BoxExtent.ToString()));
            for (const auto& Sphere : Geometry.SphereElems) AddInfo(FString::Printf(TEXT("Sphere Radius=%f Center=%s"),Sphere.Radius,*Sphere.Center.ToString()));
            for (const auto& Capsule : Geometry.SphylElems) AddInfo(FString::Printf(TEXT("Capsule Radius=%f Length=%f Center=%s"),Capsule.Radius,Capsule.Length,*Capsule.Center.ToString()));
        }
        const double GroundZ = Cat->GetActorLocation().Z;
        Cat->GetPhysicalBodyComponent()->SetMoveIntent(FVector::ForwardVector);
        Scene.Step(60);
        for (UCatModelContactBody* Contact : Model->GetBodies()) TestTrue(TEXT("walking animation updates the contact pose"), Contact->GetComponentTransform().Equals(
            Visual->GetBoneTransformByName(Contact->GetBoneName(), EBoneSpaces::WorldSpace), .001));
        Cat->GetPhysicalBodyComponent()->SetMoveIntent(FVector::ZeroVector);
        Cat->GetPhysicalBodyComponent()->RequestJump();
        double Peak = GroundZ;
        for (int32 I=0; I<120; ++I) { Scene.Step(1); Peak=FMath::Max(Peak,Cat->GetActorLocation().Z); }
        TestTrue(TEXT("model collision retains the original grounded jump"), Peak-GroundZ>75 && Peak-GroundZ<100
            && Cat->GetPhysicalBodyComponent()->IsGrounded());
        AddInfo(FString::Printf(TEXT("Event=model_contact_asset_verified Blueprint=%s Bodies=%d Shapes=%d RiseCm=%.3f"), Path, Model->GetBodies().Num(), Shapes, Peak-GroundZ));
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatModelContactGripTest,
    "Catfishing.ModelContacts.Runtime.RealReachUsesModelSurfaceAndReleasesOnExit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatModelContactGripTest::RunTest(const FString& Parameters)
{
    for (const TCHAR* Path : {TEXT("/Game/Character/BP_CatCharacter.BP_CatCharacter_C"),
        TEXT("/Game/Character/BP_CuteCatCharacter.BP_CuteCatCharacter_C")})
    {
    CatPhysicalTest::FScene Scene;
    if (!Scene.Initialize(this)) return false;
    auto* Target = CatModelContactTest::Spawn(Scene,Path,FVector(0,0,40));
    auto* Holder = Scene.SpawnCat(FVector(200,0,20));
    if (!Target || !Holder) return false;
    Scene.Step(90);
    auto* Model = Target->FindComponentByClass<UCatModelContactComponent>();
    auto* Body = Holder->GetPhysicalBodyComponent(); auto* Grab = Body->GetGrab();
    if (!TestTrue(TEXT("target has authored contact surfaces"),Model && Model->HasModelContacts())) return false;
    // Aim at the exposed side of an authored torso/head body, through the old proxy if present.
    auto* Contact = Model->GetBodies()[0].Get();
    for (UCatModelContactBody* Candidate : Model->GetBodies())
        if (Candidate->Bounds.BoxExtent.GetMax() > Contact->Bounds.BoxExtent.GetMax()) Contact = Candidate;
    const FVector Center = Contact->Bounds.Origin;
    const FVector Start = Center + FVector(0,Contact->Bounds.BoxExtent.Y+8,0);
    const FRotator Facing(0,-90,0);
    const FVector ShoulderOffset = Holder->GetActorTransform().InverseTransformPosition(Grab->GetShoulderWorldLocation(true));
    Body->TeleportBodyFromAuthority(FTransform(Facing, Start-Facing.RotateVector(ShoulderOffset)),TEXT("ModelGripFixture"));
    Body->SetViewIntent(FRotator(0,-90,0));
    Grab->SetGrabInput(true,true);
    Scene.Step(2);
    TestTrue(TEXT("real mouse reach captures a model surface"), Grab->IsGripping(true)
        && Grab->GetGripTarget(true)==Target && Grab->GetGripTargetComponent(true)->IsA<UCatModelContactBody>());
    if (!Grab->IsGripping(true)) return false;
    const FGuid GripId=Grab->GetGripState(true).GripId;
    const FVector Before = Grab->GetGripWorldLocation(true);
    Target->AddActorWorldOffset(FVector(1,0,0),false,nullptr,ETeleportType::TeleportPhysics);
    Model->RefreshPose();
    TestTrue(TEXT("grip stays on the same local model point after target motion"), Grab->GetGripWorldLocation(true).Equals(Before+FVector(1,0,0),.01));
    TestEqual(TEXT("following a bone does not recreate a grip"),Grab->GetGripState(true).GripId,GripId);
    auto* GrippedBody = CastChecked<UCatModelContactBody>(Grab->GetGripTargetComponent(true));
    auto* Pose = Target->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>()->GetVisualMesh();
    const FVector BeforeBoneMove = Grab->GetGripWorldLocation(true);
    const FName Bone = GrippedBody->GetBoneName();
    Pose->SetBoneLocationByName(Bone,Pose->GetBoneLocationByName(Bone,EBoneSpaces::WorldSpace)+FVector(0,0,1),EBoneSpaces::WorldSpace);
    Pose->RefreshBoneTransforms(); Model->RefreshPose();
    TestTrue(TEXT("the retained surface point follows its bone independently of the actor root"),Grab->GetGripWorldLocation(true).Equals(BeforeBoneMove+FVector(0,0,1),.01));
    Target->Destroy();
    TestFalse(TEXT("target exit immediately clears model grip"),Grab->IsGripping(true));
    Scene.Step(2);
    TestTrue(TEXT("target exit clears reciprocal traction"),Grab->GetLastTractionForceForDiagnostics(true).IsNearlyZero());
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatModelContactPushTest,
    "Catfishing.ModelContacts.Runtime.ModelContactReplacesCapsulePeerPush",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatModelContactPushTest::RunTest(const FString& Parameters)
{
    for (const TCHAR* Path : {TEXT("/Game/Character/BP_CatCharacter.BP_CatCharacter_C"),
        TEXT("/Game/Character/BP_CuteCatCharacter.BP_CuteCatCharacter_C")})
    {
    CatPhysicalTest::FScene Scene;
    if (!Scene.Initialize(this)) return false;
    auto* A=CatModelContactTest::Spawn(Scene,Path,FVector(0,0,40));
    auto* B=CatModelContactTest::Spawn(Scene,Path,FVector(0,200,40));
    if (!A || !B) return false;
    Scene.Step(60);
    auto* MA=A->FindComponentByClass<UCatModelContactComponent>();
    auto* MB=B->FindComponentByClass<UCatModelContactComponent>();
    if (!MA->HasModelContacts() || !MB->HasModelContacts()) return false;
    FVector Normal; double Depth=0;
    TestFalse(TEXT("separated models do not push"),MA->FindPeerContact(MB,Normal,Depth));
    TestTrue(TEXT("model peers are excluded from capsule swept movement"),A->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(B)
        && B->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(A));
    const auto* Visible = A->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>()->GetVisualMesh();
    const auto* Mesh = CastChecked<USkeletalMesh>(Visible->GetSkinnedAsset());
    const double VisibleWidth = Mesh->GetImportedBounds().TransformBy(Visible->GetComponentTransform()).BoxExtent.Y*2;
    // CuteCat's new vertex hull must fit its imported render width. The original cat keeps its
    // authored rounded capsules (wider than skin); its independent regression is the 42 cm gap
    // inside the 52 cm CMC diameter. Do not silently impose a different shape on that asset.
    const bool bVertexFitted = Mesh->GetName()==TEXT("SK_CuteCat");
    const double ClearDistance = bVertexFitted ? VisibleWidth+10 : 42;
    const double ContactLimit = bVertexFitted ? VisibleWidth+2 : ClearDistance;
    B->SetActorLocation(A->GetActorLocation()+FVector(0,ClearDistance,0),false,nullptr,ETeleportType::TeleportPhysics);
    MB->RefreshPose();
    TestFalse(TEXT("separated visible bodies do not collide, including the original cat's capsule gap"),MA->FindPeerContact(MB,Normal,Depth));
    const FVector GapPosition=B->GetActorLocation();
    Scene.Step(30);
    TestTrue(TEXT("a peer outside the rendered model cannot be displaced by an invisible air wall"),B->GetActorLocation().Equals(GapPosition,.1));
    double FirstContactDistance=0;
    for (double Distance=100;Distance>0;Distance-=1)
    {
        B->SetActorLocation(A->GetActorLocation()+FVector(0,Distance,0),false,nullptr,ETeleportType::TeleportPhysics);
        MB->RefreshPose();
        if (MA->FindPeerContact(MB,Normal,Depth)) {FirstContactDistance=Distance;break;}
    }
    TestTrue(TEXT("authored shapes produce a finite separating horizontal contact"),FirstContactDistance>0 && Depth>0 && Normal.Y>0 && FMath::Abs(Normal.Z)<.001);
    TestTrue(TEXT("peer contact respects the variant's independently verified surface envelope"),FirstContactDistance<ContactLimit);
    const double Before=B->GetActorLocation().Y;
    A->GetPhysicalBodyComponent()->SetMoveIntent(FVector::RightVector);
    Scene.Step(120);
    TestTrue(TEXT("real model contact pushes the other upright CMC"),B->GetActorLocation().Y>Before+10 && B->GetActorUpVector().Z>.999);
    AddInfo(FString::Printf(TEXT("Event=model_contact_push_verified Blueprint=%s ContactDistanceCm=%.3f RenderMeshWidthCm=%.3f ClearDistanceCm=%.3f CapsuleDiameterCm=%.3f PeerTravelCm=%.3f"),
        Path,FirstContactDistance,VisibleWidth,ClearDistance,A->GetCapsuleComponent()->GetScaledCapsuleRadius()*2,B->GetActorLocation().Y-Before));
    }
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatModelWalkingPushSlopeTest,
    "Catfishing.ModelContacts.Runtime.WalkingPushOnSlopesWithoutReaching",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatModelWalkingPushSlopeTest::RunTest(const FString&)
{
    for (const TCHAR* Path : {TEXT("/Game/Character/BP_CatCharacter.BP_CatCharacter_C"),
        TEXT("/Game/Character/BP_CuteCatCharacter.BP_CuteCatCharacter_C")})
    for (const double Pitch : {0.0, 25.0, -25.0}) for (const int32 Variant : {0, 1, 2, 3})
    {
        CatPhysicalTest::FScene Scene;
        if (!Scene.Initialize(this)) return false;
        const int32 Hz = Variant==1 ? 120 : 60;
        const bool bHitch = Variant==2, bWall = Variant==3;
        const double Roll = bHitch ? 20.0 : 0.0;
        const FRotator Slope(Pitch,0,Roll);
        Scene.Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        Scene.Floor->SetActorLocationAndRotation(-Slope.RotateVector(FVector::UpVector)*10, Slope);
        Scene.Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
        if (bWall) Scene.AddBox(FVector(300,0,150),FVector(10,400,400));
        auto* A=CatModelContactTest::Spawn(Scene,Path,FVector(0,0,140));
        auto* B=CatModelContactTest::Spawn(Scene,Path,FVector(160,0,140+160*FMath::Tan(FMath::DegreesToRadians(Pitch))));
        if (!A || !B) return false;
        for (auto* Cat : {A,B})
        {
            auto* ASC=Cat->GetCatAbilitySystemComponent();
            ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(),50);
            ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(),60);
            ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(),60);
        }
        Scene.Step(Hz*2,Hz);
        if (!TestTrue(TEXT("both formal cats settle onto the actual slope"),A->GetPhysicalBodyComponent()->IsGrounded() && B->GetPhysicalBodyComponent()->IsGrounded())) return false;
        const FVector StartA=A->GetActorLocation(), StartB=B->GetActorLocation();
        A->GetPhysicalBodyComponent()->SetMoveIntent(FVector::ForwardVector);
        double MaximumDepth=0, MaximumSeparation=0;
        auto* ModelA=A->FindComponentByClass<UCatModelContactComponent>();
        auto* ModelB=B->FindComponentByClass<UCatModelContactComponent>();
        for (int32 Frame=0;Frame<Hz*4;++Frame)
        {
            Scene.Step(1,bHitch && Frame%30==0 ? 10 : Hz);
            FVector Normal; double Depth;
            if (ModelA->FindPeerContact(ModelB,Normal,Depth))
            {
                MaximumSeparation=FMath::Max(MaximumSeparation,Depth);
                MaximumDepth=FMath::Max(MaximumDepth,CatModelContactTest::ShapePenetration(ModelA,ModelB));
            }
        }
        const double Travel=B->GetActorLocation().X-StartB.X;
        AddInfo(FString::Printf(TEXT("Event=walking_peer_push_slope_verified Blueprint=%s Pitch=%.1f Roll=%.1f Hz=%d Hitch=%d Wall=%d TravelCm=%.3f PusherTravelCm=%.3f MaxShapePenetrationCm=%.3f MaxHorizontalCorrectionEstimateCm=%.3f FinalSeparationCm=%.3f PeerStamina=%.3f Grounded=%d A=%s B=%s"),
            Path,Pitch,Roll,Hz,bHitch,bWall,Travel,A->GetActorLocation().X-StartA.X,MaximumDepth,MaximumSeparation,B->GetActorLocation().X-A->GetActorLocation().X,
            B->GetCatAbilitySystemComponent()->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()),B->GetPhysicalBodyComponent()->IsGrounded(), *A->GetActorLocation().ToCompactString(), *B->GetActorLocation().ToCompactString()));
        TestTrue(TEXT("walking alone pushes an equally strong idle friend along the slope"),Travel>40);
        TestTrue(TEXT("bodies remain ordered instead of walking through each other"),B->GetActorLocation().X>A->GetActorLocation().X+10);
        TestTrue(TEXT("contact does not leave deeply interpenetrating bodies"),MaximumDepth<8);
        TestEqual(TEXT("an idle friend does not pay active support stamina for body contact"),
            B->GetCatAbilitySystemComponent()->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()),60.0f);
        if (bWall) TestTrue(TEXT("contact correction never moves either terrain capsule through the wall"),
            A->GetActorLocation().X+A->GetCapsuleComponent()->GetScaledCapsuleRadius()<290.5
            && B->GetActorLocation().X+B->GetCapsuleComponent()->GetScaledCapsuleRadius()<290.5);
        TestTrue(TEXT("both cats keep real terrain support and stay upright"),A->GetPhysicalBodyComponent()->IsGrounded() && B->GetPhysicalBodyComponent()->IsGrounded()
            && A->GetActorUpVector().Z>.999 && B->GetActorUpVector().Z>.999);
        if (bWall)
        {
            auto* ASC=A->GetCatAbilitySystemComponent();
            const double Before=ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
            const FVector BeforePosition=A->GetActorLocation();
            Scene.Step(Hz,Hz);
            const double Paid=Before-ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
            AddInfo(FString::Printf(TEXT("Event=model_contact_wall_effort Blueprint=%s Pitch=%.1f Paid=%.3f TravelCm=%.3f"),
                Path,Pitch,Paid,FVector::Dist2D(BeforePosition,A->GetActorLocation())));
            TestTrue(TEXT("blocked model contact cannot credit depenetration as completed player intention"),Paid>.9);
        }
        const FVector StopA=A->GetActorLocation(), StopB=B->GetActorLocation();
        A->GetPhysicalBodyComponent()->SetMoveIntent(FVector::ZeroVector);
        Scene.Step(Hz/2,Hz);
        TestTrue(TEXT("releasing the pushing input stops both ungripped bodies promptly"),
            A->GetPhysicalBodyComponent()->GetVelocity().Size2D()<3 && B->GetPhysicalBodyComponent()->GetVelocity().Size2D()<3
            && FVector::Dist2D(StopA,A->GetActorLocation())<15 && FVector::Dist2D(StopB,B->GetActorLocation())<15);
        TestTrue(TEXT("ordinary body pushing needs no extended hand or retained grip"),!A->GetPhysicalBodyComponent()->GetGrab()->IsReaching(true)
            && !A->GetPhysicalBodyComponent()->GetGrab()->IsReaching(false) && !B->GetPhysicalBodyComponent()->GetGrab()->IsGripping(true) && !B->GetPhysicalBodyComponent()->GetGrab()->IsGripping(false));
    }
    return !HasAnyErrors();
}

#endif
