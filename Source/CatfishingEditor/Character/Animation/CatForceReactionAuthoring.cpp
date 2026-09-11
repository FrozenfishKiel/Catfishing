#include "../CatCharacterVariantAuthoringLibrary.h"
#include "Character/CatCharacter.h"
#include "Character/Animation/CatForceReactionComponent.h"
#include "Character/Physics/CatPhysicsPrototypeVisualComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

namespace CatForceAuthoring
{
    const TCHAR* Names[] = {TEXT("Forward"),TEXT("Backward"),TEXT("Left"),TEXT("Right")};
    const FString OriginalRoot = TEXT("/Game/Animalia/Cat/Animations/ForceReaction/");
    const FString CuteRoot = TEXT("/Game/Characters/CuteCat/Animation/Retargeted/");
    bool Save(UObject* Asset)
    {
        Asset->MarkPackageDirty();
        FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone;
        return UPackage::SavePackage(Asset->GetPackage(),Asset,
            *FPackageName::LongPackageNameToFilename(Asset->GetPackage()->GetName(),FPackageName::GetAssetPackageExtension()),Args);
    }
    UAnimMontage* Montage(const FString& Root, const FString& Name)
    {
        auto* Clip=LoadObject<UAnimSequence>(nullptr,*(Root+TEXT("AS_Force")+Name));
        if (!Clip || Clip->bEnableRootMotion || Clip->IsValidAdditive()) return nullptr;
        const FString Path=Root+TEXT("AM_Force")+Name;
        if (auto* Existing=LoadObject<UAnimMontage>(nullptr,*Path)) return Existing;
        auto* Result=NewObject<UAnimMontage>(CreatePackage(*Path),*FPackageName::GetLongPackageAssetName(Path),RF_Public|RF_Standalone);
        Result->SetSkeleton(Clip->GetSkeleton());
        Result->SlotAnimTracks.Reset();
        FSlotAnimationTrack& Track=Result->SlotAnimTracks.AddDefaulted_GetRef(); Track.SlotName=TEXT("DefaultSlot");
        FAnimSegment& Segment=Track.AnimTrack.AnimSegments.AddDefaulted_GetRef();
        Segment.SetAnimReference(Clip); Segment.AnimStartTime=0; Segment.AnimEndTime=Clip->GetPlayLength(); Segment.AnimPlayRate=1; Segment.LoopingCount=1;
        Result->SetCompositeLength(Clip->GetPlayLength());
        Result->AddAnimCompositeSection(TEXT("Default"),0);
        Result->BlendIn.SetBlendTime(.08f); Result->BlendOut.SetBlendTime(.15f);
        Result->bEnableAutoBlendOut=true;
        FAssetRegistryModule::AssetCreated(Result);
        return Save(Result) ? Result : nullptr;
    }
}

