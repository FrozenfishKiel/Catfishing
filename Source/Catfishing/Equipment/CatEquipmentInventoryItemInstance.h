#pragma once

#include "CoreMinimal.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "CatEquipmentInventoryItemInstance.generated.h"

class UCatEquipmentDefinition;

/** 装备资产进入正式库存后的运行实例；只承载装备专属状态，不让通用库存格知道鱼竿、鱼饵、鱼漂或抄网规则。 */
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

	/** 装备实例的库存 Use 裁决；扣量、借出和断竿拒绝都由同一运行实例回答。 */
	virtual ECatDomainCommandError Use(const FCatInventoryEntry& Item, int32 Quantity) const override;

	/** 装备实例的库存 UnUse 裁决；收回时只校验同一活动实例仍和装备定义一致。 */
	virtual ECatDomainCommandError UnUse(const FCatInventoryEntry& Item) const override;

	/** 读取这份装备实例是否需要 Use 后进入 held entry；鱼竿等部署物通过定义配置声明这项事实。 */
	virtual bool KeepsInventoryInstanceWhileUsed() const override;

	/** 读取这份装备实例是否需要 Use 后扣减数量；窝料、草药和修理材料通过定义配置声明这项事实。 */
	virtual bool ConsumesInventoryQuantityOnUse() const override;

	/** 装备实例只读声明自己可作为库存 Use 候选；真正的选择、权限和版本仍在结构化提交时复核。 */
	virtual bool CanUseFromInventory(const FCatInventoryEntry& InventoryEntry, APawn* UserPawn) const override;

	/** 装备库存 Use 的正式提交扩展面；正式库存重读槽位后调用它，让钓具选择规则停留在装备物品实例里。 */
	virtual FCatDomainCommandResult UseFromInventorySlotFromAuthority(
		const FCatInventoryEntry& InventoryEntry, const FCatInventoryItemUseContext& UseContext) override;

protected:
	/** 绑定装备定义后补齐装备专属运行初值；通用库存实例状态先由父类完成。 */
	virtual void HandleItemDefinitionAssigned() override;

private:
	/** 鱼竿当前耐久值，单位沿用装备定义；只有 Rod 定义会写入，其他装备保持 0。 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Catfishing|Equipment",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	double RodDurability = 0.0;

	/** 鱼竿是否已断裂；服务器根据耐久写入，客户端只用于表现和读模型展示。 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Catfishing|Equipment",
		meta = (AllowPrivateAccess = "true"))
	bool bRodBroken = false;
};
