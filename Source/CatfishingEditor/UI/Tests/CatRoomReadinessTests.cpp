#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Online/CatOnlineRoomReadiness.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatRoomReadinessTest, "Catfishing.UI.Frontend.RoomReadiness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatRoomReadinessTest::RunTest(const FString& Parameters)
{
	FCatOnlineRoomMember Host;
	Host.MemberId = FGuid::NewGuid(); Host.bIsLocalPlayer = true; Host.bIsLobbyOwner = true;
	FCatOnlineRoomMember Guest;
	Guest.MemberId = FGuid::NewGuid(); Guest.DisplayName = Host.DisplayName;
	TArray<FCatOnlineRoomMember> Members{ Host };
	TestTrue(TEXT("房主单人点击开始无需先准备"), CatOnlineRoomReadiness::CanHostStart(Members, 1, 4));
	Members.Add(Guest);
	TestFalse(TEXT("新加入队员默认未准备"), CatOnlineRoomReadiness::CanHostStart(Members, 2, 4));
	Members[1].bIsReady = true;
	TestTrue(TEXT("同名成员独立准备后可开始"), CatOnlineRoomReadiness::CanHostStart(Members, 2, 4));
	Swap(Members[0], Members[1]);
	TestTrue(TEXT("列表重排不影响准备裁决"), CatOnlineRoomReadiness::CanHostStart(Members, 2, 4));
	Members[0].bIsReady = false;
	TestFalse(TEXT("取消准备阻止开始"), CatOnlineRoomReadiness::CanHostStart(Members, 2, 4));
	Members.RemoveAt(0);
	TestTrue(TEXT("未准备成员离开释放开始条件"), CatOnlineRoomReadiness::CanHostStart(Members, 1, 4));
	TestFalse(TEXT("成员尚未同步完整不能开始"), CatOnlineRoomReadiness::CanHostStart(Members, 2, 4));
	Members.Add(Host);
	TestFalse(TEXT("重复成员不能作为已准备证据"), CatOnlineRoomReadiness::CanHostStart(Members, 2, 4));
	Members = { Host, Guest }; Members[1].bIsReady = true;
	Members[0].bIsLocalPlayer = false; Members[1].bIsLocalPlayer = true;
	TestFalse(TEXT("客户端不能执行房主开始"), CatOnlineRoomReadiness::CanHostStart(Members, 2, 4));
	Members.Reset();
	TestFalse(TEXT("无已确认成员不能开始"), CatOnlineRoomReadiness::CanHostStart(Members, 0, 4));
	return true;
}
#endif
