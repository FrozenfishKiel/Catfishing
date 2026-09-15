#pragma once

#include "CoreMinimal.h"

class UNetDriver;
class UWorld;

/** 开房期间的监听资源所有权；只管理自己创建的 Frontend 驱动，不承担 Session、成员或准入状态。 */
class FCatFrontendListener
{
public:
	/** 在现有 World 原地启动 UE 监听；已有外部驱动或缺少 World context 时拒绝，失败不接管外部资源。 */
	bool Start(UWorld* World, const FGuid& RequestId, uint64 Epoch);
	/** 只关闭仍绑定于原 World 的同一个驱动；地图切换后的新 GameNetDriver 不受影响。 */
	void Stop(const TCHAR* Reason, const FGuid& RequestId, uint64 Epoch);
	bool IsListening(const UWorld* World) const;
	bool IsChangingDriver() const { return bChangingDriver; }

private:
	TWeakObjectPtr<UWorld> ListeningWorld;
	TWeakObjectPtr<UNetDriver> ListeningDriver;
	bool bChangingDriver = false;
};
