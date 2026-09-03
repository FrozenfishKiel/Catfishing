#include "Items/World/CatWorldSurfaceResolver.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

FCatWorldSurfaceResult FCatWorldSurfaceResolver::ResolveHighestBlockingSurface(UWorld* World,
	const FVector& DesiredWorldPosition, const ECollisionChannel TraceChannel,
	const TArray<const AActor*>& IgnoredActors)
{
	FCatWorldSurfaceResult Result;
	if (!World || DesiredWorldPosition.ContainsNaN())
	{
		return Result;
	}

	// 不再以鱼当前水面高度为中心使用一个短探测窗；从上下各半个旧世界尺度贯穿查询，
	// 岸坡无论高于还是低于水面都能得到同一 XY 上的最高阻挡表面。
	constexpr double TraceHalfHeightCentimeters = UE_OLD_HALF_WORLD_MAX1;
	const FVector TraceStart(DesiredWorldPosition.X, DesiredWorldPosition.Y,
		DesiredWorldPosition.Z + TraceHalfHeightCentimeters);
	const FVector TraceEnd(DesiredWorldPosition.X, DesiredWorldPosition.Y,
		DesiredWorldPosition.Z - TraceHalfHeightCentimeters);
	if (TraceStart.ContainsNaN() || TraceEnd.ContainsNaN())
	{
		return Result;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CatWorldSurfaceResolve), true);
	for (const AActor* IgnoredActor : IgnoredActors)
	{
		if (IsValid(IgnoredActor))
		{
			QueryParams.AddIgnoredActor(IgnoredActor);
		}
	}
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, TraceChannel, QueryParams)
		|| !Hit.bBlockingHit || Hit.ImpactPoint.ContainsNaN() || Hit.ImpactNormal.ContainsNaN())
	{
		return Result;
	}

	Result.bSucceeded = true;
	Result.WorldPosition = FVector(DesiredWorldPosition.X, DesiredWorldPosition.Y, Hit.ImpactPoint.Z);
	Result.SurfaceNormal = Hit.ImpactNormal.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector::UpVector);
	Result.SurfaceActor = Hit.GetActor();
	return Result;
}
