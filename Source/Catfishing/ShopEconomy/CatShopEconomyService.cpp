#include "ShopEconomy/CatShopEconomyService.h"

#include "Logging/CatLog.h"
#include "ShopEconomy/CatShopInventoryComponent.h"
#include "ShopEconomy/CatShopEconomySettings.h"

// 创建条件流程：只允许服务器 Game World 拥有可写经济事实；客户端不能生成第二份公款或库存。
bool UCatShopEconomyService::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

// 初始化流程：先交父类，再从 Settings 冻结本局公款、命令 gate 和售鱼最小金额；摊位货架目录由各自库存组件进入 World 时注册。
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
	return Wallet;
}

// 库存读取流程：先清输出，再从指定摊位库存读取 EntryId；服务不再维护全局货架 Map。
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

// 重放判定流程：用与 CommitCatalogTransaction 完全相同的三段拼出幂等键，再只查终态表是否已有该键。
// 键的拼法必须和购买写口逐字一致，否则协调器会以为是首次、白跑一趟交付前置校验；这也是它没有独立成一套判据的原因。
// 只读：不比对载荷签名（载荷不一致由购买写口自己判 InvalidPayload），不看成败，不改任何状态。
bool UCatShopEconomyService::HasCatalogTransactionTerminal(const FCatShopPurchaseCommand& Command,
	const bool bFreeClaim) const
{
	const FString CacheKey = MakeTerminalKey(Command.Context.StableNetId,
		bFreeClaim ? TEXT("FreeClaim") : TEXT("Purchase"),
		Command.Context.RequestId);
	return TerminalCache.Contains(CacheKey);
}

// 账本读取流程：复制本局交易记录；广播层可展示金额和交付状态，但不能绕过确认入口直接改服务内数组。
TArray<FCatShopTransactionRecord> UCatShopEconomyService::GetTransactionLedgerSnapshot() const
{
	return TransactionLedger;
}

// 购买入口流程：只声明普通购买类别，具体公款/摊位库存/账本提交交给共用提交流程。
FCatShopTransactionResult UCatShopEconomyService::PurchaseCatalogEntry(const FCatShopPurchaseCommand& Command,
	UCatShopInventoryComponent* ShopInventory)
{
	return CommitCatalogTransaction(Command, ECatShopTransactionKind::Purchase, ShopInventory);
}

// 免费自取流程：把来源摊位和 EntryId 策略也交给目录交易统一缓存；错误条目的同 RequestId 后续不能换成正确条目复活。
FCatShopTransactionResult UCatShopEconomyService::ClaimFreeCatalogEntry(const FCatShopPurchaseCommand& Command,
	UCatShopInventoryComponent* ShopInventory)
{
	return CommitCatalogTransaction(Command, ECatShopTransactionKind::FreeClaim, ShopInventory);
}

// 估价流程：runtime 未就绪或收鱼价没被裁定时直接失败，否则用开局冻结的档位表求值。
// 这里不做任何兜底：飞书没给斜率，返回一个编出来的价钱比让玩家暂时卖不了鱼危险得多。
bool UCatShopEconomyService::TryAppraiseFishSale(const double WeightKilograms, int32& OutSaleValue) const
{
	OutSaleValue = 0;
	if (!bRuntimeReady || !bFishPurchasePriceDecided)
	{
		return false;
	}
	return UCatShopEconomySettings::TryEvaluateFishPurchasePrice(FishPurchasePriceAnchors, WeightKilograms, OutSaleValue);
}

