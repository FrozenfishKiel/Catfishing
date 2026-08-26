#include "UI/CatUIModalInputMode.h"

#include "Blueprint/UserWidget.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"

// 打开流程：
// 1. Controller 或焦点 Widget 缺失时不写恢复记录，避免留下无法关闭的半个模态状态。
// 2. 首次打开时保存鼠标可见性并给 Controller 申请一层移动/视角输入锁；重复打开只刷新焦点，不叠加锁。
// 3. 切到 UIOnly 并把焦点交给当前页面，让关闭键和按钮点击都由 Widget 接收。
// 4. 立即停止 PawnMovement，避免玩家按着方向键打开 UI 后角色继续沿旧输入滑动。
void CatUIModalInputMode::Open(APlayerController* Controller, UUserWidget* FocusWidget,
	FCatUIModalInputModeState& State)
{
	if (!Controller || !FocusWidget)
	{
		return;
	}
	if (!State.bInputLockApplied)
	{
		State.bPreviousMouseCursorVisible = Controller->bShowMouseCursor;
		Controller->SetIgnoreMoveInput(true);
		Controller->SetIgnoreLookInput(true);
		State.bInputLockApplied = true;
	}

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(FocusWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Controller->SetInputMode(InputMode);
	Controller->bShowMouseCursor = true;
	FocusWidget->SetKeyboardFocus();

	if (APawn* Pawn = Controller->GetPawn())
	{
		if (UPawnMovementComponent* Movement = Pawn->GetMovementComponent())
		{
			Movement->StopMovementImmediately();
		}
	}
}

// 关闭流程：
// 1. Controller 已经失效时只清空本地恢复记录，避免下一次绑定误用旧页面状态。
// 2. Controller 有效时先恢复 GameOnly，让玩家重新获得游戏输入。
// 3. 如果本页面曾申请输入锁，才恢复打开前鼠标状态并释放一层 SetIgnoreMove/LookInput，保留其他系统可能已经申请的锁。
// 4. 清空恢复记录，保证下一次打开会重新捕获新的鼠标状态。
void CatUIModalInputMode::Close(APlayerController* Controller, FCatUIModalInputModeState& State)
{
	if (!Controller)
	{
		State = FCatUIModalInputModeState();
		return;
	}

	Controller->SetInputMode(FInputModeGameOnly());
	if (State.bInputLockApplied)
	{
		Controller->bShowMouseCursor = State.bPreviousMouseCursorVisible;
		Controller->SetIgnoreMoveInput(false);
		Controller->SetIgnoreLookInput(false);
	}
	State = FCatUIModalInputModeState();
}
