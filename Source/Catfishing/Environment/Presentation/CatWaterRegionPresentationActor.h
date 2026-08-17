#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Environment/CatWaterGeometry.h"

#include "CatWaterRegionPresentationActor.generated.h"

class USceneComponent;

USTRUCT(BlueprintType)
struct FCatWaterPresentationLoop
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FName BoundaryId = NAME_None;

	UPROPERTY(BlueprintReadOnly)
	ECatWaterBoundaryOperation Operation = ECatWaterBoundaryOperation::Include;

	UPROPERTY(BlueprintReadOnly)
	TArray<FVector> WorldPoints;
};

USTRUCT(BlueprintType)
struct FCatWaterPresentationSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FCatWaterRegionHandle WaterRegion;

	UPROPERTY(BlueprintReadOnly)
	double WaterSurfaceZ = 0.0;

	UPROPERTY(BlueprintReadOnly)
	FVector2D BoundsMin = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FVector2D BoundsMax = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	TArray<FCatWaterPresentationLoop> Loops;
};

UCLASS(BlueprintType)
class CATFISHING_API ACatWaterRegionPresentationActor : public AActor
{
	GENERATED_BODY()

public:
	ACatWaterRegionPresentationActor();

	void ApplyWaterGeometryPresentation(const FCatWaterGeometryCache& Cache);
	void SetWaterPreviewVisible(bool bVisible);

	UFUNCTION(BlueprintPure, Category = "Water|Presentation")
	FCatWaterPresentationSnapshot GetPresentationSnapshot() const { return PresentationSnapshot; }

	UFUNCTION(BlueprintPure, Category = "Water|Presentation")
	bool IsWaterPreviewVisible() const { return bPreviewVisible; }

	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Water|Presentation")
	void BP_ApplyWaterGeometryPresentation(const FCatWaterPresentationSnapshot& Snapshot);

	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Water|Presentation")
	void BP_SetWaterPreviewVisible(bool bVisible);

private:
	UPROPERTY(VisibleAnywhere, Category = "Water|Presentation")
	TObjectPtr<USceneComponent> VisualRoot;

	UPROPERTY(Transient)
	FCatWaterPresentationSnapshot PresentationSnapshot;

	UPROPERTY(Transient)
	bool bPreviewVisible = false;
};
