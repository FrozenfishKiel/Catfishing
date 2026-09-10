#include "UI/ItemTooltip/CatItemTooltipWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"

// 初次建立 Slate 前先继承 UMG 初始化，再归零透明度和显示方向；正式布局缺失由 WBP 编译合同报告。
void UCatItemTooltipWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	HideTooltip(true);
}

// 逐项比较文本后更新变化值，避免活动实例轮询不断使布局失效；图片继续使用 WBP 配置的尺寸。
// 实例信息为空时收起该行；不会修改动画目标或重新定位。
void UCatItemTooltipWidget::RenderItem(const FCatItemTooltipViewData& Data)
{
	if (ItemNameText && !ItemNameText->GetText().EqualTo(Data.Name)) ItemNameText->SetText(Data.Name);
	if (ItemDescriptionText && !ItemDescriptionText->GetText().EqualTo(Data.Description)) ItemDescriptionText->SetText(Data.Description);
	if (InstanceDetailsText)
	{
		if (!InstanceDetailsText->GetText().EqualTo(Data.InstanceDetails)) InstanceDetailsText->SetText(Data.InstanceDetails);
		InstanceDetailsText->SetVisibility(Data.InstanceDetails.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (ItemIconImage)
	{
		if (ItemIconImage->GetBrush().GetResourceObject() != Data.Icon) ItemIconImage->SetBrushFromTexture(Data.Icon, false);
		ItemIconImage->SetVisibility(Data.Icon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

// 先打开不可命中的显示层，按玩家屏幕几何转换来源格中心，再从当前透明度接续淡入。
// 不追踪鼠标坐标；此行为与 Aegis 的 ShowHoverTooltip / ResolveTooltipCanvasPosition 一致。
void UCatItemTooltipWidget::ShowAt(const FVector2D& ScreenPosition)
{
	bWantsVisible = true;
	SetVisibility(ESlateVisibility::HitTestInvisible);
	ForceLayoutPrepass();
	if (RootBorder && GetOwningPlayer())
	{
		RootBorder->SetRenderTranslation(UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry(GetOwningPlayer()).AbsoluteToLocal(ScreenPosition));
	}
	if (FadeInDurationSeconds <= 0.0f) SetRenderOpacity(1.0f);
}

// 普通离开仅反转动画方向，保留最后一份文字供淡出；页面解绑则立即收起，防止下一页面看到旧物品。
void UCatItemTooltipWidget::HideTooltip(const bool bImmediate)
{
	bWantsVisible = false;
	if (bImmediate || FadeOutDurationSeconds <= 0.0f)
	{
		SetRenderOpacity(0.0f);
		SetVisibility(ESlateVisibility::Collapsed);
	}
}

// 依当前显示方向选择时长并线性趋近目标；透明度连续，所以旧淡出被新悬停接管时不会重新闪黑。
// 收起后 UMG 不再推进动画；下一次 ShowAt 会重新启用可见控件 Tick。
void UCatItemTooltipWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const float Duration = bWantsVisible ? FadeInDurationSeconds : FadeOutDurationSeconds;
	const float Target = bWantsVisible ? 1.0f : 0.0f;
	SetRenderOpacity(Duration > KINDA_SMALL_NUMBER ? FMath::FInterpConstantTo(GetRenderOpacity(), Target, InDeltaTime, 1.0f / Duration) : Target);
	if (!bWantsVisible && GetRenderOpacity() <= 0.0f) SetVisibility(ESlateVisibility::Collapsed);
}
