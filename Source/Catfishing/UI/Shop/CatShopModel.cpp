#include "UI/Shop/CatShopModel.h"

#include "Engine/World.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/PlayerController.h"
#include "ShopEconomy/CatShopInventoryComponent.h"

namespace
{
	// 公开货架查找流程：只读 GameState 已复制的权威库存快照，并限定到当前摊位库存 ID，避免不同摊位的同名 EntryId 串货。
	const FCatShopStockSnapshot* FindPublicStockSnapshot(const FCatShopPublicEconomySnapshot& Economy,
		const FGuid ShopInventoryId, const FName EntryId)
	{
		return Economy.Stocks.FindByPredicate([ShopInventoryId, EntryId](const FCatShopStockSnapshot& Stock)
		{
			return Stock.ShopInventoryId == ShopInventoryId && Stock.EntryId == EntryId;
		});
	}

	// 商品投影查找流程：在当前完整 Entries 中按 EntryId 找商品，购物车和结果提示都从同一份展示事实回读。
	const FCatShopEntryView* FindEntryView(const TArray<FCatShopEntryView>& Entries, const FName EntryId)
	{
		return Entries.FindByPredicate([EntryId](const FCatShopEntryView& Entry)
		{
			return Entry.EntryId == EntryId;
		});
	}
}

// 绑定流程：校验 Controller、GameState 和来源摊位库存，订阅商店公开快照与摊位身份复制通知；成功后刷新一次完整投影。
bool UCatShopModel::Bind(APlayerController* InController, UCatShopInventoryComponent* InShopInventory)
{
	Unbind();
	if (!InController || !InShopInventory)
	{
		return false;
	}
	ACatfishingGameState* GameState = InController->GetWorld()
		? InController->GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
	if (!GameState)
	{
		return false;
	}
	BoundPlayerController = InController;
	BoundGameState = GameState;
	BoundShopInventory = InShopInventory;
	ShopEconomyChangedHandle = GameState->OnShopEconomySnapshotChanged.AddUObject(
		this, &ThisClass::HandleShopEconomySnapshotChanged);
	ShopInventoryIdentityChangedHandle = InShopInventory->OnInventoryIdentityChanged.AddUObject(
		this, &ThisClass::HandleShopInventoryIdentityChanged);
	Refresh();
	return true;
}

// 解绑流程：从 GameState 和摊位库存移除订阅，清空 pending、购物车、弱引用和展示状态，避免跨 World 显示旧公款或旧选购队列。
void UCatShopModel::Unbind()
{
	if (ACatfishingGameState* GameState = BoundGameState.Get())
	{
		GameState->OnShopEconomySnapshotChanged.Remove(ShopEconomyChangedHandle);
	}
	if (UCatShopInventoryComponent* ShopInventory = BoundShopInventory.Get())
	{
		ShopInventory->OnInventoryIdentityChanged.Remove(ShopInventoryIdentityChangedHandle);
	}
	ShopEconomyChangedHandle.Reset();
	ShopInventoryIdentityChangedHandle.Reset();
	BoundPlayerController.Reset();
	BoundGameState.Reset();
	BoundShopInventory.Reset();
	bOpen = false;
	bActionPending = false;
	LastAction = ECatShopUIAction::None;
	LastEntryId = NAME_None;
	LastRejectedReason = FText();
	CartCountsByEntryId.Reset();
	CartEntryOrder.Reset();
	ViewState = FCatShopViewState();
}

// 打开状态流程：保存交互组件传来的打开状态并刷新投影；Model 不创建或移除 Widget。
void UCatShopModel::SetOpen(const bool bNewOpen)
{
	if (bOpen == bNewOpen)
	{
		return;
	}
	bOpen = bNewOpen;
	Refresh();
}

// 提交流程：记录最近动作，pending 状态会禁用继续加购、删除和支付，直到服务器结果明确成功或失败。
void UCatShopModel::MarkActionSubmitted(const ECatShopUIAction Action, const FName EntryId)
{
	LastAction = Action;
	LastEntryId = EntryId;
	LastRejectedReason = FText();
	bActionPending = true;
	Refresh();
}

