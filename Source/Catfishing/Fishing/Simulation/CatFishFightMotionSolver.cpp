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

FCatFishMotionSolveResult FCatFishFightMotionSolver::ProjectInitialFishToWater(const FCatFishMotionSolveInput& Input)
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

bool FCatFishFightMotionSolver::IsIntentionalLandwardHaul(
	const FCatFishBeachingIntentInput& Input)
{
	if (!Input.bLineTaut
		|| !IsFiniteMotionVector(Input.CurrentFishWorldPosition)
		|| !IsFiniteMotionVector(Input.CandidateFishWorldPosition)
		|| !IsFiniteMotionVector(Input.WaterwardDirection)
		|| Input.WaterwardDirection.IsNearlyZero()
		|| !IsFiniteMotionVector(Input.CarrierActualWorldDisplacement)
		|| !IsFiniteMotionVector(Input.NonCarrierRodTipWorldDisplacement)
		|| !FMath::IsFinite(Input.ActualReelDistanceCentimeters)
		|| Input.ActualReelDistanceCentimeters < 0.0
		|| !FMath::IsFinite(Input.ReelConstraintDistanceCentimeters)
		|| Input.ReelConstraintDistanceCentimeters < 0.0
		|| !FMath::IsFinite(Input.MinimumProgressCentimeters)
		|| Input.MinimumProgressCentimeters < 0.0)
	{
		return false;
	}

	FVector LandwardDirection = -Input.WaterwardDirection;
	LandwardDirection.Z = 0.0;
	LandwardDirection = LandwardDirection.GetSafeNormal();
	if (LandwardDirection.IsNearlyZero())
	{
		return false;
	}
	FVector FishDisplacement = Input.CandidateFishWorldPosition - Input.CurrentFishWorldPosition;
	FishDisplacement.Z = 0.0;
	FVector CarrierDisplacement = Input.CarrierActualWorldDisplacement;
	CarrierDisplacement.Z = 0.0;
	FVector NonCarrierRodTipDisplacement = Input.NonCarrierRodTipWorldDisplacement;
	NonCarrierRodTipDisplacement.Z = 0.0;
	const double FishLandwardProgress = FVector::DotProduct(FishDisplacement, LandwardDirection);
	const double CarrierLandwardProgress = FVector::DotProduct(CarrierDisplacement, LandwardDirection);
	const double ExplicitCatHaulDistance = FMath::Max(Input.ActualReelDistanceCentimeters,
		Input.ReelConstraintDistanceCentimeters)
		+ FMath::Max(0.0, CarrierLandwardProgress);
	// 只扣除向岸的竿尖扫动，横向/竖向调整不能把真实拖拽一并否掉。
	const double NonCarrierLandwardProgress = FMath::Max(0.0,
		FVector::DotProduct(NonCarrierRodTipDisplacement, LandwardDirection));
	const bool bExplicitCatHaul = ExplicitCatHaulDistance > Input.MinimumProgressCentimeters
		&& ExplicitCatHaulDistance + Input.MinimumProgressCentimeters
			>= NonCarrierLandwardProgress;
	return bExplicitCatHaul && FishLandwardProgress > Input.MinimumProgressCentimeters;
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
		|| Input.bReeling && Input.bSlacking)
	{
		return Result;
	}
	double ProposedLineLength = Input.ProposedLineLengthCentimeters;
	// 岸线层只做可恢复的空间校正。收线时最多保持旧长度，放线时至少保持旧长度，
	// 不因竿尖旋转造成的亚帧几何偏差终止整场会话。
	if (Input.bReeling)
	{
		ProposedLineLength = FMath::Min(ProposedLineLength, Input.PreviousLineLengthCentimeters);
	}
	else if (Input.bSlacking)
	{
		ProposedLineLength = FMath::Max(ProposedLineLength, Input.PreviousLineLengthCentimeters);
	}
	const double MaximumConstraintDistance = Input.MaximumConstraintDistanceCentimeters > 0.0
		? FMath::Max(ProposedLineLength,
			Input.MaximumConstraintDistanceCentimeters)
		: ProposedLineLength;

	// Boundary 容差带内也可能只返回最近岸点；不能用厘米级死区把慢速回水候选重新吸到岸线上。
	Result.bShoreContact = FVector::DistSquared2D(Input.CandidateFishWorldPosition,
		Input.ResolvedWaterWorldPosition) > FMath::Square(UE_DOUBLE_SMALL_NUMBER);
	FVector DesiredPosition;
	if (Result.bShoreContact)
	{
		// 连续拖行可能把活鱼留在烘焙轮廓外、真实岸面前。只删除候选向陆地的分量，
		// 保留鱼本步真实的入水进度；不能把它和最近岸点投影带来的大幅法向回弹一起删除。
		const FVector Waterward = FVector(Input.WaterwardDirection.X, Input.WaterwardDirection.Y, 0.0)
			.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector::ForwardVector);
		const FVector ResolvedDelta = Input.ResolvedWaterWorldPosition - Input.CurrentFishWorldPosition;
		const FVector HorizontalResolvedDelta(ResolvedDelta.X, ResolvedDelta.Y, 0.0);
		FVector TangentialDelta = HorizontalResolvedDelta
			- Waterward * FVector::DotProduct(HorizontalResolvedDelta, Waterward);
		const FVector ProposedDelta = Input.CandidateFishWorldPosition - Input.CurrentFishWorldPosition;
		const double WaterwardProgress = FMath::Max(0.0, FVector::DotProduct(ProposedDelta, Waterward));
		const FVector RecoveryDelta = (TangentialDelta + Waterward * WaterwardProgress)
			.GetClampedToMaxSize2D(ProposedDelta.Size2D());
		DesiredPosition = Input.CurrentFishWorldPosition + RecoveryDelta;
		DesiredPosition.Z = Input.ResolvedWaterWorldPosition.Z;
	}
	else
	{
		DesiredPosition = Input.ResolvedWaterWorldPosition;
	}

	if (ClampSegmentEndToSphere(Input.CurrentFishWorldPosition, DesiredPosition,
		Input.RodTipWorldPosition, MaximumConstraintDistance, Result.FishWorldPosition))
	{
		Result.LineLengthCentimeters = ProposedLineLength;
	}
	else if (Input.bReeling && ClampSegmentEndToSphere(Input.CurrentFishWorldPosition, DesiredPosition,
		Input.RodTipWorldPosition, FMath::Max(Input.PreviousLineLengthCentimeters,
			MaximumConstraintDistance), Result.FishWorldPosition))
	{
		// 岸线阻止鱼继续靠近时，本次收线只能收到实际直线距离；这是“收线未完全成功”，不是主动吐线。
		Result.LineLengthCentimeters = FMath::Max(ProposedLineLength,
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
