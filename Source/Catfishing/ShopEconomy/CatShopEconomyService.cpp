#include "ShopEconomy/CatShopEconomyService.h"

#include "ShopEconomy/CatShopEconomySettings.h"

// 创建条件流程：只允许服务器 Game World 拥有可写经济事实；客户端不能生成第二份钱包或库存。
bool UCatShopEconomyService::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

// 初始化流程：先交父类，再从 Settings 冻结本局钱包、目录、免费饵和售鱼最小金额；目录非法时服务仍存在但购买路径关闭。
void UCatShopEconomyService::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadRuntimeEconomyFromSettings();
}

// 反初始化流程：先关闭新交易，再清除仅属于本 World 的库存、账本和终态缓存；不把团队钱包带入下一局。
void UCatShopEconomyService::Deinitialize()
{
	CloseCommands();
	StockByEntryId.Reset();
	TransactionLedger.Reset();
	TerminalCache.Reset();
	TerminalPayloadByKey.Reset();
	Super::Deinitialize();
}

// 钱包读取流程：返回当前唯一团队钱包快照副本；调用方不能借引用改余额。
FCatShopWalletSnapshot UCatShopEconomyService::GetWalletSnapshot() const
{
	return Wallet;
}

// 库存读取流程：先清输出，再按稳定 EntryId 返回本局库存；目录不可用或缺项都不制造占位。
bool UCatShopEconomyService::TryGetStockSnapshot(const FName EntryId, FCatShopStockSnapshot& OutSnapshot) const
{
	OutSnapshot = FCatShopStockSnapshot();
	const FStockRecord* StockRecord = StockByEntryId.Find(EntryId);
	if (!bCatalogReady || !StockRecord)
	{
		return false;
	}
	OutSnapshot = MakeStockSnapshot(StockRecord);
	return true;
}

// 目录项读取流程：先清输出，再按稳定 EntryId 取回开局冻结的那份配置；目录不可用或缺项都不制造占位。
// 和 TryGetStockSnapshot 分开是因为两者答的不是一个问题：那个答"还剩几件"，这个答"这一项是什么、交给谁"。
bool UCatShopEconomyService::TryGetCatalogEntry(const FName EntryId, FCatShopCatalogEntry& OutEntry) const
{
	OutEntry = FCatShopCatalogEntry();
	const FStockRecord* StockRecord = StockByEntryId.Find(EntryId);
	if (!bCatalogReady || !StockRecord)
	{
		return false;
	}
	OutEntry = StockRecord->Entry;
	return true;
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

// 购买入口流程：只声明普通购买类别，具体钱包/库存/账本提交交给共用提交流程。
FCatShopTransactionResult UCatShopEconomyService::PurchaseCatalogEntry(const FCatShopPurchaseCommand& Command)
{
	return CommitCatalogTransaction(Command, ECatShopTransactionKind::Purchase);
}

// 免费自取流程：把 EntryId 策略也交给目录交易统一缓存；这样错误条目的同 RequestId 后续不能换成正确条目复活。
FCatShopTransactionResult UCatShopEconomyService::ClaimFreeCatalogEntry(const FCatShopPurchaseCommand& Command)
{
	return CommitCatalogTransaction(Command, ECatShopTransactionKind::FreeClaim);
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

// 售鱼预检流程：在 Social 删除 escrow 前只读验证同一售鱼载荷是否能进入钱包；这里不写账本，避免预检本身变成第二个提交点。
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

// 售鱼入账流程：先要求身份、鱼实例、Items 提交证据和来源都在，再按钱包版本并发，最后用重量自己估一次价并和调用方报价核对。
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
				Result.Stock = MakeStockSnapshot(StockByEntryId.Find(CurrentRecord->EntryId));
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
		// 一旦这里放行，钱包余额就会变成客户端能提出的任意数字。
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
		Result.Stock = MakeStockSnapshot(StockByEntryId.Find(Record->EntryId));
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
		// 交付确认不动钱包，但它把订单从"已付款待取"推到"已到货"，这是全队都要看到的状态变化，
		// 所以和购买、售鱼走同一条广播；订阅方据此更新同一条流水，而不是新增一条。
		OnPublicTransactionCommitted.Broadcast(MakePublicTransaction(Result.Transaction));
	}
	return Result;
}

