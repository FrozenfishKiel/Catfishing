#include "Online/Voice/CatProximityVoiceComponent.h"

#include "Online/Voice/CatVoiceSettings.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogCatVoice, Log, All);

UCatProximityVoiceComponent::UCatProximityVoiceComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UCatProximityVoiceComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer)
	{
		SetComponentTickEnabled(false);
		return;
	}
	if (APlayerState* State = Cast<APlayerState>(GetOwner()))
		State->OnPawnSet.AddUniqueDynamic(this, &ThisClass::HandlePawnSet);
	RefreshPlayerBinding();
}

void UCatProximityVoiceComponent::RefreshPlayerBinding()
{
	APlayerState* State = Cast<APlayerState>(GetOwner());
	if (bEndingPlay || !State || !GetWorld() || !GetWorld()->IsGameWorld() || GetNetMode() == NM_DedicatedServer) return;
	if (!SpatialSettings)
	{
		VoiceSource = NewObject<USceneComponent>(State, TEXT("ProximityVoiceSource"));
		State->AddInstanceComponent(VoiceSource);
		VoiceSource->RegisterComponent();
		Settings.ComponentToAttachTo = VoiceSource;
		SpatialSettings = NewObject<USoundAttenuation>(this);
		SpatialSettings->Attenuation.bSpatialize = true;
		SpatialSettings->Attenuation.bAttenuate = false;
		SpatialSettings->Attenuation.bAttenuateWithLPF = false;
		SpatialSettings->Attenuation.bEnableListenerFocus = false;
		SpatialSettings->Attenuation.bEnableOcclusion = false;
		SpatialSettings->Attenuation.bEnableReverbSend = false;
		Settings.AttenuationSettings = SpatialSettings;
		if (!GetDefault<UCatVoiceSettings>()->IsRangeValid())
			UE_LOG(LogCatVoice, Warning, TEXT("Event=voice_config_rejected World=%s NetMode=%d Authority=%d LocalRole=%d PlayerId=%d Player=%s Reason=InvalidDistanceRange"),
				*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), State->HasAuthority(), static_cast<int32>(State->GetLocalRole()), State->GetPlayerId(), *GetNameSafe(State));
	}
	const FUniqueNetIdRepl& Id = State->GetUniqueId();
	UVOIPTalker* Existing = Id.IsValid() ? UVOIPStatics::GetVOIPTalkerForPlayer(Id) : nullptr;
	if (Existing && Existing != this)
	{
		if (!bRegistrationConflict)
			UE_LOG(LogCatVoice, Warning, TEXT("Event=voice_registration_rejected World=%s NetMode=%d Authority=%d LocalRole=%d PlayerId=%d Player=%s Reason=IdentityAlreadyBound UseSeparateProcesses=true"),
				*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), State->HasAuthority(), static_cast<int32>(State->GetLocalRole()), State->GetPlayerId(), *GetNameSafe(State));
		bRegistrationConflict = true;
		MuteAndReleasePlayback();
		return;
	}
	if (RegisteredId != Id || (Id.IsValid() && !Existing))
	{
		MuteAndReleasePlayback();
		// 对无效 ID 也调用引擎入口：它会先注销旧 ID，再决定是否注册新 ID。
		RegisterWithPlayerState(State);
		RegisteredId = Id;
		LogEvent(TEXT("voice_player_registered"), Id.IsValid() ? TEXT("Ready") : TEXT("WaitingForIdentity"));
	}
	bRegistrationConflict = false;
	UpdatePlayback();
}

void UCatProximityVoiceComponent::HandlePawnSet(APlayerState* Player, APawn* NewPawn, APawn* OldPawn)
{
	UpdatePlayback();
}

void UCatProximityVoiceComponent::OnTalkingBegin(UAudioComponent* AudioComponent)
{
	if (bEndingPlay)
	{
		if (AudioComponent) AudioComponent->SetVolumeMultiplier(0.0f);
		return;
	}
	if (Playback.Get() != AudioComponent) MuteAndReleasePlayback();
	Playback = AudioComponent;
	bStreamActive = true;
	if (AudioComponent)
	{
		// 语音流在远处保持解码，靠近时同一句话可立即恢复；不使用 Stop/Play 作为距离门限。
		AudioComponent->SetVolumeMultiplier(0.0f);
		AudioComponent->bAllowSpatialization = true;
		if (SpatialSettings) AudioComponent->AdjustAttenuation(SpatialSettings->Attenuation);
	}
	UpdatePlayback();
	LogEvent(TEXT("voice_playback_begin"), TEXT("DecodedRemoteAudio"));
	Super::OnTalkingBegin(AudioComponent);
}