// 拒绝回显流程：记录本地校验或服务器回包给出的失败原因，清掉 pending 并刷新结果文本；它只改 UI 投影，不写商店账本。
void UCatShopModel::MarkActionRejected(const ECatShopUIAction Action, const FName EntryId, const FText Reason)
{
	LastAction = Action;
	LastEntryId = EntryId;
	LastRejectedReason = Reason;
	bActionPending = false;
	Refresh();
}

// 支付成功流程：清空本地购物车和 pending 状态；服务器事实会继续通过公开经济快照与公共仓库快照各自同步。
void UCatShopModel::MarkCartPaymentSucceeded()
{
	LastAction = ECatShopUIAction::PayCart;
	LastEntryId = NAME_None;
	LastRejectedReason = FText();
	bActionPending = false;
	CartCountsByEntryId.Reset();
	CartEntryOrder.Reset();
	Refresh();
}

// 加购流程：
// 1. 从当前完整商品投影确认 EntryId 仍在货架、数据已同步且没有支付 pending。
// 2. 加购不检查团队公款，只检查购物车行数、单品次数和明显的库存上限；钱够不够统一由购物车支付按钮和服务器整车提交裁决。
// 3. 首次加入记录展示顺序，重复加入只增加本地次数，然后刷新右侧已选购列表。
bool UCatShopModel::AddEntryToCart(const FName EntryId, FText& OutFailureReason)
{
	OutFailureReason = FText();
	FCatShopEntryView Entry;
	if (EntryId.IsNone() || !ViewState.bOpen || !ViewState.bEconomyAvailable
		|| ViewState.bActionPending || !TryFindEntryView(EntryId, Entry))
	{
		OutFailureReason = FText::FromString(TEXT("商店：当前商品或公款数据未就绪"));
		return false;
	}
	if (!Entry.bStockAvailable || Entry.bSoldOut)
	{
		OutFailureReason = FText::FromString(TEXT("商店：这件商品已经售罄"));
		return false;
	}
	const int32 ExistingCount = CartCountsByEntryId.FindRef(EntryId);
	if (!CartCountsByEntryId.Contains(EntryId) && CartEntryOrder.Num() >= CatShopCartLimits::MaxCartLines)
	{
		OutFailureReason = FText::FromString(TEXT("商店：购物车商品种类已达到上限"));
		return false;
	}
	if (ExistingCount >= CatShopCartLimits::MaxCartCountPerEntry)
	{
		OutFailureReason = FText::FromString(TEXT("商店：购物车数量已达到单品上限"));
		return false;
	}
	if (!Entry.bUnlimitedStock && ExistingCount >= Entry.RemainingStock)
	{
		OutFailureReason = FText::FromString(TEXT("商店：购物车数量已达到当前库存"));
		return false;
	}
	if (!CartCountsByEntryId.Contains(EntryId))
	{
		CartEntryOrder.Add(EntryId);
	}
	CartCountsByEntryId.FindOrAdd(EntryId) = ExistingCount + 1;
	LastAction = ECatShopUIAction::AddEntryToCart;
	LastEntryId = EntryId;
	LastRejectedReason = FText();
	Refresh();
	return true;
}

// 删除购物车流程：只删除一份本地选购；该动作不访问服务器，删除到 0 时同时移除展示顺序里的 EntryId。
bool UCatShopModel::RemoveOneCartItem(const FName EntryId, FText& OutFailureReason)
{
	OutFailureReason = FText();
	if (ViewState.bActionPending)
	{
		OutFailureReason = FText::FromString(TEXT("正在支付，请稍候"));
		return false;
	}
	int32* Count = CartCountsByEntryId.Find(EntryId);
	if (EntryId.IsNone() || !Count || *Count <= 0)
	{
		OutFailureReason = FText::FromString(TEXT("商店：购物车里没有这件商品"));
		return false;
	}
	--(*Count);
	if (*Count <= 0)
	{
		CartCountsByEntryId.Remove(EntryId);
		CartEntryOrder.Remove(EntryId);
	}
	LastAction = ECatShopUIAction::RemoveCartEntry;
	LastEntryId = EntryId;
	LastRejectedReason = FText();
	Refresh();
	return true;
}

