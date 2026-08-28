#include "ShopEconomy/CatShopCatalogDefinition.h"

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

	// 条目追加流程：先跳过显式关闭的配置，再转换成运行目录项并检查唯一 EntryId；任何非法启用项都会让整份运行表关闭。
	bool AppendSaleEntry(const FCatShopSaleEntry& SaleEntry, TSet<FName>& SeenEntryIds,
		TArray<FCatShopCatalogEntry>& OutEntries, FString& OutError)
	{
		if (!SaleEntry.bEnabled)
		{
			return true;
		}
		FCatShopCatalogEntry Entry;
		if (!SaleEntry.TryBuildCatalogEntry(Entry))
		{
			OutError = FString::Printf(TEXT("Shop sale entry is not runtime-ready: %s"),
				*SaleEntry.EntryId.ToString());
			return false;
		}
		if (SeenEntryIds.Contains(Entry.EntryId))
		{
			OutError = FString::Printf(TEXT("Duplicate shop entry id: %s"), *Entry.EntryId.ToString());
			return false;
		}
		SeenEntryIds.Add(Entry.EntryId);
		OutEntries.Add(MoveTemp(Entry));
		return true;
	}
}

// 出售条目转换流程：
// 1. 先清空输出，保证失败不会把上一条有效数据留给调用方误用。
// 2. 再把策划表字段复制到运行目录项，并保留展示覆盖字段给 UI 使用。
// 3. 最后调用运行目录自己的校验；价格、数量、库存和类别任一缺失都不能进入货架。
bool FCatShopSaleEntry::TryBuildCatalogEntry(FCatShopCatalogEntry& OutEntry) const
{
	OutEntry = FCatShopCatalogEntry();
	OutEntry.EntryId = EntryId;
	OutEntry.Kind = Kind;
	OutEntry.DefinitionId = DefinitionId;
	OutEntry.PurchaseQuantity = PurchaseQuantity;
	OutEntry.UnitPrice = UnitPrice;
	OutEntry.InitialStock = InitialStock;
	OutEntry.bUnlimitedStock = bUnlimitedStock;
	OutEntry.bEnabled = bEnabled;
	OutEntry.RequiredShopUnlockId = RequiredShopUnlockId;
	OutEntry.DisplayNameOverride = DisplayNameOverride;
	OutEntry.DescriptionOverride = DescriptionOverride;
	OutEntry.IconOverride = IconOverride;
	OutEntry.SortOrder = SortOrder;
	return OutEntry.IsRuntimeReady();
}

// 随机库存流程：
// 1. 无限库存条目没有“抽到几件”的意义，直接沿用 SaleEntry 的初始库存展示值。
// 2. 没配置覆盖区间时沿用 SaleEntry.InitialStock，保持固定价格和库存表可读。
// 3. 覆盖区间只接受完整、非负且下限不大于上限的配置，避免刷新时悄悄修正策划表错误。
bool FCatShopRandomSaleEntry::TryResolveRefreshedStock(FRandomStream& RandomStream, int32& OutStock) const
{
	OutStock = SaleEntry.InitialStock;
	if (SaleEntry.bUnlimitedStock)
	{
		return true;
	}
	const bool bHasMinOverride = MinRefreshedStockOverride >= 0;
	const bool bHasMaxOverride = MaxRefreshedStockOverride >= 0;
	if (!bHasMinOverride && !bHasMaxOverride)
	{
		return SaleEntry.InitialStock > 0;
	}
	if (!bHasMinOverride || !bHasMaxOverride || MinRefreshedStockOverride > MaxRefreshedStockOverride)
	{
		return false;
	}
	OutStock = RandomStream.RandRange(MinRefreshedStockOverride, MaxRefreshedStockOverride);
	return OutStock > 0;
}

// 资产刷新流程：把本资产的固定货架、随机池和刷新规则交给数组版实现；资产本身不持有运行期库存。
bool UCatShopCatalogDefinition::BuildRefreshedCatalogEntries(FRandomStream& RandomStream,
	TArray<FCatShopCatalogEntry>& OutEntries, FString& OutError) const
{
	return BuildRefreshedCatalogEntriesFromArrays(FixedEntries, RandomEntries, RefreshRule,
		RandomStream, OutEntries, OutError);
}

// 展示候选流程：收集本资产全部启用且合法的固定/随机候选，让 UI 能用公开库存 EntryId 反查展示信息。
void UCatShopCatalogDefinition::CollectDisplayCatalogEntries(TArray<FCatShopCatalogEntry>& OutEntries) const
{
	CollectDisplayCatalogEntriesFromArrays(FixedEntries, RandomEntries, OutEntries);
}

