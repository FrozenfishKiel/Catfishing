#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Online/CatRoomChatProtocol.h"
#include "Online/CatRoomChatSubsystem.h"
#include "Online/CatOnlineTypes.h"
#include "Engine/GameInstance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatRoomChatProtocolTest, "Catfishing.Unit.Online.RoomChat.Protocol", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCatRoomChatProtocolTest::RunTest(const FString& Parameters)
{
    using namespace CatRoomChatProtocol;
    const FGuid Original = FGuid::NewGuid(); FGuid Id; FString Text; TArray<uint8> Packet;
    TestTrue(TEXT("Chinese UTF8 encodes"), Encode(Original, TEXT("等我换一下鱼竿 <tag>hello</tag>"), Packet));
    TestTrue(TEXT("Unicode and markup remain plain text"), Decode(Packet, Id, Text));
    TestEqual(TEXT("Message identity retained"), Id, Original);
    TestEqual(TEXT("No interpretation of text"), Text, FString(TEXT("等我换一下鱼竿 <tag>hello</tag>")));
    const TArray<uint8> Valid = Packet;
    for (int32 Length = 0; Length <= HeaderBytes; ++Length)
    { TestFalse(TEXT("Truncated headers rejected"), Decode(MakeArrayView(Valid.GetData(), Length), Id, Text)); }
    Packet = Valid; Packet[4] = 2;
    TestFalse(TEXT("Unknown protocol rejected"), Decode(Packet, Id, Text));
    Packet = Valid; Packet[HeaderBytes] = 0xff;
    TestFalse(TEXT("Malformed UTF8 rejected"), Decode(Packet, Id, Text));
    Packet = Valid; Packet[HeaderBytes] = 0;
    TestFalse(TEXT("Embedded NUL rejected"), Decode(Packet, Id, Text));
    Packet = Valid; Packet.SetNum(HeaderBytes + MaxTextBytes + 1);
    TestFalse(TEXT("Oversize packets rejected before conversion"), Decode(Packet, Id, Text));
    TestFalse(TEXT("Whitespace rejected"), ValidateText(TEXT("   ")));
    TestFalse(TEXT("Multiline rejected"), ValidateText(TEXT("hello\nworld")));
    TestTrue(TEXT("200 Chinese characters accepted"), ValidateText(FString::ChrN(200, TCHAR(0x732b))));
    TestFalse(TEXT("201 characters rejected"), ValidateText(FString::ChrN(201, TCHAR(0x732b))));
    TestFalse(TEXT("Invalid id rejected"), Encode(FGuid(), TEXT("hello"), Packet));
    FRateLimit Limit;
    TestTrue(TEXT("Burst 1"), Limit.Accept(10)); TestTrue(TEXT("Burst 2"), Limit.Accept(10)); TestTrue(TEXT("Burst 3"), Limit.Accept(10));
    TestFalse(TEXT("Fourth burst blocked"), Limit.Accept(10));
    TestFalse(TEXT("Partial token cannot send"), Limit.Accept(10.5));
    TestTrue(TEXT("One second restores a token"), Limit.Accept(11));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatRoomChatLifecycleTest, "Catfishing.Unit.Online.RoomChat.Lifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCatRoomChatLifecycleTest::RunTest(const FString& Parameters)
{
    UGameInstance* Game = NewObject<UGameInstance>();
    UCatRoomChatSubsystem* Chat = NewObject<UCatRoomChatSubsystem>(Game);
    FCatOnlineSnapshot Snapshot; Snapshot.LobbyId = TEXT("123"); Snapshot.SessionRole = ECatOnlineSessionRole::Host;
    Snapshot.RoomMembers.AddDefaulted(); Snapshot.RoomMembers[0].MemberId = FGuid::NewGuid(); Snapshot.RoomMembers[0].DisplayName = TEXT("猫");
    Chat->ApplySnapshot(Snapshot);
    const FGuid Id = FGuid::NewGuid();
    Chat->AcceptMessage(Id, TEXT("sender"), TEXT("伙伴"), TEXT("你好"), false);
    const int32 Count = Chat->Messages.Num();
    Chat->AcceptMessage(Id, TEXT("sender"), TEXT("伙伴"), TEXT("你好"), false);
    TestEqual(TEXT("Same sender/id delivered once"), Chat->Messages.Num(), Count);
    Chat->AcceptMessage(Id, TEXT("other"), TEXT("另一个伙伴"), TEXT("不同身份"), false);
    TestEqual(TEXT("Different sender cannot suppress a message"), Chat->Messages.Num(), Count + 1);
    Snapshot.WorldState = ECatOnlineWorldState::Lake;
    Chat->ApplySnapshot(Snapshot);
    TestEqual(TEXT("World travel retains history"), Chat->Messages.Num(), Count + 1);
    FCatRoomChatMessage Pending; Pending.MessageId = FGuid::NewGuid(); Pending.Text = TEXT("本地消息"); Pending.bLocal = true; Pending.bPending = true;
    Pending.SubmittedAt = FPlatformTime::Seconds(); Chat->Append(Pending);
    const int32 BeforeEcho = Chat->Messages.Num();
    Chat->AcceptMessage(Pending.MessageId, TEXT("local"), TEXT("我"), Pending.Text, true);
    TestEqual(TEXT("Self echo updates pending instead of appending"), Chat->Messages.Num(), BeforeEcho);
    TestFalse(TEXT("Self echo settles pending"), Chat->Messages.Last().bPending);
    Pending.MessageId = FGuid::NewGuid(); Pending.SubmittedAt -= 20; Chat->Append(Pending); Chat->Tick(0);
    TestTrue(TEXT("Unconfirmed send visibly times out"), Chat->Messages.Last().bFailed);
    Chat->AcceptMessage(Pending.MessageId, TEXT("local"), TEXT("我"), Pending.Text, true);
    TestFalse(TEXT("Late self echo settles timeout"), Chat->Messages.Last().bFailed);
    for (int32 I = 0; I < 130; ++I) { Chat->AcceptMessage(FGuid::NewGuid(), TEXT("sender"), TEXT("伙伴"), TEXT("边界"), false); }
    TestEqual(TEXT("History bounded"), Chat->Messages.Num(), 100);
    Snapshot.LobbyId = TEXT("456"); Chat->ApplySnapshot(Snapshot);
    TestEqual(TEXT("New room contains only welcome"), Chat->Messages.Num(), 1);
    TestTrue(TEXT("New room resets dedup"), Chat->Seen.IsEmpty());
    Snapshot.SessionRole = ECatOnlineSessionRole::None; Chat->ApplySnapshot(Snapshot);
    TestTrue(TEXT("Leave clears history"), Chat->Messages.IsEmpty());
    TestFalse(TEXT("Offline has no transport"), Chat->IsAvailable());
    FText Error; TestFalse(TEXT("Offline sends rejected"), Chat->Send(TEXT("hello"), Error));
    TestFalse(TEXT("Offline failure is visible"), Error.IsEmpty());
    return true;
}
#endif
