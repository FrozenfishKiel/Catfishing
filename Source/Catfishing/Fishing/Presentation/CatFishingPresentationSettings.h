#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatFishingPresentationSettings.generated.h"

class ACatFishEncounterActor;
class ACatFishingHookActor;
class ACatFishingRodActor;
class UCatRodSkinDefinition;

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Catfishing Fishing Presentation"))
class CATFISHING_API UCatFishingPresentationSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	const UCatRodSkinDefinition* FindRuntimeRodSkin(FName RodSkinDefinitionId, FName RodDefinitionId) const;
	UPROPERTY(Config, EditAnywhere) TSoftClassPtr<ACatFishingRodActor> RodActorClass;
	UPROPERTY(Config, EditAnywhere) TSoftClassPtr<ACatFishingHookActor> HookActorClass;
	UPROPERTY(Config, EditAnywhere) TSoftClassPtr<ACatFishEncounterActor> FishEncounterActorClass;
	UPROPERTY(Config, EditAnywhere) TArray<TSoftObjectPtr<UCatRodSkinDefinition>> RodSkinCatalog;
};
