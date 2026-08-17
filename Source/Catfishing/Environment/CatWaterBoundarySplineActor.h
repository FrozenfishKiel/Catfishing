#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Environment/CatWaterGeometry.h"

#include "CatWaterBoundarySplineActor.generated.h"

class ACatWaterRegion;
class USplineComponent;

UCLASS(BlueprintType)
class CATFISHING_API ACatWaterBoundarySplineActor : public AActor
{
	GENERATED_BODY()

public:
	ACatWaterBoundarySplineActor();

	const USplineComponent* GetBoundarySpline() const;
	bool BuildPolygonInput(
		const FTransform& WorldToPlane,
		double MaxSampleSegmentLengthCm,
		double MaxChordErrorCm,
		FCatWaterPolygonBuildInput& OutInput,
		FString& OutError) const;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water|Boundary")
	FName BoundaryId = NAME_None;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water|Boundary")
	ECatWaterBoundaryOperation Operation = ECatWaterBoundaryOperation::Include;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water|Boundary")
	TObjectPtr<ACatWaterRegion> OwningRegion;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditMove(bool bFinished) override;
#endif

private:
	UPROPERTY(VisibleAnywhere, Category = "Water|Boundary")
	TObjectPtr<USplineComponent> BoundarySpline;
};
