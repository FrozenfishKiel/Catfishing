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
	/** 所有专用身体动作 Ability 共享的资产标签；Fishing Cancel 用它取消任一活跃的前摇窗口。 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Body_Action);
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
	 * 非 Fishing 的保留 BodyAction GameplayEvent 标签集合。
	 * PlayerController 只投递这些事件，专用 Ability 再回到原领域服务；Wet 反馈不进入玩家技能或 BodyAction 标签集合。
	 * 新增动作必须新增专用请求和 Ability，而不是补业务枚举。
	 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Body_CampRest);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Body_CampfirePlayback);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Body_RescueCharacterToCamp);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Body_RequestManualHelp);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Body_RequestMischief);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityEvent_Body_PlaceProtectionSign);
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
	/** 新一天 GE 读取的原始额度目标；只由 authority GameMode 写入 Spec，不作为可复制玩法状态。 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Run_BaseQuotaTarget);
	/** 献祭 GE 读取的 Items 冻结原始贡献；协调器不计算效率，实际贡献由 Run ASC 返回。 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Run_Sacrifice_RawContribution);
}
