#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Environment/CatWaterGeometry.h"

#include "CatWaterRegion.generated.h"

class ACatWaterBoundarySplineActor;
class ACatWaterRegionPresentationActor;
class UCatWaterQuerySubsystem;
class UCatWaterRegionPresentationSubsystem;
struct FCatWaterRegionTestAccess;
class FDataValidationContext;
class FObjectPreSaveContext;

UCLASS(BlueprintType)
class CATFISHING_API ACatWaterRegion : public AActor
{
	GENERATED_BODY()

public:
	ACatWaterRegion();

	bool HasValidBakedGeometry() const;
	FCatWaterRegionHandle GetWaterRegionHandle() const;
	const FBox2D& GetBakedBoundsForQuery() const;

	// Temporary compatibility wrappers. They read only the authoritative baked cache.
	bool IsRuntimeConfigured() const;
	bool ContainsWorldPoint(const FVector& WorldPoint) const;
	FCatWaterRegionSnapshot MakeSnapshot() const;

	FCatAggregationResult ContributeAggregation(const FCatAggregationCommand& Command);
	ECatDomainCommandError ValidateAggregation(const FCatAggregationCommand& Command) const;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water|Authoring")
	FName RegionId = NAME_None;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water|Authoring")
	double WaterSurfaceZ = 0.0;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water|Authoring", meta = (ClampMin = "0"))
	double WaterPointVerticalToleranceCm = 0.0;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water|Authoring", meta = (ClampMin = "0"))
	double BankHeightToleranceCm = 0.0;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water|Authoring", meta = (ClampMin = "0"))
	double BoundaryToleranceCm = 2.0;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water|Authoring", meta = (ClampMin = "0"))
	double MaxLandingCorrectionCm = 0.0;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water|Authoring", meta = (ClampMin = "0"))
	double MinimumWaterInsetCm = 0.0;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water|Authoring", meta = (ClampMin = "0.1"))
	double MaxSampleSegmentLengthCm = 100.0;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water|Authoring", meta = (ClampMin = "0"))
	double MaxChordErrorCm = 5.0;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water|Authoring")
	TArray<TObjectPtr<ACatWaterBoundarySplineActor>> BoundaryActors;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water|Presentation")
	TSoftClassPtr<ACatWaterRegionPresentationActor> WaterPresentationClass;

	// Deprecated compatibility fields retained until the legacy consumers migrate.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water|Prototype", meta = (DeprecatedProperty))
	bool bEnablePrototypeBounds = false;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water|Prototype", meta = (DeprecatedProperty))
	FVector LocalCenterOffset = FVector::ZeroVector;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water|Prototype", meta = (DeprecatedProperty))
	FVector HalfExtent = FVector::ZeroVector;

	UPROPERTY(EditInstanceOnly, Category = "Water|Aggregation")
	bool bEnableAggregation = false;

	UPROPERTY(EditInstanceOnly, Category = "Water|Aggregation", meta = (ClampMin = "0"))
	double AggregationBudget = 0.0;

#if WITH_EDITOR
	UFUNCTION(CallInEditor, Category = "Water|Authoring")
	void BakeGeometry();

	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
	virtual void PreSave(FObjectPreSaveContext SaveContext) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditMove(bool bFinished) override;

	void InvalidateBakedGeometry();
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	friend class ACatWaterBoundarySplineActor;
	friend class UCatWaterQuerySubsystem;
	friend class UCatWaterRegionPresentationSubsystem;
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FCatWaterRegionTestAccess;
#endif

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Water|Authoring", meta = (AllowPrivateAccess = "true"))
	int64 GeometryRevision = 0;

	UPROPERTY()
	FCatWaterGeometryCache BakedGeometry;

	UPROPERTY()
	int64 BakedSourceDigest = 0;

#if WITH_DEV_AUTOMATION_TESTS
	bool bTrustInjectedGeometryForTests = false;
#endif

	UPROPERTY(Transient)
	TObjectPtr<ACatWaterRegionPresentationActor> SpawnedPresentation;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Water|Aggregation", meta = (AllowPrivateAccess = "true"))
	int64 AggregationRevision = 1;

	FCatChumVector ChumPool;
	TMap<FString, FCatAggregationResult> AggregationTerminalCache;

#if WITH_EDITOR
	bool BuildCurrentGeometryInput(FCatWaterGeometryBuildInput& OutInput, TArray<FString>& OutErrors) const;
#endif
};
