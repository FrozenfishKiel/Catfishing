#pragma once

#include "Modules/ModuleManager.h"

class FCatfishingEditorModule final : public IModuleInterface
{
public:
	/** 编辑器模块启动入口；项目工具加载后会校正 PIE 停止运行快捷键，让普通 Escape 留给局内菜单。 */
	virtual void StartupModule() override;

	/** 编辑器模块关闭入口；移除尚未完成的快捷键重试委托，避免模块卸载后还被全局帧事件回调。 */
	virtual void ShutdownModule() override;

private:
	/** 尝试把 PlayWorld.StopPlaySession 修正为 Shift+Escape；命令尚未注册时返回 false，让启动流程延后重试。 */
	bool ApplyPlayWorldStopShortcut();

	/** 安排逐帧重试快捷键修复；只在 PlayWorld 命令尚未可见时启用，成功或耗尽预算后自动解除。 */
	void ArmPlayWorldStopShortcutRetry();

	/** 逐帧重试入口；它等待 UnrealEd 注册 PlayWorld 命令，再调用同一个修复流程并清理自身委托。 */
	void HandlePlayWorldStopShortcutRetry();

	/** PlayWorld 快捷键修复的逐帧委托句柄；启动延后时由 Arm 写入，修复成功、预算耗尽或模块关闭时清理，有效期间会让模块继续接收全局帧回调。 */
	FDelegateHandle PlayWorldStopShortcutRetryHandle;

	/** 快捷键修复还允许等待的帧数；Startup 写入默认预算，逐帧重试失败后递减，Shutdown 清零，耗尽时会停止重试并输出命令未注册警告。 */
	int32 PlayWorldStopShortcutAttemptsRemaining = 0;
};
