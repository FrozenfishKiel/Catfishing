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

	/** 本次打窝请求的幂等身份；输入组件生成，窝料场和 Equipment 都用它防止重放重复扣量或重复落点。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid RequestId;

	/** 客户端预测命中的水域版本；服务器用它复查落点是否仍在同一片有效水面上。 */
	UPROPERTY(BlueprintReadWrite)
	FCatWaterRegionHandle ExpectedWaterRegionHandle;

	/** 调用方看到的随身库存版本；Equipment Use 用它拒绝基于旧背包事实的扣量。 */
	UPROPERTY(BlueprintReadWrite)
	int64 ExpectedEquipmentRevision = 0;

	/** 玩家实际要消耗的窝料物品实例；PlaceChum 用它锁定背包中的那一格，避免同类多堆窝料被误扣。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid ChumItemInstanceId;

	/** 窝料定义身份；玩家投放时由服务器按 ChumItemInstanceId 复核和覆盖，自然事件直接写它来生成场地影响。 */
	UPROPERTY(BlueprintReadWrite)
	FName ChumDefinitionId = NAME_None;

	/** 本次投放消耗的窝料份数；Equipment Use 会从 ChumItemInstanceId 对应数量栈扣除这份数量。 */
	UPROPERTY(BlueprintReadWrite)
	int32 Quantity = 0;

	/** 客户端预测的候选落点；服务器只把它当输入重新吸附到水面，不直接信任最终坐标。 */
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
