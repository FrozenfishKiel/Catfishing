#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "InputAction.h"
#include "InputMappingContext.h"
#include "UI/Collection/CatCollectionWidget.h"
#include "UI/CatUISettings.h"
#include "UI/HUD/CatHUDWidget.h"
#include "UI/Interaction/CatInteractionPromptWidget.h"
#include "UI/Inventory/CatInventoryWidget.h"
#include "UI/InventorySlot/CatInventorySlotWidget.h"
#include "UI/Shop/CatShopWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatUISettingsSplitPlayerModulesTest,
	"Catfishing.Unit.UI.Settings.SplitPlayerModulesUseConfiguredWidgetClasses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：先验证拆分玩家 UI 默认启用并逐项指向正式 WBP 软类；再显式开关同一 gate，最后检查背包和交互输入仍来自项目既有 IMC。
bool FCatUISettingsSplitPlayerModulesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatUISettings* Settings = NewObject<UCatUISettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 UI Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	TestTrue(TEXT("正式拆分玩家 UI 默认允许装配，缺 WBP 时由调用方 fail-closed"), Settings->IsPlayerLakeUIEnabled());
	TestEqual(TEXT("默认 HUD 前端指向正式 WBP 软类"),
		Settings->HUDWidgetClass.ToSoftObjectPath().ToString(),
		FString(TEXT("/Game/UI/HUD/WBP_CatHUD.WBP_CatHUD_C")));
	TestEqual(TEXT("默认背包前端指向正式 WBP 软类"),
		Settings->InventoryWidgetClass.ToSoftObjectPath().ToString(),
		FString(TEXT("/Game/UI/Inventory/WBP_CatInventory.WBP_CatInventory_C")));
	TestEqual(TEXT("默认背包格子前端指向正式 WBP 软类"),
		Settings->InventorySlotWidgetClass.ToSoftObjectPath().ToString(),
		FString(TEXT("/Game/UI/InventorySlot/WBP_CatInventorySlot.WBP_CatInventorySlot_C")));
	TestEqual(TEXT("默认商店前端指向正式 WBP 软类"),
		Settings->ShopWidgetClass.ToSoftObjectPath().ToString(),
		FString(TEXT("/Game/UI/Shop/WBP_CatShop.WBP_CatShop_C")));
	TestEqual(TEXT("默认交互提示前端指向正式 WBP 软类"),
		Settings->InteractionPromptWidgetClass.ToSoftObjectPath().ToString(),
		FString(TEXT("/Game/UI/Interaction/WBP_CatInteractionPrompt.WBP_CatInteractionPrompt_C")));
	TestEqual(TEXT("默认图鉴前端指向正式 WBP 软类"),
		Settings->CollectionWidgetClass.ToSoftObjectPath().ToString(),
		FString(TEXT("/Game/UI/Collection/WBP_CatCollection.WBP_CatCollection_C")));

	const TSubclassOf<UCatHUDWidget> HUDClass = Settings->LoadHUDWidgetClass();
	if (TestNotNull(TEXT("正式 HUD WBP 类可加载"), HUDClass.Get()))
	{
		TestTrue(TEXT("HUD WBP 继承正式 HUD View 基类"), HUDClass->IsChildOf(UCatHUDWidget::StaticClass()));
	}
	const TSubclassOf<UCatInventoryWidget> InventoryClass = Settings->LoadInventoryWidgetClass();
	if (TestNotNull(TEXT("正式背包 WBP 类可加载"), InventoryClass.Get()))
	{
		TestTrue(TEXT("背包 WBP 继承正式 Inventory View 基类"),
			InventoryClass->IsChildOf(UCatInventoryWidget::StaticClass()));
	}
	const TSubclassOf<UCatInventorySlotWidget> SlotClass = Settings->LoadInventorySlotWidgetClass();
	if (TestNotNull(TEXT("正式背包格子 WBP 类可加载"), SlotClass.Get()))
	{
		TestTrue(TEXT("格子 WBP 继承正式 InventorySlot View 基类"),
			SlotClass->IsChildOf(UCatInventorySlotWidget::StaticClass()));
	}
	const TSubclassOf<UCatShopWidget> ShopClass = Settings->LoadShopWidgetClass();
	if (TestNotNull(TEXT("正式商店 WBP 类可加载"), ShopClass.Get()))
	{
		TestTrue(TEXT("商店 WBP 继承正式 Shop View 基类"), ShopClass->IsChildOf(UCatShopWidget::StaticClass()));
	}
	const TSubclassOf<UCatInteractionPromptWidget> InteractionClass = Settings->LoadInteractionPromptWidgetClass();
	if (TestNotNull(TEXT("正式交互提示 WBP 类可加载"), InteractionClass.Get()))
	{
		TestTrue(TEXT("交互提示 WBP 继承正式 Interaction View 基类"),
			InteractionClass->IsChildOf(UCatInteractionPromptWidget::StaticClass()));
	}
	const TSubclassOf<UCatCollectionWidget> CollectionClass = Settings->LoadCollectionWidgetClass();
	if (TestNotNull(TEXT("正式图鉴 WBP 类可加载"), CollectionClass.Get()))
	{
		TestTrue(TEXT("图鉴 WBP 继承正式 Collection View 基类"),
			CollectionClass->IsChildOf(UCatCollectionWidget::StaticClass()));
	}

	Settings->bEnablePlayerLakeUI = false;
	TestFalse(TEXT("显式关闭后不装配拆分玩家 UI"), Settings->IsPlayerLakeUIEnabled());
	Settings->bEnablePlayerLakeUI = true;
	TestTrue(TEXT("重新开启后仍走同一正式拆分 UI 配置"), Settings->IsPlayerLakeUIEnabled());
	TestEqual(TEXT("背包输入 Action 指向正式资产"),
		Settings->InventoryToggleAction.ToSoftObjectPath().ToString(),
		FString(TEXT("/Game/Input/InputAction/IA_LakeMenu.IA_LakeMenu")));
	TestEqual(TEXT("Gameplay 输入 IMC 指向项目既有 InputContext"),
		Settings->GameplayInputMappingContext.ToSoftObjectPath().ToString(),
		FString(TEXT("/Game/Input/InputContext/IMC_InputContext.IMC_InputContext")));
	TestNotNull(TEXT("背包输入 Action 资产可加载"), Settings->LoadInventoryToggleAction());
	TestNotNull(TEXT("交互输入 Action 资产可加载"), Settings->LoadInteractionConfirmAction());
	TestNotNull(TEXT("Gameplay 输入 IMC 资产可加载"), Settings->LoadGameplayInputMappingContext());
	TestEqual(TEXT("背包键名来自 IMC 映射"), Settings->ResolveInventoryToggleKeyName(), FName(TEXT("Tab")));
	TestEqual(TEXT("交互键名来自 IMC 映射"), Settings->ResolveInteractionConfirmKeyName(), FName(TEXT("E")));
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
