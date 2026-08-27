#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/CatInteractable.h"
#include "CatShopKioskActor.generated.h"

class USceneComponent;
class USphereComponent;
class UCatShopInteractionComponent;

/** 可放进关卡的商店摊位对象；它代表 ShopEconomy 的世界入口，只委托 UI 组件打开页面，不保存公款、库存或商品数据。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API ACatShopKioskActor : public AActor, public ICatInteractable
{
	GENERATED_BODY()

public:
	/** 构造商店摊位的空间根和页面打开组件；具体外观可由蓝图或关卡美术继续添加。 */
	ACatShopKioskActor();

	virtual bool CanInteract_Implementation(AController* RequestingController) const override;
	virtual FText GetInteractionPrompt_Implementation() const override;
	virtual double GetInteractionRadius_Implementation() const override;
	virtual bool Interact_Implementation(AController* RequestingController, FGuid RequestId) override;

	/** 摊位暴露给蓝图和验收脚本的交互接线；调用方只能确认组件存在，不应绕过它的交互 gate 直接开页面。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Shop")
	UCatShopInteractionComponent* GetShopInteraction() const;

protected:
	/** 按项目交互设置对准星 Trace Channel 启用摊位查询碰撞。 */
	virtual void BeginPlay() override;

private:
	/** 摊位在关卡中的空间锚点；只决定交互目标位置，不承载商店经济状态。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Shop", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	/** 保证没有额外网格碰撞的商人蓝图仍能被准星命中。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Interaction", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> InteractionCollision;

	/** 摊位拥有的页面打开能力；LocalPlayer 扫描到它后按确认键会进入正式 Shop UI。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Shop", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatShopInteractionComponent> ShopInteraction;

	/** 蓝图可以临时关闭商人交互，不需要换接口或删组件。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Interaction", meta = (AllowPrivateAccess = "true"))
	bool bInteractionEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Interaction", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", Units = "cm"))
	double InteractionRadiusCentimeters = 300.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Interaction", meta = (AllowPrivateAccess = "true"))
	FText InteractionPrompt;
};
