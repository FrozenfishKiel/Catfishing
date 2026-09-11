#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/CatInteractable.h"
#include "Inventory/CatInventoryComponent.h"
#include "CatCampInventoryActor.generated.h"

class UCatCampInventoryWidget;
class UCatInventoryComponent;
class USceneComponent;
class USphereComponent;

/** 营地公共仓库 Actor；它是共享箱子的空间交互宿主，库存事实只保存在正式 InventoryComponent 中。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API ACatCampInventoryActor : public AActor, public ICatInteractable
{
	GENERATED_BODY()

public:
	/** 公共仓库 Actor 的初始运行姿态：它是可复制、无 Tick 的关卡对象，并用默认容量作为未配置时的安全基线。 */
	ACatCampInventoryActor();

	/** 判断请求 Controller 是否能把本公共仓库作为交互目标；准星提示和本地打开都使用这条同一边界。 */
	virtual bool CanInteract_Implementation(AController* RequestingController) const override;

	/** 返回公共仓库交互提示；禁用交互时返回空文本，让提示层和执行层保持一致。 */
	virtual FText GetInteractionPrompt_Implementation() const override;

	/** 返回公共仓库允许交互的距离，单位厘米；服务器库存移动复核会复用这份距离声明。 */
	virtual double GetInteractionRadius_Implementation() const override;

	/** 执行公共仓库交互；本地玩家打开营地仓库自己的 WBP，库存写入仍只能由后续服务器请求完成。 */
	virtual bool Interact_Implementation(AController* RequestingController, FGuid RequestId) override;

	/** 读取营地公共仓库的正式库存组件；商店发货、拖放和 UI 展示都从这一份事实源读取。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Inventory")
	UCatInventoryComponent* GetInventoryComponent() const;

	/** 从反序列化槽位恢复公共仓库；这个入口内部完成 authority、容量、定义和实例约束校验，成功后提交到正式 InventoryComponent。 */
	bool RestoreInventorySlotsFromAuthority(const TArray<FCatInventoryEntry>& RestoredSlots,
		FText& OutFailure);

	/** 从正式 InventoryComponent 导出可序列化格子；保存系统用它写磁盘 DTO。 */
	bool ExportInventorySlotsFromAuthority(TArray<FCatInventoryEntry>& OutSlots, FText& OutFailure) const;

	/** 读取公共仓库对 UI 暴露的格子容量；空仓库也靠它显示稳定空格，不把空数组误认为没有仓库。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|CampInventory")
	int32 GetInventorySlotCapacityForView() const;

protected:
	/** 进入 World 后按项目交互设置对齐准星 Trace 通道；库存内容本身不在 BeginPlay 中写入。 */
	virtual void BeginPlay() override;

private:
	/** 读取本公共仓库的格子容量；配置非法时返回 0，让入库 fail-closed。 */
	int32 GetConfiguredSlotCapacity() const;

	/** 解析公共仓库交互要打开的独立库存页类；路径失效时返回空，让交互明确失败而不是退回默认库存页。 */
	TSubclassOf<UCatCampInventoryWidget> LoadInventoryViewClass() const;

	/** 公共仓库在关卡中的空间根；它只承载交互或摆放位置，不保存任何库存规则。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|CampInventory",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	/** 公共仓库的准星命中入口；没有网格碰撞的箱子蓝图也能被交互扫描命中。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Interaction",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> InteractionCollision;

	/** 营地公共仓库的正式库存事实源；商店发货、拖放移动、保存导出和 UI 展示都读取这一份组件。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Inventory",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatInventoryComponent> InventoryComponent;

	/** 公共仓库当前是否允许玩家交互；编辑器或蓝图可关闭它，提示和打开请求都会一起停用。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Interaction",
		meta = (AllowPrivateAccess = "true"))
	bool bInteractionEnabled = true;

	/** 公共仓库可被确认交互的最大距离，单位厘米；服务器移动公共格时用它复核玩家是否仍在箱子旁。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Interaction",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", Units = "cm"))
	double InteractionRadiusCentimeters = 300.0;

	/** 玩家准星命中公共仓库时看到的提示文本；设计可在蓝图里改名，但不影响仓库数据和购买发货。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Interaction",
		meta = (AllowPrivateAccess = "true"))
	FText InteractionPrompt;

	/**
	 * 营地公共仓库交互时要打开的独立 WBP 类，表示这个团队共享箱子自己的页面形态，不复用默认库存页；蓝图或配置写入它，交互时读取它。
	 * 值无效会让本次打开明确失败，而不是退回默认库存页或改动公共仓库真实物品。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|UI",
		meta = (AllowPrivateAccess = "true"))
	TSoftClassPtr<UCatCampInventoryWidget> InventoryViewClass;

	/** 公共仓库最大格子数；它独立于玩家随身格子，默认更大以承载团队购买物。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|CampInventory",
		meta = (AllowPrivateAccess = "true", ClampMin = "0"))
	int32 InventorySlotCapacity = 48;
};
