#include "Online/CatRoomAdmission.h"
#include "Online/CatOnlineSubsystem.h"
#include "Online/CatOnlineSettings.h"
#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "IPAddress.h"
#include "Misc/SecureHash.h"
#include "Logging/CatLog.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

#if WITH_STEAMWORKS
THIRD_PARTY_INCLUDES_START
#include "steam/steam_api.h"
THIRD_PARTY_INCLUDES_END
#endif

namespace
{
 const TCHAR* Alphabet = TEXT("23456789ABCDEFGHJKLMNPQRSTUVWXYZ");
 constexpr double ReservationSeconds = 30.0;
 constexpr double InviteSeconds = 600.0;

 bool ApplyAdmissionTimeouts(const AActor* Beacon, float& InitialTimeout, float& ConnectionTimeout)
 {
   const UCatOnlineSettings* Settings = GetDefault<UCatOnlineSettings>();
   if (!Settings->HasValidAdmissionTimeouts())
   {
     UE_LOG(LogCatOnline, Error, TEXT("Event=room_admission_config_invalid World=%s NetMode=%d Actor=%s Result=InvalidTimeoutOrder"),
       *GetNameSafe(Beacon->GetWorld()), int32(Beacon->GetNetMode()), *GetNameSafe(Beacon));
     return false;
   }
   // InitBase 在配置装载之后、UE 将 Beacon 时限写入驱动之前执行；不依赖构造函数覆盖 Config 属性。
   InitialTimeout = Settings->AdmissionConnectTimeoutSeconds;
   ConnectionTimeout = Settings->AdmissionRequestTimeoutSeconds;
   return true;
 }
}

ACatRoomAdmissionHost::ACatRoomAdmissionHost() { NetDriverDefinitionName = TEXT("CatAdmissionNetDriver"); }
ACatRoomAdmissionClient::ACatRoomAdmissionClient() { NetDriverDefinitionName = TEXT("CatAdmissionNetDriver"); }
bool ACatRoomAdmissionHost::InitBase()
{
 return ApplyAdmissionTimeouts(this, BeaconConnectionInitialTimeout, BeaconConnectionTimeout) && Super::InitBase();
}
bool ACatRoomAdmissionClient::InitBase()
{
 return ApplyAdmissionTimeouts(this, BeaconConnectionInitialTimeout, BeaconConnectionTimeout) && Super::InitBase();
}
ACatRoomAdmissionHostObject::ACatRoomAdmissionHostObject()
{
 ClientBeaconActorClass = ACatRoomAdmissionClient::StaticClass();
 BeaconTypeName = ClientBeaconActorClass->GetName();
}
void ACatRoomAdmissionHostObject::OnClientConnected(AOnlineBeaconClient* NewClientActor, UNetConnection* Connection)
{
 Super::OnClientConnected(NewClientActor, Connection);
 if (GetNumClientActors() > 32 || UCatRoomAdmission::PeerIdentity(Connection).IsEmpty()) { DisconnectClient(NewClientActor); return; }
 NewClientActor->SetLifeSpan(GetDefault<UCatOnlineSettings>()->AdmissionRequestTimeoutSeconds);
}
void ACatRoomAdmissionClient::OnConnected()
{
 Super::OnConnected();
 UCatRoomAdmission* Admission = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCatRoomAdmission>() : nullptr;
 if (!Admission || !Admission->IsCurrentClient(this)) { return; }
 UE_LOG(LogCatOnline, Log, TEXT("Event=room_admission_connected RequestId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s Phase=Authorize"),
   *RequestId.ToString(), *GetNameSafe(GetWorld()), int32(GetNetMode()), HasAuthority(), int32(GetLocalRole()), *GetName());
 ServerAuthorize(PendingLobby, PendingSecret, bPendingCode, RequestId);
 PendingSecret.Reset();
}
void ACatRoomAdmissionClient::ServerAuthorize_Implementation(const FString& Lobby, const FString& Secret, bool bCode, FGuid CorrelationId)
{
 if (bSubmitted) { return; }
 bSubmitted = true;
 UCatRoomAdmission* Admission = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCatRoomAdmission>() : nullptr;
 const ECatOnlineError Result = Admission && Secret.Len() <= 64 && Lobby.Len() <= 20 && CorrelationId.IsValid()
   ? Admission->Authorize(this, Lobby, Secret, bCode) : ECatOnlineError::AdmissionDenied;
 if (Result == ECatOnlineError::None)
 {
   UE_LOG(LogCatOnline, Log, TEXT("Event=room_admission_decision RequestId=%s World=%s NetMode=%d Authority=%d Player=Redacted Result=Accepted"),
     *CorrelationId.ToString(), *GetNameSafe(GetWorld()), int32(GetNetMode()), HasAuthority());
 }
 else
 {
   UE_LOG(LogCatOnline, Warning, TEXT("Event=room_admission_decision RequestId=%s World=%s NetMode=%d Authority=%d Player=Redacted Result=%s"),
     *CorrelationId.ToString(), *GetNameSafe(GetWorld()), int32(GetNetMode()), HasAuthority(), *UEnum::GetValueAsString(Result));
 }
 ClientDecision(Result, CorrelationId);
}
void ACatRoomAdmissionClient::ClientDecision_Implementation(ECatOnlineError Error, FGuid CorrelationId)
{
 if (CorrelationId != RequestId) { return; }
 if (UCatRoomAdmission* Admission = GetGameInstance()->GetSubsystem<UCatRoomAdmission>()) { Admission->CompleteClient(this, Error); }
}
void ACatRoomAdmissionClient::HandleNetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
 if (!Driver || Driver != NetDriver) { return; }
 // UE 的默认 Beacon 处理会把 ConnectionTimeout 压成 TransportError；在此保留枚举事实，不解析错误原文。
 OnFailure(FailureType == ENetworkFailure::ConnectionTimeout ? EBeaconFailureReason::TimeOut : EBeaconFailureReason::TransportError,
   FStringView());
}
void ACatRoomAdmissionClient::OnFailure(EBeaconFailureReason Reason, FStringView ErrorMessage)
{
 // 引擎错误文本可能包含连接身份；只转交稳定错误，不记录原文。终态由当前请求唯一收口。
 if (UCatRoomAdmission* Admission = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCatRoomAdmission>() : nullptr)
 {
   if (!Admission->IsCurrentClient(this)) { return; }
   UE_LOG(LogCatOnline, Warning, TEXT("Event=room_admission_connection_failed RequestId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s Reason=%s"),
     *RequestId.ToString(), *GetNameSafe(GetWorld()), int32(GetNetMode()), HasAuthority(), int32(GetLocalRole()), *GetName(), LexToString(Reason));
   Admission->CompleteClient(this, Reason == EBeaconFailureReason::TimeOut
     ? ECatOnlineError::AdmissionTimedOut : ECatOnlineError::AdmissionConnectionFailed);
 }
}

