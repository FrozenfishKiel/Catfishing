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

	/** 保留 Start→End 的运动方向，但把终点截在以 SphereCenter 为圆心的线长球内。 */
	bool ClampSegmentEndToSphere(const FVector& Start, const FVector& End, const FVector& SphereCenter,
		const double Radius, FVector& OutEnd)
	{
		if (!FMath::IsFinite(Radius) || Radius < 0.0)
		{
			return false;
		}
		if (FVector::Distance(End, SphereCenter) <= Radius + 0.01)
		{
			OutEnd = End;
			return true;
		}
		if (FVector::Distance(Start, SphereCenter) > Radius + 0.01)
		{
			return false;
		}

		const FVector Delta = End - Start;
		const double A = Delta.SizeSquared();
		if (A <= UE_DOUBLE_SMALL_NUMBER)
		{
			OutEnd = Start;
			return true;
		}
		const FVector StartFromCenter = Start - SphereCenter;
		const double B = 2.0 * FVector::DotProduct(StartFromCenter, Delta);
		const double C = StartFromCenter.SizeSquared() - FMath::Square(Radius);
		const double Discriminant = B * B - 4.0 * A * C;
		if (Discriminant < 0.0)
		{
			return false;
		}
		const double ExitTime = (-B + FMath::Sqrt(Discriminant)) / (2.0 * A);
		OutEnd = Start + Delta * FMath::Clamp(ExitTime, 0.0, 1.0);
		return IsFiniteMotionVector(OutEnd);
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

FCatFishShoreContactResult FCatFishFightMotionSolver::ResolveLiveFishShoreContact(
	const FCatFishShoreContactInput& Input)
{
	FCatFishShoreContactResult Result;
	if (!IsFiniteMotionVector(Input.CurrentFishWorldPosition)
		|| !IsFiniteMotionVector(Input.CandidateFishWorldPosition)
		|| !IsFiniteMotionVector(Input.ResolvedWaterWorldPosition)
		|| !IsFiniteMotionVector(Input.WaterwardDirection)
		|| Input.WaterwardDirection.IsNearlyZero()
		|| !IsFiniteMotionVector(Input.RodTipWorldPosition)
		|| !FMath::IsFinite(Input.PreviousLineLengthCentimeters)
		|| Input.PreviousLineLengthCentimeters < 0.0
		|| !FMath::IsFinite(Input.ProposedLineLengthCentimeters)
		|| Input.ProposedLineLengthCentimeters < 0.0
		|| !FMath::IsFinite(Input.MaximumConstraintDistanceCentimeters)
		|| Input.MaximumConstraintDistanceCentimeters < 0.0
		|| Input.ProposedLineLengthCentimeters > Input.PreviousLineLengthCentimeters + 0.01 && Input.bReeling
		|| Input.ProposedLineLengthCentimeters + 0.01 < Input.PreviousLineLengthCentimeters && Input.bSlacking
		|| Input.bReeling && Input.bSlacking
		|| !FMath::IsFinite(Input.CorrectionToleranceCentimeters)
		|| Input.CorrectionToleranceCentimeters < 0.0)
	{
		return Result;
	}
	const double MaximumConstraintDistance = Input.MaximumConstraintDistanceCentimeters > 0.0
		? FMath::Max(Input.ProposedLineLengthCentimeters,
			Input.MaximumConstraintDistanceCentimeters)
		: Input.ProposedLineLengthCentimeters;

	Result.bShoreContact = FVector::DistSquared2D(Input.CandidateFishWorldPosition,
		Input.ResolvedWaterWorldPosition) > FMath::Square(Input.CorrectionToleranceCentimeters);
	FVector DesiredPosition;
	if (Result.bShoreContact)
	{
		// 当前点是上一固定步已经通过真实水域校验的位置。把安全修正位移拆成“入水法向 + 沿岸切向”，
		// 丢掉可能很大的法向 MinimumWaterInset 回弹，只保留不超过本步原始位移的沿岸滑动。
		const FVector Waterward = FVector(Input.WaterwardDirection.X, Input.WaterwardDirection.Y, 0.0)
			.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector::ForwardVector);
		const FVector ResolvedDelta = Input.ResolvedWaterWorldPosition - Input.CurrentFishWorldPosition;
		const FVector HorizontalResolvedDelta(ResolvedDelta.X, ResolvedDelta.Y, 0.0);
		FVector TangentialDelta = HorizontalResolvedDelta
			- Waterward * FVector::DotProduct(HorizontalResolvedDelta, Waterward);
		const double ProposedStepDistance = FVector::Dist2D(Input.CurrentFishWorldPosition,
			Input.CandidateFishWorldPosition);
		if (TangentialDelta.Size2D() > ProposedStepDistance && ProposedStepDistance > 0.0)
		{
			TangentialDelta = TangentialDelta.GetSafeNormal2D() * ProposedStepDistance;
		}
		DesiredPosition = Input.CurrentFishWorldPosition + TangentialDelta;
		DesiredPosition.Z = Input.ResolvedWaterWorldPosition.Z;
	}
	else
	{
		DesiredPosition = Input.ResolvedWaterWorldPosition;
	}

	if (ClampSegmentEndToSphere(Input.CurrentFishWorldPosition, DesiredPosition,
		Input.RodTipWorldPosition, MaximumConstraintDistance, Result.FishWorldPosition))
	{
		Result.LineLengthCentimeters = Input.ProposedLineLengthCentimeters;
	}
	else if (Input.bReeling && ClampSegmentEndToSphere(Input.CurrentFishWorldPosition, DesiredPosition,
		Input.RodTipWorldPosition, FMath::Max(Input.PreviousLineLengthCentimeters,
			MaximumConstraintDistance), Result.FishWorldPosition))
	{
		// 岸线阻止鱼继续靠近时，本次收线只能收到实际直线距离；这是“收线未完全成功”，不是主动吐线。
		Result.LineLengthCentimeters = FMath::Max(Input.ProposedLineLengthCentimeters,
			FVector::Distance(Input.RodTipWorldPosition, Result.FishWorldPosition));
	}
	else
	{
		return FCatFishShoreContactResult{};
	}
	if (Input.bSlacking)
	{
		// Simulator 先用候选鱼距临时放宽线端，才能让鱼自由游动；真实水域校正可能随后把候选点挡在岸边。
		// 因此这里以服务器最终落点二次结算：鱼实际没有远离竿尖就绝不出线，已有余线也不会被收回。
		Result.LineLengthCentimeters = FMath::Max(Input.PreviousLineLengthCentimeters,
			FVector::Distance(Input.RodTipWorldPosition, Result.FishWorldPosition));
	}
	Result.bSucceeded = IsFiniteMotionVector(Result.FishWorldPosition)
		&& FMath::IsFinite(Result.LineLengthCentimeters)
		&& FVector::Distance(Input.RodTipWorldPosition, Result.FishWorldPosition)
			<= FMath::Max(Result.LineLengthCentimeters, MaximumConstraintDistance) + 0.01;
	return Result;
}
