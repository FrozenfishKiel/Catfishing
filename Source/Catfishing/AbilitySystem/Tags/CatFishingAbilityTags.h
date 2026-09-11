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
	 * 挥网、提竿这类输入动作可由命令入口发出；切线和落水只能由 Session 已确认的终局发出。
	 * Montage 只是外观反馈，不参与终局裁决或角色位移。
	 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cosmetic_Fishing_ScoopSwing);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cosmetic_Fishing_HookPull);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cosmetic_Fishing_LineCut);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cosmetic_Fishing_CatInWater);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Fishing_FightStaminaDelta);
	/** 新一天 GE 读取的基础每日供品目标；只由 authority GameMode 写入 Spec，不作为可复制玩法状态。 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Run_BaseDailyOfferingTarget);
	/** 夜晚供品结算 GE 读取的小鱼供品数量；调用方只提交冻结后的鱼事实计数，积分公式留给 ExecCalc。 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Run_Offering_SmallFishCount);
	/** 夜晚供品结算 GE 读取的中鱼供品数量；调用方只提交冻结后的鱼事实计数，积分公式留给 ExecCalc。 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Run_Offering_MediumFishCount);
	/** 夜晚供品结算 GE 读取的大鱼供品数量；调用方只提交冻结后的鱼事实计数，积分公式留给 ExecCalc。 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Run_Offering_LargeFishCount);
	/** 夜晚供品结算 GE 读取的巨鱼供品数量；调用方只提交冻结后的鱼事实计数，积分公式留给 ExecCalc。 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Run_Offering_GiantFishCount);
	/** 夜晚供品结算 GE 读取的臭鱼供品数量；它只折扣成功增益，不能把达标结果倒扣为失败。 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Run_Offering_StinkyFishCount);
	/** 夜晚供品结算 GE 读取的本日基础成功增益；来源是 RunSettings 日程，不由客户端提交。 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Run_Offering_BaseProgressGain);
	/** 夜晚供品结算 GE 读取的本日基础失败损失；来源是 RunSettings 日程，不由客户端提交。 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Run_Offering_BaseProgressLoss);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cosmetic_Fishing_LineBroken);
}
