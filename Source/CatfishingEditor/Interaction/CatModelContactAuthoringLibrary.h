#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CatModelContactAuthoringLibrary.generated.h"

/** Explicit editor migration; runtime consumes the saved PhysicsAsset without fitting geometry. */
UCLASS()
class CATFISHINGEDITOR_API UCatModelContactAuthoringLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Catfishing|Authoring|Interaction")
    static FString RefitCuteCatContacts(bool bSave = false);
};
