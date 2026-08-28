#include "ShopEconomy/CatShopInventoryComponent.h"

#include "Engine/World.h"
#include "Logging/CatLog.h"
#include "Net/UnrealNetwork.h"
#include "ShopEconomy/CatShopEconomyService.h"

namespace
{
	// 固定商品构造流程：集中写入默认表字段，避免构造函数里每条商品重复展开一组低信号赋值。
	FCatShopSaleEntry MakeDefaultSaleEntry(const TCHAR* EntryId, const ECatShopEntryKind Kind,
		const TCHAR* DefinitionId, const int32 PurchaseQuantity, const int32 UnitPrice,
		const int32 InitialStock, const bool bUnlimitedStock, const int32 SortOrder)
	{
		FCatShopSaleEntry Entry;
		Entry.EntryId = FName(EntryId);
		Entry.Kind = Kind;
		Entry.DefinitionId = FName(DefinitionId);
		Entry.PurchaseQuantity = PurchaseQuantity;
		Entry.UnitPrice = UnitPrice;
		Entry.InitialStock = InitialStock;
		Entry.bUnlimitedStock = bUnlimitedStock;
		Entry.SortOrder = SortOrder;
		return Entry;
	}

	// 随机商品构造流程：先复用默认出售条目，再补抽取权重和可选库存区间；它只用于本组件的 C++ 默认商店表。
	FCatShopRandomSaleEntry MakeDefaultRandomEntry(const TCHAR* EntryId, const ECatShopEntryKind Kind,
		const TCHAR* DefinitionId, const int32 PurchaseQuantity, const int32 UnitPrice,
		const int32 InitialStock, const int32 SortOrder, const int32 RefreshWeight,
		const int32 MinStockOverride = -1, const int32 MaxStockOverride = -1)
	{
		FCatShopRandomSaleEntry Entry;
		Entry.SaleEntry = MakeDefaultSaleEntry(EntryId, Kind, DefinitionId, PurchaseQuantity,
			UnitPrice, InitialStock, false, SortOrder);
		Entry.RefreshWeight = RefreshWeight;
		Entry.MinRefreshedStockOverride = MinStockOverride;
		Entry.MaxRefreshedStockOverride = MaxStockOverride;
		return Entry;
	}
}

// 构造流程：
// 1. 关闭 Tick 并打开组件复制，让客户端能拿到和服务器一致的摊位库存 ID。
// 2. 写入本项目首版商店表：三条固定保底商品每次都有，其他商品放入随机池。
// 3. 免费白名单只引用固定表 EntryId，不按价格为 0 反推出可领取项。
UCatShopInventoryComponent::UCatShopInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);

	RefreshRule.RandomEntryCount = 3;

	FixedSaleEntries =
	{
		MakeDefaultSaleEntry(TEXT("FixedStarterRod"), ECatShopEntryKind::EquipmentGrant,
			TEXT("StarterRodT1"), 1, 0, 0, true, 10),
		MakeDefaultSaleEntry(TEXT("FixedBugBait"), ECatShopEntryKind::InventoryQuantityGrant,
			TEXT("BugBait"), 5, 0, 0, true, 20),
		MakeDefaultSaleEntry(TEXT("FixedFeatherFloat"), ECatShopEntryKind::EquipmentGrant,
			TEXT("FeatherFloat"), 1, 0, 0, true, 30)
	};

	RandomSaleEntries =
	{
		MakeDefaultRandomEntry(TEXT("RandomShopRodT2"), ECatShopEntryKind::EquipmentGrant,
			TEXT("ShopRodT2"), 1, 3, 1, 100, 2),
		MakeDefaultRandomEntry(TEXT("RandomYarnBallFloat"), ECatShopEntryKind::EquipmentGrant,
			TEXT("YarnBallFloat"), 1, 2, 1, 110, 2),
		MakeDefaultRandomEntry(TEXT("RandomBellFloat"), ECatShopEntryKind::EquipmentGrant,
			TEXT("BellFloat"), 1, 2, 1, 120, 2),
		MakeDefaultRandomEntry(TEXT("RandomMeatBait"), ECatShopEntryKind::InventoryQuantityGrant,
			TEXT("MeatBait"), 3, 1, 2, 200, 3, 1, 3),
		MakeDefaultRandomEntry(TEXT("RandomFruitBait"), ECatShopEntryKind::InventoryQuantityGrant,
			TEXT("FruitBait"), 3, 1, 2, 210, 3, 1, 3),
		MakeDefaultRandomEntry(TEXT("RandomNectarBait"), ECatShopEntryKind::InventoryQuantityGrant,
			TEXT("NectarBait"), 3, 1, 2, 220, 3, 1, 3),
		MakeDefaultRandomEntry(TEXT("RandomMoonlightBait"), ECatShopEntryKind::InventoryQuantityGrant,
			TEXT("MoonlightBait"), 2, 2, 1, 230, 1),
		MakeDefaultRandomEntry(TEXT("RandomBugChum"), ECatShopEntryKind::InventoryQuantityGrant,
			TEXT("BugChum"), 1, 1, 2, 300, 3, 1, 3),
		MakeDefaultRandomEntry(TEXT("RandomFruitFragranceChum"), ECatShopEntryKind::InventoryQuantityGrant,
			TEXT("FruitFragranceChum"), 1, 1, 2, 310, 2, 1, 2),
		MakeDefaultRandomEntry(TEXT("RandomFermentedGrainChum"), ECatShopEntryKind::InventoryQuantityGrant,
			TEXT("FermentedGrainChum"), 1, 1, 2, 320, 2, 1, 2),
		MakeDefaultRandomEntry(TEXT("RandomHolyLightChum"), ECatShopEntryKind::InventoryQuantityGrant,
			TEXT("HolyLightChum"), 1, 2, 1, 330, 1)
	};

	FreeOrdinaryBaitEntryId = TEXT("FixedBugBait");
	FreeStarterRodEntryId = TEXT("FixedStarterRod");
	FreeStarterFloatEntryId = TEXT("FixedFeatherFloat");
}

