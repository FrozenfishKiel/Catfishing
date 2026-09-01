#include "ShopEconomy/CatShopInventoryComponent.h"

#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Logging/CatLog.h"
#include "Net/UnrealNetwork.h"
#include "ShopEconomy/CatShopEconomySettings.h"
#include "ShopEconomy/CatShopEconomyService.h"

namespace
{
	// 目录排序流程：排序只影响 UI 展示和快照稳定性，不改变购买裁决；EntryId 次序兜底让同 SortOrder 的条目也有确定顺序。
	void SortCatalogEntries(TArray<FCatShopCatalogEntry>& Entries)
	{
		Entries.StableSort([](const FCatShopCatalogEntry& Left, const FCatShopCatalogEntry& Right)
		{
			if (Left.SortOrder != Right.SortOrder)
			{
				return Left.SortOrder < Right.SortOrder;
			}
			return Left.EntryId.ToString() < Right.EntryId.ToString();
		});
	}

	/** 随机刷新池里的临时候选；它只在构建当前货架时存在，不会成为第二份库存状态。 */
	struct FWeightedRandomCandidate
	{
		/** 已经转换好的运行目录项；被抽中后会进入当前货架库存。 */
		FCatShopCatalogEntry Entry;

		/** 本候选剩余抽取权重；候选被抽走后整项移除，不再参与后续抽取。 */
		int32 Weight = 0;
	};

	// 购物车行归一化流程：先拒绝空车、超长数组、非法 EntryId 和超上限次数，再合并重复 EntryId 并按 ID 排序。
	// 库存扣减只读取这份规范化结果，避免客户端通过重复行或异常数量绕过整批校验。
	bool NormalizeCartLinesForStockConsumption(const TArray<FCatShopCartLineCommand>& Lines,
		TArray<FCatShopCartLineCommand>& OutLines)
	{
		OutLines.Reset();
		if (Lines.IsEmpty() || Lines.Num() > CatShopCartLimits::MaxCartLines)
		{
			return false;
		}
		TMap<FName, int32> CountsByEntryId;
		for (const FCatShopCartLineCommand& Line : Lines)
		{
			if (Line.EntryId.IsNone() || Line.CartCount <= 0
				|| Line.CartCount > CatShopCartLimits::MaxCartCountPerEntry)
			{
				OutLines.Reset();
				return false;
			}
			int32& Count = CountsByEntryId.FindOrAdd(Line.EntryId);
			if (Line.CartCount > CatShopCartLimits::MaxCartCountPerEntry - Count)
			{
				OutLines.Reset();
				return false;
			}
			Count += Line.CartCount;
		}
		for (const TPair<FName, int32>& Pair : CountsByEntryId)
		{
			FCatShopCartLineCommand& NormalizedLine = OutLines.AddDefaulted_GetRef();
			NormalizedLine.EntryId = Pair.Key;
			NormalizedLine.CartCount = Pair.Value;
		}
		OutLines.Sort([](const FCatShopCartLineCommand& Left, const FCatShopCartLineCommand& Right)
		{
			return Left.EntryId.ToString() < Right.EntryId.ToString();
		});
		return !OutLines.IsEmpty();
	}
}

// 构造流程：
// 1. 关闭 Tick 并打开组件复制，让客户端能拿到和服务器一致的摊位库存 ID。
// 2. 不再写入任何 C++ 默认商品，正式商品、价格、分类和随机池都必须来自策划 DataTable。
// 3. 随机抽取数量保留为组件配置，默认 0 表示只展示表中固定上架行。
UCatShopInventoryComponent::UCatShopInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	RefreshRule.RandomEntryCount = 0;
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
// 1. 客户端只等待服务器复制 ShopInventoryId 和公开商店快照。
// 2. authority 生成稳定 ID，按策划 DataTable 生成本轮初始货架，再注册给 ShopEconomy 服务用于公开快照和购买裁决。
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

