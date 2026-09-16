#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Online/CatRoomChatProtocol.h"
#include "CatRoomChatSubsystem.generated.h"

class UCatOnlineSubsystem;
struct FCatOnlineSnapshot;
struct FCatRoomChatSteamBridge;

USTRUCT(BlueprintType)
struct FCatRoomChatMessage
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FGuid MessageId;
    UPROPERTY(BlueprintReadOnly) FString SenderName;
    UPROPERTY(BlueprintReadOnly) FString Text;
    UPROPERTY(BlueprintReadOnly) bool bLocal = false;
    UPROPERTY(BlueprintReadOnly) bool bSystem = false;
    UPROPERTY(BlueprintReadOnly) bool bPending = false;
    UPROPERTY(BlueprintReadOnly) bool bFailed = false;
    double SubmittedAt = 0;
};

DECLARE_MULTICAST_DELEGATE(FCatRoomChatChanged);

/** 同一 Lobby 的瞬态文字历史跨 World 保存；Session 生命周期仍由 Online 唯一管理。 */
UCLASS()
class CATFISHING_API UCatRoomChatSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    bool Send(const FString& Text, FText& Error);
    bool IsAvailable() const { return bAvailable; }
    const TArray<FCatRoomChatMessage>& GetMessages() const { return Messages; }
    const FString& GetRoomKey() const { return LobbyId; }
    FCatRoomChatChanged OnChanged;
private:
    friend struct FCatRoomChatSteamBridge;
    friend class FCatRoomChatLifecycleTest;
    void RefreshRoom();
    void ApplySnapshot(const FCatOnlineSnapshot& Snapshot);
    void Receive(uint64 Lobby, uint64 Sender, TConstArrayView<uint8> Bytes);
    void AcceptMessage(const FGuid& Id, const FString& SenderKey, const FString& SenderName, const FString& Text, bool bLocal);
    bool Tick(float Delta);
    void Append(FCatRoomChatMessage Message);
    void LogEvent(const TCHAR* Event, const FGuid& Id, const FString& Sender, const TCHAR* Result, bool bWarning = false) const;
    UPROPERTY(Transient) TObjectPtr<UCatOnlineSubsystem> Online;
    TSharedPtr<FCatRoomChatSteamBridge> Bridge;
    FDelegateHandle OnlineHandle;
    FTSTicker::FDelegateHandle TickHandle;
    FString LobbyId;
    uint64 RoomEpoch = 0;
    bool bAvailable = false;
    TArray<FCatRoomChatMessage> Messages;
    TMap<FGuid, FString> Members;
    TArray<FString> Seen;
    TMap<FString, CatRoomChatProtocol::FRateLimit> ReceiveLimits;
    CatRoomChatProtocol::FRateLimit SendLimit;
    double NextRejectedLog = 0;
};
