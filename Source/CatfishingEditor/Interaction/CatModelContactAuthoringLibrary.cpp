#include "CatModelContactAuthoringLibrary.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "MeshUtilitiesEngine.h"
#include "MeshUtilitiesCommon.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

FString UCatModelContactAuthoringLibrary::RefitCuteCatContacts(bool bSave)
{
    auto* Mesh = LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Characters/CuteCat/Meshes/SK_CuteCat.SK_CuteCat"));
    auto* Asset = Mesh ? Mesh->GetPhysicsAsset() : nullptr;
    if (!Asset || Asset->GetPathName()!=TEXT("/Game/Characters/CuteCat/Meshes/SK_CuteCat_PhysicsAsset.SK_CuteCat_PhysicsAsset"))
        return TEXT("Error: expected CuteCat mesh and physics asset are missing");
    if (Asset->GetPackage()->IsDirty()) return TEXT("Error: refusing to overwrite unsaved physics asset edits");
    auto* Fitted = DuplicateObject<UPhysicsAsset>(Asset,GetTransientPackage());
    // Imported bones use a 100x unit conversion. Primitive fitting clamps local radius to .5 cm;
    // convex fitting uses the actual weighted vertices and does not impose that oversized minimum.
    TArray<FBoneVertInfo> Infos;
    FMeshUtilitiesEngine::CalcBoneVertInfos(Mesh,Infos,true,0);
    const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
    if (Infos.Num()!=Skeleton.GetRawBoneNum()) return TEXT("Error: LOD0 CPU skin data unavailable; original asset untouched");
    // Keep existing indices/names for consumers and constraints. Separate articulated appendages
    // BEFORE assigning vertices so neither the tail nor the space between legs enters a torso hull.
    TArray<FName> ArticulatedBones;
    for (const TCHAR* Bone : {TEXT("Spine_001"),TEXT("Spine_002"),TEXT("Spine_003"),TEXT("Neck_002"),TEXT("Neck_003"),TEXT("Neck_004")})
        ArticulatedBones.Add(FName(Bone));
    for (int32 Segment=1; Segment<=9; ++Segment)
        ArticulatedBones.Add(FName(*FString::Printf(TEXT("Tail_%03d"),Segment)));
    for (const TCHAR* Side : {TEXT("L"),TEXT("R")})
        for (const TCHAR* Pattern : {TEXT("Leg_SIDE_001"),TEXT("Leg_SIDE_002"),TEXT("Leg_SIDE_005"),TEXT("Leg_SIDE_006"),TEXT("Foot_SIDE_001"),
            TEXT("Arm_SIDE_001"),TEXT("Arm_SIDE_002"),TEXT("Arm_SIDE_005"),TEXT("Arm_SIDE_006"),TEXT("Hand_SIDE_001")})
            ArticulatedBones.Add(FName(*FString(Pattern).Replace(TEXT("SIDE"),Side)));
    for (FName Bone : ArticulatedBones)
    {
        const int32 BoneIndex=Skeleton.FindBoneIndex(Bone);
        if (BoneIndex==INDEX_NONE)
            return FString::Printf(TEXT("Error: missing articulated bone %s; original asset untouched"),*Bone.ToString());
        // Some imported terminal/control bones (e.g. Tail_009) have no skinned surface.
        // Their descendants, if any, remain assigned to the nearest fitted ancestor.
        int32 VertexCount=0;
        for (int32 Candidate=0;Candidate<Infos.Num();++Candidate)
            if (Candidate==BoneIndex || Skeleton.BoneIsChildOf(Candidate,BoneIndex)) VertexCount+=Infos[Candidate].Positions.Num();
        if (VertexCount<4) continue;
        if (Fitted->FindBodyIndex(Bone)!=INDEX_NONE) continue;
        auto* Body=NewObject<USkeletalBodySetup>(Fitted,NAME_None,RF_Transactional);
        Body->BoneName=Bone;
        Body->PhysicsType=PhysType_Kinematic;
        Fitted->SkeletalBodySetups.Add(Body);
        Fitted->UpdateBodySetupIndexMap();
    }
    TArray<int32> Indices;
    for (int32 I=0; I<Fitted->SkeletalBodySetups.Num(); ++I) Indices.Add(I);
    const auto& RefInverse = Mesh->GetRefBasesInvMatrix();
    // Direct convex cooking avoids primitive minimum sizes and decomposition's small-input cutoff.
    // Each vertex belongs to its dominant bone's nearest existing physical ancestor.
    for (int32 I : Indices)
    {
        auto* Body = Fitted->SkeletalBodySetups[I].Get();
        const int32 BodyBone = Skeleton.FindBoneIndex(Body->BoneName);
        FKConvexElem Hull;
        for (int32 Bone=0; Bone<Infos.Num(); ++Bone)
        {
            int32 OwnerBone=Bone;
            while (OwnerBone!=INDEX_NONE && Fitted->FindBodyIndex(Skeleton.GetBoneName(OwnerBone))==INDEX_NONE)
                OwnerBone=Skeleton.GetParentIndex(OwnerBone);
            if (OwnerBone!=BodyBone) continue;
            const FMatrix44f BoneToBody=RefInverse[Bone].Inverse()*RefInverse[BodyBone];
            for (const FVector3f& Point : Infos[Bone].Positions)
                Hull.VertexData.Add(FVector(BoneToBody.TransformPosition(Point)));
        }
        if (Hull.VertexData.IsEmpty() && Asset->SkeletalBodySetups.IsValidIndex(I))
        {
            // An old grouping bone can become surface-less after its actual skin bones are split.
            // Retain its identity/index but remove the obsolete enclosing shape. Runtime already
            // skips empty body setups; no phantom torso or duplicate skin is introduced.
            Body->RemoveSimpleCollision(); Body->InvalidatePhysicsData();
            continue;
        }
        if (Hull.VertexData.Num()<4) return FString::Printf(TEXT("Error: bone %s has only %d weighted vertices; original asset untouched"),*Body->BoneName.ToString(),Hull.VertexData.Num());
        Hull.UpdateElemBox();
        Body->RemoveSimpleCollision();
        Body->AggGeom.ConvexElems.Add(MoveTemp(Hull));
        Body->InvalidatePhysicsData();
        Body->CreatePhysicsMeshes();
    }
    FString Report;
    for (int32 I=0; I<Indices.Num(); ++I)
    {
        const auto* Body = Fitted->SkeletalBodySetups[I].Get();
        if (Body->AggGeom.GetElementCount()==0)
        {
            Report+=FString::Printf(TEXT("Bone=%s Convex=0 Result=IdentityOnly\n"),*Body->BoneName.ToString());
            continue;
        }
        if ((Asset->SkeletalBodySetups.IsValidIndex(I) && Body->BoneName!=Asset->SkeletalBodySetups[I]->BoneName) || Body->AggGeom.ConvexElems.Num()!=1
            || !Body->AggGeom.ConvexElems[0].GetChaosConvexMesh())
            return TEXT("Error: body identity or single hull contract changed; original asset untouched");
        const FVector Extent = Body->AggGeom.CalcAABB(FTransform::Identity).GetExtent();
        if (Extent.ContainsNaN() || Extent.GetMin()<=0 || Extent.GetMax()>=.5)
            return TEXT("Error: fitted bone-local extent outside CuteCat mesh units; original asset untouched");
        Report += FString::Printf(TEXT("Bone=%s Convex=1 Vertices=%d LocalExtentCm=%s\n"),*Body->BoneName.ToString(),Body->AggGeom.ConvexElems[0].VertexData.Num(),*Extent.ToString());
    }
    if (bSave)
    {
        for (int32 I=0; I<Indices.Num(); ++I)
        {
            if (!Asset->SkeletalBodySetups.IsValidIndex(I))
            {
                Asset->SkeletalBodySetups.Add(DuplicateObject<USkeletalBodySetup>(Fitted->SkeletalBodySetups[I],Asset));
                continue;
            }
            auto* Body = Asset->SkeletalBodySetups[I].Get();
            Body->Modify();
            Body->RemoveSimpleCollision();
            Body->AggGeom = Fitted->SkeletalBodySetups[I]->AggGeom;
            Body->InvalidatePhysicsData();
            Body->CreatePhysicsMeshes();
        }
        Asset->UpdateBodySetupIndexMap();
        Asset->UpdateBoundsBodiesArray();
        Asset->MarkPackageDirty();
        FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError;
        if (!UPackage::SavePackage(Asset->GetPackage(),Asset,
            *FPackageName::LongPackageNameToFilename(Asset->GetPackage()->GetName(),FPackageName::GetAssetPackageExtension()),Args))
            return TEXT("Error: save failed");
    }
    return FString::Printf(TEXT("Result=%s Asset=%s\n%s"),bSave?TEXT("Saved"):TEXT("Preview"),*Asset->GetPathName(),*Report);
}
