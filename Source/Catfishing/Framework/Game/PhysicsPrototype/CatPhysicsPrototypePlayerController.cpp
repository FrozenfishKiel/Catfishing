#include "Framework/Game/PhysicsPrototype/CatPhysicsPrototypePlayerController.h"

#include "Character/Physics/CatPhysicsPrototypePawn.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/InputComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Game/PhysicsPrototype/CatPhysicsPrototypeGameMode.h"
#include "InputCoreTypes.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCatPhysicsPrototypeInput, Log, All);

ACatPhysicsPrototypePlayerController::ACatPhysicsPrototypePlayerController()
{
	bShowMouseCursor = false;
}

void ACatPhysicsPrototypePlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController()) return;
	SetInputMode(FInputModeGameOnly());
	SetShowMouseCursor(false);
	EnsureLocalLighting();
	UE_LOG(LogCatPhysicsPrototypeInput, Display,
		TEXT("Event=physics_prototype_input_ready Controller=%s World=%s NetMode=%d Authority=%d LocalRole=%d Result=NativeKeysBound"),
		*GetName(), *GetNameSafe(GetWorld()), int32(GetNetMode()), HasAuthority(), int32(GetLocalRole()));
}

void ACatPhysicsPrototypePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (!InputComponent) return;
	InputComponent->BindAxisKey(EKeys::MouseX, this, &ThisClass::LookYaw);
	InputComponent->BindAxisKey(EKeys::MouseY, this, &ThisClass::LookPitch);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ThisClass::BeginLeftGrab);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &ThisClass::EndLeftGrab);
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &ThisClass::BeginRightGrab);
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &ThisClass::EndRightGrab);
	InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &ThisClass::Jump);
	InputComponent->BindKey(EKeys::R, IE_Pressed, this, &ThisClass::Reset);
	InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &ThisClass::SwitchCat);
	InputComponent->BindKey(EKeys::F1, IE_Pressed, this, &ThisClass::ToggleDiagnostics);
}

void ACatPhysicsPrototypePlayerController::PlayerTick(const float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (!IsLocalController()) return;
	const bool bFocused = HasPrototypeInputFocus();
	if (!bFocused && bObservedInputFocus) FlushPressedKeys();
	bObservedInputFocus = bFocused;
	// Polling repairs a missed release event without emitting reliable RPCs every frame.
	SetObservedGrabInput(true, bFocused && IsInputKeyDown(EKeys::LeftMouseButton));
	SetObservedGrabInput(false, bFocused && IsInputKeyDown(EKeys::RightMouseButton));
	if (ACatPhysicsPrototypePawn* PrototypePawn = Cast<ACatPhysicsPrototypePawn>(GetPawn()))
	{
		const FVector2D Move = bFocused ? FVector2D(float(IsInputKeyDown(EKeys::W)) - float(IsInputKeyDown(EKeys::S)),
			float(IsInputKeyDown(EKeys::D)) - float(IsInputKeyDown(EKeys::A))) : FVector2D::ZeroVector;
		PrototypePawn->SetPrototypeInput(Move.GetClampedToMaxSize(1.0), GetControlRotation());
	}
}

void ACatPhysicsPrototypePlayerController::LookYaw(const float Value) { AddYawInput(Value * 0.65f); }
void ACatPhysicsPrototypePlayerController::LookPitch(const float Value) { AddPitchInput(-Value * 0.65f); }
void ACatPhysicsPrototypePlayerController::BeginLeftGrab() { SetObservedGrabInput(true, HasPrototypeInputFocus()); }
void ACatPhysicsPrototypePlayerController::EndLeftGrab() { SetObservedGrabInput(true, false); }
void ACatPhysicsPrototypePlayerController::BeginRightGrab() { SetObservedGrabInput(false, HasPrototypeInputFocus()); }
void ACatPhysicsPrototypePlayerController::EndRightGrab() { SetObservedGrabInput(false, false); }
void ACatPhysicsPrototypePlayerController::Jump() { if (auto* PrototypePawn = Cast<ACatPhysicsPrototypePawn>(GetPawn())) PrototypePawn->RequestJump(); }
void ACatPhysicsPrototypePlayerController::Reset()
{
	ReleasePrototypeInput(TEXT("ResetRequested"));
	if (auto* PrototypePawn = Cast<ACatPhysicsPrototypePawn>(GetPawn())) PrototypePawn->RequestReset();
}

void ACatPhysicsPrototypePlayerController::ToggleDiagnostics()
{
	if (ACatPhysicsPrototypePawn* PrototypePawn = Cast<ACatPhysicsPrototypePawn>(GetPawn()))
		PrototypePawn->TogglePrototypeDiagnostics();
}

bool ACatPhysicsPrototypePlayerController::HasPrototypeInputFocus() const
{
	const ULocalPlayer* Local = GetLocalPlayer();
	const FViewport* Viewport = Local && Local->ViewportClient ? Local->ViewportClient->GetGameViewport() : nullptr;
	return !Viewport || (Viewport->HasFocus() && Viewport->IsForegroundWindow());
}

