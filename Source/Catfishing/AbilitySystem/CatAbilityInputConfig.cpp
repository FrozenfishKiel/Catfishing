#include "AbilitySystem/CatAbilityInputConfig.h"

#include "AbilitySystem/CatFishingAbilityTags.h"
#include "InputAction.h"

bool UCatAbilityInputConfig::IsRuntimeReady() const
{
	// 正式输入门禁先要求六项玩家意图都有独立 Action，再逐项拒绝空引用、重复 Tag 或重复 Action；
	// 最后核对稳定 Fishing Tag 集合，确保“配置可加载”不会掩盖收放线入口缺失。
	if (AbilityInputActions.Num() < 6)
	{
		return false;
	}
	TSet<FGameplayTag> SeenTags;
	TSet<const UInputAction*> SeenActions;
	for (const FCatAbilityInputAction& Entry : AbilityInputActions)
	{
		if (!Entry.InputAction || !Entry.InputTag.IsValid() || SeenTags.Contains(Entry.InputTag)
			|| SeenActions.Contains(Entry.InputAction.Get()))
		{
			return false;
		}
		SeenTags.Add(Entry.InputTag);
		SeenActions.Add(Entry.InputAction.Get());
	}
	return SeenTags.Contains(CatFishingAbilityTags::Input_Fishing_RodInteract)
		&& SeenTags.Contains(CatFishingAbilityTags::Input_Fishing_Primary)
		&& SeenTags.Contains(CatFishingAbilityTags::Input_Fishing_Slack)
		&& SeenTags.Contains(CatFishingAbilityTags::Input_Fishing_Cancel)
		&& SeenTags.Contains(CatFishingAbilityTags::Input_Fishing_Scoop)
		&& SeenTags.Contains(CatFishingAbilityTags::Input_Fishing_Chum);
}
