#include "ShopEconomy/CatShopCartCommandUtils.h"

namespace CatShopCartCommands
{
	bool IsPayloadWithinLimits(const TArray<FCatShopCartLineCommand>& Lines)
	{
		// 载荷边界检查流程：复用归一化入口但丢弃结果；这样购物车 RPC、报价和库存扣减永远共享同一套数量上限。
		TArray<FCatShopCartLineCommand> NormalizedLines;
		return NormalizeLines(Lines, NormalizedLines);
	}

	bool NormalizeLines(const TArray<FCatShopCartLineCommand>& Lines,
		TArray<FCatShopCartLineCommand>& OutLines)
	{
		// 归一化流程：
		// 1. 先拒绝空车、超长数组、非法 EntryId 和单行超限数量，避免异常输入进入查表、报价或扣库存。
		// 2. 再把重复 EntryId 聚合成同一行，并用差值判断防止聚合数量溢出上限。
		// 3. 最后按 EntryId 字符串排序，让幂等签名、报价和库存提交不受客户端数组顺序影响。
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
