#include "UI/Collection/CatFishRevealWidget.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

// 揭示渲染流程：缓存投影后按 Designer 真实绑定的控件逐项写入；没有绑定的控件跳过，不在原生层造替身布局。
// 图片没配时折叠而不是留一个空框——「没有的信息不存在，不留待解锁空位」是图鉴册 2026-09-08 的口径。
void UCatFishRevealWidget::RenderReveal(const FCatFishRevealViewData& ViewData)
{
	LastRevealViewData = ViewData;
	if (FishNameTextBlock)
	{
		FishNameTextBlock->SetText(ViewData.NameText);
	}
	if (FishDescriptionTextBlock)
	{
		FishDescriptionTextBlock->SetText(ViewData.DescriptionText);
		FishDescriptionTextBlock->SetVisibility(ViewData.DescriptionText.IsEmpty()
			? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (FishWeightTextBlock)
	{
		FishWeightTextBlock->SetText(ViewData.WeightText);
		FishWeightTextBlock->SetVisibility(ViewData.WeightText.IsEmpty()
			? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (ContinueHintTextBlock)
	{
		ContinueHintTextBlock->SetText(ViewData.ContinueHintText);
	}
	if (FishPortraitImage)
	{
		if (ViewData.Portrait)
		{
			FishPortraitImage->SetBrushFromTexture(ViewData.Portrait);
			FishPortraitImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			FishPortraitImage->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	BP_RenderReveal(ViewData);
}

// 投影读取流程：返回最近一次揭示内容；调用方只能显示，拿不到鱼定义或 Profile 写口。
const FCatFishRevealViewData& UCatFishRevealWidget::GetLastRevealViewData() const
{
	return LastRevealViewData;
}

// 收起流程：只广播一次意图；真正移出视口由 LocalPlayer UI 协调层做，避免 Widget 自己决定自己的生命周期。
void UCatFishRevealWidget::RequestDismiss()
{
	OnDismissRequested.Broadcast();
}

// 构造流程：**刻意不抢键盘焦点**。
// 主界面参考稿逐字写「你仍可移动，交互和继续钓鱼」；在 GameOnly 输入模式下把 Slate 焦点收到 UMG 上，
// 玩法输入就收不到按键了——那正好违反这句话。所以这一层只是画在屏幕上，不碰输入模式、不显示鼠标、不锁移动。
// 代价是「Space 继续」目前只有在 WBP 自己把焦点给本控件时才生效；正式做法是给它一个 InputAction，
// 那是资产，本轮不碰。为了不让浮层卡死在屏幕上，收起由 LocalPlayer UI 协调层的有界计时兜底。
void UCatFishRevealWidget::NativeConstruct()
{
	Super::NativeConstruct();
}

// 按键流程：只接 Space 与 Enter 两个「继续」键，其余一律 Unhandled 交还给玩法输入。
// 本控件不主动抢焦点，所以这条路只在 WBP 显式把焦点交给它时才走得到。
FReply UCatFishRevealWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey PressedKey = InKeyEvent.GetKey();
	if (PressedKey == EKeys::SpaceBar || PressedKey == EKeys::Enter)
	{
		RequestDismiss();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}
