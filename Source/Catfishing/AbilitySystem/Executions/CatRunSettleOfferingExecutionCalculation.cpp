#include "AbilitySystem/Executions/CatRunSettleOfferingExecutionCalculation.h"

#include "AbilitySystem/Attributes/CatRunAttributeSet.h"
#include "AbilitySystem/Attributes/CatRunModifierAttributeSet.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"

namespace
{
	// 捕获定义流程：夜晚结算只读取 Run ASC 上已经规范化的目标、世界进度和来源倍率，供品计数仍由 Spec 的 SetByCaller 承载。
	struct FRunSettleOfferingStatics
	{
		/** 当日供品目标捕获；它由 StartDay GE 先写入，结算时复用该捕获值。 */
		FGameplayEffectAttributeCaptureDefinition DailyOfferingTargetDef;
		/** 当前世界进度捕获；结算在现值基础上增减，并把结果夹到 0 到 100。 */
		FGameplayEffectAttributeCaptureDefinition WorldProgressDef;
		/** 成功增益倍率捕获；它属于来源倍率属性集，不与最终世界进度混在一起。 */
		FGameplayEffectAttributeCaptureDefinition WorldProgressGainMultiplierDef;
		/** 失败损失倍率捕获；它属于来源倍率属性集，不与最终世界进度混在一起。 */
		FGameplayEffectAttributeCaptureDefinition WorldProgressLossMultiplierDef;

		FRunSettleOfferingStatics()
			: DailyOfferingTargetDef(UCatRunAttributeSet::GetDailyOfferingTargetAttribute(), EGameplayEffectAttributeCaptureSource::Target, false)
			, WorldProgressDef(UCatRunAttributeSet::GetWorldProgressAttribute(), EGameplayEffectAttributeCaptureSource::Target, false)
			, WorldProgressGainMultiplierDef(UCatRunModifierAttributeSet::GetWorldProgressGainMultiplierAttribute(), EGameplayEffectAttributeCaptureSource::Target, false)
			, WorldProgressLossMultiplierDef(UCatRunModifierAttributeSet::GetWorldProgressLossMultiplierAttribute(), EGameplayEffectAttributeCaptureSource::Target, false)
		{
		}
	};

	// 静态定义读取流程：复用唯一捕获描述，避免为单一结算公式建立共享查询层或运行时状态。
	const FRunSettleOfferingStatics& GetRunSettleOfferingStatics()
	{
		static const FRunSettleOfferingStatics Statics;
		return Statics;
	}

	// SetByCaller 整数读取流程：供品数量和基础奖惩必须是有限整数；浮点偏差会被拒绝而不是悄悄取整。
	bool TryReadSetByCallerInt(const FGameplayEffectSpec& Spec, const FGameplayTag& Tag, int32& OutValue)
	{
		OutValue = 0;
		const float RawValue = Spec.GetSetByCallerMagnitude(Tag, false, -1.0f);
		if (!FMath::IsFinite(RawValue) || RawValue < 0.0f || RawValue > MAX_int32
			|| !FMath::IsNearlyEqual(RawValue, static_cast<float>(FMath::RoundToInt(RawValue))))
		{
			return false;
		}
		OutValue = FMath::RoundToInt(RawValue);
		return true;
	}
}

// 构造流程：登记夜晚结算需要捕获的最终属性和来源倍率；供品数量仍通过 GE Spec 输入，避免把临时供品清单写成持久属性。
UCatRunSettleOfferingExecutionCalculation::UCatRunSettleOfferingExecutionCalculation()
{
	RelevantAttributesToCapture.Add(GetRunSettleOfferingStatics().DailyOfferingTargetDef);
	RelevantAttributesToCapture.Add(GetRunSettleOfferingStatics().WorldProgressDef);
	RelevantAttributesToCapture.Add(GetRunSettleOfferingStatics().WorldProgressGainMultiplierDef);
	RelevantAttributesToCapture.Add(GetRunSettleOfferingStatics().WorldProgressLossMultiplierDef);
}

