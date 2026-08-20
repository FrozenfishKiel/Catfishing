#include "Data/CatFishDefinition.h"

// 捕获 readiness 流程：只校验进入钓鱼、捕获、鱼护和献祭链所需的字段；食用效果是另一条写口，不能反向阻塞正式鱼表落盘。
// 必填集合刻意不超出鱼表格真实存在的列，外加工程侧的显式启用 gate（bEnableRuntimeDefinition）和由最大重量乘力量系数
// 派生出来的 FishStrength：稀有度、性格模板、时段、天气和出现权重在鱼表格里都没有对应列，把它们列为必填只会逼导入
// 流程编造占位值，所以这里不再要求它们。
// SacrificeContribution 要的是"非 0"而不是"大于 0"：它现在表达世界进度增减，臭臭鱼这类负进度鱼是合法数据；
// 但 0 仍然只可能来自没填这一列，所以继续把 0 判成未就绪，不让缺列的鱼行悄悄进正式目录。
bool UCatFishDefinition::IsRuntimeDefinitionReady() const
{
	return bEnableRuntimeDefinition && !FishDefinitionId.IsNone()
		&& BodyClass != ECatFishBodyClass::Unknown && SacrificeContribution != 0
		&& RegionIds.Num() > 0 && ChumAffinities.Num() > 0
		&& FMath::IsFinite(MinimumWeightKilograms) && MinimumWeightKilograms > 0.0
		&& FMath::IsFinite(MaximumWeightKilograms) && MaximumWeightKilograms >= MinimumWeightKilograms
		&& MinimumFightParticipants >= 1 && MinimumFightParticipants <= 8
		&& FMath::IsFinite(FishStrength) && FishStrength > 0.0
		&& FMath::IsFinite(FishFightStamina) && FishFightStamina > 0.0;
}

// 食用 readiness 流程：在捕获字段已可运行后，再校验吃鱼链真正会写入的 Poison 数值，未裁决食用效果时保持 fail-closed。
// 饥饿系统已删除，不再要求 Hunger 收益。
bool UCatFishDefinition::HasRuntimeConsumptionEffect() const
{
	if (!IsRuntimeDefinitionReady())
	{
		return false;
	}
	if (FoodSafety == ECatFishFoodSafety::Safe)
	{
		return FMath::IsNearlyZero(PoisonIncrease);
	}
	if (FoodSafety == ECatFishFoodSafety::Toxic)
	{
		return FMath::IsFinite(PoisonIncrease) && PoisonIncrease > 0.0;
	}
	return false;
}
