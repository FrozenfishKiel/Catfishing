#include "Online/CatOnlineSettings.h"

// 重连 gate 流程：要求正 TTL 且白名单精确等于当前已实现的 ConnectionLost；负值、零或任何未识别位都保持 fail-closed。
bool UCatOnlineSettings::IsReconnectAdmissionReady() const
{
	return ReconnectRecordTtlSeconds > 0
		&& RecoverableFailureMask == static_cast<int64>(ECatRecoverableFailure::ConnectionLost);
}

// 建局人数读取流程：先清输出，避免失败后沿用调用者旧值；随后只接受当前好友局合同里的 1 到 8 人，非法或未裁配置一律 fail-closed。
bool UCatOnlineSettings::TryGetSessionPublicConnectionLimit(int32& OutPublicConnectionLimit) const
{
	OutPublicConnectionLimit = 0;
	if (SessionPublicConnectionLimit < 1 || SessionPublicConnectionLimit > 8)
	{
		return false;
	}
	OutPublicConnectionLimit = SessionPublicConnectionLimit;
	return true;
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

// 平台操作超时读取流程：先清输出，只接受有限正秒数；Create/Find/Join/Destroy 共用这一个窗口，未裁配置不能被零秒立即失败或无限等待替代。
bool UCatOnlineSettings::TryGetSessionOperationTimeout(double& OutSeconds) const
{
	OutSeconds = 0.0;
	if (!FMath::IsFinite(SessionOperationTimeoutSeconds) || SessionOperationTimeoutSeconds <= 0.0)
	{
		return false;
	}
	OutSeconds = SessionOperationTimeoutSeconds;
	return true;
}
