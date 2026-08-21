#include "Online/CatOnlineSettings.h"

#include "Misc/PackageName.h"

// 玩法地图读取流程：不加载 World 资产，只把配置软路径规范化为可供旅行、到达判定和 Session 过滤共用的长包名。
bool UCatOnlineSettings::TryGetGameplayMapPackage(FString& OutPackageName) const
{
	OutPackageName = GameplayMap.ToSoftObjectPath().GetLongPackageName();
	if (!FPackageName::IsValidLongPackageName(OutPackageName))
	{
		OutPackageName.Reset();
		return false;
	}
	return true;
}

// 重连 gate 流程：要求正 TTL 且白名单精确等于当前已实现的 ConnectionLost；负值、零或任何未识别位都保持 fail-closed。
bool UCatOnlineSettings::IsReconnectAdmissionReady() const
{
	return ReconnectRecordTtlSeconds > 0
		&& RecoverableFailureMask == static_cast<int64>(ECatRecoverableFailure::ConnectionLost);
}

// Host exit 超时读取流程：先清输出，只接受有限正秒数；远端 Destroy 与最终 Grant ACK 共用该有界窗口，未裁配置不能被零秒立即退出或无限等待替代。
bool UCatOnlineSettings::TryGetHostExitAckTimeout(double& OutSeconds) const
{
	OutSeconds = 0.0;
	if (!FMath::IsFinite(HostExitAckTimeoutSeconds) || HostExitAckTimeoutSeconds <= 0.0)
	{
		return false;
	}
	OutSeconds = HostExitAckTimeoutSeconds;
	return true;
}
