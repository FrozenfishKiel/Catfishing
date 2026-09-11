#include "ShopEconomy/CatShopEconomyService.h"

#include "AbilitySystem/Attributes/CatEconomyAttributeSet.h"
#include "AbilitySystem/Effects/CatShopEconomyTransactionEffect.h"
#include "AbilitySystem/Executions/CatShopEconomyTransactionExecutionCalculation.h"
#include "AbilitySystemComponent.h"
#include "Engine/DataTable.h"
#include "Framework/Game/CatfishingGameState.h"
#include "Logging/CatLog.h"
#include "ShopEconomy/CatShopCartCommandUtils.h"
#include "ShopEconomy/CatShopInventoryComponent.h"
#include "ShopEconomy/CatShopEconomySettings.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	// 售鱼命令形状校验流程：库存提交前必须确认每条鱼都有正式实例、种类和重量，且同一实例不允许在一笔收入里出现两次。
	bool IsFishSaleCommandShapeValid(const FCatShopFishSaleCommand& Command)
	{
		if (Command.Fish.IsEmpty())
		{
			return false;
		}
		TSet<FGuid> SeenFish;
		for (const FCatShopFishSaleLine& Line : Command.Fish)
		{
			if (!Line.FishInstanceId.IsValid() || Line.FishDefinitionId.IsNone() || !FMath::IsFinite(Line.WeightKilograms)
				|| Line.WeightKilograms <= 0.0 || SeenFish.Contains(Line.FishInstanceId))
			{
				return false;
			}
			SeenFish.Add(Line.FishInstanceId);
		}
		return true;
	}
}

// 创建条件流程：只允许服务器 Game World 拥有可写经济事实；客户端不能生成第二份公款或库存。
bool UCatShopEconomyService::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

// 初始化流程：先交父类，再从 Settings 读取命令 gate、事务版本和收购表引用；余额稍后由 GameState ASC 初始化，服务不保存余额副本。
void UCatShopEconomyService::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadRuntimeEconomyFromSettings();
}

// 反初始化流程：先关闭新交易，再解除摊位库存订阅、清除账本和终态缓存；不把团队公款带入下一局。
void UCatShopEconomyService::Deinitialize()
{
	CloseCommands();
	for (const FRegisteredShopInventorySubscription& Subscription : RegisteredInventoryChangedHandles)
	{
		if (UCatShopInventoryComponent* Inventory = Subscription.Inventory.Get())
		{
			Inventory->OnInventoryChanged.Remove(Subscription.Handle);
		}
	}
	RegisteredShopInventories.Reset();
	RegisteredInventoryChangedHandles.Reset();
	TransactionLedger.Reset();
	TerminalCache.Reset();
	CartTerminalCache.Reset();
	TerminalPayloadByKey.Reset();
	Super::Deinitialize();
}

// 注册流程：只接受 authority World 中真实摊位库存组件；重复注册保持幂等，并订阅它的货架变化来推动公开快照刷新。
bool UCatShopEconomyService::RegisterShopInventory(UCatShopInventoryComponent* ShopInventory)
{
	if (!ShopInventory || !ShopInventory->GetOwner() || !ShopInventory->GetOwner()->HasAuthority()
		|| !ShopInventory->GetShopInventoryId().IsValid())
	{
		return false;
	}
	for (const TWeakObjectPtr<UCatShopInventoryComponent>& ExistingInventory : RegisteredShopInventories)
	{
		if (ExistingInventory.Get() == ShopInventory)
		{
			return true;
		}
	}
	RegisteredShopInventories.Add(ShopInventory);
	const bool bAlreadySubscribed = RegisteredInventoryChangedHandles.ContainsByPredicate(
		[ShopInventory](const FRegisteredShopInventorySubscription& Subscription)
		{
			return Subscription.Inventory.Get() == ShopInventory;
		});
	if (!bAlreadySubscribed)
	{
		FRegisteredShopInventorySubscription& Subscription = RegisteredInventoryChangedHandles.AddDefaulted_GetRef();
		Subscription.Inventory = ShopInventory;
		Subscription.Handle =
			ShopInventory->OnInventoryChanged.AddUObject(this, &ThisClass::HandleRegisteredShopInventoryChanged);
	}
	OnShopInventoryRefreshed.Broadcast();
	return true;
}

// 注销流程：按组件对象精确移除注册和变化订阅；注销后广播一次，让客户端公开快照清掉这份摊位货架。
void UCatShopEconomyService::UnregisterShopInventory(UCatShopInventoryComponent* ShopInventory)
{
	if (!ShopInventory)
	{
		return;
	}
	RegisteredShopInventories.RemoveAll([ShopInventory](const TWeakObjectPtr<UCatShopInventoryComponent>& Candidate)
	{
		return !Candidate.IsValid() || Candidate.Get() == ShopInventory;
	});
	for (int32 SubscriptionIndex = RegisteredInventoryChangedHandles.Num() - 1; SubscriptionIndex >= 0; --SubscriptionIndex)
	{
		const FRegisteredShopInventorySubscription& Subscription = RegisteredInventoryChangedHandles[SubscriptionIndex];
		if (!Subscription.Inventory.IsValid() || Subscription.Inventory.Get() == ShopInventory)
		{
			if (UCatShopInventoryComponent* Inventory = Subscription.Inventory.Get())
			{
				Inventory->OnInventoryChanged.Remove(Subscription.Handle);
			}
			RegisteredInventoryChangedHandles.RemoveAtSwap(SubscriptionIndex, 1, EAllowShrinking::No);
		}
	}
	OnShopInventoryRefreshed.Broadcast();
}

// 公款读取流程：返回当前唯一团队公款快照副本；调用方不能借引用改余额。
FCatShopWalletSnapshot UCatShopEconomyService::GetWalletSnapshot() const
{
	FCatShopWalletSnapshot Snapshot;
	Snapshot.Revision = WalletRevision;
	TryGetTeamWalletBalance(Snapshot.Balance);
	return Snapshot;
}

// 库存读取流程：先清输出，再从指定摊位库存读取 EntryId；服务不维护全局货架 Map。
bool UCatShopEconomyService::TryGetStockSnapshot(const UCatShopInventoryComponent* ShopInventory,
	const FName EntryId, FCatShopStockSnapshot& OutSnapshot) const
{
	OutSnapshot = FCatShopStockSnapshot();
	if (!ShopInventory)
	{
		return false;
	}
	return ShopInventory->TryGetStockSnapshot(EntryId, OutSnapshot);
}

