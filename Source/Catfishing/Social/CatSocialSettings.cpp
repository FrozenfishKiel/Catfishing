#include "Social/CatSocialSettings.h"

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
