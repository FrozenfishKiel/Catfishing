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
	/** 非 Fishing 身体动作共享 Ability 的资产标签；AbilitySet 用它证明默认授予里存在正式 BodyAction 网关。 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Body_Command);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_ActivationPolicy_OnInputTriggered);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_ActivationPolicy_WhileInputActive);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_ActivationPolicy_OnGranted);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Fishing_Aiming);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Fishing_Reeling);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Fishing_Scooping);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Fishing_RodOperating);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Fishing_Outcome_Caught);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Fishing_Outcome_RodBroken);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Fishing_Outcome_CatInWater);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Fishing_Outcome_Cancelled);
	/**
	 * 非 Fishing 身体动作的 GameplayEvent 标签集合。
	 * PlayerController 只投递这些事件，Ability 再回到原领域服务；新增动作必须同步补 Command 枚举、标签映射和触发器。
	 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Body_RequestSacrifice);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Body_CampRest);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Body_CampfirePlayback);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Body_TransferFishToTank);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Body_RescueCharacterToCamp);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Body_RepairRodAtCamp);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Body_UseHerbOnCharacter);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Body_ConsumeFish);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Body_BeginTheft);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Body_CatchTheft);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Body_RequestManualHelp);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Body_RequestMischief);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Body_PlaceProtectionSign);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Body_CompleteShakeDry);
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
