#include "UI/Run/CatDayTransitionWidget.h"

#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"

// 渲染流程：
// 1. 接收 LocalPlayer 已算好的时间轴投影，把透明度限制在零到一，分别更新存在的遮罩与两段文本；缺少绑定时跳过该控件。
// 2. 阻断期将 WBP 设为可命中；本用户和 Slate 就绪且视图尚未持焦点时，保存原焦点、启用可聚焦并接管键盘路由。
// 3. 非阻断期改为穿透且文字全不透明；仅在自己仍持焦点时尝试归还，原目标失效或恢复失败则返回该用户游戏视口。
// 4. 非阻断时清空焦点弱引用并关闭可聚焦；用户或 Slate 未就绪则跳过焦点操作，不修改玩法操作锁或创建布局。
void UCatDayTransitionWidget::RenderTransition(const float BlackOpacity, const FText& Message, const bool bBlockPointer, const FText& SettlementDetails)
{
	const float ClampedOpacity = FMath::Clamp(BlackOpacity, 0.0f, 1.0f);
	if (BlackOverlay)
	{
		BlackOverlay->SetRenderOpacity(ClampedOpacity);
	}
	if (ResultText)
	{
		ResultText->SetText(Message);
		ResultText->SetRenderOpacity(bBlockPointer ? ClampedOpacity : 1.0f);
	}
	if (SettlementText)
	{
		SettlementText->SetText(SettlementDetails);
		SettlementText->SetRenderOpacity(bBlockPointer ? ClampedOpacity : 1.0f);
	}
	SetVisibility(bBlockPointer ? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible);
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	const TSharedPtr<FSlateUser> User = LocalPlayer ? LocalPlayer->GetSlateUser() : nullptr;
	if (User && FSlateApplication::IsInitialized())
	{
		FSlateApplication& Application = FSlateApplication::Get();
		if (bBlockPointer && !HasUserFocus(GetOwningPlayer()))
		{
			PreviousUserFocus = Application.GetUserFocusedWidget(User->GetUserIndex());
			SetIsFocusable(true);
			SetUserFocus(GetOwningPlayer());
		}
		else if (!bBlockPointer && HasUserFocus(GetOwningPlayer()))
		{
			const TSharedPtr<SWidget> Previous = PreviousUserFocus.Pin();
			if (!Previous || !Application.SetUserFocus(User->GetUserIndex(), Previous))
				Application.SetUserFocusToGameViewport(User->GetUserIndex());
		}
	}
	if (!bBlockPointer)
	{
		PreviousUserFocus.Reset();
		SetIsFocusable(false);
	}
}

// 键盘流程：锁定期消费按键，避免导航到下层按钮；非阻断的失败提示将按键交回正常路由。
FReply UCatDayTransitionWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	return GetVisibility() == ESlateVisibility::Visible ? FReply::Handled() : Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

// 指针按下：仅正式锁定可命中态消费事件；失败提示沿父类路由，不写入游戏输入模式。
FReply UCatDayTransitionWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return GetVisibility() == ESlateVisibility::Visible ? FReply::Handled() : Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

// 指针松开：当前可命中阻断态返回 Handled，避免释放事件下传；非阻断态委托父类路由，不修改输入模式。
FReply UCatDayTransitionWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return GetVisibility() == ESlateVisibility::Visible ? FReply::Handled() : Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

// 滚轮路由：黑幕持锁时终止事件传播；无锁时保留父类行为，不自行移动下层焦点。
FReply UCatDayTransitionWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return GetVisibility() == ESlateVisibility::Visible ? FReply::Handled() : Super::NativeOnMouseWheel(InGeometry, InMouseEvent);
}