// 售鱼预检流程：在 Social 删除 escrow 前只读验证同一售鱼载荷是否能进入公款；这里不写账本，避免预检本身变成第二个提交点。
bool UCatShopEconomyService::ValidateFishSale(const FCatShopFishSaleCommand& Command, ECatDomainCommandError& OutError,
	int64& OutCurrentWalletRevision) const
{
	OutError = ECatDomainCommandError::None;
	OutCurrentWalletRevision = Wallet.Revision;
	if (!Command.Context.RequestId.IsValid() || Command.Context.StableNetId.IsEmpty()
		|| !Command.FishInstanceId.IsValid() || !Command.ItemsCommitId.IsValid()
		|| Command.SourceKind == ECatShopFishSaleSource::Unknown)
	{
		OutError = ECatDomainCommandError::InvalidPayload;
		return false;
	}
	const FString CacheKey = MakeTerminalKey(Command.Context.StableNetId, TEXT("FishSale"), Command.Context.RequestId);
	const FString PayloadSignature = MakeFishSalePayloadSignature(Command);
	if (TerminalCache.Contains(CacheKey))
	{
		OutError = DoesTerminalPayloadMatch(CacheKey, PayloadSignature)
			? ECatDomainCommandError::AlreadyResolved : ECatDomainCommandError::InvalidPayload;
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
	if (Command.Context.ExpectedRevision != Wallet.Revision)
	{
		OutError = ECatDomainCommandError::RevisionConflict;
		return false;
	}
	int32 AppraisedValue = 0;
	if (!TryAppraiseFishSale(Command.WeightKilograms, AppraisedValue))
	{
		OutError = ECatDomainCommandError::PolicyUndecided;
		return false;
	}
	if (Command.SaleValue != AppraisedValue || Command.SaleValue < MinimumFishSaleValue)
	{
		OutError = ECatDomainCommandError::InvalidPayload;
		return false;
	}
	return true;
}

// 售鱼入账流程：先要求身份、鱼实例、Items 提交证据和来源都在，再按公款版本并发，最后用重量自己估一次价并和调用方报价核对。
// 鱼的删除仍然必须先由 Items 完成，这里不碰鱼；价格则相反，只认服务器估出来的那个数。
FCatShopTransactionResult UCatShopEconomyService::ApplyFishSale(const FCatShopFishSaleCommand& Command)
{
	FCatShopTransactionResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	Result.Wallet = Wallet;
	if (!Command.Context.RequestId.IsValid() || Command.Context.StableNetId.IsEmpty()
		|| !Command.FishInstanceId.IsValid() || !Command.ItemsCommitId.IsValid()
		|| Command.SourceKind == ECatShopFishSaleSource::Unknown)
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		Result.Command.Revision = Wallet.Revision;
		return Result;
	}
	const FString CacheKey = MakeTerminalKey(Command.Context.StableNetId, TEXT("FishSale"), Command.Context.RequestId);
	const FString PayloadSignature = MakeFishSalePayloadSignature(Command);
	if (const FCatShopTransactionResult* Cached = TerminalCache.Find(CacheKey))
	{
		if (!DoesTerminalPayloadMatch(CacheKey, PayloadSignature))
		{
			Result.Command.Error = ECatDomainCommandError::InvalidPayload;
			Result.Command.Revision = Wallet.Revision;
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
		Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	if (!bRuntimeReady)
	{
		Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
		Result.Command.Revision = Wallet.Revision;
		CacheTerminalResult(CacheKey, PayloadSignature, Result);
		return Result;
	}
	if (!bCommandsOpen)
	{
		Result.Command.Error = ECatDomainCommandError::CommandsClosed;
		Result.Command.Revision = Wallet.Revision;
		CacheTerminalResult(CacheKey, PayloadSignature, Result);
		return Result;
	}
	if (Command.Context.ExpectedRevision != Wallet.Revision)
	{
		Result.Command.Error = ECatDomainCommandError::RevisionConflict;
		Result.Command.Revision = Wallet.Revision;
		CacheTerminalResult(CacheKey, PayloadSignature, Result);
		return Result;
	}
	int32 AppraisedValue = 0;
	if (!TryAppraiseFishSale(Command.WeightKilograms, AppraisedValue))
	{
		// 收鱼价没被裁定时整笔拒绝，而不是按调用方报价入账：调用方报价只是它从同一个估价接口取回来的回声，
		// 一旦这里放行，公款余额就会变成客户端能提出的任意数字。
		Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
		Result.Command.Revision = Wallet.Revision;
		CacheTerminalResult(CacheKey, PayloadSignature, Result);
		return Result;
	}
	if (Command.SaleValue != AppraisedValue || Command.SaleValue < MinimumFishSaleValue)
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		Result.Command.Revision = Wallet.Revision;
		CacheTerminalResult(CacheKey, PayloadSignature, Result);
		return Result;
	}

	Wallet.Balance += Command.SaleValue;
	++Wallet.Revision;
	FCatShopTransactionRecord& Record = TransactionLedger.AddDefaulted_GetRef();
	Record.TransactionId = FGuid::NewGuid();
	Record.RequestId = Command.Context.RequestId;
	Record.StableNetId = Command.Context.StableNetId;
	Record.Kind = ECatShopTransactionKind::FishSale;
	Record.DeliveryState = ECatShopDeliveryState::None;
	Record.FishInstanceId = Command.FishInstanceId;
	Record.FishSource = Command.SourceKind;
	Record.WalletDelta = Command.SaleValue;
	Record.WalletRevision = Wallet.Revision;
	Result.Command.bCommitted = true;
	Result.Command.Error = ECatDomainCommandError::None;
	Result.Command.Revision = Wallet.Revision;
	Result.Wallet = Wallet;
	Result.Transaction = Record;
	CacheTerminalResult(CacheKey, PayloadSignature, Result);
	OnPublicTransactionCommitted.Broadcast(MakePublicTransaction(Record));
	return Result;
}

// 交付确认流程：先按确认 RequestId 重放，再用 TransactionId 找到原订单；只允许原买家用真实下游回执把 Pending 推进到 Delivered。
FCatShopTransactionResult UCatShopEconomyService::ConfirmTransactionDelivery(
	const FCatShopDeliveryConfirmationCommand& Command)
{
	FCatShopTransactionResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	Result.Wallet = Wallet;
	if (!Command.Context.RequestId.IsValid() || Command.Context.StableNetId.IsEmpty()
		|| !Command.TransactionId.IsValid() || !Command.DeliveryReceiptId.IsValid()
		|| Command.DeliveryRevision <= 0)
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		Result.Command.Revision = Wallet.Revision;
		return Result;
	}
	const FString CacheKey = MakeTerminalKey(Command.Context.StableNetId, TEXT("Delivery"), Command.Context.RequestId);
	const FString PayloadSignature = MakeDeliveryPayloadSignature(Command);
	if (const FCatShopTransactionResult* Cached = TerminalCache.Find(CacheKey))
	{
		if (!DoesTerminalPayloadMatch(CacheKey, PayloadSignature))
		{
			Result.Command.Error = ECatDomainCommandError::InvalidPayload;
			Result.Command.Revision = Wallet.Revision;
			return Result;
		}
		Result = *Cached;
		Result.Command.bCommitted = false;
		Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
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
		else if (Record->DeliveryState == ECatShopDeliveryState::Delivered)
		{
			Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
		}
		else if (Record->DeliveryState != ECatShopDeliveryState::Pending
			|| Record->EntryKind == ECatShopEntryKind::Unknown)
		{
			Result.Command.Error = ECatDomainCommandError::InvalidPhase;
		}
		else
		{
			Record->DeliveryState = ECatShopDeliveryState::Delivered;
			Record->DeliveryReceiptId = Command.DeliveryReceiptId;
			Record->DeliveryRevision = Command.DeliveryRevision;
			Result.Command.bCommitted = true;
			Result.Command.Error = ECatDomainCommandError::None;
			Result.Transaction = *Record;
		}
	}
	Result.Command.Revision = Wallet.Revision;
	Result.Wallet = Wallet;
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
// 3. 成功后公款、账本和交易幂等缓存原样保留；公开快照会通过组件变化订阅刷新。
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
	Snapshot.WalletRevision = Wallet.Revision;
	Snapshot.Balance = Wallet.Balance;
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
			// 解析不到就保持空：那说明这笔交易记录的操作者已经离局或还没进入 Active。
			// 与其挑一个还在场的人顶上，不如让表现层显示未知操作者。
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

// 设置加载流程：清空旧交易事实后读取默认对象；公款和售鱼价仍是局级配置，商店货架库存由每个摊位库存组件自己生成。
void UCatShopEconomyService::LoadRuntimeEconomyFromSettings()
{
	Wallet = FCatShopWalletSnapshot();
	TransactionLedger.Reset();
	TerminalCache.Reset();
	TerminalPayloadByKey.Reset();
	bCommandsOpen = true;
	CurrentShopDayIndex = 0;
	const UCatShopEconomySettings* Settings = GetDefault<UCatShopEconomySettings>();
	bRuntimeReady = Settings && Settings->IsRuntimeEnabled();
	if (!Settings)
	{
		return;
	}
	// 没裁过起始资金时 StartingTeamWalletBalance 是哨兵 -1，此时 bRuntimeReady 已经是 false，四个写口全部 fail-closed；
	// 这里仍夹到 0 只是为了不让快照对外暴露一个负余额，不代表哨兵被当成"裁定 0 元"接受了。
	Wallet.Balance = FMath::Max(0, Settings->StartingTeamWalletBalance);
	Wallet.Revision = 1;
	MinimumFishSaleValue = FMath::Max(1, Settings->MinimumFishSaleValue);
	// 收鱼价单独一个 gate：买东西不依赖鱼价，所以这里只冻结售鱼这一路的裁定状态和档位表，
	// 不把它并进 bRuntimeReady，否则没填鱼价会连购买和免费自取一起关掉。
	bFishPurchasePriceDecided = Settings->FishPurchasePricePolicy == ECatDomainPolicy::Enabled;
	FishPurchasePriceAnchors = Settings->FishPurchasePriceAnchors;
}

// 目录交易流程：
// 1. 先验证 RequestId、玩家身份、EntryId 和 ShopInventoryId，再用身份+操作+RequestId 查幂等缓存；重放只返回首笔终态并重读当前摊位库存快照。
// 2. 首次命令继续验证来源摊位身份、免费白名单、经济 runtime、摊位目录状态、命令门、当前货架条目、公款版本和未裁定上架条件。
// 3. 免费领取必须来自来源摊位白名单且价格为 0、库存无限；普通购买只认摊位表里的单价，并检查当前团队公款够不够。
// 4. 写入时先让摊位库存扣减当前 EntryId，再扣团队公款、追加账本、缓存终态，最后广播公开交易；因此客户端看到的是库存、公款和账本同一笔结果。
FCatShopTransactionResult UCatShopEconomyService::CommitCatalogTransaction(const FCatShopPurchaseCommand& Command,
	const ECatShopTransactionKind TransactionKind, UCatShopInventoryComponent* ShopInventory)
{
	FCatShopTransactionResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	Result.Wallet = Wallet;
	if (!Command.Context.RequestId.IsValid() || Command.Context.StableNetId.IsEmpty()
		|| Command.EntryId.IsNone() || !Command.ShopInventoryId.IsValid())
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		Result.Command.Revision = Wallet.Revision;
		return Result;
	}
	const FString CacheKey = MakeTerminalKey(Command.Context.StableNetId,
		TransactionKind == ECatShopTransactionKind::FreeClaim ? TEXT("FreeClaim") : TEXT("Purchase"),
		Command.Context.RequestId);
	const FString PayloadSignature = MakeCatalogPayloadSignature(Command, TransactionKind);
	if (const FCatShopTransactionResult* Cached = TerminalCache.Find(CacheKey))
	{
		if (!DoesTerminalPayloadMatch(CacheKey, PayloadSignature))
		{
			Result.Command.Error = ECatDomainCommandError::InvalidPayload;
			Result.Command.Revision = Wallet.Revision;
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
				if (const UCatShopInventoryComponent* CurrentInventory =
					FindRegisteredShopInventoryById(CurrentRecord->ShopInventoryId))
				{
					CurrentInventory->TryGetStockSnapshot(CurrentRecord->EntryId, Result.Stock);
				}
			}
		}
		Result.Command.bCommitted = false;
		Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	if (!ShopInventory || ShopInventory->GetShopInventoryId() != Command.ShopInventoryId)
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		Result.Command.Revision = Wallet.Revision;
		CacheTerminalResult(CacheKey, PayloadSignature, Result);
		return Result;
	}
	ShopInventory->TryGetStockSnapshot(Command.EntryId, Result.Stock);
	if (TransactionKind == ECatShopTransactionKind::FreeClaim && !IsFreeClaimEntry(Command.EntryId, ShopInventory))
	{
		Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
		Result.Command.Revision = Wallet.Revision;
		CacheTerminalResult(CacheKey, PayloadSignature, Result);
		return Result;
	}
	if (!bRuntimeReady || !ShopInventory->IsRuntimeCatalogReady())
	{
		Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
		Result.Command.Revision = Wallet.Revision;
		CacheTerminalResult(CacheKey, PayloadSignature, Result);
		return Result;
	}
	if (!bCommandsOpen)
	{
		Result.Command.Error = ECatDomainCommandError::CommandsClosed;
		Result.Command.Revision = Wallet.Revision;
		CacheTerminalResult(CacheKey, PayloadSignature, Result);
		return Result;
	}
	FCatShopCatalogEntry Entry;
	if (!ShopInventory->TryGetCatalogEntry(Command.EntryId, Entry))
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
		Result.Command.Revision = Wallet.Revision;
		CacheTerminalResult(CacheKey, PayloadSignature, Result);
		return Result;
	}
	if (Command.Context.ExpectedRevision != Wallet.Revision)
	{
		Result.Command.Error = ECatDomainCommandError::RevisionConflict;
		Result.Command.Revision = Wallet.Revision;
		CacheTerminalResult(CacheKey, PayloadSignature, Result);
		return Result;
	}
	// 运行目录构建已经拒绝带商店解锁条件的条目；这里再守一次交易写口，防止热更或迁移期数据绕过目录初始化。
	if (!Entry.RequiredShopUnlockId.IsNone())
	{
		Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
		Result.Command.Revision = Wallet.Revision;
		CacheTerminalResult(CacheKey, PayloadSignature, Result);
		return Result;
	}
	// 免费自取要求价格 0 且库存无限：基础饵、1 级竿和 1 级漂都是保底项，
	// 有限库存的"免费"品会在某天领光，基础件一旦领光就不再是保底，所以这两条一起当准入条件。
	if (TransactionKind == ECatShopTransactionKind::FreeClaim
		&& (Entry.UnitPrice != 0 || !Entry.bUnlimitedStock))
	{
		Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
		Result.Command.Revision = Wallet.Revision;
		CacheTerminalResult(CacheKey, PayloadSignature, Result);
		return Result;
	}
	if (!Entry.bUnlimitedStock && Result.Stock.RemainingStock <= 0)
	{
		Result.Command.Error = ECatDomainCommandError::CapacityExceeded;
		Result.Command.Revision = Wallet.Revision;
		CacheTerminalResult(CacheKey, PayloadSignature, Result);
		return Result;
	}
	if (Wallet.Balance < Entry.UnitPrice)
	{
		Result.Command.Error = ECatDomainCommandError::CapacityExceeded;
		Result.Command.Revision = Wallet.Revision;
		CacheTerminalResult(CacheKey, PayloadSignature, Result);
		return Result;
	}

	FCatShopStockSnapshot CommittedStock;
	if (!ShopInventory->ConsumeCatalogEntryFromAuthority(Command.EntryId, CommittedStock))
	{
		Result.Command.Error = ECatDomainCommandError::CapacityExceeded;
		Result.Command.Revision = Wallet.Revision;
		Result.Stock = CommittedStock;
		CacheTerminalResult(CacheKey, PayloadSignature, Result);
		return Result;
	}

	Wallet.Balance -= Entry.UnitPrice;
	if (Entry.UnitPrice != 0)
	{
		++Wallet.Revision;
	}
	FCatShopTransactionRecord& Record = TransactionLedger.AddDefaulted_GetRef();
	Record.TransactionId = FGuid::NewGuid();
	Record.RequestId = Command.Context.RequestId;
	Record.StableNetId = Command.Context.StableNetId;
	Record.Kind = TransactionKind;
	Record.EntryKind = Entry.Kind;
	Record.DeliveryState = ECatShopDeliveryState::Pending;
	Record.EntryId = Entry.EntryId;
	Record.ShopInventoryId = Command.ShopInventoryId;
	Record.DefinitionId = Entry.DefinitionId;
	Record.PurchaseQuantity = Entry.PurchaseQuantity;
	Record.WalletDelta = -Entry.UnitPrice;
	Record.WalletRevision = Wallet.Revision;
	Record.StockRevision = CommittedStock.Revision;
	Result.Command.bCommitted = true;
	Result.Command.Error = ECatDomainCommandError::None;
	Result.Command.Revision = Wallet.Revision;
	Result.Wallet = Wallet;
	Result.Stock = CommittedStock;
	Result.Transaction = Record;
	CacheTerminalResult(CacheKey, PayloadSignature, Result);
	OnPublicTransactionCommitted.Broadcast(MakePublicTransaction(Record));
	return Result;
}

