#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/CatInteractable.h"
#include "CatFishBuyerActor.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UCatFishInventoryItemInstance;

/** 独立收鱼对象；世界交互只卖嘴叼鱼，地面鱼护页面读取其服务范围，不承担购买摊位职责。 */
UCLASS(Blueprintable)
class CATFISHING_API ACatFishBuyerActor : public AActor, public ICatInteractable
{
	GENERATED_BODY()

public:
	/** 建立可命中的收购交互根与可配置外观；服务半径由本 Actor 统一声明。 */
	ACatFishBuyerActor();

	/** 查询能同时服务该玩家及来源的收购对象；客户端用于按钮显隐，服务器提交时仍需复核。 */
	static ACatFishBuyerActor* FindAvailableBuyer(AController* Player, AActor* Source);

	/** 玩家与鱼源都可达时才允许报价或提交；跨世界、携带中的鱼护与被遮挡来源均拒绝，地面鱼护不限制归属。 */
	bool CanServeSource(AController* Player, AActor* Source) const;

	/** 用本地正式价格表估价一条实物鱼；只读预览与 GAS 执行使用同一逐鱼取整规则。 */
	bool TryAppraiseFish(UCatFishInventoryItemInstance* Fish, int32& OutPrice) const;

	/** 只有携带实物鱼且处于服务范围时才显示世界出售交互，不以持有鱼护代替嘴叼鱼。 */
	virtual bool CanInteract_Implementation(AController* RequestingController) const override;
	/** 交互提示说明当前动作是卖出嘴里的鱼，不打开购买页面。 */
	virtual FText GetInteractionPrompt_Implementation() const override;
	/** 返回客户端扫描和服务器共用的收购半径，单位厘米。 */
	virtual double GetInteractionRadius_Implementation() const override;
	/** 客户端转发既有交互 RPC，服务器调用同一售鱼协调器并回送领域结果。 */
	virtual bool Interact_Implementation(AController* RequestingController, FGuid RequestId) override;

private:
	/** 收购对象的交互碰撞根；蓝图可调整范围外的外观，不改变服务距离裁决。 */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> InteractionCollision;
	/** 收购对象的静态外观；由正式蓝图配置，不参与定价或团队金额计算。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> BuyerMesh;
	/** 玩家及地面鱼护到买家的最大服务距离；编辑器配置，双方距离都必须满足，单位厘米。 */
	UPROPERTY(EditAnywhere, Category = "Catfishing|Shop", meta = (ClampMin = "1.0", Units = "cm"))
	double ServiceRadiusCentimeters = 300.0;
};
