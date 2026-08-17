#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "Environment/CatWaterTypes.h"
#include "UObject/SoftObjectPtr.h"

#include "CatChumFieldTypes.generated.h"

class AActor;
class UCurveFloat;

UENUM(BlueprintType)
enum class ECatChumFieldSource : uint8
{
	Player,
	NaturalEvent
};

UENUM(BlueprintType)
enum class ECatChumFieldError : uint8
{
	None,
	FeatureDisabled,
	CommandsClosed,
	InvalidIdentity,
	InvalidPayload,
	DefinitionUnavailable,
	InvalidWaterTarget,
	StaleGeometry,
	PlacementOutOfRange,
	PlacementOccluded,
	EquipmentRevisionConflict,
	EquipmentUnavailable,
	FieldCapacityExceeded,
	AlreadyResolved,
	DependencyUnavailable
};

struct FCatChumRuntimeInfluence;

USTRUCT(BlueprintType)
struct FCatChumInfluenceSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	double RadiusCentimeters = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	double DurationSeconds = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FCatChumVector BaseContribution;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<UCurveFloat> DistanceFalloffCurve;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<UCurveFloat> TimeFalloffCurve;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 MaximumQuantityPerPlacement = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName PresentationId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSoftClassPtr<AActor> PresentationClass;

	bool IsRuntimeReady() const;
	bool IsUnconfigured() const;
	bool BuildRuntimeInfluence(int32 Quantity, FCatChumRuntimeInfluence& OutRuntime) const;
};

struct FCatChumFalloffTable
{
	static constexpr int32 SampleCount = 65;
	TStaticArray<double, SampleCount> Samples{};

	double Evaluate(double NormalizedInput) const;
	bool IsRuntimeReady() const;
};

struct FCatChumRuntimeInfluence
{
	double RadiusCentimeters = 0.0;
	double DurationSeconds = 0.0;
	FCatChumVector BaseContribution;
	FCatChumFalloffTable DistanceFalloff;
	FCatChumFalloffTable TimeFalloff;
	FName PresentationId = NAME_None;
};

USTRUCT(BlueprintType)
struct FCatPlaceChumCommand
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	FGuid RequestId;

	UPROPERTY(BlueprintReadWrite)
	FCatWaterRegionHandle ExpectedWaterRegionHandle;

	UPROPERTY(BlueprintReadWrite)
	int64 ExpectedEquipmentRevision = 0;

	UPROPERTY(BlueprintReadWrite)
	FName ChumDefinitionId = NAME_None;

	UPROPERTY(BlueprintReadWrite)
	int32 Quantity = 0;

	UPROPERTY(BlueprintReadWrite)
	FVector ClientCandidateWorldPoint = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct FCatPlaceChumResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FGuid RequestId;

	UPROPERTY(BlueprintReadOnly)
	bool bCommitted = false;

	UPROPERTY(BlueprintReadOnly)
	ECatChumFieldError Error = ECatChumFieldError::DependencyUnavailable;

	UPROPERTY(BlueprintReadOnly)
	FGuid FieldId;

	UPROPERTY(BlueprintReadOnly)
	FCatWaterRegionHandle WaterRegion;

	UPROPERTY(BlueprintReadOnly)
	FVector ServerCorrectedCenter = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	double StartServerTime = 0.0;

	UPROPERTY(BlueprintReadOnly)
	double ExpireServerTime = 0.0;

	UPROPERTY(BlueprintReadOnly)
	int64 EquipmentRevision = 0;

	UPROPERTY(BlueprintReadOnly)
	int64 ChumFieldSetRevision = 0;
};

struct FCatChumFieldState
{
	FGuid FieldId;
	FCatWaterRegionHandle WaterRegion;
	FName ChumDefinitionId = NAME_None;
	FVector CenterWorldPoint = FVector::ZeroVector;
	FCatChumRuntimeInfluence Influence;
	double StartServerTime = 0.0;
	double ExpireServerTime = 0.0;
	ECatChumFieldSource Source = ECatChumFieldSource::Player;
	FString OwnerStableNetId;
	bool bPublicationFlushed = false;
};

USTRUCT(BlueprintType)
struct FCatChumSample
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	bool bSucceeded = false;

	UPROPERTY(BlueprintReadOnly)
	ECatChumFieldError Error = ECatChumFieldError::DependencyUnavailable;

	UPROPERTY(BlueprintReadOnly)
	FCatWaterRegionHandle WaterRegion;

	UPROPERTY(BlueprintReadOnly)
	int64 ChumFieldSetRevision = 0;

	UPROPERTY(BlueprintReadOnly)
	double SampleServerTime = 0.0;

	UPROPERTY(BlueprintReadOnly)
	FCatChumVector EffectiveChumVector;

	UPROPERTY(BlueprintReadOnly)
	int32 ContributingFieldCount = 0;

	TArray<FGuid> ContributingFieldIds;
};
