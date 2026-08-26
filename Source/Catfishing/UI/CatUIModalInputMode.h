#pragma once

#include "CoreMinimal.h"

class APlayerController;
class UUserWidget;

/** 单个模态 UI 打开期间的输入恢复记录；它只保存本页面改过的鼠标和输入锁状态，不代表全局 UI 栈。 */
struct FCatUIModalInputModeState
{
	/** 本页面在 Controller 上申请过移动/视角忽略计数的记录；Open 首次写 true，Close 据此只释放自己申请的一层锁。 */
	bool bInputLockApplied = false;

	/** 打开页面前 Controller 的鼠标可见性快照；Open 写入，Close 读取它恢复本页面接管前的鼠标显示状态。 */
	bool bPreviousMouseCursorVisible = false;
};

namespace CatUIModalInputMode
{
	/** 为已经挂入视口的模态 Widget 接管本地输入；成功后 UI 获得焦点，角色移动和视角输入被本页面暂停。 */
	void Open(APlayerController* Controller, UUserWidget* FocusWidget, FCatUIModalInputModeState& State);

	/** 释放本页面的模态输入接管；关闭后恢复游戏输入和鼠标状态，Controller 已失效时只清理本地恢复记录。 */
	void Close(APlayerController* Controller, FCatUIModalInputModeState& State);
}