// 每日进货流程：先确认经济、目录和写口都还开着，再确认这次确实是新的一天；
// 然后只把标了每日进货的条目重置回当日进货量，并推进它们各自的库存 Revision，让手上拿着旧库存版本的客户端知道要重读。
// 无限库存条目不动：它们表达的是飞书"竿与基础补给永不缺货"，本来就没有会被消耗掉的剩余量。
// 没标每日进货的限量条目也不动：那是一局只进一次的货，每天补它等于凭空把一局限量改成每日限量。
bool UCatShopEconomyService::AdvanceShopDay(const int32 NewDayIndex)
{
	if (!bRuntimeReady || !bCatalogReady || !bCommandsOpen || NewDayIndex <= CurrentShopDayIndex)
	{
		return false;
	}
	CurrentShopDayIndex = NewDayIndex;
	for (TPair<FName, FStockRecord>& Pair : StockByEntryId)
	{
		if (!Pair.Value.Entry.bDailyRestock)
		{
			continue;
		}
		Pair.Value.RemainingStock = Pair.Value.Entry.DailyRestockQuantity;
		++Pair.Value.Revision;
	}
	return true;
}

// 公开快照流程：把当前钱包、商店天序号和整本账本逐条转成对外形态。
// 这里不做分页或裁剪：本局账本的条数由玩家实际交易次数决定，规模和一局的容器快照同量级；
// 真到了需要限量的时候，该由复制挂载点按自己的带宽策略截断，而不是让服务先把事实丢掉。
FCatShopPublicEconomySnapshot UCatShopEconomyService::BuildPublicSnapshot(
	const TFunction<APlayerState*(const FString&)>& ResolveActorPlayerState) const
{
	FCatShopPublicEconomySnapshot Snapshot;
	Snapshot.WalletRevision = Wallet.Revision;
	Snapshot.Balance = Wallet.Balance;
	Snapshot.ShopDayIndex = CurrentShopDayIndex;
	Snapshot.Transactions.Reserve(TransactionLedger.Num());
	for (const FCatShopTransactionRecord& Record : TransactionLedger)
	{
		FCatShopPublicTransaction PublicTransaction = MakePublicTransaction(Record);
		if (ResolveActorPlayerState)
		{
			// 解析不到就保持空：那说明这笔流水的操作者已经离局或还没进入 Active。
			// 与其挑一个还在场的人顶上，不如让表现层显示未知操作者。
			PublicTransaction.ActorPlayerState = ResolveActorPlayerState(Record.StableNetId);
		}
		Snapshot.Transactions.Add(MoveTemp(PublicTransaction));
	}
	return Snapshot;
}

// 关闭流程：只把新命令 gate 置否，不清钱包、库存、账本和 TerminalCache；因此收摊后查询仍读得到本局经济事实，网络重试
// 也仍能拿回首次终态，而四个写口和每日进货都会在各自 gate 处停下。这里不需要第二个“冻结但未 teardown”的中间态，冻结与
// 收口本来就是同一件事，差别只在调用时机。
void UCatShopEconomyService::CloseCommands()
{
	bCommandsOpen = false;
}

// 设置加载流程：清空旧事实后读取默认对象；合法目录转成库存记录，重复或非法项让购买路径整体关闭但不影响售鱼 runtime gate 暴露。
void UCatShopEconomyService::LoadRuntimeEconomyFromSettings()
{
	Wallet = FCatShopWalletSnapshot();
	StockByEntryId.Reset();
	TransactionLedger.Reset();
	TerminalCache.Reset();
	TerminalPayloadByKey.Reset();
	bCommandsOpen = true;
	bCatalogReady = true;
	const UCatShopEconomySettings* Settings = GetDefault<UCatShopEconomySettings>();
	bRuntimeReady = Settings && Settings->IsRuntimeEnabled();
	if (!Settings)
	{
		bCatalogReady = false;
		return;
	}
	// 没裁过起始资金时 StartingTeamWalletBalance 是哨兵 -1，此时 bRuntimeReady 已经是 false，四个写口全部 fail-closed；
	// 这里仍夹到 0 只是为了不让快照对外暴露一个负余额，不代表哨兵被当成"裁定 0 元"接受了。
	Wallet.Balance = FMath::Max(0, Settings->StartingTeamWalletBalance);
	Wallet.Revision = 1;
	FreeOrdinaryBaitEntryId = Settings->FreeOrdinaryBaitEntryId;
	FreeStarterRodEntryId = Settings->FreeStarterRodEntryId;
	MinimumFishSaleValue = FMath::Max(1, Settings->MinimumFishSaleValue);
	// 收鱼价单独一个 gate：买东西不依赖鱼价，所以这里只冻结售鱼这一路的裁定状态和档位表，
	// 不把它并进 bRuntimeReady，否则没填鱼价会连购买和免费自取一起关掉。
	bFishPurchasePriceDecided = Settings->FishPurchasePricePolicy == ECatDomainPolicy::Enabled;
	FishPurchasePriceAnchors = Settings->FishPurchasePriceAnchors;
	CurrentShopDayIndex = 0;
	for (const FCatShopCatalogEntry& Entry : Settings->CatalogEntries)
	{
		if (!Entry.IsRuntimeReady() || StockByEntryId.Contains(Entry.EntryId))
		{
			bCatalogReady = false;
			continue;
		}
		FStockRecord& Stock = StockByEntryId.Add(Entry.EntryId);
		Stock.Entry = Entry;
		Stock.RemainingStock = Entry.InitialStock;
		Stock.Revision = 1;
	}
}

