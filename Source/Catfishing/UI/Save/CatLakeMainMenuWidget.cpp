#include "UI/Save/CatLakeMainMenuWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Input/Events.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CatLakeMainMenuWidget"

namespace
{
	constexpr float NativeMenuWidth = 320.0f;
	constexpr float NativeMenuButtonPaddingX = 16.0f;
	constexpr float NativeMenuButtonPaddingY = 10.0f;

	// 原生按钮构造只服务无 WBP fallback；正式项目资产可以完全覆盖表现而继续复用同一套意图回调。
	TSharedRef<SWidget> BuildNativeMenuButton(const FText& Label, const FOnClicked& OnClicked,
		TSharedPtr<SButton>& OutButton)
	{
		return SAssignNew(OutButton, SButton)
			.ContentPadding(FMargin(NativeMenuButtonPaddingX, NativeMenuButtonPaddingY))
			.OnClicked(OnClicked)
			[
				SNew(STextBlock)
				.Text(Label)
				.Justification(ETextJustify::Center)
			];
	}
}

// 渲染流程：
// 1. 保存 Controller 给出的唯一菜单投影，避免 Widget 从按钮状态反推 Save 或 Online 事实。
// 2. 写入可选 WBP 控件；缺少控件说明正式资产选择了自己的表现路径，不创建第二套状态。
// 3. 同步刷新原生 fallback 指针；如果当前使用 WBP，这些指针为空且不会产生副作用。
// 4. 最后通知蓝图扩展点，让动画或自定义控件读取同一份 LastMenuViewState。
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
	RefreshNativeFallbackControls();
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

// 设置请求流程：只广播设置意图；正式设置页是否存在以及如何打开由 Controller 或蓝图扩展处理。
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
	RefreshNativeFallbackControls();
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

// Widget 重建流程：正式 WBP 有 RootWidget 时完全交给 UUserWidget；没有资产根时才用原生 fallback 防止打开空页。
TSharedRef<SWidget> UCatLakeMainMenuWidget::RebuildWidget()
{
	if (WidgetTree && WidgetTree->RootWidget)
	{
		return Super::RebuildWidget();
	}
	return RebuildNativeFallbackMenu();
}

// Slate 释放流程：清掉 fallback 指针，防止下次 RenderMenu 写入已释放的 Slate 控件。
void UCatLakeMainMenuWidget::ReleaseSlateResources(const bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	NativeSettingsButton.Reset();
	NativeSaveButton.Reset();
	NativeExitGameButton.Reset();
	NativeStatusTextBlock.Reset();
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

// 意图提交流程：先把 Action 交给原生 Controller 处理权威系统，再通知蓝图做表现扩展。
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

// 原生 fallback 构建流程：创建全屏半透明遮罩和居中竖排三按钮；尺寸固定在适合键鼠菜单的窄列，避免文本导致布局跳动。
TSharedRef<SWidget> UCatLakeMainMenuWidget::RebuildNativeFallbackMenu()
{
	const FSlateBrush* SolidBrush = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	TSharedRef<SWidget> Menu =
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(SolidBrush)
			.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.52f))
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(SolidBrush)
			.BorderBackgroundColor(FLinearColor(0.035f, 0.04f, 0.045f, 0.92f))
			.Padding(FMargin(24.0f, 22.0f))
			[
				SNew(SBox)
				.WidthOverride(NativeMenuWidth)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.0f, 0.0f, 0.0f, 18.0f))
					.HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("MenuTitle", "暂停菜单"))
						.ColorAndOpacity(FSlateColor(FLinearColor::White))
						.Justification(ETextJustify::Center)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
					[
						BuildNativeMenuButton(
							LOCTEXT("SettingsButton", "设置"),
							FOnClicked::CreateUObject(this, &ThisClass::HandleNativeSettingsClicked),
							NativeSettingsButton)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
					[
						BuildNativeMenuButton(
							LOCTEXT("SaveButton", "保存"),
							FOnClicked::CreateUObject(this, &ThisClass::HandleNativeSaveClicked),
							NativeSaveButton)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.0f, 0.0f, 0.0f, 12.0f))
					[
						BuildNativeMenuButton(
							LOCTEXT("ExitGameButton", "退出游戏"),
							FOnClicked::CreateUObject(this, &ThisClass::HandleNativeExitGameClicked),
							NativeExitGameButton)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Fill)
					[
						SAssignNew(NativeStatusTextBlock, STextBlock)
						.Text(FText::GetEmpty())
						.AutoWrapText(true)
						.Justification(ETextJustify::Center)
						.ColorAndOpacity(FSlateColor(FLinearColor(0.82f, 0.86f, 0.9f, 1.0f)))
					]
				]
			]
		];
	RefreshNativeFallbackControls();
	return Menu;
}

// fallback 刷新流程：状态来自 LastMenuViewState；禁用按钮只阻止重复点击，不改变按钮语义或底层业务状态。
void UCatLakeMainMenuWidget::RefreshNativeFallbackControls()
{
	if (NativeSettingsButton)
	{
		NativeSettingsButton->SetEnabled(LastMenuViewState.bSettingsEnabled);
	}
	if (NativeSaveButton)
	{
		NativeSaveButton->SetEnabled(LastMenuViewState.bSaveEnabled);
	}
	if (NativeExitGameButton)
	{
		NativeExitGameButton->SetEnabled(LastMenuViewState.bExitEnabled);
	}
	if (NativeStatusTextBlock)
	{
		NativeStatusTextBlock->SetText(LastMenuViewState.StatusText);
		NativeStatusTextBlock->SetVisibility(
			LastMenuViewState.StatusText.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible);
	}
}

// 原生设置点击流程：转交标准 Widget 意图；Controller 负责缺页反馈或正式设置入口。
FReply UCatLakeMainMenuWidget::HandleNativeSettingsClicked()
{
	RequestOpenSettings();
	return FReply::Handled();
}

// 原生保存点击流程：转交标准 Widget 意图；Save 子系统的受理结果会通过 RenderMenu 回显。
FReply UCatLakeMainMenuWidget::HandleNativeSaveClicked()
{
	RequestSave();
	return FReply::Handled();
}

// 原生退出点击流程：转交标准 Widget 意图；Online 子系统会复用离开前保存和回前台链路。
FReply UCatLakeMainMenuWidget::HandleNativeExitGameClicked()
{
	RequestExitGame();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
