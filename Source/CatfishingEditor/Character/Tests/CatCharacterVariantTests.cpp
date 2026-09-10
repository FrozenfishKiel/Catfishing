#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Character/Physics/CatPhysicsPrototypeVisualComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "UObject/UnrealType.h"
#include "Condition/CatConditionPresentationComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PointLightComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatCharacterVariantContract,
	"Catfishing.CharacterVariants.Contract.TemplateAndSkeletonBindings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatCharacterVariantContract::RunTest(const FString& Parameters)
{
	UAnimBlueprint* Template = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Character/Animation/ABPT_CatCharacterBase"));
	UBlueprint* Base = LoadObject<UBlueprint>(nullptr, TEXT("/Game/Character/BP_CatCharacterBase"));
	if (!TestNotNull(TEXT("shared template"), Template) || !TestNotNull(TEXT("shared character base"), Base)) return false;
	TestTrue(TEXT("template is explicitly skeleton independent"), Template->bIsTemplate && !Template->TargetSkeleton);
	for (const TCHAR* Path : {TEXT("/Game/Character/BP_CatCharacter.BP_CatCharacter_C"), TEXT("/Game/Character/BP_CuteCatCharacter.BP_CuteCatCharacter_C")})
	{
		UClass* Class = LoadClass<ACatCharacter>(nullptr, Path);
		if (!TestNotNull(TEXT("selectable character class"), Class)) return false;
		TestEqual(TEXT("both skins directly inherit common character"), Class->GetSuperClass(), Base->GeneratedClass.Get());
		ACatCharacter* CDO = Class->GetDefaultObject<ACatCharacter>();
		USkeletalMesh* Mesh = CDO->GetMesh()->GetSkeletalMeshAsset();
		UAnimBlueprint* Anim = Cast<UAnimBlueprint>(CDO->GetMesh()->GetAnimClass()->ClassGeneratedBy);
		if (!TestNotNull(TEXT("skeletal mesh"), Mesh) || !TestNotNull(TEXT("animation child"), Anim)) return false;
		TestEqual(TEXT("animation child directly inherits template"), Anim->ParentClass.Get(), Template->GeneratedClass.Get());
		TestEqual(TEXT("animation child uses its own skeleton"), Anim->TargetSkeleton.Get(), Mesh->GetSkeleton());
		TestEqual(TEXT("all six actual players overridden"), Anim->ParentAssetOverrides.Num(), 6);
		for (const auto& Override : Anim->ParentAssetOverrides)
			TestTrue(TEXT("player override belongs to skin skeleton"), Override.NewAsset && Override.NewAsset->GetSkeleton() == Mesh->GetSkeleton());
		auto* Visual = CDO->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>();
		FCatQuadrupedLocomotion Calibration;
		Calibration.ConfigureRig(Visual->RigSettings);
		for (const auto& Clip : Visual->RigSettings.LocomotionAnimations)
			TestTrue(TEXT("native gait calibrates in mesh cm/s"), Calibration.GetReferenceSpeedMeshCmS(Clip.LoadSynchronous(), Mesh) > 1.0);
		TestTrue(TEXT("mouth carry attachment exists"), Mesh->FindSocket(TEXT("Mouth")) != nullptr);
		if (Visual->RigSettings.RigId == TEXT("CuteCat"))
		{
			TestEqual(TEXT("five directly parented joints on each CuteCat limb"), Visual->RigSettings.Feet[0].Bones.Num(), 5);
			for (const TCHAR* Montage : {TEXT("/Game/Animalia/Cat/AM_Attack_Agressive_Legs_01-IP_Montage"),
				TEXT("/Game/Animalia/Cat/AM_Hit_ChestL_Heavy-IP_Montage"), TEXT("/Game/Animalia/Cat/AM_Death_01-IP_Montage"),
				TEXT("/Game/Animalia/Cat/AM_Action_Scratching-IP_Montage"),
				TEXT("/Game/Animalia/Cat/AM_Attack_Left-IP_Montage"),
				TEXT("/Game/Catfishing/Animation/BodyAction/AM_BodyAction_CampRest"),
				TEXT("/Game/Catfishing/Animation/BodyAction/AM_BodyAction_CampfirePlayback"),
				TEXT("/Game/Catfishing/Animation/BodyAction/AM_BodyAction_RequestManualHelp"),
				TEXT("/Game/Catfishing/Animation/BodyAction/AM_BodyAction_RequestMischief"),
				TEXT("/Game/Catfishing/Animation/BodyAction/AM_BodyAction_RescueCharacterToCamp"),
				TEXT("/Game/Catfishing/Animation/BodyAction/AM_BodyAction_PlaceProtectionSign")})
			{
				UAnimationAsset* Original = LoadObject<UAnimationAsset>(nullptr, Montage);
				UAnimationAsset* Resolved = Visual->ResolveAnimationAsset(Original);
				TestTrue(TEXT("configured gameplay montage has a distinct target-skeleton replacement"), Original && Resolved && Resolved != Original && Resolved->GetSkeleton() == Mesh->GetSkeleton());
			}
			auto* Condition = CDO->FindComponentByClass<UCatConditionPresentationComponent>();
			FArrayProperty* Property = FindFProperty<FArrayProperty>(Condition->GetClass(), TEXT("PoseClips"));
			FScriptArrayHelper Clips(Property, Property->ContainerPtrToValuePtr<void>(Condition));
			TestEqual(TEXT("all downed presentation phases retained"), Clips.Num(), 5);
			for (int32 Index=0; Index<Clips.Num(); ++Index)
			{
				auto* Clip = Cast<UAnimSequence>(CastFieldChecked<FObjectPropertyBase>(Property->Inner)->GetObjectPropertyValue(Clips.GetRawPtr(Index)));
				TestTrue(TEXT("downed clip uses CuteCat skeleton"), Clip && Clip->GetSkeleton() == Mesh->GetSkeleton());
			}
		}
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatCharacterVariantRuntime,
	"Catfishing.CharacterVariants.Runtime.BothSkinsWalkJumpAndPlayMappedActions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatCharacterVariantRuntime::RunTest(const FString& Parameters)
{
	for (const TCHAR* Path : {TEXT("/Game/Character/BP_CatCharacter.BP_CatCharacter_C"), TEXT("/Game/Character/BP_CuteCatCharacter.BP_CuteCatCharacter_C")})
	{
		FTestWorldWrapper World;
		if (!World.CreateTestWorld(EWorldType::Game)) return false;
		World.ForwardErrorMessages(this);
		World.GetTestWorld()->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
		FTransform FloorTransform(FRotator::ZeroRotator, FVector(0,0,-10), FVector(100,100,0.2));
		auto* Floor = World.GetTestWorld()->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FloorTransform);
		Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube")));
		Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Floor->FinishSpawning(FloorTransform);
		if (!World.BeginPlayInTestWorld()) return false;
		UClass* Class = LoadClass<ACatCharacter>(nullptr, Path);
		if (!TestNotNull(TEXT("selected character class"), Class)) return false;
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACatCharacter* Cat = World.GetTestWorld()->SpawnActor<ACatCharacter>(Class,
			FVector(0,0,Class->GetDefaultObject<ACatCharacter>()->GetDefaultHalfHeight()), FRotator::ZeroRotator, Spawn);
		auto* Body = Cat->GetPhysicalBodyComponent();
		auto* Visual = Cat->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>();
		auto Step = [&](int32 Frames, bool bMove=false) { for (int32 Frame=0; Frame<Frames; ++Frame) {
			Body->SetMoveIntent(bMove ? FVector::ForwardVector : FVector::ZeroVector); World.TickTestWorld(1.0f/60.0f); } };
		if (FParse::Param(FCommandLine::Get(), TEXT("CatVariantScreenshots")))
		{
			FAssetCompilingManager::Get().FinishAllCompilation();
			if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
		}
		Step(90);
		if (!TestNotNull(TEXT("visible IK consumer initialized"), Visual->GetVisualMesh())) return false;
		auto Capture = [&](const TCHAR* Phase)
		{
			if (!FParse::Param(FCommandLine::Get(), TEXT("CatVariantScreenshots"))) return;
			AActor* Rig = World.GetTestWorld()->SpawnActor<AActor>();
			USceneCaptureComponent2D* Camera = NewObject<USceneCaptureComponent2D>(Rig);
			Rig->SetRootComponent(Camera);
			Camera->RegisterComponent();
			Camera->bCaptureEveryFrame = false;
			Camera->bCaptureOnMovement = false;
			Camera->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
			Camera->FOVAngle = 45;
			const FVector Center = Cat->GetActorLocation() + FVector(0,0,5);
			const FVector Position = Center + FVector(150,170,100);
			Camera->SetWorldLocationAndRotation(Position, (Center-Position).Rotation());
			UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(Rig);
			Target->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
			Target->InitAutoFormat(960,720);
			Target->UpdateResourceImmediate(true);
			Camera->TextureTarget = Target;
			for (const FVector Offset : {FVector(60,100,170), FVector(-100,-60,120)})
			{
				UPointLightComponent* Light = NewObject<UPointLightComponent>(Rig);
				Light->SetIntensity(15000);
				Light->SetCastShadows(false);
				Light->SetAttenuationRadius(1000);
				Light->RegisterComponent();
				Light->SetWorldLocation(Center+Offset);
			}
			FAssetCompilingManager::Get().FinishAllCompilation();
			if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
			World.GetTestWorld()->SendAllEndOfFrameUpdates();
			Camera->CaptureScene();
			const FString Directory = FPaths::ProjectSavedDir() / TEXT("CharacterVariantScreenshots");
			IFileManager::Get().MakeDirectory(*Directory, true);
			UKismetRenderingLibrary::ExportRenderTarget(World.GetTestWorld(), Target, Directory,
				Visual->RigSettings.RigId.ToString() + TEXT("_") + Phase + TEXT(".png"));
			Rig->Destroy();
		};
		Capture(TEXT("Idle"));
		TestEqual(TEXT("one animation source is the inherited Character mesh"), Visual->GetAnimationSource(), Cat->GetMesh());
		TestTrue(TEXT("capsule remains CMC authority"), Body->UsesCharacterMovement() && !Cat->GetCapsuleComponent()->IsSimulatingPhysics());
		const FVector Start = Cat->GetActorLocation();
		int32 WalkingFrames=0, GroundFrames=0;
		for (int32 Frame=0; Frame<180; ++Frame)
		{
			Step(1,true);
			const auto& Observation = Visual->GetLocomotionObservation();
			WalkingFrames += Observation.Mode == TEXT("Walking") && Observation.AnimationSpeedCmS > 1.0;
			GroundFrames += Observation.GroundMask == 15;
			if (Frame == 60) AddInfo(FString::Printf(TEXT("Event=variant_pose_probe Rig=%s Root=%s Pelvis=%s FootWorld=%s Actor=%s"),
				*Visual->RigSettings.RigId.ToString(), *Cat->GetMesh()->GetBoneSpaceTransforms()[0].ToString(),
				*Cat->GetMesh()->GetBoneTransform(Cat->GetMesh()->GetBoneIndex(Visual->RigSettings.PelvisBone)).ToString(),
				*Visual->GetVisualHandWorldLocation(true).ToString(), *Cat->GetActorLocation().ToString()));
			if (Frame == 120) Capture(TEXT("Walk"));
		}
		TestTrue(TEXT("CMC movement traverses the scene"), FVector::Dist2D(Start, Cat->GetActorLocation()) > 100);
		TestTrue(TEXT("shared template activates native calibrated walking"), WalkingFrames > 120);
		TestTrue(TEXT("all four configured feet find floor"), GroundFrames > 120);
		const auto CheckPose = [&]()
		{
			const auto& Source = Cat->GetMesh()->GetBoneSpaceTransforms();
			const auto& Solved = Visual->GetVisualMesh()->BoneSpaceTransforms;
			if (!TestEqual(TEXT("complete skeleton preserved"), Solved.Num(), Source.Num())) return;
			double MaxTranslationError=0, MaxScaleError=0;
			const int32 Pelvis = Cat->GetMesh()->GetBoneIndex(Visual->RigSettings.PelvisBone);
			const int32 Root = Cat->GetMesh()->GetBoneIndex(Visual->RigSettings.JumpRootBone);
			for (int32 Bone=0; Bone<Source.Num(); ++Bone)
			{
				TestFalse(TEXT("finite solved bone"), Solved[Bone].ContainsNaN());
				if (Bone != Pelvis && Bone != Root) MaxTranslationError = FMath::Max(MaxTranslationError, FVector::Distance(Source[Bone].GetTranslation(), Solved[Bone].GetTranslation()));
				MaxScaleError = FMath::Max(MaxScaleError, FVector::Distance(Source[Bone].GetScale3D(), Solved[Bone].GetScale3D()));
			}
			TestTrue(TEXT("IK preserves bone lengths including extra limb joints"), MaxTranslationError < 0.001);
			TestTrue(TEXT("IK preserves authored scale including imported root"), MaxScaleError < 0.001);
		};
		CheckPose();
		Step(60);
		// Exercise the real shared reaching solver on each limb layout with a reachable physical-hand target.
		const FVector PawBefore = Visual->GetVisualHandWorldLocation(true);
		const FVector HandTarget = PawBefore + FVector(3,0,4);
		Body->GetHand(true)->SetWorldLocation(HandTarget);
		Visual->SetHandReachState(true, false);
		for (int32 Frame=0; Frame<60; ++Frame)
			static_cast<UActorComponent*>(Visual)->TickComponent(1.0f/60.0f, LEVELTICK_All, nullptr);
		TestTrue(TEXT("configured full front chain reaches toward the physical hand"),
			FVector::Distance(Visual->GetVisualHandWorldLocation(true), HandTarget) < FVector::Distance(PawBefore, HandTarget) * 0.5);
		CheckPose();
		Visual->SetHandReachState(false, false);
		Step(60);
		Body->RequestJump();
		int32 AirFrames=0;
		const double GroundZ=Cat->GetActorLocation().Z;
		double Apex=GroundZ;
		for (int32 Frame=0; Frame<100; ++Frame) { Step(1); AirFrames += !Body->IsGrounded(); Apex=FMath::Max(Apex, Cat->GetActorLocation().Z); if (Frame == 12) Capture(TEXT("Jump")); }
		Cat->StopJumping();
		TestTrue(TEXT("character jump remains physical movement"), AirFrames > 5 && Apex > GroundZ + 10);
		TestTrue(TEXT("character returns to floor"), Body->IsGrounded());
		CheckPose();
		UAnimMontage* Original = LoadObject<UAnimMontage>(nullptr, TEXT("/Game/Animalia/Cat/AM_Attack_Agressive_Legs_01-IP_Montage"));
		UAnimMontage* Resolved = Cast<UAnimMontage>(Visual->ResolveAnimationAsset(Original));
		TestTrue(TEXT("global gameplay montage starts on selected skin"), Cat->PlayAnimMontage(Original) > 0);
		Step(4);
		Capture(TEXT("Action"));
		TestTrue(TEXT("selected animation instance is playing mapped asset"), Cat->GetMesh()->GetAnimInstance()->Montage_IsPlaying(Resolved));
		Cat->StopAnimMontage(Original);
		Step(90);
		TestFalse(TEXT("stop maps the same original montage identity"), Cat->GetMesh()->GetAnimInstance()->Montage_IsActive(Resolved));
		CheckPose();
		APlayerController* Controller = World.GetTestWorld()->SpawnActor<APlayerController>();
		Controller->Possess(Cat);
		if (!TestNotNull(TEXT("rod owner has real possessed player state"), Controller->PlayerState.Get())) return false;
		UClass* RodClass = LoadClass<ACatFishingRodActor>(nullptr, TEXT("/Game/Blueprint/Actors/BP_CatFishingRodActor.BP_CatFishingRodActor_C"));
		if (!TestNotNull(TEXT("production equipment rod blueprint"), RodClass)) return false;
		ACatFishingRodActor* Rod = World.GetTestWorld()->SpawnActor<ACatFishingRodActor>(RodClass);
		FCatFishingRodPresentationState Previous, Current;
		Current.OwnerPlayerState = Controller->PlayerState;
		Current.bDeployed = true;
		Rod->BP_OnRodPresentationChanged(Previous, Current);
		UAnimMontage* RodMontage = LoadObject<UAnimMontage>(nullptr, TEXT("/Game/Animalia/Cat/AM_Attack_Left-IP_Montage"));
		TestTrue(TEXT("real rod Blueprint callback accepts either character subclass and plays the mapped action"),
			Cat->GetMesh()->GetAnimInstance()->Montage_IsPlaying(Cast<UAnimMontage>(Visual->ResolveAnimationAsset(RodMontage))));
		Cat->StopAnimMontage(RodMontage);
		Rod->Destroy();
		AddInfo(FString::Printf(TEXT("Event=character_variant_runtime_verified Class=%s WalkingFrames=%d FourFootFrames=%d AirFrames=%d ApexCm=%.2f"), Path, WalkingFrames, GroundFrames, AirFrames, Apex-GroundZ));
	}
	return !HasAnyErrors();
}
#endif
