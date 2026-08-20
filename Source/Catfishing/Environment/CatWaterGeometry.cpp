#include "Environment/CatWaterGeometry.h"

#include "Algo/Sort.h"
#include "Hash/xxhash.h"

namespace CatWaterGeometryPrivate
{
	constexpr double QuantizationScale = 10.0;
	constexpr double GeometryEpsilon = 1.0e-9;

	enum class EPointPolygonRelation : uint8
	{
		Outside,
		Boundary,
		Inside
	};

	struct FCanonicalBoundary
	{
		FName BoundaryId;
		FString NormalizedBoundaryId;
		ECatWaterBoundaryOperation Operation = ECatWaterBoundaryOperation::Include;
		TArray<FVector2D> Vertices;
	};

	static FString NormalizeName(const FName Name)
	{
		FString Result = Name.ToString();
		for (TCHAR& Character : Result)
		{
			Character = FChar::ToLower(Character);
		}
		return Result;
	}

	static bool IsFiniteVector2D(const FVector2D& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y);
	}

	static bool IsFiniteVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	static bool IsQuantizable(const double Value)
	{
		return FMath::IsFinite(Value)
			&& FMath::Abs(Value) <= static_cast<double>(MAX_int64) / QuantizationScale - 1.0;
	}

	static int64 Quantize(const double Value)
	{
		return FMath::RoundToInt64(Value * QuantizationScale);
	}

	static double Dequantize(const int64 Value)
	{
		return static_cast<double>(Value) / QuantizationScale;
	}

	static FVector2D QuantizePoint(const FVector2D& Point)
	{
		return FVector2D(Dequantize(Quantize(Point.X)), Dequantize(Quantize(Point.Y)));
	}

	static double SignedDoubleArea(const TArray<FVector2D>& Vertices)
	{
		double Area = 0.0;
		for (int32 Index = 0; Index < Vertices.Num(); ++Index)
		{
			const FVector2D& A = Vertices[Index];
			const FVector2D& B = Vertices[(Index + 1) % Vertices.Num()];
			Area += A.X * B.Y - A.Y * B.X;
		}
		return Area;
	}

	static void RotateToStableStart(TArray<FVector2D>& Vertices)
	{
		int32 BestIndex = 0;
		for (int32 Index = 1; Index < Vertices.Num(); ++Index)
		{
			const FVector2D& Candidate = Vertices[Index];
			const FVector2D& Best = Vertices[BestIndex];
			if (Candidate.X < Best.X || (Candidate.X == Best.X && Candidate.Y < Best.Y))
			{
				BestIndex = Index;
			}
		}
		if (BestIndex == 0)
		{
			return;
		}

		TArray<FVector2D> Rotated;
		Rotated.Reserve(Vertices.Num());
		for (int32 Offset = 0; Offset < Vertices.Num(); ++Offset)
		{
			Rotated.Add(Vertices[(BestIndex + Offset) % Vertices.Num()]);
		}
		Vertices = MoveTemp(Rotated);
	}

	static bool CanonicalizeBoundaries(
		const TArray<FCatWaterPolygonBuildInput>& Inputs,
		TArray<FCanonicalBoundary>& OutBoundaries,
		TArray<FString>* OutErrors)
	{
		bool bValid = true;
		for (const FCatWaterPolygonBuildInput& Input : Inputs)
		{
			FCanonicalBoundary Boundary;
			Boundary.NormalizedBoundaryId = NormalizeName(Input.BoundaryId);
			Boundary.BoundaryId = Boundary.NormalizedBoundaryId.IsEmpty()
				? NAME_None
				: FName(*Boundary.NormalizedBoundaryId);
			Boundary.Operation = Input.Operation;

			if (Boundary.BoundaryId.IsNone())
			{
				bValid = false;
				if (OutErrors)
				{
					OutErrors->Add(TEXT("BoundaryId must not be None."));
				}
			}

			for (const FVector2D& Vertex : Input.Vertices)
			{
				if (!IsFiniteVector2D(Vertex) || !IsQuantizable(Vertex.X) || !IsQuantizable(Vertex.Y))
				{
					bValid = false;
					if (OutErrors)
					{
						OutErrors->Add(FString::Printf(TEXT("Boundary '%s' contains a non-finite or out-of-range vertex."),
							*Input.BoundaryId.ToString()));
					}
					continue;
				}
				const FVector2D CanonicalVertex = QuantizePoint(Vertex);
				if (Boundary.Vertices.IsEmpty() || Boundary.Vertices.Last() != CanonicalVertex)
				{
					Boundary.Vertices.Add(CanonicalVertex);
				}
			}
			if (Boundary.Vertices.Num() > 1 && Boundary.Vertices[0] == Boundary.Vertices.Last())
			{
				Boundary.Vertices.Pop(EAllowShrinking::No);
			}
			if (Boundary.Vertices.Num() < 3)
			{
				bValid = false;
				if (OutErrors)
				{
					OutErrors->Add(FString::Printf(TEXT("Boundary '%s' has fewer than three canonical vertices."),
						*Input.BoundaryId.ToString()));
				}
			}
			else
			{
				const double Area = SignedDoubleArea(Boundary.Vertices);
				if (FMath::IsNearlyZero(Area, GeometryEpsilon))
				{
					bValid = false;
					if (OutErrors)
					{
						OutErrors->Add(FString::Printf(TEXT("Boundary '%s' has zero canonical area."),
							*Input.BoundaryId.ToString()));
					}
				}
				else if (Area < 0.0)
				{
					Algo::Reverse(Boundary.Vertices);
				}
				RotateToStableStart(Boundary.Vertices);
			}
			OutBoundaries.Add(MoveTemp(Boundary));
		}

		OutBoundaries.Sort([](const FCanonicalBoundary& A, const FCanonicalBoundary& B)
		{
			const int32 NameOrder = A.NormalizedBoundaryId.Compare(B.NormalizedBoundaryId, ESearchCase::CaseSensitive);
			if (NameOrder != 0)
			{
				return NameOrder < 0;
			}
			return static_cast<uint8>(A.Operation) < static_cast<uint8>(B.Operation);
		});

		for (int32 Index = 1; Index < OutBoundaries.Num(); ++Index)
		{
			if (OutBoundaries[Index - 1].NormalizedBoundaryId == OutBoundaries[Index].NormalizedBoundaryId)
			{
				bValid = false;
				if (OutErrors)
				{
					OutErrors->Add(FString::Printf(TEXT("BoundaryId '%s' is duplicated."),
						*OutBoundaries[Index].NormalizedBoundaryId));
				}
			}
		}
		return bValid;
	}

	static double Cross(const FVector2D& A, const FVector2D& B, const FVector2D& C)
	{
		return (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
	}

	static bool PointOnSegment(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
	{
		return FMath::Abs(Cross(A, B, Point)) <= GeometryEpsilon
			&& Point.X >= FMath::Min(A.X, B.X) - GeometryEpsilon
			&& Point.X <= FMath::Max(A.X, B.X) + GeometryEpsilon
			&& Point.Y >= FMath::Min(A.Y, B.Y) - GeometryEpsilon
			&& Point.Y <= FMath::Max(A.Y, B.Y) + GeometryEpsilon;
	}

	static int32 Orientation(const FVector2D& A, const FVector2D& B, const FVector2D& C)
	{
		const double Value = Cross(A, B, C);
		return Value > GeometryEpsilon ? 1 : (Value < -GeometryEpsilon ? -1 : 0);
	}

	static bool SegmentsIntersect(
		const FVector2D& A,
		const FVector2D& B,
		const FVector2D& C,
		const FVector2D& D)
	{
		const int32 O1 = Orientation(A, B, C);
		const int32 O2 = Orientation(A, B, D);
		const int32 O3 = Orientation(C, D, A);
		const int32 O4 = Orientation(C, D, B);
		if (O1 != O2 && O3 != O4)
		{
			return true;
		}
		return (O1 == 0 && PointOnSegment(C, A, B))
			|| (O2 == 0 && PointOnSegment(D, A, B))
			|| (O3 == 0 && PointOnSegment(A, C, D))
			|| (O4 == 0 && PointOnSegment(B, C, D));
	}

	static bool HasInvalidSelfIntersection(const FCanonicalBoundary& Polygon)
	{
		const int32 NumVertices = Polygon.Vertices.Num();
		for (int32 First = 0; First < NumVertices; ++First)
		{
			const FVector2D& A = Polygon.Vertices[First];
			const FVector2D& B = Polygon.Vertices[(First + 1) % NumVertices];
			for (int32 Second = First + 1; Second < NumVertices; ++Second)
			{
				const FVector2D& C = Polygon.Vertices[Second];
				const FVector2D& D = Polygon.Vertices[(Second + 1) % NumVertices];
				const bool bAdjacent = Second == First + 1 || (First == 0 && Second == NumVertices - 1);
				if (bAdjacent)
				{
					const FVector2D Shared = Second == First + 1 ? B : A;
					const FVector2D OtherA = Second == First + 1 ? A : B;
					const FVector2D OtherB = Second == First + 1 ? D : C;
					const FVector2D RayA = OtherA - Shared;
					const FVector2D RayB = OtherB - Shared;
					if (FMath::Abs(RayA.X * RayB.Y - RayA.Y * RayB.X) <= GeometryEpsilon
						&& FVector2D::DotProduct(RayA, RayB) > GeometryEpsilon)
					{
						return true;
					}
					continue;
				}
				if (SegmentsIntersect(A, B, C, D))
				{
					return true;
				}
			}
		}
		return false;
	}

	static bool PolygonsIntersect(const FCanonicalBoundary& A, const FCanonicalBoundary& B)
	{
		for (int32 AIndex = 0; AIndex < A.Vertices.Num(); ++AIndex)
		{
			const FVector2D& A0 = A.Vertices[AIndex];
			const FVector2D& A1 = A.Vertices[(AIndex + 1) % A.Vertices.Num()];
			for (int32 BIndex = 0; BIndex < B.Vertices.Num(); ++BIndex)
			{
				const FVector2D& B0 = B.Vertices[BIndex];
				const FVector2D& B1 = B.Vertices[(BIndex + 1) % B.Vertices.Num()];
				if (SegmentsIntersect(A0, A1, B0, B1))
				{
					return true;
				}
			}
		}
		return false;
	}

	static EPointPolygonRelation ClassifyPoint(
		const TArray<FVector2D>& Vertices,
		const FVector2D& Point)
	{
		bool bInside = false;
		for (int32 Index = 0; Index < Vertices.Num(); ++Index)
		{
			const FVector2D& A = Vertices[Index];
			const FVector2D& B = Vertices[(Index + 1) % Vertices.Num()];
			if (PointOnSegment(Point, A, B))
			{
				return EPointPolygonRelation::Boundary;
			}
			if ((A.Y > Point.Y) != (B.Y > Point.Y))
			{
				const double IntersectionX = A.X + (Point.Y - A.Y) * (B.X - A.X) / (B.Y - A.Y);
				if (Point.X < IntersectionX)
				{
					bInside = !bInside;
				}
			}
		}
		return bInside ? EPointPolygonRelation::Inside : EPointPolygonRelation::Outside;
	}

	static bool ValidateTopology(const TArray<FCanonicalBoundary>& Boundaries, TArray<FString>& OutErrors)
	{
		bool bValid = true;
		TArray<const FCanonicalBoundary*> Includes;
		TArray<const FCanonicalBoundary*> Excludes;
		for (const FCanonicalBoundary& Boundary : Boundaries)
		{
			if (Boundary.Vertices.Num() >= 3 && HasInvalidSelfIntersection(Boundary))
			{
				bValid = false;
				OutErrors.Add(FString::Printf(TEXT("Boundary '%s' has a self-intersection or collinear overlap."),
					*Boundary.BoundaryId.ToString()));
			}
			(Boundary.Operation == ECatWaterBoundaryOperation::Include ? Includes : Excludes).Add(&Boundary);
		}

		if (Includes.IsEmpty())
		{
			bValid = false;
			OutErrors.Add(TEXT("At least one Include boundary is required."));
		}

		for (int32 First = 0; First < Includes.Num(); ++First)
		{
			for (int32 Second = First + 1; Second < Includes.Num(); ++Second)
			{
				const FCanonicalBoundary& A = *Includes[First];
				const FCanonicalBoundary& B = *Includes[Second];
				if (PolygonsIntersect(A, B)
					|| ClassifyPoint(A.Vertices, B.Vertices[0]) != EPointPolygonRelation::Outside
					|| ClassifyPoint(B.Vertices, A.Vertices[0]) != EPointPolygonRelation::Outside)
				{
					bValid = false;
					OutErrors.Add(FString::Printf(TEXT("Include boundaries '%s' and '%s' intersect or overlap."),
						*A.BoundaryId.ToString(), *B.BoundaryId.ToString()));
				}
			}
		}

		for (const FCanonicalBoundary* Exclude : Excludes)
		{
			int32 ContainingIncludes = 0;
			for (const FCanonicalBoundary* Include : Includes)
			{
				if (PolygonsIntersect(*Exclude, *Include))
				{
					bValid = false;
					OutErrors.Add(FString::Printf(TEXT("Exclude boundary '%s' intersects Include '%s'."),
						*Exclude->BoundaryId.ToString(), *Include->BoundaryId.ToString()));
				}
				else if (ClassifyPoint(Include->Vertices, Exclude->Vertices[0]) == EPointPolygonRelation::Inside)
				{
					++ContainingIncludes;
				}
			}
			if (ContainingIncludes != 1)
			{
				bValid = false;
				OutErrors.Add(FString::Printf(TEXT("Exclude boundary '%s' must lie strictly inside exactly one Include."),
					*Exclude->BoundaryId.ToString()));
			}
		}

		for (int32 First = 0; First < Excludes.Num(); ++First)
		{
			for (int32 Second = First + 1; Second < Excludes.Num(); ++Second)
			{
				const FCanonicalBoundary& A = *Excludes[First];
				const FCanonicalBoundary& B = *Excludes[Second];
				if (PolygonsIntersect(A, B)
					|| ClassifyPoint(A.Vertices, B.Vertices[0]) != EPointPolygonRelation::Outside
					|| ClassifyPoint(B.Vertices, A.Vertices[0]) != EPointPolygonRelation::Outside)
				{
					bValid = false;
					OutErrors.Add(FString::Printf(TEXT("Exclude boundaries '%s' and '%s' intersect, overlap, or nest."),
						*A.BoundaryId.ToString(), *B.BoundaryId.ToString()));
				}
			}
		}
		return bValid;
	}

	static void WriteUInt8(FXxHash64Builder& Builder, const uint8 Value)
	{
		Builder.Update(&Value, sizeof(Value));
	}

	static void WriteUInt32LittleEndian(FXxHash64Builder& Builder, const uint32 Value)
	{
		uint8 Bytes[4];
		for (uint32 Index = 0; Index < UE_ARRAY_COUNT(Bytes); ++Index)
		{
			Bytes[Index] = static_cast<uint8>((Value >> (Index * 8)) & 0xffU);
		}
		Builder.Update(Bytes, sizeof(Bytes));
	}

	static void WriteInt64LittleEndian(FXxHash64Builder& Builder, const int64 Value)
	{
		const uint64 UnsignedValue = static_cast<uint64>(Value);
		uint8 Bytes[8];
		for (uint32 Index = 0; Index < UE_ARRAY_COUNT(Bytes); ++Index)
		{
			Bytes[Index] = static_cast<uint8>((UnsignedValue >> (Index * 8)) & 0xffULL);
		}
		Builder.Update(Bytes, sizeof(Bytes));
	}

	static void WriteName(FXxHash64Builder& Builder, const FName Name)
	{
		const FString Lowercase = NormalizeName(Name);
		const FTCHARToUTF8 Utf8(*Lowercase);
		WriteUInt32LittleEndian(Builder, static_cast<uint32>(Utf8.Length()));
		if (Utf8.Length() > 0)
		{
			Builder.Update(Utf8.Get(), static_cast<uint64>(Utf8.Length()));
		}
	}

	static bool IsNonNegativeFinite(const double Value)
	{
		return IsQuantizable(Value) && Value >= 0.0;
	}

	static FVector2D ClosestPointOnSegment(
		const FVector2D& Point,
		const FVector2D& A,
		const FVector2D& B)
	{
		const FVector2D Segment = B - A;
		const double SegmentLengthSquared = Segment.SizeSquared();
		if (SegmentLengthSquared <= GeometryEpsilon)
		{
			return A;
		}
		const double Alpha = FMath::Clamp(FVector2D::DotProduct(Point - A, Segment) / SegmentLengthSquared, 0.0, 1.0);
		return A + Segment * Alpha;
	}

	struct FNearestShore
	{
		bool bFound = false;
		double DistanceSquared = TNumericLimits<double>::Max();
		FString NormalizedBoundaryId;
		int32 SegmentIndex = INDEX_NONE;
		FVector2D Point = FVector2D::ZeroVector;
		FVector2D Waterward = FVector2D::ZeroVector;
		ECatWaterShoreKind Kind = ECatWaterShoreKind::None;
	};

	static void ConsiderPolygon(
		const FCatWaterBakedPolygon& Polygon,
		const ECatWaterShoreKind Kind,
		const FVector2D& QueryPoint,
		FNearestShore& InOutNearest)
	{
		const FString NormalizedId = NormalizeName(Polygon.BoundaryId);
		const double DirectionSign = Kind == ECatWaterShoreKind::ExcludedBoundary ? -1.0 : 1.0;
		auto GetEdgeWaterward = [DirectionSign](const FVector2D& A, const FVector2D& B)
		{
			const FVector2D Edge = (B - A).GetSafeNormal();
			return FVector2D(-Edge.Y, Edge.X) * DirectionSign;
		};
		for (int32 SegmentIndex = 0; SegmentIndex < Polygon.Vertices.Num(); ++SegmentIndex)
		{
			const FVector2D& A = Polygon.Vertices[SegmentIndex];
			const FVector2D& B = Polygon.Vertices[(SegmentIndex + 1) % Polygon.Vertices.Num()];
			const FVector2D ShorePoint = ClosestPointOnSegment(QueryPoint, A, B);
			const double DistanceSquared = FVector2D::DistSquared(QueryPoint, ShorePoint);
			const bool bCloser = DistanceSquared < InOutNearest.DistanceSquared - GeometryEpsilon;
			const bool bEqualDistance = FMath::Abs(DistanceSquared - InOutNearest.DistanceSquared) <= GeometryEpsilon;
			const int32 NameOrder = NormalizedId.Compare(InOutNearest.NormalizedBoundaryId, ESearchCase::CaseSensitive);
			const bool bWinsTie = bEqualDistance
				&& (!InOutNearest.bFound || NameOrder < 0
					|| (NameOrder == 0 && SegmentIndex < InOutNearest.SegmentIndex));
			if (!bCloser && !bWinsTie)
			{
				continue;
			}

			FVector2D Waterward = GetEdgeWaterward(A, B);
			int32 VertexIndex = INDEX_NONE;
			if (FVector2D::DistSquared(ShorePoint, A) <= GeometryEpsilon)
			{
				VertexIndex = SegmentIndex;
			}
			else if (FVector2D::DistSquared(ShorePoint, B) <= GeometryEpsilon)
			{
				VertexIndex = (SegmentIndex + 1) % Polygon.Vertices.Num();
			}
			if (VertexIndex != INDEX_NONE)
			{
				const int32 PreviousIndex = (VertexIndex + Polygon.Vertices.Num() - 1) % Polygon.Vertices.Num();
				const int32 NextIndex = (VertexIndex + 1) % Polygon.Vertices.Num();
				const FVector2D PreviousWaterward = GetEdgeWaterward(
					Polygon.Vertices[PreviousIndex], Polygon.Vertices[VertexIndex]);
				const FVector2D NextWaterward = GetEdgeWaterward(
					Polygon.Vertices[VertexIndex], Polygon.Vertices[NextIndex]);
				Waterward = (PreviousWaterward + NextWaterward).GetSafeNormal();
			}
			InOutNearest.bFound = true;
			InOutNearest.DistanceSquared = DistanceSquared;
			InOutNearest.NormalizedBoundaryId = NormalizedId;
			InOutNearest.SegmentIndex = SegmentIndex;
			InOutNearest.Point = ShorePoint;
			InOutNearest.Waterward = Waterward;
			InOutNearest.Kind = Kind;
		}
	}
}

bool FCatWaterGeometryCache::IsRuntimeReady() const
{
	return Handle.IsValid()
		&& !IncludePolygons.IsEmpty()
		&& Bounds2D.bIsValid
		&& !PlaneToWorld.ContainsNaN()
		&& !WorldToPlane.ContainsNaN()
		&& CatWaterGeometryPrivate::IsNonNegativeFinite(WaterPointVerticalToleranceCm)
		&& CatWaterGeometryPrivate::IsNonNegativeFinite(BankHeightToleranceCm)
		&& CatWaterGeometryPrivate::IsNonNegativeFinite(BoundaryToleranceCm)
		&& CatWaterGeometryPrivate::IsNonNegativeFinite(MaxLandingCorrectionCm)
		&& CatWaterGeometryPrivate::IsNonNegativeFinite(MinimumWaterInsetCm)
		&& CatWaterGeometryPrivate::IsQuantizable(MaxSampleSegmentLengthCm)
		&& MaxSampleSegmentLengthCm > 0.0
		&& CatWaterGeometryPrivate::IsNonNegativeFinite(MaxChordErrorCm);
}

FCatWaterGeometryBuildResult FCatWaterGeometry::Build(const FCatWaterGeometryBuildInput& Input)
{
	using namespace CatWaterGeometryPrivate;
	FCatWaterGeometryBuildResult Result;
	if (Input.RegionId.IsNone())
	{
		Result.Errors.Add(TEXT("RegionId must not be None."));
	}

	const FVector Origin = Input.PlaneToWorld.GetLocation();
	const FRotator Rotation = Input.PlaneToWorld.Rotator();
	const FVector Scale = Input.PlaneToWorld.GetScale3D();
	if (!IsFiniteVector(Origin) || !IsQuantizable(Origin.X) || !IsQuantizable(Origin.Y) || !IsQuantizable(Origin.Z)
		|| Input.PlaneToWorld.ContainsNaN()
		|| !FMath::IsNearlyZero(Rotation.Pitch) || !FMath::IsNearlyZero(Rotation.Roll)
		|| !Scale.Equals(FVector::OneVector, UE_KINDA_SMALL_NUMBER))
	{
		Result.Errors.Add(TEXT("PlaneToWorld must be a finite yaw-only unit-scale transform."));
	}

	const double Settings[] = {
		Input.WaterPointVerticalToleranceCm,
		Input.BankHeightToleranceCm,
		Input.BoundaryToleranceCm,
		Input.MaxLandingCorrectionCm,
		Input.MinimumWaterInsetCm,
		Input.MaxChordErrorCm
	};
	for (const double Setting : Settings)
	{
		if (!IsNonNegativeFinite(Setting))
		{
			Result.Errors.Add(TEXT("All water geometry tolerances must be finite, quantizable, and non-negative."));
			break;
		}
	}
	if (!IsQuantizable(Input.MaxSampleSegmentLengthCm) || Input.MaxSampleSegmentLengthCm <= 0.0)
	{
		Result.Errors.Add(TEXT("MaxSampleSegmentLengthCm must be finite, quantizable, and positive."));
	}

	TArray<FCanonicalBoundary> CanonicalBoundaries;
	CanonicalizeBoundaries(Input.Boundaries, CanonicalBoundaries, &Result.Errors);
	bool bTopologyInputSafe = true;
	for (const FCanonicalBoundary& Boundary : CanonicalBoundaries)
	{
		bTopologyInputSafe &= Boundary.Vertices.Num() >= 3;
	}
	if (bTopologyInputSafe)
	{
		ValidateTopology(CanonicalBoundaries, Result.Errors);
	}
	if (!Result.Errors.IsEmpty())
	{
		return Result;
	}

	FCatWaterGeometryBuildInput CanonicalInput;
	CanonicalInput.RegionId = FName(*NormalizeName(Input.RegionId));
	const FVector CanonicalOrigin(
		Dequantize(Quantize(Origin.X)),
		Dequantize(Quantize(Origin.Y)),
		Dequantize(Quantize(Origin.Z)));
	const double CanonicalYaw = Dequantize(Quantize(FRotator::NormalizeAxis(Rotation.Yaw)));
	CanonicalInput.PlaneToWorld = FTransform(FRotator(0.0, CanonicalYaw, 0.0), CanonicalOrigin);
	CanonicalInput.WaterPointVerticalToleranceCm = Dequantize(Quantize(Input.WaterPointVerticalToleranceCm));
	CanonicalInput.BankHeightToleranceCm = Dequantize(Quantize(Input.BankHeightToleranceCm));
	CanonicalInput.BoundaryToleranceCm = Dequantize(Quantize(Input.BoundaryToleranceCm));
	CanonicalInput.MaxLandingCorrectionCm = Dequantize(Quantize(Input.MaxLandingCorrectionCm));
	CanonicalInput.MinimumWaterInsetCm = Dequantize(Quantize(Input.MinimumWaterInsetCm));
	CanonicalInput.MaxSampleSegmentLengthCm = Dequantize(Quantize(Input.MaxSampleSegmentLengthCm));
	CanonicalInput.MaxChordErrorCm = Dequantize(Quantize(Input.MaxChordErrorCm));

	Result.Cache.Bounds2D = FBox2D(ForceInit);
	for (const FCanonicalBoundary& Boundary : CanonicalBoundaries)
	{
		FCatWaterPolygonBuildInput& CanonicalBoundary = CanonicalInput.Boundaries.AddDefaulted_GetRef();
		CanonicalBoundary.BoundaryId = Boundary.BoundaryId;
		CanonicalBoundary.Operation = Boundary.Operation;
		CanonicalBoundary.Vertices = Boundary.Vertices;

		FCatWaterBakedPolygon Polygon;
		Polygon.BoundaryId = Boundary.BoundaryId;
		Polygon.Vertices = Boundary.Vertices;
		Polygon.Bounds = FBox2D(Polygon.Vertices);
		Result.Cache.Bounds2D += Polygon.Bounds;
		(Boundary.Operation == ECatWaterBoundaryOperation::Include
			? Result.Cache.IncludePolygons
			: Result.Cache.ExcludePolygons).Add(MoveTemp(Polygon));
	}

	Result.Cache.Handle.RegionId = CanonicalInput.RegionId;
	Result.Cache.Handle.GeometryRevision = ComputeRevision(CanonicalInput);
	Result.Cache.PlaneToWorld = CanonicalInput.PlaneToWorld;
	Result.Cache.WorldToPlane = CanonicalInput.PlaneToWorld.Inverse();
	Result.Cache.WaterSurfaceZ = CanonicalOrigin.Z;
	Result.Cache.WaterPointVerticalToleranceCm = CanonicalInput.WaterPointVerticalToleranceCm;
	Result.Cache.BankHeightToleranceCm = CanonicalInput.BankHeightToleranceCm;
	Result.Cache.BoundaryToleranceCm = CanonicalInput.BoundaryToleranceCm;
	Result.Cache.MaxLandingCorrectionCm = CanonicalInput.MaxLandingCorrectionCm;
	Result.Cache.MinimumWaterInsetCm = CanonicalInput.MinimumWaterInsetCm;
	Result.Cache.MaxSampleSegmentLengthCm = CanonicalInput.MaxSampleSegmentLengthCm;
	Result.Cache.MaxChordErrorCm = CanonicalInput.MaxChordErrorCm;
	Result.bSucceeded = Result.Cache.IsRuntimeReady();
	if (!Result.bSucceeded)
	{
		UE_LOG(LogTemp, Error, TEXT("Water Cache Runtime Ready Failed"));

		UE_LOG(LogTemp, Error, TEXT("Handle Valid=%d"), Result.Cache.Handle.IsValid());
		UE_LOG(LogTemp, Error, TEXT("Include Count=%d"), Result.Cache.IncludePolygons.Num());
		UE_LOG(LogTemp, Error, TEXT("Bounds Valid=%d"), Result.Cache.Bounds2D.bIsValid);
		UE_LOG(LogTemp, Error, TEXT("PlaneToWorld NaN=%d"), Result.Cache.PlaneToWorld.ContainsNaN());
		UE_LOG(LogTemp, Error, TEXT("WorldToPlane NaN=%d"), Result.Cache.WorldToPlane.ContainsNaN());
		UE_LOG(LogTemp, Error, TEXT("WaterTolerance=%f"), Result.Cache.WaterPointVerticalToleranceCm);
		UE_LOG(LogTemp, Error, TEXT("MaxSample=%f"), Result.Cache.MaxSampleSegmentLengthCm);
	}
	return Result;
}

int64 FCatWaterGeometry::ComputeRevision(const FCatWaterGeometryBuildInput& CanonicalInput)
{
	using namespace CatWaterGeometryPrivate;
	TArray<FCanonicalBoundary> Boundaries;
	CanonicalizeBoundaries(CanonicalInput.Boundaries, Boundaries, nullptr);

	FXxHash64Builder Builder;
	WriteName(Builder, CanonicalInput.RegionId);
	const FVector Origin = CanonicalInput.PlaneToWorld.GetLocation();
	WriteInt64LittleEndian(Builder, Quantize(Origin.X));
	WriteInt64LittleEndian(Builder, Quantize(Origin.Y));
	WriteInt64LittleEndian(Builder, Quantize(Origin.Z));
	WriteInt64LittleEndian(Builder, Quantize(FRotator::NormalizeAxis(CanonicalInput.PlaneToWorld.Rotator().Yaw)));
	WriteInt64LittleEndian(Builder, Quantize(CanonicalInput.WaterPointVerticalToleranceCm));
	WriteInt64LittleEndian(Builder, Quantize(CanonicalInput.BankHeightToleranceCm));
	WriteInt64LittleEndian(Builder, Quantize(CanonicalInput.BoundaryToleranceCm));
	WriteInt64LittleEndian(Builder, Quantize(CanonicalInput.MaxLandingCorrectionCm));
	WriteInt64LittleEndian(Builder, Quantize(CanonicalInput.MinimumWaterInsetCm));
	WriteInt64LittleEndian(Builder, Quantize(CanonicalInput.MaxSampleSegmentLengthCm));
	WriteInt64LittleEndian(Builder, Quantize(CanonicalInput.MaxChordErrorCm));
	WriteUInt32LittleEndian(Builder, static_cast<uint32>(Boundaries.Num()));
	for (const FCanonicalBoundary& Boundary : Boundaries)
	{
		WriteUInt8(Builder, static_cast<uint8>(Boundary.Operation));
		WriteName(Builder, Boundary.BoundaryId);
		WriteUInt32LittleEndian(Builder, static_cast<uint32>(Boundary.Vertices.Num()));
		for (const FVector2D& Vertex : Boundary.Vertices)
		{
			WriteInt64LittleEndian(Builder, Quantize(Vertex.X));
			WriteInt64LittleEndian(Builder, Quantize(Vertex.Y));
		}
	}
	const uint64 Raw = Builder.Finalize().Hash & MAX_int64;
	return static_cast<int64>(Raw == 0 ? 1 : Raw);
}

FCatWaterSpatialResult FCatWaterGeometry::QueryPoint(
	const FCatWaterGeometryCache& Cache,
	const FVector& WorldPoint,
	const double VerticalToleranceCm)
{
	using namespace CatWaterGeometryPrivate;
	FCatWaterSpatialResult Result;
	if (!IsFiniteVector(WorldPoint))
	{
		Result.Error = ECatWaterQueryError::InvalidLocation;
		return Result;
	}
	if (!Cache.IsRuntimeReady() || !FMath::IsFinite(VerticalToleranceCm) || VerticalToleranceCm < 0.0)
	{
		Result.Error = ECatWaterQueryError::InvalidGeometry;
		return Result;
	}

	Result.WaterRegion = Cache.Handle;
	const FVector PlanePoint3D = Cache.WorldToPlane.TransformPosition(WorldPoint);
	const FVector2D PlanePoint(PlanePoint3D.X, PlanePoint3D.Y);
	Result.WaterSurfaceWorldPoint = Cache.PlaneToWorld.TransformPosition(FVector(PlanePoint.X, PlanePoint.Y, 0.0));
	Result.WaterSurfaceNormal = Cache.PlaneToWorld.TransformVectorNoScale(FVector::UpVector).GetSafeNormal();
	Result.VerticalDeltaCm = FVector::DotProduct(WorldPoint - Result.WaterSurfaceWorldPoint, Result.WaterSurfaceNormal);
	if (FMath::Abs(Result.VerticalDeltaCm) > VerticalToleranceCm)
	{
		Result.Error = ECatWaterQueryError::HeightOutOfTolerance;
		return Result;
	}

	bool bInsideInclude = false;
	bool bInsideExclude = false;
	for (const FCatWaterBakedPolygon& Include : Cache.IncludePolygons)
	{
		bInsideInclude |= ClassifyPoint(Include.Vertices, PlanePoint) != EPointPolygonRelation::Outside;
	}
	for (const FCatWaterBakedPolygon& Exclude : Cache.ExcludePolygons)
	{
		bInsideExclude |= ClassifyPoint(Exclude.Vertices, PlanePoint) != EPointPolygonRelation::Outside;
	}

	FNearestShore Nearest;
	for (const FCatWaterBakedPolygon& Include : Cache.IncludePolygons)
	{
		ConsiderPolygon(Include, ECatWaterShoreKind::OuterBoundary, PlanePoint, Nearest);
	}
	for (const FCatWaterBakedPolygon& Exclude : Cache.ExcludePolygons)
	{
		ConsiderPolygon(Exclude, ECatWaterShoreKind::ExcludedBoundary, PlanePoint, Nearest);
	}
	if (!Nearest.bFound)
	{
		Result.Error = ECatWaterQueryError::InvalidGeometry;
		return Result;
	}

	const double Distance = FMath::Sqrt(Nearest.DistanceSquared);
	const bool bInsideWater = bInsideInclude && !bInsideExclude;
	if (Distance <= Cache.BoundaryToleranceCm)
	{
		Result.Containment = ECatWaterContainment::Boundary;
		Result.SignedDistanceToShoreCm = 0.0;
	}
	else
	{
		Result.Containment = bInsideWater ? ECatWaterContainment::Inside : ECatWaterContainment::Outside;
		Result.SignedDistanceToShoreCm = bInsideWater ? Distance : -Distance;
	}
	Result.NearestShoreKind = Nearest.Kind;
	Result.NearestShoreWorldPoint = Cache.PlaneToWorld.TransformPosition(FVector(Nearest.Point.X, Nearest.Point.Y, 0.0));
	Result.WaterwardDirection = Cache.PlaneToWorld.TransformVectorNoScale(
		FVector(Nearest.Waterward.X, Nearest.Waterward.Y, 0.0)).GetSafeNormal();
	Result.bSucceeded = true;
	Result.Error = ECatWaterQueryError::None;
	return Result;
}

FCatWaterSpatialResult FCatWaterGeometry::ResolveCandidatePoint(
	const FCatWaterGeometryCache& Cache,
	const FVector& CandidateWorldPoint)
{
	FCatWaterSpatialResult Result = QueryPoint(Cache, CandidateWorldPoint, Cache.BankHeightToleranceCm);
	if (!Result.bSucceeded || Result.Containment == ECatWaterContainment::Inside)
	{
		return Result;
	}
	if (Result.NearestShoreKind != ECatWaterShoreKind::OuterBoundary
		|| FMath::Abs(Result.SignedDistanceToShoreCm) > Cache.MaxLandingCorrectionCm)
	{
		Result.bSucceeded = false;
		Result.Error = ECatWaterQueryError::InvalidLocation;
		return Result;
	}

	const FVector CorrectedWorldPoint = Result.NearestShoreWorldPoint
		+ Result.WaterwardDirection * Cache.MinimumWaterInsetCm;
	FCatWaterSpatialResult Corrected = QueryPoint(Cache, CorrectedWorldPoint, Cache.WaterPointVerticalToleranceCm);
	if (!Corrected.bSucceeded || Corrected.Containment != ECatWaterContainment::Inside)
	{
		Result.bSucceeded = false;
		Result.Error = ECatWaterQueryError::InvalidLocation;
		return Result;
	}
	return Corrected;
}