// 初始货架流程：使用普通随机流从 DataTable 生成一次运行目录；成功后替换当前库存，失败后清空库存并记录目录不可用。
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

// 整车库存扣减流程：
// 1. 先在本摊位内合并重复 EntryId，并整批验证 authority、目录状态、条目存在和有限库存数量。
// 2. 所有行都通过后才进入第二轮扣减；无限库存只返回快照，有限库存按选购次数推进版本。
// 3. 本函数不广播变化，购买写口会在公款、库存和账本同一笔事务都写完后统一发布公开快照。
bool UCatShopInventoryComponent::ConsumeCatalogEntriesFromAuthority(
	const TArray<FCatShopCartLineCommand>& Lines, TArray<FCatShopStockSnapshot>& OutSnapshots)
{
	OutSnapshots.Reset();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !bCatalogReady)
	{
		return false;
	}
	TArray<FCatShopCartLineCommand> NormalizedLines;
	if (!NormalizeCartLinesForStockConsumption(Lines, NormalizedLines))
	{
		return false;
	}
	for (const FCatShopCartLineCommand& Line : NormalizedLines)
	{
		const FStockRecord* StockRecord = StockByEntryId.Find(Line.EntryId);
		if (!StockRecord)
		{
			OutSnapshots.Reset();
			return false;
		}
		if (!StockRecord->Entry.bUnlimitedStock && StockRecord->RemainingStock < Line.CartCount)
		{
			OutSnapshots.Add(MakeStockSnapshot(StockRecord));
			return false;
		}
	}
	OutSnapshots.Reserve(NormalizedLines.Num());
	for (const FCatShopCartLineCommand& Line : NormalizedLines)
	{
		FStockRecord* StockRecord = StockByEntryId.Find(Line.EntryId);
		if (!StockRecord)
		{
			OutSnapshots.Reset();
			return false;
		}
		if (!StockRecord->Entry.bUnlimitedStock)
		{
			StockRecord->RemainingStock -= Line.CartCount;
			++StockRecord->Revision;
		}
		OutSnapshots.Add(MakeStockSnapshot(StockRecord));
	}
	return true;
}