// 目录项读取流程：先清输出，再从指定摊位库存取回当前货架配置；它和库存读取分开，因为两者回答的是两个问题。
bool UCatShopEconomyService::TryGetCatalogEntry(const UCatShopInventoryComponent* ShopInventory,
	const FName EntryId, FCatShopCatalogEntry& OutEntry) const
{
	OutEntry = FCatShopCatalogEntry();
	if (!ShopInventory)
	{
		return false;
	}
	return ShopInventory->TryGetCatalogEntry(EntryId, OutEntry);
}

// 重放判定流程：用与 PurchaseCatalogCart 完全相同的三段拼出幂等键，再只查终态表是否已有该键。
// 键的拼法必须和整车购买写口逐字一致，否则商店交易入口会把重试当成首次请求，白跑一趟交付前置校验。
// 只读：不比对载荷签名（载荷不一致由购买写口自己判 InvalidPayload），不看成败，不改任何状态。
bool UCatShopEconomyService::HasCatalogCartTerminal(const FCatShopCartCommand& Command) const
{
	const FString CacheKey = MakeTerminalKey(Command.Context.StableNetId, TEXT("CartPurchase"),
		Command.Context.RequestId);
	return CartTerminalCache.Contains(CacheKey);
}

// 账本读取流程：复制本局交易记录；广播层可展示金额和交付状态，但不能绕过确认入口直接改服务内数组。
TArray<FCatShopTransactionRecord> UCatShopEconomyService::GetTransactionLedgerSnapshot() const
{
	return TransactionLedger;
}

// 整车报价流程：
// 1. 先校验请求身份、来源摊位和购物车行，再合并重复 EntryId，保证库存与价格只算一次聚合数量。
// 2. 逐行读取服务器当前货架目录和库存，计算本行小计、交付数量和整车总价；客户端传来的价格或数量倍率一律不用。
// 3. 最后按团队公款版本和余额整体裁决；任何一行库存不足、目录缺失或总价溢出都会让整车拒绝。
bool UCatShopEconomyService::ResolveCatalogCartForAuthority(const FCatShopCartCommand& Command,
	const UCatShopInventoryComponent* ShopInventory, FCatShopResolvedCart& OutResolved,
	ECatDomainCommandError& OutError) const
{
	OutResolved = FCatShopResolvedCart();
	OutError = ECatDomainCommandError::None;
	OutResolved.Wallet = GetWalletSnapshot();
	if (!Command.Context.RequestId.IsValid() || Command.Context.StableNetId.IsEmpty()
		|| !Command.ShopInventoryId.IsValid())
	{
		OutError = ECatDomainCommandError::InvalidPayload;
		return false;
	}
	if (!ShopInventory || ShopInventory->GetShopInventoryId() != Command.ShopInventoryId)
	{
		OutError = ECatDomainCommandError::InvalidPayload;
		return false;
	}
	TArray<FCatShopCartLineCommand> NormalizedLines;
	if (!CatShopCartCommands::NormalizeLines(Command.Lines, NormalizedLines))
	{
		OutError = ECatDomainCommandError::InvalidPayload;
		return false;
	}
	if (!bRuntimeReady || !ShopInventory->IsRuntimeCatalogReady())
	{
		OutError = ECatDomainCommandError::PolicyUndecided;
		return false;
	}
	if (!bCommandsOpen)
	{
		OutError = ECatDomainCommandError::CommandsClosed;
		return false;
	}
	int32 CurrentWalletBalance = 0;
	if (!TryGetTeamWalletBalance(CurrentWalletBalance))
	{
		OutError = ECatDomainCommandError::DependencyUnavailable;
		return false;
	}
	if (Command.Context.ExpectedRevision != WalletRevision)
	{
		OutError = ECatDomainCommandError::RevisionConflict;
		return false;
	}
	OutResolved.Command = Command;
	OutResolved.Command.Lines = NormalizedLines;
	OutResolved.Lines.Reserve(NormalizedLines.Num());
	int64 TotalPrice = 0;
	for (const FCatShopCartLineCommand& Line : NormalizedLines)
	{
		FCatShopCatalogEntry Entry;
		if (!ShopInventory->TryGetCatalogEntry(Line.EntryId, Entry))
		{
			OutError = ECatDomainCommandError::NotFound;
			OutResolved = FCatShopResolvedCart();
			OutResolved.Wallet = GetWalletSnapshot();
			return false;
		}
		FCatShopStockSnapshot Stock;
		if (!ShopInventory->TryGetStockSnapshot(Line.EntryId, Stock))
		{
			OutError = ECatDomainCommandError::NotFound;
			OutResolved = FCatShopResolvedCart();
			OutResolved.Wallet = GetWalletSnapshot();
			return false;
		}
		if (!Entry.IsRuntimeReady())
		{
			OutError = ECatDomainCommandError::PolicyUndecided;
			OutResolved = FCatShopResolvedCart();
			OutResolved.Wallet = GetWalletSnapshot();
			return false;
		}
		if (!Stock.bUnlimitedStock && Stock.RemainingStock < Line.CartCount)
		{
			OutError = ECatDomainCommandError::CapacityExceeded;
			OutResolved = FCatShopResolvedCart();
			OutResolved.Wallet = GetWalletSnapshot();
			return false;
		}
		const int64 DeliveryQuantity = static_cast<int64>(Entry.PurchaseQuantity) * Line.CartCount;
		const int64 LineTotalPrice = static_cast<int64>(Entry.UnitPrice) * Line.CartCount;
		if (DeliveryQuantity <= 0 || DeliveryQuantity > MAX_int32
			|| LineTotalPrice < 0 || LineTotalPrice > MAX_int32
			|| TotalPrice > MAX_int32 - LineTotalPrice)
		{
			OutError = ECatDomainCommandError::InvalidPayload;
			OutResolved = FCatShopResolvedCart();
			OutResolved.Wallet = GetWalletSnapshot();
			return false;
		}
		FCatShopResolvedCartLine& ResolvedLine = OutResolved.Lines.AddDefaulted_GetRef();
		ResolvedLine.Entry = Entry;
		ResolvedLine.CartCount = Line.CartCount;
		ResolvedLine.DeliveryQuantity = static_cast<int32>(DeliveryQuantity);
		ResolvedLine.LineTotalPrice = static_cast<int32>(LineTotalPrice);
		TotalPrice += LineTotalPrice;
	}
	if (CurrentWalletBalance < TotalPrice)
	{
		OutError = ECatDomainCommandError::CapacityExceeded;
		OutResolved = FCatShopResolvedCart();
		OutResolved.Wallet = GetWalletSnapshot();
		return false;
	}
	OutResolved.TotalPrice = static_cast<int32>(TotalPrice);
	return true;
}

