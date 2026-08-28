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
	/** 抄网再次可用前的独立冷却；不要与正在挥网或未来硬直状态混用。 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Fishing_Scoop);
	/**
	 * 非 Fishing 身体动作的 GameplayEvent 标签集合。
	 * PlayerController 只投递这些事件，Ability 再回到原领域服务；新增动作必须同步补 Command 枚举、标签映射和触发器。
	 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Body_RequestSacrifice);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Body_CampRest);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Body_CampfirePlayback);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Body_TransferObjectBetweenContainers);
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
	 * 挥网、提竿这类输入动作可由命令入口发出；断线/落水只能由 Session 已确认的终局发出。
	 * Montage 只是外观反馈，不参与终局裁决或角色位移。
	 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cosmetic_Fishing_ScoopSwing);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cosmetic_Fishing_HookPull);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cosmetic_Fishing_LineBroken);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cosmetic_Fishing_CatInWater);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Fishing_FightStaminaDelta);
}
