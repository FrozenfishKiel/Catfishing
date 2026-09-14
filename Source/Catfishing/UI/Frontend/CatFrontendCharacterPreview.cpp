#include "UI/Frontend/CatFrontendCharacterPreview.h"

#include "Animation/AnimSequence.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/TextureCube.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Logging/CatLog.h"
#include "UObject/ConstructorHelpers.h"

ACatFrontendCharacterPreview::ACatFrontendCharacterPreview()
{
	PrimaryActorTick.bCanEverTick = false;
	SetReplicates(false);
	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("PreviewMesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Mesh->CastShadow = false;
	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("PreviewCapture"));
	Capture->SetupAttachment(Mesh);
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	// SceneColorHDR disables post processing (and thus temporal AA). Fur needs the final-color path.
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalToneCurveHDR;
	Capture->bAlwaysPersistRenderingState = true;
	Capture->bCaptureEveryFrame = true;
	Capture->bCaptureOnMovement = false;
	Capture->FOVAngle = 35;
	Capture->PostProcessSettings.bOverride_AutoExposureMethod = true;
	Capture->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	Capture->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Capture->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
	Capture->PostProcessSettings.bOverride_DynamicGlobalIlluminationMethod = true;
	Capture->PostProcessSettings.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
	Capture->PostProcessSettings.bOverride_ReflectionMethod = true;
	Capture->PostProcessSettings.ReflectionMethod = EReflectionMethod::None;
	// Hair cards need diffuse fill from all directions, as in an asset preview studio.
	// This cubemap belongs only to the capture; it does not add a skylight to the game world.
	static ConstructorHelpers::FObjectFinder<UTextureCube> AmbientCube(TEXT("/Engine/EngineResources/GrayLightTextureCube.GrayLightTextureCube"));
	Capture->PostProcessSettings.AmbientCubemap = AmbientCube.Object;
	Capture->PostProcessSettings.bOverride_AmbientCubemapIntensity = true;
	Capture->PostProcessSettings.AmbientCubemapIntensity = .65f;
	Capture->PostProcessSettings.bOverride_AmbientOcclusionIntensity = true;
	Capture->PostProcessSettings.AmbientOcclusionIntensity = 0.f;
	// Final color does not retain opacity unless global alpha output is enabled. Keep that
	// project-wide setting untouched and capture inverse opacity from the same posed mesh.
	MaskCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("PreviewMaskCapture"));
	MaskCapture->SetupAttachment(Capture);
	MaskCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	MaskCapture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
	MaskCapture->bCaptureEveryFrame = true;
	MaskCapture->bCaptureOnMovement = false;
	MaskCapture->FOVAngle = Capture->FOVAngle;
	// Component registration rebuilds ShowFlags from the archetype and serialized overrides.
	// Persist overrides rather than relying on the constructor's transient bitset.
	auto SetFlags = [](USceneCaptureComponent2D* Component, std::initializer_list<TPair<const TCHAR*, bool>> Settings)
	{
		TArray<FEngineShowFlagsSetting> Flags;
		for (const auto& Entry : Settings)
		{
			FEngineShowFlagsSetting& Flag = Flags.AddDefaulted_GetRef();
			Flag.ShowFlagName = Entry.Key;
			Flag.Enabled = Entry.Value;
		}
		Component->SetShowFlagSettings(Flags);
	};
	SetFlags(Capture, {{TEXT("TemporalAA"), true}, {TEXT("AntiAliasing"), true}, {TEXT("PostProcessing"), true},
		{TEXT("AmbientCubemap"), true}, {TEXT("SkyLighting"), true},
		{TEXT("Atmosphere"), false}, {TEXT("Fog"), false}, {TEXT("MotionBlur"), false}, {TEXT("Bloom"), false}});
	SetFlags(MaskCapture, {{TEXT("Lighting"), false}, {TEXT("PostProcessing"), false},
		{TEXT("Atmosphere"), false}, {TEXT("Fog"), false}});
	KeyLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("PreviewKey"));
	KeyLight->SetupAttachment(Mesh);
	KeyLight->SetRelativeLocation(FVector(70, 70, 110));
	FillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("PreviewFill"));
	FillLight->SetupAttachment(Mesh);
	FillLight->SetRelativeLocation(FVector(-70, 10, 60));
	for (UPointLightComponent* Light : { KeyLight.Get(), FillLight.Get() })
	{
		Light->SetCastShadows(false);
		Light->SetUseInverseSquaredFalloff(false);
		Light->SetIntensity(1.5f);
		Light->SetAttenuationRadius(400.f);
	}
	KeyLight->SetLightColor(FLinearColor(1.f, .83f, .61f));
	FillLight->SetLightColor(FLinearColor(.53f, .78f, 1.f));
}

