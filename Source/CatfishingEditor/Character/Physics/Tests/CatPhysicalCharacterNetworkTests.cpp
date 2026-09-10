#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "InputKeyEventArgs.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UI/HUD/CatHUDWidget.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/SWindow.h"
#include "UnrealClient.h"

namespace CatPhysicalCharacterNetwork
{
class FRestore final : public IAutomationLatentCommand
{
public:
	FRestore()
	{
		const auto* Settings=GetDefault<ULevelEditorPlaySettings>();
		Settings->GetPlayNetMode(Mode); Settings->GetPlayNumberOfClients(Count); Settings->GetRunUnderOneProcess(OneProcess);
		NewWindowSize=FIntPoint(Settings->NewWindowWidth,Settings->NewWindowHeight); Settings->GetClientWindowSize(ClientWindowSize);
		Drivers=GEngine->NetDriverDefinitions;
		StableWorld=FWorldDelegates::OnPreWorldInitialization.AddLambda([](UWorld* World,const UWorld::InitializationValues)
		{ if (World && World->WorldType==EWorldType::PIE) World->bIsNameStableForNetworking=true; });
		GameModeInitialized=FGameModeEvents::OnGameModeInitializedEvent().AddLambda([](AGameModeBase* GameMode)
		{
			if (!GameMode || !GameMode->GetWorld() || GameMode->GetWorld()->WorldType!=EWorldType::PIE
				|| GameMode->GetClass()!=AGameModeBase::StaticClass()) return;
			GameMode->DefaultPawnClass=LoadClass<ACatCharacter>(nullptr,TEXT("/Game/Character/BP_CatCharacter.BP_CatCharacter_C"));
			GameMode->PlayerControllerClass=LoadClass<ACatfishingPlayerController>(nullptr,TEXT("/Game/Player/BP_CatFishingController.BP_CatFishingController_C"));
			GameMode->PlayerStateClass=ACatfishingPlayerState::StaticClass();
		});
	}
	~FRestore() override { Restore(); }
	bool Update() override { if (GEditor && GEditor->PlayWorld) return false; Restore(); return true; }
private:
	void Restore()
	{
		if (bRestored) return;
		auto* Settings=GetMutableDefault<ULevelEditorPlaySettings>();
		Settings->SetPlayNetMode(Mode); Settings->SetPlayNumberOfClients(Count); Settings->SetRunUnderOneProcess(OneProcess);
		Settings->NewWindowWidth=NewWindowSize.X; Settings->NewWindowHeight=NewWindowSize.Y; Settings->SetClientWindowSize(ClientWindowSize);
		if (GEngine) GEngine->NetDriverDefinitions=Drivers;
		FWorldDelegates::OnPreWorldInitialization.Remove(StableWorld);
		FGameModeEvents::OnGameModeInitializedEvent().Remove(GameModeInitialized);
		bRestored=true;
	}
	EPlayNetMode Mode=PIE_Standalone;
	int32 Count=1;
	FIntPoint NewWindowSize,ClientWindowSize;
	bool OneProcess=true,bRestored=false;
	TArray<FNetDriverDefinition> Drivers;
	FDelegateHandle StableWorld,GameModeInitialized;
};

class FVerify final : public IAutomationLatentCommand
{
public:
	explicit FVerify(FAutomationTestBase* InTest):Test(InTest),Started(FPlatformTime::Seconds()) {}
	bool Update() override
	{
		const double Now=FPlatformTime::Seconds();
		if (Now-Started>55.0)
		{
			Test->AddError(FString::Printf(TEXT("Physical formal network timeout Stage=%d LastWait={%s}; no dual-end verdict"),Stage,*LastWait));
			return true;
		}
		UWorld* Server=nullptr; UWorld* Client=nullptr;
		for (const FWorldContext& Context:GEngine->GetWorldContexts())
		{
			if (Context.WorldType!=EWorldType::PIE || !Context.World()) continue;
			if (Context.World()->GetNetMode()==NM_ListenServer) Server=Context.World();
			if (Context.World()->GetNetMode()==NM_Client) Client=Context.World();
		}
		if (!Server || !Client) return Wait(TEXT("PIE worlds"));
		auto* Local=Cast<ACatfishingPlayerController>(Client->GetFirstPlayerController());
		auto* ClientCat=Local?Cast<ACatCharacter>(Local->GetPawn()):nullptr;
		ACatfishingPlayerController* Remote=nullptr;
		ACatCharacter* HostCat=nullptr;
		for (TActorIterator<ACatfishingPlayerController> It(Server);It;++It)
		{
			if (!It->IsLocalController()) Remote=*It;
			else HostCat=Cast<ACatCharacter>(It->GetPawn());
		}
		auto* ServerCat=Remote?Cast<ACatCharacter>(Remote->GetPawn()):nullptr;
		if (!Local || !ClientCat || !ServerCat || !HostCat || !Local->PlayerState || !Remote->PlayerState)
			return Wait(TEXT("formal Controllers/Pawns/PlayerStates"));
		auto* ServerBody=ServerCat->GetPhysicalBodyComponent();
		auto* ClientBody=ClientCat->GetPhysicalBodyComponent();
		auto* ServerGrab=ServerBody->GetGrab();
		auto* ClientGrab=ClientBody->GetGrab();
		if (!ServerGrab || !ClientGrab || !ClientBody->HasMovementSample()) return Wait(TEXT("physical body initialization and first client sample"));
		if (Stage==0)
		{
			Test->TestEqual(TEXT("listen uses actual formal pawn asset"),ServerCat->GetClass()->GetPathName(),FString(TEXT("/Game/Character/BP_CatCharacter.BP_CatCharacter_C")));
			Test->TestEqual(TEXT("client uses actual formal controller asset"),Local->GetClass()->GetPathName(),FString(TEXT("/Game/Player/BP_CatFishingController.BP_CatFishingController_C")));
			const double Spacing = 3.0 * FMath::Max(ServerBody->GetBody()->GetScaledBoxExtent().X,
				HostCat->GetPhysicalBodyComponent()->GetBody()->GetScaledBoxExtent().X);
			ServerCat->TeleportTo(FVector(0,0,ServerCat->GetBodyStandRootHeightCm()),FRotator::ZeroRotator,false,false);
			HostCat->TeleportTo(FVector(0,Spacing,HostCat->GetBodyStandRootHeightCm()),FRotator(0,90,0),false,false);
			if (AController* HostController=HostCat->GetController()) HostController->SetControlRotation(FRotator(0,90,0));
			Remote->SetControlRotation(FRotator::ZeroRotator);
			Local->SetControlRotation(FRotator::ZeroRotator);
			Stage=1; StageStarted=Now;
		}
		if (Stage==1)
		{
			if (Now-StageStarted<1.0 || FVector::Distance(ClientCat->GetActorLocation(),ServerCat->GetActorLocation())>4.0)
				return Wait(TEXT("initial replicated placement"));
			Test->TestTrue(TEXT("server body owns Chaos simulation"),ServerBody->GetBody()->IsSimulatingPhysics());
			Test->TestFalse(TEXT("client observes server body snapshots"),ClientBody->GetBody()->IsSimulatingPhysics());
			// Only the owning client changes view. Server yaw must arrive through the physical input RPC, not a test write.
			Local->SetControlRotation(FRotator(0,90,0));
			Stage=2; StageStarted=Now;
		}
		if (Stage==2)
		{
			const double YawError=FMath::Abs(FMath::FindDeltaAngleDegrees(Remote->GetControlRotation().Yaw,90.0));
			const double BodyYawError=FMath::Abs(FMath::FindDeltaAngleDegrees(ServerCat->GetActorRotation().Yaw,90.0));
			if (Now-StageStarted<1.0 || YawError>2.0 || BodyYawError>5.0)
				return Wait(FString::Printf(TEXT("server view/motor yaw %.2f/%.2f"),Remote->GetControlRotation().Yaw,ServerCat->GetActorRotation().Yaw));
			Test->TestTrue(TEXT("server trace/cast controller view rotated 90 degrees without CMC movement"),Remote->GetControlRotation().Vector().Y>.99);
			Test->AddInfo(FString::Printf(TEXT("Event=physical_formal_network_view_verified ServerWorld=%s ClientWorld=%s PlayerId=%d ServerControlYaw=%.3f ClientControlYaw=%.3f ServerBodyYaw=%.3f Source=PhysicalInputRpc"),
				*Server->GetName(),*Client->GetName(),Local->PlayerState->GetPlayerId(),Remote->GetControlRotation().Yaw,Local->GetControlRotation().Yaw,ServerCat->GetActorRotation().Yaw));
			// Real key event -> formal IMC/AbilityInputConfig -> route latch -> owning-client grab RPC.
			Local->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton,IE_Pressed,1.0f));
			Stage=3; StageStarted=Now;
		}
		if (Stage==3)
		{
			if (!ServerGrab->IsGripping(true) || !ClientGrab->IsGripping(true))
				return Wait(FString::Printf(TEXT("real left grip ServerReach=%d ClientReach=%d ServerGrip=%d ClientGrip=%d"),ServerGrab->IsReaching(true),ClientGrab->IsReaching(true),ServerGrab->IsGripping(true),ClientGrab->IsGripping(true)));
			auto* ClientTarget=Cast<ACatCharacter>(ClientGrab->GetGripTarget(true));
			if (!ClientTarget || !ClientTarget->GetPlayerState() || !HostCat->GetPlayerState()
				|| ClientTarget->GetPlayerState()->GetPlayerId()!=HostCat->GetPlayerState()->GetPlayerId()
				|| ClientGrab->GetGripRevision(true)!=ServerGrab->GetGripRevision(true)) return Wait(TEXT("matching target identity/revision on client"));
			if (!Test->TestEqual(TEXT("new view grabs target at +Y rather than old +X"),ServerGrab->GetGripTarget(true),static_cast<AActor*>(HostCat))) return true;
			// Observe the HUD assembled by the real local-player subsystem; do not create a second test-only Model/View.
			int32 ActiveHUDCount=0;
			for (TObjectIterator<UCatHUDWidget> It;It;++It)
				if (It->GetWorld()==Client && It->GetOwningPlayer()==Local && It->IsInViewport())
				{ Widget=*It; ++ActiveHUDCount; }
			if (!Widget.IsValid()) return Wait(TEXT("production LocalPlayerUISubsystem attaches formal HUD"));
			if (!Test->TestEqual(TEXT("one production HUD belongs to the owning client"),ActiveHUDCount,1)) return true;
			if (!Test->TestEqual(TEXT("production HUD uses actual formal WBP"),Widget->GetClass()->GetPathName(),FString(TEXT("/Game/UI/HUD/WBP_CatHUD.WBP_CatHUD_C")))) return true;
			if (!Widget->GetLastHUDViewState().bLeftHandGripped) return Wait(TEXT("production HUD receives replicated grip through its bound model"));
			if (!Test->TestNotNull(TEXT("formal WBP delivers physical instructions"),Widget->GetWidgetFromName(TEXT("PhysicalControlTextBlock")))
				|| !Test->TestNotNull(TEXT("formal WBP delivers actual hand state"),Widget->GetWidgetFromName(TEXT("PhysicalHandStateTextBlock")))) return true;
			GripRevision=ServerGrab->GetGripRevision(true);
			TargetStart=HostCat->GetActorLocation();
			if (FApp::CanEverRender())
			{
				const FVector Center=(ClientCat->GetActorLocation()+ClientTarget->GetActorLocation())*.5;
				ACameraActor* Camera=Client->SpawnActor<ACameraActor>();
				const FVector Position=Center+FVector(-180,0,95);
				Camera->SetActorLocationAndRotation(Position,(Center-Position).Rotation());
				Camera->GetCameraComponent()->SetFieldOfView(65.0f);
				Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
				EvidenceCamera=Camera;
				Local->SetViewTarget(Camera);
			}
			Local->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::S,IE_Pressed,1.0f));
			Stage=4; StageStarted=Now;
		}
		if (Stage==4)
		{
			if (EvidenceCamera.IsValid())
			{
				const AActor* Target=ClientGrab->GetGripTarget(true);
				const FVector Center=(ClientCat->GetActorLocation()+(Target?Target->GetActorLocation():ClientCat->GetActorLocation()))*.5;
				const FVector Position=Center+FVector(-180,0,95);
				EvidenceCamera->SetActorLocationAndRotation(Position,(Center-Position).Rotation());
			}
			if (Now-StageStarted<0.8) return false;
			if (!Test->TestTrue(TEXT("contact remains physically gripped on both endpoints immediately before Flush"),
				ServerGrab->IsGripping(true) && ClientGrab->IsGripping(true) && ServerGrab->GetGripTarget(true)==HostCat)) return true;
			if (!Test->TestTrue(TEXT("formal client backward input pulls server target through actual contact"),HostCat->GetActorLocation().Y<TargetStart.Y-2.0)) return true;
			Test->AddInfo(FString::Printf(TEXT("Event=physical_formal_network_grip_verified ServerWorld=%s ClientWorld=%s PlayerId=%d GripId=%s ServerRevision=%u ClientRevision=%u TargetTravelCm=%.3f ServerObserved=1 ClientObserved=1"),
				*Server->GetName(),*Client->GetName(),Local->PlayerState->GetPlayerId(),*ServerGrab->GetGripState(true).GripId.ToString(),ServerGrab->GetGripRevision(true),ClientGrab->GetGripRevision(true),TargetStart.Y-HostCat->GetActorLocation().Y));
			if (FApp::CanEverRender()) Capture(Client);
			// Intentionally omit key-up: focus loss must release both grip and WASD through the same production cleanup path.
			Local->FlushPressedKeys();
			Stage=5; StageStarted=Now;
		}
		if (Stage==5)
		{
			if (ServerGrab->IsReaching(true)||ClientGrab->IsReaching(true)||ServerGrab->IsGripping(true)||ClientGrab->IsGripping(true)
				|| !ServerBody->GetMoveIntent().IsNearlyZero()||ClientGrab->GetGripRevision(true)<=GripRevision)
				return Wait(TEXT("Flush releases replicated contact and server movement intent"));
			if (!Widget.IsValid() || Widget->GetLastHUDViewState().bLeftHandGripped)
				return Wait(TEXT("production HUD clears replicated grip through its bound model after Flush"));
			Test->TestFalse(TEXT("client production HUD clears replicated grip after Flush"),Widget->GetLastHUDViewState().bLeftHandGripped);
			Test->TestNull(TEXT("authority contact target cleaned"),ServerGrab->GetGripTarget(true));
			Test->TestNull(TEXT("client contact target cleaned"),ClientGrab->GetGripTarget(true));
			Test->AddInfo(FString::Printf(TEXT("Event=physical_formal_network_release_verified ServerWorld=%s ClientWorld=%s PlayerId=%d ServerRevision=%u ClientRevision=%u Result=FormalInputViewGripForceFlushHUD ServerObserved=1 ClientObserved=1 Prediction=0"),
				*Server->GetName(),*Client->GetName(),Local->PlayerState->GetPlayerId(),ServerGrab->GetGripRevision(true),ClientGrab->GetGripRevision(true)));
			return true;
		}
		return false;
	}
