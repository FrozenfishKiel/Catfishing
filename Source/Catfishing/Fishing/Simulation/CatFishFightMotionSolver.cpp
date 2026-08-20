#include "Fishing/Simulation/CatFishFightMotionSolver.h"

namespace
{
	// 三个分量都是有限数才认为坐标合法，防止 NaN/Inf 污染后续几何计算。
	bool IsFiniteMotionVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	// 把点逐分量夹到包围盒范围内，得到该点在盒内（或盒面上）的最近投影。
	FVector ClampToBox(const FVector& Point, const FBox& Box)
	{
		return FVector(FMath::Clamp(Point.X, Box.Min.X, Box.Max.X),
			FMath::Clamp(Point.Y, Box.Min.Y, Box.Max.Y), FMath::Clamp(Point.Z, Box.Min.Z, Box.Max.Z));
	}
}

FCatFishMotionSolveResult FCatFishFightMotionSolver::Solve(const FCatFishMotionSolveInput& Input)
{
	// 默认 bSucceeded=false；输入的水域边界、坐标、最大线长任一非法都直接返回这个空结果。
	FCatFishMotionSolveResult Result;
	if (!Input.WaterBounds.IsValid || !IsFiniteMotionVector(Input.RodTipWorldPosition)
		|| !IsFiniteMotionVector(Input.ProposedFishWorldPosition)
		|| !FMath::IsFinite(Input.MaximumLineLengthCentimeters)
		|| Input.MaximumLineLengthCentimeters <= 0.0)
	{
		return Result;
	}

	// 第一步：把 Step() 算出的理想新位置夹到冻结的水域包围盒内（先处理"是否出界"）。
	FVector Projected = ClampToBox(Input.ProposedFishWorldPosition, Input.WaterBounds);
	const FVector FromRod = Projected - Input.RodTipWorldPosition;
	if (FromRod.SizeSquared() > FMath::Square(Input.MaximumLineLengthCentimeters))
	{
		// 夹完水域边界后仍然超出线长上限：沿竿尖→投影点方向截断到恰好等于线长，
		// 再重新夹一次水域边界（截断后的点理论上仍在盒内，这里是保险）。
		Projected = Input.RodTipWorldPosition + FromRod.GetSafeNormal() * Input.MaximumLineLengthCentimeters;
		Projected = ClampToBox(Projected, Input.WaterBounds);
	}

	// 最终校验：投影点必须真的落在水域盒内（含边界），且到竿尖的距离不超过线长上限（留 0.01 的浮点误差余量）。
	if (!Input.WaterBounds.IsInsideOrOn(Projected)
		|| FVector::DistSquared(Input.RodTipWorldPosition, Projected)
			> FMath::Square(Input.MaximumLineLengthCentimeters + 0.01))
	{
		// 两次夹取之后仍不满足约束，说明水域盒和线长上限本身互相矛盾（比如水域太窄），放弃本次求解。
		return Result;
	}

	Result.bSucceeded = true;
	Result.FishWorldPosition = Projected;
	return Result;
}
