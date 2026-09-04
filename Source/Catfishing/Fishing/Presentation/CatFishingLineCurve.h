#pragma once

#include "CoreMinimal.h"

/** 只从端点与已放线长生成下垂曲线；不保存速度、不推进物理或写回权威状态。 */
struct CATFISHING_API FCatFishingLineCurve
{
	static bool BuildPoints(const FVector& Start, const FVector& End, double PaidOutLengthCentimeters,
		int32 NumSegments, TArray<FVector>& OutPoints);
	static double MeasureLength(const TArray<FVector>& Points);
};
