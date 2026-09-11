#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "Online/CatOnlineSubsystem.h"
#include "Save/CatSaveSubsystem.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "Misc/Paths.h"

// 仅在隔离的打包副本中显式运行；通过正式入口验证预载、真实旅行、到达与返回，不修改测试外的存档。
class FCatPackagedTravelCommand : public IAutomationLatentCommand
{
public:
	FCatPackagedTravelCommand(FAutomationTestBase* InTest, UGameInstance* InGame)
		: Test(InTest), Game(InGame), Started(FPlatformTime::Seconds()),
		SlotName(TEXT("OnlinePreloadTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)) {}

	virtual bool Update() override
	{
		if (!Game.IsValid() || FPlatformTime::Seconds() - Started > 120.0)
		{
			Test->AddError(FString::Printf(TEXT("Packaged travel timed out at phase %d"), Phase));
			return true;
		}
		UCatSaveSubsystem* Save = Game->GetSubsystem<UCatSaveSubsystem>();
		UCatOnlineSubsystem* Online = Game->GetSubsystem<UCatOnlineSubsystem>();
		if (!Save || !Online) { Test->AddError(TEXT("Missing production subsystems")); return true; }
		if (Save->IsBusy()) { return false; }
		const FCatOnlineSnapshot Snapshot = Online->GetSnapshot();
		switch (Phase)
		{
		case 0:
			Save->RefreshSlotSummaries();
			++Phase;
			break;
		case 1:
			if (!Test->TestTrue(TEXT("Create isolated test slot"), Save->RequestCreateSlot(SlotName).bAccepted)) { return true; }
			++Phase;
			break;
		case 2:
			for (const FCatSaveSlotSummary& Slot : Save->GetSlotSummaries())
			{
				if (Slot.DisplayName == SlotName) { SlotId = Slot.SlotId; break; }
			}
			if (!Test->TestFalse(TEXT("New slot persisted"), SlotId.IsNone())) { return true; }
			if (!Test->TestTrue(TEXT("Load through production save entry"), Save->RequestLoadSlot(SlotId).bAccepted)) { return true; }
			++Phase;
			break;
		case 3:
			if (!Test->TestTrue(TEXT("Loaded save grants travel permission"), Save->HasLoadedRunForTravel())) { return true; }
			if (!Test->TestTrue(TEXT("Create production session"), Online->RequestCreateSession().bAccepted)) { return true; }
			++Phase;
			break;
		case 4:
			if (Snapshot.ActiveOperation != ECatOnlineOperation::None) { return false; }
			if (!Test->TestTrue(TEXT("Start through production frontend entry"), Online->RequestStartHostedGame().bAccepted)) { return true; }
			++Phase;
			break;
		case 5:
			if (Snapshot.ActiveOperation != ECatOnlineOperation::None) { return false; }
			if (!Test->TestEqual(TEXT("Host reached configured gameplay World"), Snapshot.WorldState, ECatOnlineWorldState::Lake)) { return true; }
			Test->TestEqual(TEXT("Host travel completed without error"), Snapshot.LastError, ECatOnlineError::None);
			Test->TestNotNull(TEXT("Gameplay listen driver exists"), Game->GetWorld()->GetNetDriver());
			Test->TestEqual(TEXT("Gameplay is a listen server"), Game->GetWorld()->GetNetMode(), NM_ListenServer);
			if (APlayerController* Controller = Game->GetFirstLocalPlayerController())
			{
				Test->TestTrue(TEXT("Player is possessed in gameplay World"), Controller->GetPawn() != nullptr);
			}
			else { Test->AddError(TEXT("No local player controller after travel")); }
			Arrived = FPlatformTime::Seconds();
			++Phase;
			break;
		case 6:
			if (FPlatformTime::Seconds() - Arrived < 2.0) { return false; }
			{
				ULocalPlayer* Player = Game->GetFirstGamePlayer();
				UCatLocalPlayerUISubsystem* UI = Player ? Player->GetSubsystem<UCatLocalPlayerUISubsystem>() : nullptr;
				if (!Test->TestNotNull(TEXT("Local player UI subsystem exists"), UI)) { return true; }
				const bool bReady = UI->IsGameplayLoadingReadyToDismiss(Snapshot);
				const bool bLoadingVisible = UI->GlobalLoadingScreenWidget && UI->GlobalLoadingScreenWidget->IsInViewport();
				if ((!bReady || bLoadingVisible) && FPlatformTime::Seconds() - Arrived < 15.0) { return false; }
				Test->TestTrue(TEXT("All gameplay UI consumers are ready"), bReady);
				Test->TestFalse(TEXT("Loading overlay has left the viewport"), bLoadingVisible);
				if (bReady && !bLoadingVisible)
				{
					FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/OnlinePreload-Gameplay.png"), true, false);
				}
			}
			Arrived = FPlatformTime::Seconds();
			Phase = 8;
			break;
		case 8:
			// 截图需在后续渲染帧完成，再发离房请求，避免保存到返回前台的遮罩。
			if (FPlatformTime::Seconds() - Arrived < 1.0) { return false; }
			if (!Test->TestTrue(TEXT("Leave through production cleanup entry"), Online->RequestLeave().bAccepted)) { return true; }
			Phase = 7;
			break;
		case 7:
			if (Snapshot.ActiveOperation != ECatOnlineOperation::None) { return false; }
			if (!Test->TestEqual(TEXT("Returned to frontend"), Snapshot.WorldState, ECatOnlineWorldState::Frontend)) { return true; }
			Test->TestEqual(TEXT("Session destroyed on exit"), Snapshot.SessionState, ECatOnlineSessionState::NoSession);
			Test->TestTrue(TEXT("Active test save released"), Save->GetActiveSlotId().IsNone());
			if (!Test->TestTrue(TEXT("Delete only the newly created test slot"), Save->RequestDeleteSlot(SlotId).bAccepted)) { return true; }
			Phase = 9;
			break;
		default:
			Test->AddInfo(TEXT("Packaged production create/load/start/listen/spawn/UI-ready/loading-dismissal/leave cycle completed"));
			return true;
		}
		return false;
	}
private:
	FAutomationTestBase* Test;
	TWeakObjectPtr<UGameInstance> Game;
	double Started;
	double Arrived = 0.0;
	FString SlotName;
	FName SlotId;
	int32 Phase = 0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatOnlinePackagedTravelTest,
	"Catfishing.Online.Preload.PackagedHostEnterAndLeave",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FCatOnlinePackagedTravelTest::RunTest(const FString& Parameters)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("CatOnlineTravelSmoke")))
	{
		AddInfo(TEXT("Skipped: requires -CatOnlineTravelSmoke in an isolated packaged copy"));
		return true;
	}
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::Game && Context.OwningGameInstance)
		{
			ADD_LATENT_AUTOMATION_COMMAND(FCatPackagedTravelCommand(this, Context.OwningGameInstance));
			return true;
		}
	}
	AddError(TEXT("No packaged game World"));
	return false;
}

#endif
