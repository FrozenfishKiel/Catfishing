#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatUISettings.generated.h"

class UCatCollectionWidget;
class UCatFishRevealWidget;
class UCatHUDWidget;
class UCatAltarConfirmationWidget;
class UCatDayTransitionWidget;
class UCatItemTooltipWidget;
class UCatFrontendRootWidget;
class UCatInteractionPromptWidget;
class UCatInventorySlotWidget;
class UCatInventoryContextMenuWidget;
class UCatInventoryWidget;
class UCatInventoryQuickbarWidget;
class UCatLakeMainMenuWidget;
class UInputAction;
class UInputMappingContext;

/** 正式 UI 模块的显式运行设置；只控制各 View 是否装配，不携带任何领域数值或权限。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing UI"))
class CATFISHING_API UCatUISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 建立 UI 模块默认前端契约：没有项目配置覆盖时，各模块指向正式 WBP 软路径，而不是退回 C++ 白盒类。 */
	UCatUISettings();

	/** 返回局内玩家 UI 模块是否允许装配；关闭时 LocalPlayer 仍维护 Online 生命周期但不创建 HUD/背包/提示。 */
	bool IsPlayerLakeUIEnabled() const;

	/** 返回正式主 HUD WBP 类；缺失时调用方 fail-closed，不创建原生白盒替身。 */
	TSubclassOf<UCatHUDWidget> LoadHUDWidgetClass() const;

	/** 读取正式翻天 WBP；缺失、原生类或错误父类都返回空，调用方必须隐藏并记录而非创建替身。 */
	TSubclassOf<UCatDayTransitionWidget> LoadDayTransitionWidgetClass() const;

	/** 读取正式祭坛确认 WBP；缺失、原生类或错误父类都返回空，等待确认仍由 GameMode 的超时规则收口。 */
	TSubclassOf<UCatAltarConfirmationWidget> LoadAltarConfirmationWidgetClass() const;

	/** 返回正式 Frontend Root WBP 类；缺失时 LocalPlayer fail-closed，不创建原生替身。 */
	TSubclassOf<UCatFrontendRootWidget> LoadFrontendRootWidgetClass() const;

	/** 返回正式背包主界面 WBP 类；缺失时调用方 fail-closed，不创建原生白盒替身。 */
	TSubclassOf<UCatInventoryWidget> LoadInventoryWidgetClass() const;

	/** 读取常驻快捷栏的正式 WBP 类；缺失时局内 UI fail-closed，不以原生占位替代底部布局。 */
	TSubclassOf<UCatInventoryQuickbarWidget> LoadInventoryQuickbarWidgetClass() const;

	/** 读取正式背包格子 WBP 类；缺失时背包只能显示主界面文本，不创建原生格子替身。 */
	TSubclassOf<UCatInventorySlotWidget> LoadInventorySlotWidgetClass() const;

	/** 读取物品栏专用格子布局；数字和选中外圈只属于这个视图，不装入背包窗口。 */
	TSubclassOf<UCatInventorySlotWidget> LoadInventoryQuickbarSlotWidgetClass() const;

	/** 读取所有库存共用的正式右键菜单 WBP；缺失时页面拒绝打开操作菜单，不创建白盒替代布局。 */
	TSubclassOf<UCatInventoryContextMenuWidget> LoadInventoryContextMenuWidgetClass() const;

	/** 读取迁移后的正式物品提示 WBP；资源或父类不符时返回空，不生成替代布局。 */
	TSubclassOf<UCatItemTooltipWidget> LoadItemTooltipWidgetClass() const;

	/** 读取正式交互提示 WBP 类；缺失时只关闭提示表现，不影响交互目标自己的服务器裁决。 */
	TSubclassOf<UCatInteractionPromptWidget> LoadInteractionPromptWidgetClass() const;

	/** 读取局内 ESC 主菜单类；缺失时 LocalPlayer 不创建空白菜单，也不把保存入口塞回 HUD。 */
	TSubclassOf<UCatLakeMainMenuWidget> LoadLakeMainMenuWidgetClass() const;

	/** 读取个人图鉴页 WBP 类；缺失时只关闭图鉴入口并记录，不创建原生白盒替身，也不影响 Profile 图鉴记录。 */
	TSubclassOf<UCatCollectionWidget> LoadCollectionWidgetClass() const;

	/**
	 * 读取首次解锁鱼种的特写浮层 WBP 类；缺失时只关闭这一次特写并记录一次诊断，
	 * 既不创建原生白盒替身，也不影响图鉴记录本身——记录早在 Profile 落盘时就写好了。
	 */
	TSubclassOf<UCatFishRevealWidget> LoadFishRevealWidgetClass() const;

	/** 返回配置的局内主菜单 Input Action；它应由项目既有 InputContext 映射到 Escape 或等价菜单键。 */
	UInputAction* LoadMainMenuToggleAction() const;

	/** 返回配置的背包开关 Input Action；它仍位于项目既有 InputContext 中。 */
	UInputAction* LoadInventoryToggleAction() const;

	/** 返回配置的交互确认 Input Action；它仍位于项目既有 InputContext 中。 */
	UInputAction* LoadInteractionConfirmAction() const;

	/** 返回配置的图鉴开关 Input Action；它应由项目既有 InputContext 映射到 M 键，运行时代码只绑定 Action。 */
	UInputAction* LoadCollectionToggleAction() const;

	/** 返回项目唯一 Gameplay Mapping Context；UI 只解析资产接线，不安装第二套 Context。 */
	UInputMappingContext* LoadGameplayInputMappingContext() const;

	/** 返回全局加载完成态最短展示秒数；调用方只在真实完成后读取它控制撤遮罩时机，不用它推进加载进度。 */
	float GetGlobalLoadingCompletionHoldSeconds() const;

	/** 从正式 IMC 中解析背包开关 Action 的第一个按键名；解析失败时返回 None。 */
	FName ResolveInventoryToggleKeyName() const;

	/** 从正式 IMC 中解析主菜单 Action 的第一个按键名；解析失败时返回 None。 */
	FName ResolveMainMenuToggleKeyName() const;

	/** 从正式 IMC 中解析交互确认 Action 的第一个按键名；解析失败时返回 None。 */
	FName ResolveInteractionConfirmKeyName() const;

	/** 从正式 IMC 中解析图鉴开关 Action 的第一个按键名；解析失败时返回 None。 */
	FName ResolveCollectionToggleKeyName() const;

	/** 局内玩家 UI 的装配开关；默认开启后仍要求各模块 WBP 有效，关闭只用于测试或诊断禁用玩家可见 UI。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake")
	bool bEnablePlayerLakeUI = true;

	/** 正式主 HUD WBP 类；默认只常驻天数、背包和设置入口，背包内容由库存页面打开后显示。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake|HUD")
	TSoftClassPtr<UCatHUDWidget> HUDWidgetClass;

	/** 翻天遮罩的正式 WBP 软类引用；构造器提供默认路径，项目配置可覆盖，LocalPlayer 经加载入口按请求创建实例，布局由资产维护。 */
	UPROPERTY(Config, EditAnywhere, Category="Lake|Run")
	TSoftClassPtr<UCatDayTransitionWidget> DayTransitionWidgetClass;

	/** 祭坛全员确认窗口的正式 WBP 软类引用；LocalPlayer 在等待或系统取消反馈时加载同一资产，具体操作提示由 Widget 按发起者身份渲染。 */
	UPROPERTY(Config, EditAnywhere, Category="Lake|Run")
	TSoftClassPtr<UCatAltarConfirmationWidget> AltarConfirmationWidgetClass;

	/** 正式 Frontend 根 WBP 类；只在 Frontend World 为本地玩家创建，Root 内只装配主菜单、存档、房间和设置页面。 */
	UPROPERTY(Config, EditAnywhere, Category = "Frontend")
	TSoftClassPtr<UCatFrontendRootWidget> FrontendRootWidgetClass;

	/** 正式背包主 WBP 类；它拥有 WrapBox，并按当前打开的容器容量创建格子。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake|Inventory")
	TSoftClassPtr<UCatInventoryWidget> InventoryWidgetClass;

	/** 常驻快捷栏的正式 WBP 软类引用；LocalPlayer 在玩家 Pawn 有效后创建，它只观察随身背包 Model。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake|Inventory")
	TSoftClassPtr<UCatInventoryQuickbarWidget> InventoryQuickbarWidgetClass;

	/** 正式背包格子 WBP 类；每个格子是独立 UserWidget，不是 Button。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake|Inventory")
	TSoftClassPtr<UCatInventorySlotWidget> InventorySlotWidgetClass;

	/** 物品栏专用格子软类；LocalPlayer 装配物品栏时读取，使背包格子的布局保持独立。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake|Inventory")
	TSoftClassPtr<UCatInventorySlotWidget> InventoryQuickbarSlotWidgetClass;

	/** 所有库存右键操作共用的正式菜单 WBP；页面控制器只创建一份并在不同来源间复用。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake|Inventory")
	TSoftClassPtr<UCatInventoryContextMenuWidget> InventoryContextMenuWidgetClass;

	/** 玩家唯一物品悬停框的正式 WBP；LocalPlayer 装配时读取，资产位于已有库存 Cook 目录。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake|Inventory")
	TSoftClassPtr<UCatItemTooltipWidget> ItemTooltipWidgetClass;

	/** 正式交互提示 WBP 类；只显示靠近对象和确认键提示。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake|Interaction")
	TSoftClassPtr<UCatInteractionPromptWidget> InteractionPromptWidgetClass;

	/** 局内 ESC 主菜单 WBP 类；默认指向正式资产，Controller 只通过这个 View 接收设置、保存和退出意图。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake|Save")
	TSoftClassPtr<UCatLakeMainMenuWidget> LakeMainMenuWidgetClass;

	/** 个人图鉴页 WBP 类；默认指向正式资产，页面只读 Profile durable 快照，不承载局内图鉴板与印记相册。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake|Collection")
	TSoftClassPtr<UCatCollectionWidget> CollectionWidgetClass;

	/**
	 * 首次解锁鱼种的特写浮层 WBP 类；默认指向正式资产路径，资产尚未创建时保持空并由调用方 fail-closed。
	 * 它只是一次性揭示层，不承载图鉴页——图鉴页是 CollectionWidgetClass。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake|Collection")
	TSoftClassPtr<UCatFishRevealWidget> FishRevealWidgetClass;

	/** 局内主菜单的正式 Enhanced Input Action 资产；项目应把它维护在既有 InputContext 内，运行时代码只绑定 Action。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake|Input")
	TSoftObjectPtr<UInputAction> MainMenuToggleAction;

	/** 背包开关的正式 Enhanced Input Action 资产；若与主菜单 Action 相同，背包会避让主菜单并保留 HUD 按钮入口。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake|Input")
	TSoftObjectPtr<UInputAction> InventoryToggleAction;

	/** 交互确认的正式 Enhanced Input Action 资产；它由 PlayerController 通过 Native Input Tag 唯一绑定，UI 只用它解析提示键名。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake|Input")
	TSoftObjectPtr<UInputAction> InteractionConfirmAction;

	/** 图鉴开关的正式 Enhanced Input Action 资产；项目应把它维护在既有 InputContext 内并映射到 M 键，运行时代码只绑定 Action。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake|Input")
	TSoftObjectPtr<UInputAction> CollectionToggleAction;

	/** 背包开关所在的项目唯一 Mapping Context；它只用于资产接线和键名解析，不由 UI PageController 重复安装。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake|Input")
	TSoftObjectPtr<UInputMappingContext> GameplayInputMappingContext;

	/** 全局加载遮罩完成态的最短停留时间，单位秒；只影响真实加载完成后的视觉收口，不参与 Online 状态、资源加载或进度合成。 */
	UPROPERTY(Config, EditAnywhere, Category = "Loading", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0"))
	float GlobalLoadingCompletionHoldSeconds = 0.35f;

};
