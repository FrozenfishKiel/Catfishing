#include "Growth/CatGrowthSettings.h"

// 就绪检查流程：只认显式 gate 和正槽长；三选一配表是否可用另由 IsChoiceRuntimeReady 裁决，
// 配表为空时经验照记、待选次数照攒，但不会抽出一组空选项。
bool UCatGrowthSettings::IsRuntimeReady() const
{
	return bEnableGrowthRuntime && ExperiencePerChoiceSlot > 0;
}

// 三选一就绪流程：成长 runtime 就绪、每组抽取数为正、池内有效行不少于一组的项数，且不超过准入三闸的池上限。
bool UCatGrowthSettings::IsChoiceRuntimeReady() const
{
	if (!IsRuntimeReady() || OptionsPerOffer <= 0 || MaxOptionPoolSize <= 0)
	{
		return false;
	}
	int32 ValidRows = 0;
	for (const FCatGrowthOptionConfig& Row : OptionPool)
	{
		if (Row.OptionId != ECatGrowthOptionId::None && FMath::IsFinite(Row.MagnitudePerPick)
			&& Row.MagnitudePerPick != 0.0 && FMath::IsFinite(Row.MaxTotalMagnitude)
			&& Row.MaxTotalMagnitude >= 0.0 && Row.UnlockAtChoiceOrdinal >= 1)
		{
			++ValidRows;
		}
	}
	return ValidRows >= OptionsPerOffer && ValidRows <= MaxOptionPoolSize;
}

// 配表查找流程：按身份线性查一遍 11 行级别的小表；重复行取首行，避免同一项被配两套数值时行为取决于遍历顺序。
const FCatGrowthOptionConfig* UCatGrowthSettings::FindOptionConfig(const ECatGrowthOptionId OptionId) const
{
	if (OptionId == ECatGrowthOptionId::None)
	{
		return nullptr;
	}
	return OptionPool.FindByPredicate([OptionId](const FCatGrowthOptionConfig& Row)
	{
		return Row.OptionId == OptionId;
	});
}
