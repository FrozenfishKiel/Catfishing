#include "ShopEconomy/CatShopEconomyTypes.h"

// 目录项校验流程：只确认本服务真实需要的订单字段；下游 Definition 是否存在留给 Equipment/Data，避免经济目录偷建第二份内容真相。
// UnitPrice 判 >= 0 就是在拦"没填价格"：默认值是 -1，只有配置里显式写过价格的条目才可能通过，0 元条目仍然合法通过。
// 每日进货那一段是同一种拦法：标了 bDailyRestock 却没给正的进货量，说明飞书还没拍这一项每天进几个，
// 这时候判它非法比按 0 补货更安全——按 0 补货会让这项从第二天起永远缺货，看起来像"卖光了"而不是"没配"。
// 无限库存和每日进货互斥：永不缺货的东西不需要进货量，两者同时为真只会让 AdvanceShopDay 去改一个没人读的数字。
// 商店解锁条件当前没有可信事实源，非空时直接挡在运行目录外，避免字段看似可配但购买路径实际绕过它。
bool FCatShopCatalogEntry::IsRuntimeReady() const
{
	if (!bEnabled || EntryId.IsNone() || Kind == ECatShopEntryKind::Unknown || DefinitionId.IsNone()
		|| PurchaseQuantity <= 0 || UnitPrice < 0 || !RequiredShopUnlockId.IsNone()
		|| !(bUnlimitedStock || InitialStock > 0))
	{
		return false;
	}
	return bDailyRestock ? (!bUnlimitedStock && DailyRestockQuantity > 0) : true;
}

// 表行转换流程：
// 1. 先清空输出，避免失败时泄漏上一行的有效数据。
// 2. EntryId 留空时使用 DataTable RowName，这让策划只维护一份稳定主键；其他字段逐项复制到运行目录。
// 3. 固定库存直接复用运行目录校验；随机候选允许抽中后的库存覆盖补齐 InitialStock，所以这里只先校验核心字段。
bool FCatShopCatalogTableRow::TryBuildCatalogEntry(const FName RowName, FCatShopCatalogEntry& OutEntry) const
{
	OutEntry = FCatShopCatalogEntry();
	OutEntry.EntryId = EntryId.IsNone() ? RowName : EntryId;
	OutEntry.Kind = Kind;
	OutEntry.DefinitionId = DefinitionId;
	OutEntry.DisplayCategoryId = DisplayCategoryId;
	OutEntry.PurchaseQuantity = PurchaseQuantity;
	OutEntry.UnitPrice = UnitPrice;
	OutEntry.InitialStock = InitialStock;
	OutEntry.bUnlimitedStock = bUnlimitedStock;
	OutEntry.bEnabled = bEnabled;
	OutEntry.RequiredShopUnlockId = RequiredShopUnlockId;
	OutEntry.bDailyRestock = bDailyRestock;
	OutEntry.DailyRestockQuantity = DailyRestockQuantity;
	OutEntry.DisplayNameOverride = DisplayNameOverride;
	OutEntry.DescriptionOverride = DescriptionOverride;
	OutEntry.IconOverride = IconOverride;
	OutEntry.SortOrder = SortOrder;
	const bool bHasValidRefreshStockOverride = !bAlwaysStocked && !bUnlimitedStock
		&& MinRefreshedStockOverride > 0 && MaxRefreshedStockOverride >= MinRefreshedStockOverride;
	if (!bHasValidRefreshStockOverride)
	{
		return OutEntry.IsRuntimeReady();
	}
	if (!OutEntry.bEnabled || OutEntry.EntryId.IsNone() || OutEntry.Kind == ECatShopEntryKind::Unknown
		|| OutEntry.DefinitionId.IsNone() || OutEntry.PurchaseQuantity <= 0 || OutEntry.UnitPrice < 0
		|| !OutEntry.RequiredShopUnlockId.IsNone())
	{
		return false;
	}
	return OutEntry.bDailyRestock ? (!OutEntry.bUnlimitedStock && OutEntry.DailyRestockQuantity > 0) : true;
}

// 随机库存流程：
// 1. 无限库存没有抽中几件的意义，直接沿用表里的默认库存展示值。
// 2. 没配置覆盖区间时沿用 InitialStock，让表格里看到的库存就是运行库存。
// 3. 覆盖区间必须成对填写、下限为正且下限不大于上限，避免刷新时悄悄修正策划表错误。
bool FCatShopCatalogTableRow::TryResolveRefreshedStock(FRandomStream& RandomStream, int32& OutStock) const
{
	OutStock = InitialStock;
	if (bUnlimitedStock)
	{
		return true;
	}
	const bool bHasMinOverride = MinRefreshedStockOverride >= 0;
	const bool bHasMaxOverride = MaxRefreshedStockOverride >= 0;
	if (!bHasMinOverride && !bHasMaxOverride)
	{
		return InitialStock > 0;
	}
	if (!bHasMinOverride || !bHasMaxOverride || MinRefreshedStockOverride <= 0
		|| MinRefreshedStockOverride > MaxRefreshedStockOverride)
	{
		return false;
	}
	OutStock = RandomStream.RandRange(MinRefreshedStockOverride, MaxRefreshedStockOverride);
	return OutStock > 0;
}

// 档位校验流程：重量下限必须是有限非负数，价格必须为正；两者都显式给过，这一档才可能参与估价。
bool FCatShopFishWeightPrice::IsRuntimeReady() const
{
	return FMath::IsFinite(MinimumWeightKilograms) && MinimumWeightKilograms >= 0.0 && Price > 0;
}
