#pragma once

#include "AbilitySystem/Items/Abilities/CatItemGameplayAbility.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "CatEquipmentItemAbility.generated.h"

class UCatEquipmentItemDefinition;

/** 非消耗装备的使用生命周期；来源仍是随身物品，拿出后的操作能力由装备实例另行授予。 */
UCLASS(Abstract)
class CATFISHING_API UCatEquipmentItemAbility : public UCatItemGameplayAbility
{
	GENERATED_BODY()
public:
	/** 装配、拿竿和抄取不消费使用数量、不执行自用效果；拒绝会被领域流程忽略的配置。 */
	virtual bool ValidateUseConfiguration(const UCatItemUseFragment& Configuration, FText& OutError) const override;
protected:
	/** 以冻结来源构造权威上下文后调用领域行为；异步抄取继续持有能力直至收到完成结果。 */
	virtual void CommitUse() override;
	/** 具体装备行为由 C++ 实现；上下文已固定原始来源，禁止使用当前快捷格替代。 */
	virtual FCatDomainCommandResult ExecuteEquipmentUse(const FCatInventoryItemUseContext& Context, const UCatEquipmentItemDefinition& Definition) PURE_VIRTUAL(UCatEquipmentItemAbility::ExecuteEquipmentUse, return {};);
private:
	/** 接收同步或异步领域终态；只完成仍属于本次请求的能力，迟到结果不影响下一次激活。 */
	void CompleteEquipmentUse(const FCatDomainCommandResult& Result);
};