// 复制声明流程：只复制稳定 ShopInventoryId；运行库存通过 GameState 公开快照同步，避免组件复制一份私有 Map。
void UCatShopInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, ShopInventoryId);
}

// 身份复制回调流程：只通知本地读模型重新匹配公开货架；服务器库存 Map 和交易状态不在这里改。
void UCatShopInventoryComponent::OnRep_ShopInventoryId()
{
	OnInventoryIdentityChanged.Broadcast();
}

// 启动流程：
// 1. 客户端只保留组件默认展示配置并等待服务器复制 ShopInventoryId。
// 2. authority 生成稳定 ID，按组件表生成本轮初始货架，再把自己注册给 ShopEconomy 服务用于公开快照和购买裁决。
void UCatShopInventoryComponent::BeginPlay()
{
	Super::BeginPlay();
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}
	if (!ShopInventoryId.IsValid())
	{
		ShopInventoryId = FGuid::NewGuid();
	}
	RebuildInitialInventoryFromCatalog();
	if (UCatShopEconomyService* Shop = GetWorld() ? GetWorld()->GetSubsystem<UCatShopEconomyService>() : nullptr)
	{
		Shop->RegisterShopInventory(this);
	}
}

// 结束流程：authority 离开 World 时先从 ShopEconomy 服务注销，再交还组件生命周期；客户端没有注册事实需要清理。
void UCatShopInventoryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AActor* Owner = GetOwner(); Owner && Owner->HasAuthority())
	{
		if (UCatShopEconomyService* Shop = GetWorld() ? GetWorld()->GetSubsystem<UCatShopEconomyService>() : nullptr)
		{
			Shop->UnregisterShopInventory(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

// ID 读取流程：返回服务器生成并复制的摊位库存身份；未进入 World 的默认对象可能仍是无效值。
FGuid UCatShopInventoryComponent::GetShopInventoryId() const
{
	return ShopInventoryId;
}

// 目录状态读取流程：只返回最近一次构建结果；展示和购买都应在 false 时关闭本摊位货架。
bool UCatShopInventoryComponent::IsRuntimeCatalogReady() const
{
	return bCatalogReady;
}

// 初始货架流程：使用普通随机流从组件表生成一次运行目录；成功后替换当前库存，失败后清空库存并记录目录不可用。
bool UCatShopInventoryComponent::RebuildInitialInventoryFromCatalog()
{
	FRandomStream RandomStream(FMath::Rand());
	TArray<FCatShopCatalogEntry> RuntimeEntries;
	FString CatalogError;
	if (!BuildRuntimeCatalogEntries(RandomStream, RuntimeEntries, CatalogError)
		|| !RebuildStockFromCatalogEntries(RuntimeEntries, CatalogError))
	{
		StockByEntryId.Reset();
		bCatalogReady = false;
		UE_LOG(LogCatfishing, Warning, TEXT("Event=shop_inventory_initial_load_failed Owner=%s Reason=%s"),
			*GetNameSafe(GetOwner()), *CatalogError);
		return false;
	}
	bCatalogReady = true;
	return true;
}

// 刷新流程：
// 1. 只允许 authority 用有效 RequestId 刷新本摊位，刷新触发时机不在这里决定。
// 2. 同一 RequestId 只生效一次，避免网络重试重复抽随机池。
// 3. 成功时整体替换货架并广播，失败保留旧货架，避免半刷新影响购买。
bool UCatShopInventoryComponent::RefreshShopInventoryFromCatalog(const FGuid& RequestId,
	const FCatShopRefreshRequest& Request)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid())
	{
		return false;
	}
	if (RefreshTerminalRequests.Contains(RequestId))
	{
		return false;
	}
	FRandomStream RandomStream(Request.bUseExplicitRandomSeed ? Request.RandomSeed : FMath::Rand());
	TArray<FCatShopCatalogEntry> RefreshedEntries;
	FString CatalogError;
	if (!BuildRuntimeCatalogEntries(RandomStream, RefreshedEntries, CatalogError))
	{
		UE_LOG(LogCatfishing, Warning, TEXT("Event=shop_inventory_refresh_failed Owner=%s Reason=%s"),
			*GetNameSafe(GetOwner()), *CatalogError);
		return false;
	}
	TMap<FName, FStockRecord> PreviousStock = MoveTemp(StockByEntryId);
	if (!RebuildStockFromCatalogEntries(RefreshedEntries, CatalogError))
	{
		StockByEntryId = MoveTemp(PreviousStock);
		UE_LOG(LogCatfishing, Warning, TEXT("Event=shop_inventory_refresh_failed Owner=%s Reason=%s"),
			*GetNameSafe(GetOwner()), *CatalogError);
		return false;
	}
	bCatalogReady = true;
	RefreshTerminalRequests.Add(RequestId);
	OnInventoryChanged.Broadcast();
	return true;
}

// 快照追加流程：逐条复制当前货架库存并写入本组件 ShopInventoryId；外层 GameState 用它把多摊位货架放进同一份公开经济快照。
void UCatShopInventoryComponent::AppendStockSnapshots(TArray<FCatShopStockSnapshot>& OutStocks) const
{
	if (!bCatalogReady)
	{
		return;
	}
	OutStocks.Reserve(OutStocks.Num() + StockByEntryId.Num());
	for (const TPair<FName, FStockRecord>& Pair : StockByEntryId)
	{
		OutStocks.Add(MakeStockSnapshot(&Pair.Value));
	}
}

// 库存查询流程：先清输出，再只在本组件目录可用时按 EntryId 读取；缺项不制造默认库存。
bool UCatShopInventoryComponent::TryGetStockSnapshot(const FName EntryId, FCatShopStockSnapshot& OutSnapshot) const
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

// 目录查询流程：先清输出，再按本摊位当前货架读取目录原文；未随机抽中的候选不会被当成可买商品。
bool UCatShopInventoryComponent::TryGetCatalogEntry(const FName EntryId, FCatShopCatalogEntry& OutEntry) const
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

// 库存扣减流程：只在 authority 上修改货架剩余量；无限库存返回当前快照，有限库存售罄时拒绝并保留原状态。
// 本函数不广播变化，购买写口会在公款、库存和账本同一笔事务都写完后统一发布公开快照，避免客户端看到半成品状态。
bool UCatShopInventoryComponent::ConsumeCatalogEntryFromAuthority(const FName EntryId,
	FCatShopStockSnapshot& OutSnapshot)
{
	OutSnapshot = FCatShopStockSnapshot();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !bCatalogReady)
	{
		return false;
	}
	FStockRecord* StockRecord = StockByEntryId.Find(EntryId);
	if (!StockRecord)
	{
		return false;
	}
	if (!StockRecord->Entry.bUnlimitedStock)
	{
		if (StockRecord->RemainingStock <= 0)
		{
			OutSnapshot = MakeStockSnapshot(StockRecord);
			return false;
		}
		--StockRecord->RemainingStock;
		++StockRecord->Revision;
	}
	OutSnapshot = MakeStockSnapshot(StockRecord);
	return true;
}

