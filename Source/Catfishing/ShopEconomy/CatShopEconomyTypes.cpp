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

// 档位校验流程：重量下限必须是有限非负数，价格必须为正；两者都显式给过，这一档才可能参与估价。
bool FCatShopFishWeightPrice::IsRuntimeReady() const
{
	return FMath::IsFinite(MinimumWeightKilograms) && MinimumWeightKilograms >= 0.0 && Price > 0;
}
