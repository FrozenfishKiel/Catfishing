#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Profile/CatProfileSubsystem.h"
#include "Profile/CatProfileSaveGame.h"
#include "Profile/CatCollectionSaveGame.h"
#include "Profile/CatProfileSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "UI/CatUISettings.h"
#include "UI/Collection/CatCollectionModel.h"
#include "UI/Collection/CatCollectionWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatCollectionSummaryTest,
	"Catfishing.Unit.UI.CollectionSummaryPreservesRecordsWithoutCompletionCounts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
// 先留下无账号归属的本地旧记录，再验证新图鉴不自动认领；授予后检查去重、追踪、重载与独立相册保留。
bool FCatCollectionSummaryTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
	auto* Instance = Wrapper.GetTestWorld()->GetGameInstance();
	auto* Player = NewObject<ULocalPlayer>(GEngine);
	const FString TestBase = TEXT("Batch7A_Collection_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString TestSlot = TestBase + TEXT("_0");
	const FString Account = FString::Printf(TEXT("Editor_0_%08x"), GetTypeHash(TestBase));
	const FString CollectionSlot = TEXT("CatCollection/Development/") + Account + TEXT("/Collection_v1");
	ON_SCOPE_EXIT { UGameplayStatics::DeleteGameInSlot(TestSlot, 0); UGameplayStatics::DeleteGameInSlot(CollectionSlot, 0); };
	auto* Seed = NewObject<UCatProfileSaveGame>();
	auto& ArchivedFish = Seed->FishCollection.AddDefaulted_GetRef();
	ArchivedFish.ItemId = 1771238;
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
	TestTrue(TEXT("旧本地鱼记录不自动归入新账号"), Records.IsEmpty());
	TestEqual(TEXT("独立相册记录保留"), Imprints.Num(), 1);
	for (const auto& Entry : View.Entries)
	{
		TestFalse(TEXT("未捕获卡片保持锁定"), Entry.bRecordedUnlocked);
		TestEqual(TEXT("未知鱼名不泄露"), Entry.DisplayName.ToString(), FString(TEXT("？？？")));
		TestEqual(TEXT("未知鱼饵不泄露"), Entry.BaitPreferenceText.ToString(), FString(TEXT("？？？")));
		TestEqual(TEXT("未知窝料不泄露"), Entry.ChumPreferenceText.ToString(), FString(TEXT("？？？")));
	}
	TestFalse(TEXT("未捕获鱼不能追踪"), Model->SetTrackedFish(3));
	FCatProfileGrant Grant;
	Grant.GrantId = FGuid::NewGuid();
	Grant.Kind = ECatProfileGrantKind::FishKnowledge;
	Grant.ItemId = 3;
	TestTrue(TEXT("食用知识成功保存"), Profile->ApplyGrant(Grant).bAckAllowed);
	TestFalse(TEXT("食用知识不开放追踪"), Model->SetTrackedFish(3));
	Grant.GrantId = FGuid::NewGuid();
	Grant.Kind = ECatProfileGrantKind::FishRecorded;
	Grant.WeightKilograms = 2.5;
	TestTrue(TEXT("成功捕获授予落盘"), Profile->ApplyGrant(Grant).bAckAllowed);
	TestTrue(TEXT("重复授予仍允许回执"), Profile->ApplyGrant(Grant).bAckAllowed);
	Profile->GetFishCollectionSnapshot(Records);
	TestEqual(TEXT("知识与捕获汇入同一条鱼记录"), Records.Num(), 1);
	if (Records.Num() == 1) TestEqual(TEXT("重投递不重复计数"), Records[0].EncounterCount, 1);
	TestTrue(TEXT("捕获后允许追踪"), Model->SetTrackedFish(3));
	const auto* Durable = Cast<UCatCollectionSaveGame>(UGameplayStatics::LoadGameFromSlot(CollectionSlot, 0));
	if (TestNotNull(TEXT("专用图鉴文件存在"), Durable))
	{
		TestEqual(TEXT("追踪只保存数字身份"), Durable->TrackedItemId, 3);
		TestEqual(TEXT("文件归属开发账号"), Durable->AccountKey, Account);
	}
	Model->Unbind();
	Instance->RemoveLocalPlayer(Player);
	Player = NewObject<ULocalPlayer>(GEngine);
	{
		TGuardValue<FString> ProfileSlotGuard(GetMutableDefault<UCatProfileSettings>()->SaveSlotBaseName, TestBase);
		Instance->AddLocalPlayer(Player, FPlatformUserId::CreateFromInternalId(0));
	}
	Profile = Player->GetSubsystem<UCatProfileSubsystem>();
	TestTrue(TEXT("重建玩家后读取同一档案"), Model->Bind(Player));
	TestEqual(TEXT("重进恢复追踪"), Profile->GetTrackedFish(), 3);
	TestTrue(TEXT("取消追踪成功"), Model->SetTrackedFish(0));
	TestEqual(TEXT("取消后不保留目标"), Profile->GetTrackedFish(), 0);
	TestTrue(TEXT("图鉴数据就绪"), View.bAvailable);
	for (const auto& Record : Records)
	{
		const auto* Entry = View.Entries.FindByPredicate([&](const auto& Candidate) { return Candidate.ItemId == Record.ItemId; });
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatCollectionRejectedArchiveTest,
	"Catfishing.Unit.UI.CollectionUnknownVersionPreservesOriginalFile", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 为独立开发账号写未知版本文件，再从真实 LocalPlayer 初始化加载；失败不得新建空档或允许授予覆盖。
bool FCatCollectionRejectedArchiveTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper World;
	if (!World.CreateTestWorld(EWorldType::Game)) return false;
	const FString Base = TEXT("RejectedCollection_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Account = FString::Printf(TEXT("Editor_0_%08x"), GetTypeHash(Base));
	const FString Slot = TEXT("CatCollection/Development/") + Account + TEXT("/Collection_v1");
	ON_SCOPE_EXIT { UGameplayStatics::DeleteGameInSlot(Slot, 0); UGameplayStatics::DeleteGameInSlot(Base + TEXT("_0"), 0); };
	auto* Seed = NewObject<UCatCollectionSaveGame>();
	Seed->SchemaVersion = 99;
	Seed->AccountKey = Account;
	TArray<uint8> Before, After;
	if (!UGameplayStatics::SaveGameToMemory(Seed, Before) || !UGameplayStatics::SaveDataToSlot(Before, Slot, 0)) return false;
	AddExpectedError(TEXT("Event=collection_account_load_rejected"), EAutomationExpectedErrorFlags::Contains, 1);
	auto* Player = NewObject<ULocalPlayer>(GEngine);
	{
		TGuardValue<FString> Guard(GetMutableDefault<UCatProfileSettings>()->SaveSlotBaseName, Base);
		World.GetTestWorld()->GetGameInstance()->AddLocalPlayer(Player, FPlatformUserId::CreateFromInternalId(0));
	}
	auto* Profile = Player->GetSubsystem<UCatProfileSubsystem>();
	if (!TestNotNull(TEXT("档案子系统已初始化"), Profile)) return false;
	TArray<FCatFishCollectionRecord> Records;
	TestFalse(TEXT("未知版本不能呈现为空账号"), Profile->GetFishCollectionSnapshot(Records));
	FCatProfileGrant Grant;
	Grant.GrantId = FGuid::NewGuid();
	Grant.ItemId = 3;
	Grant.Kind = ECatProfileGrantKind::FishRecorded;
	Grant.WeightKilograms = 2.0;
	TestFalse(TEXT("未知档案不能接受捕获回执"), Profile->ApplyGrant(Grant).bAckAllowed);
	TestFalse(TEXT("未知档案不能改追踪"), Profile->SetTrackedFish(3));
	TestTrue(TEXT("原文件仍可读取"), UGameplayStatics::LoadDataFromSlot(After, Slot, 0));
	TestTrue(TEXT("失败后文件逐字节保留"), Before == After);
	return !HasAnyErrors();
}
#endif
