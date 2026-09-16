#include "Online/CatOnlineSubsystem.h"
#include "Online/CatRoomAdmission.h"
#include "Online/CatOnlineSettings.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Logging/CatLog.h"

#if WITH_STEAMWORKS
THIRD_PARTY_INCLUDES_START
#include "steam/steam_api.h"
THIRD_PARTY_INCLUDES_END
#endif

void UCatOnlineSubsystem::PollCodeSearch()
{
 if (!CodeSearch)
 {
   if (FPlatformTime::Seconds() < NextCodeSearchAttempt) { return; }
   CodeSearch = MakeUnique<FCatSteamCodeSearch>();
   if (!CodeSearch->Begin(SearchInviteCode.Left(3), GameplayMapPackage))
   { SessionState = ECatOnlineSessionState::NoSession; FinishOperationFailure(ECatOnlineError::FindFailed); return; }
 }
 bool bFailed = false;
 TArray<FCatSteamCodeCandidate> Candidates;
 if (!CodeSearch->Poll(bFailed, Candidates))
 {
   if (FPlatformTime::Seconds() >= JoinResolveDeadline)
   { SessionState = ECatOnlineSessionState::NoSession; FinishOperationFailure(ECatOnlineError::JoinTargetTimedOut); }
   return;
 }
 CodeSearch.Reset();
 if (!bFailed && Candidates.IsEmpty() && FPlatformTime::Seconds() + 2 < JoinResolveDeadline)
 { NextCodeSearchAttempt = FPlatformTime::Seconds() + 1; return; }
 SessionState = ECatOnlineSessionState::NoSession;
 if (bFailed) { FinishOperationFailure(ECatOnlineError::FindFailed); return; }
 for (FCatSteamCodeCandidate& Candidate : Candidates)
 {
   Candidate.Summary.Handle.Value = FGuid::NewGuid();
   CodeCandidates.Add(Candidate.Summary.Handle.Value, Candidate.LobbyId);
   SearchSummaries.Add(MoveTemp(Candidate.Summary));
 }
 if (Candidates.Num() != 1)
 { FinishOperationFailure(Candidates.IsEmpty() ? ECatOnlineError::InvalidInviteCode : ECatOnlineError::InviteCodeCandidates); return; }
 const uint64 LobbyId = Candidates[0].LobbyId;
 FinishOperationSuccess();
 JoinCodeCandidate(LobbyId);
}
FCatOnlineResult UCatOnlineSubsystem::JoinCodeCandidate(uint64 LobbyId)
{
 if (WorldState != ECatOnlineWorldState::Frontend || SessionRole != ECatOnlineSessionRole::None || bLocalRoomActive || SearchInviteCode.IsEmpty())
 { return RejectRequest(ECatOnlineError::InvalidState); }
 FCatOnlineResult Result = BeginOperation(ECatOnlineOperation::ResolveJoin, ECatOnlineSessionState::Searching);
 if (!Result.bAccepted) { return Result; }
 JoinCredential = SearchInviteCode; bJoinWithCode = true; CancelRoomPassword();
 JoinLink = MakeUnique<FCatSteamJoinLink>();
 bJoinLinkLaunched = false; AbandonedJoinLinkLobby = 0;
 JoinResolveDeadline = FPlatformTime::Seconds() + 30.0;
 if (!JoinLink->Begin(LobbyId)) { FailJoinResolution(ECatOnlineError::JoinTargetUnavailable); Result.bAccepted = false; Result.Error = LastError; }
 return Result;
}

