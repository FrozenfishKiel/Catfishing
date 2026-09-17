#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/WidgetSwitcher.h"
#include "Online/CatOnlineSubsystem.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "UI/Frontend/CatFrontendRoomModel.h"
#include "UI/Frontend/CatFrontendPageController.h"
#include "UI/Frontend/CatFrontendRootWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatJoinSearchInteractionTest, "Catfishing.UI.Frontend.JoinSearchInteraction",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCatJoinSearchInteractionTest::RunTest(const FString& Parameters)
{
 const auto Fixture = MakeShared<FTestWorldWrapper>();
 if (!Fixture->CreateTestWorld(EWorldType::Game)) { return false; }
 UWorld* World = Fixture->GetTestWorld();
 World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
 if (!Fixture->BeginPlayInTestWorld()) { return false; }
 UGameInstance* Game = World->GetGameInstance();
 UCatOnlineSubsystem* Online = Game->GetSubsystem<UCatOnlineSubsystem>();
 ULocalPlayer* Local = NewObject<ULocalPlayer>(GEngine);
 Game->AddLocalPlayer(Local, FPlatformUserId::CreateFromInternalId(0));
 UGameViewportClient* Viewport = NewObject<UGameViewportClient>(GEngine);
 Viewport->Init(*GEngine->GetWorldContextFromWorld(World), Game, false);
 Local->ViewportClient = Viewport;
 TestTrue(TEXT("LocalPlayer resolves the fixture GameInstance"), Local->GetGameInstance() == Game);
 APlayerController* Player = World->SpawnActor<APlayerController>(); Player->SetPlayer(Local);
 UCatFrontendRoomModel* Room = NewObject<UCatFrontendRoomModel>(Game); Room->Initialize(Local);
 UCatFrontendPageController* Page = NewObject<UCatFrontendPageController>(Game);
 // 改前日志已有的设置资产缺口不属于加入页；仅对仍缺失的已知控件登记，修复资产后自动不再登记。
 // Root 的 NativeOnInitialized 会立即解析设置页，因此先只读实例化其正式设置资产。
 const auto SettingsClass = LoadClass<UUserWidget>(nullptr, TEXT("/Game/UI/Frontend/WBP_CatFrontendSettings.WBP_CatFrontendSettings_C"));
 const UUserWidget* SettingsProbe = SettingsClass ? CreateWidget<UUserWidget>(Player, SettingsClass) : nullptr;
 for (const TCHAR* Control : { TEXT("AccessibilitySettingsCategoryButton"), TEXT("TextSizeSlider"), TEXT("HighContrastCheckBox"),
   TEXT("ColorBlindModeComboBox"), TEXT("ReduceCameraShakeCheckBox"), TEXT("ReduceFlashingEffectsCheckBox"),
   TEXT("MouseSensitivitySlider"), TEXT("CameraSensitivitySlider"), TEXT("InvertYAxisCheckBox") })
 {
   if (SettingsProbe && !SettingsProbe->GetWidgetFromName(Control))
   {
     AddExpectedError(FString::Printf(TEXT("Event=frontend_widget_contract_missing Page=FrontendSettingsPage Control=%s Reason=missing_or_wrong_type"), Control),
       EAutomationExpectedErrorFlags::Contains, 0);
     AddInfo(FString::Printf(TEXT("Known settings asset gap excluded from Join-only verification: %s"), Control));
   }
 }
 const auto RootClass = LoadClass<UCatFrontendRootWidget>(nullptr, TEXT("/Game/UI/Frontend/WBP_CatFrontendRoot.WBP_CatFrontendRoot_C"));
 UCatFrontendRootWidget* View = RootClass ? CreateWidget<UCatFrontendRootWidget>(Player, RootClass) : nullptr;
 if (!TestNotNull(TEXT("Formal root WBP loads"), View)) { Room->Shutdown(); return false; }
 const UUserWidget* SettingsPage = Cast<UUserWidget>(View->GetWidgetFromName(TEXT("FrontendSettingsPage")));
 TestTrue(TEXT("Root references the inspected settings asset"), SettingsPage && SettingsPage->GetClass() == SettingsClass);
 Page->Initialize(Local, View, nullptr, Room, nullptr); View->InitializeFrontend(Page, nullptr, Room, nullptr);
 UUserWidget* Join = Cast<UUserWidget>(View->GetWidgetFromName(TEXT("JoinPage")));
 UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(View->GetWidgetFromName(TEXT("FrontendPageSwitcher")));
 UButton* Back = Join ? Cast<UButton>(Join->GetWidgetFromName(TEXT("JoinBackButton"))) : nullptr;
 UButton* Paste = Join ? Cast<UButton>(Join->GetWidgetFromName(TEXT("PasteJoinLinkButton"))) : nullptr;
 UButton* Submit = Join ? Cast<UButton>(Join->GetWidgetFromName(TEXT("JoinLinkButton"))) : nullptr;
 UButton* Refresh = Join ? Cast<UButton>(Join->GetWidgetFromName(TEXT("RefreshPublicRoomsButton"))) : nullptr;
 UEditableTextBox* Input = Join ? Cast<UEditableTextBox>(Join->GetWidgetFromName(TEXT("JoinLinkTextBox"))) : nullptr;
 const bool bControls = Back && Paste && Submit && Refresh && Input && Switcher;
 TestTrue(TEXT("Formal Join controls and page switcher exist"), bControls);
 if (bControls)
 {
   // 受控平台回执驱动正式 Model、Controller 和 WBP；不伪装真实 Steam 查询。
   const auto BeginSearch = [&]()
   {
     Online->WorldState = ECatOnlineWorldState::Frontend; Online->SessionRole = ECatOnlineSessionRole::None;
     Online->SessionState = ECatOnlineSessionState::Searching; Online->ActiveOperation = ECatOnlineOperation::Find;
     Online->SearchInviteCode.Reset(); Online->LastError = ECatOnlineError::None;
     Online->ActiveRequestId = FGuid::NewGuid(); ++Online->OperationEpoch;
     Online->ActiveSearch = MakeShared<FOnlineSessionSearch>();
     Online->ActiveSearch->SearchState = EOnlineAsyncTaskState::InProgress;
     Online->SessionSearchDeadline = FPlatformTime::Seconds() + 30.0;
     Online->FindSessionsHandle = FDelegateHandle(FDelegateHandle::GenerateNewHandle);
     Online->BroadcastSnapshot(TEXT("join_search_interaction_fixture")); View->ShowJoin();
   };
   BeginSearch();
   TestTrue(TEXT("RoomModel observes the actual search"), Room->IsFindingPublicRooms());
   TestEqual(TEXT("View model reads the fixture request"), Room->GetSnapshot().RequestId, Online->ActiveRequestId);
   TestTrue(TEXT("Input remains enabled while browsing"), Input->GetIsEnabled());
   TestTrue(TEXT("Paste remains enabled while browsing"), Paste->GetIsEnabled());
   TestTrue(TEXT("Back remains enabled while browsing"), Back->GetIsEnabled());
   TestFalse(TEXT("Join waits for the conflicting query"), Submit->GetIsEnabled());
   TestFalse(TEXT("Refresh cannot overlap a query"), Refresh->GetIsEnabled());
   Input->SetText(FText::FromString(TEXT("ABC234")));
   const uint64 Epoch = Online->OperationEpoch;
   const auto Query = Online->ActiveSearch;
   Back->OnClicked.Broadcast();
   TestTrue(TEXT("Back immediately shows menu"), Switcher->GetActiveWidget() == View->GetWidgetFromName(TEXT("MenuPage")));
   TestEqual(TEXT("Navigation does not fabricate query completion"), Online->OperationEpoch, Epoch);
   Page->RequestJoinParty();
   TestTrue(TEXT("Reopening shows the same Join page"), Switcher->GetActiveWidget() == Join);
   TestTrue(TEXT("Reopening retains the same platform query"), Online->ActiveSearch == Query);
   TestEqual(TEXT("Reopening does not submit a new epoch"), Online->OperationEpoch, Epoch);
   TestEqual(TEXT("Input survives navigation"), Input->GetText().ToString(), FString(TEXT("ABC234")));
   Back->OnClicked.Broadcast();
   Online->ActiveSearch->SearchState = EOnlineAsyncTaskState::Done;
   Online->HandleFindSessionsComplete(true, Epoch);
   TestTrue(TEXT("Completion after leaving cannot reopen Join"), Switcher->GetActiveWidget() == View->GetWidgetFromName(TEXT("MenuPage")));
   View->ShowJoin();
   TestTrue(TEXT("Join unlocks on search completion"), Submit->GetIsEnabled());
   TestEqual(TEXT("Completion preserves typed code"), Input->GetText().ToString(), FString(TEXT("ABC234")));
   BeginSearch();
   Online->ActiveSearch->SearchState = EOnlineAsyncTaskState::Failed;
   Online->HandleFindSessionsComplete(false, Online->OperationEpoch);
   TestTrue(TEXT("Search failure also restores editing and joining"), Input->GetIsEnabled() && Submit->GetIsEnabled());
   TestEqual(TEXT("Search failure retains its error"), Online->LastError, ECatOnlineError::FindFailed);
   BeginSearch(); Online->SearchInviteCode = TEXT("ABC234"); View->ShowJoin();
   TestFalse(TEXT("Automatic code lookup is not background browsing"), Room->IsFindingPublicRooms());
   TestFalse(TEXT("Automatic joining still locks input and navigation"), Input->GetIsEnabled() || Back->GetIsEnabled());
   Online->SearchInviteCode.Reset(); Online->ActiveSearch->SearchState = EOnlineAsyncTaskState::Done;
   Online->HandleFindSessionsComplete(true, Online->OperationEpoch);

   // Steam 右键加入解析失败后，FindSessions 可返回 true 而传入对象仍为 NotStarted，且永远没有回调。
   for (const auto StuckState : { EOnlineAsyncTaskState::NotStarted, EOnlineAsyncTaskState::InProgress })
   {
     BeginSearch(); Online->ActiveSearch->SearchState = StuckState;
     const uint64 StuckEpoch = Online->OperationEpoch;
     const FGuid StuckRequest = Online->ActiveRequestId;
     const double Deadline = Online->SessionSearchDeadline;
     Online->TickPlatformInvites(0);
     TestEqual(TEXT("Polling never extends the fixed search deadline"), Online->SessionSearchDeadline, Deadline);
     TestEqual(TEXT("Search remains pending before the deadline"), Online->ActiveOperation, ECatOnlineOperation::Find);
     Online->HandleFindSessionsComplete(true, StuckEpoch);
     TestEqual(TEXT("Unrelated shared completion cannot end this search"), Online->ActiveOperation, ECatOnlineOperation::Find);
     Online->SessionSearchDeadline = FPlatformTime::Seconds() - 1.0;
     Online->TickPlatformInvites(0);
     TestEqual(TEXT("Watchdog releases the operation"), Room->GetSnapshot().ActiveOperation, ECatOnlineOperation::None);
     TestEqual(TEXT("Timeout preserves the original request ID"), Room->GetSnapshot().RequestId, StuckRequest);
     TestEqual(TEXT("Watchdog clears the searching session state"), Room->GetSnapshot().SessionState, ECatOnlineSessionState::NoSession);
     TestEqual(TEXT("Timeout remains visible as a structured error"), Online->LastError, ECatOnlineError::JoinTargetTimedOut);
     TestFalse(TEXT("Timeout clears the search object"), Online->ActiveSearch.IsValid());
     TestFalse(TEXT("Timeout clears the Find delegate"), Online->FindSessionsHandle.IsValid());
     TestEqual(TEXT("Timeout clears the deadline"), Online->SessionSearchDeadline, 0.0);
     TestTrue(TEXT("Formal Join controls recover after the missing callback"), Input->GetIsEnabled() && Paste->GetIsEnabled()
       && Back->GetIsEnabled() && Submit->GetIsEnabled() && Refresh->GetIsEnabled());
     TestEqual(TEXT("Timeout preserves the typed code"), Input->GetText().ToString(), FString(TEXT("ABC234")));
     Back->OnClicked.Broadcast(); Page->RequestStartGameFlow();
     TestTrue(TEXT("Start game can open the formal save page after timeout"),
       Switcher->GetActiveWidget() == View->GetWidgetFromName(TEXT("SaveListPage")));
     // 本夹具不挂载存档 Model；重新初始化页面流程，避免把存档退出校验混入搜索回归。
     Page->Initialize(Local, View, nullptr, Room, nullptr);
     BeginSearch();
     const uint64 RetryEpoch = Online->OperationEpoch;
     Online->HandleFindSessionsComplete(true, StuckEpoch);
     Online->HandleFindSessionsComplete(false, RetryEpoch);
     TestEqual(TEXT("Late old or shared callbacks cannot finish the retry"), Online->ActiveOperation, ECatOnlineOperation::Find);
     TestEqual(TEXT("Ignored callbacks preserve the retry epoch"), Online->OperationEpoch, RetryEpoch);
     Online->ActiveSearch->SearchState = EOnlineAsyncTaskState::Done;
     Online->HandleFindSessionsComplete(true, RetryEpoch);
     TestTrue(TEXT("Retry completes normally and unlocks Join"), Submit->GetIsEnabled());
   }

   // NULL OSS 与 Steam 有相同的“已有搜索却返回 true”行为；用真实接口复现占用和取消，避免只测手填快照。
   IOnlineSubsystem* Platform = Online::GetSubsystem(World);
   const IOnlineSessionPtr Sessions = Online->GetWorldSessionInterface();
   if (TestTrue(TEXT("Isolated fixture uses the NULL session backend"), Platform && Platform->GetSubsystemName() == NULL_SUBSYSTEM && Sessions.IsValid()))
   {
     const auto OrphanSearch = MakeShared<FOnlineSessionSearch>();
     OrphanSearch->bIsLanQuery = true;
     TestTrue(TEXT("Existing platform search starts"), Sessions->FindSessions(0, OrphanSearch));
     const FCatOnlineResult Request = Online->RequestFindSessions();
     TestTrue(TEXT("Platform reports acceptance even while occupied"), Request.bAccepted);
     TestTrue(TEXT("Production query still owns its search object"), Online->ActiveSearch.IsValid());
     if (Online->ActiveSearch.IsValid())
     {
       TestEqual(TEXT("Occupied backend did not start the supplied query"), Online->ActiveSearch->SearchState, EOnlineAsyncTaskState::NotStarted);
     }
     int32 CancelCount = 0;
     FDelegateHandle CancelHandle = Sessions->AddOnCancelFindSessionsCompleteDelegate_Handle(
       FOnCancelFindSessionsCompleteDelegate::CreateLambda([&](bool bSuccess)
       {
         ++CancelCount;
         TestTrue(TEXT("Backend confirms cancellation"), bSuccess);
         TestFalse(TEXT("Find callback is detached before synchronous cancellation"), Online->FindSessionsHandle.IsValid());
         TestEqual(TEXT("Cancellation does not expose an idle reentry window"), Online->ActiveOperation, ECatOnlineOperation::Find);
         Sessions->TriggerOnFindSessionsCompleteDelegates(true);
       }));
     Online->SessionSearchDeadline = FPlatformTime::Seconds() - 1.0;
     Online->TickPlatformInvites(0);
     Sessions->ClearOnCancelFindSessionsCompleteDelegate_Handle(CancelHandle);
     TestEqual(TEXT("Watchdog cancels the actual backend exactly once"), CancelCount, 1);
     TestEqual(TEXT("Orphan platform search is released"), OrphanSearch->SearchState, EOnlineAsyncTaskState::Failed);
     TestEqual(TEXT("Synchronous cancellation cannot override timeout"), Online->LastError, ECatOnlineError::JoinTargetTimedOut);
     TestTrue(TEXT("Formal Join recovers from the real ignored request"), Submit->GetIsEnabled() && Refresh->GetIsEnabled());
     TestTrue(TEXT("Production retry is accepted"), Online->RequestFindSessions().bAccepted);
     TestTrue(TEXT("Retry actually starts on the recovered backend"), Online->ActiveSearch.IsValid()
       && Online->ActiveSearch->SearchState == EOnlineAsyncTaskState::InProgress);
     Online->SessionSearchDeadline = FPlatformTime::Seconds() - 1.0;
     Online->TickPlatformInvites(0);
   }
 }
 Page->Shutdown(); View->ResetFrontend(); Room->Shutdown();
 return true;
}
#endif
