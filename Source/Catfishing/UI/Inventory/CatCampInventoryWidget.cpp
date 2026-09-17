#include "UI/Inventory/CatCampInventoryWidget.h"
#include "UI/Collection/CatCollectionWidget.h"
#include "UI/Inventory/CatInventoryModel.h"
#include "Inventory/CatInventoryComponent.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Engine/Texture2D.h"

// 先建立库存格与原有请求监听，再绑定关闭动作；推荐由页面控制器在打开完成后推送。
void UCatCampInventoryWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (CloseInventoryButton) CloseInventoryButton->OnClicked.AddUniqueDynamic(this, &UCatInventoryWidget::RequestCloseInventory);
	RenderTracking(nullptr);
}

// 先解除本页按钮并隐藏推荐，再让父类解除库存和命令回执，重开时由当前 Model 恢复。
void UCatCampInventoryWidget::NativeDestruct()
{
	if (CloseInventoryButton) CloseInventoryButton->OnClicked.RemoveDynamic(this, &UCatInventoryWidget::RequestCloseInventory);
	RenderTracking(nullptr);
	Super::NativeDestruct();
}

// 父类完成格子重建后统计有实物的槽位；数量使用实际列表容量，不改变仓库的存放规则。
void UCatCampInventoryWidget::RefreshInventorySlots()
{
	Super::RefreshInventorySlots();
	auto* Inventory = GetInventoryContext();
	if (!CapacityText || !Inventory) return;
	const auto& Entries = Inventory->GetInventoryModel()->GetInventoryList();
	int32 Occupied = 0;
	for (const auto& Entry : Entries) if (Entry.Instance && Entry.StackCount > 0) ++Occupied;
	CapacityText->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), Occupied, Entries.Num())));
}

// 空或未解锁投影隐藏整个区域并清除旧图；有效投影更新鱼名并同步加载推荐软引用。
// 未配置或加载失败只隐藏对应图片，保留标题和空格；不写任何追踪状态。
void UCatCampInventoryWidget::RenderTracking(const FCatCollectionEntryView* Entry)
{
	const bool bShow = Entry && Entry->bRecordedUnlocked;
	if (TrackingPanel) TrackingPanel->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (TrackingNameText) TrackingNameText->SetText(bShow ? FText::Format(FText::FromString(TEXT("追踪目标：{0}")), Entry->DisplayName) : FText::GetEmpty());
	UTexture2D* Bait = bShow ? Entry->RecommendedBaitThumbnail.LoadSynchronous() : nullptr;
	UTexture2D* Chum = bShow ? Entry->RecommendedChumThumbnail.LoadSynchronous() : nullptr;
	if (RecommendedBaitImage)
	{
		RecommendedBaitImage->SetBrushFromTexture(Bait);
		RecommendedBaitImage->SetVisibility(Bait ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
	if (RecommendedChumImage)
	{
		RecommendedChumImage->SetBrushFromTexture(Chum);
		RecommendedChumImage->SetVisibility(Chum ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
}