// 整车购买流程：
// 1. 先用身份、CartPurchase 和 RequestId 查询幂等终态；成功订单重放时重读账本/库存，把交付状态带回最新值。
//    若首次终态是拒绝，重放必须返回首次错误，不能把一次失败请求伪装成已经结算。
// 2. 首次命令复用整车报价判据，然后让摊位库存整批扣减；扣库存失败时公款和账本保持不变。
// 3. 库存扣完后一次扣总价，并为每个 EntryId 写一条待交付账本，免费商品也以 0 元购买行进入同一交付链。
// 提交守卫覆盖库存、GAS、账本及广播，期间嵌套购买或售鱼直接拒绝且不缓存，避免回调抢先复用尚未完成的请求。
FCatShopCartTransactionResult UCatShopEconomyService::PurchaseCatalogCart(const FCatShopCartCommand& Command,
	UCatShopInventoryComponent* ShopInventory)
{
	FCatShopCartTransactionResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	Result.Wallet = GetWalletSnapshot();
	if (bTransactionInProgress)
	{
		Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		Result.Command.Revision = WalletRevision;
		UE_LOG(LogCatfishing, Warning, TEXT("Event=WalletTransactionRejected RequestId=%s World=%s Result=Reentry Operation=Purchase"),
			*Command.Context.RequestId.ToString(), *GetNameSafe(GetWorld()));
		return Result;
	}
	TGuardValue<bool> TransactionGuard(bTransactionInProgress, true);
	if (!Command.Context.RequestId.IsValid() || Command.Context.StableNetId.IsEmpty()
		|| !Command.ShopInventoryId.IsValid() || Command.Lines.IsEmpty())
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		Result.Command.Revision = WalletRevision;
		return Result;
	}
	const FString CacheKey = MakeTerminalKey(Command.Context.StableNetId, TEXT("CartPurchase"),
		Command.Context.RequestId);
	const FString PayloadSignature = MakeCartPayloadSignature(Command);
	if (const FCatShopCartTransactionResult* Cached = CartTerminalCache.Find(CacheKey))
	{
		if (!DoesTerminalPayloadMatch(CacheKey, PayloadSignature))
		{
			Result.Command.Error = ECatDomainCommandError::InvalidPayload;
			Result.Command.Revision = WalletRevision;
			return Result;
		}
		Result = *Cached;
		RefreshCartReplayResultFromLedger(Result);
		Result.Command.bCommitted = false;
		if (Cached->Command.Error == ECatDomainCommandError::None && !Result.Transactions.IsEmpty())
		{
			Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
		}
		return Result;
	}

	FCatShopResolvedCart ResolvedCart;
	ECatDomainCommandError Rejection = ECatDomainCommandError::None;
	if (!ResolveCatalogCartForAuthority(Command, ShopInventory, ResolvedCart, Rejection))
	{
		Result.Command.Error = Rejection;
		Result.Command.Revision = WalletRevision;
		Result.Wallet = GetWalletSnapshot();
		CacheCartTerminalResult(CacheKey, PayloadSignature, Result);
		return Result;
	}
	int32 WalletDelta = -ResolvedCart.TotalPrice;
	// 货架持有原状态直到GE确认；拒绝扣款不会消耗限量商品，公开通知仍留到双方与账本全部提交之后。
	if (!ShopInventory->ConsumeCatalogEntriesFromAuthority(ResolvedCart.Command.Lines, Result.Stocks, [&]()
		{
			return TryApplyTeamWalletTransaction(WalletDelta, Command.Context.RequestId);
		}))
	{
		Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		Result.Command.Revision = WalletRevision;
		Result.Wallet = GetWalletSnapshot();
		CacheCartTerminalResult(CacheKey, PayloadSignature, Result);
		return Result;
	}
	if (ResolvedCart.TotalPrice > 0)
	{
		++WalletRevision;
	}
	Result.Transactions.Reserve(ResolvedCart.Lines.Num());
	for (const FCatShopResolvedCartLine& Line : ResolvedCart.Lines)
	{
		FCatShopTransactionRecord& Record = TransactionLedger.AddDefaulted_GetRef();
		Record.TransactionId = FGuid::NewGuid();
		Record.RequestId = Command.Context.RequestId;
		Record.StableNetId = Command.Context.StableNetId;
		Record.bPurchase = true;
		Record.bDeliveryPending = true;
		Record.EntryId = Line.Entry.EntryId;
		Record.ShopInventoryId = Command.ShopInventoryId;
		Record.DefinitionId = Line.Entry.DefinitionId;
		Record.PurchaseQuantity = Line.DeliveryQuantity;
		Record.WalletDelta = -Line.LineTotalPrice;
		Record.WalletRevision = WalletRevision;
		if (const FCatShopStockSnapshot* Stock = Result.Stocks.FindByPredicate(
			[&Line](const FCatShopStockSnapshot& Candidate)
			{
				return Candidate.EntryId == Line.Entry.EntryId;
			}))
		{
			Record.StockRevision = Stock->Revision;
		}
		Result.Transactions.Add(Record);
	}
	Result.Command.bCommitted = true;
	Result.Command.Error = ECatDomainCommandError::None;
	Result.Command.Revision = WalletRevision;
	Result.Wallet = GetWalletSnapshot();
	CacheCartTerminalResult(CacheKey, PayloadSignature, Result);
	for (const FCatShopTransactionRecord& Record : Result.Transactions)
	{
		OnPublicTransactionCommitted.Broadcast(MakePublicTransaction(Record));
	}
	return Result;
}

// 估价流程：先要求经济运行与本地收购表可用，再把一条服务器确认的鱼交给交易 ExecCalc 的纯算式逐条计算。
bool UCatShopEconomyService::TryAppraiseFishSale(const FName FishDefinitionId, const double WeightKilograms,
	int32& OutSaleValue) const
{
	OutSaleValue = 0;
	if (!bRuntimeReady)
	{
		return false;
	}
	FCatShopFishSaleLine Line;
	Line.FishDefinitionId = FishDefinitionId;
	Line.WeightKilograms = WeightKilograms;
	TArray<FCatShopFishSaleLine> Fish;
	Fish.Add(Line);
	return UCatShopEconomyTransactionExecutionCalculation::TryCalculateFishSale(GetFishSalePriceTable(), Fish, OutSaleValue);
}

