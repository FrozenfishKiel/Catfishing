#include "UI/Shop/CatShopModel.h"

#include "Engine/World.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/PlayerController.h"
#include "ShopEconomy/CatShopEconomySettings.h"

namespace
{
	// 公开货架查找流程：只读 GameState 已复制的权威库存快照，UI 不再从交易记录反推剩余库存。
	const FCatShopStockSnapshot* FindPublicStockSnapshot(const FCatShopPublicEconomySnapshot& Economy,
		const FName EntryId)
	{
		return Economy.Stocks.FindByPredicate([EntryId](const FCatShopStockSnapshot& Stock)
		{
			return Stock.EntryId == EntryId;
		});
	}
}

// 绑定流程：校验 Controller 和 GameState，订阅商店公开快照；成功后刷新一次完整投影。
bool UCatShopModel::Bind(APlayerController* InController)
{
	Unbind();
	if (!InController)
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
	ShopEconomyChangedHandle = GameState->OnShopEconomySnapshotChanged.AddUObject(
		this, &ThisClass::HandleShopEconomySnapshotChanged);
	Refresh();
	return true;
}

// 解绑流程：从 GameState 移除公开快照订阅，清空 pending、弱引用和展示状态，避免跨 World 显示旧公款。
void UCatShopModel::Unbind()
{
	if (ACatfishingGameState* GameState = BoundGameState.Get())
	{
		GameState->OnShopEconomySnapshotChanged.Remove(ShopEconomyChangedHandle);
	}
	ShopEconomyChangedHandle.Reset();
	BoundPlayerController.Reset();
	BoundGameState.Reset();
	bOpen = false;
	bActionPending = false;
	LastAction = ECatShopUIAction::None;
	LastEntryId = NAME_None;
	LastRejectedReason = FText();
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

// 提交流程：记录最近动作和条目，pending 状态会让商品按钮禁用，直到下一次公开经济/货架快照刷新。
void UCatShopModel::MarkActionSubmitted(const ECatShopUIAction Action, const FName EntryId)
{
	LastAction = Action;
	LastEntryId = EntryId;
	LastRejectedReason = FText();
	bActionPending = true;
	Refresh();
}

// 本地拒绝流程：记录拒绝原因并刷新结果文本；这类拒绝不会发送任何服务器 RPC。
void UCatShopModel::MarkActionRejected(const ECatShopUIAction Action, const FName EntryId, const FText Reason)
{
	LastAction = Action;
	LastEntryId = EntryId;
	LastRejectedReason = Reason;
	bActionPending = false;
	Refresh();
}

// 刷新流程：读取 GameState 经济/货架快照和 Settings 商品目录，生成只读 ViewState；公开快照变化会关闭 pending 并刷新余额/库存提示。
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

	const UCatShopEconomySettings* Settings = GetDefault<UCatShopEconomySettings>();
	if (Settings)
	{
		NewState.Entries.Reserve(Settings->CatalogEntries.Num());
		for (const FCatShopCatalogEntry& Entry : Settings->CatalogEntries)
		{
			FCatShopEntryView EntryView = MakeEntryView(Entry, NewState.Economy, NewState.bEconomyAvailable);
			EntryView.bActionEnabled = NewState.bOpen && NewState.bEconomyAvailable
				&& !NewState.bActionPending && EntryView.bStockAvailable
				&& EntryView.bAffordable && !EntryView.bSoldOut;
			NewState.Entries.Add(EntryView);
		}
	}

	NewState.WalletText = NewState.bEconomyAvailable
		? FText::FromString(FString::Printf(TEXT("商店：团队公款 %d"), NewState.Economy.Balance))
		: FText::FromString(TEXT("商店：公款数据未同步"));

	if (!LastRejectedReason.IsEmpty())
	{
		NewState.ResultText = LastRejectedReason;
	}
	else if (NewState.bActionPending)
	{
		NewState.ResultText = FText::FromString(FString::Printf(TEXT("已提交：%s，等待商店结果同步"),
			*LastEntryId.ToString()));
	}
	else if (!LastEntryId.IsNone())
	{
		NewState.ResultText = FText::FromString(FString::Printf(TEXT("最近操作：%s，请在背包查看装备或耗材"),
			*LastEntryId.ToString()));
	}
	else
	{
		NewState.ResultText = FText::FromString(TEXT("请选择商品"));
	}

	ViewState = MoveTemp(NewState);
	OnViewStateChanged.Broadcast();
}

