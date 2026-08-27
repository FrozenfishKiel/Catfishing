#include "UI/CatUISettings.h"

#include "EnhancedActionKeyMapping.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "UI/Collection/CatCollectionWidget.h"
#include "UI/HUD/CatHUDWidget.h"
#include "UI/Interaction/CatInteractionPromptWidget.h"
#include "UI/Inventory/CatInventoryWidget.h"
#include "UI/InventorySlot/CatInventorySlotWidget.h"
#include "UI/Shop/CatShopWidget.h"

// 构造流程：为正式拆分 UI WBP 和输入资产写入稳定软路径；输入 Action 放在项目既有 InputContext 下维护，运行时代码只加载资产和绑定 Action。
UCatUISettings::UCatUISettings()
{
	HUDWidgetClass = TSoftClassPtr<UCatHUDWidget>(
		FSoftClassPath(TEXT("/Game/UI/HUD/WBP_CatHUD.WBP_CatHUD_C")));
	InventoryWidgetClass = TSoftClassPtr<UCatInventoryWidget>(
		FSoftClassPath(TEXT("/Game/UI/Inventory/WBP_CatInventory.WBP_CatInventory_C")));
	FishGuardInventoryWidgetClass = TSoftClassPtr<UCatInventoryWidget>(
		FSoftClassPath(TEXT("/Game/UI/Inventory/WBP_CatFishGuardInventory.WBP_CatFishGuardInventory_C")));
	InventorySlotWidgetClass = TSoftClassPtr<UCatInventorySlotWidget>(
		FSoftClassPath(TEXT("/Game/UI/InventorySlot/WBP_CatInventorySlot.WBP_CatInventorySlot_C")));
	ShopWidgetClass = TSoftClassPtr<UCatShopWidget>(
		FSoftClassPath(TEXT("/Game/UI/Shop/WBP_CatShop.WBP_CatShop_C")));
	InteractionPromptWidgetClass = TSoftClassPtr<UCatInteractionPromptWidget>(
		FSoftClassPath(TEXT("/Game/UI/Interaction/WBP_CatInteractionPrompt.WBP_CatInteractionPrompt_C")));
	CollectionWidgetClass = TSoftClassPtr<UCatCollectionWidget>(
		FSoftClassPath(TEXT("/Game/UI/Collection/WBP_CatCollection.WBP_CatCollection_C")));
	InventoryToggleAction = TSoftObjectPtr<UInputAction>(
		FSoftObjectPath(TEXT("/Game/Input/InputAction/IA_LakeMenu.IA_LakeMenu")));
	InteractionConfirmAction = TSoftObjectPtr<UInputAction>(
		FSoftObjectPath(TEXT("/Game/Input/InputAction/IA_Interact.IA_Interact")));
	GameplayInputMappingContext = TSoftObjectPtr<UInputMappingContext>(
		FSoftObjectPath(TEXT("/Game/Input/InputContext/IMC_InputContext.IMC_InputContext")));
}

// 玩家 UI gate 流程：直接返回显式项目配置；不读取诊断开关、不推导当前地图，也不改变任何领域状态。
bool UCatUISettings::IsPlayerLakeUIEnabled() const
{
	return bEnablePlayerLakeUI;
}

// HUD WBP 类加载流程：同步解析配置软类并验证继承 HUD 基类；失败返回空，让 LocalPlayer fail-closed。
TSubclassOf<UCatHUDWidget> UCatUISettings::LoadHUDWidgetClass() const
{
	UClass* LoadedClass = HUDWidgetClass.LoadSynchronous();
	if (!LoadedClass || !LoadedClass->IsChildOf(UCatHUDWidget::StaticClass()))
	{
		return nullptr;
	}
	return LoadedClass;
}

// 背包 WBP 类加载流程：同步解析配置软类并验证继承背包主界面基类；失败返回空，让 LocalPlayer fail-closed。
TSubclassOf<UCatInventoryWidget> UCatUISettings::LoadInventoryWidgetClass() const
{
	UClass* LoadedClass = InventoryWidgetClass.LoadSynchronous();
	if (!LoadedClass || !LoadedClass->IsChildOf(UCatInventoryWidget::StaticClass()))
	{
		return nullptr;
	}
	return LoadedClass;
}

// 鱼护箱子库存 WBP 类加载流程：同步解析配置软类并验证继承背包主界面基类；失败返回空，让鱼护交互明确拒绝而不是退回通用页。
TSubclassOf<UCatInventoryWidget> UCatUISettings::LoadFishGuardInventoryWidgetClass() const
{
	UClass* LoadedClass = FishGuardInventoryWidgetClass.LoadSynchronous();
	if (!LoadedClass || !LoadedClass->IsChildOf(UCatInventoryWidget::StaticClass()))
	{
		return nullptr;
	}
	return LoadedClass;
}

