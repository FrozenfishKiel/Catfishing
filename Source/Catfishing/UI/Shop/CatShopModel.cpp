#include "UI/Shop/CatShopModel.h"

#include "Engine/World.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/PlayerController.h"
#include "ShopEconomy/CatShopEconomySettings.h"

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

// 提交流程：记录最近动作和条目，pending 状态会让商品按钮禁用，直到下一次公开经济快照刷新。
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

// 刷新流程：读取 GameState 公款快照和 Settings 商品目录，生成只读 ViewState；公开快照变化会关闭 pending 并显示最新流水数。
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
			FCatShopEntryView EntryView = MakeEntryView(Entry);
			EntryView.bActionEnabled = NewState.bOpen && NewState.bEconomyAvailable && !NewState.bActionPending;
			NewState.Entries.Add(EntryView);
		}
	}

	NewState.WalletText = NewState.bEconomyAvailable
		? FText::FromString(FString::Printf(TEXT("商店：团队公款 %d | 钱包版本 %lld | 流水 %d"),
			NewState.Economy.Balance,
			NewState.Economy.WalletRevision,
			NewState.Economy.Transactions.Num()))
		: FText::FromString(TEXT("商店：公款数据未同步"));

	if (!LastRejectedReason.IsEmpty())
	{
		NewState.ResultText = LastRejectedReason;
	}
	else if (NewState.bActionPending)
	{
		NewState.ResultText = FText::FromString(FString::Printf(TEXT("已提交：%s，等待服务器公开流水刷新"),
			*LastEntryId.ToString()));
	}
	else if (!LastEntryId.IsNone())
	{
		NewState.ResultText = FText::FromString(FString::Printf(TEXT("最近操作：%s，当前公开流水 %d 条"),
			*LastEntryId.ToString(),
			NewState.Economy.Transactions.Num()));
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

// GameState 变化流程：公开经济事实已经刷新，清掉等待标记并重建商品、公款和结果文案。
void UCatShopModel::HandleShopEconomySnapshotChanged()
{
	bActionPending = false;
	Refresh();
}

// 商品投影流程：把配置目录变成中文展示行；免费条目只从 Settings 白名单判断，不按价格 0 反推。
FCatShopEntryView UCatShopModel::MakeEntryView(const FCatShopCatalogEntry& Entry) const
{
	const UCatShopEconomySettings* Settings = GetDefault<UCatShopEconomySettings>();
	FCatShopEntryView View;
	View.EntryId = Entry.EntryId;
	View.Kind = Entry.Kind;
	View.DefinitionId = Entry.DefinitionId;
	View.UnitPrice = FMath::Max(0, Entry.UnitPrice);
	View.InitialStock = Entry.InitialStock;
	View.bUnlimitedStock = Entry.bUnlimitedStock;
	View.bFreeClaim = Settings
		&& (Entry.EntryId == Settings->FreeOrdinaryBaitEntryId || Entry.EntryId == Settings->FreeStarterRodEntryId);
	const FString StockText = View.bUnlimitedStock
		? FString(TEXT("无限库存"))
		: FString::Printf(TEXT("初始库存 %d"), Entry.InitialStock);
	View.DisplayText = FText::FromString(FString::Printf(TEXT("%s | %s | 价格 %d | %s"),
		*Entry.EntryId.ToString(),
		*Entry.DefinitionId.ToString(),
		View.UnitPrice,
		*StockText));
	View.ActionText = View.bFreeClaim
		? FText::FromString(TEXT("领取"))
		: FText::FromString(TEXT("购买"));
	return View;
}
