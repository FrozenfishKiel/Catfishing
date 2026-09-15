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
#include "ShopEconomy/CatFishBuyerActor.h"

// 构建流程：完成父页库存绑定后连接全部出售按钮，并依据当前鱼护和买家位置初始化正式 WBP 的显隐。
void UCatFishGuardInventoryWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuyerRefreshElapsedSeconds = 0.0f;
	if (SellAllFishButton) { SellAllFishButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleSellAllClicked); }
	RefreshSellAllAction();
}

// 销毁流程：解绑本页唯一容器级按钮并隐藏区域，再交给父页解除库存与通用 UI 生命周期。
void UCatFishGuardInventoryWidget::NativeDestruct()
{
	if (SellAllFishButton) { SellAllFishButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleSellAllClicked); }
	SetSellAllActionVisible(false);
	BuyerRefreshElapsedSeconds = 0.0f;
	Super::NativeDestruct();
}

// 刷新流程：父类先重建通用格子并使菜单上下文失效，再按最新鱼护内容重算整批报价。
void UCatFishGuardInventoryWidget::RefreshInventorySlots()
{
	Super::RefreshInventorySlots();
	RefreshSellAllAction();
}

// Tick 流程：先确认鱼护仍在地面，失地立即撤销外部库存；仍有效时每 0.1 秒才查询附近买家并更新全部出售资格。
void UCatFishGuardInventoryWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!ResolveGroundedGuard()) { SetSellAllActionVisible(false); RequestCloseInventory(); return; }
	BuyerRefreshElapsedSeconds += InDeltaTime;
	if (BuyerRefreshElapsedSeconds >= 0.1f) { BuyerRefreshElapsedSeconds = 0.0f; RefreshSellAllAction(); }
}

// 鱼护解析流程：显示库存必须由地面鱼护拥有且等于它的正式鱼库存，任一条件不符即拒绝容器级交易。
ACatFishGuardActor* UCatFishGuardInventoryWidget::ResolveGroundedGuard() const
{
	UCatInventoryComponent* Inventory = GetInventoryContext();
	ACatFishGuardActor* Guard = Inventory ? Cast<ACatFishGuardActor>(Inventory->GetOwner()) : nullptr;
	return IsValid(Guard) && !Guard->IsActorBeingDestroyed() && Guard->IsGrounded() && Guard->GetFishInventoryComponent() == Inventory ? Guard : nullptr;
}

// 刷新全部出售流程：先判定买家范围，再逐条检查每条鱼可报价并计算总价；任何异常都禁用整批交易，避免只卖部分鱼。
void UCatFishGuardInventoryWidget::RefreshSellAllAction()
{
	ACatFishGuardActor* Guard = ResolveGroundedGuard();
	ACatFishBuyerActor* Buyer = Guard ? ACatFishBuyerActor::FindAvailableBuyer(GetOwningPlayer(), Guard) : nullptr;
	bool bAllQuoted = Buyer != nullptr;
	int64 TotalPrice = 0; int32 FishCount = 0;
	if (Buyer && Guard)
	{
		for (const FCatInventoryEntry& Entry : Guard->GetFishInventoryComponent()->GetInventoryModel()->GetInventoryList())
		{
			if (!Entry.Instance && Entry.StackCount == 0) { continue; }
			UCatFishInventoryItemInstance* Fish = Cast<UCatFishInventoryItemInstance>(Entry.Instance); int32 Price = 0;
			if (!Fish || Entry.StackCount != 1 || !Fish->GetItemInstanceId().IsValid() || !Buyer->TryAppraiseFish(Fish, Price)) { bAllQuoted = false; continue; }
			++FishCount; TotalPrice += Price;
		}
	}
	bAllQuoted = bAllQuoted && FishCount > 0 && TotalPrice <= 16777216;
	SetSellAllActionVisible(Buyer != nullptr);
	if (SellAllFishPriceText) { SellAllFishPriceText->SetText(bAllQuoted ? FText::AsNumber(TotalPrice) : NSLOCTEXT("Catfishing", "FishSalePriceUnavailable", "--")); }
	if (SellAllFishButton) { SellAllFishButton->SetIsEnabled(!PendingCommandRequestId.IsValid() && bAllQuoted); }
}

