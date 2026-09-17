#include "UI/Collection/CatCollectionWidget.h"
#include "UI/Collection/CatCollectionPageController.h"
#include "UI/Collection/CatFishCardWidget.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "UI/CatUISettings.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "InputCoreTypes.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"

// 投影更新流程：保存只读副本、清无效选择并夹限跨页索引，再重绘当前页，不提交任何追踪写入。
void UCatCollectionWidget::RenderCollection(const FCatCollectionViewState& ViewState)
{
	LastCollectionViewState = ViewState;
	// 新投影或重新打开时不沿用上一次操作错误；失败且没有广播时，提示仍保留供玩家重试。
	TrackingErrorText = FText::GetEmpty();
	if (!ViewState.Entries.ContainsByPredicate([this](const auto& E) { return E.ItemId == SelectedItemId && E.bRecordedUnlocked; })) SelectedItemId = 0;
	PageIndex = FMath::Clamp(PageIndex, 0, FMath::Max(0, (ViewState.Entries.Num() - 1) / 12));
	RefreshCards();
}

// 只读返回最近数据，便于正式 WBP 与检查工具核对同源展示。
const FCatCollectionViewState& UCatCollectionWidget::GetLastCollectionViewState() const { return LastCollectionViewState; }

// 关闭流程：控件不自行修改输入模式，交给已持有模态锁的页面控制器收口。
void UCatCollectionWidget::RequestCloseCollection()
{
	if (auto* Controller = ResolveCollectionPageController()) Controller->RequestCloseCollectionFromWidget();
}

// 页面构建流程：按固定书页设计尺寸布局，再整体按可用空间等比缩放；每页六卡，左右页之间保留装订区。
TSharedRef<SWidget> UCatCollectionWidget::RebuildWidget()
{
	FontAsset = LoadObject<UObject>(nullptr, TEXT("/Game/UI/Shop/F_CatShopChinese.F_CatShopChinese"));
	BookBrush.SetResourceObject(LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Collection/T_CollectionBook.T_CollectionBook")));
	BookBrush.ImageSize = FVector2D(1200, 870);
	BookBrush.DrawAs = ESlateBrushDrawType::Image;
	const FLinearColor Ink(0.18f, 0.12f, 0.07f);
	TSharedRef<SWidget> Result = SNew(SScaleBox).Stretch(EStretch::ScaleToFit).HAlign(HAlign_Center).VAlign(VAlign_Center)
	[SNew(SBox).WidthOverride(1200).HeightOverride(870)
		[SNew(SOverlay)
			+ SOverlay::Slot()[SNew(SImage).Image(&BookBrush)]
			+ SOverlay::Slot().Padding(80, 65, 80, 58)
			[SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 20)
				[SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Font(FSlateFontInfo(FontAsset, 36)).ColorAndOpacity(Ink)
						.Text_Lambda([this]() { return LastCollectionViewState.SummaryText; })]
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).OnClicked_Lambda([this]() { RequestCloseCollection(); return FReply::Handled(); })
						[SNew(STextBlock).Font(FSlateFontInfo(FontAsset, 16)).Text(FText::FromString(TEXT("关闭")))]]]
				+ SVerticalBox::Slot().FillHeight(1)[SAssignNew(Grid, SUniformGridPanel).SlotPadding(FMargin(7, 7)).MinDesiredSlotHeight(190)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)
				[SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).IsEnabled_Lambda([this]() { return PageIndex > 0; })
						.OnClicked_Lambda([this]() { PageIndex = FMath::Max(0, PageIndex - 1); SelectedItemId = 0; TrackingErrorText = FText::GetEmpty(); RefreshCards(); return FReply::Handled(); })
						[SNew(STextBlock).Font(FSlateFontInfo(FontAsset, 18)).Text(FText::FromString(TEXT("上一页")))]]
					+ SHorizontalBox::Slot().AutoWidth().Padding(20, 0)[SNew(SButton).IsEnabled_Lambda([this]() { return (PageIndex + 1) * 12 < LastCollectionViewState.Entries.Num(); })
						.OnClicked_Lambda([this]() { PageIndex = FMath::Min(PageIndex + 1, FMath::Max(0, (LastCollectionViewState.Entries.Num() - 1) / 12)); SelectedItemId = 0; TrackingErrorText = FText::GetEmpty(); RefreshCards(); return FReply::Handled(); })
						[SNew(STextBlock).Font(FSlateFontInfo(FontAsset, 18)).Text(FText::FromString(TEXT("下一页")))]]
					+ SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(10, 0)
					[SNew(STextBlock).Font(FSlateFontInfo(FontAsset, 16)).ColorAndOpacity(FLinearColor(0.55f, 0.12f, 0.06f))
						.Text_Lambda([this]() { return TrackingErrorText; })]
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).IsEnabled_Lambda([this]() { return LastCollectionViewState.bAvailable && SelectedItemId != 0; })
						.OnClicked_Lambda([this]()
						{
							// 点击仅提交意图；保存成功会同步广播并重读投影，不能在这里提前修改追踪编号或鱼卡样式。
							TrackingErrorText = FText::GetEmpty();
							if (!LastCollectionViewState.bAvailable || SelectedItemId == 0) return FReply::Handled();
							const bool bCancel = SelectedItemId == LastCollectionViewState.TrackedItemId;
							auto* Controller = ResolveCollectionPageController();
							if (!Controller || !Controller->RequestTrackFish(bCancel ? 0 : SelectedItemId))
								TrackingErrorText = FText::FromString(bCancel ? TEXT("取消追踪未保存，请重试") : TEXT("追踪未保存，请重试"));
							return FReply::Handled();
						})
						[SNew(STextBlock).Font(FSlateFontInfo(FontAsset, 20)).Text_Lambda([this]() { return FText::FromString(SelectedItemId && SelectedItemId == LastCollectionViewState.TrackedItemId ? TEXT("取消追踪") : TEXT("追踪")); })]]]
			]
		]
	];
	RefreshCards();
	return Result;
}

