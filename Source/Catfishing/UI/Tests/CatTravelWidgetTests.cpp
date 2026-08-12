#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Blueprint/UserWidget.h"
#include "UI/CatTravelWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatTravelWidgetClassAndHandleContractTest,
	"Catfishing.Unit.UI.TravelWidget.ClassAndOpaqueHandlesRemainViewOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：只验证 commandlet 可稳定观察的 View 合同；真实 Slate 按钮和本地玩家焦点需要可见 PIE 或后续 UI 功能测试承接。
bool FCatTravelWidgetClassAndHandleContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestTrue(TEXT("TravelWidget 是 UUserWidget 派生 View"), UCatTravelWidget::StaticClass()->IsChildOf(UUserWidget::StaticClass()));

	FCatSessionSearchHandle EmptySearchHandle;
	TestFalse(TEXT("空搜索句柄不可加入"), EmptySearchHandle.IsValid());
	FCatSessionSearchHandle SearchHandle;
	SearchHandle.Value = FGuid::NewGuid();
	TestTrue(TEXT("搜索句柄只以随机 opaque GUID 表示有效性"), SearchHandle.IsValid());

	FCatSessionInviteHandle EmptyInviteHandle;
	TestFalse(TEXT("空邀请句柄不可接受"), EmptyInviteHandle.IsValid());
	FCatSessionInviteHandle InviteHandle;
	InviteHandle.Value = FGuid::NewGuid();
	TestTrue(TEXT("邀请句柄只以随机 opaque GUID 表示有效性"), InviteHandle.IsValid());

	FCatOnlineSnapshot FrontendSnapshot;
	FrontendSnapshot.WorldState = ECatOnlineWorldState::Frontend;
	FrontendSnapshot.SessionState = ECatOnlineSessionState::NoSession;
	FrontendSnapshot.TransportState = ECatOnlineTransportState::Idle;
	FrontendSnapshot.ActiveOperation = ECatOnlineOperation::None;

	FCatSessionSearchSummary SearchSummary;
	SearchSummary.Handle = SearchHandle;
	SearchSummary.OwnerDisplayName = TEXT("HostA");
	SearchSummary.CurrentPlayers = 1;
	SearchSummary.MaxPlayers = 4;
	FrontendSnapshot.SearchResults.Add(SearchSummary);

	FCatSessionInviteSummary InviteSummary;
	InviteSummary.Handle = InviteHandle;
	InviteSummary.OwnerDisplayName = TEXT("InviteHost");
	FrontendSnapshot.AcceptedInvites.Add(InviteSummary);

	TestEqual(TEXT("Snapshot 保留搜索 opaque 句柄"), FrontendSnapshot.SearchResults[0].Handle.Value, SearchHandle.Value);
	TestEqual(TEXT("Snapshot 保留邀请 opaque 句柄"), FrontendSnapshot.AcceptedInvites[0].Handle.Value, InviteHandle.Value);
	TestEqual(TEXT("Snapshot 不从搜索结果推断 SessionRole"), FrontendSnapshot.SessionRole, ECatOnlineSessionRole::None);
	TestEqual(TEXT("Snapshot 不从邀请结果推断 ActiveOperation"), FrontendSnapshot.ActiveOperation, ECatOnlineOperation::None);

	FCatOnlineSnapshot LakeSnapshot;
	LakeSnapshot.WorldState = ECatOnlineWorldState::Lake;
	LakeSnapshot.SessionState = ECatOnlineSessionState::Host;
	LakeSnapshot.TransportState = ECatOnlineTransportState::Connected;
	LakeSnapshot.SessionRole = ECatOnlineSessionRole::Host;
	TestEqual(TEXT("Lake 快照显式携带 Host 角色"), LakeSnapshot.SessionRole, ECatOnlineSessionRole::Host);
	TestEqual(TEXT("Lake 快照不需要搜索结果才能允许离局语义"), LakeSnapshot.SearchResults.Num(), 0);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
