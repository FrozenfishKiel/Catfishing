#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CatShopInteractionComponent.generated.h"

class APlayerController;
class UCatShopModel;
class UCatShopPageController;
class UCatShopWidget;

/** 商店 Actor 的 UI 生命周期助手；它只管理本摊位打开哪个页面和页面实例，不保存货架库存。 */
UCLASS(ClassGroup = (Catfishing), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class CATFISHING_API UCatShopInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 构造商店交互组件默认值；它默认不 Tick，只响应交互调用。 */
	UCatShopInteractionComponent();

	/** 组件离开 World 时关闭当前商店 UI；避免交互对象销毁后旧页面继续持有 Controller。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 为指定玩家打开商店 UI；创建 Model/PageController/WBP 并绑定来源摊位后，由本组件拥有本次实例，营地仓库由 PlayerController 的服务器购买链路检查。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Shop")
	bool OpenShopForPlayer(APlayerController* PlayerController);

	/** 关闭并销毁当前商店 UI 实例；正常视口移除交给 PageController，组件最后只兜底仍挂在视口里的 View。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Shop")
	void CloseShop();

	/** 查询本交互对象是否正拥有一套商店页面实例；用于阻止重复打开，不以 Widget 是否仍在视口为唯一事实。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Shop")
	bool IsShopOpen() const;

private:
	/** 加载本摊位配置的商店页面类；缺失或继承错误时返回空，让交互对象 fail-closed。 */
	TSubclassOf<UCatShopWidget> LoadShopWidgetClass() const;

	/** PageController 关闭通知入口；组件收到后销毁本次商店页面对象。 */
	void HandleShopPageCloseRequested();

	/** 本摊位打开商店时使用的 WBP 类；这是世界对象自己的页面配置，不再放在全局 UI Settings。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|Shop", meta = (AllowPrivateAccess = "true"))
	TSoftClassPtr<UCatShopWidget> ShopWidgetClass;

	/** 当前商店 WBP；它由交互对象临时拥有，不在 LocalPlayer 启动时预建。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatShopWidget> ShopWidget;

	/** 当前商店 Model；它只读商品目录和公开经济快照。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatShopModel> ShopModel;

	/** 当前商店 PageController；它管理本次打开的焦点和 RPC 翻译。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatShopPageController> ShopPageController;

	/** PageController 关闭通知解绑句柄；组件销毁页面时消费。 */
	FDelegateHandle PageCloseHandle;
};
