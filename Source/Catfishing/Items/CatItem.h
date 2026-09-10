#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/CatInteractable.h"
#include "Inventory/CatInventoryStatics.h"
#include "Inventory/CatInventoryWorldItem.h"
#include "CatItem.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class USkeletalMeshComponent;

/** 通用世界拾取物；关卡配置静态发货批次，库存落地写入实例批次，成功收货前始终保留实物与运行状态。 */
UCLASS(Blueprintable, BlueprintType)
class CATFISHING_API ACatItem : public AActor, public ICatInteractable, public ICatInventoryWorldItem
{
	GENERATED_BODY()

public:
	/** 世界拾取物共用的碰撞与表现骨架；构造阶段搭建组件，资产和批次由蓝图配置，拾取裁决始终使用同一交互入口。 */
	ACatItem();

	/** 收货原子性的输入批次；交互入口用它整体检查容量并发货，派生装备可从定义生成，读取本身不改变库存。 */
	virtual FCatInventoryReceiveBatch GetPickupInventory() const;

	/** 接收来源库存的真实物品载荷；后续拾取沿用实例批次，保留耐久等实例状态而不是重新按定义发货。 */
	virtual bool InitializeFromInventoryFromAuthority(UCatInventoryItemInstance* Item, int32 Quantity) override;

	/** 生成后给派生物补齐表现或配置；基础物不需要额外状态，因此保持无副作用扩展点。 */
	virtual void InitializeActorSpawnConfig();

	/** 本地瞄准与服务器交互都会检查请求者、世界、角色和半径，避免客户端把远处或跨世界的物品送入库存。 */
	virtual bool CanInteract_Implementation(AController* RequestingController) const override;
	/** 目标扫描显示的动作文案；默认显示拾取，蓝图可换文案，但显示提示不代表服务器已通过容量裁决。 */
	virtual FText GetInteractionPrompt_Implementation() const override;
	/** 返回与服务器范围复核共用的交互距离，单位厘米。 */
	virtual double GetInteractionRadius_Implementation() const override;
	/** 客户端经现有 Controller RPC 转发；authority 以整批收货结果决定销毁或保留世界物。 */
	virtual bool Interact_Implementation(AController* RequestingController, FGuid RequestId) override;

protected:
	/** 组件与蓝图默认值就绪后调用派生物配置，使关卡放置和运行生成采用同一初始化时序。 */
	virtual void BeginPlay() override;

	/** 本世界物是否已被一次收货流程占用；服务器在入库广播前写入，失败释放，成功保持到销毁，防止回调重入重复发货。 */
	bool bPickupClaimed = false;

	/** 拾取命中的查询碰撞根；目标扫描读它，成功收货后 Actor 销毁，失败时保持原位置和可交互性。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Item")
	TObjectPtr<UBoxComponent> PickupCollision;

	/** 通用静态网格表现；蓝图配置普通掉落时读取，玩法交互不依赖它是否有资源。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Item")
	TObjectPtr<UStaticMeshComponent> StaticMesh;

	/** 通用骨骼网格表现；动画型世界物读取，和静态网格一样不改变拾取批次。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Item")
	TObjectPtr<USkeletalMeshComponent> SkeletalMesh;

	/** 拾取时交给角色的完整批次；蓝图可配置静态发货，落地入口覆盖为真实实例载荷，动态载荷优先于派生类静态发货。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|Item")
	FCatInventoryReceiveBatch StaticPickupInventory;

	/** 本 Actor 是一次库存落地的实例载体；生成端写入，读取批次时优先于装备子类的静态定义发货。 */
	UPROPERTY(Transient)
	bool bHasInventoryPayload = false;

	/** 拾取可触达半径，单位厘米；本地目标扫描和服务器权威提交都读取同一值。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|Item", meta = (ClampMin = "1.0", Units = "cm"))
	double InteractionRadiusCentimeters = 250.0;
};
