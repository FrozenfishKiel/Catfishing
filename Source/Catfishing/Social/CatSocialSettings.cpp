#include "Social/CatSocialSettings.h"

// 恶作剧 gate 流程：只要求总运行与双方权威交互距离。
// 联机社交 §3.1.4：不设系统级频率上限与时机限制（熟人自治），关键搏斗时刻也不额外豁免——被整正是戏；
// 玩坏了由房主踢人兜底。所以这里没有冷却项，也没有「恶作剧权限」这一档（09-12 裁决③该概念已退役）。
// 剩下的这个距离不是闸，是几何：够不着就整不到，与频率无关。
bool UCatSocialSettings::IsMischiefReady() const
{
	return bEnableSocialRuntime
		&& FMath::IsFinite(MischiefInteractionRangeCentimeters) && MischiefInteractionRangeCentimeters > 0.0;
}

// 立牌 gate 流程：只要求总运行与立牌自己的保护半径、放置半径。
// 它从恶作剧 gate 里拆出来（09-12）：旧实现两者共用 IsMischiefReady，于是恶作剧那边任何一项缺配都会顺手
// 把立牌一起关死——而立牌恰恰是恶作剧唯一的护栏，两者同生共死是反的。
bool UCatSocialSettings::IsProtectionSignReady() const
{
	return bEnableSocialRuntime
		&& FMath::IsFinite(ProtectionSignRadiusCentimeters) && ProtectionSignRadiusCentimeters > 0.0
		&& FMath::IsFinite(ProtectionSignPlacementRangeCentimeters) && ProtectionSignPlacementRangeCentimeters > 0.0;
}

// 求助 gate 流程：要求总运行、有限正范围和冷却；普通信号始终保持 nearby，不自动升级全局。
bool UCatSocialSettings::IsManualHelpReady() const
{
	return bEnableSocialRuntime && FMath::IsFinite(ManualHelpRadiusCentimeters) && ManualHelpRadiusCentimeters > 0.0
		&& FMath::IsFinite(ManualHelpCooldownSeconds) && ManualHelpCooldownSeconds > 0.0;
}
