#include "ShopEconomy/CatShopEconomySettings.h"

// Runtime gate 流程：只接受显式开关和非负初始公款；两张 DataTable 的逐行合法性由实际购买或售鱼时的服务器读取裁决。
bool UCatShopEconomySettings::IsRuntimeEnabled() const
{
	return bEnableShopEconomyRuntime && StartingTeamWalletBalance >= 0;
}
