#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Environment/CatChumFieldReplicationComponent.h"
#include "Environment/Presentation/CatChumFieldPresentationActor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatChumFieldPublicReplicationContractTest,
	"Catfishing.Environment.ChumField.PublicReplicationContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCatChumFieldPublicReplicationContractTest::RunTest(const FString& Parameters)
{
	const FCatChumFieldPublicItem EmptyItem;
	TestFalse(TEXT("Default public item has no field identity"), EmptyItem.FieldId.IsValid());
	TestTrue(TEXT("Replication component is available to GameState"),
		UCatChumFieldReplicationComponent::StaticClass()->HasAnyClassFlags(CLASS_Native));
	TestTrue(TEXT("Presentation actor remains a native Blueprint extension point"),
		ACatChumFieldPresentationActor::StaticClass()->HasAnyClassFlags(CLASS_Native));
	return true;
}

#endif