// 售鱼预检流程：先阻止提交回调重入，再核对身份、库存提交关联和重放载荷；失败缓存保留原错误，成功缓存才返回 AlreadyResolved。
// 首次请求用共用纯函数检查整批价格及当前余额边界，不写任何状态；ExpectedRevision 不参与售鱼并发裁决。
bool UCatShopEconomyService::ValidateFishSale(const FCatShopFishSaleCommand& Command, ECatDomainCommandError& OutError,
	int64& OutCurrentWalletRevision) const
{
	OutError = ECatDomainCommandError::None;
	OutCurrentWalletRevision = WalletRevision;
	if (bTransactionInProgress)
	{
		OutError = ECatDomainCommandError::DependencyUnavailable;
		return false;
	}
	if (!Command.Context.RequestId.IsValid() || Command.Context.StableNetId.IsEmpty()
		|| !Command.InventoryCommitId.IsValid() || !IsFishSaleCommandShapeValid(Command))
	{
		OutError = ECatDomainCommandError::InvalidPayload;
		return false;
	}
	const FString CacheKey = MakeTerminalKey(Command.Context.StableNetId, TEXT("FishSale"), Command.Context.RequestId);
	const FString PayloadSignature = MakeFishSalePayloadSignature(Command);
	if (const FCatShopTransactionResult* Cached = TerminalCache.Find(CacheKey))
	{
		OutError = !DoesTerminalPayloadMatch(CacheKey, PayloadSignature) ? ECatDomainCommandError::InvalidPayload
			: Cached->Command.bCommitted && Cached->Command.Error == ECatDomainCommandError::None
			? ECatDomainCommandError::AlreadyResolved : Cached->Command.Error;
		return false;
	}
	if (!bRuntimeReady)
	{
		OutError = ECatDomainCommandError::PolicyUndecided;
		return false;
	}
	if (!bCommandsOpen)
	{
		OutError = ECatDomainCommandError::CommandsClosed;
		return false;
	}
	int32 AppraisedValue = 0;
	if (!UCatShopEconomyTransactionExecutionCalculation::TryCalculateFishSale(GetFishSalePriceTable(), Command.Fish, AppraisedValue))
	{
		OutError = ECatDomainCommandError::PolicyUndecided;
		return false;
	}
	int32 CurrentBalance = 0;
	if (!TryGetTeamWalletBalance(CurrentBalance))
	{
		OutError = ECatDomainCommandError::DependencyUnavailable;
		return false;
	}
	if (static_cast<int64>(CurrentBalance) + AppraisedValue > 16777216)
	{
		OutError = ECatDomainCommandError::CapacityExceeded;
		return false;
	}
	return true;
}

// 售鱼入账流程：守卫阻止提交回调重入，先校验身份并保留缓存的首次成功或失败结果，再冻结鱼行交给 GE 实际计算并入账。
// 只有 GE 已执行且余额与执行回执一致，才追加首鱼摘要账本、缓存成功并广播；所有拒绝均不写成功账本，服务始终不删除鱼。
FCatShopTransactionResult UCatShopEconomyService::ApplyFishSale(const FCatShopFishSaleCommand& Command)
{
	FCatShopTransactionResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	Result.Wallet = GetWalletSnapshot();
	if (bTransactionInProgress)
	{
		Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		Result.Command.Revision = WalletRevision;
		UE_LOG(LogCatfishing, Warning, TEXT("Event=WalletTransactionRejected RequestId=%s World=%s Result=Reentry Operation=FishSale"),
			*Command.Context.RequestId.ToString(), *GetNameSafe(GetWorld()));
		return Result;
	}
	TGuardValue<bool> TransactionGuard(bTransactionInProgress, true);
	if (!Command.Context.RequestId.IsValid() || Command.Context.StableNetId.IsEmpty()
		|| !Command.InventoryCommitId.IsValid() || !IsFishSaleCommandShapeValid(Command))
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		Result.Command.Revision = WalletRevision;
		return Result;
	}
	const FString CacheKey = MakeTerminalKey(Command.Context.StableNetId, TEXT("FishSale"), Command.Context.RequestId);
	const FString PayloadSignature = MakeFishSalePayloadSignature(Command);
	if (const FCatShopTransactionResult* Cached = TerminalCache.Find(CacheKey))
	{
		if (!DoesTerminalPayloadMatch(CacheKey, PayloadSignature))
		{
			Result.Command.Error = ECatDomainCommandError::InvalidPayload;
			Result.Command.Revision = WalletRevision;
			return Result;
		}
		Result = *Cached;
		if (Result.Transaction.TransactionId.IsValid())
		{
			if (const FCatShopTransactionRecord* CurrentRecord = TransactionLedger.FindByPredicate(
				[&Result](const FCatShopTransactionRecord& Candidate)
			{
				return Candidate.TransactionId == Result.Transaction.TransactionId;
			}))
			{
				Result.Transaction = *CurrentRecord;
			}
		}
		Result.Command.bCommitted = false;
		if (Cached->Command.bCommitted && Cached->Command.Error == ECatDomainCommandError::None)
		{
			Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
		}
		Result.Wallet = GetWalletSnapshot();
		return Result;
	}
	if (!bRuntimeReady)
	{
		Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
		Result.Command.Revision = WalletRevision;
		CacheTerminalResult(CacheKey, PayloadSignature, Result);
		return Result;
	}
	if (!bCommandsOpen)
	{
		Result.Command.Error = ECatDomainCommandError::CommandsClosed;
		Result.Command.Revision = WalletRevision;
		CacheTerminalResult(CacheKey, PayloadSignature, Result);
		return Result;
	}
	int32 AppraisedValue = 0;
	if (!TryApplyTeamWalletTransaction(AppraisedValue, Command.Context.RequestId, &Command))
	{
		Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		Result.Command.Revision = WalletRevision;
		CacheTerminalResult(CacheKey, PayloadSignature, Result);
		return Result;
	}

	++WalletRevision;
	FCatShopTransactionRecord& Record = TransactionLedger.AddDefaulted_GetRef();
	Record.TransactionId = FGuid::NewGuid();
	Record.RequestId = Command.Context.RequestId;
	Record.StableNetId = Command.Context.StableNetId;
	Record.bFishSale = true;
	Record.FishInstanceId = Command.Fish[0].FishInstanceId;
	Record.WalletDelta = AppraisedValue;
	Record.WalletRevision = WalletRevision;
	Result.Command.bCommitted = true;
	Result.Command.Error = ECatDomainCommandError::None;
	Result.Command.Revision = WalletRevision;
	Result.Wallet = GetWalletSnapshot();
	Result.Transaction = Record;
	CacheTerminalResult(CacheKey, PayloadSignature, Result);
	OnPublicTransactionCommitted.Broadcast(MakePublicTransaction(Record));
	return Result;
}

