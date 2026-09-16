#include "Online/CatRoomChatSubsystem.h"
#include "Online/CatOnlineSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Logging/CatLog.h"
#include "Misc/SecureHash.h"
#include "Misc/ScopeLock.h"

#if WITH_STEAMWORKS
THIRD_PARTY_INCLUDES_START
#include "steam/steam_api.h"
THIRD_PARTY_INCLUDES_END
#endif

struct FCatRoomChatSteamBridge
{
    struct FEnvelope { uint64 Lobby = 0; uint64 Sender = 0; uint64 Epoch = 0; TArray<uint8> Bytes; };
    FCriticalSection Mutex;
    uint64 CurrentLobby = 0;
    uint64 CurrentEpoch = 0;
    int32 Dropped = 0;
    TArray<FEnvelope> Inbox;
    void Configure(uint64 Lobby, uint64 Epoch)
    {
        FScopeLock Lock(&Mutex);
        if (CurrentLobby == Lobby && CurrentEpoch == Epoch) { return; }
        CurrentLobby = Lobby; CurrentEpoch = Epoch; Inbox.Reset(); Dropped = 0;
    }
    void Enqueue(uint64 Lobby, uint64 Sender, TConstArrayView<uint8> Bytes)
    {
        FScopeLock Lock(&Mutex);
        if (!CurrentLobby || Lobby != CurrentLobby) { return; }
        if (Inbox.Num() >= 64 || Bytes.Num() > CatRoomChatProtocol::HeaderBytes + CatRoomChatProtocol::MaxTextBytes)
        { Dropped = FMath::Min(1000000, Dropped + 1); return; }
        FEnvelope Item; Item.Lobby = Lobby; Item.Sender = Sender; Item.Epoch = CurrentEpoch;
        Item.Bytes.Append(Bytes.GetData(), Bytes.Num()); Inbox.Add(MoveTemp(Item));
    }
    TArray<FEnvelope> Drain(int32& Rejected)
    {
        FScopeLock Lock(&Mutex);
        TArray<FEnvelope> Result = MoveTemp(Inbox); Inbox.Reset(); Rejected = Dropped; Dropped = 0; return Result;
    }
#if WITH_STEAMWORKS
    CCallback<FCatRoomChatSteamBridge, LobbyChatMsg_t> Callback;
    FCatRoomChatSteamBridge() : Callback(this, &FCatRoomChatSteamBridge::OnMessage) {}
    void OnMessage(LobbyChatMsg_t* Event)
    {
        // OSS 在异步线程运行 SteamAPI_RunCallbacks。这里只复制 callback 有效期内的数据，不访问 UObject 或 UI。
        if (!SteamMatchmaking() || Event->m_eChatEntryType != k_EChatEntryTypeChatMsg) { return; }
        uint8 Buffer[4096];
        CSteamID Sender;
        EChatEntryType Type;
        const int32 Size = SteamMatchmaking()->GetLobbyChatEntry(CSteamID(Event->m_ulSteamIDLobby), Event->m_iChatID, &Sender, Buffer, sizeof(Buffer), &Type);
        if (Size > 0 && Size <= sizeof(Buffer) && Sender.ConvertToUint64() == Event->m_ulSteamIDUser)
        { Enqueue(Event->m_ulSteamIDLobby, Sender.ConvertToUint64(), MakeArrayView(Buffer, Size)); }
    }
#endif
};

void UCatRoomChatSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Online = Collection.InitializeDependency<UCatOnlineSubsystem>();
    OnlineHandle = Online->OnSnapshotChanged.AddUObject(this, &ThisClass::RefreshRoom);
    Bridge = MakeShared<FCatRoomChatSteamBridge>();
    TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Tick), .1f);
    RefreshRoom();
}

void UCatRoomChatSubsystem::Deinitialize()
{
    if (Online) { Online->OnSnapshotChanged.Remove(OnlineHandle); }
    FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
    Bridge.Reset();
    Messages.Reset(); Members.Reset(); Seen.Reset(); ReceiveLimits.Reset(); LobbyId.Reset();
    bAvailable = false; Online = nullptr; OnChanged.Clear();
    Super::Deinitialize();
}

