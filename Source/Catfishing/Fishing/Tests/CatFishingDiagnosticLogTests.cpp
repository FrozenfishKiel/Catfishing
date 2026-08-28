#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Environment/CatWaterTypes.h"
#include "Fishing/Integration/CatFishingCommandTypes.h"
#include "Logging/CatLogContext.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingDiagnosticLogContractTest,
	"Catfishing.Unit.Fishing.Diagnostics.StructuredContextPreservesFailureFacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingDiagnosticLogContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCatWaterSpatialResult Spatial;
	Spatial.bSucceeded = false;
	Spatial.Error = ECatWaterQueryError::HeightOutOfTolerance;
	Spatial.Containment = ECatWaterContainment::Boundary;
	Spatial.WaterRegion.RegionId = TEXT("Lake_A");
	Spatial.WaterRegion.GeometryRevision = 42;
	Spatial.VerticalDeltaCm = 96.25;
	Spatial.SignedDistanceToShoreCm = -12.5;
	Spatial.NearestShoreKind = ECatWaterShoreKind::OuterBoundary;
	Spatial.NearestShoreWorldPoint = FVector(10.0, 20.0, 30.0);
	Spatial.WaterSurfaceWorldPoint = FVector(40.0, 50.0, 60.0);

	const FString WaterFields = CatLogContext::BuildWaterSpatialFields(
		TEXT("CenterWater"), FVector(1.0, 2.0, 3.0), Spatial);
	TestTrue(TEXT("水域日志保留查询错误枚举"), WaterFields.Contains(TEXT("CenterWaterError=ECatWaterQueryError::HeightOutOfTolerance")));
	TestTrue(TEXT("水域日志保留闭集分类"), WaterFields.Contains(TEXT("CenterWaterContainment=ECatWaterContainment::Boundary")));
	TestTrue(TEXT("水域日志保留 Region 与几何版本"),
		WaterFields.Contains(TEXT("CenterWaterRegion=Lake_A CenterWaterGeometryRevision=42")));
	TestTrue(TEXT("水域日志保留垂直差与岸距"),
		WaterFields.Contains(TEXT("CenterWaterVerticalDeltaCm=96.250 CenterWaterSignedShoreDistanceCm=-12.500")));

	const FString MissingControllerFields = CatLogContext::BuildControllerFields(nullptr);
	TestTrue(TEXT("缺 Controller 仍输出稳定字段"), MissingControllerFields.Contains(TEXT("Controller=None")));
	TestTrue(TEXT("缺身份明确标记 Invalid"), MissingControllerFields.Contains(TEXT("StableNetId=Invalid")));
	TestTrue(TEXT("缺 Controller 明确标记非本地"), MissingControllerFields.Contains(TEXT("IsLocalController=false")));

	TestEqual(TEXT("抄网策略/几何拒绝不再伪装成依赖缺失"),
		MapDomainCommandError(ECatDomainCommandError::PolicyUndecided),
		ECatFishingCommandError::ScoopGeometryFailed);
	return !HasAnyErrors();
}

#endif
