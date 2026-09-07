#pragma once

#include "CoreMinimal.h"
#include "ShopEconomy/Trading/CatShopTradingTypes.h"

namespace CatShopCartCommands
{
	/** 判断原始购物车 RPC 行是否满足服务器输入边界；只检查 EntryId 和数量形状，不读取价格、库存或摊位目录。 */
	CATFISHING_API bool IsPayloadWithinLimits(const TArray<FCatShopCartLineCommand>& Lines);

	/** 合并重复 EntryId 并按稳定顺序输出购物车行；失败时清空输出，调用方据此在查表或扣款前关闭请求。 */
	CATFISHING_API bool NormalizeLines(const TArray<FCatShopCartLineCommand>& Lines,
		TArray<FCatShopCartLineCommand>& OutLines);
}