void ACatPhysicsPrototypePlayerController::SetObservedGrabInput(const bool bLeft, const bool bHeld)
{
	ACatPhysicsPrototypePawn* PrototypePawn = Cast<ACatPhysicsPrototypePawn>(GetPawn());
	if (InputPawn.Get() != PrototypePawn)
	{
		InputPawn = PrototypePawn;
		bObservedGrabHeld[0] = bObservedGrabHeld[1] = false;
	}
	const int32 Index = bLeft ? 0 : 1;
	if (!PrototypePawn || bObservedGrabHeld[Index] == bHeld) return;
	bObservedGrabHeld[Index] = bHeld;
	PrototypePawn->SetGrabInput(bLeft, bHeld);
}

void ACatPhysicsPrototypePlayerController::ReleasePrototypeInput(const FName Reason)
{
	ACatPhysicsPrototypePawn* PrototypePawn = Cast<ACatPhysicsPrototypePawn>(GetPawn());
	const UCatPhysicsGrabComponent* Grab = PrototypePawn ? PrototypePawn->GetGrabComponent() : nullptr;
	const bool bHadGrab = bObservedGrabHeld[0] || bObservedGrabHeld[1]
		|| (Grab && (Grab->IsReaching(true) || Grab->IsReaching(false)));
	bObservedGrabHeld[0] = bObservedGrabHeld[1] = false;
	if (PrototypePawn)
	{
		PrototypePawn->SetPrototypeInput(FVector2D::ZeroVector, GetControlRotation());
		PrototypePawn->SetGrabInput(true, false);
		PrototypePawn->SetGrabInput(false, false);
	}
	if (bHadGrab)
	{
		UE_LOG(LogCatPhysicsPrototypeInput, Log,
			TEXT("Event=physics_prototype_input_released Controller=%s PrototypePawn=%s World=%s NetMode=%d Authority=%d LocalRole=%d Reason=%s Result=Released"),
			*GetName(), *GetNameSafe(PrototypePawn), *GetNameSafe(GetWorld()), int32(GetNetMode()), HasAuthority(), int32(GetLocalRole()), *Reason.ToString());
	}
}

void ACatPhysicsPrototypePlayerController::FlushPressedKeys()
{
	// GameViewport calls this on focus loss even when no subsequent PlayerTick or release event arrives.
	ReleasePrototypeInput(TEXT("PressedKeysFlushed"));
	Super::FlushPressedKeys();
}

void ACatPhysicsPrototypePlayerController::SwitchCat()
{
	ACatPhysicsPrototypeGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACatPhysicsPrototypeGameMode>() : nullptr;
	if (!GameMode || !GameMode->SwitchPrototypePawn(this))
	{
		UE_LOG(LogCatPhysicsPrototypeInput, Log,
			TEXT("Event=physics_prototype_switch_unavailable Controller=%s World=%s NetMode=%d Authority=%d LocalRole=%d Result=StandaloneOnly"),
			*GetName(), *GetNameSafe(GetWorld()), int32(GetNetMode()), HasAuthority(), int32(GetLocalRole()));
	}
}

void ACatPhysicsPrototypePlayerController::OnUnPossess()
{
	ReleasePrototypeInput(TEXT("UnPossessed"));
	InputPawn.Reset();
	Super::OnUnPossess();
}

void ACatPhysicsPrototypePlayerController::EnsureLocalLighting()
{
	UWorld* PrototypeWorld = GetWorld();
	if (!PrototypeWorld || PrototypeWorld->GetNetMode() == NM_DedicatedServer) return;
	// This is a World-level fallback. A second local controller sees the first set, and authored
	// directional lights take precedence over it. Listen/server and client Worlds each own their lights.
	for (TActorIterator<ADirectionalLight> It(PrototypeWorld); It; ++It) return;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FTransform LightTransform(FRotator(Index == 0 ? -55 : -35, Index == 0 ? -35 : 145, 0), FVector(0, 0, 300));
		ADirectionalLight* Light = PrototypeWorld->SpawnActorDeferred<ADirectionalLight>(
			ADirectionalLight::StaticClass(), LightTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (UDirectionalLightComponent* Component = Light ? Cast<UDirectionalLightComponent>(Light->GetLightComponent()) : nullptr)
		{
			Light->Tags.Add(TEXT("CatPhysicsPrototypeFallbackLighting"));
			// Set before registration so forward rendering never observes two equal-priority sun lights.
			Component->SetForwardShadingPriority(Index == 0 ? 1 : 0);
			Component->SetMobility(EComponentMobility::Movable);
			Component->SetIntensity(Index == 0 ? 4.0f : 1.5f);
			Component->SetCastShadows(Index == 0);
			Light->FinishSpawning(LightTransform);
		}
		else
		{
			if (Light) Light->Destroy();
			UE_LOG(LogCatPhysicsPrototypeInput, Warning,
				TEXT("Event=physics_prototype_lighting_failed World=%s NetMode=%d Authority=%d LocalRole=%d LightIndex=%d Result=DirectionalLightUnavailable"),
				*GetNameSafe(PrototypeWorld), int32(GetNetMode()), HasAuthority(), int32(GetLocalRole()), Index);
		}
	}
}
