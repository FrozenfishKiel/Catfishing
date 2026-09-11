#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Character/Physics/Tests/CatPhysicalTestWorld.h"
#include "Character/Physics/CatPhysicsPrototypeVisualComponent.h"
#include "Interaction/CatModelContactComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "PhysicsEngine/BodySetup.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatModelContactFitTest,
    "Catfishing.ModelContacts.Editor.CuteCatRearClearanceAndArticulation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatModelContactFitTest::RunTest(const FString&)
{
    CatPhysicalTest::FScene Scene;
    if (!Scene.Initialize(this)) return false;
    auto* Class=LoadClass<ACatCharacter>(nullptr,TEXT("/Game/Character/BP_CuteCatCharacter.BP_CuteCatCharacter_C"));
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Cat=Scene.World.GetTestWorld()->SpawnActor<ACatCharacter>(Class,FVector(0,0,80),FRotator::ZeroRotator,Params);
    if (!TestNotNull(TEXT("formal CuteCat"),Cat)) return false;
    Scene.Step(90);
    auto* Model=Cat->FindComponentByClass<UCatModelContactComponent>();
    auto* Visual=Cat->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>()->GetVisualMesh();
    auto* Mesh=CastChecked<USkeletalMesh>(Visual->GetSkinnedAsset());
    auto& LOD=Mesh->GetResourceForRendering()->LODRenderData[0];
    const auto* Indices=LOD.MultiSizeIndexContainer.GetIndexBuffer();
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("Automation/ContactFit");
    IFileManager::Get().MakeDirectory(*Directory,true);
    const FString Stamp=FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
    for (int32 PoseIndex=0;PoseIndex<2;++PoseIndex)
    {
        if (PoseIndex==1)
        {
            Cat->GetPhysicalBodyComponent()->SetMoveIntent(FVector::ForwardVector);
            Scene.Step(37);
            Cat->GetPhysicalBodyComponent()->SetMoveIntent(FVector::ZeroVector);
        }
        TArray<FMatrix44f> RefToLocal;
        Visual->GetCurrentRefToLocalMatrices(RefToLocal,0);
        TArray<FVector3f> LocalVertices;
        USkinnedMeshComponent::ComputeSkinnedPositions(Visual,LocalVertices,RefToLocal,LOD,LOD.SkinWeightVertexBuffer);
        TArray<FVector> Vertices;
        FString Obj=TEXT("# Final skinned LOD0 and actual cooked contact hulls, world cm\ng skin\n");
        for (const FVector3f& Local : LocalVertices)
        {
            const FVector World=Visual->GetComponentTransform().TransformPosition(FVector(Local));
            Vertices.Add(World);
            Obj+=FString::Printf(TEXT("v %.5f %.5f %.5f\n"),World.X,World.Y,World.Z);
        }
        for (int32 I=0;I<int32(Indices->Num());I+=3)
            Obj+=FString::Printf(TEXT("f %u %u %u\n"),Indices->Get(I)+1,Indices->Get(I+1)+1,Indices->Get(I+2)+1);
        int32 Offset=Vertices.Num()+1;
        for (UCatModelContactBody* Contact : Model->GetBodies())
        {
            Obj+=FString::Printf(TEXT("g %s\n"),*Contact->GetBoneName().ToString());
            for (const auto& Hull : Contact->GetBodySetup()->AggGeom.ConvexElems)
            {
                for (const FVector& Point : Hull.VertexData)
                {
                    const FVector World=Contact->GetComponentTransform().TransformPosition(Hull.GetTransform().TransformPosition(Point));
                    Obj+=FString::Printf(TEXT("v %.5f %.5f %.5f\n"),World.X,World.Y,World.Z);
                }
                const TArray<int32> Faces=Hull.GetChaosConvexIndices();
                for (int32 I=0;I+2<Faces.Num();I+=3)
                    Obj+=FString::Printf(TEXT("f %d %d %d\n"),Offset+Faces[I],Offset+Faces[I+1],Offset+Faces[I+2]);
                Offset+=Hull.VertexData.Num();
            }
        }
        const FString Artifact=Directory/ FString::Printf(TEXT("%s-Pose%d-Bodies%d.obj"),*Stamp,PoseIndex,Model->GetBodies().Num());
        TestTrue(TEXT("write exact skin/collision geometry for visual review"),FFileHelper::SaveStringToFile(Obj,*Artifact));
        double MaximumAir=0; int32 Tested=0;
        FString Samples=TEXT("Y,Z,AirCm,SkinHit,Bone,CollisionX,SkinX\n");
        // Rays from behind, stopped at the actor centre so the head cannot hide a rear air wall.
        // Compare with full weighted/skinned render triangles, independently of the fitting rule.
        for (double Y=-30;Y<=30;Y+=5) for (double Z=10;Z<=65;Z+=5)
        {
            const FVector Start(Cat->GetActorLocation().X-180,Cat->GetActorLocation().Y+Y,Z);
            const FVector End(Cat->GetActorLocation().X,Start.Y,Z);
            double CollisionX=End.X; bool bCollision=false;
            FName HitBone;
            for (UCatModelContactBody* Contact : Model->GetBodies())
            {
                FHitResult Hit;
                if (Contact->LineTraceComponent(Hit,Start,End,FCollisionQueryParams(SCENE_QUERY_STAT(CatRearFit),false)) && Hit.ImpactPoint.X<CollisionX)
                { CollisionX=Hit.ImpactPoint.X; bCollision=true; HitBone=Contact->GetBoneName(); }
            }
            if (!bCollision) continue;
            double SkinX=End.X; bool bSkin=false;
            double NearestSquared=UE_DOUBLE_BIG_NUMBER;
            const FVector ContactPoint(CollisionX,Start.Y,Z);
            for (int32 I=0;I<int32(Indices->Num());I+=3)
            {
                const FVector& A=Vertices[Indices->Get(I)]; const FVector& B=Vertices[Indices->Get(I+1)]; const FVector& C=Vertices[Indices->Get(I+2)];
                if (FVector::CrossProduct(B-A,C-A).SizeSquared()<UE_SMALL_NUMBER) continue;
                FVector Hit,Normal;
                if (FMath::SegmentTriangleIntersection(Start,End,A,B,C,Hit,Normal) && Hit.X<SkinX) { SkinX=Hit.X; bSkin=true; }
                NearestSquared=FMath::Min(NearestSquared,FVector::DistSquared(ContactPoint,FMath::ClosestPointOnTriangleToPoint(ContactPoint,A,B,C)));
            }
            // At grazing angles along-ray distances grow arbitrarily; surface distance measures
            // the actual empty-space thickness, while signed ray order excludes hulls inside skin.
            const double Air=!bSkin || CollisionX<SkinX ? FMath::Sqrt(NearestSquared) : 0;
            MaximumAir=FMath::Max(MaximumAir,Air); ++Tested;
            Samples+=FString::Printf(TEXT("%.1f,%.1f,%.4f,%d,%s,%.4f,%.4f\n"),Y,Z,Air,bSkin,*HitBone.ToString(),CollisionX,SkinX);
        }
        FFileHelper::SaveStringToFile(Samples,*(Artifact+TEXT(".csv")));
        AddInfo(FString::Printf(TEXT("Event=cute_contact_surface_fit Pose=%d Bodies=%d Rays=%d MaxRearSurfaceAirCm=%.3f Artifact=%s"),PoseIndex,Model->GetBodies().Num(),Tested,MaximumAir,*Artifact));
        TestTrue(TEXT("rear collision does not enclose more than 3 cm of empty space around the skinned model"),Tested>20 && MaximumAir<3);
    }
    // Articulated surfaces must move when only their bone moves, leaving the torso unchanged.
    for (const FName Bone : {FName(TEXT("Tail_005")),FName(TEXT("Hand_L_001")),FName(TEXT("Foot_R_001"))})
    {
        UCatModelContactBody* Part=nullptr;
        for (UCatModelContactBody* Contact : Model->GetBodies()) if (Contact->GetBoneName()==Bone) Part=Contact;
        if (!TestNotNull(FString::Printf(TEXT("independent contact on %s"),*Bone.ToString()),Part)) continue;
        const FVector Before=Part->Bounds.Origin;
        const FTransform Torso=Model->GetBodies()[0]->GetComponentTransform();
        Visual->SetBoneLocationByName(Bone,Visual->GetBoneLocationByName(Bone,EBoneSpaces::WorldSpace)+FVector(0,5,0),EBoneSpaces::WorldSpace);
        Visual->RefreshBoneTransforms(); Model->RefreshPose();
        TestTrue(TEXT("contact follows the articulated bone independently"),Part->Bounds.Origin.Equals(Before+FVector(0,5,0),.01));
        TestTrue(TEXT("moving a tail or paw does not move the torso collider"),Torso.Equals(Model->GetBodies()[0]->GetComponentTransform(),.001));
    }
    return !HasAnyErrors();
}
#endif
