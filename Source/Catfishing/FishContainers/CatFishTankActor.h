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

	/**
	 * 本局这口缸已经买到第几档容量；0 是初始档，每买一档 +1。
	 * 它只活在本局 World 里、不进存档——「随局清空」讲的就是这件事。
	 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|FishContainers")
	int32 GetCapacityTier() const;

	/**
	 * 声明：只回答「这一档升级现在能不能买」，不改任何状态；商店在扣钱之前问它。
	 * 边界：档位必须严格等于当前档 +1（不能跳档、不能重复买），且该档在配置里解析得出正容量。
	 *      同一个 RequestId 已经提交过时直接返回 true —— 那是可靠 RPC 重放，不是第二次购买。
	 */
	bool CanApplyCapacityUpgradeFromAuthority(int32 TargetTier, const FGuid& RequestId) const;

	/**
	 * 声明：一次问一整串档位能不能连着买下来（一车里同时买两档时用）。
	 * 实现：把档位排序后按「当前档 + 1、+ 2 …」逐个核，不去改任何状态。
	 * 边界：跳档、重复档、越过配置档数都判否；同号重放直接放行，理由同单档入口。
	 */
	bool CanApplyCapacityUpgradeSequenceFromAuthority(const TArray<int32>& TargetTiers, const FGuid& RequestId) const;

	/**
	 * 声明：把鱼缸推进到指定容量档，并按新档位扩容正式鱼库存；成功后返回 true。
	 * 实现：复用 CanApplyCapacityUpgradeFromAuthority 的同一套前置，再写档位与槽位数，最后刷新只读摘要。
	 * 边界：只缩不扩的方向不做——容量只升不降；档位不连续或配置缺失时整笔拒绝，不部分生效。
	 *      幂等键是购物车 RequestId：同一个号重复提交只在第一次真的升档，之后直接返回成功。
	 */
	bool ApplyCapacityUpgradeFromAuthority(int32 TargetTier, const FGuid& RequestId);

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

	/** 本局已购容量档；0＝初始档。服务器唯一写口是 ApplyCapacityUpgradeFromAuthority，客户端只读它做 UI 展示。 */
	UPROPERTY(Replicated)
	int32 CapacityTier = 0;

	/** 按当前档位解析鱼缸应有的槽位容量；配置未给出正容量时回退到编辑器上的 FishInventorySlotCapacity。 */
	int32 ResolveSlotCapacityForCurrentTier() const;

	/** 已经提交过的升级请求号；服务器本地保存，只用于挡住可靠 RPC 重放再升一档，不复制也不进存档。 */
	TSet<FGuid> CommittedUpgradeRequestIds;

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

	/**
	 * 共享鱼缸的兜底槽位容量；只有 CatFishContainerSettings 没给出正的初始容量时才用它，并会记一条 Warning。
	 * 正式容量来自设置里的初始档 + 已购升级档（商店册 §3.1.2：初始 10，两档 20／30）。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|FishContainers", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
	int32 FishInventorySlotCapacity = 20;

	/** 鱼缸可被确认交互的最大距离，单位为厘米；服务器空间复核读取它，值越小越容易拒绝远端请求。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Interaction", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", Units = "cm"))
	double InteractionRadiusCentimeters = 300.0;

	/** 玩家准星命中鱼缸时显示的提示文本；编辑器写入，交互提示层读取，禁用或库存缺失时不会显示。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Interaction", meta = (AllowPrivateAccess = "true"))
	FText InteractionPrompt;
};
