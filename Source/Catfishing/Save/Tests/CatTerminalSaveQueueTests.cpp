#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "Save/CatSaveSubsystem.h"
#include "Profile/CatProfileSettings.h"

// 随机测试槽，真实串行异步写入和冷读；只对失败回执注入故障，不碰玩家槽或改产品断言。
class FCatTerminalSaveQueueTestCommand final : public IAutomationLatentCommand
{
public:
	explicit FCatTerminalSaveQueueTestCommand(FAutomationTestBase* InTest) : Test(InTest) {}
	~FCatTerminalSaveQueueTestCommand()
	{
		if (!ProfileTestSlot.IsEmpty()) UGameplayStatics::DeleteGameInSlot(ProfileTestSlot, 0);
	}
	bool Update() override
	{
		if (Started == 0.0) Started = FPlatformTime::Seconds();
		if (FPlatformTime::Seconds() - Started > 45.0)
		{
			Test->AddError(TEXT("Terminal save queue timed out"));
			return true;
		}
		if (Stage == 0)
		{
			if (!World.CreateTestWorld(EWorldType::Game)) return true;
			World.ForwardErrorMessages(Test);
			FURL URL;
			URL.AddOption(TEXT("game=/Script/Catfishing.CatfishingGameModeBase"));
			if (!World.GetTestWorld()->SetGameMode(URL) || !World.BeginPlayInTestWorld()) return true;
			auto* Instance = World.GetTestWorld()->GetGameInstance();
			Player = NewObject<ULocalPlayer>(GEngine);
			{
				auto* Settings = GetMutableDefault<UCatProfileSettings>();
				const FString TestBase = TEXT("Batch7A_Profile_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
				TGuardValue<FString> ProfileSlotGuard(Settings->SaveSlotBaseName, TestBase);
				ProfileTestSlot = TestBase + TEXT("_0");
				Instance->AddLocalPlayer(Player, FPlatformUserId::CreateFromInternalId(0));
			}
			Save = Instance->GetSubsystem<UCatSaveSubsystem>();
			if (!Test->TestNotNull(TEXT("Save subsystem"), Save)) return true;
			SlotId = FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits));
			auto* Seed = CastChecked<UCatRunSaveGame>(ULocalPlayerSaveGame::CreateNewSaveGameForLocalPlayer(
				UCatRunSaveGame::StaticClass(), Player, UCatSaveSubsystem::MakeRunSlotFileName(SlotId)));
			Seed->SlotId = SlotId;
			Seed->DisplayName = TEXT("Batch7A_Original");
			Seed->LastSavedAt = FDateTime::UtcNow();
			if (!Test->TestTrue(TEXT("seed isolated disk slot"), UGameplayStatics::SaveGameToSlot(
				Seed, Seed->GetSaveSlotName(), Player->GetPlatformUserIndex()))) return true;
			Save->ActiveSlotId = SlotId;
			Save->PendingRestoreSaveGame = Seed;
			Save->bWorldRestoreApplied = true;
			Save->bSlotDirectoryLoaded = true;
			auto& Summary = Save->SlotSummaries.AddDefaulted_GetRef();
			Summary.SlotId = SlotId;
			Summary.DisplayName = Seed->DisplayName;
			ReceiptHandle = Save->OnSaveCompleted.AddLambda([this](FGuid Id, bool bSuccess) { Receipts.Add(Id, bSuccess); });
			// 模拟另一个磁盘操作占用，同时排入较新的快照与快照失败的终局标记。
			Save->bBusy = true;
			Seed->DisplayName = TEXT("Batch7A_NewerQueuedSnapshot");
			First = Save->EnqueueRunSave(SlotId, Seed).RequestId;
			auto* Mode = World.GetTestWorld()->GetAuthGameMode<ACatfishingGameModeBase>();
			Mode->RunPublicState.EndReason = ECatRunEndReason::Success;
			Save->PlayerCaptureFailure = FText::FromString(TEXT("Injected snapshot failure"));
			const auto Request = Save->RequestCompleteActiveRun();
			Terminal = Request.RequestId;
			Test->TestTrue(TEXT("terminal accepted while writer busy and snapshot invalid"), Request.bAccepted);
			Test->TestTrue(TEXT("completion fact retained before IO"), Save->PendingCompletionSlots.Contains(SlotId));
			Test->TestFalse(TEXT("queued writes prevent premature release"), Save->ReleaseActiveRun());
			Save->bBusy = false;
			Stage = 1;
			return false;
		}
		if (Save->IsBusy()) return false;
		if (Stage == 1)
		{
			Test->TestTrue(TEXT("each queued request has successful receipt"), Receipts.FindRef(First) && Receipts.FindRef(Terminal));
			auto* Disk = Cast<UCatRunSaveGame>(UGameplayStatics::LoadGameFromSlot(UCatSaveSubsystem::MakeRunSlotFileName(SlotId), Player->GetPlatformUserIndex()));
			if (!Test->TestNotNull(TEXT("cold read actual completed file"), Disk)) return true;
			Test->TestTrue(TEXT("completion actually persisted"), Disk->bRunCompleted);
			Test->TestEqual(TEXT("marker preserved preceding queued snapshot"), Disk->DisplayName, FString(TEXT("Batch7A_NewerQueuedSnapshot")));
			Test->TestFalse(TEXT("completion receipt clears pending intent"), Save->PendingCompletionSlots.Contains(SlotId));
			// 对回执入口注入写失败：载荷跨 Release 保活，然后让正式队列实际重试落盘。
			auto* Retry = CastChecked<UCatRunSaveGame>(ULocalPlayerSaveGame::LoadOrCreateSaveGameForLocalPlayer(
				UCatRunSaveGame::StaticClass(), Player, UCatSaveSubsystem::MakeRunSlotFileName(SlotId)));
			Retry->DisplayName = TEXT("Batch7A_RetriedAfterRelease");
			Save->ActiveQueuedSaveRequest = FGuid::NewGuid();
			Save->ActiveQueuedSaveSlot = SlotId;
			Save->ActiveAsyncRunSaveGame = Retry;
			Save->bBusy = true;
			const FGuid FailedId = Save->ActiveQueuedSaveRequest;
			Save->FinishDiskRequest(FailedId, false, FText::FromString(TEXT("Injected disk failure")));
			Test->TestTrue(TEXT("failed request emits a failure receipt"), Receipts.Contains(FailedId) && !Receipts[FailedId]);
			Test->TestTrue(TEXT("failed write does not prevent release"), Save->ReleaseActiveRun());
			Test->TestTrue(TEXT("retry payload survives release"), Save->RetrySavePayloads.Contains(SlotId));
			Test->TestFalse(TEXT("completed slot cannot continue before retry"), Save->RequestLoadSlot(SlotId).bAccepted);
			Save->NextSaveRetrySeconds = 0.0;
			Save->TickSaveQueue(0.0f);
			Stage = 2;
			return false;
		}
		if (Stage == 2)
		{
			auto* Disk = Cast<UCatRunSaveGame>(UGameplayStatics::LoadGameFromSlot(UCatSaveSubsystem::MakeRunSlotFileName(SlotId), Player->GetPlatformUserIndex()));
			Test->TestTrue(TEXT("retry committed after release without changing completion"), Disk && Disk->bRunCompleted
				&& Disk->DisplayName == TEXT("Batch7A_RetriedAfterRelease"));
			Test->TestFalse(TEXT("successful retry removed retained payload"), Save->RetrySavePayloads.Contains(SlotId));
			Test->TestTrue(TEXT("new run creates independent slot"), Save->RequestCreateSlot(TEXT("Batch7A_IndependentNewSlot")).bAccepted);
			Stage = 3;
			return false;
		}
		if (Stage == 3)
		{
			TArray<FName> TestSlots;
			for (const auto& Summary : Save->SlotSummaries) TestSlots.Add(Summary.SlotId);
			Test->TestEqual(TEXT("new slot does not replace terminal slot"), TestSlots.Num(), 2);
			Save->OnSaveCompleted.Remove(ReceiptHandle);
			for (const FName TestSlot : TestSlots)
				Test->TestTrue(TEXT("player delete entry removes isolated test slot"), Save->RequestDeleteSlot(TestSlot).bAccepted);
			Test->TestFalse(TEXT("delete clears completion retry identity"), Save->CompletedRunSlots.Contains(SlotId));
			return true;
		}
		return false;
	}
private:
	FAutomationTestBase* Test;
	FTestWorldWrapper World;
	UCatSaveSubsystem* Save = nullptr;
	ULocalPlayer* Player = nullptr;
	FName SlotId;
	FString ProfileTestSlot;
	FGuid First, Terminal;
	FDelegateHandle ReceiptHandle;
	TMap<FGuid, bool> Receipts;
	int32 Stage = 0;
	double Started = 0.0;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatTerminalSaveQueueTest,
	"Catfishing.Unit.Save.TerminalSaveQueueDiskReceiptsAndRetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatTerminalSaveQueueTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FCatTerminalSaveQueueTestCommand(this));
	return true;
}
#endif