private:
	bool Wait(const FString& Reason) { LastWait=Reason; return false; }
	void Capture(UWorld* World)
	{
		UGameViewportClient* Client=World->GetGameViewport();
		const TSharedPtr<SWindow> Window=Client?Client->GetWindow():nullptr;
		if (!Test->TestTrue(TEXT("independent formal client Slate window"),Window.IsValid() && FSlateApplication::IsInitialized())) return;
		TArray<FColor> Pixels;
		FIntVector Size;
		if (!Test->TestTrue(TEXT("capture final Slate composition including actual formal HUD"),FSlateApplication::Get().TakeScreenshot(Window.ToSharedRef(),Pixels,Size))) return;
		if (!Test->TestTrue(TEXT("captured viewport contains the full image"),Size.X>0 && Size.Y>0 && Pixels.Num()==Size.X*Size.Y)) return;
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Size.X,Size.Y,TArrayView64<const FColor>(Pixels.GetData(),Pixels.Num()),Png);
		const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Automation/PhysicalGrabProduction/Images"));
		IFileManager::Get().MakeDirectory(*Directory,true);
		const FString File=Directory/FString::Printf(TEXT("%s-formal-client-grip.png"),*FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")));
		if (Test->TestTrue(TEXT("writes fresh formal visual evidence"),FFileHelper::SaveArrayToFile(Png,*File)))
			Test->AddInfo(FString::Printf(TEXT("Event=physical_formal_viewport_captured File=%s Width=%d Height=%d Pawn=/Game/Character/BP_CatCharacter HUD=/Game/UI/HUD/WBP_CatHUD Map=IsolatedFixture"),*File,Size.X,Size.Y));
	}
	FAutomationTestBase* Test;
	double Started=0,StageStarted=0;
	int32 Stage=0;
	uint32 GripRevision=0;
	FVector TargetStart=FVector::ZeroVector;
	FString LastWait;
	TWeakObjectPtr<UCatHUDWidget> Widget;
	TWeakObjectPtr<ACameraActor> EvidenceCamera;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPhysicalCharacterNetworkTest,
	"Catfishing.PhysicalGrab.Network.FormalClientViewGripForceAndFocusRelease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatPhysicalCharacterNetworkTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("requires its own idle validation editor"),GEditor&&GEngine&&!GEditor->PlayWorld)) return false;
	if (!TestNotNull(TEXT("formal BP character exists"),LoadClass<ACatCharacter>(nullptr,TEXT("/Game/Character/BP_CatCharacter.BP_CatCharacter_C")))
		|| !TestNotNull(TEXT("formal BP controller exists"),LoadClass<ACatfishingPlayerController>(nullptr,TEXT("/Game/Player/BP_CatFishingController.BP_CatFishingController_C")))) return false;
	const auto Restore=MakeShared<CatPhysicalCharacterNetwork::FRestore>();
	UWorld* Map=nullptr;
	if (FApp::CanEverRender())
	{
		if (!TestTrue(TEXT("loads existing authored lighting without saving assets"),FEditorFileUtils::LoadMap(FPaths::ProjectContentDir()/TEXT("Catfishing/Prototypes/PhysicsGrabPrototype.umap"),false,false))) return false;
		Map=GEditor->GetEditorWorldContext().World();
	}
	else Map=FAutomationEditorCommonUtils::CreateNewMap();
	if (!Map) return false;
	Map->bIsNameStableForNetworking=true;
	Map->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();
	AStaticMeshActor* Floor=Map->SpawnActor<AStaticMeshActor>();
	UStaticMesh* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Floor || !Cube) return false;
	Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	Floor->GetStaticMeshComponent()->SetStaticMesh(Cube);
	Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
	Floor->SetActorTransform(FTransform(FRotator::ZeroRotator,FVector(0,0,-10),FVector(20,20,.2)));
	Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
	Map->SpawnActor<APlayerStart>(FVector(-150,0,50),FRotator::ZeroRotator);
	Map->SpawnActor<APlayerStart>(FVector(150,0,50),FRotator::ZeroRotator);
	auto* Settings=GetMutableDefault<ULevelEditorPlaySettings>();
	Settings->SetPlayNetMode(PIE_ListenServer); Settings->SetPlayNumberOfClients(2); Settings->SetRunUnderOneProcess(true);
	if (FApp::CanEverRender()) { Settings->NewWindowWidth=1280; Settings->NewWindowHeight=720; Settings->SetClientWindowSize(FIntPoint(1280,720)); }
	for (auto& Driver:GEngine->NetDriverDefinitions)
		if (Driver.DefName==TEXT("GameNetDriver")) Driver.DriverClassName=Driver.DriverClassNameFallback=TEXT("/Script/OnlineSubsystemUtils.IpNetDriver");
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatPhysicalCharacterNetwork::FVerify>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
	return true;
}

#endif
