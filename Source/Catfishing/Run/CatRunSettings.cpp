#include "Run/CatRunSettings.h"

// Runtime gate 计算流程：所有构建都要求显式总开关与当前唯一支持的 FixedDailyOfferingTarget 策略；任何未裁人数规则都不能启动 RunFlow。
bool UCatRunSettings::IsRuntimeReady() const
{
	return bEnableRunRuntime && PlayerScalingPolicy == ECatRunScalingPolicy::FixedDailyOfferingTarget;
}

// 白天参数读取流程：先把输出恢复为 Unset；随后复用 runtime gate，并按天数读取日程项；超过表长时复用最后一项，失败不留下部分可用参数。
bool UCatRunSettings::TryGetDayParameters(const int32 DayIndex, float& OutDayLengthSeconds,
	FCatRunDailyOfferingTuning& OutTuning) const
{
	OutDayLengthSeconds = 0.0f;
	OutTuning = FCatRunDailyOfferingTuning();
	if (!IsRuntimeReady() || !FMath::IsFinite(DayLengthSeconds)
		|| DayLengthSeconds <= 0.0f || DayIndex <= 0 || DailyOfferingSchedule.IsEmpty())
	{
		return false;
	}
	const int32 ScheduleIndex = FMath::Clamp(DayIndex - 1, 0, DailyOfferingSchedule.Num() - 1);
	const FCatRunDailyOfferingTuning& Tuning = DailyOfferingSchedule[ScheduleIndex];
	if (Tuning.DailyOfferingTarget <= 0 || Tuning.WorldProgressGain <= 0 || Tuning.WorldProgressLoss <= 0)
	{
		return false;
	}
	OutDayLengthSeconds = DayLengthSeconds;
	OutTuning = Tuning;
	return true;
}

// 初始世界进度读取流程：只接受明确配置的 1 到 100 整数；GameMode 会把它写入 Run ASC 和公开 DTO，之后结算只通过 GE 修改。
bool UCatRunSettings::TryGetInitialWorldProgress(int32& OutWorldProgress) const
{
	OutWorldProgress = 0;
	if (!IsRuntimeReady() || InitialWorldProgress <= 0 || InitialWorldProgress > 100)
	{
		return false;
	}
	OutWorldProgress = InitialWorldProgress;
	return true;
}

// 供品重量分类流程：先验证三段边界严格递增，再按策划案的小于关系给本条实际重量定档；输出点数只用于夜晚结算命令，不写回鱼实例。
bool UCatRunSettings::TryClassifyOfferingWeight(const double WeightKilograms,
	ECatOfferingWeightClass& OutWeightClass, int32& OutOfferingPoints) const
{
	OutWeightClass = ECatOfferingWeightClass::Small;
	OutOfferingPoints = 0;
	if (!IsRuntimeReady() || !FMath::IsFinite(WeightKilograms) || WeightKilograms <= 0.0
		|| !FMath::IsFinite(SmallOfferingMaxWeightKilograms)
		|| !FMath::IsFinite(MediumOfferingMaxWeightKilograms)
		|| !FMath::IsFinite(LargeOfferingMaxWeightKilograms)
		|| SmallOfferingMaxWeightKilograms <= 0.0
		|| MediumOfferingMaxWeightKilograms <= SmallOfferingMaxWeightKilograms
		|| LargeOfferingMaxWeightKilograms <= MediumOfferingMaxWeightKilograms)
	{
		return false;
	}
	if (WeightKilograms < SmallOfferingMaxWeightKilograms)
	{
		OutWeightClass = ECatOfferingWeightClass::Small;
		OutOfferingPoints = 1;
		return true;
	}
	if (WeightKilograms < MediumOfferingMaxWeightKilograms)
	{
		OutWeightClass = ECatOfferingWeightClass::Medium;
		OutOfferingPoints = 2;
		return true;
	}
	if (WeightKilograms < LargeOfferingMaxWeightKilograms)
	{
		OutWeightClass = ECatOfferingWeightClass::Large;
		OutOfferingPoints = 4;
		return true;
	}
	OutWeightClass = ECatOfferingWeightClass::Giant;
	OutOfferingPoints = 10;
	return true;
}

// 臭鱼判定流程：只把配置中的稳定 FishDefinitionId 当作增益折扣来源；未配置或普通鱼都返回 false，避免按显示名猜测。
bool UCatRunSettings::IsStinkyOfferingFish(const FName FishDefinitionId) const
{
	return !FishDefinitionId.IsNone() && StinkyOfferingFishDefinitionIds.Contains(FishDefinitionId);
}

// 成功结算资格计算流程：先要求策略总开关显式启用，再要求世界进度达到策划案的 100；毕业裁决只看世界进度。
bool UCatRunSettings::CanEnterSuccessSettlementNight(const int32 WorldProgress) const
{
	return IsSuccessSettlementEnabled() && WorldProgress >= 100;
}

// 成功终局 gate 读取流程：只有显式 Enabled 才表示成功结算功能开放；是否到最终天由 CanEnterSuccessSettlementNight 继续裁决。
bool UCatRunSettings::IsSuccessSettlementEnabled() const
{
	return SuccessSettlementPolicy == ECatRunPolicyDecision::Enabled;
}
