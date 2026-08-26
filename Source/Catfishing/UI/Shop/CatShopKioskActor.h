#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CatShopKioskActor.generated.h"

class USceneComponent;
class UCatShopInteractionComponent;

/** 可放进关卡的商店交互对象；它只拥有商店交互组件，不保存钱包、库存或商品数据。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API ACatShopKioskActor : public AActor
{
	GENERATED_BODY()

public:
	/** 构造商店交互 Actor 的根组件和商店交互组件；具体外观可由蓝图或关卡美术继续添加。 */
	ACatShopKioskActor();

	/** 返回商店交互组件；蓝图和验收脚本用它确认该 Actor 真正接入按键交互链路。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Shop")
	UCatShopInteractionComponent* GetShopInteraction() const;

private:
	/** Actor 根组件；这里只提供可放置的空间锚点，不承担碰撞或外观。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Shop", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	/** 商店交互能力组件；LocalPlayer 扫描到它后按确认键会调用 OpenShopForPlayer。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Shop", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatShopInteractionComponent> ShopInteraction;
};
