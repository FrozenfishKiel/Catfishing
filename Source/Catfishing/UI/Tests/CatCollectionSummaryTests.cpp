#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Profile/CatProfileSubsystem.h"
#include "Profile/CatProfileSaveGame.h"
#include "Profile/CatProfileSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "UI/CatUISettings.h"
#include "UI/Collection/CatCollectionModel.h"
#include "UI/Collection/CatCollectionWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatCollectionSummaryTest,
	"Catfishing.Unit.UI.CollectionSummaryPreservesRecordsWithoutCompletionCounts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatCollectionSummaryTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
	auto* Instance = Wrapper.GetTestWorld()->GetGameInstance();
	auto* Player = NewObject<ULocalPlayer>(GEngine);
	const FString TestBase = TEXT("Batch7A_Collection_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString TestSlot = TestBase + TEXT("_0");
	ON_SCOPE_EXIT { UGameplayStatics::DeleteGameInSlot(TestSlot, 0); };
	auto* Seed = NewObject<UCatProfileSaveGame>();
	auto& ArchivedFish = Seed->FishCollection.AddDefaulted_GetRef();
	ArchivedFish.FishDefinitionId = TEXT("Batch7AArchivedFish");
	ArchivedFish.bRecordedUnlocked = true;
	ArchivedFish.BestWeightKilograms = 9.5;
	Seed->Imprints.AddDefaulted_GetRef().ImprintId = FGuid::NewGuid();
	if (!TestTrue(TEXT("seed isolated profile with page and album record"), UGameplayStatics::SaveGameToSlot(Seed, TestSlot, 0))) return false;
	{
		TGuardValue<FString> ProfileSlotGuard(GetMutableDefault<UCatProfileSettings>()->SaveSlotBaseName, TestBase);
		Instance->AddLocalPlayer(Player, FPlatformUserId::CreateFromInternalId(0));
	}
	auto* Profile = Player->GetSubsystem<UCatProfileSubsystem>();
	auto* Model = NewObject<UCatCollectionModel>();
	if (!TestNotNull(TEXT("read-only profile source"), Profile) || !TestTrue(TEXT("model binds profile"), Model->Bind(Player))) return false;
	const auto& View = Model->GetViewState();
	if (!TestTrue(TEXT("available summary branch exercised"), View.bAvailable)) return false;
	const FString Summary = View.SummaryText.ToString();
	TestFalse(TEXT("summary contains no completion fraction or photo count"), Summary.Contains(TEXT("/")) || Summary.Contains(TEXT("张")) || Summary.Contains(TEXT("已收集")));
	for (const TCHAR Character : Summary) TestFalse(TEXT("summary contains no numerical completion metric"), FChar::IsDigit(Character));
	TArray<FCatFishCollectionRecord> Records;
	TArray<FCatLocalImprintRecord> Imprints;
	Profile->GetFishCollectionSnapshot(Records);
	Profile->GetLocalImprintSnapshot(Imprints);
	TestEqual(TEXT("album entries retained as records"), View.Imprints.Num(), Imprints.Num());
	for (const auto& Record : Records)
	{
		const auto* Entry = View.Entries.FindByPredicate([&](const auto& Candidate) { return Candidate.FishDefinitionId == Record.FishDefinitionId; });
		if (!TestNotNull(TEXT("existing page retained"), Entry)) return false;
		TestEqual(TEXT("personal best retained"), Entry->BestWeightKilograms, Record.BestWeightKilograms);
	}
	const auto WidgetClass = GetDefault<UCatUISettings>()->LoadCollectionWidgetClass();
	if (!TestNotNull(TEXT("formal collection widget loads"), WidgetClass.Get())) return false;
	auto* Widget = NewObject<UCatCollectionWidget>(Wrapper.GetTestWorld(), WidgetClass);
	Widget->RenderCollection(View);
	TestEqual(TEXT("widget consumes model summary"), Widget->GetLastCollectionViewState().SummaryText.ToString(), Summary);
	TestEqual(TEXT("widget keeps existing pages"), Widget->GetLastCollectionViewState().Entries.Num(), View.Entries.Num());
	Model->Unbind();
	return !HasAnyErrors();
}
#endif