void UCatRoomChatSubsystem::RefreshRoom()
{
    if (Online) { ApplySnapshot(Online->GetSnapshot()); }
}

void UCatRoomChatSubsystem::ApplySnapshot(const FCatOnlineSnapshot& Snapshot)
{
    const FString NextLobby = !Snapshot.bLocalRoomActive && Snapshot.SessionRole != ECatOnlineSessionRole::None ? Snapshot.LobbyId : FString();
    const bool bChangedRoom = NextLobby != LobbyId;
    bool bNotify = bChangedRoom;
    if (bChangedRoom)
    {
        ++RoomEpoch;
        LogEvent(TEXT("room_chat_reset"), FGuid(), FString(), TEXT("RoomChanged"));
        LobbyId = NextLobby;
        Messages.Reset(); Members.Reset(); Seen.Reset(); ReceiveLimits.Reset(); SendLimit = {};
        if (!LobbyId.IsEmpty())
        {
            FCatRoomChatMessage Welcome; Welcome.bSystem = true;
            Welcome.Text = TEXT("已加入房间，和伙伴打个招呼吧"); Append(MoveTemp(Welcome));
        }
    }
    bool bNextAvailable = !LobbyId.IsEmpty() && Snapshot.ActiveOperation != ECatOnlineOperation::Leave;
#if WITH_STEAMWORKS
    bNextAvailable &= SteamMatchmaking() != nullptr && SteamUser() != nullptr && SteamUser()->BLoggedOn();
#else
    bNextAvailable = false;
#endif
    bNotify |= bNextAvailable != bAvailable;
    bAvailable = bNextAvailable;
    if (Bridge) { Bridge->Configure(bAvailable ? FCString::Strtoui64(*LobbyId, nullptr, 10) : 0, RoomEpoch); }
    TMap<FGuid, FString> NextMembers;
    for (const auto& Member : Snapshot.RoomMembers)
    {
        NextMembers.Add(Member.MemberId, Member.DisplayName);
        if (!bChangedRoom && !LobbyId.IsEmpty() && !Members.Contains(Member.MemberId))
        {
            FCatRoomChatMessage Message; Message.bSystem = true; Message.Text = Member.DisplayName + TEXT(" 加入了房间");
            Append(MoveTemp(Message)); bNotify = true;
        }
    }
    if (!bChangedRoom && !LobbyId.IsEmpty())
    {
        for (const auto& Member : Members)
        {
            if (!NextMembers.Contains(Member.Key))
            {
                FCatRoomChatMessage Message; Message.bSystem = true; Message.Text = Member.Value + TEXT(" 离开了房间");
                Append(MoveTemp(Message)); bNotify = true;
            }
        }
    }
    Members = MoveTemp(NextMembers);
    if (bNotify) { OnChanged.Broadcast(); }
}

bool UCatRoomChatSubsystem::Send(const FString& Input, FText& Error)
{
    RefreshRoom();
    const FString Text = Input.TrimStartAndEnd();
    const FGuid Id = FGuid::NewGuid();
    auto Reject = [&](const TCHAR* Code, const TCHAR* Hint)
    {
        Error = FText::FromString(Hint); LogEvent(TEXT("room_chat_send_rejected"), Id, FString(), Code, true); return false;
    };
    if (!bAvailable) { return Reject(TEXT("Unavailable"), TEXT("房间聊天暂不可用，请确认联机连接")); }
    TArray<uint8> Packet;
    if (!CatRoomChatProtocol::Encode(Id, Text, Packet)) { return Reject(TEXT("InvalidText"), TEXT("请输入 1–200 字的单行消息")); }
    if (!SendLimit.Accept(FPlatformTime::Seconds())) { return Reject(TEXT("RateLimited"), TEXT("发得太快了，稍等一下再发送")); }
#if WITH_STEAMWORKS
    LogEvent(TEXT("room_chat_send_requested"), Id, FMD5::HashAnsiString(*LexToString(SteamUser()->GetSteamID().ConvertToUint64())), TEXT("Submitting"));
    if (!SteamMatchmaking()->SendLobbyChatMsg(CSteamID(FCString::Strtoui64(*LobbyId, nullptr, 10)), Packet.GetData(), Packet.Num()))
    { return Reject(TEXT("SteamRejected"), TEXT("发送失败，请稍后重试")); }
    FCatRoomChatMessage Message;
    Message.MessageId = Id; Message.SenderName = TEXT("我"); Message.Text = Text;
    Message.bLocal = true; Message.bPending = true; Message.SubmittedAt = FPlatformTime::Seconds();
    Append(MoveTemp(Message)); OnChanged.Broadcast(); Error = FText::GetEmpty(); return true;
#else
    return Reject(TEXT("PlatformUnavailable"), TEXT("当前平台不支持房间聊天"));
#endif
}

