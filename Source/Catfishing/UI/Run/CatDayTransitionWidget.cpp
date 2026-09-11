#include "UI/Run/CatDayTransitionWidget.h"

#include "Styling/CoreStyle.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

// 构建流程：先铺纯黑背景，再居中放置可换行文字；初始透明且无文字，迟到快照不会先闪一帧黑。
TSharedRef<SWidget> UCatDayTransitionWidget::RebuildWidget()
{
	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SAssignNew(BlackOverlay, SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor::Transparent)
			// 消费黑底点击，避免视口在按下时抢走本过渡的键盘焦点。
			.OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })
		]
		// 文字获得视口内的完整可用宽度，再按文本对齐居中；按期望宽度居中会让自动换行把短标题挤成两行。
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Center).Padding(48.0f)
		[
			SAssignNew(ResultText, STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 36))
			.ColorAndOpacity(FLinearColor::White)
			.ShadowOffset(FVector2D(1.0f, 1.0f))
			.ShadowColorAndOpacity(FLinearColor::Black)
			.Justification(ETextJustify::Center)
			.AutoWrapText(true)
		];
}

// 渲染流程：限制黑底透明度并写文案；锁定时阻断点击、保存并接管键盘焦点，解除时只归还自己持有的焦点，失败反馈允许穿透。
void UCatDayTransitionWidget::RenderTransition(const float BlackOpacity, const FText& Message, const bool bBlockPointer)
{
	if (BlackOverlay)
	{
		BlackOverlay->SetBorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, FMath::Clamp(BlackOpacity, 0.0f, 1.0f)));
	}
	if (ResultText)
	{
		ResultText->SetText(Message);
		ResultText->SetRenderOpacity(bBlockPointer ? FMath::Clamp(BlackOpacity, 0.0f, 1.0f) : 1.0f);
	}
	SetVisibility(bBlockPointer ? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible);
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	const TSharedPtr<FSlateUser> User = LocalPlayer ? LocalPlayer->GetSlateUser() : nullptr;
	if (User && FSlateApplication::IsInitialized())
	{
		FSlateApplication& Slate = FSlateApplication::Get();
		if (bBlockPointer && !HasUserFocus(GetOwningPlayer()))
		{
			PreviousUserFocus = Slate.GetUserFocusedWidget(User->GetUserIndex());
			SetIsFocusable(true);
			SetUserFocus(GetOwningPlayer());
		}
		else if (!bBlockPointer && HasUserFocus(GetOwningPlayer()))
		{
			const TSharedPtr<SWidget> Previous = PreviousUserFocus.Pin();
			// 原页面可能已被关闭但 Slate 引用尚未失效；无法归还时回到所属玩家视口，不能让失败提示留住键盘。
			if (!Previous || !Slate.SetUserFocus(User->GetUserIndex(), Previous))
			{
				Slate.SetUserFocusToGameViewport(User->GetUserIndex());
			}
			PreviousUserFocus.Reset();
		}
	}
}

// 键盘流程：锁定期消费按键，避免导航到下层按钮；非阻断的失败提示将按键交回正常路由。
FReply UCatDayTransitionWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	return GetVisibility() == ESlateVisibility::Visible ? FReply::Handled() : Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

// 释放流程：先让 UMG 结束自身及子树资源，再丢弃本类的 Slate 引用；下次重建重新创建整棵树。
void UCatDayTransitionWidget::ReleaseSlateResources(const bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	BlackOverlay.Reset();
	ResultText.Reset();
}