// 展示候选流程：使用和运行货架相同的组件数据源；正式资产缺失或保底固定项非法时清空候选，避免 UI 展示服务器不会接受的商品。
void UCatShopInventoryComponent::CollectDisplayCatalogEntries(TArray<FCatShopCatalogEntry>& OutEntries) const
{
	OutEntries.Reset();
	FString IgnoredError;
	if (!ShopCatalog.IsNull())
	{
		const UCatShopCatalogDefinition* LoadedCatalog = ShopCatalog.LoadSynchronous();
		if (LoadedCatalog && ValidateRequiredFixedSaleEntries(LoadedCatalog->FixedEntries, IgnoredError))
		{
			LoadedCatalog->CollectDisplayCatalogEntries(OutEntries);
		}
		return;
	}
	if (ValidateRequiredFixedSaleEntries(FixedSaleEntries, IgnoredError))
	{
		UCatShopCatalogDefinition::CollectDisplayCatalogEntriesFromArrays(FixedSaleEntries, RandomSaleEntries,
			OutEntries);
	}
}

// 免费白名单流程：只认本组件显式配置的三条保底项；价格为 0 的其他商品仍按普通购买入口处理。
bool UCatShopInventoryComponent::IsFreeClaimEntry(const FName EntryId) const
{
	if (EntryId.IsNone())
	{
		return false;
	}
	return EntryId == FreeOrdinaryBaitEntryId || EntryId == FreeStarterRodEntryId
		|| EntryId == FreeStarterFloatEntryId;
}

