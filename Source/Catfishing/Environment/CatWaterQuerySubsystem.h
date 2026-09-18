#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "Environment/CatWaterTypes.h"

#include "CatWaterQuerySubsystem.generated.h"

class ACatWaterRegion;

UCLASS()
class CATFISHING_API UCatWaterQuerySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 渔网成功捕获后登记局部禁生区；只影响后续新鱼，时长为秒、半径为厘米，服务器拒绝非法数值。 */
	bool AddFishSpawnSuppressionFromAuthority(const FVector& Center, const FCatWaterRegionHandle& Region, double Radius, double Duration);
	/** 查询该点此刻是否禁止产生新鱼，返回 true 表示受限；过期区在查询时回收，既有鱼与搏斗不受影响。 */
	bool IsFishSpawnSuppressed(const FVector& Point, const FCatWaterRegionHandle& Region) const;
	/** 掉落物空气墙：扫过水平投影的整个线段及包围半径；不受离水面高度限制，不跳过窄水域。 */
	bool DoesWorldDropSweepTouchWater(const FVector& Start, const FVector& End, double RadiusCentimeters) const;
	FCatWaterSpatialResult QueryWaterPoint(const FVector& WorldPoint, const FCatWaterRegionHandle& ExpectedHandle) const;
	FCatWaterSpatialResult QueryShoreRelation(const FVector& WorldPoint, const FCatWaterRegionHandle& ExpectedHandle) const;
	/** 查询指定水域内脚点的浸没深度；不使用岸高容差，也不裁决“危险”。 */
	FCatWaterImmersionResult QueryImmersionAtWorldPoint(const FVector& WorldPoint,
		const FCatWaterRegionHandle& ExpectedHandle) const;
	FCatWaterSpatialResult QueryNearestShoreForPreview(const FVector& WorldPoint, FName OptionalRegionId = NAME_None) const;
	FCatWaterSpatialResult ResolveRayToWater(const FVector& RayOrigin, const FVector& RayDirection,
		const FCatWaterRegionHandle& ExpectedHandle) const;
	FCatWaterSpatialResult ResolveCandidatePointToWater(const FVector& CandidateWorldPoint,
		const FCatWaterRegionHandle& ExpectedHandle) const;
	ECatWaterQueryError FindRegionById(FName RegionId, FCatWaterRegionHandle& OutHandle) const;

private:
	/** 一次渔网留下的空间与结束时刻；只保存禁生事实，不保存鱼或钓鱼计时器。 */
	struct FFishSpawnSuppression
	{
		/** 圆形区域中心的世界坐标；登记后不随玩家移动。 */
		FVector Center;
		/** 所属水域身份；相邻水域不受圆形投影误伤。 */
		FCatWaterRegionHandle Region;
		/** 水平半径，单位厘米；查询以二维距离判断。 */
		double Radius = 0.0;
		/** World 秒时钟上的到期点；查询以当前世界时间清理。 */
		double ExpiresAt = 0.0;
	};
	/** 当前世界的临时禁生区；随 World 销毁，无持久化或复制的第二份鱼状态。 */
	mutable TArray<FFishSpawnSuppression> FishSpawnSuppressions;
	friend class ACatWaterRegion;

	void RegisterRegion(ACatWaterRegion* Region);
	void UnregisterRegion(const ACatWaterRegion* Region);
	void CompactRegistry() const;
	ECatWaterQueryError ResolveExactRegion(const FCatWaterRegionHandle& ExpectedHandle,
		const ACatWaterRegion*& OutRegion) const;
	bool HasOverlappingWaterResult(const ACatWaterRegion* Target, const FVector& WorldPoint,
		double VerticalToleranceCm, bool bUseBankTolerance) const;

	mutable TMap<FName, TArray<TWeakObjectPtr<ACatWaterRegion>>> RegionsById;
};