// 展示候选流程：使用和运行货架相同的 DataTable 数据源；正式表缺失或类型错误时清空候选，避免 UI 展示服务器不会接受的商品。
void UCatShopInventoryComponent::CollectDisplayCatalogEntries(TArray<FCatShopCatalogEntry>& OutEntries) const
{
	OutEntries.Reset();
	TSoftObjectPtr<UDataTable> CatalogTable = ResolveShopCatalogTable();
	if (CatalogTable.IsNull())
	{
		return;
	}
	const UDataTable* LoadedTable = CatalogTable.LoadSynchronous();
	if (!LoadedTable)
	{
		return;
	}
	CollectDisplayCatalogEntriesFromTable(*LoadedTable, OutEntries);
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

// 运行目录构建流程：正式商品表是唯一入口；表未配置、加载失败或行类型错误都会让本摊位 fail-closed。
bool UCatShopInventoryComponent::BuildRuntimeCatalogEntries(FRandomStream& RandomStream,
	TArray<FCatShopCatalogEntry>& OutEntries, FString& OutError) const
{
	OutEntries.Reset();
	OutError.Reset();
	TSoftObjectPtr<UDataTable> CatalogTable = ResolveShopCatalogTable();
	if (CatalogTable.IsNull())
	{
		OutError = TEXT("Shop catalog DataTable and project default DataTable are not configured.");
		return false;
	}
	const UDataTable* LoadedTable = CatalogTable.LoadSynchronous();
	if (!LoadedTable)
	{
		OutError = TEXT("Configured shop catalog DataTable could not be loaded.");
		return false;
	}
	return BuildCatalogEntriesFromTable(*LoadedTable, RandomStream, OutEntries, OutError);
}

// DataTable 构建流程：
// 1. 先要求表结构就是 FCatShopCatalogTableRow，再遍历启用行并用 RowName 兜底 EntryId。
// 2. 固定上架行直接进入输出；随机候选要求正权重，并在进入候选池前解析本轮库存覆盖。
// 3. 随机池按权重不放回抽取 RefreshRule.RandomEntryCount 条，权重总量超出随机接口范围也视为配置错误。
// 4. 最后整体排序；任一启用行非法都会关闭整份目录，避免客户端看到服务器不会接受的货架。
bool UCatShopInventoryComponent::BuildCatalogEntriesFromTable(const UDataTable& CatalogTable,
	FRandomStream& RandomStream, TArray<FCatShopCatalogEntry>& OutEntries, FString& OutError) const
{
	OutEntries.Reset();
	OutError.Reset();
	const UScriptStruct* RowStruct = CatalogTable.GetRowStruct();
	if (!RowStruct || !RowStruct->IsChildOf(FCatShopCatalogTableRow::StaticStruct()))
	{
		OutError = TEXT("Shop catalog DataTable row type must be FCatShopCatalogTableRow.");
		return false;
	}
	TSet<FName> SeenEntryIds;
	TArray<FWeightedRandomCandidate> Candidates;
	for (const TPair<FName, uint8*>& Pair : CatalogTable.GetRowMap())
	{
		const FCatShopCatalogTableRow* Row =
			reinterpret_cast<const FCatShopCatalogTableRow*>(Pair.Value);
		if (!Row || !Row->bEnabled)
		{
			continue;
		}
		FCatShopCatalogEntry RuntimeEntry;
		if (!Row->TryBuildCatalogEntry(Pair.Key, RuntimeEntry))
		{
			OutError = FString::Printf(TEXT("Shop catalog row is not runtime-ready: %s"),
				*Pair.Key.ToString());
			OutEntries.Reset();
			return false;
		}
		if (SeenEntryIds.Contains(RuntimeEntry.EntryId))
		{
			OutError = FString::Printf(TEXT("Duplicate shop catalog entry id: %s"),
				*RuntimeEntry.EntryId.ToString());
			OutEntries.Reset();
			return false;
		}
		SeenEntryIds.Add(RuntimeEntry.EntryId);
		if (Row->bAlwaysStocked)
		{
			OutEntries.Add(MoveTemp(RuntimeEntry));
			continue;
		}
		if (Row->RefreshWeight <= 0)
		{
			OutError = FString::Printf(TEXT("Enabled random shop catalog row has no refresh weight: %s"),
				*Pair.Key.ToString());
			OutEntries.Reset();
			return false;
		}
		int32 RefreshedStock = 0;
		if (!Row->TryResolveRefreshedStock(RandomStream, RefreshedStock))
		{
			OutError = FString::Printf(TEXT("Shop catalog row stock override is invalid: %s"),
				*Pair.Key.ToString());
			OutEntries.Reset();
			return false;
		}
		RuntimeEntry.InitialStock = RefreshedStock;
		FWeightedRandomCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.Entry = MoveTemp(RuntimeEntry);
		Candidate.Weight = Row->RefreshWeight;
	}
	const int32 DrawCount = FMath::Min(FMath::Max(0, RefreshRule.RandomEntryCount), Candidates.Num());
	for (int32 DrawIndex = 0; DrawIndex < DrawCount; ++DrawIndex)
	{
		int64 TotalWeight = 0;
		for (const FWeightedRandomCandidate& Candidate : Candidates)
		{
			TotalWeight += FMath::Max(0, Candidate.Weight);
			if (TotalWeight > MAX_int32)
			{
				OutError = TEXT("Shop catalog random refresh weight total exceeds int32 range.");
				OutEntries.Reset();
				return false;
			}
		}
		if (TotalWeight <= 0)
		{
			break;
		}
		int32 Pick = RandomStream.RandRange(1, static_cast<int32>(TotalWeight));
		int32 PickedIndex = INDEX_NONE;
		for (int32 CandidateIndex = 0; CandidateIndex < Candidates.Num(); ++CandidateIndex)
		{
			Pick -= FMath::Max(0, Candidates[CandidateIndex].Weight);
			if (Pick <= 0)
			{
				PickedIndex = CandidateIndex;
				break;
			}
		}
		if (PickedIndex == INDEX_NONE)
		{
			break;
		}
		OutEntries.Add(MoveTemp(Candidates[PickedIndex].Entry));
		Candidates.RemoveAtSwap(PickedIndex, 1, EAllowShrinking::No);
	}
	SortCatalogEntries(OutEntries);
	return true;
}

// 出售表解析流程：摊位实例表拥有最高优先级；没有实例表时才读项目默认表，让普通摊位少配一遍，特殊摊位仍能覆写。
TSoftObjectPtr<UDataTable> UCatShopInventoryComponent::ResolveShopCatalogTable() const
{
	if (!ShopCatalogTable.IsNull())
	{
		return ShopCatalogTable;
	}
	const UCatShopEconomySettings* Settings = GetDefault<UCatShopEconomySettings>();
	return Settings ? Settings->DefaultShopCatalogTable : TSoftObjectPtr<UDataTable>();
}

// DataTable 展示候选流程：
// 1. 只读取启用且有上架路径的行；固定行和有权重的随机候选都可作为 UI 候选。
// 2. 行本身非法时不进入展示候选，服务器初始构建会继续用 fail-closed 日志暴露配置错误。
// 3. 重复 EntryId 只保留首次候选，避免 WBP 按公开库存反查时出现两个同名商品行。
void UCatShopInventoryComponent::CollectDisplayCatalogEntriesFromTable(const UDataTable& CatalogTable,
	TArray<FCatShopCatalogEntry>& OutEntries) const
{
	OutEntries.Reset();
	const UScriptStruct* RowStruct = CatalogTable.GetRowStruct();
	if (!RowStruct || !RowStruct->IsChildOf(FCatShopCatalogTableRow::StaticStruct()))
	{
		return;
	}
	TSet<FName> SeenEntryIds;
	for (const TPair<FName, uint8*>& Pair : CatalogTable.GetRowMap())
	{
		const FCatShopCatalogTableRow* Row =
			reinterpret_cast<const FCatShopCatalogTableRow*>(Pair.Value);
		if (!Row || !Row->bEnabled || (!Row->bAlwaysStocked && Row->RefreshWeight <= 0))
		{
			continue;
		}
		FCatShopCatalogEntry RuntimeEntry;
		if (!Row->TryBuildCatalogEntry(Pair.Key, RuntimeEntry) || SeenEntryIds.Contains(RuntimeEntry.EntryId))
		{
			continue;
		}
		SeenEntryIds.Add(RuntimeEntry.EntryId);
		OutEntries.Add(MoveTemp(RuntimeEntry));
	}
	SortCatalogEntries(OutEntries);
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

// 库存快照流程：复制本组件 ID、目录主键、本轮初始数量、剩余数量、无限库存标记和版本；空记录保持默认值。
FCatShopStockSnapshot UCatShopInventoryComponent::MakeStockSnapshot(const FStockRecord* StockRecord) const
{
	FCatShopStockSnapshot Snapshot;
	if (!StockRecord)
	{
		return Snapshot;
	}
	Snapshot.ShopInventoryId = ShopInventoryId;
	Snapshot.EntryId = StockRecord->Entry.EntryId;
	Snapshot.InitialStock = StockRecord->Entry.InitialStock;
	Snapshot.RemainingStock = StockRecord->RemainingStock;
	Snapshot.bUnlimitedStock = StockRecord->Entry.bUnlimitedStock;
	Snapshot.Revision = StockRecord->Revision;
	return Snapshot;
}
