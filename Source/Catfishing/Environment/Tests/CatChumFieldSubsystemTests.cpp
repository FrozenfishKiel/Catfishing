#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Curves/CurveFloat.h"
#include "Engine/World.h"
#include "Environment/CatChumFieldSettings.h"
#include "Environment/CatChumFieldSubsystem.h"
#include "Environment/CatWaterGeometry.h"
#include "Environment/CatWaterRegion.h"
#include "Environment/Tests/CatWaterTestFixtures.h"
#include "Tests/AutomationCommon.h"

namespace CatChumFieldSubsystemTest
{
	static FCatWaterGeometryCache BuildRegion(FName Id, FVector Origin, bool bWithHole = false)
	{
		FCatWaterGeometryBuildInput Input;
		Input.RegionId = Id; Input.PlaneToWorld = FTransform(FRotator::ZeroRotator, Origin);
		Input.WaterPointVerticalToleranceCm = 10; Input.BankHeightToleranceCm = 40;
		Input.BoundaryToleranceCm = 1; Input.MaxLandingCorrectionCm = 25;
		Input.MinimumWaterInsetCm = 5; Input.MaxSampleSegmentLengthCm = 100; Input.MaxChordErrorCm = 5;
		FCatWaterPolygonBuildInput& Outer = Input.Boundaries.AddDefaulted_GetRef();
		Outer.BoundaryId = TEXT("Outer"); Outer.Vertices = {{0,0},{1000,0},{1000,1000},{0,1000}};
		if (bWithHole)
		{
			FCatWaterPolygonBuildInput& Hole = Input.Boundaries.AddDefaulted_GetRef();
			Hole.BoundaryId = TEXT("Hole"); Hole.Operation = ECatWaterBoundaryOperation::Exclude;
			Hole.Vertices = {{400,400},{600,400},{600,600},{400,600}};
		}
		return FCatWaterGeometry::Build(Input).Cache;
	}

	static ACatWaterRegion* SpawnRegion(UWorld* World, const FCatWaterGeometryCache& Cache)
	{
		const FTransform Transform(FRotator::ZeroRotator, Cache.PlaneToWorld.GetLocation());
		ACatWaterRegion* Region = World->SpawnActorDeferred<ACatWaterRegion>(ACatWaterRegion::StaticClass(), Transform);
		FCatWaterRegionTestAccess::InjectBakedGeometry(*Region, Cache);
		Region->FinishSpawning(Transform);
		return Region;
	}

	static FCatChumInfluenceSpec MakeInfluence(double Fishy = 2.0, double Radius = 300.0, double Duration = 10.0)
	{
		UCurveFloat* Distance = NewObject<UCurveFloat>(GetTransientPackage());
		Distance->FloatCurve.AddKey(0.0f, 1.0f); Distance->FloatCurve.AddKey(1.0f, 0.0f);
		UCurveFloat* Time = NewObject<UCurveFloat>(GetTransientPackage());
		Time->FloatCurve.AddKey(0.0f, 1.0f); Time->FloatCurve.AddKey(1.0f, 0.0f);
		FCatChumInfluenceSpec Spec;
		Spec.RadiusCentimeters = Radius; Spec.DurationSeconds = Duration;
		Spec.BaseContribution.Fishy = Fishy;
		Spec.DistanceFalloffCurve = Distance; Spec.TimeFalloffCurve = Time;
		Spec.MaximumQuantityPerPlacement = 4;
		return Spec;
	}

	struct FSettingsScope
	{
		UCatChumFieldSettings* Settings = GetMutableDefault<UCatChumFieldSettings>();
		bool bSavedEnable = false;
		int32 SavedMaxFields = 0;
		double SavedMaxContribution = 0.0;
		double SavedRange = 0.0;
		double SavedAngle = 0.0;
		TEnumAsByte<ECollisionChannel> SavedChannel = ECC_Visibility;
		double SavedCleanupInterval = 0.0;
		FSettingsScope()
		{
			bSavedEnable = Settings->bEnableChumFieldRuntime;
			SavedMaxFields = Settings->MaxActiveFieldsPerRegion;
			SavedMaxContribution = Settings->MaxRawContributionPerRegion;
			SavedRange = Settings->MaxPlacementRangeCentimeters;
			SavedAngle = Settings->MaxAimDeviationDegrees;
			SavedChannel = Settings->PlacementLineOfSightChannel;
			SavedCleanupInterval = Settings->ExpiredCleanupIntervalSeconds;
			Settings->bEnableChumFieldRuntime = true; Settings->MaxActiveFieldsPerRegion = 4;
			Settings->MaxRawContributionPerRegion = 100.0; Settings->MaxPlacementRangeCentimeters = 2000.0;
			Settings->MaxAimDeviationDegrees = 60.0; Settings->PlacementLineOfSightChannel = ECC_Visibility;
			Settings->ExpiredCleanupIntervalSeconds = 0.05;
		}
		~FSettingsScope()
		{
			Settings->bEnableChumFieldRuntime = bSavedEnable;
			Settings->MaxActiveFieldsPerRegion = SavedMaxFields;
			Settings->MaxRawContributionPerRegion = SavedMaxContribution;
			Settings->MaxPlacementRangeCentimeters = SavedRange;
			Settings->MaxAimDeviationDegrees = SavedAngle;
			Settings->PlacementLineOfSightChannel = SavedChannel;
			Settings->ExpiredCleanupIntervalSeconds = SavedCleanupInterval;
		}
	};

