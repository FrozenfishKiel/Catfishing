#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Character/CatCharacter.h"
#include "Character/Animation/CatForceReactionComponent.h"
#include "Character/Physics/CatPhysicsPrototypeVisualComponent.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Character/Physics/Tests/CatPhysicalTestWorld.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PointLightComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatForceReactionAssetTest,"Catfishing.ForceReaction.Contract.SkinMontagesAndActualPoseDirections",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCatForceReactionAssetTest::RunTest(const FString& Parameters)
{
    for (const bool Cute:{false,true})
    {
        auto* Class=LoadClass<ACatCharacter>(nullptr,Cute ? TEXT("/Game/Character/BP_CuteCatCharacter.BP_CuteCatCharacter_C") : TEXT("/Game/Character/BP_CatCharacter.BP_CatCharacter_C"));
        if (!TestNotNull(TEXT("skin class"),Class)) return false;
        const auto* Cat=Class->GetDefaultObject<ACatCharacter>();
        const auto* Reaction=Cat->FindComponentByClass<UCatForceReactionComponent>();
        const auto* Visual=Cat->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>();
        if (!TestNotNull(TEXT("inherited reaction consumer"),Reaction) || !TestNotNull(TEXT("visual mapper"),Visual)) return false;
        TestTrue(TEXT("saved skin enables reaction"),Reaction->bEnabled);
        if (!TestEqual(TEXT("four configured directions"),Reaction->DirectionalMontages.Num(),4)) return false;
        TSet<UAnimMontage*> Unique;
        const FVector Forward=Visual->RigSettings.ForwardAxis.GetSafeNormal2D();
        const FVector Right=FVector::CrossProduct(FVector::UpVector,Forward);
        const FVector Directions[]={Forward,-Forward,-Right,Right};
        for (int32 Direction=0;Direction<4;++Direction)
        {
            auto* Montage=Cast<UAnimMontage>(Visual->ResolveAnimationAsset(Reaction->DirectionalMontages[Direction]));
            if (!TestNotNull(TEXT("resolved skin montage"),Montage)) return false;
            Unique.Add(Montage);
            if (!TestEqual(TEXT("single montage slot"),Montage->SlotAnimTracks.Num(),1)) return false;
            const auto& Track=Montage->SlotAnimTracks[0];
            TestEqual(TEXT("shared template slot"),Track.SlotName,FName(TEXT("DefaultSlot")));
            if (!TestEqual(TEXT("single nonlooping segment"),Track.AnimTrack.AnimSegments.Num(),1)) return false;
            const auto& Segment=Track.AnimTrack.AnimSegments[0];
            TestEqual(TEXT("one play"),Segment.LoopingCount,1);
            auto* Clip=Cast<UAnimSequence>(Segment.GetAnimReference());
            if (!TestNotNull(TEXT("real sequence"),Clip)) return false;
            TestFalse(TEXT("presentation cannot move authority root"),Clip->bEnableRootMotion);
            TestFalse(TEXT("montage consumes a full pose"),Clip->IsValidAdditive());
            TestTrue(TEXT("correct skeleton"),Clip->GetSkeleton()->GetPathName().Contains(Cute ? TEXT("CuteCat") : TEXT("Animalia")));
            const auto& Ref=Clip->GetSkeleton()->GetReferenceSkeleton(); const auto* Model=Clip->GetDataModel();
            const int32 Head=Ref.FindBoneIndex(Cute ? TEXT("Head_001") : TEXT("RigHead"));
            const int32 Chest=Ref.FindBoneIndex(Cute ? TEXT("Root_002") : TEXT("RigChest"));
            FVector Base,Peak=FVector::ZeroVector;
            for (int32 Frame=0;Frame<Model->GetNumberOfKeys();++Frame)
            {
                TArray<FTransform> CS; CS.SetNum(Ref.GetNum());
                for (int32 Bone=0;Bone<Ref.GetNum();++Bone)
                {
                    const FTransform Local=Model->IsValidBoneTrackName(Ref.GetBoneName(Bone)) ? Model->GetBoneTrackTransform(Ref.GetBoneName(Bone),FFrameNumber(Frame)) : Ref.GetRefBonePose()[Bone];
                    const int32 Parent=Ref.GetParentIndex(Bone); CS[Bone]=Parent==INDEX_NONE ? Local : Local*CS[Parent];
                    TestTrue(TEXT("finite uncollapsed bone transform"),!CS[Bone].ContainsNaN() && Local.GetScale3D().GetMin()>0);
                    if (Cute && Bone==Head) TestTrue(TEXT("retarget preserves native head bone length"),Local.GetTranslation().Equals(Ref.GetRefBonePose()[Bone].GetTranslation(),.001));
                }
                const FVector Center=(CS[Head].GetLocation()+CS[Chest].GetLocation())*.5;
                if (Frame==0) Base=Center;
                FVector Delta=Center-Base; Delta.Z=0;
                if (Delta.SizeSquared()>Peak.SizeSquared()) Peak=Delta;
            }
            const double Alignment=FVector::DotProduct(Peak.GetSafeNormal(),Directions[Direction]);
            AddInfo(FString::Printf(TEXT("Event=force_reaction_pose_audited Skin=%s Montage=%s Direction=%d PeakCm=%s Alignment=%.3f"),Cute ? TEXT("CuteCat") : TEXT("Animalia"),*Montage->GetName(),Direction,*Peak.ToCompactString(),Alignment));
            TestTrue(TEXT("actual torso/head movement follows configured force direction"),Peak.Size()>1 && Alignment>.7);
        }
        TestEqual(TEXT("opposing directions use distinct compatible montages"),Unique.Num(),4);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatForceReactionVisibleTest,"Catfishing.ForceReaction.Runtime.FormalCuteCatFourDirectionsAndBusyMontage",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCatForceReactionVisibleTest::RunTest(const FString& Parameters)
{
    CatPhysicalTest::FScene Scene; if (!Scene.Initialize(this)) return false;
    auto* World=Scene.World.GetTestWorld();
    auto* Class=LoadClass<ACatCharacter>(nullptr,TEXT("/Game/Character/BP_CuteCatCharacter.BP_CuteCatCharacter_C"));
    if (!Class) return false;
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Cat=World->SpawnActor<ACatCharacter>(Class,FVector(0,0,Class->GetDefaultObject<ACatCharacter>()->GetDefaultHalfHeight()),FRotator::ZeroRotator,Params);
    auto* Body=Cat->GetPhysicalBodyComponent(); auto* Reaction=Cat->FindComponentByClass<UCatForceReactionComponent>();
    auto* Visual=Cat->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>();
    const bool Capture=FParse::Param(FCommandLine::Get(),TEXT("CatForceReactionScreenshots"));
    auto Shot=[&](const TCHAR* Name)
    {
        if (!Capture) return;
        auto* Rig=World->SpawnActor<AActor>(); auto* Camera=NewObject<USceneCaptureComponent2D>(Rig);
        Rig->SetRootComponent(Camera); Camera->RegisterComponent(); Camera->bCaptureEveryFrame=false; Camera->bCaptureOnMovement=false;
        Camera->CaptureSource=ESceneCaptureSource::SCS_FinalColorLDR; Camera->FOVAngle=35;
        const FVector Center=Cat->GetActorLocation()+FVector(0,0,0), Position=Center+FVector(130,170,115);
        Camera->SetWorldLocationAndRotation(Position,(Center-Position).Rotation());
        auto* Target=NewObject<UTextureRenderTarget2D>(Rig); Target->RenderTargetFormat=ETextureRenderTargetFormat::RTF_RGBA8;
        Target->InitAutoFormat(960,720); Target->UpdateResourceImmediate(true); Camera->TextureTarget=Target;
        for (const FVector Offset:{FVector(60,100,170),FVector(-100,-60,120)})
        {
            auto* Light=NewObject<UPointLightComponent>(Rig); Light->SetIntensity(15000); Light->SetCastShadows(false);
            Light->SetAttenuationRadius(1000); Light->RegisterComponent(); Light->SetWorldLocation(Center+Offset);
        }
        FAssetCompilingManager::Get().FinishAllCompilation();
        if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
        World->SendAllEndOfFrameUpdates(); Camera->CaptureScene();
        const FString Directory=FPaths::ProjectSavedDir()/TEXT("ForceReaction/Screenshots");
        IFileManager::Get().MakeDirectory(*Directory,true);
        UKismetRenderingLibrary::ExportRenderTarget(World,Target,Directory,FString(Name)+TEXT(".png")); Rig->Destroy();
    };
    Scene.Step(90); Shot(TEXT("Idle"));
    const FVector Forces[]={FVector(1000,0,0),FVector(-1000,0,0),FVector(0,-1000,0),FVector(0,1000,0)};
    const TCHAR* Names[]={TEXT("Forward"),TEXT("Backward"),TEXT("Left"),TEXT("Right")};
    for (int32 D=0;D<4;++D)
    {
        Body->SetExternalForceFromAuthority(Scene.Floor,Forces[D],false,false,true); Scene.Step(15);
        TestEqual(TEXT("one onset per released direction"),Reaction->GetPlayedCount(),uint32(D+1));
        TestEqual(TEXT("actual selected direction"),int32(Reaction->GetObservedDirection()),D);
        auto* Montage=Cast<UAnimMontage>(Visual->ResolveAnimationAsset(Reaction->DirectionalMontages[D]));
        TestTrue(TEXT("formal CuteCat ABP consumes one-shot"),Cat->GetMesh()->GetAnimInstance()->Montage_IsPlaying(Montage));
        Shot(Names[D]); Body->ClearExternalForce(Scene.Floor); Scene.Step(150);
    }
    auto* Busy=LoadObject<UAnimMontage>(nullptr,TEXT("/Game/Animalia/Cat/AM_Attack_Left-IP_Montage"));
    TestTrue(TEXT("existing gameplay montage starts"),Cat->PlayAnimMontage(Busy)>0);
    Body->SetExternalForceFromAuthority(Scene.Floor,Forces[0],false,false,true); Scene.Step(1);
    TestEqual(TEXT("reaction cannot interrupt another action"),Reaction->GetPlayedCount(),uint32(4));
    Scene.Step(180);
    TestEqual(TEXT("skipped onset is not queued after busy montage finishes"),Reaction->GetPlayedCount(),uint32(4));
    Body->ClearExternalForce(Scene.Floor); Scene.Step(30);
    Body->SetLocomotionEnabledFromAuthority(false,TEXT("ReactionSuppressionTest"));
    Body->SetExternalForceFromAuthority(Scene.Floor,Forces[0],false,false,true); Scene.Step(1);
    Body->SetLocomotionEnabledFromAuthority(true,TEXT("ReactionSuppressionTest")); Scene.Step(60);
    TestEqual(TEXT("restoring movement during same load does not replay suppressed onset"),Reaction->GetPlayedCount(),uint32(4));
    return !HasAnyErrors();
}
#endif
