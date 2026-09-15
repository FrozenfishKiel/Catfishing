#include "AbilitySystem/Config/CatAbilityInputConfig.h"

#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "InputAction.h"

bool UCatAbilityInputConfig::IsRuntimeReady() const
{
	if (!IsNativeInputConfigurationValid())
	{
		return false;
	}
	// 交互与鱼竿互动由 NativeInputActions 路由；AbilityInputActions 只保留仍由 GAS 按键驱动的收线、松线和取消。
	if (AbilityInputActions.Num() < 3)
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
	return SeenTags.Contains(CatFishingAbilityTags::Input_Fishing_Primary)
		&& SeenTags.Contains(CatFishingAbilityTags::Input_Fishing_Slack)
		&& SeenTags.Contains(CatFishingAbilityTags::Input_Fishing_Cancel);
}
