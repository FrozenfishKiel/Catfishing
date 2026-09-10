#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

class AActor;
class UWorld;

/** 世界物品沿任意高度落到同一 XY 上最高阻挡表面的权威查询结果。 */
struct CATFISHING_API FCatWorldSurfaceResult
{
	bool bSucceeded = false;
	FVector WorldPosition = FVector::ZeroVector;
	FVector SurfaceNormal = FVector::UpVector;
	TWeakObjectPtr<AActor> SurfaceActor;
};

/** 岸上鱼与其他世界物品共用的垂直地表解析；不保存玩法状态。 */
class CATFISHING_API FCatWorldSurfaceResolver
{
public:
	static FCatWorldSurfaceResult ResolveHighestBlockingSurface(UWorld* World,
		const FVector& DesiredWorldPosition, ECollisionChannel TraceChannel,
		const TArray<const AActor*>& IgnoredActors);
};
