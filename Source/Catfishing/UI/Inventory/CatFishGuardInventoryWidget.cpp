#include "UI/Inventory/CatFishGuardInventoryWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "FishContainers/CatFishGuardActor.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Inventory/CatFishOnlyInventoryComponent.h"
#include "Inventory/CatInventoryComponent.h"
#include "UI/Inventory/CatInventoryModel.h"
#include "Logging/CatLog.h"
#include "ShopEconomy/CatFishBuyerActor.h"

// 选中格变化流程：先让父页更新共享选择和按钮状态，再按新条目重算鱼护专属单鱼出售按钮。
void UCatFishGuardInventoryWidget::RequestSelectSlot(const int32 SlotIndex)
{
	Super::RequestSelectSlot(SlotIndex);
	RefreshSellActions();
}

// 构建流程：先完成父页的库存绑定，再绑定可选出售按钮并根据当前鱼护和买家位置初始化正式 WBP 的可见性。
void UCatFishGuardInventoryWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuyerRefreshElapsedSeconds = 0.0f;
	if (SellFishButton) { SellFishButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleSellSelectedClicked); }
	if (SellAllFishButton) { SellAllFishButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleSellAllClicked); }
	RefreshSellActions();
}

// 销毁流程：先解绑本页创建的按钮回调，再交由父页解除库存和通用按钮；不持有买家或鱼护的订阅。
void UCatFishGuardInventoryWidget::NativeDestruct()
{
	if (SellFishButton) { SellFishButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleSellSelectedClicked); }
	if (SellAllFishButton) { SellAllFishButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleSellAllClicked); }
	SetSellActionsVisible(false);
	BuyerRefreshElapsedSeconds = 0.0f;
	Super::NativeDestruct();
}

// 库存刷新流程：父类先清除数量和失效选择并重建格子，再依据最新库存重算报价；不向 Model 写入交易等待状态。
void UCatFishGuardInventoryWidget::RefreshInventorySlots()
{
	Super::RefreshInventorySlots();
	RefreshSellActions();
}

// Tick 流程：每帧先确认鱼护没有被拾入库存，失地即关闭当前外部库存页；仍在地面时每 0.1 秒才查询一次附近买家并刷新出售入口。
void UCatFishGuardInventoryWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!ResolveGroundedGuard())
	{
		SetSellActionsVisible(false);
		RequestCloseInventory();
		return;
	}
	BuyerRefreshElapsedSeconds += InDeltaTime;
	if (BuyerRefreshElapsedSeconds >= 0.1f)
	{
		BuyerRefreshElapsedSeconds = 0.0f;
		RefreshSellActions();
	}
}

// 鱼护解析流程：显示库存必须由一个地面鱼护拥有，且要与该鱼护的正式鱼库存完全相同；任一条件不符即拒绝出售。
ACatFishGuardActor* UCatFishGuardInventoryWidget::ResolveGroundedGuard() const
{
	UCatInventoryComponent* Inventory = GetInventoryContext();
	ACatFishGuardActor* Guard = Inventory ? Cast<ACatFishGuardActor>(Inventory->GetOwner()) : nullptr;
	return IsValid(Guard) && !Guard->IsActorBeingDestroyed() && Guard->IsGrounded()
		&& Guard->GetFishInventoryComponent() == Inventory ? Guard : nullptr;
}

