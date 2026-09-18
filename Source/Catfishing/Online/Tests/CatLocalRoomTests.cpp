#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Collection/CatRunImprintService.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Online/CatOnlineSubsystem.h"
#include "Save/CatSaveSubsystem.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "UI/Frontend/CatFrontendPageController.h"
#include "UI/Frontend/CatFrontendRoomModel.h"
#include "UI/Frontend/CatFrontendRootWidget.h"
#include "UI/Save/CatLakeMainMenuController.h"
#include "UI/Save/CatLakeMainMenuWidget.h"
#include "Tests/AutomationCommon.h"
#include "UObject/GarbageCollection.h"
#if WITH_EDITOR
#include "Editor.h"
#include "Tests/AutomationEditorCommon.h"
#include "Settings/LevelEditorPlaySettings.h"
#endif

// 显式 -CatLocalRoomSmoke -nosteam 的独立游戏进程；只创建/删除本测试唯一槽。
class FCatLocalTravelCommand : public IAutomationLatentCommand
{
public:
 FCatLocalTravelCommand(FAutomationTestBase* InTest, UGameInstance* InGame)
 : Test(InTest), Game(InGame), Started(FPlatformTime::Seconds()), Name(TEXT("离线回归_") + FGuid::NewGuid().ToString().Left(8)) {}
 // 逐帧驱动正式菜单：创建隔离槽、进入游戏，向不存在的测试接收者生成未确认记录，再点击手动保存并等写入结束。
 // 随后退出，等主菜单根界面入视口且遮罩撤下，再重读存档；阶段未就绪继续等下一帧，失败或超时立即报告。
 virtual bool Update() override
 {
   if (!Game.IsValid())
   {
     for (const FWorldContext& Context : GEngine->GetWorldContexts())
     { if (Context.WorldType == EWorldType::PIE && Context.OwningGameInstance) { Game = Context.OwningGameInstance; break; } }
   }
   if (FPlatformTime::Seconds() - Started > 240)
   { Test->AddError(FString::Printf(TEXT("Local travel timeout phase=%d"), Phase)); return true; }
   if (!Game.IsValid()) { return false; }
   UCatSaveSubsystem* Save = Game->GetSubsystem<UCatSaveSubsystem>();
   UCatOnlineSubsystem* Online = Game->GetSubsystem<UCatOnlineSubsystem>();
   ULocalPlayer* Local = Game->GetFirstGamePlayer();
   UCatLocalPlayerUISubsystem* UI = Local ? Local->GetSubsystem<UCatLocalPlayerUISubsystem>() : nullptr;
   if (!Save || !Online || !UI || Save->IsBusy()) { return false; }
   const auto Snapshot = Online->GetSnapshot();
   if (Snapshot.ActiveOperation != ECatOnlineOperation::None) { return false; }
   switch (Phase)
   {
   case 0:
     if (!UI->FrontendPageController || !UI->FrontendRootWidget) { return false; }
     UI->FrontendPageController->RequestStartGameFlow(); ++Phase; break;
   case 1:
     if (!Test->TestTrue(TEXT("Create unique real slot"), Save->RequestCreateSlot(Name).bAccepted)) { return true; }
     ++Phase; break;
   case 2:
     for (const auto& Slot : Save->GetSlotSummaries()) { if (Slot.DisplayName == Name) { SlotId = Slot.SlotId; } }
     if (!Test->TestFalse(TEXT("Unique slot persisted"), SlotId.IsNone())) { return true; }
     UI->FrontendPageController->RequestSelectSaveSlot(SlotId);
     UI->FrontendPageController->RequestLoadSelectedSaveSlot(); ++Phase; break;
   case 3:
     if (!Snapshot.bLocalRoomActive) { return false; }
     Test->TestEqual(TEXT("Local room has no platform session"), Snapshot.SessionState, ECatOnlineSessionState::NoSession);
     Test->TestEqual(TEXT("Local room is not a Steam host"), Snapshot.SessionRole, ECatOnlineSessionRole::None);
     Test->TestTrue(TEXT("No fabricated lobby"), Snapshot.LobbyId.IsEmpty() && Snapshot.InviteCode.IsEmpty());
     Test->TestEqual(TEXT("Exactly one local member"), Snapshot.RoomMembers.Num(), 1);
     Test->TestNull(TEXT("Preparing a save never starts listening"), Game->GetWorld()->GetNetDriver());
     if (!Test->TestTrue(TEXT("Formal room page is visible"), UI->FrontendRootWidget->IsShowingRoom())) { return true; }
     {
       UUserWidget* Room = Cast<UUserWidget>(UI->FrontendRootWidget->GetWidgetFromName(TEXT("RoomPage")));
       UButton* Enable = Room ? Cast<UButton>(Room->GetWidgetFromName(TEXT("OpenRoomInviteButton"))) : nullptr;
       if (!Test->TestNotNull(TEXT("Formal online button exists"), Enable)) { return true; }
       const auto* Label = Cast<UTextBlock>(Enable->GetContent());
       Test->TestTrue(TEXT("Offline button says enable online"), Label && Label->GetText().ToString() == TEXT("开启联机"));
       Enable->OnClicked.Broadcast();
       Test->TestEqual(TEXT("No Steam gives a specific error"), Online->GetSnapshot().LastError, ECatOnlineError::OnlineHostingUnavailable);
       Test->TestTrue(TEXT("Online failure preserves loaded save"), Save->HasLoadedRunForTravel());
       Test->TestTrue(TEXT("Online failure preserves solo start"), UI->FrontendRoomModel->CanStartGame());
       Arrived = FPlatformTime::Seconds();
     }
     ++Phase; break;
   case 4:
     if (FPlatformTime::Seconds() - Arrived < 5) { return false; }
     FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/LocalRoom-Prepare.png"), true, false);
     Phase = 40; break;
   case 40:
     // 真实退出准备页，再读同一槽，验证本地载荷释放而不是伪造 Session Destroy。
     {
       auto* Room = Cast<UUserWidget>(UI->FrontendRootWidget->GetWidgetFromName(TEXT("RoomPage")));
       auto* Leave = Room ? Cast<UButton>(Room->GetWidgetFromName(TEXT("LeaveRoomButton"))) : nullptr;
       if (!Test->TestNotNull(TEXT("Formal leave button exists"), Leave)) { return true; }
       Leave->OnClicked.Broadcast();
     }
     Phase = 5; break;
   case 5:
     Test->TestFalse(TEXT("Leave removes local room"), Snapshot.bLocalRoomActive);
     Test->TestTrue(TEXT("Leave releases loaded payload"), Save->GetActiveSlotId().IsNone());
     UI->FrontendPageController->RequestSelectSaveSlot(SlotId);
     UI->FrontendPageController->RequestLoadSelectedSaveSlot(); ++Phase; break;
   case 6:
     if (!Snapshot.bLocalRoomActive) { return false; }
     UI->FrontendPageController->RequestStartRoomGame(); ++Phase; break;
   case 7:
     if (!Test->TestEqual(TEXT("Actual travel reaches gameplay"), Snapshot.WorldState, ECatOnlineWorldState::Lake)) { return true; }
     Test->TestEqual(TEXT("Travel stays standalone"), Game->GetWorld()->GetNetMode(), NM_Standalone);
     Test->TestNull(TEXT("Gameplay has no network driver"), Game->GetWorld()->GetNetDriver());
     {
       APlayerController* PC = Game->GetFirstLocalPlayerController();
       ACatfishingGameModeBase* Mode = Game->GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>();
       if (!Test->TestTrue(TEXT("Offline player spawned and can play"), PC && PC->GetPawn() && Mode && Mode->CanAcceptGameplayCommand(PC))) { return true; }
       Test->TestTrue(TEXT("Local identity activated"), PC->PlayerState && PC->PlayerState->GetUniqueId().IsValid()
         && PC->PlayerState->GetUniqueId()->GetType() == FName(TEXT("CAT_LOCAL")));
     }
     Arrived = FPlatformTime::Seconds(); ++Phase; break;
   case 8:
     if (FPlatformTime::Seconds() - Arrived < 5) { return false; }
     if ((!UI->IsGameplayLoadingReadyToDismiss(Snapshot) || (UI->GlobalLoadingScreenWidget && UI->GlobalLoadingScreenWidget->IsInViewport()))
       && FPlatformTime::Seconds() - Arrived < 25) { return false; }
     Test->TestTrue(TEXT("Actual gameplay UI readiness"), UI->IsGameplayLoadingReadyToDismiss(Snapshot));
     Test->TestFalse(TEXT("Actual loading overlay dismissed"), UI->GlobalLoadingScreenWidget && UI->GlobalLoadingScreenWidget->IsInViewport());
     FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/LocalRoom-Gameplay.png"), true, false);
     ++Phase; break;
   case 9:
     if (!Test->TestNotNull(TEXT("In-game menu controller exists"), UI->LakeMainMenuController.Get())) { return true; }
     UI->LakeMainMenuController->ToggleMenu();
     Phase = 90; break;
   case 90:
     // 只向不存在的测试接收者生成未确认记录，不写任何真实玩家档案；完整退出链必须仍能返回。
     {
       UCatRunImprintService* Imprint = Game->GetWorld()->GetSubsystem<UCatRunImprintService>();
       if (!Test->TestNotNull(TEXT("Gameplay imprint service exists"), Imprint)) { return true; }
       if (!Test->TestTrue(TEXT("Real pending record created before manual save"),
         Imprint->RecordCommittedUnlock(TEXT("ExitRegressionOnly"), TEXT("ExitRegressionAbsentRecipient")).IsValid())) { return true; }
       Test->TestEqual(TEXT("Pending record has no fabricated ACK"), Imprint->GetPendingGrantAckCount(), 1);
       Test->AddExpectedMessage(TEXT("Event=run_teardown_unconfirmed_grants"), EAutomationExpectedMessageFlags::Contains, 1);
     }
     if (!Test->TestNotNull(TEXT("Formal in-game menu exists before save"), UI->LakeMainMenuWidget.Get())) { return true; }
     {
       auto* SaveButton = Cast<UButton>(UI->LakeMainMenuWidget->GetWidgetFromName(TEXT("SaveButton")));
       if (!Test->TestNotNull(TEXT("Formal save button exists"), SaveButton)) { return true; }
       Test->TestTrue(TEXT("Manual save button enabled"), SaveButton->GetIsEnabled());
       SaveButton->OnClicked.Broadcast();
       if (!Test->TestTrue(TEXT("Manual save queued"), Save->IsBusy())) { return true; }
     }
     Phase = 91; break;
   case 91:
     if (Save->IsBusy()) { return false; }
     if (!Test->TestNotNull(TEXT("Formal in-game menu exists"), UI->LakeMainMenuWidget.Get())) { return true; }
     {
       auto* Return = Cast<UButton>(UI->LakeMainMenuWidget->GetWidgetFromName(TEXT("ReturnToMainMenuButton")));
       if (!Test->TestNotNull(TEXT("Formal return button exists"), Return)) { return true; }
       Test->TestTrue(TEXT("Offline return button enabled"), Return->GetIsEnabled());
       Return->OnClicked.Broadcast();
     }
     if (!Test->TestEqual(TEXT("Return click submits the leave operation"), Online->GetSnapshot().ActiveOperation, ECatOnlineOperation::Leave)) { return true; }
     Phase = 10; break;
   case 10:
     if (!Test->TestEqual(TEXT("Save and return reaches frontend"), Snapshot.WorldState, ECatOnlineWorldState::Frontend)) { return true; }
     // 地图到达不等于玩家可操作；等待正式根界面入视口且遮罩真实撤下，再继续重读存档。
     if (!UI->IsFrontendLoadingReadyToDismiss(Snapshot)
       || (UI->GlobalLoadingScreenWidget && UI->GlobalLoadingScreenWidget->IsInViewport())) { return false; }
     Test->TestTrue(TEXT("Returned frontend root visible"), UI->FrontendRootWidget && UI->FrontendRootWidget->IsInViewport());
     FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/LocalRoom-Returned.png"), true, false);
     Test->TestFalse(TEXT("Return clears local room"), Snapshot.bLocalRoomActive);
     Test->TestTrue(TEXT("Return releases save"), Save->GetActiveSlotId().IsNone());
     Test->TestNull(TEXT("Return has no driver"), Game->GetWorld()->GetNetDriver());
     if (!Test->TestTrue(TEXT("Reopen saved offline run"), Save->RequestLoadSlot(SlotId).bAccepted)) { return true; }
     ++Phase; break;
   case 11:
     Test->TestTrue(TEXT("Saved run can travel again"), Save->HasLoadedRunForTravel());
     if (const auto* Slot = Save->GetSlotSummaries().FindByPredicate([this](const auto& S) { return S.SlotId == SlotId; }))
     { Test->TestTrue(TEXT("Offline world progress persisted"), Slot->DayIndex > 0); }
     else { Test->AddError(TEXT("Saved summary missing")); }
     Save->ReleaseActiveRun();
     Test->TestTrue(TEXT("Delete only the generated test slot"), Save->RequestDeleteSlot(SlotId).bAccepted);
     ++Phase; break;
   case 12:
     // 旅行本身结束不代表闲置地图的子系统已收尾；在同一用例中回收，不能把 ensure 留给下一项测试。
     CollectGarbage(RF_NoFlags, true);
     ++Phase; break;
   default: Test->AddInfo(TEXT("Offline prepare/promotion failure/cancel/reload/start/spawn/UI/save/return/reload completed")); return true;
   }
   return false;
 }
private:
 FAutomationTestBase* Test;
 TWeakObjectPtr<UGameInstance> Game;
 double Started, Arrived = 0;
 FString Name; FName SlotId; int32 Phase = 0;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatLocalRoomTravelTest, "Catfishing.Online.LocalRoom.Travel",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FCatLocalRoomTravelTest::RunTest(const FString& Parameters)
{
 if (!FParse::Param(FCommandLine::Get(), TEXT("CatLocalRoomSmoke")) || !FParse::Param(FCommandLine::Get(), TEXT("nosteam")))
 { AddInfo(TEXT("Skipped: requires isolated process with -CatLocalRoomSmoke -nosteam")); return true; }
 // 精确隔离当前正式设置资产的九个既有缺口；不把这些资产视为本轮完成。
 for (const TCHAR* Control : {TEXT("AccessibilitySettingsCategoryButton"), TEXT("TextSizeSlider"), TEXT("HighContrastCheckBox"),
   TEXT("ColorBlindModeComboBox"), TEXT("ReduceCameraShakeCheckBox"), TEXT("ReduceFlashingEffectsCheckBox"),
   TEXT("MouseSensitivitySlider"), TEXT("CameraSensitivitySlider"), TEXT("InvertYAxisCheckBox")})
 {
   AddExpectedError(FString::Printf(TEXT("Event=frontend_widget_contract_missing Page=FrontendSettingsPage Control=%s Reason=missing_or_wrong_type"), Control), EAutomationExpectedErrorFlags::Contains, 0);
 }
#if WITH_EDITOR
 if (GIsEditor && GEditor)
 {
   auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
   EPlayNetMode OldMode; int32 OldCount; bool OldOneProcess;
   Settings->GetPlayNetMode(OldMode); Settings->GetPlayNumberOfClients(OldCount); Settings->GetRunUnderOneProcess(OldOneProcess);
   Settings->SetPlayNetMode(PIE_Standalone); Settings->SetPlayNumberOfClients(1); Settings->SetRunUnderOneProcess(true);
   FAutomationEditorCommonUtils::LoadMap(TEXT("/Game/Catfishing/Maps/Frontend"));
   ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
   ADD_LATENT_AUTOMATION_COMMAND(FCatLocalTravelCommand(this, nullptr));
   ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
   ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]()
   {
     CollectGarbage(RF_NoFlags, true);
     return true;
   }));
   ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([OldMode, OldCount, OldOneProcess]()
   {
     auto* Restore = GetMutableDefault<ULevelEditorPlaySettings>();
     Restore->SetPlayNetMode(OldMode); Restore->SetPlayNumberOfClients(OldCount); Restore->SetRunUnderOneProcess(OldOneProcess);
     return true;
   }));
   return true;
 }
#endif
 for (const FWorldContext& Context : GEngine->GetWorldContexts())
 {
   if (Context.WorldType == EWorldType::Game && Context.OwningGameInstance)
   { ADD_LATENT_AUTOMATION_COMMAND(FCatLocalTravelCommand(this, Context.OwningGameInstance)); return true; }
 }
 AddError(TEXT("No standalone game World")); return false;
}
#endif
