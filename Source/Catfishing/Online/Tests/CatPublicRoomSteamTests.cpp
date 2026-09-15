#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Online/CatOnlineSubsystem.h"
#include "Online/CatSteamCodeSearch.h"
#include "Online/CatRoomAdmission.h"
#include "EngineUtils.h"
#include "Online/CatOnlineSettings.h"
#include "OnlineSubsystemUtils.h"
#include "Online/OnlineSessionNames.h"

class FCatPublicRoomSteamCommand : public IAutomationLatentCommand
{
public:
 FCatPublicRoomSteamCommand(FAutomationTestBase* InTest, UGameInstance* InGame) : Test(InTest), Game(InGame), Started(FPlatformTime::Seconds()) {}
 virtual bool Update() override
 {
   if (!Game.IsValid() || FPlatformTime::Seconds() - Started > 90)
   { Test->AddError(FString::Printf(TEXT("Public room verification timed out at phase %d"), Phase)); return true; }
   UCatOnlineSubsystem* Online = Game->GetSubsystem<UCatOnlineSubsystem>();
   const FCatOnlineSnapshot Snapshot = Online->GetSnapshot();
   if (Snapshot.ActiveOperation != ECatOnlineOperation::None) { return false; }
   switch (Phase)
   {
   case 0:
     if (!Test->TestTrue(TEXT("Create public host"), Online->RequestCreateSession().bAccepted)) { return true; }
     ++Phase; break;
   case 1:
     if (!Test->TestEqual(TEXT("Host creation succeeded"), Snapshot.LastError, ECatOnlineError::None)) { return true; }
     Test->TestEqual(TEXT("Public is the configured default"), Snapshot.SessionAccess, ECatSessionAccessPolicy::Public);
     Test->TestEqual(TEXT("Host sees full six-character code"), Snapshot.InviteCode.Len(), 6);
     if (!Test->TestTrue(TEXT("Submit name, capacity and password"), Online->RequestUpdateRoomSettings(TEXT("PublicRoomSmoke"), 3, ECatSessionAccessPolicy::Public, TEXT("fixture secret"), false).bAccepted)) { return true; }
     ++Phase; break;
   case 2:
   {
     Test->TestEqual(TEXT("Settings committed"), Snapshot.LastError, ECatOnlineError::None);
     Test->TestEqual(TEXT("Room name round-trips through platform"), Snapshot.RoomName, FString(TEXT("PublicRoomSmoke")));
     Test->TestEqual(TEXT("Capacity round-trips"), Snapshot.MaxPlayers, 3);
     Test->TestTrue(TEXT("Host password enabled"), Snapshot.bHasPassword);
     FString Map; GetDefault<UCatOnlineSettings>()->TryGetGameplayMapPackage(Map);
     CodeQuery = MakeUnique<FCatSteamCodeSearch>();
     if (!Test->TestTrue(TEXT("Global code-route lookup submitted"), CodeQuery->Begin(Snapshot.InviteCode.Left(3), Map))) { return true; }
     ++Phase; break;
   }
   case 3:
   {
     if (!CodeQuery)
     {
       if (FPlatformTime::Seconds() < NextQuery) { return false; }
       FString Map; GetDefault<UCatOnlineSettings>()->TryGetGameplayMapPackage(Map);
       CodeQuery = MakeUnique<FCatSteamCodeSearch>(); CodeQuery->Begin(Snapshot.InviteCode.Left(3), Map);
     }
     bool bFailed = false; TArray<FCatSteamCodeCandidate> Results;
     if (!CodeQuery->Poll(bFailed, Results)) { return false; }
     if (!bFailed && Results.IsEmpty() && FPlatformTime::Seconds() - Started < 25)
     { CodeQuery.Reset(); NextQuery = FPlatformTime::Seconds() + 1; return false; }
     Test->TestFalse(TEXT("Steam global lookup completed"), bFailed);
     Test->TestTrue(TEXT("Global route finds this public room"), Results.ContainsByPredicate([&](const auto& Entry) { return LexToString(Entry.LobbyId) == Snapshot.LobbyId; }));
     CodeQuery.Reset();
     Sessions = Online::GetSessionInterface(Game->GetWorld());
     Search = MakeShared<FOnlineSessionSearch>(); Search->MaxSearchResults = 50;
     Search->QuerySettings.Set(SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);
     Search->QuerySettings.Set(FName(TEXT("CAT_CODE_ROUTE")), Snapshot.InviteCode.Left(3), EOnlineComparisonOp::Equals);
     FindHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(FOnFindSessionsCompleteDelegate::CreateLambda([this](bool bSuccess) { bFindDone = true; bFindSuccess = bSuccess; }));
     if (!Sessions->FindSessions(0, Search.ToSharedRef())) { Test->AddError(TEXT("OSS verification search rejected")); return true; }
     ++Phase; break;
   }
   case 4:
     if (!bFindDone) { return false; }
     Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle); FindHandle.Reset();
     Test->TestTrue(TEXT("OSS search completes"), bFindSuccess);
     // UE OnlineSessionAsyncLobbySteam.cpp excludes IsMemberOfLobby entries before parsing.
     // A second Steam account is required to exercise discovery/parse of this host.
     Test->TestFalse(TEXT("OSS excludes the room already joined by this host"), Search->SearchResults.ContainsByPredicate([&](const auto& Entry)
     { return Entry.Session.SessionInfo.IsValid() && Entry.Session.SessionInfo->GetSessionId().ToString() == Snapshot.LobbyId; }));
     if (!Test->TestTrue(TEXT("Clear password submitted"), Online->RequestUpdateRoomSettings(TEXT("PublicRoomSmoke"), 4, ECatSessionAccessPolicy::Public, TEXT(""), true).bAccepted)) { return true; }
     ++Phase; break;
   case 5:
     Test->TestFalse(TEXT("Password removal confirmed"), Snapshot.bHasPassword);
     if (!Test->TestTrue(TEXT("Leave frontend room"), Online->RequestLeave().bAccepted)) { return true; }
     ++Phase; break;
   default:
     Test->TestEqual(TEXT("Session cleaned"), Snapshot.SessionState, ECatOnlineSessionState::NoSession);
     Test->TestTrue(TEXT("Code and password cleared"), Snapshot.InviteCode.IsEmpty() && !Snapshot.bHasPassword);
     { int32 Hosts = 0; for (TActorIterator<ACatRoomAdmissionHostObject> It(Game->GetWorld()); It; ++It) { ++Hosts; }
       Test->TestEqual(TEXT("Leaving removes the beacon host object"), Hosts, 0); }
     Test->AddInfo(TEXT("Steam host/settings/global-code-discovery/OSS-self-filter/cleanup checks ended; external OSS discovery and remote admission require two accounts."));
     return true;
   }
   return false;
 }
 ~FCatPublicRoomSteamCommand() { if (Sessions.IsValid() && FindHandle.IsValid()) { Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle); } }
private:
 FAutomationTestBase* Test;
 TWeakObjectPtr<UGameInstance> Game;
 double Started;
 double NextQuery = 0;
 int32 Phase = 0;
 TUniquePtr<FCatSteamCodeSearch> CodeQuery;
 IOnlineSessionPtr Sessions;
 TSharedPtr<FOnlineSessionSearch> Search;
 FDelegateHandle FindHandle;
 bool bFindDone = false, bFindSuccess = false;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPublicRoomSteamTest, "Catfishing.Online.Rooms.SteamHostDiscovery", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FCatPublicRoomSteamTest::RunTest(const FString& Parameters)
{
 if (!FParse::Param(FCommandLine::Get(), TEXT("CatPublicRoomSmoke"))) { AddInfo(TEXT("Skipped: requires isolated -CatPublicRoomSmoke game")); return true; }
 for (const FWorldContext& Context : GEngine->GetWorldContexts())
 { if (Context.WorldType == EWorldType::Game && Context.OwningGameInstance) { ADD_LATENT_AUTOMATION_COMMAND(FCatPublicRoomSteamCommand(this, Context.OwningGameInstance)); return true; } }
 AddError(TEXT("Game world unavailable")); return false;
}
#endif