// 每日进货流程：只接受更大的天序号，再重置标记了每日进货的有限库存；无限库存和一局限量商品保持原样。
bool UCatShopInventoryComponent::AdvanceShopDay(const int32 NewDayIndex)
{
	if (!bCatalogReady || NewDayIndex <= CurrentShopDayIndex)
	{
		return false;
	}
	CurrentShopDayIndex = NewDayIndex;
	bool bChanged = false;
	for (TPair<FName, FStockRecord>& Pair : StockByEntryId)
	{
		if (!Pair.Value.Entry.bDailyRestock)
		{
			continue;
		}
		Pair.Value.RemainingStock = Pair.Value.Entry.DailyRestockQuantity;
		++Pair.Value.Revision;
		bChanged = true;
	}
	if (bChanged)
	{
		OnInventoryChanged.Broadcast();
	}
	return bChanged;
}

// 保底目录收集流程：要求三条免费自取 ID 都显式配置并且互不重复；缺一条都会让本摊位无法保证“每次都有”。
bool UCatShopInventoryComponent::CollectRequiredStarterShopEntries(
	TArray<FRequiredStarterShopEntry>& OutRequiredEntries, FString& OutError) const
{
	OutRequiredEntries.Reset();
	OutError.Reset();
	const FRequiredStarterShopEntry RequiredEntries[] =
	{
		{ FreeStarterRodEntryId, TEXT("FreeStarterRodEntryId") },
		{ FreeOrdinaryBaitEntryId, TEXT("FreeOrdinaryBaitEntryId") },
		{ FreeStarterFloatEntryId, TEXT("FreeStarterFloatEntryId") }
	};
	TSet<FName> SeenEntryIds;
	for (const FRequiredStarterShopEntry& RequiredEntry : RequiredEntries)
	{
		if (RequiredEntry.EntryId.IsNone())
		{
			OutError = FString::Printf(TEXT("Required starter shop entry id is not configured: %s"),
				RequiredEntry.ConfigName);
			return false;
		}
		if (SeenEntryIds.Contains(RequiredEntry.EntryId))
		{
			OutError = FString::Printf(TEXT("Duplicate starter shop entry id: %s"),
				*RequiredEntry.EntryId.ToString());
			return false;
		}
		SeenEntryIds.Add(RequiredEntry.EntryId);
		OutRequiredEntries.Add(RequiredEntry);
	}
	return true;
}

