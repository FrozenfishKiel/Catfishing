#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Items/CatItemsSettings.h"
#include "Items/CatItemTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatItemsSettingsCapacityMappingTest,
	"Catfishing.Unit.Items.Settings.CapacityMappingFailClosedByContainerKind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：用瞬态 Items Settings 读取容器容量映射；未知种类和非正容量必须返回 0，只有对应种类的显式正值才能开放新增鱼事务。
bool FCatItemsSettingsCapacityMappingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatItemsSettings* Settings = NewObject<UCatItemsSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 Items Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	Settings->PersonalGuardCapacity = 0;
	Settings->SharedFishTankCapacity = 0;

	TestEqual(TEXT("默认个人鱼护容量为 0"), Settings->GetContainerCapacity(static_cast<uint8>(ECatContainerKind::PersonalGuard)), 0);
	TestEqual(TEXT("默认共享鱼缸容量为 0"), Settings->GetContainerCapacity(static_cast<uint8>(ECatContainerKind::SharedFishTank)), 0);
	TestEqual(TEXT("未知容器种类容量为 0"), Settings->GetContainerCapacity(static_cast<uint8>(ECatContainerKind::Unknown)), 0);

	Settings->PersonalGuardCapacity = 2;
	Settings->SharedFishTankCapacity = 5;
	TestEqual(TEXT("个人鱼护读取显式容量"), Settings->GetContainerCapacity(static_cast<uint8>(ECatContainerKind::PersonalGuard)), 2);
	TestEqual(TEXT("共享鱼缸读取显式容量"), Settings->GetContainerCapacity(static_cast<uint8>(ECatContainerKind::SharedFishTank)), 5);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatItemsSettingsProjectDefaultsTest,
	"Catfishing.Unit.Items.Settings.ProjectDefaultsExposePlayableContainers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：直接读取项目 DefaultGame.ini 中的 Items Settings，确认个人鱼护与共享鱼缸都已给出显式容量；未知容器仍保持
// 0，证明默认配置只开放被列入 Work5 的容器入口。
bool FCatItemsSettingsProjectDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UCatItemsSettings* Settings = GetDefault<UCatItemsSettings>();
	TestNotNull(TEXT("项目 Items Settings 可读取"), Settings);
	if (!Settings)
	{
		return false;
	}

	TestEqual(TEXT("项目默认个人鱼护容量可运行"), Settings->GetContainerCapacity(static_cast<uint8>(ECatContainerKind::PersonalGuard)), 4);
	TestEqual(TEXT("项目默认共享鱼缸容量可运行"), Settings->GetContainerCapacity(static_cast<uint8>(ECatContainerKind::SharedFishTank)), 12);
	TestEqual(TEXT("项目默认未知容器仍 fail-closed"), Settings->GetContainerCapacity(static_cast<uint8>(ECatContainerKind::Unknown)), 0);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