// 清空购物车流程：只清本地 EntryId/次数队列并刷新 ViewState；服务器货架、公款和公共仓库不受影响。
void UCatShopModel::ClearCart()
{
	CartCountsByEntryId.Reset();
	CartEntryOrder.Reset();
	Refresh();
}

// RPC 行构建流程：按本地购物车展示顺序导出有效 EntryId 和次数；超出输入上限时整车拒绝，服务端仍会再次合并、查价、查库存和查仓库容量。
bool UCatShopModel::BuildCartCommandLines(TArray<FCatShopCartLineCommand>& OutLines) const
{
	OutLines.Reset();
	for (const FName EntryId : CartEntryOrder)
	{
		const int32 Count = CartCountsByEntryId.FindRef(EntryId);
		if (EntryId.IsNone() || Count <= 0 || Count > CatShopCartLimits::MaxCartCountPerEntry
			|| OutLines.Num() >= CatShopCartLimits::MaxCartLines)
		{
			OutLines.Reset();
			return false;
		}
		FCatShopCartLineCommand& Line = OutLines.AddDefaulted_GetRef();
		Line.EntryId = EntryId;
		Line.CartCount = Count;
	}
	return !OutLines.IsEmpty();
}

// 刷新流程：读取 GameState 经济/货架快照和当前摊位库存组件的商品候选，生成只读 ViewState；购物车总价每次都从最新 Entries 重新推导。
void UCatShopModel::Refresh()
{
	FCatShopViewState NewState;
	NewState.bOpen = bOpen;
	NewState.LastAction = LastAction;
	NewState.LastEntryId = LastEntryId;
	NewState.bActionPending = bActionPending;
	if (const ACatfishingGameState* GameState = BoundGameState.Get())
	{
		NewState.Economy = GameState->GetShopEconomySnapshot();
		NewState.bEconomyAvailable = true;
	}

	const UCatShopInventoryComponent* ShopInventory = BoundShopInventory.Get();
	if (ShopInventory)
	{
		TArray<FCatShopCatalogEntry> DisplayCatalogEntries;
		ShopInventory->CollectDisplayCatalogEntries(DisplayCatalogEntries);
		NewState.Entries.Reserve(DisplayCatalogEntries.Num());
		for (const FCatShopCatalogEntry& Entry : DisplayCatalogEntries)
		{
			if (NewState.bEconomyAvailable
				&& !FindPublicStockSnapshot(NewState.Economy, ShopInventory->GetShopInventoryId(), Entry.EntryId))
			{
				continue;
			}
			FCatShopEntryView EntryView = MakeEntryView(Entry, NewState.Economy, NewState.bEconomyAvailable);
			EntryView.bActionEnabled = NewState.bOpen && NewState.bEconomyAvailable
				&& !NewState.bActionPending && EntryView.bStockAvailable && !EntryView.bSoldOut;
			NewState.Entries.Add(EntryView);
		}
	}
	BuildCartViewState(NewState);

	NewState.WalletText = NewState.bEconomyAvailable
		? FText::FromString(FString::Printf(TEXT("商店：团队公款 %d"), NewState.Economy.Balance))
		: FText::FromString(TEXT("商店：公款数据未同步"));

	if (!LastRejectedReason.IsEmpty())
	{
		NewState.ResultText = LastRejectedReason;
	}
	else if (NewState.bActionPending)
	{
		NewState.ResultText = FText::FromString(TEXT("已提交购物车，等待商店结果同步"));
	}
	else if (LastAction == ECatShopUIAction::PayCart)
	{
		NewState.ResultText = FText::FromString(TEXT("支付成功，请在营地公共仓库查看新物品"));
	}
	else if (LastAction == ECatShopUIAction::AddEntryToCart && !LastEntryId.IsNone())
	{
		const FCatShopEntryView* Entry = FindEntryView(NewState.Entries, LastEntryId);
		NewState.ResultText = FText::FromString(FString::Printf(TEXT("已选购：%s"),
			Entry ? *Entry->DisplayNameText.ToString() : *LastEntryId.ToString()));
	}
	else if (LastAction == ECatShopUIAction::RemoveCartEntry && !LastEntryId.IsNone())
	{
		NewState.ResultText = FText::FromString(TEXT("已从已选购中移除一份"));
	}
	else
	{
		NewState.ResultText = FText::FromString(TEXT("请选择商品加入已选购"));
	}

	ViewState = MoveTemp(NewState);
	OnViewStateChanged.Broadcast();
}

