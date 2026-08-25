#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Online/CatOnlineSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatOnlineSettingsReconnectAndExitTest,
	"Catfishing.Unit.Online.Settings.ReconnectAndHostExitTimeoutFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：先清掉 Config 对象从 DefaultGame.ini 继承的正式地图与 Host exit 配置来验证未配置时 fail-closed，再填回有效值证明正式 Lake 与正超时可被读取。
bool FCatOnlineSettingsReconnectAndExitTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatOnlineSettings* Settings = NewObject<UCatOnlineSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 Online Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	double TimeoutSeconds = 9.0;
	FString GameplayMapPackage(TEXT("stale"));
	Settings->GameplayMap = TSoftObjectPtr<UWorld>();
	Settings->HostExitAckTimeoutSeconds = 0.0;
	TestFalse(TEXT("默认玩法地图未配置时读取失败"), Settings->TryGetGameplayMapPackage(GameplayMapPackage));
	TestTrue(TEXT("玩法地图读取失败时清空输出"), GameplayMapPackage.IsEmpty());
	Settings->GameplayMap = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Catfishing/Maps/Lake.Lake")));
	TestTrue(TEXT("有效 World 软路径可读取为长包名"), Settings->TryGetGameplayMapPackage(GameplayMapPackage));
	TestEqual(TEXT("玩法地图规范化为不含对象名的长包名"), GameplayMapPackage, FString(TEXT("/Game/Catfishing/Maps/Lake")));
	TestFalse(TEXT("默认重连准入不可用"), Settings->IsReconnectAdmissionReady());
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
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
