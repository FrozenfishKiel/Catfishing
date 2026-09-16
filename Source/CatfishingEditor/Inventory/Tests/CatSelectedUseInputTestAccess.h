#pragma once

#include "AbilitySystem/Input/CatAbilityInputBindingComponent.h"
#include "Framework/Game/CatfishingPlayerController.h"

/** 定向回归使用真实左键路由，避免直接调用 Use 隐藏 Grab/Ability 分流错误。 */
struct FCatSelectedUseInputTestAccess
{
	static void Press(ACatfishingPlayerController* Controller)
	{
		if (auto* Input = Controller->FindComponentByClass<UCatAbilityInputBindingComponent>())
			Input->HandleAbilityInputTagPressed(FGameplayTag::RequestGameplayTag(TEXT("Cat.Input.Fishing.Primary")));
	}
	static void Release(ACatfishingPlayerController* Controller)
	{
		if (auto* Input = Controller->FindComponentByClass<UCatAbilityInputBindingComponent>())
			Input->HandleAbilityInputTagReleased(FGameplayTag::RequestGameplayTag(TEXT("Cat.Input.Fishing.Primary")));
	}
};
