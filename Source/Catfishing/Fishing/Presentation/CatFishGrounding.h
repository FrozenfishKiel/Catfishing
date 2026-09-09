#pragma once

#include "CoreMinimal.h"

/** 鱼体落地的纯表现支撑计算；接触点仍由权威地表查询给出。 */
namespace CatFishGrounding
{
	CATFISHING_API double ComputeVerticalLift(const FBox& LocalBounds, const FTransform& MeshWorldTransform,
		const FVector& ContactPoint, const FVector& SurfaceNormal);
}
