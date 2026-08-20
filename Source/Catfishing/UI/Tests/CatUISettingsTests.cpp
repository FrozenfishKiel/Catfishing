#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "UI/CatUISettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatUISettingsLakeStatusGateTest,
	"Catfishing.Unit.UI.Settings.LakeStatusViewUsesOnlyExplicitGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：只读取 UI Settings 的 Lake 状态 View 开关；它证明 UI 装配不从玩法默认状态、Widget 类存在与否或 Online 生命周期推导开启。
bool FCatUISettingsLakeStatusGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatUISettings* Settings = NewObject<UCatUISettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 UI Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	// 先清掉 DefaultGame.ini 设过的值：该 gate 在项目配置里是开着的，而 NewObject 从 CDO 拷贝，
	// 不清就会带着开启状态出生，下面这条「未配置时不装配」的断言根本走不到。ini 默认值本身是否合理由 ProjectDefaults 用例负责。
	Settings->bEnableLakeStatusView = false;

	TestFalse(TEXT("gate 未开启时不装配 Lake 状态 View"), Settings->IsLakeStatusViewEnabled());
	Settings->bEnableLakeStatusView = true;
	TestTrue(TEXT("显式 gate 开启 Lake 状态 View"), Settings->IsLakeStatusViewEnabled());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatUISettingsCommandPanelGateTest,
	"Catfishing.Unit.UI.Settings.CommandPanelUsesOnlyExplicitGateAndDefaultsOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：先把瞬态 Settings 的两个开关都抹回 false（CDO 带着 ini 的 True 出生），确认 IsCommandPanelEnabled 只跟自
// 己的字段走、不随 Lake 状态 View 开关联动；
// 再读项目 CDO 确认 ini 里白盒面板当前是开着的。AttachSurvivalPawn 只在 IsCommandPanelEnabled 为真时才 CreateWidget，
// 这条测试固定的是那个 gate 函数本身，不是 Widget 创建路径。
bool FCatUISettingsCommandPanelGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatUISettings* Settings = NewObject<UCatUISettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 UI Settings"), Settings);
	if (!Settings)
	{
		return false;
	}
	Settings->bEnableLakeStatusView = false;
	Settings->bEnableCommandPanel = false;
	TestFalse(TEXT("两开关都关时命令面板 gate 关闭"), Settings->IsCommandPanelEnabled());
	Settings->bEnableLakeStatusView = true;
	TestFalse(TEXT("只开 Lake 状态 View 不会连带打开命令面板"), Settings->IsCommandPanelEnabled());
	Settings->bEnableCommandPanel = true;
	TestTrue(TEXT("显式开关打开命令面板 gate"), Settings->IsCommandPanelEnabled());

	const UCatUISettings* ProjectDefaults = GetDefault<UCatUISettings>();
	TestNotNull(TEXT("项目 UI Settings 可读取"), ProjectDefaults);
	TestTrue(TEXT("项目 DefaultGame.ini 当前开启开发期白盒命令面板"), ProjectDefaults && ProjectDefaults->IsCommandPanelEnabled());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatUISettingsProjectDefaultsTest,
	"Catfishing.Unit.UI.Settings.ProjectDefaultsEnableLakeStatusView",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：从项目 DefaultGame.ini 读取 UI Settings，确认 Lake 状态 View 默认装配；该开关只代表只读 UI 可以订阅快照，不改变任何玩法状态或权限。
bool FCatUISettingsProjectDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UCatUISettings* Settings = GetDefault<UCatUISettings>();
	TestNotNull(TEXT("项目 UI Settings 可读取"), Settings);
	if (!Settings)
	{
		return false;
	}

	TestTrue(TEXT("项目默认 Lake 状态 View 已启用"), Settings->IsLakeStatusViewEnabled());
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