	struct FFixture
	{
		FSettingsScope Settings;
		FTestWorldWrapper World;
		UCatChumFieldSubsystem* Fields = nullptr;
		ACatWaterRegion* RegionA = nullptr;
		ACatWaterRegion* RegionB = nullptr;
		bool Create(FAutomationTestBase& Test, bool bHole = false)
		{
			if (!World.CreateTestWorld(EWorldType::Game)) return false;
			World.BeginPlayInTestWorld();
			UWorld* TestWorld = World.GetTestWorld();
			RegionA = SpawnRegion(TestWorld, BuildRegion(TEXT("LakeA"), FVector(0,0,100), bHole));
			RegionB = SpawnRegion(TestWorld, BuildRegion(TEXT("LakeB"), FVector(2000,0,100)));
			Fields = TestWorld->GetSubsystem<UCatChumFieldSubsystem>();
			Test.TestNotNull(TEXT("authority field subsystem"), Fields);
			return Fields && RegionA && RegionB;
		}
		FCatPrepareChumFieldResult Prepare(FGuid RequestId, FVector Center, ACatWaterRegion* Region,
			double Time = 10.0, FCatChumInfluenceSpec Influence = MakeInfluence(), FString Identity = TEXT("Player"))
		{
			FCatPrepareChumFieldRequest Request;
			Request.StableNetId = MoveTemp(Identity); Request.Command.RequestId = RequestId;
			Request.Command.ExpectedWaterRegionHandle = Region->GetWaterRegionHandle();
			Request.Command.ChumDefinitionId = TEXT("Chum"); Request.Command.Quantity = 1;
			Request.ServerCorrectedCenter = Center; Request.Influence = MoveTemp(Influence); Request.ServerTime = Time;
			return Fields->PrepareField(Request);
		}
		FCatPlaceChumResult Activate(const FCatPrepareChumFieldResult& Prepared, int64 EquipmentRevision = 1)
		{
			FCatPlaceChumResult Result = Fields->ActivatePreparedFieldDeferred(Prepared.CommitToken, EquipmentRevision);
			Fields->StoreTerminalResult(TEXT("Player"), Result);
			Fields->PublishActivatedField(Result.FieldId);
			return Result;
		}
	};
}

#define CAT_CHUM_FIELD_TEST(ClassName, Suffix) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(ClassName, "Catfishing.Unit.Environment.ChumField." Suffix, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

CAT_CHUM_FIELD_TEST(FCatChumOverlapTest, "SampleAddsOnlyOverlappingActiveFieldsInSameRegion")
CAT_CHUM_FIELD_TEST(FCatChumLandTest, "ExcludeAndLandPointsNeverContribute")
CAT_CHUM_FIELD_TEST(FCatChumFalloffTest, "DistanceTimeFalloffAndExpiryAreDeterministic")
CAT_CHUM_FIELD_TEST(FCatChumStableSumTest, "FieldIdsStabilizeFloatingPointAccumulation")
CAT_CHUM_FIELD_TEST(FCatChumBudgetTest, "BudgetReservationAbortAndActivationAreAtomic")
CAT_CHUM_FIELD_TEST(FCatChumReplayTest, "ReplayDoesNotReserveOrCreateTwice")
CAT_CHUM_FIELD_TEST(FCatChumCleanupTest, "CleanupAdvancesFieldSetRevisionExactlyOnce")
CAT_CHUM_FIELD_TEST(FCatChumTimerTest, "AuthorityTimerCleansExpiredFieldWithoutSampling")
CAT_CHUM_FIELD_TEST(FCatChumRegionRevisionTest, "DistantFieldDoesNotCausePlacementRevisionConflict")

