#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/BufferArchive.h"
#include "Misc/FileHelper.h"
#include "Tests/AutomationCommon.h"
#include "Engine/SceneCapture2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/DirectionalLight.h"
#include "Components/LightComponent.h"
#include "GameFramework/PlayerState.h"
#include "ImageUtils.h"
#include "ShaderCompiler.h"
#include "AssetCompilingManager.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/Actors/CatFishingHookActor.h"
#include "Fishing/Presentation/CatRodBendComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatRodBendRenderTest,
	"Catfishing.Editor.Fishing.RodBendRender",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatRodBendRenderTest::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddError(TEXT("RodBendRender requires a rendering RHI; run with -RenderOffscreen, without -nullrhi."));
		return false;
	}
	UClass* RodClass = LoadClass<ACatFishingRodActor>(nullptr, TEXT("/Game/Blueprint/Actors/BP_CatFishingRodActor.BP_CatFishingRodActor_C"));
	UClass* HookClass = LoadClass<ACatFishingHookActor>(nullptr, TEXT("/Game/Blueprint/Actors/BP_CatFishingHookActor.BP_CatFishingHookActor_C"));
	if (!TestNotNull(TEXT("rod loads"), RodClass) || !TestNotNull(TEXT("hook loads"), HookClass)) return false;
	FAssetCompilingManager::Get().FinishAllCompilation();
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Wrapper.GetTestWorld();
	ACatFishingRodActor* Rod = World->SpawnActor<ACatFishingRodActor>(RodClass);
	APlayerState* Owner = World->SpawnActor<APlayerState>();
	Rod->ConfigureCanonicalAnchorsFromAuthority(FTransform(FVector(161.52, -1.30, 151.89)), FTransform::Identity, FTransform::Identity);
	Rod->InitializeAuthoritativeIdentity(FGuid::NewGuid(), FGuid::NewGuid(), TEXT("Rod_Basic"), NAME_None, Owner, nullptr, true, false);
	FActorSpawnParameters Spawn;
	Spawn.Owner = Rod;
	ACatFishingHookActor* Hook = World->SpawnActor<ACatFishingHookActor>(HookClass, FTransform::Identity, Spawn);
	Hook->InitializeAuthoritativeIdentity(FGuid::NewGuid(), FGuid::NewGuid());
	ADirectionalLight* Light = World->SpawnActor<ADirectionalLight>();
	Light->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	Light->SetActorRotation(FRotator(-35, -50, 0));
	Light->GetLightComponent()->SetIntensity(4.0f);
	ASceneCapture2D* Camera = World->SpawnActor<ASceneCapture2D>();
	const FVector Target(80, 20, 80), CameraLocation(650, 20, 80);
	Camera->SetActorLocationAndRotation(CameraLocation, (Target - CameraLocation).Rotation());
	USceneCaptureComponent2D* Capture = Camera->GetCaptureComponent2D();
	Capture->ProjectionType = ECameraProjectionMode::Orthographic;
	Capture->OrthoWidth = 230.0f;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->ShowFlags.SetMotionBlur(false);
	Capture->ShowFlags.SetTemporalAA(false);
	Capture->ShowFlags.SetLighting(false);
	Capture->PostProcessSettings.bOverride_AutoExposureMethod = true;
	Capture->PostProcessSettings.AutoExposureMethod = AEM_Manual;
	Capture->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Capture->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
	Capture->PostProcessSettings.bOverride_AutoExposureBias = true;
	Capture->PostProcessSettings.AutoExposureBias = 3.0f;
	UTextureRenderTarget2D* Texture = NewObject<UTextureRenderTarget2D>(Capture);
	Texture->RenderTargetFormat = RTF_RGBA8;
	Texture->ClearColor = FLinearColor(0.08f, 0.10f, 0.13f, 1.0f);
	Texture->InitAutoFormat(1024, 1024);
	Texture->UpdateResourceImmediate(true);
	Capture->TextureTarget = Texture;
	Wrapper.BeginPlayInTestWorld();
	UCatRodBendComponent* Bend = Rod->FindComponentByClass<UCatRodBendComponent>();
	if (!TestTrue(TEXT("formal deformation ready for rendering"), Bend && Bend->IsVisualReady())) return false;
	Hook->FinalizeAuthoritativeLandingOnce(true, Bend->GetRestTipWorld() + FVector(0, 700, 0));
	if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
	const FString Output = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/RodBend/Images"));
	IFileManager::Get().MakeDirectory(*Output, true);
	for (int32 Pose = 0; Pose < 3; ++Pose)
	{
		const TCHAR* Name = Pose == 0 ? TEXT("01-rest.png") : Pose == 1 ? TEXT("02-loaded.png") : TEXT("03-released.png");
		Hook->SetFishingLinePresentationFromAuthority(700, 700, 0, Pose == 1 ? 1 : 0, Pose == 1, Pose == 1 ? 100 : 0);
		for (int32 I = 0; I < 180; ++I) Wrapper.TickTestWorld(1.0f / 60.0f);
		World->SendAllEndOfFrameUpdates();
		Capture->CaptureScene();
		FBufferArchive PNG;
		if (!TestTrue(TEXT("capture exports PNG"), FImageUtils::ExportRenderTarget2DAsPNG(Texture, PNG))) return false;
		if (!TestTrue(TEXT("capture written"), FFileHelper::SaveArrayToFile(PNG, *(Output / Name)))) return false;
		AddInfo(FString::Printf(TEXT("RodBendRender Pose=%s File=%s BendRadians=%s"), Name, *(Output / Name), *Bend->GetBendRadians().ToString()));
	}
	return !HasAnyErrors();
}

#endif
