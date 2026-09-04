#include "Fishing/Presentation/CatFishGrounding.h"

double CatFishGrounding::ComputeVerticalLift(const FBox& LocalBounds, const FTransform& MeshWorldTransform,
	const FVector& ContactPoint, const FVector& SurfaceNormal)
{
	if (!LocalBounds.IsValid || MeshWorldTransform.ContainsNaN() || ContactPoint.ContainsNaN()) return 0.0;
	FVector Normal = SurfaceNormal.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector::UpVector);
	if (Normal.ContainsNaN() || Normal.Z < 0.05) Normal = FVector::UpVector;
	double LowestDistance = TNumericLimits<double>::Max();
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		const FVector LocalPoint((Corner & 1) ? LocalBounds.Max.X : LocalBounds.Min.X,
			(Corner & 2) ? LocalBounds.Max.Y : LocalBounds.Min.Y,
			(Corner & 4) ? LocalBounds.Max.Z : LocalBounds.Min.Z);
		LowestDistance = FMath::Min(LowestDistance,
			FVector::DotProduct(MeshWorldTransform.TransformPosition(LocalPoint) - ContactPoint, Normal));
	}
	// 保留 1cm 间隙防止深度闪烁；按世界 Z 托起，不能让 Actor 的 90° 侧翻把抬升转成横移。
	return FMath::Max(0.0, (1.0 - LowestDistance) / Normal.Z);
}
