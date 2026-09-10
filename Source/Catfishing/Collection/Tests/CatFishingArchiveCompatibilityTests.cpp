#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Collection/CatRunImprintService.h"
#include "Kismet/GameplayStatics.h"
#include "Profile/CatProfileSaveGame.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingCaptureArchiveRetainedTest,
	"Catfishing.Unit.Collection.FishingArchive.CaptureGrantRemainsIdempotent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingCaptureArchiveRetainedTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper World;
	if (!TestTrue(TEXT("create authority world"), World.CreateTestWorld(EWorldType::Game))) return false;
	World.ForwardErrorMessages(this);
	UCatRunImprintService* Service = World.GetTestWorld()->GetSubsystem<UCatRunImprintService>();
	if (!TestNotNull(TEXT("archive service"), Service)) return false;
	FCatCaptureCommittedResult Capture;
	Capture.CaptureRequestId = FGuid::NewGuid();
	Capture.FishingSessionId = FGuid::NewGuid();
	Capture.FishInstance.FishInstanceId = FGuid::NewGuid();
	Capture.FishInstance.FishDefinitionId = TEXT("Pike");
	Capture.FishInstance.WeightKilograms = 3.25;
	FCatCaptureConditionSnapshot Condition;
	Condition.RegionId = TEXT("River");
	const FGuid GrantId = Service->RecordCommittedCapture(Capture, TEXT("ArchiveTestPlayer"), Condition);
	TestTrue(TEXT("capture still creates a grant"), GrantId.IsValid());
	TestEqual(TEXT("same committed capture reuses its grant"),
		Service->RecordCommittedCapture(Capture, TEXT("ArchiveTestPlayer"), Condition), GrantId);
	TArray<FCatGrantDeliveryRecord> Records;
	Service->CopyGrantDeliveryRecordsForAutomation(Records);
	if (!TestEqual(TEXT("exactly one pending grant"), Records.Num(), 1)) return false;
	TestEqual(TEXT("capture is recorded, not a silhouette"), Records[0].Grant.Kind, ECatProfileGrantKind::FishRecorded);
	TestEqual(TEXT("original fish identity"), Records[0].Grant.FishDefinitionId, Capture.FishInstance.FishDefinitionId);
	TestEqual(TEXT("original weight in kilograms"), Records[0].Grant.WeightKilograms, 3.25);
	TestEqual(TEXT("original capture region"), Records[0].Grant.CaptureCondition.RegionId, Condition.RegionId);
	TestEqual(TEXT("recipient preserved"), Records[0].Grant.RecipientStableNetId, FString(TEXT("ArchiveTestPlayer")));
	TestEqual(TEXT("offline recipient is not falsely acknowledged"), Records[0].Stage, ECatGrantDeliveryStage::Pending);
	TestFalse(TEXT("pending grant still blocks archive completion"), Service->AreAllGrantAcksComplete());
	Service->Deinitialize();
	TestFalse(TEXT("teardown closes new capture grants"), Service->CanRecordCommittedCapture());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingSilhouetteArchiveRetainedTest,
	"Catfishing.Unit.Collection.FishingArchive.SilhouetteSaveAndPendingJournalRemainReadable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingSilhouetteArchiveRetainedTest::RunTest(const FString& Parameters)
{
	// The removed producer must not renumber durable enum values or delete Profile payloads.
	TestEqual(TEXT("silhouette grant keeps its wire value"), static_cast<uint8>(ECatProfileGrantKind::FishSilhouette), uint8(1));
	TestEqual(TEXT("silhouette state keeps its wire value"), static_cast<uint8>(ECatFishCollectionState::Silhouette), uint8(1));
	UCatProfileSaveGame* Original = NewObject<UCatProfileSaveGame>();
	FCatFishCollectionRecord& Fish = Original->FishCollection.AddDefaulted_GetRef();
	Fish.FishDefinitionId = TEXT("Pike");
	Fish.State = ECatFishCollectionState::Silhouette;
	Fish.EncounterCount = 1;
	FCatPendingGrantJournalEntry& Pending = Original->GrantJournal.AddDefaulted_GetRef();
	Pending.Grant.GrantId = FGuid::NewGuid();
	Pending.Grant.Kind = ECatProfileGrantKind::FishSilhouette;
	Pending.Grant.FishDefinitionId = TEXT("ElectricEel");
	Pending.Stage = ECatGrantJournalStage::Pending;
	const FGuid PendingId = Pending.Grant.GrantId;
	TArray<uint8> Bytes;
	if (!TestTrue(TEXT("serialize through actual SaveGame format"), UGameplayStatics::SaveGameToMemory(Original, Bytes))) return false;
	const UCatProfileSaveGame* Reloaded = Cast<UCatProfileSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!TestNotNull(TEXT("reload archived profile"), Reloaded)) return false;
	TestEqual(TEXT("profile schema preserved"), Reloaded->SchemaVersion, 1);
	if (!TestEqual(TEXT("existing fish retained"), Reloaded->FishCollection.Num(), 1)
		|| !TestEqual(TEXT("pending journal retained"), Reloaded->GrantJournal.Num(), 1)) return false;
	TestEqual(TEXT("existing silhouette identity"), Reloaded->FishCollection[0].FishDefinitionId, FName(TEXT("Pike")));
	TestEqual(TEXT("existing silhouette state"), Reloaded->FishCollection[0].State, ECatFishCollectionState::Silhouette);
	TestEqual(TEXT("encounter count retained"), Reloaded->FishCollection[0].EncounterCount, 1);
	TestEqual(TEXT("pending grant identity retained"), Reloaded->GrantJournal[0].Grant.GrantId, PendingId);
	TestEqual(TEXT("pending fish identity retained"), Reloaded->GrantJournal[0].Grant.FishDefinitionId, FName(TEXT("ElectricEel")));
	TestEqual(TEXT("pending silhouette type retained"), Reloaded->GrantJournal[0].Grant.Kind, ECatProfileGrantKind::FishSilhouette);
	TestEqual(TEXT("pending grant remains unacknowledged"), Reloaded->GrantJournal[0].Stage, ECatGrantJournalStage::Pending);
	return !HasAnyErrors();
}

#endif
