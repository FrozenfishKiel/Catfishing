#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/CatInteractable.h"
#include "Inventory/CatInventoryWorldItem.h"
#include "CatFishGuardActor.generated.h"

class UCatFishGuardInventoryWidget;
class UCatFishOnlyInventoryComponent;
class USceneComponent;
class USphereComponent;
class UBoxComponent;
class ACatCharacter;
class UCatInventoryItemDefinition;
class UCatFishGuardInventoryItemInstance;

/** 持有唯一鱼库存的鱼护 Actor；地面时允许开护，入包后保留原库存并按服务器归属附着嘴部或隐藏，不把内鱼搬到角色。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API ACatFishGuardActor : public AActor, public ICatInteractable, public ICatInventoryWorldItem
{
	GENERATED_BODY()

public:
	/** 创建鱼护的场景根、容器复制出口和交互入口；容量和容器 ID 等到服务器 BeginPlay 时写入。 */
	ACatFishGuardActor();

	/** 复制库存归属；地面交互与嘴边表现使用同一归属，不依赖背包子对象的网络到达顺序。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 从物品定义首次生成空鱼护时绑定库存身份；已有鱼护放下直接复用 Actor，不调用此初始化。 */
	virtual bool InitializeFromInventoryFromAuthority(UCatInventoryItemInstance* Item, int32 Quantity) override;

	/** 长按拾取入口；只在嘴空且背包完整收货后把原鱼护附着到嘴部，内部鱼不做复制或重建。 */
	bool PickUpFromAuthority(AController* RequestingController, FGuid RequestId);

	/** 库存实例迁移时更新载体宿主；空表示地面，角色宿主可叼起，仓库宿主只隐藏表现。 */
	void SetInventoryOwnerFromAuthority(AActor* NewInventoryOwner);

	/** 鱼护是否仍是可直接打开的地面容器；出售和通用库存触达校验都必须先检查它。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|FishContainers")
	bool IsGrounded() const;

	/** 查找嘴部正在携带的鱼护；与既有叼鱼检测共同维持单嘴占用，未叼任何鱼护时返回空。 */
	static ACatFishGuardActor* FindCarriedGuard(const ACatCharacter* Character);

	/** 蓝图读取鱼护持有的正式鱼库存组件；拖拽、吃鱼、售鱼和复制共用这份事实，避免鱼护再维护一套鱼数组。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|FishContainers")
	UCatFishOnlyInventoryComponent* GetFishInventoryComponent() const;

	/** 判断请求 Controller 是否能把本鱼护作为交互目标；只承认玩家 Controller、已注册容器和有效复制出口。 */
	virtual bool CanInteract_Implementation(AController* RequestingController) const override;

	/** 返回鱼护当前提示文本；鱼护禁用或容器尚未注册时返回空文本，避免准星提示早于可用状态出现。 */
	virtual FText GetInteractionPrompt_Implementation() const override;

	/** 返回鱼护交互距离，单位为厘米；非有限值按 0 处理，让服务器空间复核 fail-closed。 */
	virtual double GetInteractionRadius_Implementation() const override;

	/** 执行鱼护交互；客户端负责打开本地库存页并转发请求，authority 只向本鱼护容器提交嘴上叼鱼。 */
	virtual bool Interact_Implementation(AController* RequestingController, FGuid RequestId) override;

protected:
	/** 接收服务器附着时保留本物原有世界尺寸；位置、朝向及解除附着仍沿用引擎处理。 */
	virtual void OnRep_AttachmentReplication() override;

	/** authority 进入 World 时按配置补齐正式鱼库存槽位；客户端只等待 InventoryComponent 复制。 */
	virtual void BeginPlay() override;

