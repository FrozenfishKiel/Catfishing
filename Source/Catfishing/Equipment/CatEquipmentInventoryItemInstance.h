#pragma once

#include "CoreMinimal.h"
#include "Equipment/CatEquipmentTypes.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "CatEquipmentInventoryItemInstance.generated.h"

class UCatEquipmentDefinition;

/** 装备资产进入正式库存后的运行实例；只承载装备专属状态，不让通用库存格知道鱼竿、鱼饵或鱼漂规则。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatEquipmentInventoryItemInstance : public UCatInventoryItemInstance
{
	GENERATED_BODY()

public:
	/** 构造装备库存实例；具体定义和耐久初值要等库存组件正式绑定定义资产后才能确定。 */
	UCatEquipmentInventoryItemInstance(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** 复制声明流程：鱼竿耐久和断裂状态随实例复制，其他装备状态仍由定义资产只读提供。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 读取鱼竿当前耐久；非鱼竿实例固定返回 0，避免调用方把普通物品误当工具状态。 */
	double GetRodDurability() const;

	/** 读取鱼竿是否已经断裂；非鱼竿实例固定返回 false，具体能否使用仍由装备定义裁决。 */
	bool IsRodBroken() const;

	/** authority 写入鱼竿运行状态；钓鱼磨损和存档恢复通过它保持实例状态，而不是修改库存格结构。 */
	void SetRodRuntimeStateFromAuthority(double NewRodDurability, bool bNewRodBroken);

	/** 把装备库存实例投影成旧运行库存格；迁移期 UI/存档仍读旧结构，但事实来源已经是库存实例。 */
	bool BuildLegacyRunInventorySlot(int32 StackCount, FCatRunInventorySlot& OutSlot) const;

protected:
	/** 绑定装备定义后补齐装备专属运行初值；通用库存实例状态先由父类完成。 */
	virtual void HandleItemDefinitionAssigned() override;

private:
	/** 鱼竿当前耐久值，单位沿用装备定义；只有 Rod 定义会写入，其他装备保持 0。 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Catfishing|Equipment",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	double RodDurability = 0.0;

	/** 鱼竿是否已断裂；服务器根据耐久写入，客户端只用于表现和旧快照投影。 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Catfishing|Equipment",
		meta = (AllowPrivateAccess = "true"))
	bool bRodBroken = false;
};