// 状态读取流程：返回最近商店投影；调用方不能通过它取得 GameState 或 Settings 指针。
const FCatShopViewState& UCatShopModel::GetViewState() const
{
	return ViewState;
}

// 条目查询流程：只在当前完整商品投影中查找商品行，避免点击过期蓝图行时绕过最新目录。
bool UCatShopModel::TryFindEntryView(const FName EntryId, FCatShopEntryView& OutEntry) const
{
	const FCatShopEntryView* FoundEntry = FindEntryView(ViewState.Entries, EntryId);
	if (!FoundEntry)
	{
		OutEntry = FCatShopEntryView();
		return false;
	}
	OutEntry = *FoundEntry;
	return true;
}

// GameState 变化流程：公开经济/货架事实已经刷新，重新推导商品、公款和购物车可支付性；pending 只由服务器结果回包关闭。
void UCatShopModel::HandleShopEconomySnapshotChanged()
{
	Refresh();
}

// 摊位身份变化流程：客户端收到服务器复制的 ShopInventoryId 后重建投影；pending 状态不在这里清，因为这不是订单结果。
void UCatShopModel::HandleShopInventoryIdentityChanged()
{
	Refresh();
}

// 商品投影流程：
// 1. 把 Catalog 展示字段和装备定义展示字段合成中文展示行，分类和图标覆盖直接来自策划表。
// 2. 使用公开货架库存读取有限库存剩余数，并用当前团队公款推导单品是否买得起；加购不受单品余额影响。
// 3. 这些结果只影响 UI 展示和明显无效点击；真正扣款、数量和公共仓库发货仍在服务器 ShopEconomy/OrderCoordinator。
FCatShopEntryView UCatShopModel::MakeEntryView(const FCatShopCatalogEntry& Entry,
	const FCatShopPublicEconomySnapshot& Economy, const bool bEconomyAvailable) const
{
	const UCatShopInventoryComponent* ShopInventory = BoundShopInventory.Get();
	const UCatEquipmentSettings* EquipmentSettings = GetDefault<UCatEquipmentSettings>();
	const UCatEquipmentDefinition* Definition =
		EquipmentSettings ? EquipmentSettings->FindRuntimeDefinition(Entry.DefinitionId) : nullptr;
	const FCatShopStockSnapshot* Stock = bEconomyAvailable && ShopInventory
		? FindPublicStockSnapshot(Economy, ShopInventory->GetShopInventoryId(), Entry.EntryId) : nullptr;
	FCatShopEntryView View;
	View.EntryId = Entry.EntryId;
	View.Kind = Entry.Kind;
	View.DefinitionId = Entry.DefinitionId;
	View.DisplayCategoryId = Entry.DisplayCategoryId;
	View.PurchaseQuantity = FMath::Max(1, Entry.PurchaseQuantity);
	View.UnitPrice = FMath::Max(0, Entry.UnitPrice);
	View.bStockAvailable = Stock != nullptr;
	View.bUnlimitedStock = Stock ? Stock->bUnlimitedStock : Entry.bUnlimitedStock;
	View.InitialStock = Stock ? FMath::Max(0, Stock->InitialStock) : Entry.InitialStock;
	View.RemainingStock = Stock ? FMath::Max(0, Stock->RemainingStock) : 0;
	View.bSoldOut = View.bStockAvailable && !View.bUnlimitedStock && View.RemainingStock <= 0;
	View.bAffordable = !bEconomyAvailable || View.UnitPrice <= 0 || Economy.Balance >= View.UnitPrice;
	View.CartCount = CartCountsByEntryId.FindRef(Entry.EntryId);
	View.IconOverride = Entry.IconOverride;
	View.DisplayNameText = !Entry.DisplayNameOverride.IsEmpty()
		? Entry.DisplayNameOverride
		: (Definition && !Definition->DisplayName.IsEmpty()
			? Definition->DisplayName
			: FText::FromName(Entry.DefinitionId.IsNone() ? Entry.EntryId : Entry.DefinitionId));
	View.DescriptionText = !Entry.DescriptionOverride.IsEmpty()
		? Entry.DescriptionOverride
		: (Definition ? Definition->Description : FText());
	const FString StockText = !View.bStockAvailable
		? FString(TEXT("库存：未同步"))
		: View.bUnlimitedStock
		? FString(TEXT("库存：不限"))
		: (View.bSoldOut ? FString(TEXT("库存：已售罄"))
			: FString::Printf(TEXT("库存：剩余 %d/%d"), View.RemainingStock, View.InitialStock));
	const FString PriceText = View.UnitPrice <= 0
		? FString(TEXT("免费"))
		: FString::Printf(TEXT("价格 %d"), View.UnitPrice);
	const FString QuantityText = View.PurchaseQuantity > 1
		? FString::Printf(TEXT("数量 x%d | "), View.PurchaseQuantity) : FString();
	const FString CartText = View.CartCount > 0
		? FString::Printf(TEXT(" | 已选 %d"), View.CartCount) : FString();
	View.DisplayText = FText::FromString(FString::Printf(TEXT("%s | %s%s | %s%s"),
		*View.DisplayNameText.ToString(),
		*QuantityText,
		*PriceText,
		*StockText,
		*CartText));
	View.ActionText = FText::FromString(TEXT("加入"));
	return View;
}

