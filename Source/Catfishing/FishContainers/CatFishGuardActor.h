#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/CatInteractable.h"
#include "CatFishGuardActor.generated.h"

class UCatFishGuardInventoryWidget;
class UCatFishOnlyInventoryComponent;
class USceneComponent;
class USphereComponent;

/** 世界里的鱼护箱子宿主；它承载一份鱼容器，可被玩家交互打开，不挂在 Character 或 PlayerState 身上。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API ACatFishGuardActor : public AActor, public ICatInteractable
{
	GENERATED_BODY()

public:
	/** 创建鱼护的场景根、容器复制出口和交互入口；容量和容器 ID 等到服务器 BeginPlay 时写入。 */
	ACatFishGuardActor();

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
	/** authority 进入 World 时按配置补齐正式鱼库存槽位；客户端只等待 InventoryComponent 复制。 */
	virtual void BeginPlay() override;

private:
	/** authority 复核请求角色与本鱼护的距离/视线；客户端准星命中不能代替服务器空间校验。 */
	bool IsAuthorityRequestSpatiallyValid(const AController* RequestingController) const;

	/** 解析本鱼护交互要打开的库存页类；同步读取本 Actor 配置，失败时返回空，让交互打开明确拒绝而不是退回普通背包。 */
	TSubclassOf<UCatFishGuardInventoryWidget> LoadInventoryViewClass() const;

	/** 鱼护的独立世界根；蓝图可在它下面挂网兜、箱体或其他表现组件，不影响容器真相。 */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> GuardRoot;

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
