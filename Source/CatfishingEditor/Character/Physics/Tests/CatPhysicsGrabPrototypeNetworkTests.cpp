#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Character/Physics/CatPhysicsPrototypePawn.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "Framework/Game/PhysicsPrototype/CatPhysicsPrototypeGameMode.h"
#include "FileHelpers.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "ImageUtils.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UnrealClient.h"

namespace CatPhysicsGrabNetwork
{

class FRestore final : public IAutomationLatentCommand
{
public:
	FRestore()
	{
		const auto* Settings = GetDefault<ULevelEditorPlaySettings>();
		Settings->GetPlayNetMode(Mode);
		Settings->GetPlayNumberOfClients(Count);
		Settings->GetRunUnderOneProcess(OneProcess);
		Drivers = GEngine->NetDriverDefinitions;
		WorldHandle = FWorldDelegates::OnPreWorldInitialization.AddLambda([](UWorld* World, const UWorld::InitializationValues)
		{
			if (World && World->WorldType == EWorldType::PIE) World->bIsNameStableForNetworking = true;
		});
	}
	~FRestore() override { Restore(); }
	bool Update() override
	{
		if (GEditor && GEditor->PlayWorld) return false;
		Restore();
		return true;
	}
private:
	void Restore()
	{
		if (bRestored) return;
		auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
		Settings->SetPlayNetMode(Mode);
		Settings->SetPlayNumberOfClients(Count);
		Settings->SetRunUnderOneProcess(OneProcess);
		if (GEngine) GEngine->NetDriverDefinitions = Drivers;
		FWorldDelegates::OnPreWorldInitialization.Remove(WorldHandle);
		bRestored = true;
	}
	EPlayNetMode Mode = PIE_Standalone;
	int32 Count = 1;
	bool OneProcess = true;
	bool bRestored = false;
	TArray<FNetDriverDefinition> Drivers;
	FDelegateHandle WorldHandle;
};

class FVerify final : public IAutomationLatentCommand
{
public:
	explicit FVerify(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}
	bool Update() override
	{
		const double Now = FPlatformTime::Seconds();
		if (Now - Started > 45.0)
		{
			Test->AddError(FString::Printf(TEXT("Physics grab network timed out Stage=%d; no client/server verdict"), Stage));
			return true;
		}
		UWorld* Server = nullptr;
		UWorld* Client = nullptr;
		for (const auto& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType != EWorldType::PIE || !Context.World()) continue;
			if (Context.World()->GetNetMode() == NM_ListenServer) Server = Context.World();
			if (Context.World()->GetNetMode() == NM_Client) Client = Context.World();
		}
		if (!Server || !Client) return false;
		APlayerController* Local = Client->GetFirstPlayerController();
		auto* ClientPawn = Local ? Cast<ACatPhysicsPrototypePawn>(Local->GetPawn()) : nullptr;
		if (!ClientPawn || !Local->PlayerState) return false;
		ACatPhysicsPrototypePawn* AuthorityPawn = nullptr;
		ACatPhysicsPrototypePawn* HostPawn = nullptr;
		for (TActorIterator<ACatPhysicsPrototypePawn> It(Server); It; ++It)
		{
			if (It->GetPlayerState() && It->GetPlayerState()->GetPlayerId() == Local->PlayerState->GetPlayerId()) AuthorityPawn = *It;
			else if (It->IsLocallyControlled()) HostPawn = *It;
		}
		if (!AuthorityPawn || (!HostPawn && Stage < 6)) return false;
		auto* ServerGrab = AuthorityPawn->GetGrabComponent();
		auto* ClientGrab = ClientPawn->GetGrabComponent();
		if (Stage == 0)
		{
			Local->SetActorTickEnabled(false);
			for (auto It = Server->GetPlayerControllerIterator(); It; ++It)
				if (It->Get()) It->Get()->SetActorTickEnabled(false);
			Place(AuthorityPawn, FVector(0, -150, 20));
			Place(HostPawn, FVector(39, -150, 20));
			TargetId = HostPawn->GetPrototypeId();
			Stage = 1;
			StageStarted = Now;
		}
		// The released pair can coast apart. A second grab uses actual forward movement to return within reach.
		const FVector2D Move = Stage == 3 ? FVector2D(-1, 0) : Stage == 5 ? FVector2D(1, 0) : FVector2D::ZeroVector;
		ClientPawn->SetPrototypeInput(Move, FRotator::ZeroRotator);
		if (HostPawn) HostPawn->SetPrototypeInput(FVector2D::ZeroVector, FRotator::ZeroRotator);
		if (Stage == 1)
		{
			if (Now - StageStarted < 0.8 || FVector::Distance(ClientPawn->GetActorLocation(), AuthorityPawn->GetActorLocation()) > 5.0) return false;
			Test->TestTrue(TEXT("server owns the physical body"), AuthorityPawn->GetPhysicsBody()->IsSimulatingPhysics());
			Test->TestFalse(TEXT("client only consumes authoritative snapshots"), ClientPawn->GetPhysicsBody()->IsSimulatingPhysics());
			ClientPawn->SetGrabInput(true, true); // Real owning-client RPC; never call authority ApplyGrabInput.
			Stage = 2;
			StageStarted = Now;
		}
		if (Stage == 2)
		{
			if (!ServerGrab->IsGripping(true) || !ClientGrab->IsGripping(true)) return false;
			auto* ObservedTarget = Cast<ACatPhysicsPrototypePawn>(ClientGrab->GetGripTarget(true));
			if (!ObservedTarget || ObservedTarget->GetPrototypeId() != TargetId) return false;
			if (!Test->TestTrue(TEXT("owning-client reach latches the other player's server body"), ServerGrab->GetGripTarget(true) == HostPawn)) return true;
			if (ServerGrab->GetGripRevision(true) != ClientGrab->GetGripRevision(true)) return false;
			GripRevision = ServerGrab->GetGripRevision(true);
			TargetStart = HostPawn->GetActorLocation();
			if (FApp::CanEverRender())
			{
				// A client-only observer camera makes both cats and the contacting paw visible; gameplay aim is unchanged.
				const FVector Center = (ClientPawn->GetActorLocation() + ObservedTarget->GetActorLocation()) * 0.5;
				ACameraActor* Camera = Client->SpawnActor<ACameraActor>();
				if (Camera)
				{
					const FVector Position = Center + FVector(-65, -100, 55);
					Camera->SetActorLocationAndRotation(Position, (Center - Position).Rotation());
					Camera->GetCameraComponent()->SetFieldOfView(65.0f);
					Local->SetViewTarget(Camera);
				}
			}
			Stage = 3;
			StageStarted = Now;
		}
		if (Stage == 3)
		{
			if (Now - StageStarted < 0.8) return false;
			if (!Test->TestTrue(TEXT("client movement RPC transfers physical force through the server grip"),
				HostPawn->GetActorLocation().X < TargetStart.X - 2.0)) return true;
			Test->AddInfo(FString::Printf(TEXT("Event=physics_prototype_network_force_verified TargetTravelCm=%.3f GripRevision=%u"),
				TargetStart.X - HostPawn->GetActorLocation().X, GripRevision));
			if (FApp::CanEverRender()) CaptureViewport(Client, TEXT("gripped-and-pulling"));
			ClientPawn->SetGrabInput(true, false);
			Stage = 4;
			StageStarted = Now;
		}
		if (Stage == 4)
		{
			if (ServerGrab->IsGripping(true) || ClientGrab->IsGripping(true) || ServerGrab->IsReaching(true) || ClientGrab->IsReaching(true)) return false;
			if (ClientGrab->GetGripRevision(true) <= GripRevision) return false;
			Test->TestNull(TEXT("server release clears its contact target"), ServerGrab->GetGripTarget(true));
			Test->TestNull(TEXT("owning client observes release with no residual target"), ClientGrab->GetGripTarget(true));
			ClientPawn->SetGrabInput(true, true);
			Stage = 5;
			StageStarted = Now;
		}
		if (Stage == 5)
		{
			if (!ServerGrab->IsGripping(true) || !ClientGrab->IsGripping(true)) return false;
			if (!Test->TestTrue(TEXT("second contact still points to the same server target"), ServerGrab->GetGripTarget(true) == HostPawn)) return true;
			GripRevision = ClientGrab->GetGripRevision(true);
			HostPawn->Destroy();
			Stage = 6;
			StageStarted = Now;
			return false;
		}
		if (Stage == 6)
		{
			if (ServerGrab->IsGripping(true) || ClientGrab->IsGripping(true) || ClientGrab->GetGripRevision(true) <= GripRevision) return false;
			Test->TestNull(TEXT("server clears a destroyed target"), ServerGrab->GetGripTarget(true));
			Test->TestNull(TEXT("client receives destroyed-target cleanup"), ClientGrab->GetGripTarget(true));
			ClientPawn->SetGrabInput(true, false);
			Test->AddInfo(TEXT("Event=physics_prototype_network_verified ListenPlayers=2 Result=RpcGripForceReleaseDestroy SnapshotInterpolation=1 Prediction=0"));
			return true;
		}
		return false;
	}
private:
	void CaptureViewport(UWorld* World, const TCHAR* Label)
	{
		UGameViewportClient* ViewportClient = World ? World->GetGameViewport() : nullptr;
		FViewport* Viewport = ViewportClient ? ViewportClient->Viewport : nullptr;
		if (!Test->TestNotNull(TEXT("rendering validation has its own client viewport"), Viewport)) return;
		TArray<FColor> Pixels;
		if (!Test->TestTrue(TEXT("captures the new prototype client viewport"), GetViewportScreenShot(Viewport, Pixels))) return;
		const FIntPoint Size = Viewport->GetSizeXY();
		if (!Test->TestTrue(TEXT("captured image dimensions match the viewport"), Size.X > 0 && Size.Y > 0 && Pixels.Num() == Size.X * Size.Y)) return;
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Size.X, Size.Y, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
		const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/PhysicsGrabPrototype/Images"));
		IFileManager::Get().MakeDirectory(*Directory, true);
		const FString File = Directory / FString::Printf(TEXT("%s-%s.png"), *FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")), Label);
		if (Test->TestTrue(TEXT("writes fresh prototype visual evidence"), FFileHelper::SaveArrayToFile(Png, *File)))
			Test->AddInfo(FString::Printf(TEXT("Event=physics_prototype_viewport_captured File=%s Width=%d Height=%d"), *File, Size.X, Size.Y));
	}
	static void Place(ACatPhysicsPrototypePawn* Pawn, FVector Location)
	{
		const FVector Delta = Location - Pawn->GetActorLocation();
		for (UPrimitiveComponent* Body : {static_cast<UPrimitiveComponent*>(Pawn->GetPhysicsBody()),
			static_cast<UPrimitiveComponent*>(Pawn->GetLeftHand()), static_cast<UPrimitiveComponent*>(Pawn->GetRightHand())})
		{
			Body->SetWorldLocation(Body->GetComponentLocation() + Delta, false, nullptr, ETeleportType::TeleportPhysics);
			Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
		}
	}
	FAutomationTestBase* Test;
	double Started;
	double StageStarted = 0.0;
	int32 Stage = 0;
	uint32 GripRevision = 0;
	FGuid TargetId;
	FVector TargetStart = FVector::ZeroVector;
};

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPhysicsPrototypeNetworkTest,
	"Catfishing.PhysicsGrabPrototype.Network.ListenClientGripForceReleaseAndTargetExit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatPhysicsPrototypeNetworkTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("requires its own idle validation editor"), GEditor && GEngine && !GEditor->PlayWorld)) return false;
	const auto Restore = MakeShared<CatPhysicsGrabNetwork::FRestore>();
	UWorld* Map = nullptr;
	if (FApp::CanEverRender())
	{
		const FString MapFile = FPaths::ProjectContentDir() / TEXT("Catfishing/Prototypes/PhysicsGrabPrototype.umap");
		if (!TestTrue(TEXT("rendering validation loads the generated prototype map; run CreateMap first"),
			FEditorFileUtils::LoadMap(MapFile, false, false))) return false;
		Map = GEditor->GetEditorWorldContext().World();
		if (!TestTrue(TEXT("generated map binds the independent native prototype GameMode"), Map
			&& Map->GetWorldSettings()->DefaultGameMode == ACatPhysicsPrototypeGameMode::StaticClass())) return false;
	}
	else Map = FAutomationEditorCommonUtils::CreateNewMap();
	if (!Map) return false;
	Map->bIsNameStableForNetworking = true;
	Map->GetWorldSettings()->DefaultGameMode = ACatPhysicsPrototypeGameMode::StaticClass();
	auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	Settings->SetPlayNetMode(PIE_ListenServer);
	Settings->SetPlayNumberOfClients(2);
	Settings->SetRunUnderOneProcess(true);
	for (auto& Driver : GEngine->NetDriverDefinitions)
		if (Driver.DefName == TEXT("GameNetDriver"))
			Driver.DriverClassName = Driver.DriverClassNameFallback = TEXT("/Script/OnlineSubsystemUtils.IpNetDriver");
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatPhysicsGrabNetwork::FVerify>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
	return true;
}

#endif
