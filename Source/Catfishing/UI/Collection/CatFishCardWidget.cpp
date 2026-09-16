#include "UI/Collection/CatFishCardWidget.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

/** 鱼卡的只读追踪外观桥；引擎把常驻按下属性设为 protected，需在按钮内部接入，不能伪造鼠标按住事件。 */
class SCatTrackedFishButton final : public SButton
{
public:
	/** 先构造标准按钮及点击处理，再绑定来自已保存投影的按下外观；不修改真正的输入按下状态。 */
	void Construct(const FArguments& Args, TAttribute<bool> Tracked)
	{
		SButton::Construct(Args);
		SetAppearPressed(MoveTemp(Tracked));
	}
};

// 更新流程：保存投影与已确认追踪态，只加载本鱼图片；缺图保留空画刷，不拿其他鱼占位，锁定时由绘制层压暗。
void UCatFishCardWidget::RenderCard(const FCatCollectionEntryView& Entry, const bool bTracked)
{
	Data = Entry;
	bIsTracked = bTracked && Entry.bRecordedUnlocked;
	UTexture2D* Texture = Data.Thumbnail.LoadSynchronous();
	FishBrush.SetResourceObject(Texture);
	FishBrush.DrawAs = ESlateBrushDrawType::Image;
	// ScaleBox 以画刷尺寸判断原图比例；不能用卡槽的宽高冒充源图尺寸，否则方图会把鱼压扁。
	// 缺图时图片层隐藏，单位尺寸只用于保持有效布局输入，不加载其他鱼图补位。
	FishBrush.ImageSize = Texture ? FVector2D(FMath::Max(1, Texture->GetSizeX()), FMath::Max(1, Texture->GetSizeY())) : FVector2D(1, 1);
}

// 构建流程：按钮只对已捕获且绑定了选择回调的卡生效；背景、鱼图和文字分别绘制。
// 按当前获准表现，锁定鱼直接压暗自己的现有图片作占位，不声称白底纹理已经成为透明剪影；文字仍只取问号。
TSharedRef<SWidget> UCatFishCardWidget::RebuildWidget()
{
	FontAsset = LoadObject<UObject>(nullptr, TEXT("/Game/UI/Shop/F_CatShopChinese.F_CatShopChinese"));
	FrameBrush = FSlateRoundedBoxBrush(FLinearColor(0.91f, 0.81f, 0.60f, 0.34f), 5.0f,
		FLinearColor(0.48f, 0.33f, 0.17f, 0.7f), 1.0f);
	const FLinearColor Ink(0.18f, 0.12f, 0.07f);
	TSharedRef<SButton> Button = SNew(SCatTrackedFishButton, TAttribute<bool>::CreateLambda([this]() { return bIsTracked; }))
		.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
		.ContentPadding(0)
		.PressedPaddingOverride(FMargin(2))
		.OnClicked_Lambda([this]() { if (Data.bRecordedUnlocked) OnSelected.ExecuteIfBound(Data.ItemId); return FReply::Handled(); })
		[
			SNew(SBorder).BorderImage(&FrameBrush).Padding(12)
			.BorderBackgroundColor_Lambda([this]() { return bIsTracked ? FLinearColor(1.0f, 0.70f, 0.28f) : IsHovered() ? FLinearColor(1.0f, 0.93f, 0.78f) : FLinearColor::White; })
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[SNew(STextBlock).Font(FSlateFontInfo(FontAsset, 11)).ColorAndOpacity(Ink).Text(FText::FromString(TEXT("追踪中")))
						.Visibility_Lambda([this]() { return bIsTracked ? EVisibility::HitTestInvisible : EVisibility::Hidden; })]
					+ SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Center)
					[SNew(STextBlock).Font(FSlateFontInfo(FontAsset, 21)).ColorAndOpacity(Ink).Text_Lambda([this]() { return Data.DisplayName; })]]
				+ SVerticalBox::Slot().FillHeight(1).Padding(0, 5)
				[SNew(SOverlay)
					+ SOverlay::Slot()[SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[SNew(SImage).Image(&FishBrush)
						.Visibility_Lambda([this]() { return FishBrush.GetResourceObject() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
						.ColorAndOpacity_Lambda([this]() { return Data.bRecordedUnlocked ? FLinearColor::White : FLinearColor(0.14f, 0.12f, 0.09f, 0.8f); })]]
					+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
					[SNew(STextBlock).Font(FSlateFontInfo(FontAsset, 14)).ColorAndOpacity(Ink)
						.Text(FText::FromString(TEXT("鱼图缺失")))
						.Visibility_Lambda([this]() { return FishBrush.GetResourceObject() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })]]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)
				[SNew(STextBlock).Font(FSlateFontInfo(FontAsset, 14)).ColorAndOpacity(Ink).AutoWrapText(true)
					.Text_Lambda([this]() { return FText::Format(FText::FromString(TEXT("鱼饵偏好：{0}")), Data.BaitPreferenceText); })]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)
				[SNew(STextBlock).Font(FSlateFontInfo(FontAsset, 14)).ColorAndOpacity(Ink).AutoWrapText(true)
					.Text_Lambda([this]() { return FText::Format(FText::FromString(TEXT("窝料偏好：{0}")), Data.ChumPreferenceText); })]
			]
		];
	return Button;
}
