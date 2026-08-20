#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Collection/CatImprintSettings.h"
#include "Social/CatSocialSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatSocialSettingsPolicyReadinessTest,
	"Catfishing.Unit.Social.Settings.TheftMischiefAndManualHelpHaveIndependentGates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：在一份瞬态 Social Settings 上分别补齐偷鱼、恶作剧、放牌、手动求助以及偷鱼售出/进食的空间配置；六个公开
// readiness 必须互相独立，不能因为某一路完整而开放另一条未裁路径。
// 偷取与恶作剧权限已经不在 Settings 层裁决（运行期由局主策略持有），所以这里只锁数值参数的独立性，不再断言权限对 readiness 的影响。
bool FCatSocialSettingsPolicyReadinessTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatSocialSettings* Settings = NewObject<UCatSocialSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 Social Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	Settings->bEnableSocialRuntime = false;
	Settings->TheftPermission = ECatDomainPolicy::Unset;
	Settings->TheftEatingWindowSeconds = 0.0;
	Settings->TheftInteractionRangeCentimeters = 0.0;
	Settings->TheftCatchRangeCentimeters = 0.0;
	Settings->SharedTankRecoveryPolicy = ECatSharedTankRecoveryPolicy::Undecided;
	Settings->TheftCaughtImprintEventId = NAME_None;
	Settings->MischiefPermission = ECatDomainPolicy::Unset;
	Settings->MischiefInteractionRangeCentimeters = 0.0;
	Settings->ProtectionSignRadiusCentimeters = 0.0;
	Settings->ProtectionSignPlacementRangeCentimeters = 0.0;
	Settings->ManualHelpRadiusCentimeters = 0.0;
	Settings->ManualHelpCooldownSeconds = 0.0;
	Settings->TheftSaleShopAnchorTag = NAME_None;
	Settings->TheftSaleShopRangeCentimeters = 0.0;
	Settings->TheftConsumeVictimEscapeDistanceCentimeters = 0.0;

	TestFalse(TEXT("默认偷鱼不可运行"), Settings->AreTheftParametersReady());
	TestFalse(TEXT("默认恶作剧不可运行"), Settings->AreMischiefParametersReady());
	TestFalse(TEXT("默认放牌不可运行"), Settings->IsProtectionSignReady());
	TestFalse(TEXT("默认手动求助不可运行"), Settings->IsManualHelpReady());

	Settings->bEnableSocialRuntime = true;
	Settings->TheftPermission = ECatDomainPolicy::Enabled;
	Settings->TheftEatingWindowSeconds = 8.0;
	Settings->TheftInteractionRangeCentimeters = 150.0;
	Settings->TheftCatchRangeCentimeters = 100.0;
	Settings->SharedTankRecoveryPolicy = ECatSharedTankRecoveryPolicy::AnyActivePlayer;
	TestTrue(TEXT("偷鱼完整配置后可运行"), Settings->AreTheftParametersReady());
	TestFalse(TEXT("偷鱼完整不会顺带开放恶作剧"), Settings->AreMischiefParametersReady());
	TestFalse(TEXT("偷鱼完整不会顺带开放放牌"), Settings->IsProtectionSignReady());
	TestFalse(TEXT("偷鱼完整不会顺带开放手动求助"), Settings->IsManualHelpReady());
	TestFalse(TEXT("偷鱼参数齐全不代表可以把赃物卖掉，商店锚点与到店距离仍未登记"), Settings->IsTheftSaleReady());
	TestFalse(TEXT("偷鱼参数齐全不代表可以把赃物吃掉，逃离距离仍未登记"), Settings->IsTheftConsumptionReady());

	Settings->TheftSaleShopAnchorTag = TEXT("CatShopAnchor");
	TestFalse(TEXT("只登记商店标签、缺到店距离时售出仍不可达"), Settings->IsTheftSaleReady());
	Settings->TheftSaleShopRangeCentimeters = 300.0;
	TestTrue(TEXT("商店标签与到店距离齐全后售出可达"), Settings->IsTheftSaleReady());
	TestFalse(TEXT("售出可达不会顺带开放进食"), Settings->IsTheftConsumptionReady());

	Settings->TheftConsumeVictimEscapeDistanceCentimeters = 800.0;
	TestTrue(TEXT("逃离距离登记后进食可达"), Settings->IsTheftConsumptionReady());

	Settings->TheftEatingWindowSeconds = 0.0;
	TestFalse(TEXT("偷鱼基础参数缺失时售出一并不可达"), Settings->IsTheftSaleReady());
	TestFalse(TEXT("偷鱼基础参数缺失时进食一并不可达"), Settings->IsTheftConsumptionReady());
	Settings->TheftEatingWindowSeconds = 8.0;

	Settings->ProtectionSignRadiusCentimeters = 200.0;
	Settings->ProtectionSignPlacementRangeCentimeters = 80.0;
	TestTrue(TEXT("牌子自身半径与放置距离齐全后放牌即可运行"), Settings->IsProtectionSignReady());
	TestFalse(TEXT("恶作剧交互距离仍未填时放牌 gate 不代表恶作剧可运行"), Settings->AreMischiefParametersReady());

	Settings->MischiefInteractionRangeCentimeters = 120.0;
	TestTrue(TEXT("恶作剧交互距离齐全后可运行"), Settings->AreMischiefParametersReady());

	// 权限已经搬到运行期局主策略，Settings 里的这一项只是开局默认值；把它改成 Disabled 不该再影响任何 Settings 层 readiness。
	Settings->MischiefPermission = ECatDomainPolicy::Disabled;
	TestTrue(TEXT("Settings 里的恶作剧默认权限不再参与参数 readiness"), Settings->AreMischiefParametersReady());
	Settings->TheftPermission = ECatDomainPolicy::Disabled;
	TestTrue(TEXT("Settings 里的偷取默认权限不再参与参数 readiness"), Settings->AreTheftParametersReady());
	TestTrue(TEXT("关闭默认恶作剧权限不会连带关掉放牌护栏"), Settings->IsProtectionSignReady());

	Settings->ManualHelpRadiusCentimeters = 500.0;
	Settings->ManualHelpCooldownSeconds = 6.0;
	TestTrue(TEXT("手动求助完整配置后可运行"), Settings->IsManualHelpReady());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatSocialSettingsProjectDefaultsTest,
	"Catfishing.Unit.Social.Settings.ProjectDefaultsEnableWork6SocialPolicies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：从项目 DefaultGame.ini 读取 Social Settings，确认 Work6 默认只开放已裁的偷鱼、普通恶作剧、防骚扰牌和手动求助入口；