FString UCatRoomAdmission::PeerIdentity(UNetConnection* Connection)
{
 if (!Connection || !Connection->GetDriver()
   || Connection->GetDriver()->GetClass()->GetPathName() != TEXT("/Script/SteamSockets.SteamSocketsNetDriver")) { return FString(); }
 const TSharedPtr<const FInternetAddr> Address = Connection->GetRemoteAddr();
 // SteamShared/IPAddressSteam.cpp 定义的公开协议名；避免依赖其私有地址实现。
 if (!Address.IsValid() || Address->GetProtocolType() != FName(TEXT("SteamSocketsP2P"))) { return FString(); }
 const FString Id = Address->ToString(false);
#if WITH_STEAMWORKS
 if (Id.IsNumeric() && CSteamID(FCString::Strtoui64(*Id, nullptr, 10)).BIndividualAccount()) { return Id; }
#endif
 return FString();
}

bool UCatRoomAdmission::NormalizeCode(const FString& Input, FString& OutCode)
{
 OutCode = Input.TrimStartAndEnd().ToUpper();
 if (OutCode.Len() != 6) { OutCode.Reset(); return false; }
 for (TCHAR Character : OutCode) { if (!FCString::Strchr(Alphabet, Character)) { OutCode.Reset(); return false; } }
 return true;
}
FString UCatRoomAdmission::GenerateCode()
{
 const uint32 Random = FGuid::NewGuid().A;
 FString Result;
 for (int32 Index = 0; Index < 6; ++Index) { Result.AppendChar(Alphabet[(Random >> (Index * 5)) & 31]); }
 return Result;
}
bool UCatRoomAdmission::StartHost(const FString& LobbyId, const FString& OwnerId, const FString& InviteCode)
{
 StopHost();
 HostLobby = LobbyId; HostOwner = OwnerId; Code = InviteCode;
 MaxPlayers = 4;
 ListenPort = GetWorld() ? GetWorld()->URL.Port + 1 : 0;
 if (ListenPort <= 0 || ListenPort > 65535 || !ResumeListener()) { StopHost(); return false; }
 TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Tick), 0.5f);
 PublishFacts();
 return true;
}
bool UCatRoomAdmission::ResumeListener()
{
 if (Host.IsValid()) { return true; }
 if (HostLobby.IsEmpty() || !GetWorld()) { return false; }
 ACatRoomAdmissionHost* Listener = GetWorld()->SpawnActor<ACatRoomAdmissionHost>();
 if (!Listener) { return false; }
 Listener->ListenPort = ListenPort;
 if (!Listener->InitHost()) { Listener->DestroyBeacon(); return false; }
 ACatRoomAdmissionHostObject* Object = GetWorld()->SpawnActor<ACatRoomAdmissionHostObject>();
 if (!Object) { Listener->DestroyBeacon(); return false; }
 Listener->RegisterHost(Object);
 Listener->PauseBeaconRequests(false);
 Host = Listener;
 UE_LOG(LogCatOnline, Log, TEXT("Event=room_admission_listening World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s Port=%d ConnectTimeoutSeconds=%.2f RequestTimeoutSeconds=%.2f Result=Ready"),
   *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), Listener->HasAuthority(), int32(Listener->GetLocalRole()), *Listener->GetName(), ListenPort,
   GetDefault<UCatOnlineSettings>()->AdmissionConnectTimeoutSeconds, GetDefault<UCatOnlineSettings>()->AdmissionRequestTimeoutSeconds);
 return true;
}
void UCatRoomAdmission::SuspendListener()
{
 if (ACatRoomAdmissionHost* Listener = Host.Get())
 {
   Host.Reset();
   const FString Type = ACatRoomAdmissionClient::StaticClass()->GetName();
   AOnlineBeaconHostObject* Object = Listener->GetHost(Type);
   Listener->UnregisterHost(Type);
   if (Object) { Object->Destroy(); }
   Listener->DestroyBeacon();
   UE_LOG(LogCatOnline, Log, TEXT("Event=room_admission_stopped World=%s NetMode=%d Authority=1"),
     *GetNameSafe(GetWorld()), GetWorld() ? int32(GetWorld()->GetNetMode()) : -1);
 }
}
void UCatRoomAdmission::StopHost()
{
 SuspendListener(); HostLobby.Reset(); HostOwner.Reset(); Password.Reset(); Code.Reset(); RoomName.Reset();
 Grants.Reset(); Invited.Reset(); Attempts.Reset(); LastPublishedMembers.Reset();
 bFactsDirty = bFactsFailureLogged = bMembersFailureLogged = false; NextFactsAttempt = 0;
 if (TickHandle.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(TickHandle); TickHandle.Reset(); }
}
void UCatRoomAdmission::Deinitialize() { CancelClient(); StopHost(); Super::Deinitialize(); }
void UCatRoomAdmission::Invite(const FString& PeerId) { if (IsHosting()) { Invited.Add(PeerId, FPlatformTime::Seconds() + InviteSeconds); } }
bool UCatRoomAdmission::IsAuthorized(const FString& PeerId) const
{
 if (!IsHosting()) { return false; }
 if (PeerId == HostOwner) { return true; }
 const FGrant* Grant = Grants.Find(PeerId);
 return Grant && Grant->Expires > FPlatformTime::Seconds();
}
bool UCatRoomAdmission::ValidateGameplayPeer(const FString& Address, const FUniqueNetIdRepl& UniqueId) const
{
 if (!IsHosting()) { return false; }
 if (!UniqueId.IsValid()) { return false; }
 FString Peer = Address, Port;
 Address.Split(TEXT(":"), &Peer, &Port);
 return UniqueId->ToString() == Peer && IsAuthorized(Peer);
}
ECatOnlineError UCatRoomAdmission::Authorize(ACatRoomAdmissionClient* Beacon, const FString& LobbyId, const FString& Secret, bool bCode)
{
 const FString Peer = PeerIdentity(Beacon ? Beacon->GetNetConnection() : nullptr);
 if (Peer.IsEmpty() || LobbyId != HostLobby || !IsHosting()) { return ECatOnlineError::AdmissionDenied; }
 const FCatOnlineSnapshot Snapshot = GetGameInstance()->GetSubsystem<UCatOnlineSubsystem>()->GetSnapshot();
 if (Snapshot.ActiveOperation != ECatOnlineOperation::None || Snapshot.SessionState != ECatOnlineSessionState::Host
   || (Snapshot.WorldState != ECatOnlineWorldState::Frontend && Snapshot.WorldState != ECatOnlineWorldState::Lake))
 { return ECatOnlineError::AdmissionUnavailable; }
 return AuthorizePeer(Peer, Secret, bCode);
}
ECatOnlineError UCatRoomAdmission::AuthorizePeer(const FString& Peer, const FString& Secret, bool bCode)
{
 if (!IsHosting() || Peer.IsEmpty()) { return ECatOnlineError::AdmissionDenied; }
 const double Now = FPlatformTime::Seconds();
 // 有界映射；全局上限到达时拒绝新来源，不驱逐正在冷却的来源。
 if (!Attempts.Contains(Peer) && Attempts.Num() >= 1024) { return ECatOnlineError::AdmissionRateLimited; }
 FAttempts& Attempt = Attempts.FindOrAdd(Peer);
 if (Now >= Attempt.Until) { Attempt.Count = 0; Attempt.Until = Now + 60.0; }
 if (++Attempt.Count > 5) { return ECatOnlineError::AdmissionRateLimited; }
 const double* Invitation = Invited.Find(Peer);
 const bool bInvited = Invitation && *Invitation > Now;
 if (!IsAuthorized(Peer) && !bInvited)
 {
   if (bCode)
   {
     FString Normalized;
     if (!NormalizeCode(Secret, Normalized) || Normalized != Code) { return ECatOnlineError::InvalidInviteCode; }
   }
   else if (!Password.IsEmpty() && Secret != Password)
   { return Secret.IsEmpty() ? ECatOnlineError::PasswordRequired : ECatOnlineError::PasswordIncorrect; }
 }
 int32 Occupied = 1;
 for (const auto& Entry : Grants) { if (Entry.Key != Peer && Entry.Value.Expires > Now) { ++Occupied; } }
 if (Occupied >= MaxPlayers && Peer != HostOwner) { return ECatOnlineError::SessionFull; }
 Grants.FindOrAdd(Peer).Expires = Now + ReservationSeconds;
 Invited.Remove(Peer);
 return ECatOnlineError::None;
}

