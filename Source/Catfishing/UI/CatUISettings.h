#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatUISettings.generated.h"

class UCatHUDWidget;
class UCatInteractionPromptWidget;
class UCatInventorySlotWidget;
class UCatInventoryWidget;
class UCatShopWidget;
class UCatCollectionWidget;
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

	/** 返回正式状态 HUD WBP 类；缺失时调用方 fail-closed，不创建原生白盒替身。 */
	TSubclassOf<UCatHUDWidget> LoadHUDWidgetClass() const;

	/** 返回正式背包主界面 WBP 类；缺失时调用方 fail-closed，不创建原生白盒替身。 */
	TSubclassOf<UCatInventoryWidget> LoadInventoryWidgetClass() const;

	/** 读取正式背包格子 WBP 类；缺失时背包只能显示主界面文本，不创建原生格子替身。 */
	TSubclassOf<UCatInventorySlotWidget> LoadInventorySlotWidgetClass() const;

	/** 返回正式商店 WBP 类；它只由商店交互对象打开，不由 LocalPlayer 预建。 */
	TSubclassOf<UCatShopWidget> LoadShopWidgetClass() const;

	/** 读取正式交互提示 WBP 类；缺失时只关闭提示表现，不影响交互目标自己的服务器裁决。 */
	TSubclassOf<UCatInteractionPromptWidget> LoadInteractionPromptWidgetClass() const;

	/** 读取正式图鉴/相册 WBP 类；缺失时图鉴入口 fail-closed，避免用鱼护实物容器临时代替 Profile 记录。 */
	TSubclassOf<UCatCollectionWidget> LoadCollectionWidgetClass() const;

	/** 返回配置的背包开关 Input Action；它仍位于项目既有 InputContext 中。 */
	UInputAction* LoadInventoryToggleAction() const;

	/** 返回配置的交互确认 Input Action；它仍位于项目既有 InputContext 中。 */
	UInputAction* LoadInteractionConfirmAction() const;

	/** 返回项目唯一 Gameplay Mapping Context；UI 只解析资产接线，不安装第二套 Context。 */
	UInputMappingContext* LoadGameplayInputMappingContext() const;

	/** 从正式 IMC 中解析背包开关 Action 的第一个按键名；解析失败时返回 None。 */
	FName ResolveInventoryToggleKeyName() const;

	/** 从正式 IMC 中解析交互确认 Action 的第一个按键名；解析失败时返回 None。 */
	FName ResolveInteractionConfirmKeyName() const;

	/** 局内玩家 UI 的装配开关；默认开启后仍要求各模块 WBP 有效，关闭只用于测试或临时禁用玩家可见 UI。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake")
	bool bEnablePlayerLakeUI = true;

	/** 正式状态 HUD WBP 类；只显示猫状态、钓鱼反馈和短提示。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake|HUD")
	TSoftClassPtr<UCatHUDWidget> HUDWidgetClass;

	/** 正式背包主 WBP 类；它拥有 WrapBox，并按当前打开的容器容量创建格子。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake|Inventory")
	TSoftClassPtr<UCatInventoryWidget> InventoryWidgetClass;

	/** 正式背包格子 WBP 类；每个格子是独立 UserWidget，不是 Button。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake|Inventory")
	TSoftClassPtr<UCatInventorySlotWidget> InventorySlotWidgetClass;

	/** 正式商店 WBP 类；由世界交互对象创建并显示商品、资金和购买结果。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake|Shop")
	TSoftClassPtr<UCatShopWidget> ShopWidgetClass;

	/** 正式交互提示 WBP 类；只显示靠近对象和确认键提示。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake|Interaction")
	TSoftClassPtr<UCatInteractionPromptWidget> InteractionPromptWidgetClass;

	/** 正式图鉴/相册 WBP 类；只读 Profile 图鉴记录。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake|Collection")
	TSoftClassPtr<UCatCollectionWidget> CollectionWidgetClass;

	/** 背包开关的正式 Enhanced Input Action 资产；项目应把它维护在既有 InputContext 内，运行时代码只绑定 Action。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake|Input")
	TSoftObjectPtr<UInputAction> InventoryToggleAction;

	/** 交互确认的正式 Enhanced Input Action 资产；它由 PlayerController 通过 Native Input Tag 唯一绑定，UI 只用它解析提示键名。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake|Input")
	TSoftObjectPtr<UInputAction> InteractionConfirmAction;

	/** 背包开关所在的项目唯一 Mapping Context；它只用于资产接线和键名解析，不由 UI PageController 重复安装。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake|Input")
	TSoftObjectPtr<UInputMappingContext> GameplayInputMappingContext;

};
