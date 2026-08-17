#pragma once

#include "CoreMinimal.h"
#include "Environment/CatChumFieldTypes.h"
#include "Subsystems/WorldSubsystem.h"

#include "CatChumPlacementService.generated.h"

class APlayerController;

UCLASS()
class CATFISHING_API UCatChumPlacementService final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	FCatPlaceChumResult PlaceChum(APlayerController* RequestingController,
		const FCatPlaceChumCommand& Command);
};