bool UCatRoomAdmission::BeginClient(const FString& OwnerId, int32 Port, const FString& LobbyId, const FString& Secret, bool bCode,
 FGuid RequestId, TFunction<void(ECatOnlineError)> Callback)
{
 CancelClient();
 if (!GetWorld() || !OwnerId.IsNumeric() || Port <= 0 || Port > 65535 || Secret.Len() > 64) { return false; }
 const UCatOnlineSettings* Settings = GetDefault<UCatOnlineSettings>();
 if (!Settings->HasValidAdmissionTimeouts()) { return false; }
 ACatRoomAdmissionClient* Beacon = GetWorld()->SpawnActor<ACatRoomAdmissionClient>();
 if (!Beacon) { return false; }
 Client = Beacon; ClientCallback = MoveTemp(Callback); ClientStartedAt = FPlatformTime::Seconds();
 ClientDeadline = ClientStartedAt + Settings->AdmissionRequestTimeoutSeconds;
 Beacon->PendingLobby = LobbyId; Beacon->PendingSecret = Secret; Beacon->bPendingCode = bCode; Beacon->RequestId = RequestId;
 FURL URL(nullptr, *FString::Printf(TEXT("steam.%s:%d"), *OwnerId, Port), TRAVEL_Absolute);
 UE_LOG(LogCatOnline, Log, TEXT("Event=room_admission_requested RequestId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s Port=%d Method=%s Phase=Connect ConnectTimeoutSeconds=%.2f RequestTimeoutSeconds=%.2f OperationTimeoutSeconds=%.2f"),
   *RequestId.ToString(), *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), Beacon->HasAuthority(), int32(Beacon->GetLocalRole()), *Beacon->GetName(), Port,
   bCode ? TEXT("Code") : TEXT("Standard"), Settings->AdmissionConnectTimeoutSeconds, Settings->AdmissionRequestTimeoutSeconds, Settings->AdmissionOperationTimeoutSeconds);
 if (!Beacon->InitClient(URL)) { CompleteClient(Beacon, ECatOnlineError::AdmissionConnectionFailed); return false; }
 if (!IsCurrentClient(Beacon)) { return false; }
 if (!TickHandle.IsValid()) { TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Tick), 0.5f); }
 return true;
}
void UCatRoomAdmission::CancelClient()
{
 if (ACatRoomAdmissionClient* Beacon = Client.Get(); Beacon && ClientCallback)
 {
   UE_LOG(LogCatOnline, Log, TEXT("Event=room_admission_cancelled RequestId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s ElapsedSeconds=%.3f Result=Cancelled"),
     *Beacon->RequestId.ToString(), *GetNameSafe(GetWorld()), int32(Beacon->GetNetMode()), Beacon->HasAuthority(), int32(Beacon->GetLocalRole()), *Beacon->GetName(), FPlatformTime::Seconds() - ClientStartedAt);
 }
 ClientCallback = nullptr; ClientDeadline = 0; ClientStartedAt = 0;
 if (ACatRoomAdmissionClient* Beacon = Client.Get()) { Client.Reset(); Beacon->PendingSecret.Reset(); Beacon->DestroyBeacon(); }
}
bool UCatRoomAdmission::IsCurrentClient(const ACatRoomAdmissionClient* Source) const
{
 return Source && Client.Get() == Source && bool(ClientCallback);
}
void UCatRoomAdmission::CompleteClient(ACatRoomAdmissionClient* Source, ECatOnlineError Error)
{
 if (IsCurrentClient(Source)) { CompleteClient(Error); }
}
void UCatRoomAdmission::CompleteClient(ECatOnlineError Error)
{
 if (!ClientCallback) { return; }
 const ACatRoomAdmissionClient* Beacon = Client.Get();
 UE_LOG(LogCatOnline, Log, TEXT("Event=room_admission_completed RequestId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s ElapsedSeconds=%.3f Result=%s"),
   Beacon ? *Beacon->RequestId.ToString() : TEXT("None"), *GetNameSafe(GetWorld()), GetWorld() ? int32(GetWorld()->GetNetMode()) : -1,
   Beacon ? Beacon->HasAuthority() : false, Beacon ? int32(Beacon->GetLocalRole()) : -1, *GetNameSafe(Beacon), FPlatformTime::Seconds() - ClientStartedAt, *UEnum::GetValueAsString(Error));
 TFunction<void(ECatOnlineError)> Callback = MoveTemp(ClientCallback);
 CancelClient();
 if (Callback) { Callback(Error); }
}
ECatOnlineError UCatRoomAdmission::ValidateSettings(const FString& Name, int32 Capacity, const FString& NewPassword) const
{
 int32 Reservations = 1;
 for (const auto& Entry : Grants) { if (Entry.Value.Expires > FPlatformTime::Seconds()) { ++Reservations; } }
 return !IsHosting() || Name.TrimStartAndEnd().IsEmpty() || Name.Len() > 32 || NewPassword.Len() > 32
   || Capacity < Reservations || Capacity > 4 ? ECatOnlineError::RoomSettingsInvalid : ECatOnlineError::None;
}
void UCatRoomAdmission::ApplySettings(const FString& Name, int32 Capacity, const FString& NewPassword, bool bClearPassword)
{
 RoomName = Name.TrimStartAndEnd(); MaxPlayers = Capacity;
 if (bClearPassword) { Password.Reset(); } else if (!NewPassword.IsEmpty()) { Password = NewPassword; }
 PublishFacts();
}
void UCatRoomAdmission::PublishFacts()
{
#if WITH_STEAMWORKS
 if (!IsHosting() || !SteamMatchmaking()) { return; }
 const CSteamID Lobby(FCString::Strtoui64(*HostLobby, nullptr, 10));
 bool bPublished = SteamMatchmaking()->SetLobbyData(Lobby, "CAT_ADMISSION_PORT_s", TCHAR_TO_UTF8(*LexToString(ListenPort)));
 bPublished &= SteamMatchmaking()->SetLobbyData(Lobby, "CAT_PASSWORD_s", Password.IsEmpty() ? "0" : "1");
 bPublished &= SteamMatchmaking()->SetLobbyData(Lobby, "CAT_CAPACITY_s", TCHAR_TO_UTF8(*LexToString(MaxPlayers)));
 if (!RoomName.IsEmpty()) { bPublished &= SteamMatchmaking()->SetLobbyData(Lobby, "CAT_ROOM_NAME_s", TCHAR_TO_UTF8(*RoomName)); }
 bFactsDirty = !bPublished;
 NextFactsAttempt = FPlatformTime::Seconds() + 2.0;
 if (!bPublished && !bFactsFailureLogged)
 { UE_LOG(LogCatOnline, Warning, TEXT("Event=room_facts_publish_failed World=%s NetMode=%d Authority=1 Result=RetryPending"), *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode())); }
 if (bPublished && bFactsFailureLogged)
 { UE_LOG(LogCatOnline, Log, TEXT("Event=room_facts_publish_recovered World=%s NetMode=%d Authority=1"), *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode())); }
 bFactsFailureLogged = !bPublished;