private:
	/** 归属复制后同步碰撞；只有服务器裁决嘴部和隐藏，客户端保留引擎收到的附着结果，避免复制顺序改变表现。 */
	UFUNCTION()
	void OnRep_InventoryOwner();

	/** 库存宿主销毁时把保管的鱼护留在原地；解除回调并保留内部鱼，不跟随猫 Actor 一起丢失。 */
	UFUNCTION()
	void HandleInventoryOwnerDestroyed(AActor* DestroyedActor);

	/** 库存持有者；服务器由实例归属写入，客户端复制后关闭地面交互并更新携带表现，空值代表地面鱼护。 */
	UPROPERTY(ReplicatedUsing = OnRep_InventoryOwner)
	TObjectPtr<AActor> InventoryOwner;

	/** 鱼护本体的物品身份；第一次拾取时创建并在背包与地面间复用，内部鱼仍归 FishInventory。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatFishGuardInventoryItemInstance> GuardItem;

	/** 鱼护本体的静态物品定义；关卡和运行生成共用它，未配置时只拒绝拾取，不影响原有开护入鱼。 */
	UPROPERTY(EditDefaultsOnly, Category = "Catfishing|Inventory")
	TSoftObjectPtr<UCatInventoryItemDefinition> GuardDefinition;

	/** 鱼护挂到嘴部后的局部位置与朝向；服务器读取这两项，缩放分量不参与附着，以保留场景中原鱼护尺寸。 */
	UPROPERTY(EditDefaultsOnly, Category = "Catfishing|Inventory")
	FTransform MouthCarryTransform = FTransform::Identity;
	/** authority 复核请求角色与本鱼护的距离/视线；客户端准星命中不能代替服务器空间校验。 */
	bool IsAuthorityRequestSpatiallyValid(const AController* RequestingController) const;

	/** 解析本鱼护交互要打开的库存页类；同步读取本 Actor 配置，失败时返回空，让交互打开明确拒绝而不是退回普通背包。 */
	TSubclassOf<UCatFishGuardInventoryWidget> LoadInventoryViewClass() const;

	/** 鱼护蓝图既有的表现挂点；现在位于物理根下，保留网兜和箱体的附件关系，不参与库存或嘴部所有权判断。 */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> GuardRoot;

	/** 与箱体大小一致的物理根；蓝图按模型设置半尺寸和表现偏移，落点检测与复制刚体运动共同读取。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> WorldCollision;

	/** 保证没有额外网格碰撞的鱼护蓝图仍能被准星命中。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Interaction", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> InteractionCollision;

	/** 鱼护的正式库存组件；它只限制鱼定义进入，槽位、移动、使用和变化通知全部沿用 InventoryComponent。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|FishContainers", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatFishOnlyInventoryComponent> FishInventory;

	/** 鱼护默认槽位容量；BeginPlay 在服务器写入正式库存组件，运行时不会走鱼容器设置表。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|FishContainers", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
	int32 FishInventorySlotCapacity = 8;

	/** 鱼护交互打开时使用的库存 WBP 类，表示这个世界容器希望呈现的页面形态。 */
	/** 蓝图或配置写入它，交互时读取它；值无效会让本次打开失败，不会影响容器内真实鱼数组。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|UI", meta = (AllowPrivateAccess = "true"))
	TSoftClassPtr<UCatFishGuardInventoryWidget> InventoryViewClass;

	/** 鱼护是否允许成为交互目标；蓝图或编辑器可关闭它，交互扫描和提示读取后会一起隐藏入口。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Interaction", meta = (AllowPrivateAccess = "true"))
	bool bInteractionEnabled = true;

	/** 鱼护可被确认交互的最大距离，单位为厘米；服务器空间复核读取它，值越小越容易拒绝远端请求。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Interaction", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", Units = "cm"))
	double InteractionRadiusCentimeters = 300.0;

	/** 玩家准星命中鱼护时显示的提示文本；编辑器写入，交互提示层读取，禁用或未注册时不会显示。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Interaction", meta = (AllowPrivateAccess = "true"))
	FText InteractionPrompt;

};
