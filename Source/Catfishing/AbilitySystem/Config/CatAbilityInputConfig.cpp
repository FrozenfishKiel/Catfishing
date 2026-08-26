#include "AbilitySystem/Config/CatAbilityInputConfig.h"

#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "InputAction.h"

bool UCatAbilityInputConfig::IsRuntimeReady() const
{
	if (!IsNativeInputConfigurationValid())
	{
		return false;
	}
	// 交互 Native Input 与六项 GAS 钓鱼意图都必须完整；右键松线是核心输入，不再作为可选项。
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
