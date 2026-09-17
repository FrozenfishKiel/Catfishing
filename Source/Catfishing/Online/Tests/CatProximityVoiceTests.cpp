#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Online/Voice/CatVoiceSettings.h"
#include "Online/Voice/CatProximityVoiceComponent.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/WorldSettings.h"
#include "OnlineSubsystemTypes.h"
#include "OnlineSubsystemUtils.h"
#include "VoipListenerSynthComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatProximityVoiceRangeTest,
	"Catfishing.Online.Voice.LinearRange", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatProximityVoiceRangeTest::RunTest(const FString& Parameters)
{
	UCatVoiceSettings* Settings = NewObject<UCatVoiceSettings>();
	Settings->FullVolumeDistanceCm = 300.0f;
	Settings->SilentDistanceCm = 2000.0f;
	TestEqual(TEXT("Inside full-volume radius"), Settings->GetGainForDistance(100), 1.0f);
	TestEqual(TEXT("Full-volume boundary"), Settings->GetGainForDistance(300), 1.0f);
	TestEqual(TEXT("One quarter of falloff distance"), Settings->GetGainForDistance(725), 0.75f);
	TestEqual(TEXT("Linear midpoint"), Settings->GetGainForDistance(1150), 0.5f);
	TestEqual(TEXT("Silence boundary"), Settings->GetGainForDistance(2000), 0.0f);
	TestEqual(TEXT("Beyond silence boundary"), Settings->GetGainForDistance(3000), 0.0f);
	TestEqual(TEXT("Invalid distance fails closed"), Settings->GetGainForDistance(-1), 0.0f);
	Settings->SilentDistanceCm = Settings->FullVolumeDistanceCm;
	TestFalse(TEXT("Zero falloff range is invalid"), Settings->IsRangeValid());
	TestEqual(TEXT("Invalid range is silent even nearby"), Settings->GetGainForDistance(0), 0.0f);
	Settings->SilentDistanceCm = 100;
	TestFalse(TEXT("Reversed range is invalid"), Settings->IsRangeValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatProximityVoiceLifecycleTest,
	"Catfishing.Online.Voice.PlaybackLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatProximityVoiceLifecycleTest::RunTest(const FString& Parameters)
{
	// 使用真实 PlayerState、引擎 TalkerMap、Pawn 事件和播放回调；不假装这段合成回调证明了 Steam 麦克风传输。
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	TestWorld.ForwardErrorMessages(this);
	UWorld* World = TestWorld.GetTestWorld();
	World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	if (!TestWorld.BeginPlayInTestWorld()) return false;
	auto SpawnPawn = [World](const FVector& Location)
	{
		APawn* Pawn = World->SpawnActor<APawn>();
		USceneComponent* Root = NewObject<USceneComponent>(Pawn);
		Pawn->AddInstanceComponent(Root);
		Pawn->SetRootComponent(Root);
		Root->RegisterComponent();
		Pawn->SetActorLocation(Location);
		return Pawn;
	};
	APawn* Listener = SpawnPawn(FVector::ZeroVector);
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	Controller->SetAsLocalPlayerController();
	Controller->Possess(Listener);
	ACatfishingPlayerState* State = World->SpawnActor<ACatfishingPlayerState>();
	UCatProximityVoiceComponent* Voice = State->FindComponentByClass<UCatProximityVoiceComponent>();
	if (!TestNotNull(TEXT("Production PlayerState installs voice component"), Voice)) return false;
	TestFalse(TEXT("Waits for replicated identity"), Voice->RegisteredId.IsValid());
	const FUniqueNetIdRef UniqueId = FUniqueNetIdString::Create(FGuid::NewGuid().ToString(), TEXT("CAT_TEST"));
	const FUniqueNetIdRepl Id(UniqueId);
	State->SetUniqueId(Id);
	TestTrue(TEXT("Identity callback registers in actual engine map"), UVOIPStatics::GetVOIPTalkerForPlayer(Id) == Voice);
	UVoipListenerSynthComponent* Synth = NewObject<UVoipListenerSynthComponent>(State);
	Synth->RegisterComponent();
	ApplyVoiceSettings(Synth, Voice->Settings);
	UAudioComponent* Audio = Synth->GetAudioComponent();
	if (!TestNotNull(TEXT("Engine voice synth creates real playback component"), Audio)) return false;
	Voice->OnTalkingBegin(Audio);
	TestEqual(TEXT("Packets before Pawn arrives cannot play globally"), Audio->VolumeMultiplier, 0.0f);
	APawn* Speaker = SpawnPawn(FVector(100, 0, 0));
	Speaker->SetPlayerState(State);
	TestTrue(TEXT("Actual audio attaches to stable voice source"), Audio->GetAttachParent() == Voice->VoiceSource);
	TestEqual(TEXT("Pawn event positions voice source"), Voice->VoiceSource->GetComponentLocation(), Speaker->GetActorLocation());
	TestEqual(TEXT("Pawn arrival unmutes active stream"), Audio->VolumeMultiplier, 1.0f);
	TestEqual(TEXT("New stream has no stale activity"), Voice->GetAudibleVoiceLevel(), 0.0f);
	Voice->OnAudioComponentEnvelopeValue(Audio, 0.4f);
	TestEqual(TEXT("Actual talker envelope feeds activity"), Voice->GetAudibleVoiceLevel(), 0.4f);
	TestFalse(TEXT("Camera-distance attenuation disabled"), Audio->AttenuationOverrides.bAttenuate);
	TestTrue(TEXT("Voice remains spatialized"), Audio->AttenuationOverrides.bSpatialize);
	const UCatVoiceSettings* Settings = GetDefault<UCatVoiceSettings>();
	Speaker->SetActorLocation(FVector((Settings->FullVolumeDistanceCm + Settings->SilentDistanceCm) * 0.5f, 0, 0));
	TestWorld.TickTestWorld();
	TestEqual(TEXT("Moving mid-sentence applies linear half gain"), Audio->VolumeMultiplier, 0.5f);
	TestEqual(TEXT("Activity uses audible level after attenuation"), Voice->GetAudibleVoiceLevel(), 0.2f);
	AActor* Camera = World->SpawnActor<AActor>();
	Controller->SetViewTarget(Camera);
	TestWorld.TickTestWorld();
	TestEqual(TEXT("Changing view target does not affect voice gain"), Audio->VolumeMultiplier, 0.5f);
	Speaker->SetActorLocation(FVector(Settings->SilentDistanceCm + 1000, 0, 0));
	TestWorld.TickTestWorld();
	TestEqual(TEXT("Out of range is silent"), Audio->VolumeMultiplier, 0.0f);
	TestFalse(TEXT("Out of range cannot retain speaker row"), Voice->HasAudibleStream());
	TestEqual(TEXT("Out of range envelope is silent"), Voice->GetAudibleVoiceLevel(), 0.0f);
	Speaker->SetActorLocation(FVector(100, 0, 0));
	TestWorld.TickTestWorld();
	TestEqual(TEXT("Same stream resumes without another talking callback"), Audio->VolumeMultiplier, 1.0f);
	Controller->UnPossess();
	TestWorld.TickTestWorld();
	TestEqual(TEXT("No local body means no global spectator voice"), Audio->VolumeMultiplier, 0.0f);
	Controller->Possess(Listener);
	Speaker->SetPlayerState(nullptr);
	TestEqual(TEXT("Lost speaker immediately mutes"), Audio->VolumeMultiplier, 0.0f);
	TestTrue(TEXT("Lost speaker retains a safe reusable attachment"), Audio->GetAttachParent() == Voice->VoiceSource);
	APawn* Replacement = SpawnPawn(FVector(100, 0, 0));
	Replacement->SetPlayerState(State);
	TestEqual(TEXT("Replacement Pawn repositions stable source"), Voice->VoiceSource->GetComponentLocation(), Replacement->GetActorLocation());
	TestEqual(TEXT("Replacement Pawn restores same stream"), Audio->VolumeMultiplier, 1.0f);
	Voice->OnTalkingEnd();
	TestEqual(TEXT("Idle stream is muted"), Audio->VolumeMultiplier, 0.0f);
	TestEqual(TEXT("Stream end cannot reuse cached loudness"), Voice->GetAudibleVoiceLevel(), 0.0f);
	TestWorld.TickTestWorld();
	TestEqual(TEXT("Ticks do not unmute idle streams"), Audio->VolumeMultiplier, 0.0f);
	TestTrue(TEXT("Idle stream keeps reusable audio attached"), Audio->GetAttachParent() == Voice->VoiceSource);
	ApplyVoiceSettings(Synth, Voice->Settings);
	Voice->OnTalkingBegin(Audio);
	TestEqual(TEXT("Repeated engine playback resumes without attachment errors"), Audio->VolumeMultiplier, 1.0f);
	State->SetUniqueId(FUniqueNetIdRepl());
	TestNull(TEXT("Invalidated identity unregisters old mapping"), UVOIPStatics::GetVOIPTalkerForPlayer(Id));
	TestEqual(TEXT("Invalidated identity mutes active stream"), Audio->VolumeMultiplier, 0.0f);
	State->SetUniqueId(Id);
	ApplyVoiceSettings(Synth, Voice->Settings);
	Voice->OnTalkingBegin(Audio);
	ACatfishingPlayerState* RejoiningState = World->SpawnActor<ACatfishingPlayerState>();
	AddExpectedMessage(TEXT("Event=voice_registration_rejected"), EAutomationExpectedMessageFlags::Contains, 1);
	RejoiningState->SetUniqueId(Id);
	TestTrue(TEXT("Overlapping identity cannot steal active receiver"), UVOIPStatics::GetVOIPTalkerForPlayer(Id) == Voice);
	TestWorld.TickTestWorld();
	State->Destroy();
	TestNull(TEXT("Player exit unregisters immediately, before GC"), UVOIPStatics::GetVOIPTalkerForPlayer(Id));
	TestEqual(TEXT("Player exit mutes borrowed audio"), Audio->VolumeMultiplier, 0.0f);
	TestNull(TEXT("Player exit releases borrowed audio attachment"), Audio->GetAttachParent());
	TestWorld.TickTestWorld();
	TestTrue(TEXT("Replacement identity retries after old receiver exits"), UVOIPStatics::GetVOIPTalkerForPlayer(Id)
		== RejoiningState->FindComponentByClass<UCatProximityVoiceComponent>());
	TestWorld.DestroyTestWorld(true);
	TestNull(TEXT("World teardown and GC release replacement receiver"), UVOIPStatics::GetVOIPTalkerForPlayer(Id));
	return true;
}

#endif
