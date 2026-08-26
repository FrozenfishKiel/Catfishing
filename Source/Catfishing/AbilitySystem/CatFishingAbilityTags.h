#pragma once

#include "NativeGameplayTags.h"

namespace CatFishingAbilityTags
{
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Fishing_RodInteract);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Fishing_Primary);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Fishing_Cancel);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Fishing_Scoop);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Fishing_Chum);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Fishing_Slack);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Fishing_RodInteract);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Fishing_Primary);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Fishing_Cancel);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Fishing_Scoop);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Fishing_Chum);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Fishing_Slack);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_ActivationPolicy_OnInputTriggered);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_ActivationPolicy_WhileInputActive);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_ActivationPolicy_OnGranted);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Fishing_Aiming);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Fishing_Reeling);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Fishing_Scooping);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Fishing_RodOperating);
	/** 抄网再次可用前的独立冷却；不要与正在挥网/未来踉跄硬直的 State_Fishing_Scooping 混用。 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Fishing_Scoop);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Fishing_Outcome_Caught);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Fishing_Outcome_RodBroken);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Fishing_Outcome_LineBroken);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Fishing_Outcome_CatInWater);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Fishing_Outcome_Cancelled);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Fishing_Cast);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Fishing_Reel);
	/**
	 * 猫身上的一次性表现事件标签（ACatCharacter::Multicast_PlayCosmeticEvent 的载荷）。
	 * 只给"失败时不留任何权威痕迹"的动作用——挥网落空、提竿空竿，没有状态变化可供表现层读取，
	 * 但多人派对里其他玩家必须看得到。其余动作（放竿/收竿/断竿/抛竿/打窝）都有复制状态，走各自的表现事件，不重复走这条。
	 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cosmetic_Fishing_ScoopSwing);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cosmetic_Fishing_HookPull);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Fishing_FightStaminaDelta);
}