// 保底固定项检查流程：每条白名单都必须在固定表里，且转换后价格为 0、库存无限；随机池命中不能满足保底承诺。
bool UCatShopInventoryComponent::ValidateRequiredFixedSaleEntries(const TArray<FCatShopSaleEntry>& Entries,
	FString& OutError) const
{
	TArray<FRequiredStarterShopEntry> RequiredEntries;
	if (!CollectRequiredStarterShopEntries(RequiredEntries, OutError))
	{
		return false;
	}
	for (const FRequiredStarterShopEntry& RequiredEntry : RequiredEntries)
	{
		const FCatShopSaleEntry* SaleEntry = Entries.FindByPredicate(
			[RequiredEntry](const FCatShopSaleEntry& Candidate)
			{
				return Candidate.EntryId == RequiredEntry.EntryId;
			});
		if (!SaleEntry)
		{
			OutError = FString::Printf(TEXT("Required starter shop entry must be fixed: %s"),
				*RequiredEntry.EntryId.ToString());
			return false;
		}
		FCatShopCatalogEntry RuntimeEntry;
		if (!SaleEntry->TryBuildCatalogEntry(RuntimeEntry)
			|| RuntimeEntry.UnitPrice != 0 || !RuntimeEntry.bUnlimitedStock)
		{
			OutError = FString::Printf(TEXT("Required starter shop entry must be enabled, free and unlimited: %s"),
				*RequiredEntry.EntryId.ToString());
			return false;
		}
	}
	return true;
}

// 运行目录构建流程：正式 Catalog 资产存在时只用资产；否则使用组件内联固定/随机表，不再回退全局 Settings 目录。
bool UCatShopInventoryComponent::BuildRuntimeCatalogEntries(FRandomStream& RandomStream,
	TArray<FCatShopCatalogEntry>& OutEntries, FString& OutError) const
{
	OutEntries.Reset();
	OutError.Reset();
	if (!ShopCatalog.IsNull())
	{
		const UCatShopCatalogDefinition* LoadedCatalog = ShopCatalog.LoadSynchronous();
		if (!LoadedCatalog)
		{
			OutError = TEXT("Configured shop catalog could not be loaded.");
			return false;
		}
		if (!ValidateRequiredFixedSaleEntries(LoadedCatalog->FixedEntries, OutError))
		{
			return false;
		}
		return LoadedCatalog->BuildRefreshedCatalogEntries(RandomStream, OutEntries, OutError);
	}
	if (!ValidateRequiredFixedSaleEntries(FixedSaleEntries, OutError))
	{
		return false;
	}
	return UCatShopCatalogDefinition::BuildRefreshedCatalogEntriesFromArrays(
		FixedSaleEntries, RandomSaleEntries, RefreshRule, RandomStream, OutEntries, OutError);
}

// 货架重建流程：逐条校验运行目录、拒绝重复 EntryId，并把库存版本从 1 开始；失败时清空临时结果，不留下半张货架。
bool UCatShopInventoryComponent::RebuildStockFromCatalogEntries(const TArray<FCatShopCatalogEntry>& CatalogEntries,
	FString& OutError)
{
	StockByEntryId.Reset();
	OutError.Reset();
	if (CatalogEntries.IsEmpty())
	{
		OutError = TEXT("Shop catalog has no runtime entries.");
		return false;
	}
	for (const FCatShopCatalogEntry& Entry : CatalogEntries)
	{
		if (!Entry.IsRuntimeReady())
		{
			OutError = FString::Printf(TEXT("Shop catalog entry is not runtime-ready: %s"),
				*Entry.EntryId.ToString());
			StockByEntryId.Reset();
			return false;
		}
		if (StockByEntryId.Contains(Entry.EntryId))
		{
			OutError = FString::Printf(TEXT("Duplicate shop catalog entry id: %s"), *Entry.EntryId.ToString());
			StockByEntryId.Reset();
			return false;
		}
		FStockRecord& Stock = StockByEntryId.Add(Entry.EntryId);
		Stock.Entry = Entry;
		Stock.RemainingStock = Entry.InitialStock;
		Stock.Revision = 1;
	}
	return true;
}

// 库存快照流程：复制本组件 ID、目录主键、剩余数量、无限库存标记和版本；空记录保持默认值。
FCatShopStockSnapshot UCatShopInventoryComponent::MakeStockSnapshot(const FStockRecord* StockRecord) const
{
	FCatShopStockSnapshot Snapshot;
	if (!StockRecord)
	{
		return Snapshot;
	}
	Snapshot.ShopInventoryId = ShopInventoryId;
	Snapshot.EntryId = StockRecord->Entry.EntryId;
	Snapshot.RemainingStock = StockRecord->RemainingStock;
	Snapshot.bUnlimitedStock = StockRecord->Entry.bUnlimitedStock;
	Snapshot.Revision = StockRecord->Revision;
	return Snapshot;
}
