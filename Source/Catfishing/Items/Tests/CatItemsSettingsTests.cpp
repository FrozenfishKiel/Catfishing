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

	TestEqual(TEXT("默认个人鱼护容量为 0"), Settings->GetContainerCapacity(static_cast<uint8>(ECatContainerKind::PersonalGuard)), 0);
	TestEqual(TEXT("默认共享鱼缸容量为 0"), Settings->GetContainerCapacity(static_cast<uint8>(ECatContainerKind::SharedFishTank)), 0);
	TestEqual(TEXT("未知容器种类容量为 0"), Settings->GetContainerCapacity(static_cast<uint8>(ECatContainerKind::Unknown)), 0);

	Settings->PersonalGuardCapacity = 2;
	Settings->SharedFishTankCapacity = 5;
	TestEqual(TEXT("个人鱼护读取显式容量"), Settings->GetContainerCapacity(static_cast<uint8>(ECatContainerKind::PersonalGuard)), 2);
	TestEqual(TEXT("共享鱼缸读取显式容量"), Settings->GetContainerCapacity(static_cast<uint8>(ECatContainerKind::SharedFishTank)), 5);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