// 交付确认流程：先按确认 RequestId 重放，再用 TransactionId 找到原订单；成功终态可幂等返回，失败终态必须返回首次错误。
// 只允许原买家用真实下游回执把 Pending 推进到 Delivered。
FCatShopTransactionResult UCatShopEconomyService::ConfirmTransactionDelivery(
	const FCatShopDeliveryConfirmationCommand& Command)
{
	FCatShopTransactionResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	Result.Wallet = GetWalletSnapshot();
	if (!Command.Context.RequestId.IsValid() || Command.Context.StableNetId.IsEmpty()
		|| !Command.TransactionId.IsValid() || !Command.DeliveryReceiptId.IsValid())
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		Result.Command.Revision = WalletRevision;
		return Result;
	}
	const FString CacheKey = MakeTerminalKey(Command.Context.StableNetId, TEXT("Delivery"), Command.Context.RequestId);
	const FString PayloadSignature = MakeDeliveryPayloadSignature(Command);
	if (const FCatShopTransactionResult* Cached = TerminalCache.Find(CacheKey))
	{
		if (!DoesTerminalPayloadMatch(CacheKey, PayloadSignature))
		{
			Result.Command.Error = ECatDomainCommandError::InvalidPayload;
			Result.Command.Revision = WalletRevision;
			return Result;
		}
		Result = *Cached;
		if (Cached->Command.bCommitted && Cached->Command.Error == ECatDomainCommandError::None)
		{
			Result.Command.bCommitted = false;
			Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
		}
		return Result;
	}
	FCatShopTransactionRecord* Record = TransactionLedger.FindByPredicate([&Command](const FCatShopTransactionRecord& Candidate)
	{
		return Candidate.TransactionId == Command.TransactionId;
	});
	if (!bCommandsOpen)
	{
		Result.Command.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (!Record)
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
	}
	else
	{
		Result.Transaction = *Record;
		if (const UCatShopInventoryComponent* CurrentInventory =
			FindRegisteredShopInventoryById(Record->ShopInventoryId))
		{
			CurrentInventory->TryGetStockSnapshot(Record->EntryId, Result.Stock);
		}
		if (Record->StableNetId != Command.Context.StableNetId)
		{
			Result.Command.Error = ECatDomainCommandError::PermissionDenied;
		}
		else if (Command.Context.ExpectedRevision != Record->WalletRevision)
		{
			Result.Command.Error = ECatDomainCommandError::RevisionConflict;
		}
		else if (Record->bDeliveryConfirmed)
		{
			Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
		}
		else if (!Record->bPurchase || !Record->bDeliveryPending
			|| Record->DefinitionId.IsNone() || Record->PurchaseQuantity <= 0)
		{
			Result.Command.Error = ECatDomainCommandError::InvalidPhase;
		}
		else
		{
			Record->bDeliveryPending = false;
			Record->bDeliveryConfirmed = true;
			Record->DeliveryReceiptId = Command.DeliveryReceiptId;
			Result.Command.bCommitted = true;
			Result.Command.Error = ECatDomainCommandError::None;
			Result.Transaction = *Record;
		}
	}
	Result.Command.Revision = WalletRevision;
	Result.Wallet = GetWalletSnapshot();
	CacheTerminalResult(CacheKey, PayloadSignature, Result);
	if (Result.Command.bCommitted)
	{
		// 交付确认不动公款，但它把订单从"已付款待取"推到"已到货"，这是全队都要看到的状态变化，
		// 所以和购买、售鱼走同一条广播；订阅方据此更新同一条交易记录，而不是新增一条。
		OnPublicTransactionCommitted.Broadcast(MakePublicTransaction(Result.Transaction));
	}
	return Result;
}

// 每日进货流程：先确认经济和写口还开着，再把新天序号广播给所有已注册摊位库存；每个摊位自己决定哪些条目需要补货。
bool UCatShopEconomyService::AdvanceShopDay(const int32 NewDayIndex)
{
	if (!bRuntimeReady || !bCommandsOpen || NewDayIndex <= CurrentShopDayIndex)
	{
		return false;
	}
	CurrentShopDayIndex = NewDayIndex;
	bool bAnyChanged = false;
	for (const TWeakObjectPtr<UCatShopInventoryComponent>& InventoryPtr : RegisteredShopInventories)
	{
		UCatShopInventoryComponent* Inventory = InventoryPtr.Get();
		if (Inventory && Inventory->AdvanceShopDay(NewDayIndex))
		{
			bAnyChanged = true;
		}
	}
	return bAnyChanged;
}

// 显式刷新流程：
// 1. 先验证 runtime、命令门、来源摊位库存、注册关系和 RequestId；刷新触发时机仍由调用方决定。
// 2. 具体抽取、库存替换和变化广播交给来源摊位库存组件，服务不读取全局商店表。
// 3. 成功后公款、账本和交易幂等缓存不被修改；公开快照会通过组件变化订阅刷新。
bool UCatShopEconomyService::RefreshShopInventoryFromCatalog(UCatShopInventoryComponent* ShopInventory,
	const FGuid& RequestId, const FCatShopRefreshRequest& Request)
{
	if (!RequestId.IsValid() || !bRuntimeReady || !bCommandsOpen || !ShopInventory)
	{
		return false;
	}
	const bool bRegistered = RegisteredShopInventories.ContainsByPredicate(
		[ShopInventory](const TWeakObjectPtr<UCatShopInventoryComponent>& Candidate)
		{
			return Candidate.Get() == ShopInventory;
		});
	if (!bRegistered)
	{
		return false;
	}
	if (!ShopInventory->RefreshShopInventoryFromCatalog(RequestId, Request))
	{
		return false;
	}
	return true;
}

