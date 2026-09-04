#include "Fishing/Presentation/CatFishingLineCurveComponent.h"

#include "Fishing/Presentation/CatFishingLineCurve.h"

UCatFishingLineCurveComponent::UCatFishingLineCurveComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);
	SetCastShadow(false);
}

void UCatFishingLineCurveComponent::ClearCurve()
{
	CurveWorldPoints.Reset();
	if (GetNumSections() > 0) ClearAllMeshSections();
	LastPaidOutLength = LastWidth = -1.0;
}

double UCatFishingLineCurveComponent::GetCurveLengthCentimeters() const
{
	return FCatFishingLineCurve::MeasureLength(CurveWorldPoints);
}

bool UCatFishingLineCurveComponent::UpdateCurve(const FVector& Start, const FVector& End,
	const double PaidOutLengthCentimeters, const int32 NumSegments, const double WidthCentimeters)
{
	if (GetNetMode() == NM_DedicatedServer) { ClearCurve(); return true; }
	if (!FMath::IsFinite(WidthCentimeters) || WidthCentimeters <= 0.0)
	{
		ClearCurve();
		return false;
	}
	if (CurveWorldPoints.Num() == NumSegments + 1 && CurveWorldPoints[0].Equals(Start, 0.001)
		&& CurveWorldPoints.Last().Equals(End, 0.001) && FMath::IsNearlyEqual(LastPaidOutLength, PaidOutLengthCentimeters, 0.001)
		&& FMath::IsNearlyEqual(LastWidth, WidthCentimeters, 0.001)) return true;
	if (!FCatFishingLineCurve::BuildPoints(Start, End, PaidOutLengthCentimeters, NumSegments, CurveWorldPoints))
	{
		ClearCurve();
		return false;
	}
	if (FCatFishingLineCurve::MeasureLength(CurveWorldPoints) < UE_DOUBLE_KINDA_SMALL_NUMBER)
	{
		ClearCurve();
		return true;
	}
	// 网格以起点为原点，避免大世界坐标降低顶点精度；所有计算仍使用世界中的向下方向。
	SetWorldTransform(FTransform(Start));
	constexpr int32 Sides = 6;
	constexpr int32 RingSize = Sides + 1;
	const int32 VertexCount = CurveWorldPoints.Num() * RingSize;
	const bool bNewTopology = !GetProcMeshSection(0) || GetProcMeshSection(0)->ProcVertexBuffer.Num() != VertexCount;
	Vertices.SetNumUninitialized(VertexCount);
	Normals.SetNumUninitialized(VertexCount);
	UVs.SetNumUninitialized(VertexCount);
	Tangents.SetNumUninitialized(VertexCount);
	FVector PreviousNormal = FVector::ZeroVector;
	double AlongLine = 0.0;
	for (int32 PointIndex = 0; PointIndex < CurveWorldPoints.Num(); ++PointIndex)
	{
		const FVector Before = CurveWorldPoints[FMath::Max(0, PointIndex - 1)];
		const FVector After = CurveWorldPoints[FMath::Min(CurveWorldPoints.Num() - 1, PointIndex + 1)];
		FVector Tangent = (After - Before).GetSafeNormal();
		if (Tangent.IsNearlyZero()) Tangent = (After - CurveWorldPoints[PointIndex]).GetSafeNormal();
		if (Tangent.IsNearlyZero()) Tangent = FVector::ForwardVector;
		FVector Normal = PreviousNormal - FVector::DotProduct(PreviousNormal, Tangent) * Tangent;
		FVector Binormal;
		if (!Normal.Normalize()) Tangent.FindBestAxisVectors(Normal, Binormal);
		Binormal = FVector::CrossProduct(Tangent, Normal).GetSafeNormal();
		PreviousNormal = Normal;
		if (PointIndex > 0) AlongLine += FVector::Distance(CurveWorldPoints[PointIndex - 1], CurveWorldPoints[PointIndex]);
		for (int32 Side = 0; Side <= Sides; ++Side)
		{
			const double Angle = 2.0 * UE_DOUBLE_PI * Side / Sides;
			const FVector Radial = Normal * FMath::Cos(Angle) + Binormal * FMath::Sin(Angle);
			const int32 VertexIndex = PointIndex * RingSize + Side;
			Vertices[VertexIndex] = CurveWorldPoints[PointIndex] - Start + Radial * (WidthCentimeters * 0.5);
			Normals[VertexIndex] = Radial;
			UVs[VertexIndex] = FVector2D(AlongLine / 100.0, static_cast<double>(Side) / Sides);
			Tangents[VertexIndex] = FProcMeshTangent(Tangent, false);
		}
	}
	if (bNewTopology)
	{
		Triangles.Reset(NumSegments * Sides * 6);
		for (int32 Segment = 0; Segment < NumSegments; ++Segment)
		{
			for (int32 Side = 0; Side < Sides; ++Side)
			{
				const int32 A = Segment * RingSize + Side;
				const int32 B = A + RingSize;
				Triangles.Append({A, A + 1, B, A + 1, B + 1, B});
			}
		}
		CreateMeshSection(0, Vertices, Triangles, Normals, UVs, TArray<FColor>(), Tangents, false);
	}
	else UpdateMeshSection(0, Vertices, Normals, UVs, TArray<FColor>(), Tangents);
	LastPaidOutLength = PaidOutLengthCentimeters;
	LastWidth = WidthCentimeters;
	return true;
}
