#include "Fishing/CatFishingSettings.h"

#include "Data/CatFishPersonalityDefinition.h"

// 运行 gate 流程：要求产品显式开启总开关、提供 StateTree 软引用、有限正响应窗/终态复制窗与近岸验证；任一为 Unset 都阻止会话创建。
bool UCatFishingSettings::IsRuntimeReady() const
{
	return bEnableFishingRuntime && !FishingSessionStateTree.IsNull() && !FishBehaviorStateTree.IsNull()
		&& FMath::IsFinite(TrueBiteWindowSeconds) && TrueBiteWindowSeconds > 0.0
		&& FMath::IsFinite(FightStrengthPerKilogram) && FightStrengthPerKilogram > 0.0
		&& FMath::IsFinite(FightAccelerationPerStrength) && FightAccelerationPerStrength > 0.0
		&& FMath::IsFinite(FightDriveResponseSeconds) && FightDriveResponseSeconds > 0.0
		&& bEnableNearShoreValidation
		&& FMath::IsFinite(ScoopReachCentimeters) && ScoopReachCentimeters > 0.0
		&& FMath::IsFinite(TerminalReplicationWindowSeconds) && TerminalReplicationWindowSeconds > 0.0;
}

const UCatBitePersonalityDefinition* UCatFishingSettings::FindBitePersonality(const FName PersonalityId) const
{
	if (PersonalityId.IsNone()) return nullptr;
	const UCatBitePersonalityDefinition* Match = nullptr;
	for (const TSoftObjectPtr<UCatBitePersonalityDefinition>& Entry : BitePersonalities)
	{
		const UCatBitePersonalityDefinition* Candidate = Entry.LoadSynchronous();
		if (Candidate && Candidate->BitePersonalityId == PersonalityId && Candidate->IsRuntimeDefinitionReady())
		{
			if (Match) return nullptr;
			Match = Candidate;
		}
	}
	return Match;
}

const UCatFightPersonalityDefinition* UCatFishingSettings::FindFightPersonality(const FName PersonalityId) const
{
	if (PersonalityId.IsNone()) return nullptr;
	const UCatFightPersonalityDefinition* Match = nullptr;
	for (const TSoftObjectPtr<UCatFightPersonalityDefinition>& Entry : FightPersonalities)
	{
		const UCatFightPersonalityDefinition* Candidate = Entry.LoadSynchronous();
		if (Candidate && Candidate->FightPersonalityId == PersonalityId && Candidate->IsRuntimeDefinitionReady())
		{
			if (Match) return nullptr;
			Match = Candidate;
		}
	}
	return Match;
}

// 抢抄距离读取流程：先清输出，再复用完整 runtime gate 并读取有限正厘米值；调用方只能比较服务器 Character 与 StateTree 提供的权威目标。
bool UCatFishingSettings::TryGetScoopReach(double& OutReachCentimeters) const
{
	OutReachCentimeters = 0.0;
	if (!IsRuntimeReady())
	{
		return false;
	}
	OutReachCentimeters = ScoopReachCentimeters;
	return true;
}

bool UCatFishingSettings::TryGetScoopCooldown(double& OutCooldownSeconds) const
{
	OutCooldownSeconds = 0.0;
	if (!FMath::IsFinite(ScoopCooldownSeconds) || ScoopCooldownSeconds <= 0.0)
	{
		return false;
	}
	OutCooldownSeconds = ScoopCooldownSeconds;
	return true;
}

bool UCatFishingSettings::TryGetBiteWarning(double& OutWarningSeconds) const
{
	OutWarningSeconds = 0.0;
	if (!FMath::IsFinite(BiteWarningSeconds) || BiteWarningSeconds <= 0.0
		|| !FMath::IsFinite(MinimumBiteDelaySeconds) || MinimumBiteDelaySeconds < 0.0
		|| !FMath::IsFinite(MaximumBiteDelaySeconds)
		|| MaximumBiteDelaySeconds < MinimumBiteDelaySeconds + BiteWarningSeconds)
	{
		return false;
	}
	OutWarningSeconds = BiteWarningSeconds;
	return true;
}

bool UCatFishingSettings::TryGetRodOperatorLayout(int32& OutMaximumSlots,
	double& OutSlotSpacingCentimeters) const
{
	OutMaximumSlots = 0;
	OutSlotSpacingCentimeters = 0.0;
	// 槽位数组会进入复制状态，因此必须有明确上限，避免错误配置制造无界公开状态。
	if (MaximumRodOperatorSlots < 1 || MaximumRodOperatorSlots > 8
		|| !FMath::IsFinite(RodOperatorSlotSpacingCentimeters)
		|| RodOperatorSlotSpacingCentimeters < 0.0)
	{
		return false;
	}
	OutMaximumSlots = MaximumRodOperatorSlots;
	OutSlotSpacingCentimeters = RodOperatorSlotSpacingCentimeters;
	return true;
}

// 终态留存读取流程：先清输出，再复用完整 runtime gate；只返回有限正秒数，调用方可用同一值设置 Actor lifespan。
bool UCatFishingSettings::TryGetTerminalReplicationWindow(double& OutWindowSeconds) const
{
	OutWindowSeconds = 0.0;
	if (!IsRuntimeReady())
	{
		return false;
	}
	OutWindowSeconds = TerminalReplicationWindowSeconds;
	return true;
}
