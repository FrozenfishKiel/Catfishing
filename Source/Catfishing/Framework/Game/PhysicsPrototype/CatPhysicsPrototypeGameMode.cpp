#include "Framework/Game/PhysicsPrototype/CatPhysicsPrototypeGameMode.h"

#include "Character/Physics/CatPhysicsPrototypePawn.h"
#include "Engine/World.h"
#include "Framework/Game/PhysicsPrototype/CatPhysicsPrototypeHUD.h"
#include "Framework/Game/PhysicsPrototype/CatPhysicsPrototypePlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Interaction/Grab/CatPhysicsGrabProp.h"

DEFINE_LOG_CATEGORY_STATIC(LogCatPhysicsPrototypeArena, Log, All);

ACatPhysicsPrototypeGameMode::ACatPhysicsPrototypeGameMode()
{
	DefaultPawnClass = ACatPhysicsPrototypePawn::StaticClass();
	PlayerControllerClass = ACatPhysicsPrototypePlayerController::StaticClass();
	HUDClass = ACatPhysicsPrototypeHUD::StaticClass();
}

void ACatPhysicsPrototypeGameMode::StartPlay()
{
	EnsurePrototypeArena();
	Super::StartPlay();
}

void ACatPhysicsPrototypeGameMode::EnsurePrototypeArena()
{
	if (!HasAuthority() || bArenaCreated || !GetWorld()) return;
	bArenaCreated = true;
	const auto SpawnProp = [this](const TCHAR* Name, const FVector& Position, const FVector& Size,
		const FLinearColor& Color, const bool bDynamic = false, const float Mass = 1.0f,
		const bool bRod = false, const FRotator Rotation = FRotator::ZeroRotator)
	{
		FActorSpawnParameters Params;
		Params.Name = FName(Name);
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACatPhysicsGrabProp* Prop = GetWorld()->SpawnActor<ACatPhysicsGrabProp>(Position, Rotation, Params);
		if (!Prop || !Prop->ConfigureFromAuthority(Size, bDynamic, Mass, Color, bRod))
		{
			UE_LOG(LogCatPhysicsPrototypeArena, Error,
				TEXT("Event=physics_prototype_arena_prop_failed Actor=%s World=%s NetMode=%d Authority=1 LocalRole=%d Result=SpawnFailed"),
				Name, *GetNameSafe(GetWorld()), int32(GetNetMode()), int32(GetLocalRole()));
		}
	};
	SpawnProp(TEXT("CatPhysicsPrototype_Floor"), FVector(100, 0, -5), FVector(1000, 800, 10), FLinearColor(0.38f, 0.48f, 0.49f));
	SpawnProp(TEXT("CatPhysicsPrototype_FarWall"), FVector(590, 0, 50), FVector(20, 800, 100), FLinearColor(0.25f, 0.39f, 0.44f));
	SpawnProp(TEXT("CatPhysicsPrototype_LeftWall"), FVector(100, -390, 35), FVector(1000, 20, 70), FLinearColor(0.30f, 0.42f, 0.44f));
	SpawnProp(TEXT("CatPhysicsPrototype_RightWall"), FVector(100, 390, 35), FVector(1000, 20, 70), FLinearColor(0.30f, 0.42f, 0.44f));
	SpawnProp(TEXT("CatPhysicsPrototype_BackWall"), FVector(-390, 0, 35), FVector(20, 800, 70), FLinearColor(0.30f, 0.42f, 0.44f));
	SpawnProp(TEXT("CatPhysicsPrototype_Step5"), FVector(95, -85, 2.5), FVector(30, 85, 5), FLinearColor(0.67f, 0.51f, 0.28f));
	SpawnProp(TEXT("CatPhysicsPrototype_Step12"), FVector(125, -85, 6), FVector(30, 85, 12), FLinearColor(0.72f, 0.57f, 0.32f));
	SpawnProp(TEXT("CatPhysicsPrototype_Platform25"), FVector(175, -85, 12.5), FVector(70, 85, 25), FLinearColor(0.80f, 0.63f, 0.36f));
	SpawnProp(TEXT("CatPhysicsPrototype_Platform45"), FVector(245, -85, 22.5), FVector(70, 85, 45), FLinearColor(0.84f, 0.68f, 0.40f));
	SpawnProp(TEXT("CatPhysicsPrototype_GrabWall"), FVector(180, 85, 32.5), FVector(12, 80, 65), FLinearColor(0.39f, 0.57f, 0.68f));
	SpawnProp(TEXT("CatPhysicsPrototype_GrabBeam"), FVector(180, 85, 67), FVector(24, 100, 4), FLinearColor(0.61f, 0.75f, 0.80f));
	SpawnProp(TEXT("CatPhysicsPrototype_Rod"), FVector(42, 0, 3), FVector(2, 2, 90), FLinearColor(0.89f, 0.63f, 0.24f), true, 0.45f, true, FRotator(90, 0, 0));
	SpawnProp(TEXT("CatPhysicsPrototype_Box"), FVector(65, 60, 11), FVector(18, 18, 18), FLinearColor(0.73f, 0.37f, 0.23f), true, 1.5f);
	SpawnProp(TEXT("CatPhysicsPrototype_HeavyBox"), FVector(100, 65, 14), FVector(24, 24, 24), FLinearColor(0.40f, 0.38f, 0.62f), true, 4.0f);
	SpawnPrototypePawn(0);
	SpawnPrototypePawn(1);
	UE_LOG(LogCatPhysicsPrototypeArena, Display,
		TEXT("Event=physics_prototype_arena_ready World=%s NetMode=%d Authority=1 LocalRole=%d PawnCount=%d Result=Ready"),
		*GetNameSafe(GetWorld()), int32(GetNetMode()), int32(GetLocalRole()), PrototypePawns.Num());
}

