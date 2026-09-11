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
    TArray<int32> Indices;
    for (int32 I=0; I<Fitted->SkeletalBodySetups.Num(); ++I) Indices.Add(I);
    const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
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
        if (Hull.VertexData.Num()<4) return TEXT("Error: body has insufficient weighted vertices; original asset untouched");
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
        if (Body->BoneName!=Asset->SkeletalBodySetups[I]->BoneName || Body->AggGeom.ConvexElems.Num()!=1)
            return TEXT("Error: body identity or single hull contract changed; original asset untouched");
        const FVector Extent = Body->AggGeom.CalcAABB(FTransform::Identity).GetExtent();
        if (Extent.ContainsNaN() || Extent.GetMin()<=0 || Extent.GetMax()>=.5)
            return TEXT("Error: fitted bone-local extent outside CuteCat mesh units; original asset untouched");
        Report += FString::Printf(TEXT("Bone=%s Convex=1 LocalExtentCm=%s\n"),*Body->BoneName.ToString(),*Extent.ToString());
    }
    if (bSave)
    {
        for (int32 I=0; I<Indices.Num(); ++I)
        {
            auto* Body = Asset->SkeletalBodySetups[I].Get();
            Body->Modify();
            Body->RemoveSimpleCollision();
            Body->AggGeom = Fitted->SkeletalBodySetups[I]->AggGeom;
            Body->InvalidatePhysicsData();
            Body->CreatePhysicsMeshes();
        }
        Asset->MarkPackageDirty();
        FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError;
        if (!UPackage::SavePackage(Asset->GetPackage(),Asset,
            *FPackageName::LongPackageNameToFilename(Asset->GetPackage()->GetName(),FPackageName::GetAssetPackageExtension()),Args))
            return TEXT("Error: save failed");
    }
    return FString::Printf(TEXT("Result=%s Asset=%s\n%s"),bSave?TEXT("Saved"):TEXT("Preview"),*Asset->GetPathName(),*Report);
}
