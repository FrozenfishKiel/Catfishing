#include "AbilitySystem/Executions/CatRunStartDayExecutionCalculation.h"

#include "AbilitySystem/Attributes/CatRunAttributeSet.h"
#include "AbilitySystem/Attributes/CatRunModifierAttributeSet.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"

namespace
{
	// 捕获定义流程：DayStart 只在目标 Run ASC 捕获来源倍率集，避免 GameMode 或 UI 再手写同一乘法。
	struct FRunStartDayStatics
	{
		/** 每日目标倍率来源捕获；它属于 RunModifierSet，不与最终 DailyOfferingTarget 混在同一个 AttributeSet。 */
		FGameplayEffectAttributeCaptureDefinition DailyOfferingTargetMultiplierDef;
		/** 每日压力来源捕获；它只表达数值输入，不把天气、天数或环境事件属性化。 */
		FGameplayEffectAttributeCaptureDefinition DailyPressureDef;

		FRunStartDayStatics()
			: DailyOfferingTargetMultiplierDef(UCatRunModifierAttributeSet::GetDailyOfferingTargetMultiplierAttribute(), EGameplayEffectAttributeCaptureSource::Target, false)
			, DailyPressureDef(UCatRunModifierAttributeSet::GetDailyPressureAttribute(), EGameplayEffectAttributeCaptureSource::Target, false)
		{
		}
	};

	// 静态定义读取流程：为每个 ExecCalc 复用一组 Attribute capture 描述，不创建运行时注册表或第二份数值状态。
	const FRunStartDayStatics& GetRunStartDayStatics()
	{
		static const FRunStartDayStatics Statics;
		return Statics;
	}
}

// 构造流程：把本 ExecCalc 可能读取的目标倍率和日压力加入 GE 捕获集合，应用时由 GAS 提供当前聚合值。
UCatRunStartDayExecutionCalculation::UCatRunStartDayExecutionCalculation()
{
	RelevantAttributesToCapture.Add(GetRunStartDayStatics().DailyOfferingTargetMultiplierDef);
	RelevantAttributesToCapture.Add(GetRunStartDayStatics().DailyPressureDef);
}

// 目标换算流程：先要求基础目标与两个倍率都是可结算正数，再用 double 承接乘法，最后把可复制的当天目标限制在 int32 正区间。
bool UCatRunStartDayExecutionCalculation::TryCalculateDailyOfferingTarget(const float BaseDailyOfferingTarget,
	const float DailyOfferingTargetMultiplier, const float DailyPressure, int32& OutDailyOfferingTarget)
{
	OutDailyOfferingTarget = 0;
	const double CalculatedTarget = static_cast<double>(BaseDailyOfferingTarget) * DailyOfferingTargetMultiplier * DailyPressure;
	if (!FMath::IsFinite(BaseDailyOfferingTarget) || !FMath::IsFinite(DailyOfferingTargetMultiplier) || !FMath::IsFinite(DailyPressure)
		|| BaseDailyOfferingTarget <= 0.0f || DailyOfferingTargetMultiplier <= 0.0f || DailyPressure <= 0.0f
		|| !FMath::IsFinite(CalculatedTarget) || CalculatedTarget > MAX_int32)
	{
		return false;
	}
	OutDailyOfferingTarget = FMath::RoundToInt(CalculatedTarget);
	return OutDailyOfferingTarget > 0;
}

// 计算流程：
// 1. 从 Spec 读取服务器提交的基础目标，并从目标 Run ASC 捕获两个倍率。
// 2. 仅接受有限且为正的输入，避免坏数据把 Attribute 写成不可恢复的 NaN 或负目标。
// 3. 用四舍五入后的乘积覆盖 DailyOfferingTarget，并清空上次供品点和上次进度变化；不接触 DTO、Revision 或 StateTree。
void UCatRunStartDayExecutionCalculation::Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
	FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
	const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();
	FAggregatorEvaluateParameters EvaluationParameters;
	float DailyOfferingTargetMultiplier = 1.0f;
	float DailyPressure = 1.0f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(GetRunStartDayStatics().DailyOfferingTargetMultiplierDef,
		EvaluationParameters, DailyOfferingTargetMultiplier);
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(GetRunStartDayStatics().DailyPressureDef,
		EvaluationParameters, DailyPressure);
	const float BaseDailyOfferingTarget = Spec.GetSetByCallerMagnitude(CatFishingAbilityTags::Data_Run_BaseDailyOfferingTarget, false, -1.0f);
	int32 RoundedTarget = 0;
	if (!TryCalculateDailyOfferingTarget(BaseDailyOfferingTarget, DailyOfferingTargetMultiplier, DailyPressure, RoundedTarget))
	{
		return;
	}
	OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(UCatRunAttributeSet::GetDailyOfferingTargetAttribute(),
		EGameplayModOp::Override, static_cast<float>(RoundedTarget)));
	OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(UCatRunAttributeSet::GetLastOfferingPointsAttribute(),
		EGameplayModOp::Override, 0.0f));
	OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(UCatRunAttributeSet::GetLastWorldProgressDeltaAttribute(),
		EGameplayModOp::Override, 0.0f));
}
