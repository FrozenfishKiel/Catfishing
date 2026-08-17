#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CatRodSkinDefinition.generated.h"

class UMaterialInterface;
class USkeletalMesh;
class UStaticMesh;

/** Cosmetic-only rod skin. Gameplay geometry always comes from the rod equipment definition. */
UCLASS(BlueprintType)
class CATFISHING_API UCatRodSkinDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	bool IsRuntimeDefinitionReady() const;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FName RodSkinDefinitionId = NAME_None;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) TSoftObjectPtr<USkeletalMesh> SkeletalMesh;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) TSoftObjectPtr<UStaticMesh> StaticMesh;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) TArray<TSoftObjectPtr<UMaterialInterface>> Materials;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FName VfxSetId = NAME_None;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FName SfxSetId = NAME_None;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FName AnimationSetId = NAME_None;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FTransform VisualRelativeTransform = FTransform::Identity;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) TMap<FName, FName> VisualSocketMap;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) TArray<FName> CompatibleRodDefinitionIds;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FName RequiredUnlockId = NAME_None;
};
