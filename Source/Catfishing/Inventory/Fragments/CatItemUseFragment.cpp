#include "Inventory/Fragments/CatItemUseFragment.h"
#include "AbilitySystem/Items/Abilities/CatItemGameplayAbility.h"
#include "GameplayEffect.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

// 配置检查流程：先核对非抽象能力、非负数量与前摇，以及效果引用和参数数值，再让能力 CDO 检查该行为支持的字段组合；不检查 GE 内部修饰器。
bool UCatItemUseFragment::IsRuntimeReady() const
{
	if (ResourceCapacity < 0 || ResourceCost < 1 || (ResourceCapacity > 0 && ResourceCost > ResourceCapacity)) return false;
	// 次数属于单件实例；堆叠合并只有数量语义，会丢失各件剩余次数。
	const UCatInventoryItemDefinition* Definition = GetTypedOuter<UCatInventoryItemDefinition>();
	if (ResourceCapacity > 0 && Definition && Definition->GetMaxStackCount() != 1) return false;
	if (!AbilityClass || AbilityClass->HasAnyClassFlags(CLASS_Abstract) || ConsumeCount < 0 || !FMath::IsFinite(CommitDelay) || CommitDelay < 0.f) return false;
	for (const auto& Effect : Effects) if (!Effect) return false;
	for (const auto& Parameter : Magnitudes) if (!Parameter.Key.IsValid() || !FMath::IsFinite(Parameter.Value)) return false;
	FText Error;
	return AbilityClass->GetDefaultObject<UCatItemGameplayAbility>()->ValidateUseConfiguration(*this, Error);
}
#if WITH_EDITOR
// 编辑器检查流程：复用运行配置约束，给出面向资产作者的错误；不会加载场景或创建角色来校验静态数据。
EDataValidationResult UCatItemUseFragment::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult Parent = Super::IsDataValid(Context);
	if (!IsRuntimeReady())
	{
		FText Error;
		if (AbilityClass) AbilityClass->GetDefaultObject<UCatItemGameplayAbility>()->ValidateUseConfiguration(*this, Error);
		Context.AddError(Error.IsEmpty() ? NSLOCTEXT("CatItem", "InvalidUse", "使用配置无效：请检查非抽象能力类、非负消耗数量、有限前摇秒数和效果参数。") : Error);
		return EDataValidationResult::Invalid;
	}
	return Parent == EDataValidationResult::Invalid ? Parent : EDataValidationResult::Valid;
}
#endif
