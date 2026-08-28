#include "UI/HUD/CatHUDWidget.h"

#include "Components/TextBlock.h"
#include "Rendering/DrawElementTypes.h"

// HUD 渲染流程：缓存只读投影，复制 Designer 绑定文本，然后触发蓝图扩展点；不访问任何玩法对象或按钮逻辑。
void UCatHUDWidget::RenderHUD(const FCatHUDViewState& ViewState)
{
	LastHUDViewState = ViewState;
	BlueprintCatStatusText = ViewState.CatStatusText;
	BlueprintFishingFeedbackText = ViewState.FishingFeedbackText;
	if (CatStatusTextBlock)
	{
		CatStatusTextBlock->SetText(BlueprintCatStatusText);
	}
	if (FishingFeedbackTextBlock)
	{
		FishingFeedbackTextBlock->SetText(BlueprintFishingFeedbackText);
	}
	BP_RenderHUD(LastHUDViewState);
}

// 状态读取流程：返回最近 HUD 投影；调用者只能展示，不获得后端订阅或写入口。
const FCatHUDViewState& UCatHUDWidget::GetLastHUDViewState() const
{
	return LastHUDViewState;
}

// 准星绘制流程：先让 WBP 和子控件完成绘制，再在最终层用本 HUD 的局部中心画四条灰色短线。
// 本 Widget 只会由 LocalPlayer UI 子系统为本地 Controller 创建，不读取 NetMode 或 HasAuthority，远端客户端不会依赖服务器生成 UI。
int32 UCatHUDWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, const int32 LayerId,
	const FWidgetStyle& InWidgetStyle, const bool bParentEnabled) const
{
	const int32 MaxLayer = Super::NativePaint(
		Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	if (LocalSize.X <= 0.0f || LocalSize.Y <= 0.0f || CrosshairArmLength <= 0.0f || CrosshairThickness <= 0.0f)
	{
		return MaxLayer;
	}

	const FVector2D Center = LocalSize * 0.5f;
	const float Inner = FMath::Max(0.0f, CrosshairGap);
	const float Outer = Inner + CrosshairArmLength;
	const uint32 CrosshairLayer = static_cast<uint32>(MaxLayer + 1);
	const FPaintGeometry PaintGeometry = AllottedGeometry.ToPaintGeometry();

	auto DrawArm = [&](const FVector2D& Start, const FVector2D& End)
	{
		TArray<FVector2D> Points;
		Points.Reserve(2);
		Points.Add(Start);
		Points.Add(End);
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			CrosshairLayer,
			PaintGeometry,
			Points,
			ESlateDrawEffect::None,
			CrosshairColor,
			true,
			CrosshairThickness);
	};

	DrawArm(Center + FVector2D(-Outer, 0.0f), Center + FVector2D(-Inner, 0.0f));
	DrawArm(Center + FVector2D(Inner, 0.0f), Center + FVector2D(Outer, 0.0f));
	DrawArm(Center + FVector2D(0.0f, -Outer), Center + FVector2D(0.0f, -Inner));
	DrawArm(Center + FVector2D(0.0f, Inner), Center + FVector2D(0.0f, Outer));
	return static_cast<int32>(CrosshairLayer);
}