// 购物车投影流程：
// 1. 逐条按本地购物车顺序回读当前完整商品数组；货架刷新、库存变化或 DataTable 变化都会在这里重新计算可支付性。
// 2. 每行重新计算交付总量和小计；行数超限的尾部条目不生成展示行，但会标记购物车失效并禁用支付。
//    缺项、售罄、库存不足或整数溢出同样只标记失效，不尝试替玩家删行。
// 3. 支付按钮统一根据空车、pending、数据同步、购物车有效性和团队公款总额裁决；资金不足文案保持给 WBP 鼠标悬停使用。
void UCatShopModel::BuildCartViewState(FCatShopViewState& InOutState) const
{
	int64 TotalPrice = 0;
	bool bInvalidLine = false;
	InOutState.CartLines.Reset();
	InOutState.CartLines.Reserve(CartEntryOrder.Num());
	for (const FName EntryId : CartEntryOrder)
	{
		const int32 CartCount = CartCountsByEntryId.FindRef(EntryId);
		if (EntryId.IsNone() || CartCount <= 0)
		{
			continue;
		}
		if (InOutState.CartLines.Num() >= CatShopCartLimits::MaxCartLines)
		{
			bInvalidLine = true;
			continue;
		}
		const FCatShopEntryView* Entry = FindEntryView(InOutState.Entries, EntryId);
		FCatShopCartLineView& CartLine = InOutState.CartLines.AddDefaulted_GetRef();
		CartLine.EntryId = EntryId;
		CartLine.CartCount = CartCount;
		if (!Entry)
		{
			bInvalidLine = true;
			CartLine.DisplayNameText = FText::FromName(EntryId);
			CartLine.DisplayText = FText::FromString(FString::Printf(TEXT("%s x %d | 商品状态已变化"),
				*EntryId.ToString(), CartCount));
			continue;
		}
		CartLine.DefinitionId = Entry->DefinitionId;
		CartLine.DisplayCategoryId = Entry->DisplayCategoryId;
		CartLine.PurchaseQuantity = Entry->PurchaseQuantity;
		CartLine.UnitPrice = Entry->UnitPrice;
		CartLine.DisplayNameText = Entry->DisplayNameText;
		CartLine.IconOverride = Entry->IconOverride;
		const int64 DeliveryQuantity = static_cast<int64>(Entry->PurchaseQuantity) * CartCount;
		const int64 LineTotal = static_cast<int64>(Entry->UnitPrice) * CartCount;
		const bool bQuantityOverflow = DeliveryQuantity <= 0 || DeliveryQuantity > MAX_int32;
		const bool bPriceOverflow = LineTotal < 0 || LineTotal > MAX_int32 || TotalPrice > MAX_int32 - LineTotal;
		CartLine.DeliveryQuantity = bQuantityOverflow ? 0 : static_cast<int32>(DeliveryQuantity);
		CartLine.LineTotalPrice = bPriceOverflow ? 0 : static_cast<int32>(LineTotal);
		CartLine.bLineAvailable = Entry->bStockAvailable && !Entry->bSoldOut
			&& (Entry->bUnlimitedStock || CartCount <= Entry->RemainingStock)
			&& !bQuantityOverflow && !bPriceOverflow;
		if (!CartLine.bLineAvailable)
		{
			bInvalidLine = true;
		}
		else
		{
			TotalPrice += LineTotal;
		}
		const FString QuantityText = CartLine.DeliveryQuantity > CartCount
			? FString::Printf(TEXT("，到货 %d"), CartLine.DeliveryQuantity) : FString();
		CartLine.DisplayText = FText::FromString(FString::Printf(TEXT("%s x %d%s | 小计 %d"),
			*CartLine.DisplayNameText.ToString(), CartCount, *QuantityText, CartLine.LineTotalPrice));
	}
	InOutState.CartTotalPrice = TotalPrice > MAX_int32 ? MAX_int32 : static_cast<int32>(TotalPrice);
	InOutState.bCartHasInvalidLines = bInvalidLine;
	InOutState.CartTotalText = FText::FromString(FString::Printf(TEXT("总计：%d"), InOutState.CartTotalPrice));
	InOutState.PayButtonText = FText::FromString(TEXT("支付"));
	const bool bHasCartLines = !InOutState.CartLines.IsEmpty();
	const bool bFundsEnough = InOutState.bEconomyAvailable
		&& InOutState.Economy.Balance >= InOutState.CartTotalPrice;
	InOutState.bCanPayCart = InOutState.bOpen && InOutState.bEconomyAvailable && !InOutState.bActionPending
		&& bHasCartLines && !InOutState.bCartHasInvalidLines && bFundsEnough;
	if (InOutState.bCanPayCart)
	{
		InOutState.PayDisabledReasonText = FText();
	}
	else if (InOutState.bActionPending)
	{
		InOutState.PayDisabledReasonText = FText::FromString(TEXT("正在支付，请稍候"));
	}
	else if (!InOutState.bEconomyAvailable)
	{
		InOutState.PayDisabledReasonText = FText::FromString(TEXT("商店数据未同步"));
	}
	else if (!bHasCartLines)
	{
		InOutState.PayDisabledReasonText = FText::FromString(TEXT("请先选购商品"));
	}
	else if (InOutState.bCartHasInvalidLines)
	{
		InOutState.PayDisabledReasonText = FText::FromString(TEXT("购物车商品状态已变化"));
	}
	else if (!bFundsEnough)
	{
		InOutState.PayDisabledReasonText = FText::FromString(TEXT("资金不足，无法购买！"));
	}
}
