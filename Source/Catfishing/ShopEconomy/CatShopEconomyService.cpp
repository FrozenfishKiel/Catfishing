#include "ShopEconomy/CatShopEconomyService.h"

#include "AbilitySystem/Attributes/CatEconomyAttributeSet.h"
#include "AbilitySystem/Effects/CatShopEconomyTransactionEffect.h"
#include "AbilitySystem/Executions/CatShopEconomyTransactionExecutionCalculation.h"
#include "AbilitySystemComponent.h"
#include "Camp/CatCampInventoryActor.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Inventory/CatFishGuardInventoryItemInstance.h"
#include "Inventory/CatFishOnlyInventoryComponent.h"
#include "FishContainers/CatFishGuardActor.h"
#include "Items/CatItem.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Engine/DataTable.h"
#include "Data/CatFishDefinition.h"
#include "EngineUtils.h"
#include "Framework/Game/CatfishingGameState.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventorySettings.h"
#include "Inventory/CatInventoryStatics.h"
#include "Logging/CatLog.h"
#include "Misc/ScopeExit.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventorySettings.h"
#include "Inventory/CatInventoryItemDefinition.h"
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
	// 先立起拆除标记再关门：teardown 不是收摊，不能借这条路径再往营地发一批小鱼干。
	bTearingDown = true;
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

// 账本读取流程：复制本局交易记录；广播层可展示金额、提交时刻和天序号，但不能改服务内数组。
TArray<FCatShopTransactionRecord> UCatShopEconomyService::GetTransactionLedgerSnapshot() const
{
	return TransactionLedger;
}

// 整车报价流程：
// 1. 先校验请求身份、来源摊位和购物车行，再合并重复 EntryId，保证库存与价格只算一次聚合数量。
// 2. 逐行读取服务器当前货架目录和库存，计算本行小计、交付数量和整车总价；客户端传来的价格或数量倍率一律不用。
// 3. 最后按服务器当前团队余额整体裁决；客户端钱包快照不参与，任何一行库存不足、目录缺失或总价溢出都会让整车拒绝。
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
	// 公款由全队共享；客户端快照可能落后，购买只按这次服务器读取的余额裁决。
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
			OutResolved.FailureReason = TEXT("OutOfStock");
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
		OutResolved.FailureReason = TEXT("InsufficientFunds");
		return false;
	}
	OutResolved.TotalPrice = static_cast<int32>(TotalPrice);
	return true;
}

