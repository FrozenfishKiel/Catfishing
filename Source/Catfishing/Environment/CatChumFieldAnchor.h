#pragma once

#include "CoreMinimal.h"
#include "Environment/CatWaterTypes.h"
#include "GameFramework/Actor.h"

#include "CatChumFieldAnchor.generated.h"

UCLASS(BlueprintType)
class CATFISHING_API ACatChumFieldAnchor : public AActor
{
	GENERATED_BODY()

public:
	ACatChumFieldAnchor();

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Chum")
	FName AnchorId = NAME_None;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Chum")
	FCatWaterRegionHandle ExpectedWaterRegionHandle;
};