// 目录交易流程：验证 runtime、命令、幂等缓存、钱包版本、库存、价格和免费饵约束；成功只写钱包、库存和账本。
FCatShopTransactionResult UCatShopEconomyService::CommitCatalogTransaction(const FCatShopPurchaseCommand& Command,
	const ECatShopTransactionKind TransactionKind)
{
	FCatShopTransactionResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	Result.Wallet = Wallet;
	if (!Command.Context.RequestId.IsValid() || Command.Context.StableNetId.IsEmpty() || Command.EntryId.IsNone())
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
				Result.Stock = MakeStockSnapshot(StockByEntryId.Find(CurrentRecord->EntryId));
			}
		}
		Result.Command.bCommitted = false;
		Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	FStockRecord* StockRecord = StockByEntryId.Find(Command.EntryId);
	Result.Stock = MakeStockSnapshot(StockRecord);
	if (TransactionKind == ECatShopTransactionKind::FreeClaim && !IsFreeClaimEntry(Command.EntryId))
	{
		Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
		Result.Command.Revision = Wallet.Revision;
		CacheTerminalResult(CacheKey, PayloadSignature, Result);
		return Result;
	}
	if (!bRuntimeReady || !bCatalogReady)
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
	if (!StockRecord)
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
	// 免费自取要求价格 0 且库存无限：飞书对普通饵和 1 级竿写的都是免费不限量，
	// 有限库存的"免费"品会在某天领光，保底竿一旦领光就不再是保底，所以这两条一起当准入条件。
	if (TransactionKind == ECatShopTransactionKind::FreeClaim
		&& (StockRecord->Entry.UnitPrice != 0 || !StockRecord->Entry.bUnlimitedStock))
	{
		Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
		Result.Command.Revision = Wallet.Revision;
		CacheTerminalResult(CacheKey, PayloadSignature, Result);
		return Result;
	}
	if (!StockRecord->Entry.bUnlimitedStock && StockRecord->RemainingStock <= 0)
	{
		Result.Command.Error = ECatDomainCommandError::CapacityExceeded;
		Result.Command.Revision = Wallet.Revision;
		Result.Stock = MakeStockSnapshot(StockRecord);
		CacheTerminalResult(CacheKey, PayloadSignature, Result);
		return Result;
	}
	if (Wallet.Balance < StockRecord->Entry.UnitPrice)
	{
		Result.Command.Error = ECatDomainCommandError::CapacityExceeded;
		Result.Command.Revision = Wallet.Revision;
		Result.Stock = MakeStockSnapshot(StockRecord);
		CacheTerminalResult(CacheKey, PayloadSignature, Result);
		return Result;
	}

	Wallet.Balance -= StockRecord->Entry.UnitPrice;
	if (StockRecord->Entry.UnitPrice != 0)
	{
		++Wallet.Revision;
	}
	if (!StockRecord->Entry.bUnlimitedStock)
	{
		--StockRecord->RemainingStock;
		++StockRecord->Revision;
	}
	FCatShopTransactionRecord& Record = TransactionLedger.AddDefaulted_GetRef();
	Record.TransactionId = FGuid::NewGuid();
	Record.RequestId = Command.Context.RequestId;
	Record.StableNetId = Command.Context.StableNetId;
	Record.Kind = TransactionKind;
	Record.EntryKind = StockRecord->Entry.Kind;
	Record.DeliveryState = ECatShopDeliveryState::Pending;
	Record.EntryId = StockRecord->Entry.EntryId;
	Record.DefinitionId = StockRecord->Entry.DefinitionId;
	Record.WalletDelta = -StockRecord->Entry.UnitPrice;
	Record.WalletRevision = Wallet.Revision;
	Record.StockRevision = StockRecord->Revision;
	Result.Command.bCommitted = true;
	Result.Command.Error = ECatDomainCommandError::None;
	Result.Command.Revision = Wallet.Revision;
	Result.Wallet = Wallet;
	Result.Stock = MakeStockSnapshot(StockRecord);
	Result.Transaction = Record;
	CacheTerminalResult(CacheKey, PayloadSignature, Result);
	OnPublicTransactionCommitted.Broadcast(MakePublicTransaction(Record));
	return Result;
}