bool FCatChumOverlapTest::RunTest(const FString& Parameters)
{
	(void)Parameters; using namespace CatChumFieldSubsystemTest;
	FFixture F; if (!F.Create(*this)) return false;
	F.Activate(F.Prepare(FGuid::NewGuid(), FVector(200,200,100), F.RegionA));
	F.Activate(F.Prepare(FGuid::NewGuid(), FVector(2200,200,100), F.RegionB));
	const FCatChumSample AtA = F.Fields->SampleChumAtPoint(FVector(200,200,100), F.RegionA->GetWaterRegionHandle(), 10.0);
	TestTrue(TEXT("same-region sample succeeds"), AtA.bSucceeded);
	TestEqual(TEXT("only overlapping same-region field contributes"), AtA.ContributingFieldCount, 1);
	TestEqual(TEXT("contribution value"), AtA.EffectiveChumVector.Fishy, 2.0);
	return !HasAnyErrors();
}

bool FCatChumLandTest::RunTest(const FString& Parameters)
{
	(void)Parameters; using namespace CatChumFieldSubsystemTest;
	FFixture F; if (!F.Create(*this, true)) return false;
	F.Activate(F.Prepare(FGuid::NewGuid(), FVector(300,300,100), F.RegionA));
	TestFalse(TEXT("excluded island sample fails"), F.Fields->SampleChumAtPoint(FVector(500,500,100), F.RegionA->GetWaterRegionHandle(), 10.0).bSucceeded);
	TestFalse(TEXT("land sample fails"), F.Fields->SampleChumAtPoint(FVector(1200,500,100), F.RegionA->GetWaterRegionHandle(), 10.0).bSucceeded);
	return !HasAnyErrors();
}

bool FCatChumFalloffTest::RunTest(const FString& Parameters)
{
	(void)Parameters; using namespace CatChumFieldSubsystemTest;
	FFixture F; if (!F.Create(*this)) return false;
	F.Activate(F.Prepare(FGuid::NewGuid(), FVector(200,200,100), F.RegionA));
	TestEqual(TEXT("half distance and half time multiply"), F.Fields->SampleChumAtPoint(
		FVector(350,200,100), F.RegionA->GetWaterRegionHandle(), 15.0).EffectiveChumVector.Fishy, 0.5);
	TestEqual(TEXT("expiry is half-open"), F.Fields->SampleChumAtPoint(
		FVector(200,200,100), F.RegionA->GetWaterRegionHandle(), 20.0).ContributingFieldCount, 0);
	return !HasAnyErrors();
}

bool FCatChumStableSumTest::RunTest(const FString& Parameters)
{
	(void)Parameters; using namespace CatChumFieldSubsystemTest;
	auto SampleForOrder = [this](bool bReverse)
	{
		FFixture F; F.Create(*this);
		const FGuid Small(0,0,0,1), Medium(0,0,0,2), Large(0,0,0,3);
		const TArray<FGuid> Ids = bReverse ? TArray<FGuid>{Large, Medium, Small} : TArray<FGuid>{Small, Medium, Large};
		const TArray<double> Values = bReverse ? TArray<double>{1.0, 1.0, 1.0e16} : TArray<double>{1.0e16, 1.0, 1.0};
		for (int32 Index = 0; Index < Ids.Num(); ++Index) F.Activate(F.Prepare(Ids[Index], FVector(200,200,100), F.RegionA, 10.0, MakeInfluence(Values[Index])));
		return F.Fields->SampleChumAtPoint(FVector(200,200,100), F.RegionA->GetWaterRegionHandle(), 10.0).EffectiveChumVector.Fishy;
	};
	TestEqual(TEXT("GUID ordering stabilizes accumulation"), SampleForOrder(false), SampleForOrder(true));
	return !HasAnyErrors();
}

bool FCatChumBudgetTest::RunTest(const FString& Parameters)
{
	(void)Parameters; using namespace CatChumFieldSubsystemTest;
	FFixture F; if (!F.Create(*this)) return false;
	F.Settings.Settings->MaxActiveFieldsPerRegion = 1; F.Settings.Settings->MaxRawContributionPerRegion = 3.0;
	const FCatPrepareChumFieldResult First = F.Prepare(FGuid::NewGuid(), FVector(200,200,100), F.RegionA, 10.0, MakeInfluence(2.0));
	const FCatPrepareChumFieldResult Blocked = F.Prepare(FGuid::NewGuid(), FVector(250,200,100), F.RegionA, 10.0, MakeInfluence(2.0));
	TestTrue(TEXT("first pending reserves slot"), First.bPrepared);
	TestEqual(TEXT("pending reservation blocks capacity"), Blocked.Error, ECatChumFieldError::FieldCapacityExceeded);
	F.Fields->AbortPreparedField(First.CommitToken);
	const FCatPrepareChumFieldResult AfterAbort = F.Prepare(FGuid::NewGuid(), FVector(250,200,100), F.RegionA, 10.0, MakeInfluence(2.0));
	TestTrue(TEXT("abort releases capacity and contribution"), AfterAbort.bPrepared);
	TestTrue(TEXT("activation cannot fail after valid prepare"), F.Activate(AfterAbort).bCommitted);
	return !HasAnyErrors();
}

