#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Online/CatOnlineSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatOnlineSettingsReconnectAndExitTest,
	"Catfishing.Unit.Online.Settings.ReconnectAndHostExitTimeoutFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatOnlineSettingsPublicConnectionLimitTest,
	"Catfishing.Unit.Online.Settings.PublicConnectionLimitIsOneToEight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatOnlineSettingsProjectDefaultsTest,
	"Catfishing.Unit.Online.Settings.ProjectDefaultsEnableFriendsSession",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatOnlineSettingsReconnectProjectDefaultsTest,
	"Catfishing.Unit.Online.Settings.ProjectDefaultsEnableConnectionLostReconnect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：构造独立 Online Settings 并显式写入未裁重连和 Host exit 状态；项目默认值另有测试覆盖，本用例只验证白名
// 单、TTL 与 Host exit 超时读取 gate 的边界。
bool FCatOnlineSettingsReconnectAndExitTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatOnlineSettings* Settings = NewObject<UCatOnlineSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 Online Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	Settings->ReconnectRecordTtlSeconds = INDEX_NONE;
	Settings->RecoverableFailureMask = -1;
	Settings->HostExitAckTimeoutSeconds = 0.0;
	double TimeoutSeconds = 9.0;
	TestFalse(TEXT("显式未裁重连准入不可用"), Settings->IsReconnectAdmissionReady());
	TestFalse(TEXT("默认 Host exit 超时读取失败"), Settings->TryGetHostExitAckTimeout(TimeoutSeconds));
	TestEqual(TEXT("失败时 Host exit 超时清零"), TimeoutSeconds, 0.0);

	Settings->ReconnectRecordTtlSeconds = 30;
	Settings->RecoverableFailureMask = static_cast<int64>(ECatRecoverableFailure::ConnectionLost);
	TestTrue(TEXT("只允许 ConnectionLost 且 TTL 为正时重连准入可用"), Settings->IsReconnectAdmissionReady());
	Settings->RecoverableFailureMask = 3;
	TestFalse(TEXT("出现未识别恢复位时重连准入 fail-closed"), Settings->IsReconnectAdmissionReady());

	Settings->HostExitAckTimeoutSeconds = 4.5;
	TestTrue(TEXT("正 Host exit ACK 超时可读取"), Settings->TryGetHostExitAckTimeout(TimeoutSeconds));
	TestEqual(TEXT("Host exit ACK 超时保持配置值"), TimeoutSeconds, 4.5);

	// 平台操作超时和 Host exit ACK 超时是两个独立窗口：前者约束 Create/Find/Join/Destroy 等待平台回调，后者约束等待远端 ACK。
	Settings->SessionOperationTimeoutSeconds = 0.0;
	double SessionOperationTimeoutSeconds = 9.0;
	TestFalse(TEXT("未裁平台操作超时读取失败"), Settings->TryGetSessionOperationTimeout(SessionOperationTimeoutSeconds));
	TestEqual(TEXT("失败时平台操作超时清零"), SessionOperationTimeoutSeconds, 0.0);
	Settings->SessionOperationTimeoutSeconds = 12.5;
	TestTrue(TEXT("正平台操作超时可读取"), Settings->TryGetSessionOperationTimeout(SessionOperationTimeoutSeconds));
	TestEqual(TEXT("平台操作超时保持配置值"), SessionOperationTimeoutSeconds, 12.5);
	return !HasAnyErrors();
}

// 测试流程：构造独立 Online 设置对象，只通过公开读取方法验证建局人数 gate；显式写成 0 的未裁状态和 9 人必须
// fail-closed，1 人与 8 人分别锁住单人和满员边界。
bool FCatOnlineSettingsPublicConnectionLimitTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatOnlineSettings* Settings = NewObject<UCatOnlineSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 Online Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	Settings->SessionPublicConnectionLimit = 0;
	int32 PublicConnectionLimit = 7;
	TestFalse(TEXT("显式 0 人建局人数不可读取"), Settings->TryGetSessionPublicConnectionLimit(PublicConnectionLimit));
	TestEqual(TEXT("0 人失败时建局人数清零"), PublicConnectionLimit, 0);

	Settings->SessionPublicConnectionLimit = 1;
	TestTrue(TEXT("1 人好友局是合法下界"), Settings->TryGetSessionPublicConnectionLimit(PublicConnectionLimit));
	TestEqual(TEXT("1 人边界原样输出"), PublicConnectionLimit, 1);

	Settings->SessionPublicConnectionLimit = 8;
	TestTrue(TEXT("8 人好友局是合法上界"), Settings->TryGetSessionPublicConnectionLimit(PublicConnectionLimit));
	TestEqual(TEXT("8 人边界原样输出"), PublicConnectionLimit, 8);

	Settings->SessionPublicConnectionLimit = 0;
	PublicConnectionLimit = 8;
	TestFalse(TEXT("0 人建局人数保持 fail-closed"), Settings->TryGetSessionPublicConnectionLimit(PublicConnectionLimit));
	TestEqual(TEXT("0 人失败时输出清零"), PublicConnectionLimit, 0);

	Settings->SessionPublicConnectionLimit = 9;
	PublicConnectionLimit = 8;
	TestFalse(TEXT("超过 8 人不被当前好友局合同接受"), Settings->TryGetSessionPublicConnectionLimit(PublicConnectionLimit));
	TestEqual(TEXT("超过上界失败时输出清零"), PublicConnectionLimit, 0);

	return !HasAnyErrors();
}

