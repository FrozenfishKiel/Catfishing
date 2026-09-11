#pragma once

#include "CoreMinimal.h"
#include "CatCharacterRigSettings.generated.h"

class UAnimSequence;

/** Ordered, directly parented joints from shoulder/hip to the contact bone. */
USTRUCT(BlueprintType)
struct CATFISHING_API FCatCharacterLimbChain
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rig") TArray<FName> Bones;
};

/** Per-character presentation contract. Distances elsewhere remain unscaled mesh centimetres. */
USTRUCT(BlueprintType)
struct CATFISHING_API FCatCharacterRigSettings
{
	GENERATED_BODY()
	FCatCharacterRigSettings();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rig") FName RigId = TEXT("AnimaliaCat");
	/** Exactly four chains: front left, front right, rear left, rear right. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rig") TArray<FCatCharacterLimbChain> Feet;
	/** Common ancestor of all four limb roots; only its bounded translation is adjusted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rig") FName PelvisBone = TEXT("RigPelvis");
	/** Horizontal forward direction in the mesh's authored coordinate system. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rig") FVector ForwardAxis = FVector(0,1,0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rig") TArray<TSoftObjectPtr<UAnimSequence>> StandingAnimations;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rig") TArray<TSoftObjectPtr<UAnimSequence>> LocomotionAnimations;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rig") FName JumpStateMachine = TEXT("Main States");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rig") TArray<FName> JumpStates = {TEXT("Jump"), TEXT("Fall Loop"), TEXT("Land")};
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rig") FName JumpRootBone = TEXT("RigRoot");
};