bool FCatChumReplayTest::RunTest(const FString& Parameters)
{
	(void)Parameters; using namespace CatChumFieldSubsystemTest;
	FFixture F; if (!F.Create(*this)) return false;
	const FGuid RequestId = FGuid::NewGuid(); const FCatPlaceChumResult First = F.Activate(F.Prepare(RequestId, FVector(200,200,100), F.RegionA));
	FCatPlaceChumResult Replay; TestTrue(TEXT("terminal stored"), F.Fields->TryGetTerminalResult(TEXT("Player"), RequestId, Replay));
	TestEqual(TEXT("terminal field frozen"), Replay.FieldId, First.FieldId);
	FCatPlaceChumResult Replacement; Replacement.RequestId = RequestId; Replacement.Error = ECatChumFieldError::InvalidPayload;
	F.Fields->StoreTerminalResult(TEXT("Player"), Replacement);
	FCatPlaceChumResult StillFirst; F.Fields->TryGetTerminalResult(TEXT("Player"), RequestId, StillFirst);
	TestEqual(TEXT("terminal is first-result wins"), StillFirst.FieldId, First.FieldId);
	TestEqual(TEXT("replay prepare cannot create second"), F.Prepare(RequestId, FVector(300,300,100), F.RegionA).Error, ECatChumFieldError::AlreadyResolved);
	return !HasAnyErrors();
}

bool FCatChumCleanupTest::RunTest(const FString& Parameters)
{
	(void)Parameters; using namespace CatChumFieldSubsystemTest;
	FFixture F; if (!F.Create(*this)) return false;
	const FCatPlaceChumResult Active = F.Activate(F.Prepare(FGuid::NewGuid(), FVector(200,200,100), F.RegionA));
	TestEqual(TEXT("one expired removed"), F.Fields->CleanupExpiredFields(20.0), 1);
	const FCatChumSample After = F.Fields->SampleChumAtPoint(FVector(200,200,100), F.RegionA->GetWaterRegionHandle(), 20.0);
	TestEqual(TEXT("cleanup advances revision once"), After.ChumFieldSetRevision, Active.ChumFieldSetRevision + 1);
	TestEqual(TEXT("second cleanup removes none"), F.Fields->CleanupExpiredFields(21.0), 0);
	TestEqual(TEXT("second cleanup does not advance"), F.Fields->SampleChumAtPoint(FVector(200,200,100), F.RegionA->GetWaterRegionHandle(), 21.0).ChumFieldSetRevision, After.ChumFieldSetRevision);
	return !HasAnyErrors();
}

bool FCatChumTimerTest::RunTest(const FString& Parameters)
{
	(void)Parameters; using namespace CatChumFieldSubsystemTest;
	FFixture F; F.Settings.Settings->ExpiredCleanupIntervalSeconds = 0.01; if (!F.Create(*this)) return false;
	const double Now = F.World.GetTestWorld()->GetTimeSeconds();
	F.Activate(F.Prepare(FGuid::NewGuid(), FVector(200,200,100), F.RegionA, Now, MakeInfluence(2.0, 300.0, 0.02)));
	int32 Removed = 0; F.Fields->OnFieldRemoved.AddLambda([&Removed](FGuid) { ++Removed; });
	F.World.TickTestWorld(0.1f);
	F.World.TickTestWorld(0.1f);
	TestEqual(TEXT("authority timer removes expiry without sampling"), Removed, 1);
	return !HasAnyErrors();
}

bool FCatChumRegionRevisionTest::RunTest(const FString& Parameters)
{
	(void)Parameters; using namespace CatChumFieldSubsystemTest;
	FFixture F; if (!F.Create(*this)) return false;
	const FCatPlaceChumResult A = F.Activate(F.Prepare(FGuid::NewGuid(), FVector(200,200,100), F.RegionA));
	F.Activate(F.Prepare(FGuid::NewGuid(), FVector(2200,200,100), F.RegionB));
	const FCatChumSample SampleA = F.Fields->SampleChumAtPoint(FVector(200,200,100), F.RegionA->GetWaterRegionHandle(), 10.0);
	TestEqual(TEXT("distant region does not change local field revision"), SampleA.ChumFieldSetRevision, A.ChumFieldSetRevision);
	return !HasAnyErrors();
}

#undef CAT_CHUM_FIELD_TEST

#endif