// 公开快照流程：把当前公款、已注册摊位货架库存和整本账本逐条转成对外形态。
// 这里不做分页或裁剪：本局账本和货架的规模由目录与玩家实际交易次数决定，规模和一局的容器快照同量级；
// 真到了需要限量的时候，该由复制挂载点按自己的带宽策略截断，而不是让服务先把事实丢掉。
FCatShopPublicEconomySnapshot UCatShopEconomyService::BuildPublicSnapshot(
	const TFunction<APlayerState*(const FString&)>& ResolveActorPlayerState) const
{
	FCatShopPublicEconomySnapshot Snapshot;
	const FCatShopWalletSnapshot WalletSnapshot = GetWalletSnapshot();
	Snapshot.WalletRevision = WalletSnapshot.Revision;
	Snapshot.Balance = WalletSnapshot.Balance;
	Snapshot.ShopDayIndex = CurrentShopDayIndex;
	for (const TWeakObjectPtr<UCatShopInventoryComponent>& InventoryPtr : RegisteredShopInventories)
	{
		if (const UCatShopInventoryComponent* Inventory = InventoryPtr.Get())
		{
			Inventory->AppendStockSnapshots(Snapshot.Stocks);
		}
	}
	Snapshot.Transactions.Reserve(TransactionLedger.Num());
	for (const FCatShopTransactionRecord& Record : TransactionLedger)
	{
		FCatShopPublicTransaction PublicTransaction = MakePublicTransaction(Record);
		if (ResolveActorPlayerState)
		{
			// 解析不到就保持空：这说明这笔交易记录的操作者已经离局或尚未进入 Active。
			// 与其挑一个场内玩家顶上，不如让表现层显示未知操作者。
			PublicTransaction.ActorPlayerState = ResolveActorPlayerState(Record.StableNetId);
		}
		Snapshot.Transactions.Add(MoveTemp(PublicTransaction));
	}
	return Snapshot;
}

// 关闭流程：只把新命令 gate 置否，不清公款、库存、账本和 TerminalCache；因此收摊后查询仍读得到本局经济事实，网络重试
// 也仍能拿回首次终态，而四个写口和每日进货都会在各自 gate 处停下。这里不需要第二个“冻结但未 teardown”的中间态，冻结与
// 收口本来就是同一件事，差别只在调用时机。
void UCatShopEconomyService::CloseCommands()
{
	bCommandsOpen = false;
}

#if !UE_BUILD_SHIPPING
// 开发期商店救援流程：先要求经济 runtime 本身可用，再只恢复命令 gate；它不重置公款、账本、货架或缓存，让随后的 DayActive 按正式 AdvanceShopDay 补日状态。
bool UCatShopEconomyService::ReopenCommandsForDebugForceNextDay()
{
	if (!bRuntimeReady)
	{
		return false;
	}
	bCommandsOpen = true;
	return true;
}
#endif

// 设置加载流程：清空本局事务投影后读取默认对象；先在 int32 域拒绝超过 16777216 的起始金额，防止转 float 后静默舍入成合法余额。
// 合法起始余额仍由 GameState ASC 播种，这里只保留版本、运行 gate 和收购表引用。
void UCatShopEconomyService::LoadRuntimeEconomyFromSettings()
{
	WalletRevision = 0;
	TransactionLedger.Reset();
	TerminalCache.Reset();
	CartTerminalCache.Reset();
	TerminalPayloadByKey.Reset();
	bCommandsOpen = true;
	CurrentShopDayIndex = 0;
	const UCatShopEconomySettings* Settings = GetDefault<UCatShopEconomySettings>();
	bRuntimeReady = Settings && Settings->IsRuntimeEnabled() && Settings->StartingTeamWalletBalance <= 16777216;
	if (Settings && Settings->StartingTeamWalletBalance > 16777216)
	{
		UE_LOG(LogCatfishing, Warning, TEXT("Event=WalletInitializationRejected World=%s Result=InitialBalanceOutOfRange Amount=%d"),
			*GetNameSafe(GetWorld()), Settings->StartingTeamWalletBalance);
	}
	if (!Settings)
	{
		return;
	}
	WalletRevision = 1;
	FishSalePriceTable = Settings->DefaultFishSalePriceTable;
}

// 余额读取流程：先确认 GameState ASC 的 Owner/Avatar 已初始化且持有经济属性，再比较基础值和当前值并检查 double 整数边界。
// 尚未就绪、存在临时余额修饰或金额非法都返回 false 和零输出；合法时直接投影唯一 GAS 余额，不重算也不缓存起始资金。
bool UCatShopEconomyService::TryGetTeamWalletBalance(int32& OutBalance) const
{
	OutBalance = 0;
	if (!bRuntimeReady)
	{
		return false;
	}
	const UWorld* World = GetWorld();
	const ACatfishingGameState* GameState = World ? World->GetGameState<ACatfishingGameState>() : nullptr;
	const UCatEconomyAttributeSet* EconomyAttributes = GameState ? GameState->GetEconomyAttributeSet() : nullptr;
	const UAbilitySystemComponent* ASC = GameState ? GameState->GetRunAbilitySystemComponentFromAuthority() : nullptr;
	if (!EconomyAttributes || !ASC || ASC->GetOwnerActor() != GameState || ASC->GetAvatarActor() != GameState
		|| !ASC->HasAttributeSetForAttribute(UCatEconomyAttributeSet::GetTeamWalletBalanceAttribute()))
	{
		return false;
	}
	const double Balance = EconomyAttributes->GetTeamWalletBalance();
	if (!FMath::IsFinite(Balance) || Balance < 0.0 || Balance > 16777216.0 || Balance != FMath::RoundToDouble(Balance)
		|| ASC->GetNumericAttributeBase(UCatEconomyAttributeSet::GetTeamWalletBalanceAttribute()) != Balance)
	{
		return false;
	}
	OutBalance = static_cast<int32>(Balance);
	return true;
}

