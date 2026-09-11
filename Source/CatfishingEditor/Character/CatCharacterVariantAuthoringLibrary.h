#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CatCharacterVariantAuthoringLibrary.generated.h"

/** Editor-only audit and migration of the shared character Blueprint and its skins. */
UCLASS()
class CATFISHINGEDITOR_API UCatCharacterVariantAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Derive four body-space reactions from the audited lateral hit, preserving the standing heading and bone lengths. */
	UFUNCTION(BlueprintCallable, Category="Catfishing|Authoring|Character")
	static FString CreateForceReactionSourceClips(bool bRebuildGeneratedClips = false);
	/** After retarget/normalization, create non-root-motion montages and wire the two existing character skins. */
	UFUNCTION(BlueprintCallable, Category="Catfishing|Authoring|Character")
	static FString FinalizeForceReactionAssets();
	UFUNCTION(BlueprintCallable, Category="Catfishing|Authoring|Character")
	static FString InspectBlueprint(const FString& AssetPath);
	UFUNCTION(BlueprintCallable, Category="Catfishing|Authoring|Character")
	static FString InspectSkeleton(const FString& AssetPath);
	/** One-time migration; refuses pre-existing outputs and only saves after every Blueprint compiles. */
	UFUNCTION(BlueprintCallable, Category="Catfishing|Authoring|Character")
	static FString CreateCharacterFamily();
	/** Restore imported bone units, migrate collapsed legacy exports, and make lean poses rotation-only. */
	UFUNCTION(BlueprintCallable, Category="Catfishing|Authoring|Character")
	static FString NormalizeCuteCatRetargetedAnimations();
	/** Migrate the audited rod presentation consumer to the shared native character playback API. */
	UFUNCTION(BlueprintCallable, Category="Catfishing|Authoring|Character")
	static FString MigrateRodCharacterConsumer();
};