// 供品点换算流程：先拒绝负数量和 int32 溢出，再按小/中/大/巨鱼的 1/2/4/10 点公式求和；空祭坛会得到 0 点并进入未达标结算。
bool UCatRunSettleOfferingExecutionCalculation::TryCalculateOfferingPointsFromCounts(const int32 SmallFishCount,
	const int32 MediumFishCount, const int32 LargeFishCount, const int32 GiantFishCount, int32& OutOfferedPoints)
{
	OutOfferedPoints = 0;
	if (SmallFishCount < 0 || MediumFishCount < 0 || LargeFishCount < 0 || GiantFishCount < 0)
	{
		return false;
	}
	const int64 OfferedPoints = static_cast<int64>(SmallFishCount)
		+ static_cast<int64>(MediumFishCount) * 2
		+ static_cast<int64>(LargeFishCount) * 4
		+ static_cast<int64>(GiantFishCount) * 10;
	if (OfferedPoints < 0 || OfferedPoints > MAX_int32)
	{
		return false;
	}
	OutOfferedPoints = static_cast<int32>(OfferedPoints);
	return true;
}

// 结算公式流程：先计算供品点和总鱼数，再按是否达标选择增益或损失；臭鱼每条折扣 25% 达标增益，最低为 0，失败损失不受臭鱼额外放大。
bool UCatRunSettleOfferingExecutionCalculation::TryCalculateSettlement(const int32 SmallFishCount,
	const int32 MediumFishCount, const int32 LargeFishCount, const int32 GiantFishCount,
	const int32 StinkyFishCount, const int32 BaseProgressGain, const int32 BaseProgressLoss,
	const int32 DailyOfferingTarget, const int32 CurrentWorldProgress, const float WorldProgressGainMultiplier,
	const float WorldProgressLossMultiplier, FCatRunOfferingSettlementResult& OutResult)
{
	OutResult = FCatRunOfferingSettlementResult();
	int32 OfferedPoints = 0;
	if (!TryCalculateOfferingPointsFromCounts(SmallFishCount, MediumFishCount, LargeFishCount, GiantFishCount,
		OfferedPoints))
	{
		return false;
	}
	const int64 TotalFishCount = static_cast<int64>(SmallFishCount) + MediumFishCount + LargeFishCount + GiantFishCount;
	if (StinkyFishCount < 0 || StinkyFishCount > TotalFishCount || BaseProgressGain < 0 || BaseProgressLoss < 0
		|| DailyOfferingTarget <= 0 || CurrentWorldProgress < 0 || CurrentWorldProgress > 100
		|| !FMath::IsFinite(WorldProgressGainMultiplier) || !FMath::IsFinite(WorldProgressLossMultiplier)
		|| WorldProgressGainMultiplier < 0.0f || WorldProgressLossMultiplier < 0.0f)
	{
		return false;
	}

	const bool bMetDailyTarget = OfferedPoints >= DailyOfferingTarget;
	int32 WorldProgressDelta = 0;
	if (bMetDailyTarget)
	{
		const double StinkyPenaltyMultiplier = FMath::Clamp(1.0 - static_cast<double>(StinkyFishCount) * 0.25, 0.0, 1.0);
		const double RawGain = static_cast<double>(BaseProgressGain) * WorldProgressGainMultiplier * StinkyPenaltyMultiplier;
		if (!FMath::IsFinite(RawGain) || RawGain < 0.0)
		{
			return false;
		}
		WorldProgressDelta = FMath::RoundToInt(FMath::Min(RawGain, 100.0));
	}
	else
	{
		const double RawLoss = static_cast<double>(BaseProgressLoss) * WorldProgressLossMultiplier;
		if (!FMath::IsFinite(RawLoss) || RawLoss < 0.0)
		{
			return false;
		}
		WorldProgressDelta = -FMath::RoundToInt(FMath::Min(RawLoss, 100.0));
	}

	OutResult.OfferedPoints = OfferedPoints;
	OutResult.WorldProgressDelta = WorldProgressDelta;
	OutResult.NewWorldProgress = FMath::Clamp(CurrentWorldProgress + WorldProgressDelta, 0, 100);
	OutResult.bMetDailyTarget = bMetDailyTarget;
	return true;
}

