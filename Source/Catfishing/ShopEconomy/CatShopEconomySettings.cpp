#include "ShopEconomy/CatShopEconomySettings.h"

// Runtime gate 流程：只接受显式开关和非负初始公款；具体目录重复、缺字段和免费饵引用由服务初始化后暴露为目录不可用。
// StartingTeamWalletBalance 判 >= 0 就是在拦"这条没裁过"：它默认是哨兵 -1，只有配置里显式写过才可能过 gate。
bool UCatShopEconomySettings::IsRuntimeEnabled() const
{
	return bEnableShopEconomyRuntime && StartingTeamWalletBalance >= 0 && MinimumFishSaleValue > 0;
}

// 估价流程：先清输出，再整表校验，最后取重量落到的最高一档。
// 整表校验放在每次估价里而不是加载时做一次，是因为调用方可能持有一份开局冻结的副本，
// 让判定跟着表走才能保证"这张表能不能定价"和"这条鱼多少钱"永远是同一份依据。
bool UCatShopEconomySettings::TryEvaluateFishPurchasePrice(const TArray<FCatShopFishWeightPrice>& Anchors,
	const double WeightKilograms, int32& OutPrice)
{
	OutPrice = 0;
	if (Anchors.IsEmpty())
	{
		return false;
	}
	for (int32 Index = 0; Index < Anchors.Num(); ++Index)
	{
		if (!Anchors[Index].IsRuntimeReady())
		{
			return false;
		}
		// 重量严格递增保证档位边界唯一，价格不递减对应飞书"鱼体重重＝钱多"；
		// 任一条被破坏都说明这张表不是一条合法的体重轴，此时宁可整体不定价。
		if (Index > 0 && (Anchors[Index].MinimumWeightKilograms <= Anchors[Index - 1].MinimumWeightKilograms
			|| Anchors[Index].Price < Anchors[Index - 1].Price))
		{
			return false;
		}
	}
	if (!FMath::IsFinite(WeightKilograms) || WeightKilograms <= 0.0)
	{
		return false;
	}
	const FCatShopFishWeightPrice* Matched = nullptr;
	for (const FCatShopFishWeightPrice& Anchor : Anchors)
	{
		if (Anchor.MinimumWeightKilograms > WeightKilograms)
		{
			break;
		}
		Matched = &Anchor;
	}
	if (!Matched)
	{
		return false;
	}
	OutPrice = Matched->Price;
	return true;
}
