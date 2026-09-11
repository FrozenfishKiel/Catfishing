#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/CatInteractable.h"
#include "CatFishTankActor.generated.h"

class UCatFishOnlyInventoryComponent;
class UCatFishTankInteractionComponent;
class UCatFishTankWorldInfoComponent;
class USceneComponent;
class USphereComponent;

/** 固定营地的一局共享鱼缸宿主；只承载一份正式鱼库存，不拥有转移、供品结算或偷取规则。 */
UCLASS()
class CATFISHING_API ACatFishTankActor : public AActor, public ICatInteractable
{
	GENERATED_BODY()

public:
	/** 建立共享鱼缸库存宿主和只读信息锚点并关闭 Tick；鱼内容由 FishInventory 持有，摘要只投影库存事实。 */
	ACatFishTankActor();

	/** 判断请求 Controller 是否能把本鱼缸作为交互目标；只承认玩家 Controller、交互开关和正式库存组件。 */
	virtual bool CanInteract_Implementation(AController* RequestingController) const override;

	/** 返回鱼缸当前提示文本；鱼缸禁用或库存组件缺失时返回空文本，避免准星提示早于可用状态出现。 */
	virtual FText GetInteractionPrompt_Implementation() const override;

	/** 返回鱼缸交互距离，单位为厘米；非有限值按 0 处理，让服务器空间复核保守失败。 */
	virtual double GetInteractionRadius_Implementation() const override;

	/** 执行鱼缸交互；客户端用鱼缸库存组件打开本地库存页，服务器侧只复核目标可交互。 */
	virtual bool Interact_Implementation(AController* RequestingController, FGuid RequestId) override;

	/** 蓝图读取共享鱼缸持有的正式鱼库存组件；拖拽、吃鱼、售鱼和复制共用这份事实，避免鱼缸再维护一套鱼数组。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|FishContainers")
	UCatFishOnlyInventoryComponent* GetFishInventoryComponent() const;

	/** 该鱼缸暴露给世界交互扫描的能力组件；人工验收和蓝图只读取它确认交互接线。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|FishContainers")
	UCatFishTankInteractionComponent* GetTankInteraction() const;

protected:
	/** authority 入场时按编辑器容量补齐正式库存，再显式发布只读摘要；客户端等待库存与摘要各自复制。 */
	virtual void BeginPlay() override;

	/** 鱼缸销毁时清理仍由本库存保管的隐藏鱼 Actor；已 Carry 离开库存的鱼不属于本容器。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 共享鱼缸的固定场景根；关卡用它摆放位置，运行时不把该坐标当成库存真相。 */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> TankRoot;

	/** 保证没有额外网格碰撞的鱼缸蓝图仍能被准星命中。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Interaction", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> InteractionCollision;

	/** 共享鱼缸的正式库存组件；它只限制鱼定义进入，槽位、移动、使用和变化通知全部沿用 InventoryComponent。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|FishContainers", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatFishOnlyInventoryComponent> FishInventory;

	/** 鱼缸的本地交互入口；它只把共享鱼缸库存作为背包外部上下文打开，真实移动仍由库存事务决定。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|FishContainers", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatFishTankInteractionComponent> TankInteraction;

	/** 共享鱼缸的只读世界信息锚点；构造时创建并挂接，Actor 入场时请求首次汇总，服务器发布库存摘要供客户端 UI 和祭坛读取，不提供库存写口。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|World Info", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatFishTankWorldInfoComponent> WorldInfo;

	/** 鱼缸是否允许成为交互目标；蓝图或编辑器可关闭它，交互扫描和提示读取后会一起隐藏入口。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Interaction", meta = (AllowPrivateAccess = "true"))
	bool bInteractionEnabled = true;

	/** 共享鱼缸默认槽位容量；BeginPlay 在服务器写入正式库存组件，运行时不会走鱼容器设置表。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|FishContainers", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
	int32 FishInventorySlotCapacity = 20;

	/** 鱼缸可被确认交互的最大距离，单位为厘米；服务器空间复核读取它，值越小越容易拒绝远端请求。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Interaction", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", Units = "cm"))
	double InteractionRadiusCentimeters = 300.0;

	/** 玩家准星命中鱼缸时显示的提示文本；编辑器写入，交互提示层读取，禁用或库存缺失时不会显示。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Interaction", meta = (AllowPrivateAccess = "true"))
	FText InteractionPrompt;
};
