#pragma once

#include "CoreMinimal.h"
#include "Online/CatOnlineTypes.h"
#include "OnlineBeaconClient.h"
#include "OnlineBeaconHost.h"
#include "OnlineBeaconHostObject.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "CatRoomAdmission.generated.h"

/** 房主端授权，始终以 Steam 传输层对端身份为键；不信任客户端声明的邀请来源。 */
UCLASS()
class CATFISHING_API UCatRoomAdmission : public UGameInstanceSubsystem
{
 GENERATED_BODY()
public:
 virtual void Deinitialize() override;
 bool StartHost(const FString& LobbyId, const FString& OwnerId, const FString& InviteCode);
 void StopHost();
 void SuspendListener();
 bool ResumeListener();
 void Invite(const FString& PeerId);
 bool IsAuthorized(const FString& PeerId) const;
 bool ValidateGameplayPeer(const FString& Address, const FUniqueNetIdRepl& UniqueId) const;
 ECatOnlineError Authorize(class ACatRoomAdmissionClient* Client, const FString& LobbyId, const FString& Secret, bool bCode);
 bool BeginClient(const FString& OwnerId, int32 Port, const FString& LobbyId, const FString& Secret, bool bCode, FGuid RequestId,
   TFunction<void(ECatOnlineError)> Callback);
 void CancelClient();
 void CompleteClient(ECatOnlineError Error);
 ECatOnlineError ValidateSettings(const FString& Name, int32 Capacity, const FString& NewPassword) const;
 void ApplySettings(const FString& Name, int32 Capacity, const FString& NewPassword, bool bClearPassword);
 FString GetCode() const { return Code; }
 FString GetRoute() const { return Code.Left(3); }
 bool HasPassword() const { return !Password.IsEmpty(); }
 bool IsHosting() const { return !HostLobby.IsEmpty(); }
 int32 GetPort() const { return ListenPort; }
 static bool NormalizeCode(const FString& Input, FString& OutCode);
 static FString GenerateCode();
 static FString PeerIdentity(UNetConnection* Connection);
private:
 friend class FCatRoomAdmissionContractTest;
 ECatOnlineError AuthorizePeer(const FString& Peer, const FString& Secret, bool bCode);
 struct FGrant { double Expires = 0; bool bObservedMember = false; };
 struct FAttempts { double Until = 0; int32 Count = 0; };
 bool Tick(float DeltaSeconds);
 void PublishFacts();
 FString HostLobby, HostOwner, Password, Code, RoomName;
 int32 MaxPlayers = 4;
 int32 ListenPort = 0;
 TMap<FString, FGrant> Grants;
 TMap<FString, double> Invited;
 TMap<FString, FAttempts> Attempts;
 FString LastPublishedMembers;
 bool bFactsDirty = false;
 bool bFactsFailureLogged = false;
 bool bMembersFailureLogged = false;
 double NextFactsAttempt = 0;
 FTSTicker::FDelegateHandle TickHandle;
 TWeakObjectPtr<class ACatRoomAdmissionHost> Host;
 TWeakObjectPtr<class ACatRoomAdmissionClient> Client;
 TFunction<void(ECatOnlineError)> ClientCallback;
 double ClientDeadline = 0;
};

UCLASS(Transient, NotPlaceable)
class CATFISHING_API ACatRoomAdmissionHost : public AOnlineBeaconHost
{
 GENERATED_BODY()
public:
 ACatRoomAdmissionHost();
};

UCLASS(Transient, NotPlaceable)
class CATFISHING_API ACatRoomAdmissionHostObject : public AOnlineBeaconHostObject
{
 GENERATED_BODY()
public:
 ACatRoomAdmissionHostObject();
 virtual void OnClientConnected(AOnlineBeaconClient* NewClientActor, UNetConnection* ClientConnection) override;
};

/** Secret 只作为加密 Steam 通道上的 RPC 参数；绝不放到 URL、公开 Lobby 元数据或日志。 */
UCLASS(Transient, NotPlaceable)
class CATFISHING_API ACatRoomAdmissionClient : public AOnlineBeaconClient
{
 GENERATED_BODY()
public:
 ACatRoomAdmissionClient();
 FString PendingLobby, PendingSecret;
 bool bPendingCode = false;
 FGuid RequestId;
 virtual void OnConnected() override;
 virtual FString GetLoginOptions(const FUniqueNetIdRepl& PlayerId) override { return FString(); }
 virtual void OnFailure(EBeaconFailureReason Reason, FStringView ErrorMessage) override;
 UFUNCTION(Server, Reliable) void ServerAuthorize(const FString& Lobby, const FString& Secret, bool bCode, FGuid CorrelationId);
 UFUNCTION(Client, Reliable) void ClientDecision(ECatOnlineError Error, FGuid CorrelationId);
private:
 bool bSubmitted = false;
};
