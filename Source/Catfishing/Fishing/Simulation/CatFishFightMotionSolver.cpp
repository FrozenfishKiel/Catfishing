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

FCatFishMotionSolveResult FCatFishFightMotionSolver::ResolveExhaustedWaterFallback(
	const FVector& CurrentFishPosition, const FVector& CandidateFishPosition,
	const double WaterSurfaceZ, const bool bIntentionalLandwardHaul)
{
	FCatFishMotionSolveResult Result;
	if (!IsFiniteMotionVector(CurrentFishPosition) || !IsFiniteMotionVector(CandidateFishPosition)
		|| !FMath::IsFinite(WaterSurfaceZ)) return Result;
	// 水域烘焙轮廓与真实干地可能存在间隙。这里只延续已经由线长约束求出的位移，
	// 不自动上岸、不生成 Pickup；纯甩杆则停留在当前水面位置，不倒退回轮廓内。
	Result.FishWorldPosition = bIntentionalLandwardHaul ? CandidateFishPosition : CurrentFishPosition;
	Result.FishWorldPosition.Z = WaterSurfaceZ;
	Result.bSucceeded = true;
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
	const double ExplicitCatHaulDistance = Input.ActualReelDistanceCentimeters
		+ FMath::Max(0.0, CarrierLandwardProgress);
	// 若本步主要是竿尖绕 Grip 扫动，先留在水里；旋转稳定后，真实收线/猫平移会自然重获资格。
	const bool bExplicitCatHaul = ExplicitCatHaulDistance > Input.MinimumProgressCentimeters
		&& ExplicitCatHaulDistance + Input.MinimumProgressCentimeters
			>= NonCarrierRodTipDisplacement.Size2D();
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
		|| Input.bReeling && Input.bSlacking
		|| !FMath::IsFinite(Input.CorrectionToleranceCentimeters)
		|| Input.CorrectionToleranceCentimeters < 0.0)
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

	Result.bShoreContact = FVector::DistSquared2D(Input.CandidateFishWorldPosition,
		Input.ResolvedWaterWorldPosition) > FMath::Square(Input.CorrectionToleranceCentimeters);
	FVector DesiredPosition;
	if (Result.bShoreContact && Input.bAllowBeaching)
	{
		// 猫端已经沿绷紧鱼线把鱼拉向岸上：保留越岸候选的 XY，Z 由调用方使用真实地面统一结算。
		// MaximumConstraintDistance 包含该候选点到竿尖的实际距离，所以即使竿尖快速移动、旧鱼点已经落在
		// 新约束球之外，也不再因为“起点必须在球内”的旧前提误判为求解失败。
		DesiredPosition = Input.CandidateFishWorldPosition;
		Result.bBeached = true;
	}
	else if (Result.bShoreContact)
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