FString UCatCharacterVariantAuthoringLibrary::CreateForceReactionSourceClips(bool bRebuildGeneratedClips)
{
    using namespace CatForceAuthoring;
    auto* Source=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Animalia/Cat/Animations/InPlace/Hit_ChestL_Heavy-IP"));
    if (!Source) return TEXT("ERROR: Missing audited chest hit");
    const auto* Model=Source->GetDataModel();
    const auto& Ref=Source->GetSkeleton()->GetReferenceSkeleton();
    const int32 Count=Ref.GetNum(), Keys=Model->GetNumberOfKeys();
    const int32 Chest=Ref.FindBoneIndex(TEXT("RigChest")), Head=Ref.FindBoneIndex(TEXT("RigHead")), Pelvis=Ref.FindBoneIndex(TEXT("RigPelvis"));
    if (Chest==INDEX_NONE || Head==INDEX_NONE || Pelvis==INDEX_NONE) return TEXT("ERROR: Source skeleton changed");
    TArray<TArray<FTransform>> Local, Component;
    Local.SetNum(Keys); Component.SetNum(Keys);
    FVector Peak=FVector::ZeroVector;
    for (int32 Frame=0;Frame<Keys;++Frame)
    {
        Local[Frame].SetNum(Count); Component[Frame].SetNum(Count);
        for (int32 Bone=0;Bone<Count;++Bone)
        {
            Local[Frame][Bone]=Model->IsValidBoneTrackName(Ref.GetBoneName(Bone))
                ? Model->GetBoneTrackTransform(Ref.GetBoneName(Bone),FFrameNumber(Frame)) : Ref.GetRefBonePose()[Bone];
            const int32 Parent=Ref.GetParentIndex(Bone);
            Component[Frame][Bone]=Parent==INDEX_NONE ? Local[Frame][Bone] : Local[Frame][Bone]*Component[Frame][Parent];
        }
        FVector Delta=(Component[Frame][Chest].GetLocation()+Component[Frame][Head].GetLocation()
            -Component[0][Chest].GetLocation()-Component[0][Head].GetLocation())*.5; Delta.Z=0;
        if (Delta.SizeSquared()>Peak.SizeSquared()) Peak=Delta;
    }
    if (Peak.Size()<1) return TEXT("ERROR: Source has no measurable lateral reaction");
    // Animalia faces +Y (right=-X). Use the original impact/rebound envelope; yawing a yaw
    // recoil cannot turn it into a forward pitch. Move/tilt the pelvis with fixed bone lengths.
    const FVector Directions[]={FVector(0,1,0),FVector(0,-1,0),FVector(1,0,0),FVector(-1,0,0)};
    for (int32 Direction=0;Direction<4;++Direction)
    {
        const FString Name=FString(TEXT("AS_Force"))+Names[Direction], Path=OriginalRoot+Name;
        const bool Exists=FPackageName::DoesPackageExist(Path);
        if (Exists && !bRebuildGeneratedClips) continue;
        auto* Clip=Exists ? LoadObject<UAnimSequence>(nullptr,*Path) : Cast<UAnimSequence>(
            FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().DuplicateAsset(Name,OriginalRoot.LeftChop(1),Source));
        if (!Clip) return TEXT("ERROR: Cannot duplicate hit");
        TArray<TArray<FVector>> Positions,Scales; TArray<TArray<FQuat>> Rotations;
        Positions.SetNum(Count); Scales.SetNum(Count); Rotations.SetNum(Count);
        for (int32 Frame=0;Frame<Keys;++Frame)
        {
            const FVector Sample=(Component[Frame][Chest].GetLocation()+Component[Frame][Head].GetLocation()
                -Component[0][Chest].GetLocation()-Component[0][Head].GetLocation())*.5;
            const double Envelope=Frame==Keys-1 ? 0 : FMath::Clamp(FVector::DotProduct(Sample,Peak.GetSafeNormal()),-Peak.Size()*.25,Peak.Size());
            const double Dip=Frame==Keys-1 ? 0 : (Component[Frame][Pelvis].GetLocation().Z-Component[0][Pelvis].GetLocation().Z)*.5;
            const FQuat Tilt(FVector::CrossProduct(FVector::UpVector,Directions[Direction]),FMath::Clamp(Envelope*.02,-.06,.18));
            for (int32 Bone=0;Bone<Count;++Bone)
            {
                const int32 Parent=Ref.GetParentIndex(Bone);
                FQuat Rotation=Local[0][Bone].GetRotation();
                FVector Position=Local[0][Bone].GetLocation();
                if (Bone==Pelvis && Parent!=INDEX_NONE)
                {
                    Rotation=(Component[0][Parent].GetRotation().Inverse()*Tilt*Component[0][Bone].GetRotation()).GetNormalized();
                    Position+=Component[0][Parent].InverseTransformVectorNoScale(Directions[Direction]*Envelope*.6+FVector(0,0,Dip));
                }
                Rotations[Bone].Add(Rotation);
                Positions[Bone].Add(Position); Scales[Bone].Add(Local[0][Bone].GetScale3D());
            }
        }
        auto& Controller=Clip->GetController(); Controller.OpenBracket(FText::FromString(TEXT("Directional force reaction")),false);
        for (int32 Bone=0;Bone<Count;++Bone)
            if (Clip->GetDataModel()->IsValidBoneTrackName(Ref.GetBoneName(Bone)))
                Controller.SetBoneTrackKeys(Ref.GetBoneName(Bone),Positions[Bone],Rotations[Bone],Scales[Bone],false);
        Controller.CloseBracket(false);
        Clip->bEnableRootMotion=false; Clip->bForceRootLock=true;
        if (!Save(Clip)) return TEXT("ERROR: Cannot save directional clip");
    }
    return TEXT("Event=force_reaction_source_authored Result=FourDirections Source=Hit_ChestL_Heavy-IP");
}

FString UCatCharacterVariantAuthoringLibrary::FinalizeForceReactionAssets()
{
    using namespace CatForceAuthoring;
    TArray<TObjectPtr<UAnimMontage>> Original,Cute;
    for (const auto* Name:Names)
    {
        Original.Add(Montage(OriginalRoot,Name)); Cute.Add(Montage(CuteRoot,Name));
        if (!Original.Last() || !Cute.Last()) return TEXT("ERROR: Missing non-additive retargeted reaction");
    }
    // Configure concrete skin CDOs, preserving the abstract template and every other animation override.
    for (const auto* Skin:{TEXT("BP_CatCharacter"),TEXT("BP_CuteCatCharacter")})
    {
        auto* BP=LoadObject<UBlueprint>(nullptr,*(FString(TEXT("/Game/Character/"))+Skin));
        auto* Cat=BP && BP->GeneratedClass ? BP->GeneratedClass->GetDefaultObject<ACatCharacter>() : nullptr;
        auto* Reaction=Cat ? Cat->FindComponentByClass<UCatForceReactionComponent>() : nullptr;
        auto* Visual=Cat ? Cat->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>() : nullptr;
        if (!Reaction || !Visual) return TEXT("ERROR: Character reaction consumer missing");
        bool Changed=!Reaction->bEnabled || Reaction->DirectionalMontages!=Original;
        if (Changed) { Reaction->Modify(); Reaction->bEnabled=true; Reaction->DirectionalMontages=Original; }
        if (FString(Skin)==TEXT("BP_CuteCatCharacter"))
            for (int32 I=0;I<4;++I)
            {
                const FSoftObjectPath Key(Original[I]);
                if (Visual->AnimationOverrides.FindRef(Key)==Cute[I]) continue;
                Visual->Modify(); Visual->AnimationOverrides.Add(Key,Cute[I]); Changed=true;
            }
        if (!Changed) continue;
        FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
        FKismetEditorUtilities::CompileBlueprint(BP);
        if (BP->Status==BS_Error || !Save(BP)) return TEXT("ERROR: Character compile/save failed");
    }
    return TEXT("Event=force_reaction_assets_bound Result=OriginalAndCuteCat FourMontages=1");
}
