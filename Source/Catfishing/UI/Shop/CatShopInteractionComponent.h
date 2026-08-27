#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CatShopInteractionComponent.generated.h"

class APlayerController;
class UCatShopModel;
class UCatShopPageController;
class UCatShopWidget;

/** 商店 Actor 的 UI 生命周期助手；不是第二个交互入口，E 键只调用 Owner 上的 ICatInteractable::Interact。 */
UCLASS(ClassGroup = (Catfishing), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class CATFISHING_API UCatShopInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 构造商店交互组件默认值；它默认不 Tick，只响应交互调用。 */
	UCatShopInteractionComponent();

	/** 组件离开 World 时关闭当前商店 UI；避免交互对象销毁后旧页面继续持有 Controller。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 为指定玩家打开商店 UI；创建 Model/PageController/WBP 后由本组件拥有本次实例。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Shop")
	bool OpenShopForPlayer(APlayerController* PlayerController);

	/** 关闭并销毁当前商店 UI 实例；玩家离开交互范围或点击关闭都会走这里。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Shop")
	void CloseShop();

	/** 查询本交互对象是否正拥有一套商店页面实例；用于阻止重复打开，不以 Widget 是否仍在视口为唯一事实。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Shop")
	bool IsShopOpen() const;

private:
	/** PageController 关闭通知入口；组件收到后销毁本次商店页面对象。 */
	void HandleShopPageCloseRequested();

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
