#pragma once

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"

namespace CatPhysicalTest
{
/** Real Chaos world. No manually integrated movement or synthetic grounded state. */
struct FScene
{
	FTestWorldWrapper World;
	AStaticMeshActor* Floor = nullptr;
	AStaticMeshActor* AddBox(const FVector& Position, const FVector& HalfExtents)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		const FTransform Transform(FRotator::ZeroRotator, Position, HalfExtents / 50.0);
		AStaticMeshActor* Box = World.GetTestWorld()->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform);
		if (!Cube || !Box) return nullptr;
		Box->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		if (!Box->GetStaticMeshComponent()->SetStaticMesh(Cube)) return nullptr;
		Box->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Box->FinishSpawning(Transform);
		Box->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
		return Box;
	}
	bool Initialize(FAutomationTestBase* Test)
	{
		if (!World.CreateTestWorld(EWorldType::Game)) return false;
		World.ForwardErrorMessages(Test);
		World.GetTestWorld()->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
		Floor = AddBox(FVector(0, 0, -10), FVector(2000, 2000, 10));
		return Floor && World.BeginPlayInTestWorld();
	}
	ACatCharacter* SpawnCat(const FVector& Position)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World.GetTestWorld()->SpawnActor<ACatCharacter>(Position, FRotator::ZeroRotator, Params);
	}
	void Step(int32 Frames, int32 Frequency = 60)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame) World.TickTestWorld(1.0f / Frequency);
	}
};
}
#endif