// 卡片更新流程：清除上一跨页，按左页两列三行、右页两列三行放置；中央两列的内边距给装订留白。
void UCatCollectionWidget::RefreshCards()
{
	if (!Grid) return;
	Grid->ClearChildren();
	Cards.Reset();
	// 末页空位仍占书页网格，防止少量鱼卡被拉满跨页；占位没有鱼种数据、画面或交互。
	if (LastCollectionViewState.Entries.Num() - PageIndex * 12 < 12)
		Grid->AddSlot(3, 2)[SNew(SSpacer)];
	for (int32 Index = PageIndex * 12; Index < FMath::Min((PageIndex + 1) * 12, LastCollectionViewState.Entries.Num()); ++Index)
	{
		UCatFishCardWidget* Card = GetOwningPlayer() ? CreateWidget<UCatFishCardWidget>(GetOwningPlayer()) : NewObject<UCatFishCardWidget>(this);
		// 编辑器预览没有 PlayerController，NewObject 后仍须走 UUserWidget 初始化以建立 WidgetTree。
		if (!Card) continue;
		Card->Initialize();
		const auto& Entry = LastCollectionViewState.Entries[Index];
		Card->RenderCard(Entry, LastCollectionViewState.bAvailable && Entry.ItemId == LastCollectionViewState.TrackedItemId);
		Card->OnSelected.BindUObject(this, &ThisClass::SelectFish);
		Cards.Add(Card);
		const int32 Local = Index - PageIndex * 12;
		const int32 Column = Local < 6 ? Local % 2 : 2 + (Local - 6) % 2;
		const int32 Row = Local < 6 ? Local / 2 : (Local - 6) / 2;
		Grid->AddSlot(Column, Row)[SNew(SBox).Padding(Column == 1 ? FMargin(0, 0, 20, 0) : Column == 2 ? FMargin(20, 0, 0, 0) : FMargin(0))[Card->TakeWidget()]];
	}
}

// 点击只接受已捕获卡，记录下一次按钮操作的目标并清除旧错误；不刷新鱼卡，不把待操作对象冒充追踪。
void UCatCollectionWidget::SelectFish(const int32 ItemId)
{
	const auto* Entry = LastCollectionViewState.Entries.FindByPredicate([ItemId](const auto& E) { return E.ItemId == ItemId; });
	if (!Entry || !Entry->bRecordedUnlocked) return;
	SelectedItemId = ItemId;
	TrackingErrorText = FText::GetEmpty();
}

// Slate 释放时清除网格和卡片，父类继续释放自身控件资源。
void UCatCollectionWidget::ReleaseSlateResources(const bool bReleaseChildren)
{
	Grid.Reset();
	Cards.Reset();
	Super::ReleaseSlateResources(bReleaseChildren);
}

// 在子控件消费之前处理关闭键，避免焦点落在列表控件上后无法关闭整页。
FReply UCatCollectionWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (ShouldCloseCollectionFromKey(InKeyEvent))
	{
		RequestCloseCollection();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

// 根控件直接收到按键时复用同一关闭判断；其余输入保持默认传播。
FReply UCatCollectionWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (ShouldCloseCollectionFromKey(InKeyEvent))
	{
		RequestCloseCollection();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

// 控制器解析流程：按本地玩家取得已装配的图鉴控制器，不另建 Model。
UCatCollectionPageController* UCatCollectionWidget::ResolveCollectionPageController() const
{
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	UCatLocalPlayerUISubsystem* UI = LocalPlayer ? LocalPlayer->GetSubsystem<UCatLocalPlayerUISubsystem>() : nullptr;
	return UI ? UI->GetCollectionPageController() : nullptr;
}

// 关闭条件读取页面控制器的唯一打开状态；接受 Escape 与从正式 IMC 解析出的图鉴键，不硬编码 M。
bool UCatCollectionWidget::ShouldCloseCollectionFromKey(const FKeyEvent& InKeyEvent) const
{
	const UCatCollectionPageController* Controller = ResolveCollectionPageController();
	if (!Controller || !Controller->IsCollectionOpen())
	{
		return false;
	}
	const FName Key = InKeyEvent.GetKey().GetFName();
	const UCatUISettings* Settings = GetDefault<UCatUISettings>();
	return Key == EKeys::Escape.GetFName() || Key == Settings->ResolveCollectionToggleKeyName();
}
