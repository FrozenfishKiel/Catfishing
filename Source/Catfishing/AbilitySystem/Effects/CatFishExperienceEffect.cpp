#include "AbilitySystem/Effects/CatFishExperienceEffect.h"
#include "AbilitySystem/Attributes/CatGrowthAttributeSet.h"
#include "NativeGameplayTags.h"
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ItemFishExperience, "Cat.Data.Item.FishExperience");

// 效果配置流程：只添加一项即时经验元属性修饰，实际成长仍由属性执行回调处理。
UCatGE_FishExperience::UCatGE_FishExperience()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayModifierInfo& Modifier = Modifiers.AddDefaulted_GetRef();
	Modifier.Attribute = UCatGrowthAttributeSet::GetIncomingExperienceAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	FSetByCallerFloat Value;
	Value.DataTag = GetExperienceTag();
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Value);
}
// 参数读取流程：返回唯一的原生标签，不创建另一份可变配置。
FGameplayTag UCatGE_FishExperience::GetExperienceTag() { return TAG_ItemFishExperience; }
