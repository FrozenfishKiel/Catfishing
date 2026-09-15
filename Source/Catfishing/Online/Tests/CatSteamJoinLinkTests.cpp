#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Online/CatSteamJoinLink.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatSteamJoinLinkParseTest, "Catfishing.Online.Join.LinkParsing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FCatSteamJoinLinkParseTest::RunTest(const FString& Parameters)
{
	// 完整 Lobby ID 可逆解析，拒绝其他游戏、非 Lobby 身份、溢出、URL 附加参数与壳命令。
	uint64 Lobby = 0;
	const FString Id(TEXT("109775241234567890"));
	TestTrue(TEXT("完整房间 ID"), CatSteamJoinLink::Parse(Id, 480, Lobby));
	TestEqual(TEXT("不损失 64 位精度"), Lobby, uint64(109775241234567890ULL));
	TestTrue(TEXT("首尾空白和链接"), CatSteamJoinLink::Parse(TEXT("  steam://joinlobby/480/") + Id + TEXT("/76561198000000000  "), 480, Lobby));
	TestTrue(TEXT("不依赖链接中的房主字段"), CatSteamJoinLink::Parse(TEXT("steam://joinlobby/480/") + Id, 480, Lobby));
	for (const FString& Input : TArray<FString>{ TEXT(""), TEXT("AB7K2Q"), TEXT("76561198000000000"), TEXT("18446744073709551616"),
		TEXT("steam://joinlobby/999/") + Id, TEXT("steam://joinlobby/480//") + Id,
		TEXT("steam://joinlobby/480/") + Id + TEXT("?command=quit"), TEXT("steam://joinlobby/480/") + Id + TEXT("/abc"),
		Id + TEXT(";quit"), TEXT("https://example.com"), TEXT("-1"), TEXT("+109775241234567890") })
	{
		Lobby = 42;
		TestFalse(TEXT("拒绝非法输入"), CatSteamJoinLink::Parse(Input, 480, Lobby));
		TestEqual(TEXT("失败清空输出"), Lobby, uint64(0));
	}
	return true;
}
#endif
