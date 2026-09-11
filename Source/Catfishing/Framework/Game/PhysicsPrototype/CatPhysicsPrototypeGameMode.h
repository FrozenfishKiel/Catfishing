#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CatPhysicsPrototypeGameMode.generated.h"

class ACatPhysicsPrototypePawn;

/** Opt-in physics trial host. It never starts the production Run, Fishing, inventory or save flow. */
UCLASS()
class CATFISHING_API ACatPhysicsPrototypeGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ACatPhysicsPrototypeGameMode();
	virtual void StartPlay() override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	void EnsurePrototypeArena();
	bool SwitchPrototypePawn(APlayerController* Controller);
	const TArray<TObjectPtr<ACatPhysicsPrototypePawn>>& GetPrototypePawns() const { return PrototypePawns; }

private:
	ACatPhysicsPrototypePawn* SpawnPrototypePawn(int32 Index);
	UPROPERTY() TArray<TObjectPtr<ACatPhysicsPrototypePawn>> PrototypePawns;
	bool bArenaCreated = false;
};
