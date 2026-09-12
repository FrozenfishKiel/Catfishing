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

/**
 * 缸里一条鱼的只读公开事实。
 *
 * 存在的理由：共用大鱼缸是「活的图鉴」——缸内要按实际鱼种游动，看缸等于翻一遍图鉴。
 * 在此之前鱼缸只把整份库存组件用 BlueprintPure 抛给蓝图，蓝图既要自己遍历槽位、
 * 又要自己把 ItemInstance 下钻成鱼实例，表现层实际拿不到「缸里现在是哪几种鱼、各多重」。
 * 本结构只投影生成游动表现需要的三件事，不含捕获者身份，也不提供任何写口。
 */
USTRUCT(BlueprintType)
struct FCatFishTankOccupant
{
	GENERATED_BODY()

	/** 局内鱼实例 ID；表现层用它把一条游动的鱼和库存里的那一条对上，鱼进出缸时不会认错。 */
	UPROPERTY(BlueprintReadOnly, Category = "Catfishing|FishContainers")
	FGuid FishInstanceId;

	/** 鱼种稳定 ID；配合 UCatFishDataStatics::FindFishPresentationDefinition 取网格与动画。 */
	UPROPERTY(BlueprintReadOnly, Category = "Catfishing|FishContainers")
	FName FishDefinitionId = NAME_None;

	/** 该个体的真实重量（千克）；缸内游动表现按它取统一可视缩放，同鱼种不同个体大小不同。 */
	UPROPERTY(BlueprintReadOnly, Category = "Catfishing|FishContainers")
	double WeightKilograms = 0.0;
};

/** 缸内鱼清单发生变化的蓝图通知；收到后重新读 GetTankOccupants，不从事件载荷拿可写事实。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCatFishTankOccupantsChanged);

/** 固定营地的一局共享鱼缸宿主；只承载一份正式鱼库存，不拥有转移或供品结算规则（拿鱼就是转移，没有另一套偷取规则）。 */
UCLASS()
class CATFISHING_API ACatFishTankActor : public AActor, public ICatInteractable
{
	GENERATED_BODY()

public:
	/** 建立共享鱼缸库存宿主和只读信息锚点并关闭 Tick；鱼内容由 FishInventory 持有，摘要只投影库存事实。 */
	ACatFishTankActor();

	/** 注册复制字段；鱼内容仍由库存组件自己复制，这里只加一局稳定的鱼缸容器 ID。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

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

	/**
	 * 缸内实物鱼清单，按槽位顺序；服务器与客户端读到的都是同一份复制库存的投影。
	 * 这是「缸内按实际鱼种游动」的数据接缝：表现层拿到鱼种 ID 与重量后自己生成游动的鱼，
	 * 不碰库存事务，也不改变这条鱼的去向。空槽与非鱼实例都会被跳过。
	 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|FishContainers")
	TArray<FCatFishTankOccupant> GetTankOccupants() const;

	/**
	 * 本鱼缸的容器稳定 ID。
	 * 现阶段鱼缸内容由 FishInventory 持有、未经 UCatFishContainerService 注册，因此这里返回服务器入场时
	 * 分配并复制下来的一局稳定 ID，供表现层区分「这条游动的鱼属于哪口缸」。
	 * 等鱼缸接上鱼容器服务后改为返回服务分配的 ContainerId，调用方签名不变。
	 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|FishContainers")
	FGuid GetTankContainerId() const;

	/** 缸内鱼清单变化通知；蓝图订阅它增删游动的鱼，不必逐帧轮询 GetTankOccupants。 */
	UPROPERTY(BlueprintAssignable, Category = "Catfishing|FishContainers")
	FCatFishTankOccupantsChanged OnTankOccupantsChanged;

protected:
	/** authority 入场时按编辑器容量补齐正式库存，再显式发布只读摘要；客户端等待库存与摘要各自复制。 */
	virtual void BeginPlay() override;

	/** 鱼缸销毁时清理仍由本库存保管的隐藏鱼 Actor；已 Carry 离开库存的鱼不属于本容器。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 库存任一次本地变化后把通知转成蓝图可订阅的形式；本函数不读写库存，只转发。 */
	void HandleFishInventoryChanged();

	/** 本机订阅正式鱼库存变化的配对凭据；BeginPlay 建立、EndPlay 成对移除。 */
	FDelegateHandle FishInventoryChangedHandle;

	/** 服务器入场时分配的一局鱼缸容器 ID；只读复制给客户端，不是库存写口，也不参与持久化映射。 */
	UPROPERTY(Replicated)
	FGuid TankContainerId;

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
