#include "Social/CatSocialSettings.h"

// 偷鱼 gate 流程：同时要求总运行、显式 Enabled、有限正窗口以及开始/追回两段权威距离；共享缸额外策略由具体命令按容器种类检查。
bool UCatSocialSettings::IsTheftReady() const
{
	return bEnableSocialRuntime && TheftPermission == ECatDomainPolicy::Enabled
		&& FMath::IsFinite(TheftEatingWindowSeconds) && TheftEatingWindowSeconds > 0.0
		&& FMath::IsFinite(TheftInteractionRangeCentimeters) && TheftInteractionRangeCentimeters > 0.0
		&& FMath::IsFinite(TheftCatchRangeCentimeters) && TheftCatchRangeCentimeters > 0.0;
}

// 恶作剧 gate 流程：要求显式 Enabled、有限正冷却和双方权威交互距离；Unset/Disabled 均不会被好友局默认值覆盖。
bool UCatSocialSettings::IsMischiefReady() const
{
	return bEnableSocialRuntime && MischiefPermission == ECatDomainPolicy::Enabled
		&& FMath::IsFinite(MischiefCooldownSeconds) && MischiefCooldownSeconds > 0.0
		&& FMath::IsFinite(MischiefInteractionRangeCentimeters) && MischiefInteractionRangeCentimeters > 0.0;
}

// 求助 gate 流程：要求总运行、有限正范围和冷却；普通信号始终保持 nearby，不自动升级全局。
bool UCatSocialSettings::IsManualHelpReady() const
{
	return bEnableSocialRuntime && FMath::IsFinite(ManualHelpRadiusCentimeters) && ManualHelpRadiusCentimeters > 0.0
		&& FMath::IsFinite(ManualHelpCooldownSeconds) && ManualHelpCooldownSeconds > 0.0;
}