// 出售刷新流程：先按地面资格与双方距离决定显隐，再对当前选择及全部鱼调用同一逐鱼估价 API。
// 空鱼护禁用全部出售；坏报价或总价超出GAS精确整数范围时不显示部分总价，余额容量由服务器成交时另行复核。
void UCatFishGuardInventoryWidget::RefreshSellActions()
{
	ACatFishGuardActor* Guard = ResolveGroundedGuard();
	ACatFishBuyerActor* Buyer = Guard ? ACatFishBuyerActor::FindAvailableBuyer(GetOwningPlayer(), Guard) : nullptr;
	const bool bCanSellHere = Buyer != nullptr;
	SetSellActionsVisible(bCanSellHere);
	FCatInventoryEntry SelectedEntry;
	int32 QuotedSlotIndex = INDEX_NONE;
	UCatFishInventoryItemInstance* SelectedFish = GetSelectedInventoryEntry(SelectedEntry, QuotedSlotIndex)
		? Cast<UCatFishInventoryItemInstance>(SelectedEntry.Instance) : nullptr;
	int32 SelectedPrice = 0;
	const bool bSelectedQuoted = Buyer && SelectedFish && SelectedFish->GetItemInstanceId().IsValid()
		&& SelectedEntry.StackCount == 1 && Buyer->TryAppraiseFish(SelectedFish, SelectedPrice);
	int64 TotalPrice = 0;
	int32 FishCount = 0;
	bool bAllQuoted = bCanSellHere;
	if (Buyer && Guard)
	{
		for (const FCatInventoryEntry& Entry : Guard->GetFishInventoryComponent()->GetInventoryModel()->GetInventoryList())
		{
			if (!Entry.Instance && Entry.StackCount == 0) { continue; }
			UCatFishInventoryItemInstance* Fish = Cast<UCatFishInventoryItemInstance>(Entry.Instance);
			int32 Price = 0;
			if (!Fish || Entry.StackCount != 1 || !Fish->GetItemInstanceId().IsValid() || !Buyer->TryAppraiseFish(Fish, Price))
			{
				bAllQuoted = false;
				continue;
			}
			++FishCount;
			TotalPrice += Price;
		}
	}
	bAllQuoted = bAllQuoted && TotalPrice <= 16777216;
	const FText UnavailablePrice = NSLOCTEXT("Catfishing", "FishSalePriceUnavailable", "--");
	if (SellFishPriceText) { SellFishPriceText->SetText(bSelectedQuoted ? FText::AsNumber(SelectedPrice) : UnavailablePrice); }
	if (SellAllFishPriceText) { SellAllFishPriceText->SetText(bAllQuoted ? FText::AsNumber(TotalPrice) : UnavailablePrice); }
	if (SellFishButton) { SellFishButton->SetIsEnabled(!PendingCommandRequestId.IsValid() && bSelectedQuoted); }
	if (SellAllFishButton) { SellAllFishButton->SetIsEnabled(!PendingCommandRequestId.IsValid() && bAllQuoted && FishCount > 0); }
}

// 可见性更新流程：面板存在时作为正式布局总开关，两个按钮同时独立同步，确保未接面板的 WBP 不会遗留可点击出售入口。
void UCatFishGuardInventoryWidget::SetSellActionsVisible(const bool bVisible)
{
	const ESlateVisibility ActionVisibility = bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	if (SellActionsPanel) { SellActionsPanel->SetVisibility(ActionVisibility); }
	if (SellFishButton) { SellFishButton->SetVisibility(ActionVisibility); }
	if (SellAllFishButton) { SellAllFishButton->SetVisibility(ActionVisibility); }
	if (SellFishPriceText) { SellFishPriceText->SetVisibility(ActionVisibility); }
	if (SellAllFishPriceText) { SellAllFishPriceText->SetVisibility(ActionVisibility); }
	if (!bVisible)
	{
		if (SellFishButton) { SellFishButton->SetIsEnabled(false); }
		if (SellAllFishButton) { SellAllFishButton->SetIsEnabled(false); }
		if (SellFishPriceText) { SellFishPriceText->SetText(FText::GetEmpty()); }
		if (SellAllFishPriceText) { SellAllFishPriceText->SetText(FText::GetEmpty()); }
	}
}

// 单鱼出售流程：读取当前选中格的显示副本，固定其有效实例 ID，再交给统一批量提交；本地不会写价格或删除物品。
void UCatFishGuardInventoryWidget::HandleSellSelectedClicked()
{
	if (PendingCommandRequestId.IsValid()) { return; }
	FCatInventoryEntry SelectedEntry;
	int32 SaleSlotIndex = INDEX_NONE;
	UCatFishInventoryItemInstance* Fish = GetSelectedInventoryEntry(SelectedEntry, SaleSlotIndex)
		? Cast<UCatFishInventoryItemInstance>(SelectedEntry.Instance) : nullptr;
	ACatFishGuardActor* Guard = ResolveGroundedGuard();
	if (!Fish || !Fish->GetItemInstanceId().IsValid() || !Guard)
	{
		return;
	}
	TArray<FGuid> FishInstanceIds;
	FishInstanceIds.Add(Fish->GetItemInstanceId());
	SubmitFishSale(Guard, FishInstanceIds);
}