// 整车提交：货架先保留原状态，协调器静默准备全部实物再付款；成功才追加账本和整车广播。
// 拒绝由协调器恢复实物、本服务下的货架组件恢复库存；重放读取首次完整终态。
FCatShopCartTransactionResult UCatShopEconomyService::PurchaseCatalogCart(const FCatShopCartCommand& Command,
	UCatShopInventoryComponent* ShopInventory, TFunctionRef<bool(TFunctionRef<bool()>)> CommitDeliveryAndPayment)
{
	FCatShopCartTransactionResult Result;
	// 在首个提前返回前注册同步退出日志，每次离开本作用域时读取最终 Result，不另存或修改交易状态。
	// 接受结果（含成功重放）使用 Log，其余拒绝使用 Warning；RequestId 供请求链关联，玩家身份仅输出散列。
	// Authority=1 来自本服务仅在服务器创建的约束；LocalRole 读取来源货架宿主，缺失时为 -1，并非请求玩家的角色。
	// 日志守卫先于事务守卫构造，因此常规路径先解除事务守卫再记录结果；它不是异步回调，也不承担回滚。
	ON_SCOPE_EXIT
	{
		const FString Message = FString::Printf(
			TEXT("Event=shop_cart_result RequestId=%s World=%s NetMode=%d Authority=1 LocalRole=%d Player=%08x Inventory=%s Error=%s Committed=%d"),
			*Command.Context.RequestId.ToString(), *GetNameSafe(GetWorld()), GetWorld()->GetNetMode(),
			ShopInventory && ShopInventory->GetOwner() ? static_cast<int32>(ShopInventory->GetOwner()->GetLocalRole()) : -1,
			GetTypeHash(Command.Context.StableNetId), TEXT("TransactionCoordinator"),
			*UEnum::GetValueAsString(Result.Command.Error), Result.Command.bCommitted);
		if (CatIsAcceptedDomainCommandResult(Result.Command))
		{
			UE_LOG(LogCatfishing, Log, TEXT("%s"), *Message);
		}
		else
		{
			UE_LOG(LogCatfishing, Warning, TEXT("%s"), *Message);
		}
	};
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
		RefreshCartReplaySnapshots(Result);
		MarkCommandReplayed(Result.Command);
		return Result;
	}

	FCatShopResolvedCart ResolvedCart;
	ECatDomainCommandError Rejection = ECatDomainCommandError::None;
	if (!ResolveCatalogCartForAuthority(Command, ShopInventory, ResolvedCart, Rejection))
	{
		Result.Command.Error = Rejection;
		Result.Command.FailureReason = ResolvedCart.FailureReason;
		Result.Command.Revision = WalletRevision;
		Result.Wallet = GetWalletSnapshot();
		CacheCartTerminalResult(CacheKey, PayloadSignature, Result);
		return Result;
	}
	int32 WalletDelta = -ResolvedCart.TotalPrice;
	bool bPaymentAttempted = false;
	// 货架持有原状态直到GE确认；拒绝扣款不会消耗限量商品，公开通知仍留到双方与账本全部提交之后。
	if (!ShopInventory->ConsumeCatalogEntriesFromAuthority(ResolvedCart.Command.Lines, Result.Stocks, [&]()
		{
			return CommitDeliveryAndPayment([&]()
			{
				bPaymentAttempted = true;
				return TryApplyTeamWalletTransaction(WalletDelta, Command.Context.RequestId);
			});
		}))
	{
		Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		Result.Command.FailureReason = bPaymentAttempted ? TEXT("PaymentRejected") : TEXT("DeliveryRejected");
		Result.Command.Revision = WalletRevision;
		Result.Wallet = GetWalletSnapshot();
		UE_LOG(LogCatfishing, Warning, TEXT("Event=shop_cart_rejected CartId=%s RequestId=%s World=%s NetMode=%d Authority=1 Result=%s"),
			*FGuid::NewDeterministicGuid(CacheKey).ToString(), *Command.Context.RequestId.ToString(),
			*GetNameSafe(GetWorld()), GetWorld()->GetNetMode(), *Result.Command.FailureReason.ToString());
		CacheCartTerminalResult(CacheKey, PayloadSignature, Result);
		return Result;
	}
	if (ResolvedCart.TotalPrice > 0)
	{
		++WalletRevision;
	}
	// 一车里的每一行共用同一个提交时刻：它们是同一次成交，回看时也应该落在同一格。
	const FDateTime CommittedAtUtc = FDateTime::UtcNow();
	Result.Transactions.Reserve(ResolvedCart.Lines.Num());
	for (const FCatShopResolvedCartLine& Line : ResolvedCart.Lines)
	{
		FCatShopTransactionRecord& Record = TransactionLedger.AddDefaulted_GetRef();
		Record.TransactionId = FGuid::NewGuid();
		Record.RequestId = Command.Context.RequestId;
		Record.CartId = FGuid::NewDeterministicGuid(CacheKey);
		Record.Items.Add({Line.Entry.DefinitionId, Line.DeliveryQuantity});
		Record.StableNetId = Command.Context.StableNetId;
		Record.bPurchase = true;
		Record.CommittedAtUtc = CommittedAtUtc;
		Record.ShopDayIndex = CurrentShopDayIndex;
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
	FCatShopPublicTransaction Cart = MakePublicTransaction(Result.Transactions[0]);
	Cart.Items.Reset();
	Cart.WalletDelta = -ResolvedCart.TotalPrice;
	for (const auto& Record : Result.Transactions) Cart.Items.Append(Record.Items);
	UE_LOG(LogCatfishing, Log, TEXT("Event=shop_cart_committed CartId=%s RequestId=%s World=%s NetMode=%d Authority=1 Items=%d Spent=%d Balance=%d"),
		*Cart.CartId.ToString(), *Command.Context.RequestId.ToString(), *GetNameSafe(GetWorld()), GetWorld()->GetNetMode(), Cart.Items.Num(), ResolvedCart.TotalPrice, Result.Wallet.Balance);
	OnPublicTransactionCommitted.Broadcast(Cart);
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
	Record.CartId = FGuid::NewDeterministicGuid(CacheKey);
	for (const auto& Fish : Command.Fish)
	{
		Record.Items.Add({Fish.FishDefinitionId, 1});
		const auto* Definition = Cast<UCatFishDefinition>(GetDefault<UCatInventorySettings>()->FindRuntimeDefinition(Fish.FishDefinitionId));
		Record.bContainsGiantFish |= Definition && Definition->BodyClass == ECatFishBodyClass::Giant;
	}
	Record.CommittedAtUtc = FDateTime::UtcNow();
	Record.ShopDayIndex = CurrentShopDayIndex;
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

// 清晨节拍流程：先确认经济和写口还开着，再对每个已注册摊位先换货架、后补货。
// 1. 换货架＝商店册 §3.1.2「装备每日刷新」：按摊位自己的出售表重抽一轮，固定上架行留下，随机候选重新按权重抽。
//    RequestId 由摊位身份与天序号确定性拼出，摊位侧的刷新幂等集合据此保证同一天只换一次。
// 2. 补货＝同节「特殊饵与特殊道具每日限量进货」：只把标了 bDailyRestock 的有限库存重置回当日进货量。
// 3. 开局第一天不换货架——那轮货架是摊位 BeginPlay 抽的，玩家还没照面就重抽等于白抽；换货架从第二天清晨开始。
// 顺序不能反：先换货架决定今天卖什么，再补货决定这批货今天有几份；反过来补的会是上一轮已被换掉的行。
bool UCatShopEconomyService::AdvanceShopDay(const int32 NewDayIndex)
{
	if (!bRuntimeReady || !bCommandsOpen || NewDayIndex <= CurrentShopDayIndex)
	{
		return false;
	}
	const bool bFirstShopDay = CurrentShopDayIndex <= 0;
	CurrentShopDayIndex = NewDayIndex;
	bool bAnyChanged = false;
	for (const TWeakObjectPtr<UCatShopInventoryComponent>& InventoryPtr : RegisteredShopInventories)
	{
		UCatShopInventoryComponent* Inventory = InventoryPtr.Get();
		if (!Inventory)
		{
			continue;
		}
		if (!bFirstShopDay)
		{
			const FGuid RefreshRequestId =
				MakeShopDayRefreshRequestId(Inventory->GetShopInventoryId(), NewDayIndex);
			// 正常运行不指定随机种子；要复现某一天的货架时由调试入口自己带种子调 RefreshShopInventoryFromCatalog。
			if (RefreshShopInventoryFromCatalog(Inventory, RefreshRequestId, FCatShopRefreshRequest()))
			{
				bAnyChanged = true;
			}
		}
		if (Inventory->AdvanceShopDay(NewDayIndex))
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
	Snapshot.bCommandsOpen = bCommandsOpen;
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
		FCatShopPublicTransaction* Cart = Snapshot.Transactions.FindByPredicate(
			[&](const auto& Existing) { return Existing.CartId == PublicTransaction.CartId; });
		if (Cart)
		{
			Cart->Items.Append(PublicTransaction.Items);
			Cart->WalletDelta += PublicTransaction.WalletDelta;
			Cart->bContainsGiantFish |= PublicTransaction.bContainsGiantFish;
		}
		else Snapshot.Transactions.Add(MoveTemp(PublicTransaction));
	}
	return Snapshot;
}

// 关门只改变营业门；普通退出和失败不得借它兑换毕业资源。
void UCatShopEconomyService::CloseCommands()
{
	if (!bCommandsOpen) return;
	bCommandsOpen = false;
	OnShopInventoryRefreshed.Broadcast();
}

bool UCatShopEconomyService::TryGetOriginalItemPrice(const FName DefinitionId, int32& OutPrice) const
{
	OutPrice = INDEX_NONE;
	const auto* Settings = GetDefault<UCatShopEconomySettings>();
	const auto* Table = Settings ? Settings->DefaultShopCatalogTable.LoadSynchronous() : nullptr;
	if (!Table || Table->GetRowStruct() != FCatShopCatalogTableRow::StaticStruct()) return false;
	// 原价来自完整商品配置，不从当日抽中的货架或最后一次成交反推。
	for (const auto& Pair : Table->GetRowMap())
	{
		const auto* Row = reinterpret_cast<const FCatShopCatalogTableRow*>(Pair.Value);
		if (Row->DefinitionId != DefinitionId) continue;
		if (Row->UnitPrice < 0 || Row->PurchaseQuantity <= 0 || Row->UnitPrice % Row->PurchaseQuantity != 0) return false;
		const int32 Price = Row->UnitPrice / Row->PurchaseQuantity;
		if (OutPrice != INDEX_NONE && OutPrice != Price) return false;
		OutPrice = Price;
	}
	return OutPrice != INDEX_NONE;
}

// 收摊只由毕业出口进入；失败调用独立清理入口。余额、装备与兑换物在同一同步提交中切换。
int32 UCatShopEconomyService::ConvertSettlementLeftoversToDriedFish()
{
	return FinalizeRunResourcesFromAuthority(true);
}

void UCatShopEconomyService::ClearFailedRunResourcesFromAuthority()
{
	FinalizeRunResourcesFromAuthority(false);
}

int32 UCatShopEconomyService::FinalizeRunResourcesFromAuthority(const bool bGraduation)
{
	auto* World = GetWorld();
	const auto* Mode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	if (!Mode || bTearingDown || bTransactionInProgress || bSettlementResourcesFinalized) return 0;
	const auto Reason = Mode->GetRunPublicState().EndReason;
	if ((bGraduation && Reason != ECatRunEndReason::Success)
		|| (!bGraduation && Reason != ECatRunEndReason::WorldProgressDepleted)) return 0;
	CloseCommands();
	TGuardValue<bool> TransactionGuard(bTransactionInProgress, true);
	const auto* Settings = GetDefault<UCatShopEconomySettings>();
	auto* DriedFish = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition(Settings->SettlementDriedFishDefinitionId);
	int32 CoinCost = 0, Balance = 0;
	const auto Reject = [&](const TCHAR* Failure)
	{
		UE_LOG(LogCatfishing, Warning, TEXT("Event=shop_settlement_rejected World=%s NetMode=%d Authority=1 Graduation=%d Result=%s"),
			*GetNameSafe(World), World->GetNetMode(), bGraduation, Failure);
		return 0;
	};
	if (!TryGetTeamWalletBalance(Balance)) return Reject(TEXT("WalletUnavailable"));

	TMap<UCatInventoryComponent*, TArray<FCatInventoryEntry>> Before, After, Held;
	TSet<FGuid> PricedIds;
	TSet<AActor*> RemoveActors;
	int64 EquipmentCoins = 0;
	int32 EquipmentCount = 0, SkippedEquipmentCount = 0;
	const auto PriceDefinition = [&](UCatInventoryItemDefinition* Definition, int32 Count)
	{
		if (!bGraduation) return;
		if (Definition && Definition->IsA<UCatFishDefinition>()) return;
		int32 Price = 0;
		if (!Definition || Count <= 0 || !TryGetOriginalItemPrice(Definition->GetInventoryDefinitionId(), Price)
			|| EquipmentCoins > MAX_int64 - int64(Price) * Count)
		{
			SkippedEquipmentCount += FMath::Max(0, Count);
			UE_LOG(LogCatfishing, Warning, TEXT("Event=shop_settlement_equipment_skipped World=%s NetMode=%d Authority=1 Definition=%s Count=%d Result=ExcludedFromExchangeValue"),
				*GetNameSafe(World), World->GetNetMode(), Definition ? *Definition->GetInventoryDefinitionId().ToString() : TEXT("None"), Count);
			return;
		}
		const int64 AddedValue = int64(Price) * Count;
		EquipmentCoins += AddedValue;
		EquipmentCount += Count;
		return;
	};
	const auto PriceInstance = [&](UCatInventoryItemInstance* Instance, int32 Count)
	{
		if (!Instance || Count <= 0) return false;
		if (PricedIds.Contains(Instance->GetItemInstanceId())) return true;
		PricedIds.Add(Instance->GetItemInstanceId());
		PriceDefinition(Instance->GetItemDefinition(), Count);
		if ((!bGraduation || !Instance->IsA<UCatFishInventoryItemInstance>()) && Instance->GetWorldActor())
			RemoveActors.Add(Instance->GetWorldActor());
		return true;
	};
	UCatInventoryComponent* Target = nullptr;
	UCatInventoryComponent* Fallback = nullptr;
	int32 Camps = 0, Stores = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (!It->HasAuthority()) continue;
		TInlineComponentArray<UCatInventoryComponent*> Inventories(*It);
		for (auto* Inventory : Inventories)
		{
			Before.Add(Inventory, Inventory->GetInventoryEntries());
			auto& Remaining = After.Add(Inventory, Inventory->GetInventoryEntries());
			for (auto& Entry : Remaining)
			{
				if (!Entry.Instance || Entry.StackCount <= 0) continue;
				if (!PriceInstance(Entry.Instance, Entry.StackCount)) return Reject(TEXT("InvalidResourceInstance"));
				if (!bGraduation || !Entry.Instance->IsA<UCatFishInventoryItemInstance>()) Entry = FCatInventoryEntry(Inventory);
			}
			auto& HeldEntries = Held.Add(Inventory);
			Inventory->AppendHeldInventoryEntriesFromAuthority(HeldEntries);
			for (const auto& Entry : HeldEntries)
				if (!PriceInstance(Entry.Instance, Entry.StackCount)) return Reject(TEXT("InvalidResourceInstance"));
			if (It->IsA<ACatCampInventoryActor>())
			{
				++Camps;
				Fallback = Inventory;
				if (Inventory->GetTeamStorageRole() == ECatTeamStorageRole::SupplyStore) { ++Stores; Target = Inventory; }
			}
		}
	}
	if (Stores > 1) Target = nullptr;
	if (!Target && Camps == 1) Target = Fallback;
	for (TActorIterator<ACatItem> It(World); It; ++It)
	{
		if (!It->IsAwaitingPickup()) continue;
		const auto Batch = It->GetPickupInventory();
		for (const auto& Entry : Batch.DefinitionEntries)
			PriceDefinition(Entry.ItemDefinition, Entry.Count);
		for (const auto& Entry : Batch.InstanceEntries)
			if (!PriceInstance(Entry.ItemInstance, Entry.Count)) return Reject(TEXT("InvalidResourceInstance"));
		RemoveActors.Add(*It);
	}
	// 部署鱼竿由 Fishing 的实例 ID 关联 held 库存，不保证使用通用 WorldActor 引用。
	for (TActorIterator<ACatFishingRodActor> It(World); It; ++It)
	{
		const auto& Rod = It->GetPresentationState();
		if (Rod.ItemInstanceId.IsValid() && !PricedIds.Contains(Rod.ItemInstanceId))
		{
			PriceDefinition(GetDefault<UCatInventorySettings>()->FindRuntimeDefinition(Rod.RodDefinitionId), 1);
			PricedIds.Add(Rod.ItemInstanceId);
		}
		RemoveActors.Add(*It);
	}
	// 地面鱼护的壳也属于剩余装备；护内鱼不折钱，提交前准备保留原身份的地面载体。
	for (TActorIterator<ACatFishGuardActor> It(World); It; ++It)
	{
		if (!RemoveActors.Contains(*It))
		{
			if (It->GuardItem)
			{
				if (!PriceInstance(It->GuardItem, 1)) return Reject(TEXT("InvalidResourceInstance"));
			}
			else PriceDefinition(It->GuardDefinition.LoadSynchronous(), 1);
			RemoveActors.Add(*It);
		}
	}
	if (EquipmentCoins > MAX_int64 - Balance) return Reject(TEXT("EquipmentValueOverflow"));
	const int64 TotalCoins = int64(Balance) + EquipmentCoins;
	// 兑换缺配只跳过最后一项，不阻止清款、装备退役与世界资源清理。
	const bool bCanExchange = bGraduation && DriedFish && DriedFish->IsInventoryRuntimeDefinitionReady()
		&& TryGetOriginalItemPrice(Settings->SettlementDriedFishDefinitionId, CoinCost) && CoinCost > 0;
	if (bGraduation && !bCanExchange)
		UE_LOG(LogCatfishing, Warning, TEXT("Event=shop_settlement_exchange_skipped World=%s NetMode=%d Authority=1 Definition=%s Result=DriedFishDefinitionOrCatalogPriceMissing DriedFish=0"),
			*GetNameSafe(World), World->GetNetMode(), *Settings->SettlementDriedFishDefinitionId.ToString());
	const int64 Count64 = bCanExchange ? TotalCoins / CoinCost : 0;
	if (Count64 > MAX_int32 || (Count64 > 0 && !Target)) return Reject(TEXT("DriedFishDeliveryUnavailable"));
	const int32 Count = int32(Count64);
	struct FPreservedFish { UCatFishInventoryItemInstance* Item; ACatFishPickupActor* Actor; AActor* OldActor; };
	TArray<FPreservedFish> PreservedFish;
	const auto CancelPreparedFish = [&]()
	{
		for (auto& Fish : PreservedFish) Fish.Actor->Destroy();
	};
	if (bGraduation)
	{
		for (AActor* Actor : RemoveActors)
		{
			auto* Guard = Cast<ACatFishGuardActor>(Actor);
			if (!Guard) continue;
			auto* Inventory = Guard->GetFishInventoryComponent();
			auto& Entries = After.FindChecked(Inventory);
			for (auto& Entry : Entries)
			{
				auto* Fish = Cast<UCatFishInventoryItemInstance>(Entry.Instance);
				if (!Fish || Entry.StackCount <= 0) continue;
				FActorSpawnParameters Spawn;
				Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				auto* Pickup = World->SpawnActor<ACatFishPickupActor>(Guard->GetActorLocation(), Guard->GetActorRotation(), Spawn);
				if (!Pickup || !Pickup->InitializeFromInventoryForCarryFromAuthority(Fish, 1))
				{
					if (Pickup) Pickup->Destroy();
					CancelPreparedFish();
					return Reject(TEXT("GuardFishPreservationFailed"));
				}
				Pickup->SetActorHiddenInGame(true);
				Pickup->SetActorEnableCollision(false);
				PreservedFish.Add({Fish, Pickup, Fish->GetWorldActor()});
				Entry = FCatInventoryEntry(Inventory);
			}
		}
	}
	const auto Rollback = [&]()
	{
		for (const auto& Pair : Before) verify(Pair.Key->ReplaceInventoryEntriesFromAuthority(Pair.Value, Pair.Value.Num(), false));
		CancelPreparedFish();
	};
	for (const auto& Pair : After)
	{
		if (!Pair.Key->ReplaceInventoryEntriesFromAuthority(Pair.Value, Pair.Value.Num(), false))
		{ Rollback(); return Reject(TEXT("ResourcePreparationFailed")); }
	}
	if (Count > 0)
	{
		FCatInventoryReceiveBatch Batch;
		auto& Entry = Batch.DefinitionEntries.AddDefaulted_GetRef();
		Entry.ItemDefinition = DriedFish;
		Entry.Count = Count;
		if (!Target->TryAddInventoryBatchInternal(Batch, false))
		{ Rollback(); return Reject(TEXT("DriedFishDeliveryCapacity")); }
	}
	int32 Delta = -Balance;
	if (!TryApplyTeamWalletTransaction(Delta, FGuid::NewGuid()))
	{ Rollback(); return Reject(TEXT("WalletClearRejected")); }
	// 后续只做已验证身份的退役与通知，不再调用可拒绝的付款/交付写口。
	bSettlementResourcesFinalized = true;
	++WalletRevision;
	for (const auto& Pair : Held)
		for (const auto& Entry : Pair.Value)
			if (!bGraduation || !Entry.Instance->IsA<UCatFishInventoryItemInstance>())
				verify(Pair.Key->RetireHeldInventoryEntryFromAuthority(Entry.Instance->GetItemInstanceId()));
	for (auto& Fish : PreservedFish)
	{
		Fish.Item->SetWorldActor(Fish.Actor);
		Fish.Item->SetRuntimeOwnerActor(Fish.Actor);
		Fish.Actor->SetActorHiddenInGame(false);
		Fish.Actor->SetActorEnableCollision(true);
		if (Fish.OldActor && Fish.OldActor != Fish.Actor) Fish.OldActor->Destroy();
	}
	if (!bGraduation)
		for (TActorIterator<ACatFishPickupActor> It(World); It; ++It) RemoveActors.Add(*It);
	for (AActor* Actor : RemoveActors) if (IsValid(Actor)) Actor->Destroy();
	for (const auto& Pair : Before) if (IsValid(Pair.Key) && !Pair.Key->GetOwner()->IsActorBeingDestroyed()) Pair.Key->BroadcastInventoryChange();
	OnShopInventoryRefreshed.Broadcast();
	UE_LOG(LogCatfishing, Log, TEXT("Event=shop_settlement_resources_finalized World=%s NetMode=%d Authority=1 Graduation=%d WalletBefore=%d Equipment=%d SkippedEquipment=%d EquipmentCoins=%lld UnitPrice=%d DriedFish=%d Discarded=%lld WalletAfter=0"),
		*GetNameSafe(World), World->GetNetMode(), bGraduation, Balance, EquipmentCount, SkippedEquipmentCount, EquipmentCoins, CoinCost, Count,
		bCanExchange ? TotalCoins % CoinCost : TotalCoins);
	return Count;
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
bool UCatShopEconomyService::RestoreWalletFromAuthority(const int32 Balance)
{
	int32 Previous = 0;
	if (Balance < 0 || Balance > 16777216 || bTransactionInProgress || !TransactionLedger.IsEmpty()
		|| !TryGetTeamWalletBalance(Previous)) return false;
	// 存档恢复不是交易收入；只在尚无交易的启动断点写回唯一 ASC 基础值。
	auto* GameState = GetWorld()->GetGameState<ACatfishingGameState>();
	auto* ASC = GameState ? GameState->GetRunAbilitySystemComponentFromAuthority() : nullptr;
	if (!ASC) return false;
	TGuardValue<bool> RestoreGuard(bTransactionInProgress, true);
	ASC->SetNumericAttributeBase(UCatEconomyAttributeSet::GetTeamWalletBalanceAttribute(), static_cast<float>(Balance));
	int32 Restored = 0;
	if (!TryGetTeamWalletBalance(Restored) || Restored != Balance)
	{
		ASC->SetNumericAttributeBase(UCatEconomyAttributeSet::GetTeamWalletBalanceAttribute(), static_cast<float>(Previous));
		UE_LOG(LogCatfishing, Warning, TEXT("Event=shop_wallet_restore_rejected World=%s NetMode=%d Authority=1 Result=ReadbackMismatch"),
			*GetNameSafe(GetWorld()), GetWorld()->GetNetMode());
		return false;
	}
	++WalletRevision;
	UE_LOG(LogCatfishing, Log, TEXT("Event=shop_wallet_restored World=%s NetMode=%d Authority=1 Balance=%d Revision=%lld"),
		*GetNameSafe(GetWorld()), GetWorld()->GetNetMode(), Balance, WalletRevision);
	OnShopInventoryRefreshed.Broadcast();
	return true;
}

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
#if WITH_DEV_AUTOMATION_TESTS
	if (FailPaymentForTest) return false;
#endif
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
		// GE 回调异常不得留下半次扣款；恢复唯一 ASC 的原基础值，外层同步恢复实物和货架。
		ASC->SetNumericAttributeBase(UCatEconomyAttributeSet::GetTeamWalletBalanceAttribute(), static_cast<float>(CurrentBalance));
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

// 重放保留首次已成交记录，按记录的来源摊位刷新货架，再读取当前公款；只改返回副本，不写库存或缓存。
// 先清空旧货架快照，已注销货架或找不到的条目直接跳过；公款及命令版本仍刷新，成交记录保持首次事实。
void UCatShopEconomyService::RefreshCartReplaySnapshots(FCatShopCartTransactionResult& Result) const
{
	Result.Stocks.Reset();
	for (FCatShopTransactionRecord& Record : Result.Transactions)
	{
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

// 清晨刷新请求号流程：把摊位身份和天序号拼成一段固定文本，再取确定性 GUID。
// 同一摊位同一天永远算出同一个号，所以哪怕清晨那一拍被重复触发，摊位侧的刷新幂等集合也只会让货架换一轮；
// 换成随机 GUID 就得靠调用方自己记「今天换过没有」，那份状态迟早和摊位实际货架对不上。
FGuid UCatShopEconomyService::MakeShopDayRefreshRequestId(const FGuid& ShopInventoryId, const int32 DayIndex)
{
	return FGuid::NewDeterministicGuid(FString::Printf(TEXT("CatShopDayRefresh|%s|%d"),
		*ShopInventoryId.ToString(EGuidFormats::DigitsWithHyphens), DayIndex));
}

// 公开交易记录构造流程：复制账本里可以公开的字段。
// ActorPlayerState 刻意留空：服务只有服务器私有 StableNetId，按项目约定它不能进复制 DTO，
// 由持有身份映射的复制挂载点在发出去之前补上公开身份。
FCatShopPublicTransaction UCatShopEconomyService::MakePublicTransaction(const FCatShopTransactionRecord& Record)
{
	FCatShopPublicTransaction Public;
	Public.TransactionId = Record.TransactionId;
	Public.CartId = Record.CartId;
	Public.Items = Record.Items;
	Public.bContainsGiantFish = Record.bContainsGiantFish;
	Public.bPurchase = Record.bPurchase;
	Public.bFishSale = Record.bFishSale;
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
// 1. 正常购物车先按购买写口相同规则归一化，再冻结来源摊位和 EntryId/选购次数；钱包快照不属于购买意图。
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
	return FString::Printf(TEXT("Shop=%s|Normalized=%s|Lines=%s"),
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