bool ACatFrontendCharacterPreview::InitializePreview(TSubclassOf<ACharacter> CharacterClass, UAnimSequence* Animation)
{
	const ACharacter* Default = CharacterClass ? CharacterClass->GetDefaultObject<ACharacter>() : nullptr;
	const USkeletalMeshComponent* Source = Default ? Default->GetMesh() : nullptr;
	USkeletalMesh* Asset = Source ? Source->GetSkeletalMeshAsset() : nullptr;
	if (!Asset) { return false; }
	Mesh->SetSkeletalMesh(Asset);
	for (int32 Index = 0; Index < Source->GetNumMaterials(); ++Index) { Mesh->SetMaterial(Index, Source->GetMaterial(Index)); }
	if (Animation && Animation->GetSkeleton() == Asset->GetSkeleton()) { Mesh->PlayAnimation(Animation, true); }
	const float Radius = FMath::Max(Asset->GetBounds().SphereRadius, 10.f);
	const FVector Target(0, 0, Radius * .43f);
	const FVector Camera = Target + FVector(0.f, 2.15f, .2f) * Radius;
	Capture->SetRelativeLocation(Camera);
	Capture->SetRelativeRotation((Target - Camera).Rotation());
	Texture = NewObject<UTextureRenderTarget2D>(this);
	Texture->ClearColor = FLinearColor(0, 0, 0, 1);
	Texture->InitCustomFormat(640, 768, PF_FloatRGBA, false);
	Capture->TextureTarget = Texture;
	Capture->ShowOnlyComponent(Mesh);
	MaskTexture = NewObject<UTextureRenderTarget2D>(this);
	MaskTexture->ClearColor = FLinearColor(0, 0, 0, 1);
	MaskTexture->InitCustomFormat(640, 768, PF_FloatRGBA, false);
	MaskCapture->TextureTarget = MaskTexture;
	MaskCapture->ShowOnlyComponent(Mesh);
	// Both captures update on the next frame; an extra immediate capture duplicates that work.
	UE_LOG(LogCatUI, Log, TEXT("Event=frontend_character_preview_rendering World=%s NetMode=%d Actor=%s PostProcessing=%d TemporalAA=%d AntiAliasing=%d AmbientCube=%s AmbientIntensity=%.2f"),
		*GetNameSafe(GetWorld()), GetWorld() ? int32(GetWorld()->GetNetMode()) : -1, *GetName(),
		Capture->ShowFlags.PostProcessing, Capture->ShowFlags.TemporalAA, Capture->ShowFlags.AntiAliasing,
		*GetNameSafe(Capture->PostProcessSettings.AmbientCubemap), Capture->PostProcessSettings.AmbientCubemapIntensity);
	UE_LOG(LogCatUI, Log, TEXT("Event=frontend_character_preview_created World=%s NetMode=%d Actor=%s Source=%s Color=FinalToneCurveHDR Mask=SceneColorInverseOpacity Size=640x768"),
		*GetNameSafe(GetWorld()), GetWorld() ? int32(GetWorld()->GetNetMode()) : -1, *GetName(), *CharacterClass->GetPathName());
	return true;
}
