#include "UI/Collection/CatCollectionWidget.h"

#include "Components/TextBlock.h"

// 图鉴渲染流程：缓存只读投影并同步摘要和简单列表文本；复杂列表控件仍可由 WBP 根据 Entries 表现。
void UCatCollectionWidget::RenderCollection(const FCatCollectionViewState& ViewState)
{
	LastCollectionViewState = ViewState;
	BlueprintSummaryText = ViewState.SummaryText;
	TArray<FString> Lines;
	Lines.Reserve(ViewState.Entries.Num());
	for (const FCatCollectionEntryView& Entry : ViewState.Entries)
	{
		Lines.Add(Entry.DisplayText.ToString());
	}
	BlueprintEntriesText = FText::FromString(FString::Join(Lines, TEXT("\n")));
	if (SummaryTextBlock)
	{
		SummaryTextBlock->SetText(BlueprintSummaryText);
	}
	if (EntriesTextBlock)
	{
		EntriesTextBlock->SetText(BlueprintEntriesText);
	}
	BP_RenderCollection(LastCollectionViewState);
}

// 状态读取流程：返回最近图鉴投影；调用者不能通过它修改 Profile 或实物鱼容器。
const FCatCollectionViewState& UCatCollectionWidget::GetLastCollectionViewState() const
{
	return LastCollectionViewState;
}
