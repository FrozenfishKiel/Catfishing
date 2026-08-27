#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Interaction/CatInteractable.h"
#include "Items/CatFishGuardActor.h"
#include "Items/CatFishTankActor.h"
#include "Items/World/CatFishPickupActor.h"
#include "ShopEconomy/CatShopKioskActor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatCommonInteractionActorContractTest,
	"Catfishing.Unit.Interaction.CommonActorContract.FishShopAndContainerUseOneInterface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatCommonInteractionActorContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestTrue(TEXT("上岸鱼实现通用交互接口"),
		ACatFishPickupActor::StaticClass()->ImplementsInterface(UCatInteractable::StaticClass()));
	TestTrue(TEXT("商店 Actor 实现通用交互接口"),
		ACatShopKioskActor::StaticClass()->ImplementsInterface(UCatInteractable::StaticClass()));
	TestTrue(TEXT("共享容器 Actor 实现通用交互接口"),
		ACatFishTankActor::StaticClass()->ImplementsInterface(UCatInteractable::StaticClass()));
	TestTrue(TEXT("地面鱼护 Actor 实现通用交互接口"),
		ACatFishGuardActor::StaticClass()->ImplementsInterface(UCatInteractable::StaticClass()));

	ACatShopKioskActor* ShopCDO = GetMutableDefault<ACatShopKioskActor>();
	ACatFishTankActor* TankCDO = GetMutableDefault<ACatFishTankActor>();
	ACatFishGuardActor* GuardCDO = GetMutableDefault<ACatFishGuardActor>();
	if (TestNotNull(TEXT("商店 CDO 可用"), ShopCDO))
	{
		TestTrue(TEXT("商店由 Actor 提供交互半径"),
			ICatInteractable::Execute_GetInteractionRadius(ShopCDO) > 0.0);
		TestFalse(TEXT("缺失请求 Controller 时商店 fail-closed"),
			ICatInteractable::Execute_CanInteract(ShopCDO, nullptr));
	}
	if (TestNotNull(TEXT("鱼缸 CDO 可用"), TankCDO))
	{
		TestTrue(TEXT("容器由 Actor 提供交互半径"),
			ICatInteractable::Execute_GetInteractionRadius(TankCDO) > 0.0);
		TestFalse(TEXT("缺失请求 Controller 时容器 fail-closed"),
			ICatInteractable::Execute_CanInteract(TankCDO, nullptr));
	}
	if (TestNotNull(TEXT("地面鱼护 CDO 可用"), GuardCDO))
	{
		TestTrue(TEXT("地面鱼护由 Actor 提供交互半径"),
			ICatInteractable::Execute_GetInteractionRadius(GuardCDO) > 0.0);
		TestFalse(TEXT("缺失请求 Controller 时地面鱼护 fail-closed"),
			ICatInteractable::Execute_CanInteract(GuardCDO, nullptr));
	}
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
