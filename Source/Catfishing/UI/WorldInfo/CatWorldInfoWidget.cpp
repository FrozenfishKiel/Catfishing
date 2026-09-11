#include "UI/WorldInfo/CatWorldInfoWidget.h"
#include "Components/Image.h"
#include "Components/ListView.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"

// 列表绑定：先转发列表项事件，再读取载荷；类型不符时使用空行覆盖旧文本和图标，负数或非有限进度收起，其余限制在零到一。
// 四个 BindWidget 控件由正式资产保证存在；图标和进度控件收起时，其外层固定尺寸容器仍由资产保留。
void UCatWorldInfoRowWidget::NativeOnListItemObjectSet(UObject* ListItemObject)
{
	IUserObjectListEntry::NativeOnListItemObjectSet(ListItemObject);
	const UCatWorldInfoListItem* Item = Cast<UCatWorldInfoListItem>(ListItemObject);
	const FCatWorldInfoRow Row = Item ? Item->Data : FCatWorldInfoRow();
	LabelText->SetText(Row.Label);
	ValueText->SetText(Row.Value);
	RowIcon->SetBrushFromTexture(Row.Icon);
	RowIcon->SetVisibility(Row.Icon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	const bool bProgress = FMath::IsFinite(Row.Progress) && Row.Progress >= 0;
	RowProgress->SetPercent(bProgress ? FMath::Clamp(Row.Progress, 0.0f, 1.0f) : 0);
	RowProgress->SetVisibility(bProgress ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

// 初始化：UMG 先建立资产控件，再禁用焦点和命中；信息牌不会截获原有 E 键或鼠标。
void UCatWorldInfoWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(false);
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

// 数据渲染：
// 1. 调用方已排除 Hidden 且正式绑定已就绪；Full 选择详情页，其余值选择摘要页，并覆盖该页标题、状态文本与显隐。
// 2. 详情接收全部行，摘要只保留 bSummary 行；为每行创建归本视图所有的 UObject，复制数据而不持有源 Actor。
// 3. 替换当前页的 ListView 载荷，让列表复用正式行模板；未激活页保留原载荷，切换到它时再完整覆盖。
void UCatWorldInfoWidget::RenderInfo_Implementation(const FCatWorldInfoViewData& Data, const ECatWorldInfoDetail Detail)
{
	const bool bFull = Detail == ECatWorldInfoDetail::Full;
	DetailSwitcher->SetActiveWidgetIndex(bFull ? 1 : 0);
	UTextBlock* Title = bFull ? TitleText : SummaryTitleText;
	UTextBlock* Status = bFull ? StatusText : SummaryStatusText;
	UListView* List = bFull ? InfoList : SummaryInfoList;
	Title->SetText(Data.Title);
	Status->SetText(Data.Status);
	Status->SetVisibility(Data.Status.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	TArray<UObject*> Items;
	for (const FCatWorldInfoRow& Row : Data.Rows)
	{
		if (!bFull && !Row.bSummary) continue;
		UCatWorldInfoListItem* Item = NewObject<UCatWorldInfoListItem>(this);
		Item->Data = Row;
		Items.Add(Item);
	}
	List->SetListItems(Items);
}
