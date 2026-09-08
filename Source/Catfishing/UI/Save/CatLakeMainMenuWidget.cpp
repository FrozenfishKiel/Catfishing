#include "UI/Save/CatLakeMainMenuWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Input/Events.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"

#define LOCTEXT_NAMESPACE "CatLakeMainMenuWidget"

// 渲染流程：
// 1. 保存 Controller 给出的唯一菜单投影，避免 Widget 从按钮状态反推 Save 或 Online 事实。
// 2. 写入可选 WBP 控件；缺少控件说明正式资产选择了自己的表现路径，不创建第二套状态。
// 3. 最后通知蓝图扩展点，让动画或自定义控件读取同一份 LastMenuViewState。
void UCatLakeMainMenuWidget::RenderMenu(const FCatLakeMainMenuViewState& ViewState)
{
	LastMenuViewState = ViewState;
	if (StatusTextBlock)
	{
		StatusTextBlock->SetText(LastMenuViewState.StatusText);
		StatusTextBlock->SetVisibility(
			LastMenuViewState.StatusText.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (SettingsButton)
	{
		SettingsButton->SetIsEnabled(LastMenuViewState.bSettingsEnabled);
	}
	if (SaveButton)
	{
		SaveButton->SetIsEnabled(LastMenuViewState.bSaveEnabled);
	}
	if (ExitGameButton)
	{
		ExitGameButton->SetIsEnabled(LastMenuViewState.bExitEnabled);
	}
	BP_RenderMenu(LastMenuViewState);
}

// 状态读取流程：返回最后一次 Controller 渲染输入；调用方不能据此提交保存或离局，只能用于表现绑定。
const FCatLakeMainMenuViewState& UCatLakeMainMenuWidget::GetLastMenuViewState() const
{
	return LastMenuViewState;
}

// 关闭请求流程：把 ESC、关闭按钮或蓝图调用统一转成 Close 意图；本对象不直接切换输入模式。
void UCatLakeMainMenuWidget::RequestCloseMenu()
{
	SubmitMenuAction(ECatLakeMainMenuAction::Close);
}

// 设置请求流程：只广播设置意图；正式设置页是否存在由 Controller 处理，蓝图扩展只做按钮反馈或动画。
void UCatLakeMainMenuWidget::RequestOpenSettings()
{
	SubmitMenuAction(ECatLakeMainMenuAction::OpenSettings);
}

// 保存请求流程：只广播保存意图；Save 子系统负责判断 Host、活动槽、busy 和磁盘结果。
void UCatLakeMainMenuWidget::RequestSave()
{
	SubmitMenuAction(ECatLakeMainMenuAction::Save);
}

// 退出请求流程：只广播离开当前局意图；Online 子系统负责保存、Session teardown 和回前台。
void UCatLakeMainMenuWidget::RequestExitGame()
{
	SubmitMenuAction(ECatLakeMainMenuAction::ExitGame);
}

// 构造流程：父类完成控件绑定后打开焦点能力，再把 Designer 可选按钮接到统一意图入口。
void UCatLakeMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	BindDesignerButtons();
}

// 销毁流程：先解除 Designer 按钮动态绑定，避免下次 Slate 重建时重复 AddDynamic，再交还父类生命周期。
void UCatLakeMainMenuWidget::NativeDestruct()
{
	UnbindDesignerButtons();
	Super::NativeDestruct();
}

// 预览键流程：子按钮处理前先识别 Escape；关闭意图仍通过 Controller，保证输入锁成对释放。
FReply UCatLakeMainMenuWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (ShouldCloseMenuFromKey(InKeyEvent))
	{
		RequestCloseMenu();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

// 键盘流程：当菜单根直接持有焦点时复用同一关闭键判断；其它按键继续走父类默认处理。
FReply UCatLakeMainMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (ShouldCloseMenuFromKey(InKeyEvent))
	{
		RequestCloseMenu();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

// Designer 按钮绑定流程：每个按钮先移除本对象旧绑定再新增，覆盖 Construct 重入和 WBP 热重建。
void UCatLakeMainMenuWidget::BindDesignerButtons()
{
	if (SettingsButton)
	{
		SettingsButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestOpenSettings);
		SettingsButton->OnClicked.AddDynamic(this, &ThisClass::RequestOpenSettings);
	}
	if (SaveButton)
	{
		SaveButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestSave);
		SaveButton->OnClicked.AddDynamic(this, &ThisClass::RequestSave);
	}
	if (ExitGameButton)
	{
		ExitGameButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestExitGame);
		ExitGameButton->OnClicked.AddDynamic(this, &ThisClass::RequestExitGame);
	}
	if (CloseButton)
	{
		CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestCloseMenu);
		CloseButton->OnClicked.AddDynamic(this, &ThisClass::RequestCloseMenu);
	}
}

// Designer 按钮解绑流程：只解除本类添加的动态委托，蓝图自己绑定的动画或声音反馈不被清掉。
void UCatLakeMainMenuWidget::UnbindDesignerButtons()
{
	if (SettingsButton)
	{
		SettingsButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestOpenSettings);
	}
	if (SaveButton)
	{
		SaveButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestSave);
	}
	if (ExitGameButton)
	{
		ExitGameButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestExitGame);
	}
	if (CloseButton)
	{
		CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestCloseMenu);
	}
}

// 意图提交流程：先把 Action 交给菜单 Controller 处理权威系统，再通知蓝图做不改变业务状态的表现扩展。
void UCatLakeMainMenuWidget::SubmitMenuAction(const ECatLakeMainMenuAction Action)
{
	OnActionRequested.Broadcast(Action);
	BP_HandleMenuAction(Action);
}

// 关闭键判断流程：只消费菜单已经获得焦点后的 Escape；运行时 ESC 绑定仍由 Enhanced Input Action 负责。
bool UCatLakeMainMenuWidget::ShouldCloseMenuFromKey(const FKeyEvent& InKeyEvent) const
{
	return InKeyEvent.GetKey() == EKeys::Escape;
}

#undef LOCTEXT_NAMESPACE