// 数组刷新流程：
// 1. 固定条目永远先进入货架，满足初级竿、饵、漂每次都出现的业务口径。
// 2. 随机池先整池校验 EntryId 唯一和权重有效性，再按权重不放回抽取，避免同一 EntryId 在一轮货架里出现两次。
// 3. 被抽中条目根据随机库存覆盖写出本次 InitialStock，最后按 SortOrder 稳定排序。
bool UCatShopCatalogDefinition::BuildRefreshedCatalogEntriesFromArrays(
	const TArray<FCatShopSaleEntry>& FixedEntries, const TArray<FCatShopRandomSaleEntry>& RandomEntries,
	const FCatShopRefreshRule& RefreshRule, FRandomStream& RandomStream,
	TArray<FCatShopCatalogEntry>& OutEntries, FString& OutError)
{
	OutEntries.Reset();
	OutError.Reset();
	TSet<FName> SeenEntryIds;
	for (const FCatShopSaleEntry& Entry : FixedEntries)
	{
		if (!AppendSaleEntry(Entry, SeenEntryIds, OutEntries, OutError))
		{
			OutEntries.Reset();
			return false;
		}
	}

	struct FWeightedRandomCandidate
	{
		/** 已经转换好的运行目录项；被抽中后会直接进入当前货架库存。 */
		FCatShopCatalogEntry Entry;
		/** 本候选剩余抽取权重；候选被抽走后整项移除，不再参与后续抽取。 */
		int32 Weight = 0;
	};

	TArray<FWeightedRandomCandidate> Candidates;
	TSet<FName> RandomEntryIds;
	for (const FCatShopRandomSaleEntry& Candidate : RandomEntries)
	{
		if (!Candidate.SaleEntry.bEnabled || Candidate.RefreshWeight <= 0)
		{
			continue;
		}
		if (RandomEntryIds.Contains(Candidate.SaleEntry.EntryId) || SeenEntryIds.Contains(Candidate.SaleEntry.EntryId))
		{
			OutError = FString::Printf(TEXT("Duplicate shop random entry id: %s"),
				*Candidate.SaleEntry.EntryId.ToString());
			OutEntries.Reset();
			return false;
		}
		FCatShopCatalogEntry RuntimeEntry;
		if (!Candidate.SaleEntry.TryBuildCatalogEntry(RuntimeEntry))
		{
			OutError = FString::Printf(TEXT("Shop random entry is not runtime-ready: %s"),
				*Candidate.SaleEntry.EntryId.ToString());
			OutEntries.Reset();
			return false;
		}
		int32 RefreshedStock = 0;
		if (!Candidate.TryResolveRefreshedStock(RandomStream, RefreshedStock))
		{
			OutError = FString::Printf(TEXT("Shop random entry stock override is invalid: %s"),
				*Candidate.SaleEntry.EntryId.ToString());
			OutEntries.Reset();
			return false;
		}
		RuntimeEntry.InitialStock = RefreshedStock;
		FWeightedRandomCandidate& WeightedCandidate = Candidates.AddDefaulted_GetRef();
		WeightedCandidate.Entry = MoveTemp(RuntimeEntry);
		WeightedCandidate.Weight = Candidate.RefreshWeight;
		RandomEntryIds.Add(WeightedCandidate.Entry.EntryId);
	}

	const int32 DrawCount = FMath::Min(FMath::Max(0, RefreshRule.RandomEntryCount), Candidates.Num());
	for (int32 DrawIndex = 0; DrawIndex < DrawCount; ++DrawIndex)
	{
		int32 TotalWeight = 0;
		for (const FWeightedRandomCandidate& Candidate : Candidates)
		{
			TotalWeight += FMath::Max(0, Candidate.Weight);
		}
		if (TotalWeight <= 0)
		{
			break;
		}
		int32 Pick = RandomStream.RandRange(1, TotalWeight);
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
		SeenEntryIds.Add(Candidates[PickedIndex].Entry.EntryId);
		OutEntries.Add(MoveTemp(Candidates[PickedIndex].Entry));
		Candidates.RemoveAtSwap(PickedIndex, 1, EAllowShrinking::No);
	}

	SortCatalogEntries(OutEntries);
	return true;
}

// 数组展示候选流程：
// 1. 固定条目和随机池条目都先转换成运行目录形态，禁用或非法条目不进入 UI 候选。
// 2. 重复 EntryId 只保留首次出现的条目，避免 UI 反查公开库存时出现两个同名按钮。
// 3. 最后按 SortOrder 稳定排序，保证 WBP 动态行每次重建顺序一致。
void UCatShopCatalogDefinition::CollectDisplayCatalogEntriesFromArrays(const TArray<FCatShopSaleEntry>& FixedEntries,
	const TArray<FCatShopRandomSaleEntry>& RandomEntries, TArray<FCatShopCatalogEntry>& OutEntries)
{
	OutEntries.Reset();
	TSet<FName> SeenEntryIds;
	FString IgnoredError;
	for (const FCatShopSaleEntry& Entry : FixedEntries)
	{
		AppendSaleEntry(Entry, SeenEntryIds, OutEntries, IgnoredError);
	}
	for (const FCatShopRandomSaleEntry& Entry : RandomEntries)
	{
		AppendSaleEntry(Entry.SaleEntry, SeenEntryIds, OutEntries, IgnoredError);
	}
	SortCatalogEntries(OutEntries);
}