// 库存快照流程：复制目录项主键、剩余数量、无限标记和版本；空记录保持默认，方便拒绝结果携带空 Stock。
FCatShopStockSnapshot UCatShopEconomyService::MakeStockSnapshot(const FStockRecord* StockRecord)
{
	FCatShopStockSnapshot Snapshot;
	if (!StockRecord)
	{
		return Snapshot;
	}
	Snapshot.EntryId = StockRecord->Entry.EntryId;
	Snapshot.RemainingStock = StockRecord->RemainingStock;
	Snapshot.bUnlimitedStock = StockRecord->Entry.bUnlimitedStock;
	Snapshot.Revision = StockRecord->Revision;
	return Snapshot;
}

// 公开流水构造流程：复制账本里可以公开的字段。
// ActorPlayerState 刻意留空：服务只有服务器私有 StableNetId，按项目约定它不能进复制 DTO，
// 由持有身份映射的复制挂载点在发出去之前补上公开身份。
FCatShopPublicTransaction UCatShopEconomyService::MakePublicTransaction(const FCatShopTransactionRecord& Record)
{
	FCatShopPublicTransaction Public;
	Public.TransactionId = Record.TransactionId;
	Public.Kind = Record.Kind;
	Public.EntryId = Record.EntryId;
	Public.DefinitionId = Record.DefinitionId;
	Public.FishSource = Record.FishSource;
	Public.WalletDelta = Record.WalletDelta;
	Public.DeliveryState = Record.DeliveryState;
	return Public;
}

// 免费自取判定流程：只认两个显式配置项，且不接受 None。
// 不按"单价为 0"反推免费自取，是因为那样一来任何漏填价格或被改成 0 的条目都会自动变成可白拿的品类。
bool UCatShopEconomyService::IsFreeClaimEntry(const FName EntryId) const
{
	if (EntryId.IsNone())
	{
		return false;
	}
	return EntryId == FreeOrdinaryBaitEntryId || EntryId == FreeStarterRodEntryId;
}

// 幂等键流程：只组合服务器身份、操作和 RequestId；Entry/Fish/Receipt 留给 payload signature 比对，避免同 RequestId 换业务字段时生成第二条交易。
FString UCatShopEconomyService::MakeTerminalKey(const FString& StableNetId, const TCHAR* Operation,
	const FGuid RequestId)
{
	return FString::Printf(TEXT("%s|%s|%s"), *StableNetId, Operation,
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
}

// 目录载荷签名流程：冻结交易类别、钱包前提和 EntryId；价格与库存来自服务端目录，不接受客户端提交。
FString UCatShopEconomyService::MakeCatalogPayloadSignature(const FCatShopPurchaseCommand& Command,
	const ECatShopTransactionKind TransactionKind)
{
	return FString::Printf(TEXT("Kind=%d|Expected=%lld|Entry=%s"), static_cast<int32>(TransactionKind),
		Command.Context.ExpectedRevision, *Command.EntryId.ToString());
}

// 售鱼载荷签名流程：冻结钱包前提、鱼实例、Items 提交证据、来源、重量和估值；同 RequestId 改任一项都不是合法重放。
// 重量必须进签名：它是收购价的唯一输入，同一个 RequestId 换一条更重的鱼重放就等于换了一笔生意。
FString UCatShopEconomyService::MakeFishSalePayloadSignature(const FCatShopFishSaleCommand& Command)
{
	return FString::Printf(TEXT("Expected=%lld|Fish=%s|ItemsCommit=%s|Source=%d|Weight=%.6f|Value=%d"),
		Command.Context.ExpectedRevision,
		*Command.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*Command.ItemsCommitId.ToString(EGuidFormats::DigitsWithHyphens),
		static_cast<int32>(Command.SourceKind), Command.WeightKilograms, Command.SaleValue);
}

// 交付载荷签名流程：冻结原交易、下游回执、下游版本和钱包前提；回执漂移必须拒绝而不是重放。
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
