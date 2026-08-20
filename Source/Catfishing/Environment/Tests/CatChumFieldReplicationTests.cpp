#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Components/InstancedStaticMeshComponent.h"
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
	ACatChumFieldPresentationActor* Presentation = GetMutableDefault<ACatChumFieldPresentationActor>();
	TestFalse(TEXT("presentation fallback is local-only"), Presentation->GetIsReplicated());
	const UInstancedStaticMeshComponent* Ring = Cast<UInstancedStaticMeshComponent>(
		Presentation->GetDefaultSubobjectByName(TEXT("PlaceholderRing")));
	const UInstancedStaticMeshComponent* Specks = Cast<UInstancedStaticMeshComponent>(
		Presentation->GetDefaultSubobjectByName(TEXT("PlaceholderSpecks")));
	TestNotNull(TEXT("presentation fallback owns a procedural ring"), Ring);
	TestNotNull(TEXT("presentation fallback owns deterministic chum specks"), Specks);
	if (Ring && Specks)
	{
		TestEqual(TEXT("ring collision is disabled"), Ring->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		TestEqual(TEXT("speck collision is disabled"), Specks->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	}

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("create chum presentation game world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	if (UWorld* World = WorldWrapper.GetTestWorld())
	{
		ACatChumFieldPresentationActor* First = World->SpawnActor<ACatChumFieldPresentationActor>();
		ACatChumFieldPresentationActor* Second = World->SpawnActor<ACatChumFieldPresentationActor>();
		FCatChumFieldPublicItem State;
		State.FieldId = FGuid(1, 2, 3, 4);
		State.CenterWorldPoint = FVector(200.0, 300.0, 400.0);
		State.RadiusCentimeters = 500.0;
		First->ApplyPublicState(State, true);
		Second->ApplyPublicState(State, true);
		UInstancedStaticMeshComponent* FirstRing = Cast<UInstancedStaticMeshComponent>(
			First->GetDefaultSubobjectByName(TEXT("PlaceholderRing")));
		UInstancedStaticMeshComponent* SecondRing = Cast<UInstancedStaticMeshComponent>(
			Second->GetDefaultSubobjectByName(TEXT("PlaceholderRing")));
		UInstancedStaticMeshComponent* FirstSpecks = Cast<UInstancedStaticMeshComponent>(
			First->GetDefaultSubobjectByName(TEXT("PlaceholderSpecks")));
		UInstancedStaticMeshComponent* SecondSpecks = Cast<UInstancedStaticMeshComponent>(
			Second->GetDefaultSubobjectByName(TEXT("PlaceholderSpecks")));
		TestTrue(TEXT("same radius creates ring instances"), FirstRing && FirstRing->GetInstanceCount() > 0);
		TestTrue(TEXT("same field creates speck instances"), FirstSpecks && FirstSpecks->GetInstanceCount() > 0);
		if (FirstRing && SecondRing && FirstSpecks && SecondSpecks)
		{
			TestEqual(TEXT("same radius creates equal ring counts"), FirstRing->GetInstanceCount(), SecondRing->GetInstanceCount());
			TestEqual(TEXT("same FieldId creates equal speck counts"), FirstSpecks->GetInstanceCount(), SecondSpecks->GetInstanceCount());
			FTransform FirstTransform;
			FTransform SecondTransform;
			FirstSpecks->GetInstanceTransform(0, FirstTransform, false);
			SecondSpecks->GetInstanceTransform(0, SecondTransform, false);
			TestTrue(TEXT("same replicated FieldId creates deterministic local speck layout"),
				FirstTransform.Equals(SecondTransform));
		}
	}
	return !HasAnyErrors();
}

#endif
