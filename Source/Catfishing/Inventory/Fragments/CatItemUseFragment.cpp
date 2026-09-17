#include "Inventory/Fragments/CatItemUseFragment.h"
#include "AbilitySystem/Items/CatItemGameplayAbility.h"
#include "GameplayEffect.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

// 配置检查流程：能力类型必须存在，成本和前摇不得为负，效果及命名参数必须可解析；不猜测 GE 内部实现。
bool UCatItemUseFragment::IsRuntimeReady() const
{
	if (!AbilityClass || ConsumeCount < 0 || !FMath::IsFinite(CommitDelay) || CommitDelay < 0.f) return false;
	for (const auto& Effect : Effects) if (!Effect) return false;
	for (const auto& Parameter : Magnitudes) if (!Parameter.Key.IsValid() || !FMath::IsFinite(Parameter.Value)) return false;
	return true;
}
#if WITH_EDITOR
// 编辑器检查流程：复用运行配置约束，给出面向资产作者的错误；不会加载场景或创建角色来校验静态数据。
EDataValidationResult UCatItemUseFragment::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult Parent = Super::IsDataValid(Context);
	if (!IsRuntimeReady())
	{
		Context.AddError(NSLOCTEXT("CatItem", "InvalidUse", "使用配置无效：请检查能力类、非负消耗数量、有限前摇秒数和效果参数。"));
		return EDataValidationResult::Invalid;
	}
	return Parent == EDataValidationResult::Invalid ? Parent : EDataValidationResult::Valid;
}
#endif