// "偷鱼被抓双方入册"是飞书印记册唯一拍定必进的触发（决策记录 D-07），所以这里要求它的事件 ID 已配置、且印记准入名单里
// 有同一个名字——两处对不上时候选会被准入静默挡掉，这条断言就是防止两边各写一个名字。
// 另外锁住偷鱼两个终态的空间参数仍未登记：飞书只给了定性描述没给数值，这里必须读到"未配置"，而不是某个被顺手填进去的数字。
bool FCatSocialSettingsProjectDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UCatSocialSettings* Settings = GetDefault<UCatSocialSettings>();
	TestNotNull(TEXT("项目 Social Settings 可读取"), Settings);
	if (!Settings)
	{
		return false;
	}

	TestTrue(TEXT("项目默认偷鱼数值参数齐全"), Settings->AreTheftParametersReady());
	TestEqual(TEXT("项目默认偷鱼权限开局为开启"), Settings->TheftPermission, ECatDomainPolicy::Enabled);
	// 飞书只给了"进食时间与奔店路程就是追回窗口"这句定性描述，没有给任何距离数值，参数与校准记录也没挂号。
	// 所以项目配置里这两项必须保持未登记，售出和进食两个终态在真实数值裁下来之前不可达；这条断言就是防止有人顺手编一个数字填进去。
	TestTrue(TEXT("项目默认没有编造商店锚点标签"), Settings->TheftSaleShopAnchorTag.IsNone());
	TestEqual(TEXT("项目默认没有编造到店距离"), Settings->TheftSaleShopRangeCentimeters, 0.0);
	TestEqual(TEXT("项目默认没有编造逃离受害者距离"), Settings->TheftConsumeVictimEscapeDistanceCentimeters, 0.0);
	TestFalse(TEXT("项目默认偷鱼售出不可达"), Settings->IsTheftSaleReady());
	TestFalse(TEXT("项目默认偷鱼进食不可达"), Settings->IsTheftConsumptionReady());
	TestEqual(TEXT("项目默认偷鱼进食窗口"), Settings->TheftEatingWindowSeconds, 30.0);
	TestEqual(TEXT("项目默认偷鱼交互距离"), Settings->TheftInteractionRangeCentimeters, 150.0);
	TestEqual(TEXT("项目默认追回距离"), Settings->TheftCatchRangeCentimeters, 200.0);
	TestEqual(TEXT("项目默认共享鱼缸只允许原主人追回"), Settings->SharedTankRecoveryPolicy, ECatSharedTankRecoveryPolicy::OriginalOwner);
	TestFalse(TEXT("项目默认已登记偷鱼被抓印记事件（飞书唯一拍定必进的触发）"), Settings->TheftCaughtImprintEventId.IsNone());
	TestTrue(TEXT("偷鱼被抓印记事件与印记准入名单逐字一致"),
		GetDefault<UCatImprintSettings>()->IsImprintEventAllowed(Settings->TheftCaughtImprintEventId));

	TestTrue(TEXT("项目默认普通恶作剧数值参数齐全"), Settings->AreMischiefParametersReady());
	TestEqual(TEXT("项目默认恶作剧权限开局为开启"), Settings->MischiefPermission, ECatDomainPolicy::Enabled);
	TestEqual(TEXT("项目默认恶作剧距离"), Settings->MischiefInteractionRangeCentimeters, 150.0);

	TestTrue(TEXT("项目默认放牌策略可运行"), Settings->IsProtectionSignReady());
	TestEqual(TEXT("项目默认保护牌半径"), Settings->ProtectionSignRadiusCentimeters, 250.0);
	TestEqual(TEXT("项目默认保护牌放置距离"), Settings->ProtectionSignPlacementRangeCentimeters, 120.0);

	TestTrue(TEXT("项目默认手动求助策略可运行"), Settings->IsManualHelpReady());
	TestEqual(TEXT("项目默认手动求助范围"), Settings->ManualHelpRadiusCentimeters, 2000.0);
	TestEqual(TEXT("项目默认手动求助冷却"), Settings->ManualHelpCooldownSeconds, 5.0);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