// 余额写入流程：先读取已就绪的钱包，再创建一个 source 冻结购买金额或售鱼行和收购表；强引用覆盖同步 GE 及全部回调，防止弱 source 被 GC 回收。
// GE 在内部完成估价和余额边界裁决；服务核对应用、执行标记与最终余额并按请求落盘，零收入也必须真实执行，成功后才回传实际金额供账本使用。
bool UCatShopEconomyService::TryApplyTeamWalletTransaction(int32& InOutDelta, const FGuid& RequestId,
	const FCatShopFishSaleCommand* FishSale)
{
	int32 CurrentBalance = 0;
	if (!TryGetTeamWalletBalance(CurrentBalance))
	{
		UE_LOG(LogCatfishing, Warning, TEXT("Event=WalletTransactionRejected RequestId=%s World=%s Result=WalletUnavailable"),
			*RequestId.ToString(), *GetNameSafe(GetWorld()));
		return false;
	}
	UWorld* World = GetWorld();
	ACatfishingGameState* GameState = World ? World->GetGameState<ACatfishingGameState>() : nullptr;
	UAbilitySystemComponent* ASC = GameState ? GameState->GetRunAbilitySystemComponentFromAuthority() : nullptr;
	if (!ASC || !GameState->GetEconomyAttributeSetFromAuthority())
	{
		UE_LOG(LogCatfishing, Warning, TEXT("Event=WalletTransactionRejected RequestId=%s World=%s Result=ASCMissing"),
			*RequestId.ToString(), *GetNameSafe(World));
		return false;
	}
	TStrongObjectPtr<UCatShopEconomyTransactionSource> Source(NewObject<UCatShopEconomyTransactionSource>(GetTransientPackage()));
	Source->RequestId = RequestId;
	Source->WalletDelta = InOutDelta;
	if (FishSale)
	{
		Source->Fish = FishSale->Fish;
		Source->PriceTable = GetFishSalePriceTable();
	}
	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(Source.Get());
	const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UCatGE_ShopEconomyTransaction::StaticClass(), 1.0f, Context);
	if (!Spec.IsValid())
	{
		UE_LOG(LogCatfishing, Warning, TEXT("Event=WalletTransactionRejected RequestId=%s World=%s Result=SpecUnavailable"),
			*RequestId.ToString(), *GetNameSafe(World));
		return false;
	}
	const bool bApplied = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get()).WasSuccessfullyApplied();
	int32 AppliedBalance = 0;
	const int64 NextBalance = static_cast<int64>(CurrentBalance) + Source->WalletDelta;
	const bool bBalanceReadable = TryGetTeamWalletBalance(AppliedBalance);
	const bool bSucceeded = bApplied && Source->bExecuted && Source->bBalanceApplied && bBalanceReadable
		&& AppliedBalance == NextBalance;
	if (!bSucceeded)
	{
		UE_LOG(LogCatfishing, Warning, TEXT("Event=WalletTransactionRejected RequestId=%s World=%s NetMode=%d Authority=1 Actor=%s LocalRole=%d Result=ExecutionOrBalanceMismatch Applied=%d Executed=%d BalanceApplied=%d BalanceReadable=%d Before=%d Delta=%d After=%d"),
			*RequestId.ToString(), *GetNameSafe(World), static_cast<int32>(World->GetNetMode()), *GameState->GetName(),
			static_cast<int32>(GameState->GetLocalRole()), bApplied, Source->bExecuted, Source->bBalanceApplied, bBalanceReadable,
			CurrentBalance, Source->WalletDelta, AppliedBalance);
		return false;
	}
	UE_LOG(LogCatfishing, Log, TEXT("Event=WalletTransactionApplied RequestId=%s World=%s NetMode=%d Authority=1 Actor=%s LocalRole=%d Result=Success Before=%d Delta=%d After=%d"),
		*RequestId.ToString(), *GetNameSafe(World), static_cast<int32>(World->GetNetMode()), *GameState->GetName(),
		static_cast<int32>(GameState->GetLocalRole()),
		CurrentBalance, Source->WalletDelta, AppliedBalance);
	InOutDelta = Source->WalletDelta;
	return true;
}

// 收购表读取流程：软引用只在需要估价时解析，资产不存在或尚未可用时返回空并让售鱼整单拒绝，不生成任何默认价格。
UDataTable* UCatShopEconomyService::GetFishSalePriceTable() const
{
	return FishSalePriceTable.IsNull() ? nullptr : FishSalePriceTable.LoadSynchronous();
}

// 购物车重放刷新流程：
// 1. 按缓存里的 TransactionId 到当前账本重读每一行，避免已确认交付的订单仍返回首次缓存时的 Pending。
// 2. 再按每行来源摊位重读当前库存快照，让客户端收到的重放结果和公开货架保持同一版本事实。
// 3. 只改传入结果副本，不修改账本、库存或终态缓存。
void UCatShopEconomyService::RefreshCartReplayResultFromLedger(FCatShopCartTransactionResult& Result) const
{
	Result.Stocks.Reset();
	for (FCatShopTransactionRecord& Record : Result.Transactions)
	{
		if (Record.TransactionId.IsValid())
		{
			if (const FCatShopTransactionRecord* CurrentRecord = TransactionLedger.FindByPredicate(
				[&Record](const FCatShopTransactionRecord& Candidate)
				{
					return Candidate.TransactionId == Record.TransactionId;
				}))
			{
				Record = *CurrentRecord;
			}
		}
		if (const UCatShopInventoryComponent* CurrentInventory =
			FindRegisteredShopInventoryById(Record.ShopInventoryId))
		{
			FCatShopStockSnapshot Stock;
			if (CurrentInventory->TryGetStockSnapshot(Record.EntryId, Stock))
			{
				Result.Stocks.Add(Stock);
			}
		}
	}
	Result.Wallet = GetWalletSnapshot();
	Result.Command.Revision = WalletRevision;
}

// 公开交易记录构造流程：复制账本里可以公开的字段。
// ActorPlayerState 刻意留空：服务只有服务器私有 StableNetId，按项目约定它不能进复制 DTO，
// 由持有身份映射的复制挂载点在发出去之前补上公开身份。
FCatShopPublicTransaction UCatShopEconomyService::MakePublicTransaction(const FCatShopTransactionRecord& Record)
{
	FCatShopPublicTransaction Public;
	Public.TransactionId = Record.TransactionId;
	Public.bPurchase = Record.bPurchase;
	Public.bFishSale = Record.bFishSale;
	Public.bDeliveryPending = Record.bDeliveryPending;
	Public.bDeliveryConfirmed = Record.bDeliveryConfirmed;
	Public.EntryId = Record.EntryId;
	Public.ShopInventoryId = Record.ShopInventoryId;
	Public.DefinitionId = Record.DefinitionId;
	Public.PurchaseQuantity = Record.PurchaseQuantity;
	Public.FishInstanceId = Record.FishInstanceId;
	Public.WalletDelta = Record.WalletDelta;
	return Public;
}