// 计算流程：
// 1. 从 Spec 读取夜晚祭坛冻结后的鱼数量、臭鱼数量和本日日程奖惩。
// 2. 从 Run ASC 捕获每日目标、当前世界进度和来源倍率，所有输入都必须是有限可结算整数。
// 3. 复用静态结算公式后覆盖 LastOfferingPoints、LastWorldProgressDelta 和 WorldProgress；不直接发送 StateTree 事件。
void UCatRunSettleOfferingExecutionCalculation::Execute_Implementation(
	const FGameplayEffectCustomExecutionParameters& ExecutionParams,
	FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
	const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();
	FAggregatorEvaluateParameters EvaluationParameters;
	float DailyOfferingTarget = 0.0f;
	float CurrentWorldProgress = 0.0f;
	float WorldProgressGainMultiplier = 1.0f;
	float WorldProgressLossMultiplier = 1.0f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(GetRunSettleOfferingStatics().DailyOfferingTargetDef,
		EvaluationParameters, DailyOfferingTarget);
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(GetRunSettleOfferingStatics().WorldProgressDef,
		EvaluationParameters, CurrentWorldProgress);
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(GetRunSettleOfferingStatics().WorldProgressGainMultiplierDef,
		EvaluationParameters, WorldProgressGainMultiplier);
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(GetRunSettleOfferingStatics().WorldProgressLossMultiplierDef,
		EvaluationParameters, WorldProgressLossMultiplier);

	int32 SmallFishCount = 0;
	int32 MediumFishCount = 0;
	int32 LargeFishCount = 0;
	int32 GiantFishCount = 0;
	int32 StinkyFishCount = 0;
	int32 BaseProgressGain = 0;
	int32 BaseProgressLoss = 0;
	if (!TryReadSetByCallerInt(Spec, CatFishingAbilityTags::Data_Run_Offering_SmallFishCount, SmallFishCount)
		|| !TryReadSetByCallerInt(Spec, CatFishingAbilityTags::Data_Run_Offering_MediumFishCount, MediumFishCount)
		|| !TryReadSetByCallerInt(Spec, CatFishingAbilityTags::Data_Run_Offering_LargeFishCount, LargeFishCount)
		|| !TryReadSetByCallerInt(Spec, CatFishingAbilityTags::Data_Run_Offering_GiantFishCount, GiantFishCount)
		|| !TryReadSetByCallerInt(Spec, CatFishingAbilityTags::Data_Run_Offering_StinkyFishCount, StinkyFishCount)
		|| !TryReadSetByCallerInt(Spec, CatFishingAbilityTags::Data_Run_Offering_BaseProgressGain, BaseProgressGain)
		|| !TryReadSetByCallerInt(Spec, CatFishingAbilityTags::Data_Run_Offering_BaseProgressLoss, BaseProgressLoss)
		|| !FMath::IsFinite(DailyOfferingTarget) || !FMath::IsNearlyEqual(DailyOfferingTarget, static_cast<float>(FMath::RoundToInt(DailyOfferingTarget)))
		|| !FMath::IsFinite(CurrentWorldProgress) || !FMath::IsNearlyEqual(CurrentWorldProgress, static_cast<float>(FMath::RoundToInt(CurrentWorldProgress))))
	{
		return;
	}

	FCatRunOfferingSettlementResult Settlement;
	if (!TryCalculateSettlement(SmallFishCount, MediumFishCount, LargeFishCount, GiantFishCount, StinkyFishCount,
		BaseProgressGain, BaseProgressLoss, FMath::RoundToInt(DailyOfferingTarget),
		FMath::RoundToInt(CurrentWorldProgress), WorldProgressGainMultiplier,
		WorldProgressLossMultiplier, Settlement))
	{
		return;
	}

	OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(UCatRunAttributeSet::GetLastOfferingPointsAttribute(),
		EGameplayModOp::Override, static_cast<float>(Settlement.OfferedPoints)));
	OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(UCatRunAttributeSet::GetLastWorldProgressDeltaAttribute(),
		EGameplayModOp::Override, static_cast<float>(Settlement.WorldProgressDelta)));
	OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(UCatRunAttributeSet::GetWorldProgressAttribute(),
		EGameplayModOp::Override, static_cast<float>(Settlement.NewWorldProgress)));
}
