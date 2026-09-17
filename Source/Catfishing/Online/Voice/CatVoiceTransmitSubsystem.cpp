#include "Online/Voice/CatVoiceTransmitSubsystem.h"

#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Interfaces/VoiceInterface.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "Settings/CatGameUserSettings.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCatVoiceTransmit, Log, All);

void UCatVoiceTransmitSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &ThisClass::HandleWorldCleanup);
}

void UCatVoiceTransmitSubsystem::Deinitialize()
{
	Suspend(TEXT("LocalPlayerRemoved"));
	bDeinitialized = true;
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
	Super::Deinitialize();
}

void UCatVoiceTransmitSubsystem::Configure(UWorld* World, const ECatVoiceInputMode Mode)
{
	Suspend(TEXT("PreferencesApplied"));
	ConfiguredWorld = World;
	ObservedWorld = World;
	ObservedController = GetLocalPlayer()->GetPlayerController(World);
	ConfiguredLocalUser = GetLocalPlayer()->GetControllerId();
	IOnlineSubsystem* OnlineSubsystem = World ? Online::GetSubsystem(World) : nullptr;
	ActiveVoice = OnlineSubsystem ? OnlineSubsystem->GetVoiceInterface() : nullptr;
	InputMode = Mode;
	bReady = World && World != DepartingWorld.Get();
	// 配置设备和注册 talker 可能有自动发送副作用，先强制停发，再由游戏输入上下文决定。
	Dispatch(false, TEXT("PreferencesApplied"), true);
	Reconcile(IsContextAllowed(), TEXT("PreferencesApplied"));
}

void UCatVoiceTransmitSubsystem::Suspend(const FName Reason)
{
	if (Reason == TEXT("Travel")) { DepartingWorld = ConfiguredWorld; }
	bHeld = false;
	bReady = false;
	Dispatch(false, Reason, true);
	ActiveVoice.Reset();
}

void UCatVoiceTransmitSubsystem::CancelHeldInput(const FName Reason)
{
	bHeld = false;
	Dispatch(false, Reason);
}

void UCatVoiceTransmitSubsystem::SetPushToTalkHeld(const bool bNewHeld)
{
	bHeld = bNewHeld && bReady && InputMode == ECatVoiceInputMode::PushToTalk && IsContextAllowed();
	Reconcile(IsContextAllowed(), bNewHeld ? TEXT("KeyPressed") : TEXT("KeyReleased"));
}

bool UCatVoiceTransmitSubsystem::IsContextAllowed() const
{
	const ULocalPlayer* Player = GetLocalPlayer();
	const UWorld* World = ConfiguredWorld.Get();
	const APlayerController* Controller = Player && World ? Player->GetPlayerController(World) : nullptr;
	UGameViewportClient* ViewportClient = Player ? Player->ViewportClient : nullptr;
	return Controller && Controller->IsLocalController() && Controller->GetWorld() == World
		&& !World->bIsTearingDown && !World->IsPaused() && Controller->GetPawn() && !Controller->IsMoveInputIgnored()
		&& ViewportClient && !ViewportClient->IgnoreInput() && ViewportClient->Viewport
		&& ViewportClient->Viewport->HasFocus() && FSlateApplication::IsInitialized()
		&& FSlateApplication::Get().IsActive();
}

void UCatVoiceTransmitSubsystem::Tick(float DeltaTime)
{
	ULocalPlayer* Player = GetLocalPlayer();
	UWorld* World = Player ? Player->GetWorld() : nullptr;
	APlayerController* Controller = Player && World ? Player->GetPlayerController(World) : nullptr;
	if (Controller != ObservedController.Get() || World != ObservedWorld.Get()
		|| (Player && ConfiguredLocalUser != Player->GetControllerId()))
	{
		Suspend(TEXT("ControllerOrWorldChanged"));
		ObservedController = Controller;
		ObservedWorld = World;
		const int32 User = Player ? Player->GetControllerId() : INDEX_NONE;
		ConfiguredLocalUser = User;
		if (Controller && Controller->IsLocalController() && World && !World->bIsTearingDown && User >= 0 && User <= MAX_uint8)
		{
			if (UCatGameUserSettings* Settings = UCatGameUserSettings::Get())
			{
				Settings->ApplyVoicePreferences(World, uint8(User), Settings->GetAudioInputDeviceId(), Settings->GetVoiceInputMode(), true);
			}
		}
	}
	Reconcile(IsContextAllowed(), TEXT("InputContext"));
}

void UCatVoiceTransmitSubsystem::Reconcile(const bool bContextAllowed, const FName Reason)
{
	if (!bContextAllowed) { bHeld = false; }
	const bool bWantSend = bReady && bContextAllowed && (InputMode == ECatVoiceInputMode::AlwaysOn
		|| (InputMode == ECatVoiceInputMode::PushToTalk && bHeld));
	Dispatch(bWantSend, Reason);
}

void UCatVoiceTransmitSubsystem::Dispatch(const bool bEnable, const FName Reason, const bool bForce)
{
	if (!bForce && bSending == bEnable) { return; }
	UWorld* World = ConfiguredWorld.Get();
	const int32 User = ConfiguredLocalUser;
	const IOnlineVoicePtr& Voice = ActiveVoice;
	const bool bAvailable = Voice.IsValid() && User >= 0 && User < Voice->GetNumLocalTalkers();
	if (bAvailable)
	{
		// 注册与设备选择只在配置事务中执行；按键启停保留 UE 的异步 Stop 尾部状态机。
		if (bEnable) { Voice->StartNetworkedVoice(uint8(User)); }
		else { Voice->StopNetworkedVoice(uint8(User)); Voice->ClearVoicePackets(); }
	}
	bSending = bAvailable && bEnable;
	if (bEnable && !bAvailable) { bReady = false; }
	if (World)
	{
		const APlayerController* Controller = GetLocalPlayer()->GetPlayerController(World);
		UE_LOG(LogCatVoiceTransmit, Log, TEXT("Event=voice_transmit_gate World=%s NetMode=%d Authority=%d LocalRole=%d LocalUser=%d Mode=%d Sending=%d Reason=%s Result=%s CaptureConfirmed=false"),
			*GetNameSafe(World), int32(World->GetNetMode()), Controller && Controller->HasAuthority(), Controller ? int32(Controller->GetLocalRole()) : -1, User, int32(InputMode), bSending, *Reason.ToString(), bAvailable ? TEXT("Dispatched") : TEXT("VoiceUnavailable"));
	}
}

void UCatVoiceTransmitSubsystem::HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	if (ConfiguredWorld.Get() == World) { Suspend(TEXT("WorldCleanup")); ConfiguredWorld.Reset(); }
}