// 全部出售流程：等待回执期间不重发；遍历当前地面鱼护库存并收集唯一鱼 ID，异常非空条目使整批取消，空鱼护不提交。
void UCatFishGuardInventoryWidget::HandleSellAllClicked()
{
	if (PendingCommandRequestId.IsValid()) { return; }
	ACatFishGuardActor* Guard = ResolveGroundedGuard();
	UCatFishOnlyInventoryComponent* FishInventory = Guard ? Guard->GetFishInventoryComponent() : nullptr;
	if (!FishInventory)
	{
		return;
	}
	TArray<FGuid> FishInstanceIds;
	for (const FCatInventoryEntry& Entry : FishInventory->GetInventoryModel()->GetInventoryList())
	{
		if (!Entry.Instance && Entry.StackCount == 0) { continue; }
		UCatFishInventoryItemInstance* Fish = Cast<UCatFishInventoryItemInstance>(Entry.Instance);
		if (!Fish || Entry.StackCount != 1 || !Fish->GetItemInstanceId().IsValid())
		{
			RefreshSellActions();
			return;
		}
		FishInstanceIds.AddUnique(Fish->GetItemInstanceId());
	}
	if (!FishInstanceIds.IsEmpty())
	{
		SubmitFishSale(Guard, FishInstanceIds);
	}
}

// 售鱼提交流程：在提交瞬间重新确认买家和鱼护仍有效，再生成一个请求 ID 和固定鱼 ID 数组交给服务器；客户端不传售价。
void UCatFishGuardInventoryWidget::SubmitFishSale(ACatFishGuardActor* Guard, const TArray<FGuid>& FishInstanceIds)
{
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(GetOwningPlayer());
	ACatFishBuyerActor* Buyer = Guard ? ACatFishBuyerActor::FindAvailableBuyer(Controller, Guard) : nullptr;
	if (PendingCommandRequestId.IsValid() || !Controller || !Buyer || Guard != ResolveGroundedGuard() || FishInstanceIds.IsEmpty())
	{
		RefreshSellActions();
		return;
	}
	// 点击与低频刷新之间库存或配置仍可能变化；提交前逐 ID 复核整批报价，禁止把部分有效鱼偷偷当成全部出售。
	int64 TotalPrice = 0;
	const TArray<FCatInventoryEntry>& Entries = Guard->GetFishInventoryComponent()->GetInventoryModel()->GetInventoryList();
	for (const FGuid& FishId : FishInstanceIds)
	{
		const FCatInventoryEntry* Entry = Entries.FindByPredicate([&FishId](const FCatInventoryEntry& Candidate)
		{
			return Candidate.Instance && Candidate.Instance->GetItemInstanceId() == FishId;
		});
		UCatFishInventoryItemInstance* Fish = Entry ? Cast<UCatFishInventoryItemInstance>(Entry->Instance) : nullptr;
		int32 Price = 0;
		if (!Fish || Entry->StackCount != 1 || !Buyer->TryAppraiseFish(Fish, Price))
		{
			RefreshSellActions();
			return;
		}
		TotalPrice += Price;
	}
	if (TotalPrice > MAX_int32) { RefreshSellActions(); return; }
	const FGuid RequestId = FGuid::NewGuid();
	BeginInventoryCommand(RequestId);
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_fish_sale_submitted World=%s NetMode=%d Authority=%d LocalRole=%d Controller=%s RequestId=%s Buyer=%s Source=%s FishCount=%d PreviewPrice=%lld"),
		*GetPathNameSafe(GetWorld()), static_cast<int32>(Controller->GetNetMode()), Controller->HasAuthority(),
		static_cast<int32>(Controller->GetLocalRole()), *GetNameSafe(Controller), *RequestId.ToString(),
		*GetPathNameSafe(Buyer), *GetPathNameSafe(Guard), FishInstanceIds.Num(), TotalPrice);
	Controller->ServerSellFishBatch(RequestId, Buyer, Guard, FishInstanceIds);
}
