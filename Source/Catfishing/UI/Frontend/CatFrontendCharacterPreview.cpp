#include "UI/Frontend/CatFrontendCharacterPreview.h"

#include "Animation/AnimSequence.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Logging/CatLog.h"

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
	Capture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
	Capture->bCaptureEveryFrame = true;
	Capture->bCaptureOnMovement = false;
	Capture->FOVAngle = 35;
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->ShowFlags.SetFog(false);
	Capture->ShowFlags.SetMotionBlur(false);
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
		Light->SetIntensity(5.f);
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
	Capture->CaptureScene();
	UE_LOG(LogCatUI, Log, TEXT("Event=frontend_character_preview_created World=%s NetMode=%d Actor=%s Source=%s"),
		*GetNameSafe(GetWorld()), GetWorld() ? int32(GetWorld()->GetNetMode()) : -1, *GetName(), *CharacterClass->GetPathName());
	return true;
}
