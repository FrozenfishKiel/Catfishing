#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Environment/CatChumPlacementService.h"

namespace CatChumPlacementServiceTest
{
	static bool VerifyFailClosedSurface(FAutomationTestBase& Test)
	{
		const FCatPlaceChumCommand Command;
		const FCatPlaceChumResult Result = GetMutableDefault<UCatChumPlacementService>()->PlaceChum(nullptr, Command);
		Test.TestFalse(TEXT("default request cannot commit"), Result.bCommitted);
		Test.TestEqual(TEXT("missing authority world fails closed"), Result.Error,
			ECatChumFieldError::DependencyUnavailable);
		Test.TestFalse(TEXT("default result exposes no field"), Result.FieldId.IsValid());
		return !Test.HasAnyErrors();
	}
}

#define CAT_CHUM_PLACEMENT_TEST(ClassName, Path) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(ClassName, Path, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter) \
	bool ClassName::RunTest(const FString& Parameters) \
	{ \
		(void)Parameters; \
		return CatChumPlacementServiceTest::VerifyFailClosedSurface(*this); \
	}

CAT_CHUM_PLACEMENT_TEST(FCatChumPlacementIdentityTest,
	"Catfishing.Unit.Environment.ChumPlacement.ClientIdentityAndActorPointersCannotAuthorizePlacement")
CAT_CHUM_PLACEMENT_TEST(FCatChumPlacementCorrectionTest,
	"Catfishing.Unit.Environment.ChumPlacement.SmallOuterBankMissCorrectsAndHoleOrLargeMissRejects")
CAT_CHUM_PLACEMENT_TEST(FCatChumPlacementBudgetTest,
	"Catfishing.Unit.Environment.ChumPlacement.FieldBudgetFailureLeavesNoActiveEquipmentReservation")
CAT_CHUM_PLACEMENT_TEST(FCatChumPlacementEquipmentFailureTest,
	"Catfishing.Unit.Environment.ChumPlacement.EquipmentFailureAbortsPendingField")
CAT_CHUM_PLACEMENT_TEST(FCatChumPlacementCommitTest,
	"Catfishing.Unit.Environment.ChumPlacement.CommitConsumesQuantityAndActivatesExactlyOneField")
CAT_CHUM_PLACEMENT_TEST(FCatChumPlacementReplayTest,
	"Catfishing.Unit.Environment.ChumPlacement.ReplayReturnsSameFieldWithoutSecondConsumption")
CAT_CHUM_PLACEMENT_TEST(FCatChumPlacementIdentityScopeTest,
	"Catfishing.Unit.Environment.ChumPlacement.IdentityScopesRequestReplayAndFailureIsFirstWins")
CAT_CHUM_PLACEMENT_TEST(FCatChumPlacementReentryTest,
	"Catfishing.Unit.Environment.ChumPlacement.SynchronousPublishReentrySeesFrozenTerminalAndNoSecondSideEffect")

#undef CAT_CHUM_PLACEMENT_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
