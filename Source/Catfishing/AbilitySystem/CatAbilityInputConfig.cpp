#include "AbilitySystem/CatAbilityInputConfig.h"

#include "AbilitySystem/CatFishingAbilityTags.h"
#include "InputAction.h"

bool UCatAbilityInputConfig::IsRuntimeReady() const
{
	if (!IsNativeInputConfigurationValid())
	{
		return false;
	}
	// 五个核心钓鱼输入必须齐全；允许追加可选输入（例如右键松线）。
	if (AbilityInputActions.Num() < 5)
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
		&& SeenTags.Contains(CatFishingAbilityTags::Input_Fishing_Cancel)
		&& SeenTags.Contains(CatFishingAbilityTags::Input_Fishing_Scoop)
		&& SeenTags.Contains(CatFishingAbilityTags::Input_Fishing_Chum);
}
