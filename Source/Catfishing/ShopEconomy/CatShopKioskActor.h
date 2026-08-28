#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/CatInteractable.h"
#include "CatShopKioskActor.generated.h"

class USceneComponent;
class USphereComponent;
class UCatShopInteractionComponent;
class UCatShopInventoryComponent;

/** 可放进关卡的商店摊位对象；它代表 ShopEconomy 的世界入口，自己持有交互 UI 组件和本摊位货架库存组件。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API ACatShopKioskActor : public AActor, public ICatInteractable
{
	GENERATED_BODY()

public:
	/** 构造商店摊位的空间根、页面打开组件和货架库存组件；具体外观可由蓝图或关卡美术继续添加。 */
	ACatShopKioskActor();

	/** 判断本地玩家是否能打开此摊位；只检查交互开关、本地 Controller 和页面状态，不解析营地仓库。 */
	virtual bool CanInteract_Implementation(AController* RequestingController) const override;
	/** 返回当前摊位提示文本；不可交互或页面已打开时返回空文本，避免 UI 继续显示旧提示。 */
	virtual FText GetInteractionPrompt_Implementation() const override;
	/** 返回摊位距离证明使用的交互半径；非法或负数配置会收口为 0，服务端下单校验复用同一口径。 */
	virtual double GetInteractionRadius_Implementation() const override;
	/** 执行本地确认交互；RequestId 和交互 gate 都有效时只打开商店页面，不触发购买或仓库解析。 */
	virtual bool Interact_Implementation(AController* RequestingController, FGuid RequestId) override;

	/** 摊位暴露给蓝图和验收脚本的交互接线；调用方只能确认组件存在，不应绕过它的交互 gate 直接开页面。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Shop")
	UCatShopInteractionComponent* GetShopInteraction() const;

	/** 摊位暴露给蓝图和订单链路的货架库存；购买时只用这份组件表解释 EntryId。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Shop")
	UCatShopInventoryComponent* GetShopInventory() const;

	/** 服务端下单前验证请求确实来自这个摊位旁边的玩家；它只管摊位距离，不再持有或解析营地仓库。 */
	bool CanServeOrderFromAuthority(AController* RequestingController) const;

protected:
	/** 按项目交互设置对准星 Trace Channel 启用摊位查询碰撞。 */
	virtual void BeginPlay() override;

private:
	/** 摊位在关卡中的空间锚点；只决定交互目标位置，不承载商店经济状态。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Shop", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	/** 摊位查询碰撞体，表示没有额外网格碰撞时仍可被准星命中的交互范围；只参与查询，不产生重叠事件。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Interaction", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> InteractionCollision;

	/** 摊位拥有的页面打开能力；LocalPlayer 扫描到它后按确认键会进入正式 Shop UI。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Shop", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatShopInteractionComponent> ShopInteraction;

	/** 摊位拥有的货架库存能力，表示这个世界商店当前卖什么、怎么刷新以及每项剩余多少。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Shop", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatShopInventoryComponent> ShopInventory;

	/** 摊位交互开关，表示当前摆放物是否允许打开商店；蓝图写入后会同时影响提示、打开页面和服务端下单距离证明。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Interaction", meta = (AllowPrivateAccess = "true"))
	bool bInteractionEnabled = true;

	/** 摊位交互半径，表示玩家必须离摊位多近才算仍在旁边；本地交互提示和服务端订单来源校验都读取它。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Interaction", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", Units = "cm"))
	double InteractionRadiusCentimeters = 300.0;

	/** 摊位可用时显示给本地玩家的交互文案；交互关闭或页面已打开时不会继续暴露给提示 UI。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Interaction", meta = (AllowPrivateAccess = "true"))
	FText InteractionPrompt;
};