// 背包格子 WBP 类加载流程：同步解析配置软类并验证继承格子基类；失败返回空，让背包不能创建抽象原生格子。
TSubclassOf<UCatInventorySlotWidget> UCatUISettings::LoadInventorySlotWidgetClass() const
{
	UClass* LoadedClass = InventorySlotWidgetClass.LoadSynchronous();
	if (!LoadedClass || !LoadedClass->IsChildOf(UCatInventorySlotWidget::StaticClass()))
	{
		return nullptr;
	}
	return LoadedClass;
}

// 商店 WBP 类加载流程：同步解析配置软类并验证继承商店基类；失败返回空，让交互对象 fail-closed。
TSubclassOf<UCatShopWidget> UCatUISettings::LoadShopWidgetClass() const
{
	UClass* LoadedClass = ShopWidgetClass.LoadSynchronous();
	if (!LoadedClass || !LoadedClass->IsChildOf(UCatShopWidget::StaticClass()))
	{
		return nullptr;
	}
	return LoadedClass;
}

// 交互提示 WBP 类加载流程：同步解析配置软类并验证继承提示基类；失败返回空，让 LocalPlayer 不显示伪提示。
TSubclassOf<UCatInteractionPromptWidget> UCatUISettings::LoadInteractionPromptWidgetClass() const
{
	UClass* LoadedClass = InteractionPromptWidgetClass.LoadSynchronous();
	if (!LoadedClass || !LoadedClass->IsChildOf(UCatInteractionPromptWidget::StaticClass()))
	{
		return nullptr;
	}
	return LoadedClass;
}

// 图鉴 WBP 类加载流程：同步解析配置软类并验证继承图鉴基类；失败返回空，让打开入口 fail-closed。
TSubclassOf<UCatCollectionWidget> UCatUISettings::LoadCollectionWidgetClass() const
{
	UClass* LoadedClass = CollectionWidgetClass.LoadSynchronous();
	if (!LoadedClass || !LoadedClass->IsChildOf(UCatCollectionWidget::StaticClass()))
	{
		return nullptr;
	}
	return LoadedClass;
}

// 背包 Action 加载流程：同步解析配置软引用；失败返回空，让 PageController 记录降级并保留鼠标按钮入口。
UInputAction* UCatUISettings::LoadInventoryToggleAction() const
{
	return InventoryToggleAction.LoadSynchronous();
}

// 交互确认 Action 加载流程：同步解析配置软引用；失败返回空，让交互控制器只隐藏提示或记录降级。
UInputAction* UCatUISettings::LoadInteractionConfirmAction() const
{
	return InteractionConfirmAction.LoadSynchronous();
}

// Gameplay IMC 加载流程：同步解析项目既有 InputContext；返回空表示正式输入资产缺失，调用方不得补建第二套 Context。
UInputMappingContext* UCatUISettings::LoadGameplayInputMappingContext() const
{
	return GameplayInputMappingContext.LoadSynchronous();
}

// 背包键名解析流程：
// 1. 先加载配置的 Action 和 Mapping Context，缺任一资产都返回 None。
// 2. 再遍历 IMC 默认映射，找到该 Action 的第一条有效按键。
// 3. 结果只用于 UIOnly 焦点下关闭背包和提示文案，不参与运行时重新 MapKey。
FName UCatUISettings::ResolveInventoryToggleKeyName() const
{
	const UInputAction* Action = LoadInventoryToggleAction();
	const UInputMappingContext* MappingContext = LoadGameplayInputMappingContext();
	if (!Action || !MappingContext)
	{
		return NAME_None;
	}
	for (const FEnhancedActionKeyMapping& Mapping : MappingContext->GetMappings())
	{
		if (Mapping.Action == Action && Mapping.Key.IsValid())
		{
			return Mapping.Key.GetFName();
		}
	}
	return NAME_None;
}

// 交互键名解析流程：
// 1. 先加载配置的确认 Action 和项目唯一 Mapping Context，缺任一资产都返回 None。
// 2. 再遍历 IMC 默认映射，找到该 Action 的第一条有效按键。
// 3. 结果只用于交互提示文本，不参与运行时重新 MapKey。
FName UCatUISettings::ResolveInteractionConfirmKeyName() const
{
	const UInputAction* Action = LoadInteractionConfirmAction();
	const UInputMappingContext* MappingContext = LoadGameplayInputMappingContext();
	if (!Action || !MappingContext)
	{
		return NAME_None;
	}
	for (const FEnhancedActionKeyMapping& Mapping : MappingContext->GetMappings())
	{
		if (Mapping.Action == Action && Mapping.Key.IsValid())
		{
			return Mapping.Key.GetFName();
		}
	}
	return NAME_None;
}
