#if WITH_DEV_AUTOMATION_TESTS
#include "Online/CatRoomAdmission.h"
#include "Online/CatOnlineSettings.h"
#include "Online/CatOnlineSubsystem.h"
#include "Profile/CatProfileSettings.h"
#include "UI/Frontend/CatFrontendRoomModel.h"
#include "UI/Frontend/CatFrontendPageController.h"
#include "UI/Frontend/CatFrontendRootWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatRoomAdmissionTimeoutTest, "Catfishing.Online.Rooms.AdmissionTimeout",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCatRoomAdmissionTimeoutTest::RunTest(const FString& Parameters)
{
 const UCatOnlineSettings* Settings = GetDefault<UCatOnlineSettings>();
 if (!TestTrue(TEXT("Production admission budgets are ordered"), Settings->HasValidAdmissionTimeouts())) { return false; }
 UCatOnlineSettings* Invalid = NewObject<UCatOnlineSettings>();
 Invalid->AdmissionRequestTimeoutSeconds = Invalid->AdmissionConnectTimeoutSeconds;
 TestFalse(TEXT("Equal connection/request budgets are rejected"), Invalid->HasValidAdmissionTimeouts());
 Invalid->AdmissionConnectTimeoutSeconds = -1;
 TestFalse(TEXT("Negative timeout is rejected"), Invalid->HasValidAdmissionTimeouts());

 const auto Fixture = MakeShared<FTestWorldWrapper>();
 if (!Fixture->CreateTestWorld(EWorldType::Game)) { return false; }
 UWorld* World = Fixture->GetTestWorld();
 World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
 if (!Fixture->BeginPlayInTestWorld()) { return false; }
 UGameInstance* Game = World->GetGameInstance();
 UCatRoomAdmission* Admission = Game->GetSubsystem<UCatRoomAdmission>();
 UCatOnlineSubsystem* Online = Game->GetSubsystem<UCatOnlineSubsystem>();

 // 独立定义只用于本测试，复用正式 Cat Beacon InitBase；不替换生产 Steam 驱动定义。
 FNetDriverDefinition Definition;
 Definition.DefName = FName(*(TEXT("AdmissionTimeoutTest_") + FGuid::NewGuid().ToString()));
 Definition.DriverClassName = TEXT("/Script/OnlineSubsystemUtils.IpNetDriver");
 Definition.DriverClassNameFallback = Definition.DriverClassName;
 GEngine->NetDriverDefinitions.Add(Definition);
 ACatRoomAdmissionHost* Host = World->SpawnActor<ACatRoomAdmissionHost>();
 Host->NetDriverDefinitionName = Definition.DefName;
 Host->ListenPort = 0;
 if (!TestTrue(TEXT("Real host socket starts with admission configuration"), Host->InitHost()))
 {
   Host->DestroyBeacon();
   GEngine->NetDriverDefinitions.RemoveAll([&](const auto& D) { return D.DefName == Definition.DefName; });
   return false;
 }
 TestEqual(TEXT("Host actual driver uses admission connect budget"), Host->NetDriver->InitialConnectTimeout, Settings->AdmissionConnectTimeoutSeconds);
 TestEqual(TEXT("Host actual driver uses admission request budget"), Host->NetDriver->ConnectionTimeout, Settings->AdmissionRequestTimeoutSeconds);

 ACatRoomAdmissionClient* Client = World->SpawnActor<ACatRoomAdmissionClient>();
 Client->NetDriverDefinitionName = Definition.DefName;
 Client->RequestId = FGuid::NewGuid();
 const auto Calls = MakeShared<int32>(0);
 const auto Outcome = MakeShared<ECatOnlineError>(ECatOnlineError::None);
 Admission->Client = Client;
 Admission->ClientCallback = [Calls, Outcome](ECatOnlineError Error) { ++*Calls; *Outcome = Error; };
 Admission->ClientStartedAt = FPlatformTime::Seconds();
 Admission->ClientDeadline = Admission->ClientStartedAt + Settings->AdmissionRequestTimeoutSeconds;
 // Host 不 Tick、不应答；只 Tick 真实客户端驱动，重现首次建连连续五秒收不到包。
 FURL URL(nullptr, *FString::Printf(TEXT("127.0.0.1:%d"), Host->GetListenPort()), TRAVEL_Absolute);
 if (!TestTrue(TEXT("Real client socket initializes"), Client->InitClient(URL)))
 {
   Admission->CancelClient(); Host->DestroyBeacon();
   GEngine->NetDriverDefinitions.RemoveAll([&](const auto& D) { return D.DefName == Definition.DefName; });
   return false;
 }
 UNetDriver* Driver = Client->NetDriver;
 TestEqual(TEXT("Client actual driver overrides inherited five-second timeout"), Driver->InitialConnectTimeout, Settings->AdmissionConnectTimeoutSeconds);
 TestEqual(TEXT("Client and host use the same post-connect budget"), Driver->ConnectionTimeout, Host->NetDriver->ConnectionTimeout);
 Driver->TimeoutMultiplierForUnoptimizedBuilds = 1;
 Driver->bNoTimeouts = false;
 const double Started = FPlatformTime::Seconds();
 const auto LastTick = MakeShared<double>(Started);
 ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Fixture, World, Admission, Online, Host, Client, Driver, Calls, Outcome, Started, LastTick, Definition]()
 {
   const double Now = FPlatformTime::Seconds();
   const float Delta = float(Now - *LastTick); *LastTick = Now;
   if (Admission->IsCurrentClient(Client)) { Driver->TickDispatch(Delta); Driver->TickFlush(Delta); Admission->Tick(Delta); }
   if (Now - Started < 6.2) { return false; }
   TestEqual(TEXT("No premature callback after more than five seconds without packets"), *Calls, 0);
   TestTrue(TEXT("Real connection remains pending beyond the old timeout"), Admission->IsCurrentClient(Client));
   TestTrue(TEXT("Real driver still owns the pending connection"), Driver->ServerConnection != nullptr);

   // 广播真实引擎枚举：其他驱动的失败不得结束本请求，本驱动超时必须准确分类且只收口一次。
   Client->HandleNetworkFailure(World, Host->NetDriver, ENetworkFailure::ConnectionTimeout, FString());
   TestEqual(TEXT("Unrelated driver failure is ignored"), *Calls, 0);
   Client->HandleNetworkFailure(World, Driver, ENetworkFailure::ConnectionTimeout, FString());
   TestEqual(TEXT("Transport timeout completes exactly once"), *Calls, 1);
   TestEqual(TEXT("Transport timeout retains its error category"), *Outcome, ECatOnlineError::AdmissionTimedOut);
   Client->OnFailure(EBeaconFailureReason::TransportError, FStringView());
   TestEqual(TEXT("Duplicate failure cannot deliver another completion"), *Calls, 1);

   // 接上正式 Model/WBP/返回按钮，验证取消及迟到回执，不依赖 Steam 账号。
   TGuardValue<bool> DisableProfileWrites(GetMutableDefault<UCatProfileSettings>()->bEnableProfilePersistence, false);
   UGameInstance* Instance = World->GetGameInstance();
   ULocalPlayer* Local = NewObject<ULocalPlayer>(GEngine);
   Instance->AddLocalPlayer(Local, FPlatformUserId::CreateFromInternalId(0));
   UGameViewportClient* Viewport = NewObject<UGameViewportClient>(GEngine);
   Viewport->Init(*GEngine->GetWorldContextFromWorld(World), Instance, false);
   Local->ViewportClient = Viewport;
   TestTrue(TEXT("LocalPlayer resolves the admission GameInstance"), Local->GetGameInstance() == Instance);
   APlayerController* Player = World->SpawnActor<APlayerController>(); Player->SetPlayer(Local);
   UCatFrontendRoomModel* Room = NewObject<UCatFrontendRoomModel>(Instance); Room->Initialize(Local);
   UCatFrontendPageController* Page = NewObject<UCatFrontendPageController>(Instance);
   const auto RootClass = LoadClass<UCatFrontendRootWidget>(nullptr, TEXT("/Game/UI/Frontend/WBP_CatFrontendRoot.WBP_CatFrontendRoot_C"));
   // 基线已确认的设置资产缺口；CreateWidget 即会校验，需先登记具体错误。资产补齐后应移除此隔离。
   for (const TCHAR* Control : { TEXT("AccessibilitySettingsCategoryButton"), TEXT("TextSizeSlider"), TEXT("HighContrastCheckBox"),
     TEXT("ColorBlindModeComboBox"), TEXT("ReduceCameraShakeCheckBox"), TEXT("ReduceFlashingEffectsCheckBox"),
     TEXT("MouseSensitivitySlider"), TEXT("CameraSensitivitySlider"), TEXT("InvertYAxisCheckBox") })
   {
     AddExpectedError(FString::Printf(TEXT("Event=frontend_widget_contract_missing Page=FrontendSettingsPage Control=%s Reason=missing_or_wrong_type"), Control),
       EAutomationExpectedErrorFlags::Contains, 0);
     AddInfo(FString::Printf(TEXT("Known settings asset gap excluded from admission verification: %s"), Control));
   }
   UCatFrontendRootWidget* View = RootClass ? CreateWidget<UCatFrontendRootWidget>(Player, RootClass) : nullptr;
   if (TestNotNull(TEXT("Formal frontend WBP loads"), View))
   {
     Page->Initialize(Local, View, nullptr, Room, nullptr); View->InitializeFrontend(Page, nullptr, Room, nullptr);
     Online->WorldState = ECatOnlineWorldState::Frontend; Online->SessionRole = ECatOnlineSessionRole::None;
     Online->SessionState = ECatOnlineSessionState::Searching; Online->ActiveOperation = ECatOnlineOperation::ResolveJoin;
     Online->LastError = ECatOnlineError::None;
     ACatRoomAdmissionClient* Retry = World->SpawnActor<ACatRoomAdmissionClient>(); Retry->RequestId = FGuid::NewGuid();
     Admission->Client = Retry; Admission->ClientStartedAt = FPlatformTime::Seconds();
     Admission->ClientDeadline = Admission->ClientStartedAt + 30;
     Admission->ClientCallback = [Calls](ECatOnlineError) { ++*Calls; };
     Online->BroadcastSnapshot(TEXT("admission_timeout_test_retry")); View->ShowJoin();
     TestTrue(TEXT("Room model observes the cancellable admission"), Room->CanCancelAdmission());
     UUserWidget* Join = Cast<UUserWidget>(View->GetWidgetFromName(TEXT("JoinPage")));
     UButton* Back = Join ? Cast<UButton>(Join->GetWidgetFromName(TEXT("JoinBackButton"))) : nullptr;
     if (TestNotNull(TEXT("Formal join back button exists"), Back))
     {
       TestTrue(TEXT("Back button enabled while waiting for admission"), Back->GetIsEnabled());
       const uint64 Epoch = Online->OperationEpoch;
       Back->OnClicked.Broadcast();
       TestFalse(TEXT("Back button cancels the current admission"), Admission->HasPendingClient());
       TestTrue(TEXT("Cancel invalidates old operation callbacks"), Online->OperationEpoch > Epoch);
       TestEqual(TEXT("Cancel returns to idle without a session"), Online->GetSnapshot().SessionState, ECatOnlineSessionState::NoSession);
       Retry->ClientDecision_Implementation(ECatOnlineError::None, Retry->RequestId);
       TestEqual(TEXT("Cancelled success callback cannot join a room"), *Calls, 1);
     }
     Online->LastError = ECatOnlineError::AdmissionTimedOut; Online->BroadcastSnapshot(TEXT("admission_timeout_test_error")); View->ShowJoin();
     TestTrue(TEXT("Timeout presentation is distinct from host unavailable"), Room->GetLastResultText().ToString().Contains(TEXT("连接房间超时")));
     Page->Shutdown(); View->ResetFrontend();
   }
   Room->Shutdown();

   ACatRoomAdmissionClient* NewClient = World->SpawnActor<ACatRoomAdmissionClient>(); NewClient->RequestId = FGuid::NewGuid();
   Admission->Client = NewClient; Admission->ClientStartedAt = Now;
   Admission->ClientCallback = [Calls, Outcome](ECatOnlineError Error) { ++*Calls; *Outcome = Error; };
   Client->ClientDecision_Implementation(ECatOnlineError::None, Client->RequestId);
   TestEqual(TEXT("Old beacon cannot complete a new request"), *Calls, 1);
   NewClient->ClientDecision_Implementation(ECatOnlineError::None, FGuid::NewGuid());
   TestEqual(TEXT("Wrong request id cannot authorize current client"), *Calls, 1);
   Admission->ClientDeadline = FPlatformTime::Seconds() - 1;
   Admission->Tick(0); Admission->Tick(0);
   TestEqual(TEXT("Total request deadline completes once"), *Calls, 2);
   TestEqual(TEXT("Total deadline is classified as admission timeout"), *Outcome, ECatOnlineError::AdmissionTimedOut);
   TestFalse(TEXT("No pending admission remains"), Admission->HasPendingClient());
   ACatRoomAdmissionClient* Success = World->SpawnActor<ACatRoomAdmissionClient>(); Success->RequestId = FGuid::NewGuid();
   Admission->Client = Success; Admission->ClientStartedAt = FPlatformTime::Seconds();
   Admission->ClientCallback = [Calls, Outcome](ECatOnlineError Error) { ++*Calls; *Outcome = Error; };
   Success->ClientDecision_Implementation(ECatOnlineError::None, Success->RequestId);
   Success->ClientDecision_Implementation(ECatOnlineError::None, Success->RequestId);
   TestEqual(TEXT("A new valid authorization succeeds exactly once after timeout"), *Calls, 3);
   TestEqual(TEXT("Valid authorization preserves the success result"), *Outcome, ECatOnlineError::None);
   Host->DestroyBeacon();
   GEngine->NetDriverDefinitions.RemoveAll([&](const auto& D) { return D.DefName == Definition.DefName; });
   return true;
 }));
 return true;
}
#endif
