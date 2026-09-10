#pragma once

#include "CoreMinimal.h"
#include "Items/CatItem.h"
#include "CatEquipmentItem.generated.h"

class UCatEquipmentDefinition;

/** 未部署装备的世界拾取物；它只把装备定义翻译为统一库存批次，不承担已部署鱼竿的世界表现。 */
UCLASS(Blueprintable, BlueprintType)
class CATFISHING_API ACatEquipmentItem : public ACatItem
{
	GENERATED_BODY()

public:
	/** 写入这件世界物对应的装备定义；生成器或蓝图在交互前配置，库存仍以定义的实例规则创建物品。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Item")
	void SetEquipmentDefinition(UCatEquipmentDefinition* InEquipmentDefinition);
	/** 当前掉落物的装备身份来源；生成器或蓝图可据此核对配置，但读取不会创建库存实例或提交拾取。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Item")
	UCatEquipmentDefinition* GetEquipmentDefinition() const;
	/** 装备世界物生成后校验定义是否完整；无效定义保持可见但拾取会被批次校验拒绝。 */
	virtual void InitializeActorSpawnConfig() override;
	/** 从装备定义构造一件的正式库存收货批次，避免世界物重复保存另一套定义 ID 或实例类型。 */
	virtual FCatInventoryReceiveBatch GetPickupInventory() const override;

protected:
	/** 这件掉落物代表的静态装备定义；蓝图或生成器写入，拾取时由它确定库存定义和运行实例类型。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|Item")
	TObjectPtr<UCatEquipmentDefinition> EquipmentDefinition;
};