// 显隐同步流程：区域存在时作为总开关，按钮与价格仍分别同步，保证旧 WBP 未接区域时不会遗留可点击入口。
void UCatFishGuardInventoryWidget::SetSellAllActionVisible(const bool bVisible)
{
	const ESlateVisibility ActionVisibility = bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	if (SellActionsPanel) { SellActionsPanel->SetVisibility(ActionVisibility); }
	if (SellAllFishButton) { SellAllFishButton->SetVisibility(ActionVisibility); if (!bVisible) { SellAllFishButton->SetIsEnabled(false); } }
	if (SellAllFishPriceText) { SellAllFishPriceText->SetVisibility(ActionVisibility); if (!bVisible) { SellAllFishPriceText->SetText(FText::GetEmpty()); } }
}

// 全部出售流程：遍历当前地面鱼护的正式库存并收集唯一 ID；发现异常条目时不提交部分集合。
void UCatFishGuardInventoryWidget::HandleSellAllClicked()
{
	if (PendingCommandRequestId.IsValid()) { return; }
	ACatFishGuardActor* Guard = ResolveGroundedGuard();
	UCatFishOnlyInventoryComponent* FishInventory = Guard ? Guard->GetFishInventoryComponent() : nullptr;
	if (!FishInventory) { return; }
	TArray<FGuid> FishInstanceIds;
	for (const FCatInventoryEntry& Entry : FishInventory->GetInventoryModel()->GetInventoryList())
	{
		if (!Entry.Instance && Entry.StackCount == 0) { continue; }
		UCatFishInventoryItemInstance* Fish = Cast<UCatFishInventoryItemInstance>(Entry.Instance);
		if (!Fish || Entry.StackCount != 1 || !Fish->GetItemInstanceId().IsValid()) { RefreshSellAllAction(); return; }
		FishInstanceIds.AddUnique(Fish->GetItemInstanceId());
	}
	if (!FishInstanceIds.IsEmpty()) { SubmitFishSale(Guard, FishInstanceIds); }
}

// 提交流程：提交瞬间重查买家、鱼护和每条实例报价，确认整批仍可成交后才登记 RequestId 并调用既有服务器批量交易 RPC。
void UCatFishGuardInventoryWidget::SubmitFishSale(ACatFishGuardActor* Guard, const TArray<FGuid>& FishInstanceIds)
{
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(GetOwningPlayer());
	ACatFishBuyerActor* Buyer = Guard ? ACatFishBuyerActor::FindAvailableBuyer(Controller, Guard) : nullptr;
	if (PendingCommandRequestId.IsValid() || !Controller || !Buyer || Guard != ResolveGroundedGuard() || FishInstanceIds.IsEmpty()) { RefreshSellAllAction(); return; }
	int64 TotalPrice = 0; const TArray<FCatInventoryEntry>& Entries = Guard->GetFishInventoryComponent()->GetInventoryModel()->GetInventoryList();
	for (const FGuid& FishId : FishInstanceIds)
	{
		const FCatInventoryEntry* Entry = Entries.FindByPredicate([&FishId](const FCatInventoryEntry& Candidate) { return Candidate.Instance && Candidate.Instance->GetItemInstanceId() == FishId; });
		UCatFishInventoryItemInstance* Fish = Entry ? Cast<UCatFishInventoryItemInstance>(Entry->Instance) : nullptr; int32 Price = 0;
		if (!Fish || Entry->StackCount != 1 || !Buyer->TryAppraiseFish(Fish, Price)) { RefreshSellAllAction(); return; }
		TotalPrice += Price;
	}
	if (TotalPrice > MAX_int32) { RefreshSellAllAction(); return; }
	const FGuid RequestId = FGuid::NewGuid(); BeginInventoryCommand(RequestId);
	Controller->ServerSellFishBatch(RequestId, Buyer, Guard, FishInstanceIds);
}
