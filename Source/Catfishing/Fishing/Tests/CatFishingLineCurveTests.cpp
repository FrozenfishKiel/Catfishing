#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Fishing/Presentation/CatFishingLineCurve.h"
#include "Fishing/Presentation/CatFishingLineCurveComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingLineCurveGeometryTest,
	"Catfishing.Unit.Fishing.Presentation.LineCurvePreservesLengthWithoutInertia",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingLineCurveGeometryTest::RunTest(const FString& Parameters)
{
	TArray<FVector> Points;
	const FVector Start(100000.0, -300000.0, 200.0);
	const FVector End = Start + FVector(1000.0, 0.0, 0.0);
	TestTrue(TEXT("taut curve builds"), FCatFishingLineCurve::BuildPoints(Start, End, 1000.0, 64, Points));
	for (const FVector& Point : Points) TestEqual(TEXT("taut line has no artificial sag"), Point.Z, Start.Z);
	TestEqual(TEXT("first endpoint is exact"), Points[0], Start);
	TestEqual(TEXT("last endpoint is exact"), Points.Last(), End);
	double PreviousSag = 0.0;
	for (const double Length : {1000.01, 1010.0, 1100.0, 1500.0, 5000.0})
	{
		TestTrue(TEXT("slack curve builds"), FCatFishingLineCurve::BuildPoints(Start, End, Length, 64, Points));
		TestTrue(TEXT("drawn arc accounts for paid out line"), FMath::IsNearlyEqual(FCatFishingLineCurve::MeasureLength(Points), Length, 0.01));
		const double Sag = Start.Z - Points[32].Z;
		TestTrue(TEXT("more slack increases sag"), Sag > PreviousSag);
		PreviousSag = Sag;
	}
	for (const FVector& Offset : {FVector(0.0, 0.0, 500.0), FVector(0.001, 0.0, -500.0), FVector::ZeroVector})
	{
		TestTrue(TEXT("vertical or coincident endpoints build"), FCatFishingLineCurve::BuildPoints(Start, Start + Offset, 700.0, 64, Points));
		TestTrue(TEXT("vertical/loop curve retains paid out length"), FMath::IsNearlyEqual(FCatFishingLineCurve::MeasureLength(Points), 700.0, 0.01));
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			TestFalse(TEXT("curve stays finite"), Points[Index].ContainsNaN());
			TestTrue(TEXT("neighbouring points do not collapse"), FVector::Distance(Points[Index - 1], Points[Index]) > 0.0001);
		}
	}
	TArray<FVector> Left, Right;
	FCatFishingLineCurve::BuildPoints(Start, Start + FVector(-0.001, 0.0, 500.0), 700.0, 64, Left);
	FCatFishingLineCurve::BuildPoints(Start, Start + FVector(0.001, 0.0, 500.0), 700.0, 64, Right);
	for (int32 Index = 0; Index < Left.Num(); ++Index)
		TestTrue(TEXT("crossing vertical does not flip the sag plane"), Left[Index].Equals(Right[Index], 0.01));
	UCatFishingLineCurveComponent* Mesh = NewObject<UCatFishingLineCurveComponent>();
	TestTrue(TEXT("tube mesh builds"), Mesh->UpdateCurve(Start, End, 1100.0, 64, 1.25));
	const TArray<FVector> RestShape = Mesh->GetCurveWorldPoints();
	TestTrue(TEXT("moving endpoints updates mesh"), Mesh->UpdateCurve(Start + FVector(300, 0, 100), End, 1200.0, 64, 1.25));
	TestTrue(TEXT("returning endpoints updates mesh"), Mesh->UpdateCurve(Start, End, 1100.0, 64, 1.25));
	TestTrue(TEXT("shape depends only on current endpoints and length, with no residual velocity"), RestShape == Mesh->GetCurveWorldPoints());
	const FProcMeshSection* Section = Mesh->GetProcMeshSection(0);
	TestNotNull(TEXT("visible mesh section exists"), Section);
	if (Section)
	{
		TestFalse(TEXT("mesh section creates no collision"), Section->bEnableCollision);
		for (const FProcMeshVertex& Vertex : Section->ProcVertexBuffer)
		{
			TestFalse(TEXT("mesh vertices stay finite"), Vertex.Position.ContainsNaN() || Vertex.Normal.ContainsNaN());
			TestTrue(TEXT("normals have unit length"), FMath::IsNearlyEqual(Vertex.Normal.Size(), 1.0, 0.001));
		}
		for (int32 Index = 0; Index < Section->ProcIndexBuffer.Num(); Index += 3)
		{
			const auto& A = Section->ProcVertexBuffer[Section->ProcIndexBuffer[Index]];
			const auto& B = Section->ProcVertexBuffer[Section->ProcIndexBuffer[Index + 1]];
			const auto& C = Section->ProcVertexBuffer[Section->ProcIndexBuffer[Index + 2]];
			TestTrue(TEXT("tube winding faces outward"), FVector::DotProduct(FVector::CrossProduct(B.Position - A.Position, C.Position - A.Position), A.Normal + B.Normal + C.Normal) > 0.0);
		}
	}
	TestTrue(TEXT("subdivision change rebuilds topology"), Mesh->UpdateCurve(Start, End, 1100.0, 32, 1.25));
	TestEqual(TEXT("topology follows new ring count"), Mesh->GetProcMeshSection(0)->ProcVertexBuffer.Num(), 33 * 7);
	TestTrue(TEXT("zero-length line is accepted"), Mesh->UpdateCurve(Start, Start, 0.0, 64, 1.25));
	TestEqual(TEXT("zero-length line has no degenerate mesh"), Mesh->GetNumSections(), 0);
	TestFalse(TEXT("invalid length fails closed"), Mesh->UpdateCurve(Start, End, -1.0, 64, 1.25));
	TestEqual(TEXT("invalid input leaves no stale mesh"), Mesh->GetNumSections(), 0);
	return !HasAnyErrors();
}

#endif
