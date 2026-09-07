#pragma once

#include "CoreMinimal.h"
#include "CatWaterTypes.generated.h"

/** WaterQuery 的只读拒绝语义；本模块不启用 UE Experimental Water，也不写回 Run。 */
UENUM(BlueprintType)
enum class ECatWaterQueryError : uint8
{
	/** 找到唯一合法水域并返回快照。 */
	None,
	/** Run 快照不允许当前阶段钓鱼。 */
	FishingClosed,
	/** 查询位置不是有限数值。 */
	InvalidLocation,
	/** 查询方向不是有限的单位向量。 */
	InvalidDirection,
	/** 没有配置完整且包含该位置的水域。 */
	RegionNotFound,
	/** 多个水域同时命中；几何优先级未裁时拒绝猜测。 */
	AmbiguousRegion,
	/** 查询所依据的 Run Revision 已陈旧。 */
	RevisionConflict,
	/** 查询所依据的几何 Revision 已陈旧。 */
	StaleGeometry,
	/** 水域几何未通过规范化或拓扑校验。 */
	InvalidGeometry,
	/** 查询点与水面平面的垂直距离超出调用方容差。 */
	HeightOutOfTolerance
};

/** 单个样条轮廓对 WaterRegion 的布尔贡献。 */
UENUM(BlueprintType)
enum class ECatWaterBoundaryOperation : uint8
{
	Include,
	Exclude
};

/** 点相对最终二维水域的闭集分类。 */
UENUM(BlueprintType)
enum class ECatWaterContainment : uint8
{
	Outside,
	Boundary,
	Inside
};

/** 最近岸线所属的轮廓域。 */
UENUM(BlueprintType)
enum class ECatWaterShoreKind : uint8
{
	None,
	OuterBoundary,
	ExcludedBoundary
};

/** 稳定 WaterRegion ID 与其不可变几何版本。 */
USTRUCT(BlueprintType)
struct FCatWaterRegionHandle
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FName RegionId = NAME_None;

	UPROPERTY(BlueprintReadOnly)
	int64 GeometryRevision = 0;

	bool IsValid() const
	{
		return !RegionId.IsNone() && GeometryRevision > 0;
	}

	bool operator==(const FCatWaterRegionHandle& Other) const
	{
		return RegionId == Other.RegionId && GeometryRevision == Other.GeometryRevision;
	}
};
/** 纯几何查询返回的只读空间事实。 */
USTRUCT(BlueprintType)
struct FCatWaterSpatialResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	bool bSucceeded = false;

	UPROPERTY(BlueprintReadOnly)
	ECatWaterQueryError Error = ECatWaterQueryError::RegionNotFound;

	UPROPERTY(BlueprintReadOnly)
	FCatWaterRegionHandle WaterRegion;

	UPROPERTY(BlueprintReadOnly)
	ECatWaterContainment Containment = ECatWaterContainment::Outside;

	UPROPERTY(BlueprintReadOnly)
	ECatWaterShoreKind NearestShoreKind = ECatWaterShoreKind::None;

	UPROPERTY(BlueprintReadOnly)
	double SignedDistanceToShoreCm = 0.0;

	UPROPERTY(BlueprintReadOnly)
	FVector NearestShoreWorldPoint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FVector WaterwardDirection = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FVector WaterSurfaceWorldPoint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FVector WaterSurfaceNormal = FVector::UpVector;

	UPROPERTY(BlueprintReadOnly)
	double VerticalDeltaCm = 0.0;
};

/** 指定水域下的脚点浸没事实；深度为正表示脚点位于水面下，不包含玩法危险阈值。 */
USTRUCT(BlueprintType)
struct FCatWaterImmersionResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	bool bSucceeded = false;
	UPROPERTY(BlueprintReadOnly)
	ECatWaterQueryError Error = ECatWaterQueryError::RegionNotFound;
	UPROPERTY(BlueprintReadOnly)
	FCatWaterRegionHandle WaterRegion;
	UPROPERTY(BlueprintReadOnly)
	ECatWaterContainment Containment = ECatWaterContainment::Outside;
	UPROPERTY(BlueprintReadOnly)
	FVector WaterSurfaceWorldPoint = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly)
	double ImmersionDepthCentimeters = 0.0;
};

/** 同一水域共享窝料池的三轴向量；腥、香、酵只表达输入维度，不包含吸引公式。 */
USTRUCT(BlueprintType)
struct FCatChumVector
{
	GENERATED_BODY()

	/** 腥味贡献；非有限或负值无效。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	double Fishy = 0.0;

	/** 香味贡献；非有限或负值无效。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	double Fragrant = 0.0;

	/** 发酵味贡献；非有限或负值无效。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	double Fermented = 0.0;

	/** 校验共享聚鱼三轴能否作为服务器环境事实；负值、非有限值和全零均拒绝，鱼种偏好仍由数据表而非本 DTO 推导。 */
	bool IsValidContribution() const
	{
		return FMath::IsFinite(Fishy) && FMath::IsFinite(Fragrant) && FMath::IsFinite(Fermented)
			&& Fishy >= 0.0 && Fragrant >= 0.0 && Fermented >= 0.0
			&& (Fishy > 0.0 || Fragrant > 0.0 || Fermented > 0.0);
	}

	FCatChumVector ScaledBy(const double Scale) const
	{
		FCatChumVector Result;
		if (!FMath::IsFinite(Scale) || Scale < 0.0 || !FMath::IsFinite(Fishy) || !FMath::IsFinite(Fragrant)
			|| !FMath::IsFinite(Fermented) || Fishy < 0.0 || Fragrant < 0.0 || Fermented < 0.0)
		{
			return Result;
		}
		Result.Fishy = Fishy * Scale;
		Result.Fragrant = Fragrant * Scale;
		Result.Fermented = Fermented * Scale;
		if (!FMath::IsFinite(Result.Fishy) || !FMath::IsFinite(Result.Fragrant) || !FMath::IsFinite(Result.Fermented))
		{
			return FCatChumVector();
		}
		return Result;
	}

	void Accumulate(const FCatChumVector& Other)
	{
		if (!FMath::IsFinite(Fishy) || !FMath::IsFinite(Fragrant) || !FMath::IsFinite(Fermented)
			|| !FMath::IsFinite(Other.Fishy) || !FMath::IsFinite(Other.Fragrant) || !FMath::IsFinite(Other.Fermented)
			|| Other.Fishy < 0.0 || Other.Fragrant < 0.0 || Other.Fermented < 0.0)
		{
			return;
		}
		const double NewFishy = Fishy + Other.Fishy;
		const double NewFragrant = Fragrant + Other.Fragrant;
		const double NewFermented = Fermented + Other.Fermented;
		if (FMath::IsFinite(NewFishy) && FMath::IsFinite(NewFragrant) && FMath::IsFinite(NewFermented))
		{
			Fishy = NewFishy;
			Fragrant = NewFragrant;
			Fermented = NewFermented;
		}
	}
};

