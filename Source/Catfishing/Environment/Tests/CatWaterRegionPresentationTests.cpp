#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Components/PrimitiveComponent.h"
#include "Environment/CatWaterGeometry.h"
#include "Environment/CatWaterRegion.h"
#include "Environment/Presentation/CatWaterRegionPresentationActor.h"
#include "Environment/Presentation/CatWaterRegionPresentationSubsystem.h"
#include "Environment/Tests/CatWaterTestFixtures.h"

namespace CatWaterPresentationTest
{
	static FCatWaterGeometryCache BuildCache()
	{
		FCatWaterGeometryBuildInput Input;
		Input.RegionId = TEXT("LakeA"); Input.PlaneToWorld = FTransform(FRotator::ZeroRotator, FVector(0,0,100));
		Input.WaterPointVerticalToleranceCm = 10; Input.BankHeightToleranceCm = 30; Input.BoundaryToleranceCm = 1;
		Input.MaxLandingCorrectionCm = 20; Input.MinimumWaterInsetCm = 5;
		FCatWaterPolygonBuildInput& Boundary = Input.Boundaries.AddDefaulted_GetRef();
		Boundary.BoundaryId = TEXT("Outer"); Boundary.Vertices = {{0,0},{200,0},{200,200},{0,200}};
		return FCatWaterGeometry::Build(Input).Cache;
	}

	static ACatWaterRegion* SpawnRegion(UWorld* World)
	{
		ACatWaterRegion* Region = World->SpawnActorDeferred<ACatWaterRegion>(ACatWaterRegion::StaticClass(), FTransform::Identity);
		FCatWaterRegionTestAccess::InjectBakedGeometry(*Region, BuildCache());
		Region->WaterPresentationClass = ACatWaterRegionPresentationActor::StaticClass();
		Region->FinishSpawning(FTransform::Identity);
		return Region;
	}
}

#define CAT_PRESENTATION_TEST(ClassName, Suffix) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(ClassName, "Catfishing.Unit.Environment.WaterPresentation." Suffix, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

CAT_PRESENTATION_TEST(FCatWaterPresentationContractTest, "BlueprintHooksAreCosmeticAndCollisionFree")
CAT_PRESENTATION_TEST(FCatWaterPresentationAuthorityTest, "PreviewDefaultsHiddenAndNeverFeedsAuthority")
CAT_PRESENTATION_TEST(FCatWaterPresentationRegistryTest, "SubsystemUsesExactHandleAndNoWorldScan")

bool FCatWaterPresentationContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	ACatWaterRegionPresentationActor* Actor = NewObject<ACatWaterRegionPresentationActor>();
	TestFalse(TEXT("presentation does not tick"), Actor->PrimaryActorTick.bCanEverTick);
	TestFalse(TEXT("presentation does not replicate"), Actor->GetIsReplicated());
	TestNotNull(TEXT("visual root exists"), Actor->GetRootComponent());
	TestFalse(TEXT("visual root never affects navigation"), Actor->GetRootComponent()->CanEverAffectNavigation());
	for (const FName Name : {FName(TEXT("BP_ApplyWaterGeometryPresentation")), FName(TEXT("BP_SetWaterPreviewVisible"))})
	{
		const UFunction* Event = Actor->FindFunction(Name);
		TestNotNull(TEXT("Blueprint event exists"), Event);
		if (Event) TestTrue(TEXT("Blueprint event is cosmetic"), Event->HasAnyFunctionFlags(FUNC_BlueprintCosmetic));
	}
	for (UActorComponent* Component : Actor->GetComponents())
	{
		if (const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
			TestEqual(TEXT("presentation primitive has no collision"), Primitive->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	}
	return !HasAnyErrors();
}

bool FCatWaterPresentationAuthorityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper; WorldWrapper.CreateTestWorld(EWorldType::Game); WorldWrapper.BeginPlayInTestWorld(); UWorld* World = WorldWrapper.GetTestWorld();
	ACatWaterRegion* Region = CatWaterPresentationTest::SpawnRegion(World);
	UCatWaterRegionPresentationSubsystem* Subsystem = World->GetSubsystem<UCatWaterRegionPresentationSubsystem>();
	const FCatWaterRegionHandle Handle = Region->GetWaterRegionHandle();
	TestTrue(TEXT("presentation registered"), Subsystem->SetLocalWaterPreviewVisible(Handle, false));
	TestTrue(TEXT("authority remains baked and valid"), Region->HasValidBakedGeometry());
	TestEqual(TEXT("presentation never changes authority revision"), Region->GetWaterRegionHandle(), Handle);
	return !HasAnyErrors();
}

bool FCatWaterPresentationRegistryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper; WorldWrapper.CreateTestWorld(EWorldType::Game); WorldWrapper.BeginPlayInTestWorld(); UWorld* World = WorldWrapper.GetTestWorld();
	ACatWaterRegion* Region = CatWaterPresentationTest::SpawnRegion(World);
	UCatWaterRegionPresentationSubsystem* Subsystem = World->GetSubsystem<UCatWaterRegionPresentationSubsystem>();
	FCatWaterRegionHandle Exact = Region->GetWaterRegionHandle();
	TestTrue(TEXT("exact handle toggles local preview"), Subsystem->SetLocalWaterPreviewVisible(Exact, true));
	++Exact.GeometryRevision;
	TestFalse(TEXT("stale handle cannot find presentation"), Subsystem->SetLocalWaterPreviewVisible(Exact, true));
	Region->Destroy();
	TestFalse(TEXT("destroyed region unregisters presentation"), Subsystem->SetLocalWaterPreviewVisible(Region->GetWaterRegionHandle(), true));
	return !HasAnyErrors();
}

#undef CAT_PRESENTATION_TEST

#endif
