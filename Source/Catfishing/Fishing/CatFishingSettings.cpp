#include "Fishing/CatFishingSettings.h"

// 运行 gate 流程：要求产品显式开启总开关、提供 StateTree 软引用、有限正响应窗/终态复制窗与近岸验证；任一为 Unset 都阻止会话创建。
// 搏斗开局的 D₀ 不在这里把关：它已经改成按当前鱼漂的射程与精准度现算，属于每一竿各自的落点事实，
// 由装备目录（漂射程必须为正）和 Boundary（落点距离必须为正）分别 fail-closed，不再是一个全局配置项。
bool UCatFishingSettings::IsRuntimeReady() const
{
	return bEnableFishingRuntime && !FishingSessionStateTree.IsNull()
		&& FMath::IsFinite(TrueBiteWindowSeconds) && TrueBiteWindowSeconds > 0.0
		&& bEnableNearShoreValidation
		&& FMath::IsFinite(ScoopReachCentimeters) && ScoopReachCentimeters > 0.0
		&& FMath::IsFinite(TerminalReplicationWindowSeconds) && TerminalReplicationWindowSeconds > 0.0;
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