void UCatProximityVoiceComponent::OnTalkingEnd()
{
	// 引擎下次 ApplyVoiceSettings 会复用同一 AudioComponent；保留附着点，避免对已注册组件调用 SetupAttachment。
	bStreamActive = false;
	if (Playback.IsValid()) Playback->SetVolumeMultiplier(0.0f);
	bWasAudible = false;
	LogEvent(TEXT("voice_playback_end"), TEXT("StreamIdle"));
	Super::OnTalkingEnd();
}

void UCatProximityVoiceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// 旅行/重连时新旧 PlayerState 可能短暂重叠；旧接收器释放后自动重试，冲突期间只记录一次拒绝。
	if (bRegistrationConflict) RefreshPlayerBinding();
	else UpdatePlayback();
}

void UCatProximityVoiceComponent::UpdatePlayback()
{
	if (bEndingPlay || !GetWorld()) return;
	const APlayerState* State = Cast<APlayerState>(GetOwner());
	APawn* Speaker = State ? State->GetPawn() : nullptr;
	USceneComponent* Source = IsValid(Speaker) ? Speaker->GetRootComponent() : nullptr;
	if (Source && VoiceSource) VoiceSource->SetWorldLocation(Source->GetComponentLocation());
	if (BoundSource.Get() != Source)
	{
		BoundSource = Source;
		LogEvent(TEXT("voice_source_changed"), Source ? TEXT("PawnBound") : TEXT("MutedNoPawn"));
	}
	UAudioComponent* Audio = Playback.Get();
	if (!Audio) return;
	if (VoiceSource && Audio->GetAttachParent() != VoiceSource)
		Audio->AttachToComponent(VoiceSource, FAttachmentTransformRules::SnapToTargetNotIncludingScale);

	APawn* Listener = nullptr;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (PC && PC->IsLocalController())
		{
			Listener = PC->GetPawn();
			break;
		}
	}
	const float Gain = bStreamActive && RegisteredId.IsValid() && Source && IsValid(Listener) && Speaker != Listener && !bRegistrationConflict
		? GetDefault<UCatVoiceSettings>()->GetGainForDistance(FVector::Distance(Speaker->GetActorLocation(), Listener->GetActorLocation()))
		: 0.0f;
	Audio->SetVolumeMultiplier(Gain);
	if (bWasAudible != (Gain > 0.0f))
	{
		bWasAudible = Gain > 0.0f;
		LogEvent(TEXT("voice_audibility_changed"), bWasAudible ? TEXT("InRange") : TEXT("Muted"));
	}
}

void UCatProximityVoiceComponent::MuteAndReleasePlayback()
{
	if (UAudioComponent* Audio = Playback.Get())
	{
		Audio->SetVolumeMultiplier(0.0f);
	}
	Playback.Reset();
	bStreamActive = false;
	bWasAudible = false;
}

void UCatProximityVoiceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	MuteAndReleasePlayback();
	Settings.ComponentToAttachTo = nullptr;
	if (VoiceSource) VoiceSource->DestroyComponent();
	VoiceSource = nullptr;
	BoundSource.Reset();
	if (APlayerState* State = Cast<APlayerState>(GetOwner()))
		State->OnPawnSet.RemoveDynamic(this, &ThisClass::HandlePawnSet);
	LogEvent(TEXT("voice_player_released"), TEXT("PlaybackMuted"));
	Super::EndPlay(EndPlayReason);
}

void UCatProximityVoiceComponent::LogEvent(const TCHAR* Event, const TCHAR* Result) const
{
	const APlayerState* State = Cast<APlayerState>(GetOwner());
	UE_LOG(LogCatVoice, Log, TEXT("Event=%s World=%s NetMode=%d Authority=%d LocalRole=%d PlayerId=%d Player=%s Pawn=%s Gain=%.3f FullCm=%.1f SilentCm=%.1f Result=%s"),
		Event, *GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), GetOwner() && GetOwner()->HasAuthority(),
		GetOwner() ? static_cast<int32>(GetOwner()->GetLocalRole()) : 0, State ? State->GetPlayerId() : INDEX_NONE,
		*GetNameSafe(State), *GetNameSafe(State ? State->GetPawn() : nullptr), Playback.IsValid() ? Playback->VolumeMultiplier : 0.0f,
		GetDefault<UCatVoiceSettings>()->FullVolumeDistanceCm, GetDefault<UCatVoiceSettings>()->SilentDistanceCm, Result);
}