ACatPhysicsPrototypePawn* ACatPhysicsPrototypeGameMode::SpawnPrototypePawn(const int32 Index)
{
	FActorSpawnParameters Params;
	Params.Name = FName(*FString::Printf(TEXT("CatPhysicsPrototype_Cat%d"), Index + 1));
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Location = Index < 2 ? FVector(0, Index == 0 ? -20 : 20, 20)
		: FVector(-50.0 * (Index / 2), Index % 2 == 0 ? -20 : 20, 20);
	ACatPhysicsPrototypePawn* Pawn = GetWorld()->SpawnActor<ACatPhysicsPrototypePawn>(Location, FRotator::ZeroRotator, Params);
	if (Pawn) PrototypePawns.Add(Pawn);
	return Pawn;
}

void ACatPhysicsPrototypeGameMode::RestartPlayer(AController* NewPlayer)
{
	if (!NewPlayer || !HasAuthority()) return;
	EnsurePrototypeArena();
	if (NewPlayer->GetPawn()) return;
	ACatPhysicsPrototypePawn* Available = nullptr;
	for (ACatPhysicsPrototypePawn* Pawn : PrototypePawns)
	{
		if (IsValid(Pawn) && !Pawn->GetController()) { Available = Pawn; break; }
	}
	if (!Available) Available = SpawnPrototypePawn(PrototypePawns.Num());
	if (!Available)
	{
		FailedToRestartPlayer(NewPlayer);
		return;
	}
	NewPlayer->SetPawn(Available);
	FinishRestartPlayer(NewPlayer, FRotator(-15.0, 0.0, 0.0));
	// FinishRestartPlayer first sends the body's upright rotation to the owning client.
	// Send the intended camera pitch afterwards as well, preserving the physical body's orientation.
	NewPlayer->ClientSetRotation(FRotator(-15.0, 0.0, 0.0), true);
	UE_LOG(LogCatPhysicsPrototypeArena, Display,
		TEXT("Event=physics_prototype_player_joined World=%s NetMode=%d Authority=1 LocalRole=%d PlayerId=%d Pawn=%s Result=Possessed"),
		*GetNameSafe(GetWorld()), int32(GetNetMode()), int32(GetLocalRole()),
		NewPlayer->PlayerState ? NewPlayer->PlayerState->GetPlayerId() : INDEX_NONE, *GetNameSafe(Available));
}

bool ACatPhysicsPrototypeGameMode::SwitchPrototypePawn(APlayerController* Controller)
{
	if (!Controller || !HasAuthority() || GetNetMode() != NM_Standalone) return false;
	ACatPhysicsPrototypePawn* Current = Cast<ACatPhysicsPrototypePawn>(Controller->GetPawn());
	ACatPhysicsPrototypePawn* Next = nullptr;
	for (ACatPhysicsPrototypePawn* Pawn : PrototypePawns)
	{
		if (IsValid(Pawn) && Pawn != Current && !Pawn->GetController()) { Next = Pawn; break; }
	}
	if (!Next) return false;
	if (Current)
	{
		Current->SetPrototypeInput(FVector2D::ZeroVector, Controller->GetControlRotation());
		Current->SetGrabInput(true, false);
		Current->SetGrabInput(false, false);
	}
	Controller->Possess(Next);
	Controller->SetViewTargetWithBlend(Next, 0.15f);
	UE_LOG(LogCatPhysicsPrototypeArena, Log,
		TEXT("Event=physics_prototype_pawn_switched World=%s NetMode=%d Authority=1 LocalRole=%d From=%s To=%s Result=Possessed"),
		*GetNameSafe(GetWorld()), int32(GetNetMode()), int32(GetLocalRole()), *GetNameSafe(Current), *GetNameSafe(Next));
	return true;
}

void ACatPhysicsPrototypeGameMode::Logout(AController* Exiting)
{
	if (ACatPhysicsPrototypePawn* Pawn = Exiting ? Cast<ACatPhysicsPrototypePawn>(Exiting->GetPawn()) : nullptr)
	{
		Pawn->SetPrototypeInput(FVector2D::ZeroVector, Exiting->GetControlRotation());
		if (UCatPhysicsGrabComponent* Grab = Pawn->GetGrabComponent()) Grab->ReleaseAllFromAuthority(TEXT("PlayerLogout"));
		Exiting->UnPossess();
	}
	Super::Logout(Exiting);
}