// 公开交易记录构造流程：复制账本里可以公开的字段。
// ActorPlayerState 刻意留空：服务只有服务器私有 StableNetId，按项目约定它不能进复制 DTO，
// 由持有身份映射的复制挂载点在发出去之前补上公开身份。
FCatShopPublicTransaction UCatShopEconomyService::MakePublicTransaction(const FCatShopTransactionRecord& Record)
{
	FCatShopPublicTransaction Public;
	Public.TransactionId = Record.TransactionId;
	Public.Kind = Record.Kind;
	Public.EntryId = Record.EntryId;
	Public.ShopInventoryId = Record.ShopInventoryId;
	Public.DefinitionId = Record.DefinitionId;
	Public.PurchaseQuantity = Record.PurchaseQuantity;
	Public.FishSource = Record.FishSource;
	Public.WalletDelta = Record.WalletDelta;
	Public.DeliveryState = Record.DeliveryState;
	return Public;
}

// 免费自取判定流程：只认来源摊位库存组件上的三条显式配置项；不按"单价为 0"反推，避免漏填价格变成白拿。
bool UCatShopEconomyService::IsFreeClaimEntry(const FName EntryId,
	const UCatShopInventoryComponent* ShopInventory) const
{
	if (EntryId.IsNone() || !ShopInventory)
	{
		return false;
	}
	return ShopInventory->IsFreeClaimEntry(EntryId);
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

// 目录载荷签名流程：冻结交易类别、公款前提、来源摊位库存和 EntryId；价格与库存来自摊位组件，不接受客户端提交。
FString UCatShopEconomyService::MakeCatalogPayloadSignature(const FCatShopPurchaseCommand& Command,
	const ECatShopTransactionKind TransactionKind)
{
	return FString::Printf(TEXT("Kind=%d|Expected=%lld|Shop=%s|Entry=%s"), static_cast<int32>(TransactionKind),
		Command.Context.ExpectedRevision, *Command.ShopInventoryId.ToString(EGuidFormats::DigitsWithHyphens),
		*Command.EntryId.ToString());
}

// 售鱼载荷签名流程：冻结公款前提、鱼实例、Items 提交证据、来源、重量和估值；同 RequestId 改任一项都不是合法重放。
// 重量必须进签名：它是收购价的唯一输入，同一个 RequestId 换一条更重的鱼重放就等于换了一笔生意。
FString UCatShopEconomyService::MakeFishSalePayloadSignature(const FCatShopFishSaleCommand& Command)
{
	return FString::Printf(TEXT("Expected=%lld|Fish=%s|ItemsCommit=%s|Source=%d|Weight=%.6f|Value=%d"),
		Command.Context.ExpectedRevision,
		*Command.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*Command.ItemsCommitId.ToString(EGuidFormats::DigitsWithHyphens),
		static_cast<int32>(Command.SourceKind), Command.WeightKilograms, Command.SaleValue);
}

// 交付载荷签名流程：冻结原交易、下游回执、下游版本和公款前提；回执漂移必须拒绝而不是重放。
FString UCatShopEconomyService::MakeDeliveryPayloadSignature(const FCatShopDeliveryConfirmationCommand& Command)
{
	return FString::Printf(TEXT("Expected=%lld|Transaction=%s|Receipt=%s|DeliveryRevision=%lld"),
		Command.Context.ExpectedRevision, *Command.TransactionId.ToString(EGuidFormats::DigitsWithHyphens),
		*Command.DeliveryReceiptId.ToString(EGuidFormats::DigitsWithHyphens), Command.DeliveryRevision);
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
