#include "UI/Shop/CatShopWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"

// 构造流程：绑定可选 Designer 按钮；动态商品列表可以不放这些按钮，直接在蓝图商品行调用参数化请求函数。
void UCatShopWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (CloseButton)
	{
		CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCloseClicked);
		CloseButton->OnClicked.AddDynamic(this, &ThisClass::HandleCloseClicked);
	}
	if (PurchaseShopRodT2Button)
	{
		PurchaseShopRodT2Button->OnClicked.RemoveDynamic(this, &ThisClass::HandlePurchaseRodClicked);
		PurchaseShopRodT2Button->OnClicked.AddDynamic(this, &ThisClass::HandlePurchaseRodClicked);
	}
	if (PurchaseBugChumButton)
	{
		PurchaseBugChumButton->OnClicked.RemoveDynamic(this, &ThisClass::HandlePurchaseChumClicked);
		PurchaseBugChumButton->OnClicked.AddDynamic(this, &ThisClass::HandlePurchaseChumClicked);
	}
	if (ClaimFreeBugBaitButton)
	{
		ClaimFreeBugBaitButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleClaimBaitClicked);
		ClaimFreeBugBaitButton->OnClicked.AddDynamic(this, &ThisClass::HandleClaimBaitClicked);
	}
	if (ClaimFreeStarterRodButton)
	{
		ClaimFreeStarterRodButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleClaimStarterRodClicked);
		ClaimFreeStarterRodButton->OnClicked.AddDynamic(this, &ThisClass::HandleClaimStarterRodClicked);
	}
}

// 析构流程：
// 1. 解除本 View 绑定到可选按钮上的 UMG 点击事件，防止再次入视口时叠加回调。
// 2. 保留关闭和商品动作意图订阅；RemoveFromParent 只是页面离开视口，PageController::Unbind 才是外部订阅的结束点。
void UCatShopWidget::NativeDestruct()
{
	if (CloseButton)
	{
		CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCloseClicked);
	}
	if (PurchaseShopRodT2Button)
	{
		PurchaseShopRodT2Button->OnClicked.RemoveDynamic(this, &ThisClass::HandlePurchaseRodClicked);
	}
	if (PurchaseBugChumButton)
	{
		PurchaseBugChumButton->OnClicked.RemoveDynamic(this, &ThisClass::HandlePurchaseChumClicked);
	}
	if (ClaimFreeBugBaitButton)
	{
		ClaimFreeBugBaitButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleClaimBaitClicked);
	}
	if (ClaimFreeStarterRodButton)
	{
		ClaimFreeStarterRodButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleClaimStarterRodClicked);
	}
	Super::NativeDestruct();
}

// 渲染流程：缓存只读投影，复制 Designer 绑定字段，然后给蓝图扩展点一次完整状态。
void UCatShopWidget::RenderShop(const FCatShopViewState& ViewState)
{
	LastShopViewState = ViewState;
	BlueprintWalletText = ViewState.WalletText;
	BlueprintResultText = ViewState.ResultText;
	BlueprintEntries = ViewState.Entries;
	TArray<FString> Lines;
	Lines.Reserve(ViewState.Entries.Num());
	for (const FCatShopEntryView& Entry : ViewState.Entries)
	{
		Lines.Add(Entry.DisplayText.ToString());
	}
	BlueprintEntriesText = FText::FromString(FString::Join(Lines, TEXT("\n")));
	if (WalletTextBlock)
	{
		WalletTextBlock->SetText(BlueprintWalletText);
	}
	if (ResultTextBlock)
	{
		ResultTextBlock->SetText(BlueprintResultText);
	}
	if (EntriesTextBlock)
	{
		EntriesTextBlock->SetText(BlueprintEntriesText);
	}
	const bool bActionEnabled = ViewState.bOpen && ViewState.bEconomyAvailable && !ViewState.bActionPending;
	if (PurchaseShopRodT2Button)
	{
		PurchaseShopRodT2Button->SetIsEnabled(bActionEnabled);
	}
	if (PurchaseBugChumButton)
	{
		PurchaseBugChumButton->SetIsEnabled(bActionEnabled);
	}
	if (ClaimFreeBugBaitButton)
	{
		ClaimFreeBugBaitButton->SetIsEnabled(bActionEnabled);
	}
	if (ClaimFreeStarterRodButton)
	{
		ClaimFreeStarterRodButton->SetIsEnabled(bActionEnabled);
	}
	BP_RenderShop(LastShopViewState);
}

// 状态读取流程：返回最近商店投影；调用者不能通过它取得公款或商品写口。
const FCatShopViewState& UCatShopWidget::GetLastShopViewState() const
{
	return LastShopViewState;
}

// 购买请求流程：只允许有效 EntryId 出口；价格、库存、钱包版本由 PageController 和服务器后端补齐。
void UCatShopWidget::RequestPurchaseEntry(const FName EntryId)
{
	if (EntryId.IsNone())
	{
		return;
	}
	OnEntryActionRequested.Broadcast(EntryId, ECatShopUIAction::PurchaseEntry);
}

// 免费领取流程：只允许有效 EntryId 出口；是否真免费由服务器 ShopEconomy 白名单裁决。
void UCatShopWidget::RequestFreeClaimEntry(const FName EntryId)
{
	if (EntryId.IsNone())
	{
		return;
	}
	OnEntryActionRequested.Broadcast(EntryId, ECatShopUIAction::ClaimFreeEntry);
}

// 关闭请求流程：只广播关闭意图，输入模式和 Widget 生命周期由 PageController/交互组件处理。
void UCatShopWidget::RequestCloseShop()
{
	OnCloseRequested.Broadcast();
}

// 默认按钮流程：二级竿按钮只发固定目录 ID 的购买意图，仍不携带价格或库存。
void UCatShopWidget::HandlePurchaseRodClicked()
{
	RequestPurchaseEntry(TEXT("ShopRodT2Order"));
}

// 默认按钮流程：窝料按钮只发固定目录 ID 的购买意图，仍不携带价格或库存。
void UCatShopWidget::HandlePurchaseChumClicked()
{
	RequestPurchaseEntry(TEXT("ShopBugChumOrder"));
}

// 默认按钮流程：免费鱼饵按钮只发固定目录 ID 的领取意图，服务器会验证它是否属于免费条目。
void UCatShopWidget::HandleClaimBaitClicked()
{
	RequestFreeClaimEntry(TEXT("FreeBugBaitClaim"));
}

// 默认按钮流程：保底竿按钮只发固定目录 ID 的领取意图，服务器会验证它是否属于免费条目。
void UCatShopWidget::HandleClaimStarterRodClicked()
{
	RequestFreeClaimEntry(TEXT("FreeStarterRodClaim"));
}

// 默认关闭按钮流程：复用统一关闭请求入口，避免按钮和蓝图形成两条关闭路径。
void UCatShopWidget::HandleCloseClicked()
{
	RequestCloseShop();
}