#endif
}
bool UCatRoomAdmission::Tick(float DeltaSeconds)
{
 const double Now = FPlatformTime::Seconds();
 if (ClientDeadline > 0 && Now >= ClientDeadline)
 {
   UE_LOG(LogCatOnline, Warning, TEXT("Event=room_admission_deadline_expired RequestId=%s World=%s NetMode=%d ElapsedSeconds=%.3f Result=AdmissionTimedOut"),
     Client.IsValid() ? *Client->RequestId.ToString() : TEXT("None"), *GetNameSafe(GetWorld()), GetWorld() ? int32(GetWorld()->GetNetMode()) : -1, Now - ClientStartedAt);
   CompleteClient(ECatOnlineError::AdmissionTimedOut);
 }
#if WITH_STEAMWORKS
 if (!IsHosting() || !SteamMatchmaking()) { return true; }
 if (bFactsDirty && Now >= NextFactsAttempt) { PublishFacts(); }
 const CSteamID Lobby(FCString::Strtoui64(*HostLobby, nullptr, 10));
 TSet<FString> Members;
 for (int32 Index = 0; Index < SteamMatchmaking()->GetNumLobbyMembers(Lobby); ++Index)
 { Members.Add(LexToString(SteamMatchmaking()->GetLobbyMemberByIndex(Lobby, Index).ConvertToUint64())); }
 // 退出平台 Lobby 不得凭空释放仍在 UE 游戏连接中的席位。
 if (UWorld* World = GetWorld())
 {
   for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
   {
     const APlayerController* PC = It->Get();
     if (PC && PC->PlayerState && PC->PlayerState->GetUniqueId().IsValid()) { Members.Add(PC->PlayerState->GetUniqueId()->ToString()); }
   }
 }
 FString Approved = FMD5::HashAnsiString(*(HostLobby + TEXT(":") + HostOwner)) + TEXT(";");
 for (auto It = Grants.CreateIterator(); It; ++It)
 {
   if (!It.Value().bObservedMember && It.Value().Expires <= Now) { It.RemoveCurrent(); continue; }
   if (Members.Contains(It.Key())) { It.Value().bObservedMember = true; It.Value().Expires = Now + ReservationSeconds; Approved += FMD5::HashAnsiString(*(HostLobby + TEXT(":") + It.Key())) + TEXT(";"); }
   else if (It.Value().bObservedMember || It.Value().Expires <= Now) { It.RemoveCurrent(); }
 }
 for (auto It = Invited.CreateIterator(); It; ++It) { if (It.Value() <= Now) { It.RemoveCurrent(); } }
 for (auto It = Attempts.CreateIterator(); It; ++It) { if (It.Value().Until <= Now) { It.RemoveCurrent(); } }
 if (Approved != LastPublishedMembers)
 {
   if (SteamMatchmaking()->SetLobbyData(Lobby, "CAT_ADMITTED_s", TCHAR_TO_UTF8(*Approved)))
   {
     LastPublishedMembers = Approved; bMembersFailureLogged = false;
     UE_LOG(LogCatOnline, Log, TEXT("Event=room_members_published World=%s NetMode=%d Authority=1 Count=%d"),
       *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), Approved.Len() / 33);
   }
   else if (!bMembersFailureLogged)
   {
     bMembersFailureLogged = true;
     UE_LOG(LogCatOnline, Warning, TEXT("Event=room_members_publish_failed World=%s NetMode=%d Authority=1 Result=RetryPending"),
       *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()));
   }
 }
#endif
 return true;
}
