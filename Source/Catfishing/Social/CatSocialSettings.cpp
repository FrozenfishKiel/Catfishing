#include "Social/CatSocialSettings.h"

// 偷鱼参数 gate 流程：要求总运行、有限正窗口以及开始/追回两段权威距离；共享缸额外策略由具体命令按容器种类检查。偷取权
// 限不在这里判断——它已经是局主运行期可写的策略，本文件的 TheftPermission 只是那份策略的开局默认值，两处都判会让局主打
// 开权限的动作被静态配置无声否决。
bool UCatSocialSettings::AreTheftParametersReady() const
{
	return bEnableSocialRuntime
		&& FMath::IsFinite(TheftEatingWindowSeconds) && TheftEatingWindowSeconds > 0.0
		&& FMath::IsFinite(TheftInteractionRangeCentimeters) && TheftInteractionRangeCentimeters > 0.0
		&& FMath::IsFinite(TheftCatchRangeCentimeters) && TheftCatchRangeCentimeters > 0.0;
}

// 恶作剧参数 gate 流程：要求总运行和有限正的双方权威交互距离。恶作剧权限同样交给运行期策略，这里不判；恶作剧按需求不
// 设系统级频率上限，密度交给熟人自治，所以这里也没有冷却判据。
bool UCatSocialSettings::AreMischiefParametersReady() const
{
	return bEnableSocialRuntime
		&& FMath::IsFinite(MischiefInteractionRangeCentimeters) && MischiefInteractionRangeCentimeters > 0.0;
}

// 售出 gate 流程：在偷鱼参数齐全之外，额外要求关卡已经登记商店锚点标签，并给出有限正的到店距离。两项缺一就返回
// false，售出这个终态整体不可达；这样"跑到商店卖掉"永远需要真的跑到商店，而不是原地等时间。
bool UCatSocialSettings::IsTheftSaleReady() const
{
	return AreTheftParametersReady() && !TheftSaleShopAnchorTag.IsNone()
		&& FMath::IsFinite(TheftSaleShopRangeCentimeters) && TheftSaleShopRangeCentimeters > 0.0;
}

// 进食 gate 流程：在偷鱼参数齐全之外，额外要求给出有限正的"已经跑离受害者"距离。未裁时返回 false，进食终态不可达，窗
// 口到期只会原样返还，绝不会退化成站在受害者面前把鱼吃掉。
bool UCatSocialSettings::IsTheftConsumptionReady() const
{
	return AreTheftParametersReady()
		&& FMath::IsFinite(TheftConsumeVictimEscapeDistanceCentimeters)
		&& TheftConsumeVictimEscapeDistanceCentimeters > 0.0;
}

// 放牌 gate 流程：只要求总运行和牌子自身的有限正半径与放置距离；护栏不读 MischiefPermission，避免关掉恶作剧的同时把挡偷窃的牌子一起关掉。
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
