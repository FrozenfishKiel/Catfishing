#include "AbilitySystem/Effects/CatItemEffects.h"
#include "AbilitySystem/Attributes/CatItemBonusAttributeSet.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Tags/CatStateTags.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

namespace CatItemEffectTags
{
 UE_DEFINE_GAMEPLAY_TAG(NextFish, "Cat.Effect.Item.NextFish");
 UE_DEFINE_GAMEPLAY_TAG(NextFight, "Cat.Effect.Item.NextFight");
 UE_DEFINE_GAMEPLAY_TAG(FightBound, "Cat.Effect.Item.FightBound");
 UE_DEFINE_GAMEPLAY_TAG(RestoreAmount, "Cat.Data.Item.RestoreStamina");
 UE_DEFINE_GAMEPLAY_TAG(Splash, "GameplayCue.Cat.Item.Splash");
}
// 恢复配置流程：以提交时计算的缺额写入绿段体力；属性集继续执行上限约束。
UCatGE_RestoreStamina::UCatGE_RestoreStamina()
{
 DurationPolicy = EGameplayEffectDurationType::Instant;
 auto& Modifier = Modifiers.AddDefaulted_GetRef();
 Modifier.Attribute = UCatSurvivalAttributeSet::GetFightStaminaAttribute();
 Modifier.ModifierOp = EGameplayModOp::Additive;
 FSetByCallerFloat Amount; Amount.DataTag = CatItemEffectTags::RestoreAmount;
 Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Amount);
}
// 幸运配置流程：给下一次抽鱼加权并授予效果身份；倍率先用二倍，策划可在 GE 资产内调整。
UCatGE_LuckyCatch::UCatGE_LuckyCatch()
{
 DurationPolicy = EGameplayEffectDurationType::Infinite;
 auto& Modifier = Modifiers.AddDefaulted_GetRef();
 Modifier.Attribute = UCatItemBonusAttributeSet::GetRareFishWeightMultiplierAttribute();
 Modifier.ModifierOp = EGameplayModOp::Multiplicitive;
 Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(2.f));
 auto* Tags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("NextFishTags"));
 FInheritedTagContainer Changes; Changes.Added.AddTag(CatItemEffectTags::NextFish);
 Tags->SetAndApplyTargetTagChanges(Changes); GEComponents.Add(Tags);
}
// 减耗配置流程：成本乘零点七而恢复不变，效果保留到绑定的搏斗结束。
UCatGE_FightEfficiency::UCatGE_FightEfficiency()
{
 DurationPolicy = EGameplayEffectDurationType::Infinite;
 auto& Modifier = Modifiers.AddDefaulted_GetRef();
 Modifier.Attribute = UCatItemBonusAttributeSet::GetStaminaCostMultiplierAttribute();
 Modifier.ModifierOp = EGameplayModOp::Multiplicitive;
 Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(.7f));
 auto* Tags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("NextFightTags"));
 FInheritedTagContainer Changes; Changes.Added.AddTag(CatItemEffectTags::NextFight);
 Tags->SetAndApplyTargetTagChanges(Changes); GEComponents.Add(Tags);
}
// 湿毛配置流程：授予十秒纯表现状态并发布 GC；无伤害、减速或能力取消字段。
UCatGE_ItemSplash::UCatGE_ItemSplash()
{
 DurationPolicy = EGameplayEffectDurationType::HasDuration;
 DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(10.f));
 auto* Tags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("WetTags"));
 FInheritedTagContainer Changes; Changes.Added.AddTag(CatStateTags::Wet);
 Tags->SetAndApplyTargetTagChanges(Changes); GEComponents.Add(Tags);
 FGameplayEffectCue Cue; Cue.GameplayCueTags.AddTag(CatItemEffectTags::Splash);
 GameplayCues.Add(Cue);
}
