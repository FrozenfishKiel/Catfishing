#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "UI/Voice/CatVoiceActivityWidget.h"
#include "Online/Voice/CatProximityVoiceComponent.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Components/AudioComponent.h"
#include "OnlineSubsystemTypes.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Serialization/BufferArchive.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatVoiceActivityWidgetTest, "Catfishing.Online.Voice.ActivityWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatVoiceActivityWidgetTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game)) { return false; }
	UWorld* World = Fixture.GetTestWorld();
	World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	if (!Fixture.BeginPlayInTestWorld()) { return false; }
	UGameInstance* Game = World->GetGameInstance();
	auto* Local = NewObject<ULocalPlayer>(GEngine);
	Game->AddLocalPlayer(Local, FPlatformUserId::CreateFromInternalId(0));
	auto* Viewport = NewObject<UGameViewportClient>(GEngine);
	Viewport->Init(*GEngine->GetWorldContextFromWorld(World), Game, false);
	Local->ViewportClient = Viewport;
	auto* Controller = World->SpawnActor<APlayerController>();
	Controller->SetPlayer(Local);
	auto SpawnPawn = [World]()
	{
		auto* Pawn = World->SpawnActor<APawn>();
		auto* Root = NewObject<USceneComponent>(Pawn);
		Pawn->AddInstanceComponent(Root); Pawn->SetRootComponent(Root); Root->RegisterComponent();
		return Pawn;
	};
	Controller->Possess(SpawnPawn());
	auto* Widget = CreateWidget<UCatVoiceActivityWidget>(Controller);
	if (!TestNotNull(TEXT("Production speaker widget"), Widget)) { return false; }
	const auto Slate = Widget->TakeWidget();
	TestEqual(TEXT("Overlay never captures input"), Widget->GetVisibility(), ESlateVisibility::HitTestInvisible);
	TestNotNull(TEXT("Cooked Chinese font reference"), Widget->SpeakerFont.FontObject.Get());
	TArray<ACatfishingPlayerState*> Players;
	TArray<UCatProximityVoiceComponent*> Voices;
	TArray<UAudioComponent*> Audios;
	for (const TCHAR* Name : {TEXT("湖边的小橘猫"), TEXT("Mochi"), TEXT("一个名字特别特别长的说话玩家测试换行")})
	{
		auto* Player = World->SpawnActor<ACatfishingPlayerState>();
		Player->SetPlayerName(Name);
		const FUniqueNetIdRef Id = FUniqueNetIdString::Create(FGuid::NewGuid().ToString(), TEXT("CAT_TEST"));
		Player->SetUniqueId(FUniqueNetIdRepl(Id));
		auto* Pawn = SpawnPawn(); Pawn->SetActorLocation(FVector(100,0,0)); Pawn->SetPlayerState(Player);
		auto* Voice = Player->FindComponentByClass<UCatProximityVoiceComponent>();
		auto* Audio = NewObject<UAudioComponent>(Player); Audio->RegisterComponent(); Voice->OnTalkingBegin(Audio);
		Players.Add(Player); Voices.Add(Voice); Audios.Add(Audio);
	}
	auto Count = [Widget]() { return Widget->Speakers.FilterByPredicate([](const auto& Row) { return Row.Envelope.bActive; }).Num(); };
	Widget->RefreshSpeakers(.05f);
	TestEqual(TEXT("Streams without sound do not show players"), Count(), 0);
	for (int32 I=0; I<Voices.Num(); ++I) { Voices[I]->OnAudioComponentEnvelopeValue(Audios[I], .25f + I * .1f); }
	Widget->RefreshSpeakers(.05f); Widget->RefreshSpeakers(.05f);
	TestEqual(TEXT("Real PlayerArray and talker envelopes feed all three rows"), Count(), 3);
	if (FApp::CanEverRender())
	{
		FWidgetRenderer Renderer(true);
		const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/VoiceUI");
		IFileManager::Get().MakeDirectory(*Directory, true);
		TArray<uint8> Previous;
		for (int32 Frame=0; Frame<2; ++Frame)
		{
			Widget->AnimationTime = Frame * .22f;
			auto* Target = Renderer.DrawWidget(Slate, FVector2D(1280,720));
			FBufferArchive PNG;
			if (TestNotNull(TEXT("Actual widget render target"), Target) && TestTrue(TEXT("Export speaker overlay"), FImageUtils::ExportRenderTarget2DAsPNG(Target, PNG)))
			{
				TestTrue(TEXT("Save rendered overlay"), FFileHelper::SaveArrayToFile(PNG, *(Directory / FString::Printf(TEXT("Speakers-%d.png"), Frame))));
				if (Frame) { TestTrue(TEXT("Speaker wave animation changes actual rendered pixels"), Previous != TArray<uint8>(PNG)); }
				Previous = PNG;
			}
		}
	}
	Voices[0]->OnAudioComponentEnvelopeValue(Audios[0], 0);
	for (int32 I=0; I<6; ++I) { Widget->RefreshSpeakers(.05f); }
	TestEqual(TEXT("Silence removes only the quiet player"), Count(), 2);
	Voices[1]->OnTalkingEnd(); Widget->RefreshSpeakers(.01f);
	TestEqual(TEXT("Stream end removes row immediately"), Count(), 1);
	Players[2]->GetPawn()->SetActorLocation(FVector(10000,0,0));
	Fixture.TickTestWorld(); Widget->RefreshSpeakers(.01f);
	TestEqual(TEXT("Out of audible range removes remaining row"), Count(), 0);
	Players[2]->GetPawn()->SetActorLocation(FVector(100,0,0));
	Fixture.TickTestWorld(); Widget->RefreshSpeakers(.05f); Widget->RefreshSpeakers(.05f);
	TestEqual(TEXT("Returning audible voice restores row"), Count(), 1);
	Players[2]->Destroy(); Widget->RefreshSpeakers(.01f);
	TestEqual(TEXT("Disconnected player removed"), Count(), 0);
	Widget->NativeDestruct();
	TestTrue(TEXT("Teardown clears all cached identities"), Widget->Speakers.IsEmpty());
	Game->RemoveLocalPlayer(Local); Local->ViewportClient = nullptr;
	return true;
}
#endif
