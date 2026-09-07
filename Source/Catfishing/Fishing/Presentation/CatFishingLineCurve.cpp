#include "Fishing/Presentation/CatFishingLineCurve.h"

bool FCatFishingLineCurve::BuildPoints(const FVector& Start, const FVector& End,
	const double PaidOutLengthCentimeters, const int32 NumSegments, TArray<FVector>& OutPoints)
{
	OutPoints.Reset();
	if (Start.ContainsNaN() || End.ContainsNaN() || !FMath::IsFinite(PaidOutLengthCentimeters)
		|| PaidOutLengthCentimeters < 0.0 || NumSegments < 4 || NumSegments > 256)
	{
		return false;
	}
	const FVector Delta = End - Start;
	const double DirectLength = Delta.Size();
	const double TargetLength = FMath::Max(DirectLength, PaidOutLengthCentimeters);
	if (!FMath::IsFinite(TargetLength)) return false;

	// 水平线使用向下的抛物线。接近垂直/重合时连续加入小幅侧向开口，避免重力方向与线重合而折返叠线。
	// 开口方向固定在世界 X，不随端点跨过竖直方向翻转；它是几何形状，不是外力或摆动。
	const double HorizontalRatio = DirectLength > UE_DOUBLE_SMALL_NUMBER ? Delta.Size2D() / DirectLength : 0.0;
	const double VerticalBlend = 1.0 - FMath::SmoothStep(0.0, 0.25, HorizontalRatio);
	TArray<FVector, TInlineAllocator<257>> Offsets;
	Offsets.SetNumUninitialized(NumSegments + 1);
	for (int32 Index = 0; Index <= NumSegments; ++Index)
	{
		const double T = static_cast<double>(Index) / NumSegments;
		Offsets[Index] = FVector(0.2 * VerticalBlend * FMath::Sin(2.0 * UE_DOUBLE_PI * T), 0.0, -4.0 * T * (1.0 - T));
	}
	Offsets[0] = Offsets.Last() = FVector::ZeroVector;
	const auto LengthAtSag = [&](const double Sag)
	{
		double Length = 0.0;
		FVector Previous = FVector::ZeroVector;
		for (int32 Index = 1; Index <= NumSegments; ++Index)
		{
			const FVector Point = Delta * (static_cast<double>(Index) / NumSegments) + Offsets[Index] * Sag;
			Length += FVector::Distance(Previous, Point);
			Previous = Point;
		}
		return Length;
	};

	double Sag = 0.0;
	if (TargetLength > DirectLength + UE_DOUBLE_KINDA_SMALL_NUMBER)
	{
		double Low = 0.0;
		double High = TargetLength;
		// 对实际渲染折线求弧长，余线不会被固定下垂比例吞掉；二分只解几何，不是 Cable 物理迭代。
		for (int32 Iteration = 0; Iteration < 24; ++Iteration)
		{
			const double Mid = Low + (High - Low) * 0.5;
			if (LengthAtSag(Mid) < TargetLength) Low = Mid;
			else High = Mid;
		}
		Sag = Low + (High - Low) * 0.5;
	}
	OutPoints.SetNumUninitialized(NumSegments + 1);
	for (int32 Index = 0; Index <= NumSegments; ++Index)
	{
		OutPoints[Index] = Start + Delta * (static_cast<double>(Index) / NumSegments) + Offsets[Index] * Sag;
	}
	OutPoints[0] = Start;
	OutPoints.Last() = End;
	return true;
}

double FCatFishingLineCurve::MeasureLength(const TArray<FVector>& Points)
{
	double Length = 0.0;
	for (int32 Index = 1; Index < Points.Num(); ++Index) Length += FVector::Distance(Points[Index - 1], Points[Index]);
	return Length;
}