void UCatRoomChatSubsystem::Receive(uint64 Lobby, uint64 Sender, TConstArrayView<uint8> Bytes)
{
    RefreshRoom();
    if (!bAvailable || LobbyId != LexToString(Lobby)) { return; }
#if WITH_STEAMWORKS
    const CSteamID LobbySteamId(Lobby), SenderSteamId(Sender);
    bool bMember = false;
    for (int32 I = 0; I < SteamMatchmaking()->GetNumLobbyMembers(LobbySteamId); ++I)
    { bMember |= SteamMatchmaking()->GetLobbyMemberByIndex(LobbySteamId, I) == SenderSteamId; }
    if (!bMember) { return; }
    FGuid Id; FString Text;
    const FString SenderKey = FMD5::HashAnsiString(*LexToString(Sender));
    const double Now = FPlatformTime::Seconds();
    if (!CatRoomChatProtocol::Decode(Bytes, Id, Text))
    {
        if (Now >= NextRejectedLog) { NextRejectedLog = Now + 5; LogEvent(TEXT("room_chat_receive_rejected"), Id, SenderKey, TEXT("InvalidPacket"), true); }
        return;
    }
    const FString Key = SenderKey + Id.ToString();
    if (Seen.Contains(Key)) { return; }
    if (!ReceiveLimits.Contains(SenderKey) && ReceiveLimits.Num() >= 128) { ReceiveLimits.Reset(); }
    if (!ReceiveLimits.FindOrAdd(SenderKey).Accept(Now))
    {
        if (Now >= NextRejectedLog) { NextRejectedLog = Now + 5; LogEvent(TEXT("room_chat_receive_rejected"), Id, SenderKey, TEXT("RateLimited"), true); }
        return;
    }
    FString Name = SteamFriends() ? UTF8_TO_TCHAR(SteamFriends()->GetFriendPersonaName(SenderSteamId)) : TEXT("伙伴");
    Name.ReplaceInline(TEXT("\n"), TEXT(" ")); Name.ReplaceInline(TEXT("\r"), TEXT(" ")); Name = Name.Left(40);
    AcceptMessage(Id, SenderKey, Name, Text, SteamUser()->GetSteamID() == SenderSteamId);
#endif
}

void UCatRoomChatSubsystem::AcceptMessage(const FGuid& Id, const FString& SenderKey, const FString& SenderName, const FString& Text, bool bLocal)
{
    const FString Key = SenderKey + Id.ToString();
    if (Seen.Contains(Key)) { return; }
    Seen.Add(Key); if (Seen.Num() > 512) { Seen.RemoveAt(0); }
    FCatRoomChatMessage* Pending = bLocal ? Messages.FindByPredicate([&](const auto& Message) { return Message.bLocal && Message.MessageId == Id; }) : nullptr;
    if (Pending) { Pending->bPending = false; Pending->bFailed = false; }
    else
    {
        FCatRoomChatMessage Message; Message.MessageId = Id; Message.SenderName = bLocal ? TEXT("我") : SenderName;
        Message.Text = Text; Message.bLocal = bLocal; Append(MoveTemp(Message));
    }
    LogEvent(TEXT("room_chat_received"), Id, SenderKey, bLocal ? TEXT("LocalEcho") : TEXT("Received"));
    OnChanged.Broadcast();
}