// 测试流程：读取项目配置驱动的 Online Settings 默认对象；它锁定 WORK-02 当前工程决策——新建局默认是 8 人好友局，同时避
// 免把临时测试对象的手动赋值误当成项目默认行为。
bool FCatOnlineSettingsProjectDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UCatOnlineSettings* Settings = GetDefault<UCatOnlineSettings>();
	TestNotNull(TEXT("项目默认 Online Settings 可以被配置系统加载"), Settings);
	if (!Settings)
	{
		return false;
	}

	TestEqual(TEXT("项目默认建局可见性采用好友局策略"), Settings->SessionAccess, ECatSessionAccessPolicy::FriendsOnly);

	int32 PublicConnectionLimit = 0;
	TestTrue(TEXT("项目默认建局人数必须通过 1 到 8 人合同"), Settings->TryGetSessionPublicConnectionLimit(PublicConnectionLimit));
	TestEqual(TEXT("项目默认好友局人数上限为 8"), PublicConnectionLimit, 8);

	return !HasAnyErrors();
}

// 测试流程：读取项目配置驱动的 Online Settings 默认对象；它锁定 WORK-02 当前重连裁决——只有 ConnectionLost 在同一权威
// 进程内保留 60 秒准入，主动离局和过期记录都不能被当作恢复。
bool FCatOnlineSettingsReconnectProjectDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UCatOnlineSettings* Settings = GetDefault<UCatOnlineSettings>();
	TestNotNull(TEXT("项目默认 Online Settings 可以被配置系统加载"), Settings);
	if (!Settings)
	{
		return false;
	}

	TestEqual(TEXT("项目默认重连 TTL 为 60 秒"), Settings->ReconnectRecordTtlSeconds, 60);
	TestEqual(TEXT("项目默认只允许 ConnectionLost 建立恢复记录"), Settings->RecoverableFailureMask,
		static_cast<int64>(ECatRecoverableFailure::ConnectionLost));
	TestEqual(TEXT("主动离局默认不保留恢复资格"), Settings->VoluntaryLeaveRecovery, ECatPolicyDecision::Disabled);
	TestEqual(TEXT("过期恢复记录默认不转成普通中途加入"), Settings->ExpiredAdmission, ECatPolicyDecision::Disabled);
	TestTrue(TEXT("项目默认 ConnectionLost 重连 gate 可用"), Settings->IsReconnectAdmissionReady());

	double HostExitAckTimeoutSeconds = 0.0;
	TestTrue(TEXT("项目默认 Host exit ACK 超时可读取"), Settings->TryGetHostExitAckTimeout(HostExitAckTimeoutSeconds));
	TestEqual(TEXT("项目默认 Host exit ACK 超时为 10 秒"), HostExitAckTimeoutSeconds, 10.0);

	// 30 秒是本轮登记的工程暂定值（飞书对平台回调等待时长没有裁定）；
	// 这条断言故意直接锁该值，因此 Config/DefaultGame.ini 补上 SessionOperationTimeoutSeconds=30.0 之前它会失败，
	// 而在补上之前 Create/Find/Join/Destroy 会以 PolicyUndecided 拒绝提交，不会无界等待。
	double SessionOperationTimeoutSeconds = 0.0;
	TestTrue(TEXT("项目默认平台操作超时可读取"), Settings->TryGetSessionOperationTimeout(SessionOperationTimeoutSeconds));
	TestEqual(TEXT("项目默认平台操作超时为 30 秒"), SessionOperationTimeoutSeconds, 30.0);

	return !HasAnyErrors();
}
#endif // WITH_DEV_AUTOMATION_TESTS