FCatOnlineResult UCatOnlineSubsystem::BeginRoomAdmission(const FOnlineSessionSearchResult& Target)
{
 FCatOnlineResult Result;
 if (ActiveOperation == ECatOnlineOperation::ResolveJoin)
 {
   ClearOperationDelegates(); Result.bAccepted = true; Result.RequestId = ActiveRequestId;
 }
 else { Result = BeginOperation(ECatOnlineOperation::ResolveJoin, ECatOnlineSessionState::Searching); }
 if (!Result.bAccepted) { return Result; }
 PasswordRetryTarget = FOnlineSessionSearchResult();
 const FString LobbyId = Target.Session.SessionInfo.IsValid() ? Target.Session.SessionInfo->GetSessionId().ToString() : FString();
 FString OwnerId;
 int32 Port = 0;
#if WITH_STEAMWORKS
 if (SteamMatchmaking() && LobbyId.IsNumeric())
 {
   const CSteamID Lobby(FCString::Strtoui64(*LobbyId, nullptr, 10));
   OwnerId = LexToString(SteamMatchmaking()->GetLobbyOwner(Lobby).ConvertToUint64());
   Port = FCStringAnsi::Atoi(SteamMatchmaking()->GetLobbyData(Lobby, "CAT_ADMISSION_PORT_s"));
 }
#endif
 const uint64 Epoch = OperationEpoch;
 const UCatOnlineSettings* Settings = GetDefault<UCatOnlineSettings>();
 if (!Settings->HasValidAdmissionTimeouts())
 {
   UE_LOG(LogCatOnline, Error, TEXT("Event=room_admission_config_invalid RequestId=%s World=%s Result=InvalidTimeoutOrder"), *ActiveRequestId.ToString(), *GetNameSafe(GetWorld()));
   FailJoinResolution(ECatOnlineError::AdmissionConnectionFailed); Result.bAccepted = false; Result.Error = LastError; return Result;
 }
 JoinResolveDeadline = FPlatformTime::Seconds() + Settings->AdmissionOperationTimeoutSeconds;
 const bool bStarted = GetGameInstance()->GetSubsystem<UCatRoomAdmission>()->BeginClient(OwnerId, Port, LobbyId, JoinCredential, bJoinWithCode,
   ActiveRequestId, [WeakThis = TWeakObjectPtr<UCatOnlineSubsystem>(this), Target, OwnerId, Epoch](ECatOnlineError Error)
   {
     UCatOnlineSubsystem* Self = WeakThis.Get();
     if (!Self || Self->OperationEpoch != Epoch || Self->ActiveOperation != ECatOnlineOperation::ResolveJoin) { return; }
     Self->JoinCredential.Reset(); Self->bJoinWithCode = false;
     if (Error != ECatOnlineError::None)
     {
       if (Error == ECatOnlineError::PasswordRequired || Error == ECatOnlineError::PasswordIncorrect) { Self->PasswordRetryTarget = Target; }
       Self->FailJoinResolution(Error);
       return;
     }
     Self->VerifiedHostOwnerId = OwnerId;
     const FCatOnlineResult Joined = Self->RequestJoinInternal(Target, true);
     if (!Joined.bAccepted && Self->ActiveOperation == ECatOnlineOperation::ResolveJoin) { Self->FailJoinResolution(Joined.Error); }
   });
 JoinCredential.Reset();
 if (!bStarted && ActiveOperation == ECatOnlineOperation::ResolveJoin && Epoch == OperationEpoch)
 { FailJoinResolution(ECatOnlineError::AdmissionConnectionFailed); Result.bAccepted = false; Result.Error = LastError; }
 if (bStarted && ActiveOperation == ECatOnlineOperation::ResolveJoin && Epoch == OperationEpoch)
 { BroadcastSnapshot(TEXT("online_admission_started")); }
 return Result;
}
bool UCatOnlineSubsystem::CanCancelRoomAdmission() const
{
 const UCatRoomAdmission* Admission = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCatRoomAdmission>() : nullptr;
 return ActiveOperation == ECatOnlineOperation::ResolveJoin && SessionRole == ECatOnlineSessionRole::None
   && WorldState == ECatOnlineWorldState::Frontend && Admission && Admission->HasPendingClient();
}
bool UCatOnlineSubsystem::CancelRoomAdmission()
{
 if (!CanCancelRoomAdmission()) { return false; }
 UE_LOG(LogCatOnline, Log, TEXT("Event=online_admission_cancelled RequestId=%s Epoch=%llu World=%s NetMode=%d Result=Cancelled"),
   *ActiveRequestId.ToString(), OperationEpoch, *GetNameSafe(GetWorld()), GetWorld() ? int32(GetWorld()->GetNetMode()) : -1);
 JoinCredential.Reset(); bJoinWithCode = false; CancelRoomPassword();
 SessionState = ECatOnlineSessionState::NoSession;
 // 统一终态负责取消 Beacon、移除委托并推进 epoch；不伪造房间或加入成功。
 FinishOperationSuccess();
 return true;
}
FCatOnlineResult UCatOnlineSubsystem::RequestSubmitRoomPassword(const FString& Password)
{
 if (ActiveOperation != ECatOnlineOperation::None) { return RejectRequest(ECatOnlineError::CommandAlreadyPending); }
 if (!PasswordRetryTarget.IsValid() || Password.Len() > 32) { return RejectRequest(ECatOnlineError::InvalidState); }
 const FOnlineSessionSearchResult Target = PasswordRetryTarget;
 JoinCredential = Password; bJoinWithCode = false;
 return RequestJoinInternal(Target);
}
void UCatOnlineSubsystem::CancelRoomPassword()
{
 PasswordRetryTarget = FOnlineSessionSearchResult();
}
FCatOnlineResult UCatOnlineSubsystem::RequestUpdateRoomSettings(const FString& Name, int32 Capacity, ECatSessionAccessPolicy Access,
 const FString& Password, bool bClearPassword)
{
 if (ActiveOperation != ECatOnlineOperation::None) { return RejectRequest(ECatOnlineError::CommandAlreadyPending); }
 if (SessionRole != ECatOnlineSessionRole::Host || SessionState != ECatOnlineSessionState::Host || WorldState != ECatOnlineWorldState::Frontend)
 { return RejectRequest(ECatOnlineError::InvalidState); }
 UCatRoomAdmission* Admission = GetGameInstance()->GetSubsystem<UCatRoomAdmission>();
 const ECatOnlineError Validation = Admission->ValidateSettings(Name, Capacity, Password);
 if (Validation != ECatOnlineError::None) { return RejectRequest(Validation); }
 if (Access != ECatSessionAccessPolicy::Public && Access != ECatSessionAccessPolicy::FriendsOnly && Access != ECatSessionAccessPolicy::InviteOnly)
 { return RejectRequest(ECatOnlineError::RoomSettingsInvalid); }
 const IOnlineSessionPtr Sessions = GetWorldSessionInterface();
 FNamedOnlineSession* Session = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
 if (!Session) { return RejectRequest(ECatOnlineError::SessionInterfaceUnavailable); }
 FOnlineSessionSettings Updated = Session->SessionSettings;
 Updated.NumPublicConnections = Capacity;
 Updated.bShouldAdvertise = Access != ECatSessionAccessPolicy::InviteOnly;
 Updated.bAllowJoinViaPresence = Access == ECatSessionAccessPolicy::Public;
 Updated.bAllowJoinViaPresenceFriendsOnly = Access == ECatSessionAccessPolicy::FriendsOnly;
 Updated.Set(FName(TEXT("CAT_ROOM_NAME")), Name.TrimStartAndEnd(), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
 FCatOnlineResult Result = BeginOperation(ECatOnlineOperation::UpdateRoom, ECatOnlineSessionState::Host);
 if (!Result.bAccepted) { return Result; }
 PendingRoomName = Name.TrimStartAndEnd(); PendingRoomCapacity = Capacity; PendingRoomPassword = Password; bPendingClearPassword = bClearPassword;
 PreviousRoomSettings = Session->SessionSettings;
 RoomSettingsDeadline = FPlatformTime::Seconds() + 20.0;
 OperationSessionInterface = Sessions;
 const uint64 Epoch = OperationEpoch;
 RoomSettingsHandle = Sessions->AddOnUpdateSessionCompleteDelegate_Handle(FOnUpdateSessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleRoomSettingsComplete, Epoch));
 if (!Sessions->UpdateSession(NAME_GameSession, Updated, true) && OperationEpoch == Epoch && RoomSettingsHandle.IsValid())
 { HandleRoomSettingsComplete(NAME_GameSession, false, Epoch); Result.bAccepted = false; Result.Error = LastError; }
 return Result;
}
void UCatOnlineSubsystem::HandleRoomSettingsComplete(FName SessionName, bool bSuccess, uint64 Epoch)
{
 if (SessionName != NAME_GameSession || Epoch != OperationEpoch || ActiveOperation != ECatOnlineOperation::UpdateRoom || !RoomSettingsHandle.IsValid()) { return; }
 if (OperationSessionInterface.IsValid()) { OperationSessionInterface->ClearOnUpdateSessionCompleteDelegate_Handle(RoomSettingsHandle); }
 RoomSettingsHandle.Reset();
 if (!bSuccess)
 {
   if (FNamedOnlineSession* Session = OperationSessionInterface.IsValid() ? OperationSessionInterface->GetNamedSession(NAME_GameSession) : nullptr;
     Session && PreviousRoomSettings.IsSet()) { Session->SessionSettings = PreviousRoomSettings.GetValue(); }
   PreviousRoomSettings.Reset();
   FinishOperationFailure(ECatOnlineError::RoomSettingsFailed); return;
 }
 PreviousRoomSettings.Reset();
 GetGameInstance()->GetSubsystem<UCatRoomAdmission>()->ApplySettings(PendingRoomName, PendingRoomCapacity, PendingRoomPassword, bPendingClearPassword);
 PendingRoomPassword.Reset();
 RefreshRoomSnapshotFacts();
 FinishOperationSuccess();
}