bool UCatRoomChatSubsystem::Tick(float Delta)
{
    check(IsInGameThread());
    if (Bridge)
    {
        int32 Dropped = 0;
        const TArray<FCatRoomChatSteamBridge::FEnvelope> Inbox = Bridge->Drain(Dropped);
        if (Dropped && FPlatformTime::Seconds() >= NextRejectedLog)
        {
            NextRejectedLog = FPlatformTime::Seconds() + 5;
            LogEvent(TEXT("room_chat_receive_rejected"), FGuid(), FString(), TEXT("InboxOrPacketLimit"), true);
        }
        for (const auto& Packet : Inbox)
        {
            if (Packet.Epoch == RoomEpoch) { Receive(Packet.Lobby, Packet.Sender, Packet.Bytes); }
        }
    }
    bool bChanged = false;
    for (auto& Message : Messages)
    {
        if (Message.bPending && FPlatformTime::Seconds() - Message.SubmittedAt > 10)
        {
            Message.bPending = false; Message.bFailed = true; bChanged = true;
            LogEvent(TEXT("room_chat_echo_timeout"), Message.MessageId, FString(), TEXT("DeliveryUnconfirmed"), true);
        }
    }
    if (bChanged) { OnChanged.Broadcast(); }
    return true;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Async/Async.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatRoomChatInboxTest, "Catfishing.Unit.Online.RoomChat.AsyncInbox", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCatRoomChatInboxTest::RunTest(const FString& Parameters)
{
    FCatRoomChatSteamBridge Bridge;
    Bridge.Configure(123, 1);
    TArray<uint8> Packet; CatRoomChatProtocol::Encode(FGuid::NewGuid(), TEXT("异步消息"), Packet);
    Async(EAsyncExecution::ThreadPool, [&]
    {
        Bridge.Enqueue(999, 10, Packet);
        for (int32 I = 0; I < 70; ++I) { Bridge.Enqueue(123, 10, Packet); }
    }).Get();
    int32 Dropped = 0;
    auto Inbox = Bridge.Drain(Dropped);
    TestEqual(TEXT("Async queue bounded"), Inbox.Num(), 64);
    TestEqual(TEXT("Overflow counted"), Dropped, 6);
    TestEqual(TEXT("Packet belongs to configured epoch"), Inbox[0].Epoch, uint64(1));
    TestTrue(TEXT("Callback payload copied"), Inbox[0].Bytes == Packet);
    Bridge.Enqueue(123, 10, Packet); Bridge.Configure(456, 2);
    TestTrue(TEXT("Room transition clears old inbox"), Bridge.Drain(Dropped).IsEmpty());
    Bridge.Enqueue(123, 10, Packet);
    TestTrue(TEXT("Late old-room callback rejected"), Bridge.Drain(Dropped).IsEmpty());
    Bridge.Configure(0, 3); Bridge.Enqueue(456, 10, Packet);
    TestTrue(TEXT("Disabled channel accepts nothing"), Bridge.Drain(Dropped).IsEmpty());
    return true;
}
#endif

void UCatRoomChatSubsystem::Append(FCatRoomChatMessage Message)
{
    if (!Message.MessageId.IsValid()) { Message.MessageId = FGuid::NewGuid(); }
    Messages.Add(MoveTemp(Message));
    if (Messages.Num() > CatRoomChatProtocol::HistoryLimit) { Messages.RemoveAt(0, Messages.Num() - CatRoomChatProtocol::HistoryLimit); }
}

void UCatRoomChatSubsystem::LogEvent(const TCHAR* Event, const FGuid& Id, const FString& Sender, const TCHAR* Result, bool bWarning) const
{
    const UWorld* World = GetWorld();
    const APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
    const FString Line = FString::Printf(TEXT("Event=%s World=%s NetMode=%d Authority=%d LocalRole=%d Lobby=%s MessageId=%s Member=%s Result=%s"),
        Event, *GetNameSafe(World), World ? int32(World->GetNetMode()) : -1, Player && Player->HasAuthority(), Player ? int32(Player->GetLocalRole()) : -1,
        *FMD5::HashAnsiString(*LobbyId), *Id.ToString(), *Sender, Result);
    if (bWarning) { UE_LOG(LogCatOnline, Warning, TEXT("%s"), *Line); }
    else { UE_LOG(LogCatOnline, Log, TEXT("%s"), *Line); }
}
