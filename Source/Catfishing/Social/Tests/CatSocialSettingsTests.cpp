#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Social/CatSocialSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatSocialSettingsPolicyReadinessTest,
	"Catfishing.Unit.Social.Settings.TheftMischiefAndManualHelpHaveIndependentGates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：在一份瞬态 Social Settings 上分别补齐偷鱼、恶作剧和手动求助配置；三个公开 readiness 必须互相独立，不能因为某一路完整而开放另一条未裁路径。
bool FCatSocialSettingsPolicyReadinessTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatSocialSettings* Settings = NewObject<UCatSocialSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 Social Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	TestFalse(TEXT("默认偷鱼不可运行"), Settings->IsTheftReady());
	TestFalse(TEXT("默认恶作剧不可运行"), Settings->IsMischiefReady());
	TestFalse(TEXT("默认手动求助不可运行"), Settings->IsManualHelpReady());

	Settings->bEnableSocialRuntime = true;
	Settings->TheftPermission = ECatDomainPolicy::Enabled;
	Settings->TheftEatingWindowSeconds = 8.0;
	Settings->TheftInteractionRangeCentimeters = 150.0;
	Settings->TheftCatchRangeCentimeters = 100.0;
	Settings->SharedTankRecoveryPolicy = ECatSharedTankRecoveryPolicy::AnyActivePlayer;
	TestTrue(TEXT("偷鱼完整配置后可运行"), Settings->IsTheftReady());
	TestFalse(TEXT("偷鱼完整不会顺带开放恶作剧"), Settings->IsMischiefReady());
	TestFalse(TEXT("偷鱼完整不会顺带开放手动求助"), Settings->IsManualHelpReady());

	Settings->MischiefPermission = ECatDomainPolicy::Enabled;
	Settings->MischiefCooldownSeconds = 5.0;
	Settings->MischiefInteractionRangeCentimeters = 120.0;
	Settings->ProtectionSignRadiusCentimeters = 200.0;
	Settings->ProtectionSignPlacementRangeCentimeters = 80.0;
	TestTrue(TEXT("恶作剧完整配置后可运行"), Settings->IsMischiefReady());

	Settings->ManualHelpRadiusCentimeters = 500.0;
	Settings->ManualHelpCooldownSeconds = 6.0;
	TestTrue(TEXT("手动求助完整配置后可运行"), Settings->IsManualHelpReady());
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