// 摊位库存查找流程：先拒绝无效 ID，再只在当前 World 注册过的组件里查找；销毁中的弱引用会被跳过。
UCatShopInventoryComponent* UCatShopEconomyService::FindRegisteredShopInventoryById(const FGuid ShopInventoryId) const
{
	if (!ShopInventoryId.IsValid())
	{
		return nullptr;
	}
	for (const TWeakObjectPtr<UCatShopInventoryComponent>& InventoryPtr : RegisteredShopInventories)
	{
		if (UCatShopInventoryComponent* Inventory = InventoryPtr.Get();
			Inventory && Inventory->GetShopInventoryId() == ShopInventoryId)
		{
			return Inventory;
		}
	}
	return nullptr;
}

// 注册摊位变化流程：不解释是哪件商品变化，只广播“公开商店快照需要重建”；GameMode 仍通过 BuildPublicSnapshot 读取完整事实。
void UCatShopEconomyService::HandleRegisteredShopInventoryChanged()
{
	OnShopInventoryRefreshed.Broadcast();
}

// 幂等键流程：只组合服务器身份、操作和 RequestId；Entry/Fish/Receipt 留给 payload signature 比对，避免同 RequestId 换业务字段时生成第二条交易。
FString UCatShopEconomyService::MakeTerminalKey(const FString& StableNetId, const TCHAR* Operation,
	const FGuid RequestId)
{
	return FString::Printf(TEXT("%s|%s|%s"), *StableNetId, Operation,
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
}

// 购物车载荷签名流程：
// 1. 正常购物车先按购买写口相同规则归一化，再冻结公款前提、来源摊位和 EntryId/选购次数。
// 2. 非法购物车也记录原始行签名，避免不同坏载荷都落到空 Lines= 后绕过同 RequestId 漂移检查。
// 3. 价格、库存和发货数量不进签名，它们来自服务器摊位目录和公开经济事实，重放时只能回读不能由客户端指定。
FString UCatShopEconomyService::MakeCartPayloadSignature(const FCatShopCartCommand& Command)
{
	TArray<FCatShopCartLineCommand> NormalizedLines;
	const bool bNormalized = CatShopCartCommands::NormalizeLines(Command.Lines, NormalizedLines);
	TArray<FString> LineParts;
	if (bNormalized)
	{
		LineParts.Reserve(NormalizedLines.Num());
		for (const FCatShopCartLineCommand& Line : NormalizedLines)
		{
			LineParts.Add(FString::Printf(TEXT("%s:%d"), *Line.EntryId.ToString(), Line.CartCount));
		}
	}
	else
	{
		LineParts.Reserve(Command.Lines.Num());
		for (int32 LineIndex = 0; LineIndex < Command.Lines.Num(); ++LineIndex)
		{
			const FCatShopCartLineCommand& Line = Command.Lines[LineIndex];
			LineParts.Add(FString::Printf(TEXT("%d:%s:%d"), LineIndex, *Line.EntryId.ToString(), Line.CartCount));
		}
	}
	return FString::Printf(TEXT("Expected=%lld|Shop=%s|Normalized=%s|Lines=%s"),
		Command.Context.ExpectedRevision,
		*Command.ShopInventoryId.ToString(EGuidFormats::DigitsWithHyphens),
		bNormalized ? TEXT("true") : TEXT("false"),
		*FString::Join(LineParts, TEXT(",")));
}

// 售鱼载荷签名流程：冻结库存提交证据及每条鱼的身份、种类和 double 重量；17 位有效数字保留可往返精度，防止舍入阈值两侧的不同重量被当成重放。
FString UCatShopEconomyService::MakeFishSalePayloadSignature(const FCatShopFishSaleCommand& Command)
{
	TArray<FString> Lines;
	Lines.Reserve(Command.Fish.Num());
	for (const FCatShopFishSaleLine& Line : Command.Fish)
	{
		Lines.Add(FString::Printf(TEXT("%s:%s:%.17g"), *Line.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
			*Line.FishDefinitionId.ToString(), Line.WeightKilograms));
	}
	return FString::Printf(TEXT("InventoryCommit=%s|Fish=%s"),
		*Command.InventoryCommitId.ToString(EGuidFormats::DigitsWithHyphens), *FString::Join(Lines, TEXT(",")));
}

// 交付载荷签名流程：冻结原交易、下游回执和公款前提；回执漂移必须拒绝而不是重放。
FString UCatShopEconomyService::MakeDeliveryPayloadSignature(const FCatShopDeliveryConfirmationCommand& Command)
{
	return FString::Printf(TEXT("Expected=%lld|Transaction=%s|Receipt=%s"),
		Command.Context.ExpectedRevision, *Command.TransactionId.ToString(EGuidFormats::DigitsWithHyphens),
		*Command.DeliveryReceiptId.ToString(EGuidFormats::DigitsWithHyphens));
}

// 载荷比对流程：终态缓存必须伴随签名一起存在且完全一致；缺失签名按不安全缓存处理并拒绝漂移。
bool UCatShopEconomyService::DoesTerminalPayloadMatch(const FString& CacheKey, const FString& PayloadSignature) const
{
	const FString* StoredPayload = TerminalPayloadByKey.Find(CacheKey);
	return StoredPayload && *StoredPayload == PayloadSignature;
}

// 终态缓存流程：同时记录结果和签名；调用方必须先完成业务校验，缓存本身不二次推导交易事实。
void UCatShopEconomyService::CacheTerminalResult(const FString& CacheKey, const FString& PayloadSignature,
	const FCatShopTransactionResult& Result)
{
	TerminalCache.Add(CacheKey, Result);
	TerminalPayloadByKey.Add(CacheKey, PayloadSignature);
}

// 购物车终态缓存流程：购物车结果单独保存多账本记录，载荷签名仍进入共享签名表，保证同 RequestId 不能换车重放。
void UCatShopEconomyService::CacheCartTerminalResult(const FString& CacheKey, const FString& PayloadSignature,
	const FCatShopCartTransactionResult& Result)
{
	CartTerminalCache.Add(CacheKey, Result);
	TerminalPayloadByKey.Add(CacheKey, PayloadSignature);
}
