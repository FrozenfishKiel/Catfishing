#include "UI/CatUISettings.h"

#include "EnhancedActionKeyMapping.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "UI/Collection/CatCollectionWidget.h"
#include "UI/Collection/CatFishRevealWidget.h"
#include "UI/HUD/CatHUDWidget.h"
#include "UI/Run/CatAltarConfirmationWidget.h"
#include "UI/Run/CatDayTransitionWidget.h"
#include "UI/Frontend/CatFrontendRootWidget.h"
#include "UI/Interaction/CatInteractionPromptWidget.h"
#include "UI/Inventory/CatInventoryWidget.h"
#include "UI/Inventory/CatInventoryQuickbarWidget.h"
#include "UI/Inventory/CatInventoryContextMenuWidget.h"
#include "UI/InventorySlot/CatInventorySlotWidget.h"
#include "UI/ItemTooltip/CatItemTooltipWidget.h"
#include "UI/Save/CatLakeMainMenuWidget.h"

// 构造流程：为翻天、物品提示、HUD、背包及格子、交互提示、前端、局内菜单和图鉴写入正式 WBP 默认软路径，再设置既有 InputAction 与 InputContext 的输入资产路径。
// 此处只保存可被项目配置覆盖的引用，不加载或创建控件；各加载入口在实际装配时解析资产。
UCatUISettings::UCatUISettings()
{
	DayTransitionWidgetClass = TSoftClassPtr<UCatDayTransitionWidget>(
		FSoftClassPath(TEXT("/Game/UI/Run/WBP_CatDayTransition.WBP_CatDayTransition_C")));
	AltarConfirmationWidgetClass = TSoftClassPtr<UCatAltarConfirmationWidget>(
		FSoftClassPath(TEXT("/Game/UI/Run/WBP_CatAltarConfirmation.WBP_CatAltarConfirmation_C")));
	ItemTooltipWidgetClass = TSoftClassPtr<UCatItemTooltipWidget>(
		FSoftClassPath(TEXT("/Game/UI/Inventory/WBP_CatItemTooltip.WBP_CatItemTooltip_C")));
	HUDWidgetClass = TSoftClassPtr<UCatHUDWidget>(
		FSoftClassPath(TEXT("/Game/UI/HUD/WBP_CatHUD.WBP_CatHUD_C")));
	FrontendRootWidgetClass = TSoftClassPtr<UCatFrontendRootWidget>(
		FSoftClassPath(TEXT("/Game/UI/Frontend/WBP_CatFrontendRoot.WBP_CatFrontendRoot_C")));
	InventoryWidgetClass = TSoftClassPtr<UCatInventoryWidget>(
		FSoftClassPath(TEXT("/Game/UI/Inventory/WBP_CatInventory.WBP_CatInventory_C")));
	InventoryQuickbarWidgetClass = TSoftClassPtr<UCatInventoryQuickbarWidget>(
		FSoftClassPath(TEXT("/Game/UI/Inventory/WBP_CatInventoryQuickbar.WBP_CatInventoryQuickbar_C")));
	InventorySlotWidgetClass = TSoftClassPtr<UCatInventorySlotWidget>(
		FSoftClassPath(TEXT("/Game/UI/InventorySlot/WBP_CatInventorySlot.WBP_CatInventorySlot_C")));
	InventoryQuickbarSlotWidgetClass = TSoftClassPtr<UCatInventorySlotWidget>(
		FSoftClassPath(TEXT("/Game/UI/InventorySlot/WBP_CatInventoryQuickbarSlot.WBP_CatInventoryQuickbarSlot_C")));
	InventoryContextMenuWidgetClass = TSoftClassPtr<UCatInventoryContextMenuWidget>(
		FSoftClassPath(TEXT("/Game/UI/Inventory/WBP_CatInventoryContextMenu.WBP_CatInventoryContextMenu_C")));
	InteractionPromptWidgetClass = TSoftClassPtr<UCatInteractionPromptWidget>(
		FSoftClassPath(TEXT("/Game/UI/Interaction/WBP_CatInteractionPrompt.WBP_CatInteractionPrompt_C")));
	LakeMainMenuWidgetClass = TSoftClassPtr<UCatLakeMainMenuWidget>(
		FSoftClassPath(TEXT("/Game/UI/Save/WBP_CatLakeMainMenu.WBP_CatLakeMainMenu_C")));
	CollectionWidgetClass = TSoftClassPtr<UCatCollectionWidget>(
		FSoftClassPath(TEXT("/Game/UI/Collection/WBP_CatCollection.WBP_CatCollection_C")));
	FishRevealWidgetClass = TSoftClassPtr<UCatFishRevealWidget>(
		FSoftClassPath(TEXT("/Game/UI/Collection/WBP_CatFishReveal.WBP_CatFishReveal_C")));
	MainMenuToggleAction = TSoftObjectPtr<UInputAction>(
		FSoftObjectPath(TEXT("/Game/Input/InputAction/IA_LakeMenu.IA_LakeMenu")));
	InventoryToggleAction = TSoftObjectPtr<UInputAction>(
		FSoftObjectPath(TEXT("/Game/Input/InputAction/IA_Inventory.IA_Inventory")));
	InteractionConfirmAction = TSoftObjectPtr<UInputAction>(
		FSoftObjectPath(TEXT("/Game/Input/InputAction/IA_Interact.IA_Interact")));
	CollectionToggleAction = TSoftObjectPtr<UInputAction>(
		FSoftObjectPath(TEXT("/Game/Input/InputAction/IA_Collection.IA_Collection")));
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

// Frontend Root WBP 类加载流程：同步解析配置软类并验证继承正式 Root 基类；失败返回空，让 LocalPlayer 保持无前端而不是退回原生 TravelWidget。
TSubclassOf<UCatFrontendRootWidget> UCatUISettings::LoadFrontendRootWidgetClass() const
{
	UClass* LoadedClass = FrontendRootWidgetClass.LoadSynchronous();
	if (!LoadedClass || !LoadedClass->IsChildOf(UCatFrontendRootWidget::StaticClass()))
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

// 正式翻天视图加载：解析软类后核对 UMG 父类和蓝图生成标志；失败不实例化原生类，不改变过场时钟。
TSubclassOf<UCatDayTransitionWidget> UCatUISettings::LoadDayTransitionWidgetClass() const
{
	UClass* LoadedClass = DayTransitionWidgetClass.LoadSynchronous();
	return LoadedClass && LoadedClass->IsChildOf(UCatDayTransitionWidget::StaticClass())
		&& LoadedClass->HasAnyClassFlags(CLASS_CompiledFromBlueprint) ? LoadedClass : nullptr;
}

// 快捷栏 WBP 类加载流程：同步解析默认或项目覆盖的软类并核对原生父类；失败返回空，由 LocalPlayer 拒绝创建不完整 HUD。
TSubclassOf<UCatInventoryQuickbarWidget> UCatUISettings::LoadInventoryQuickbarWidgetClass() const
{
	UClass* LoadedClass = InventoryQuickbarWidgetClass.LoadSynchronous();
	return LoadedClass && LoadedClass->IsChildOf(UCatInventoryQuickbarWidget::StaticClass()) ? LoadedClass : nullptr;
}

// 右键菜单类加载流程：同步解析配置软类并核对统一原生父类；失败返回空，让页面控制器拒绝展示没有正式布局的操作入口。
TSubclassOf<UCatInventoryContextMenuWidget> UCatUISettings::LoadInventoryContextMenuWidgetClass() const
{
	UClass* LoadedClass = InventoryContextMenuWidgetClass.LoadSynchronous();
	return LoadedClass && LoadedClass->IsChildOf(UCatInventoryContextMenuWidget::StaticClass()) ? LoadedClass : nullptr;
}

// 正式祭坛确认视图加载流程：同步解析配置软类并核对正式 UMG 父类；失败返回空，不创建无法反映正式版式的原生替身。
TSubclassOf<UCatAltarConfirmationWidget> UCatUISettings::LoadAltarConfirmationWidgetClass() const
{
	UClass* LoadedClass = AltarConfirmationWidgetClass.LoadSynchronous();
	return LoadedClass && LoadedClass->IsChildOf(UCatAltarConfirmationWidget::StaticClass())
		&& LoadedClass->HasAnyClassFlags(CLASS_CompiledFromBlueprint) ? LoadedClass : nullptr;
}

// 解析正式软类并验证父类；失败返回空，由 LocalPlayer 记录缺失，避免迁移未完成时显示白盒替身。
TSubclassOf<UCatItemTooltipWidget> UCatUISettings::LoadItemTooltipWidgetClass() const
{
	UClass* LoadedClass = ItemTooltipWidgetClass.LoadSynchronous();
	return LoadedClass && LoadedClass->IsChildOf(UCatItemTooltipWidget::StaticClass()) ? LoadedClass : nullptr;
}

// 局内菜单类加载流程：同步解析配置软类并验证继承菜单基类；失败返回空，避免 LocalPlayer 创建无交互空页。
TSubclassOf<UCatLakeMainMenuWidget> UCatUISettings::LoadLakeMainMenuWidgetClass() const
{
	UClass* LoadedClass = LakeMainMenuWidgetClass.LoadSynchronous();
	if (!LoadedClass || !LoadedClass->IsChildOf(UCatLakeMainMenuWidget::StaticClass()))
	{
		return nullptr;
	}
	return LoadedClass;
}

// 图鉴 WBP 类加载流程：同步解析配置软类并验证继承图鉴基类；失败返回空，让图鉴入口 fail-closed，不创建原生白盒替身。
TSubclassOf<UCatCollectionWidget> UCatUISettings::LoadCollectionWidgetClass() const
{
	UClass* LoadedClass = CollectionWidgetClass.LoadSynchronous();
	if (!LoadedClass || !LoadedClass->IsChildOf(UCatCollectionWidget::StaticClass()))
	{
		return nullptr;
	}
	return LoadedClass;
}

// 首解锁特写 WBP 类加载流程：同步解析配置软类并验证继承特写基类；失败返回空。
// 这一层缺席只意味着这次不弹特写，图鉴记录仍然已经写进 Profile——所以这里绝不能把它做成阻断写入的闸门。
TSubclassOf<UCatFishRevealWidget> UCatUISettings::LoadFishRevealWidgetClass() const
{
	UClass* LoadedClass = FishRevealWidgetClass.LoadSynchronous();
	if (!LoadedClass || !LoadedClass->IsChildOf(UCatFishRevealWidget::StaticClass()))
	{
		return nullptr;
	}
	return LoadedClass;
}

// 主菜单 Action 加载流程：同步解析配置软引用；失败返回空，让菜单控制器记录降级且不硬写 Escape。
UInputAction* UCatUISettings::LoadMainMenuToggleAction() const
{
	return MainMenuToggleAction.LoadSynchronous();
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

// 图鉴 Action 加载流程：同步解析配置软引用；失败返回空，让图鉴页面控制器记录降级并只保留 HUD 与局内菜单按钮入口。
UInputAction* UCatUISettings::LoadCollectionToggleAction() const
{
	return CollectionToggleAction.LoadSynchronous();
}

// Gameplay IMC 加载流程：同步解析项目既有 InputContext；返回空表示正式输入资产缺失，调用方不得补建第二套 Context。
UInputMappingContext* UCatUISettings::LoadGameplayInputMappingContext() const
{
	return GameplayInputMappingContext.LoadSynchronous();
}

// 完成态停留读取流程：读取项目 UI 设置中的秒数，非法浮点或负值按 0 处理；UI 只在进入游戏真实就绪后使用此值，返回主菜单不读取它延迟撤罩。
float UCatUISettings::GetGlobalLoadingCompletionHoldSeconds() const
{
	return FMath::IsFinite(GlobalLoadingCompletionHoldSeconds)
		? FMath::Max(0.0f, GlobalLoadingCompletionHoldSeconds) : 0.0f;
}

// 背包键名解析流程：
// 1. 先加载配置的 Action 和 Mapping Context，缺任一资产都返回 None。
// 2. 如果该 Action 已被局内主菜单占用，背包快捷键提示返回 None，避免把 Escape 继续显示成背包键。
// 3. 再遍历 IMC 默认映射，找到该 Action 的第一条有效按键。
// 4. 结果只用于 UIOnly 焦点下关闭背包和提示文案，不参与运行时重新 MapKey。
FName UCatUISettings::ResolveInventoryToggleKeyName() const
{
	const UInputAction* Action = LoadInventoryToggleAction();
	const UInputAction* MainMenuAction = LoadMainMenuToggleAction();
	const UInputMappingContext* MappingContext = LoadGameplayInputMappingContext();
	if (!Action || !MappingContext)
	{
		return NAME_None;
	}
	if (MainMenuAction && Action == MainMenuAction)
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

// 主菜单键名解析流程：
// 1. 先加载配置的 Action 和 Mapping Context，缺任一资产都返回 None。
// 2. 再遍历 IMC 默认映射，找到该 Action 的第一条有效按键。
// 3. 结果只用于菜单提示或未来 WBP 文案，不参与运行时重新 MapKey。
FName UCatUISettings::ResolveMainMenuToggleKeyName() const
{
	const UInputAction* Action = LoadMainMenuToggleAction();
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

// 物品栏格子加载流程：解析独立软类并验证格子父类；失败返回空，由装配入口记录缺失，不回退到背包布局。
TSubclassOf<UCatInventorySlotWidget> UCatUISettings::LoadInventoryQuickbarSlotWidgetClass() const
{
	UClass* LoadedClass = InventoryQuickbarSlotWidgetClass.LoadSynchronous();
	return LoadedClass && LoadedClass->IsChildOf(UCatInventorySlotWidget::StaticClass()) ? LoadedClass : nullptr;
}

// 图鉴键名解析流程：
// 1. 先加载配置的图鉴 Action 和项目唯一 Mapping Context，缺任一资产都返回 None。
// 2. 如果该 Action 已被局内主菜单或背包占用，图鉴快捷键提示返回 None，避免把同一个键显示成两个入口。
// 3. 再遍历 IMC 默认映射，找到该 Action 的第一条有效按键。
// 4. 结果只用于 UIOnly 焦点下关闭图鉴页，不参与运行时重新 MapKey。
FName UCatUISettings::ResolveCollectionToggleKeyName() const
{
	const UInputAction* Action = LoadCollectionToggleAction();
	const UInputAction* MainMenuAction = LoadMainMenuToggleAction();
	const UInputAction* InventoryAction = LoadInventoryToggleAction();
	const UInputMappingContext* MappingContext = LoadGameplayInputMappingContext();
	if (!Action || !MappingContext)
	{
		return NAME_None;
	}
	if ((MainMenuAction && Action == MainMenuAction) || (InventoryAction && Action == InventoryAction))
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