// 状态读取流程：返回最近商店投影；调用方不能通过它取得 GameState 或 Settings 指针。
const FCatShopViewState& UCatShopModel::GetViewState() const
{
	return ViewState;
}

// 条目查询流程：只在当前投影中查找商品行，避免点击过期蓝图行时绕过最新目录。
bool UCatShopModel::TryFindEntryView(const FName EntryId, FCatShopEntryView& OutEntry) const
{
	const FCatShopEntryView* FoundEntry = ViewState.Entries.FindByPredicate(
		[EntryId](const FCatShopEntryView& Entry)
		{
			return Entry.EntryId == EntryId;
		});
	if (!FoundEntry)
	{
		OutEntry = FCatShopEntryView();
		return false;
	}
	OutEntry = *FoundEntry;
	return true;
}

// GameState 变化流程：公开经济/货架事实已经刷新，清掉等待标记并重建商品、公款和结果文案。
void UCatShopModel::HandleShopEconomySnapshotChanged()
{
	bActionPending = false;
	Refresh();
}

// 商品投影流程：
// 1. 把配置目录变成中文展示行，免费条目只从 Settings 白名单判断，不按价格 0 反推。
// 2. 使用公开货架库存读取有限库存剩余数，并用当前团队公款推导付费项是否买得起。
// 3. 这些结果只影响 UI 展示和明显无效点击；真正扣款和库存裁决仍在服务器 ShopEconomy。
FCatShopEntryView UCatShopModel::MakeEntryView(const FCatShopCatalogEntry& Entry,
	const FCatShopPublicEconomySnapshot& Economy, const bool bEconomyAvailable) const
{
	const UCatShopEconomySettings* Settings = GetDefault<UCatShopEconomySettings>();
	const FCatShopStockSnapshot* Stock = bEconomyAvailable ? FindPublicStockSnapshot(Economy, Entry.EntryId) : nullptr;
	FCatShopEntryView View;
	View.EntryId = Entry.EntryId;
	View.Kind = Entry.Kind;
	View.DefinitionId = Entry.DefinitionId;
	View.UnitPrice = FMath::Max(0, Entry.UnitPrice);
	View.InitialStock = Entry.InitialStock;
	View.bStockAvailable = Stock != nullptr;
	View.bUnlimitedStock = Stock ? Stock->bUnlimitedStock : Entry.bUnlimitedStock;
	View.RemainingStock = Stock ? FMath::Max(0, Stock->RemainingStock) : 0;
	View.bSoldOut = View.bStockAvailable && !View.bUnlimitedStock && View.RemainingStock <= 0;
	View.bAffordable = !bEconomyAvailable || View.UnitPrice <= 0 || Economy.Balance >= View.UnitPrice;
	View.bFreeClaim = Settings
		&& (Entry.EntryId == Settings->FreeOrdinaryBaitEntryId || Entry.EntryId == Settings->FreeStarterRodEntryId);
	const FString StockText = !View.bStockAvailable
		? FString(TEXT("库存：未同步"))
		: View.bUnlimitedStock
		? FString(TEXT("库存：不限"))
		: (View.bSoldOut ? FString(TEXT("库存：已售罄"))
			: FString::Printf(TEXT("库存：剩余 %d/%d"), View.RemainingStock, Entry.InitialStock));
	const FString PriceText = View.UnitPrice <= 0
		? FString(TEXT("免费"))
		: FString::Printf(TEXT("价格 %d"), View.UnitPrice);
	const FString AffordableText = View.bAffordable ? FString() : FString(TEXT(" | 公款不足"));
	View.DisplayText = FText::FromString(FString::Printf(TEXT("%s | %s | %s | %s%s"),
		*Entry.EntryId.ToString(),
		*Entry.DefinitionId.ToString(),
		*PriceText,
		*StockText,
		*AffordableText));
	View.ActionText = View.bFreeClaim
		? FText::FromString(TEXT("领取"))
		: FText::FromString(TEXT("购买"));
	return View;
}
