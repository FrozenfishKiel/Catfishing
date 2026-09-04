#include "AbilitySystem/Executions/CatRunApplySacrificeExecutionCalculation.h"

#include "AbilitySystem/Attributes/CatRunAttributeSet.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"

namespace
{
	// 捕获定义流程：献祭只从目标 Run ASC 取效率，让协调器继续只负责冻结 Items 事务而不拥有数值公式。
	struct FRunApplySacrificeStatics
	{
		/** 献祭效率的目标属性捕获；GE 应用时读取当前 Run ASC，协调器不持有倍率。 */
		FGameplayEffectAttributeCaptureDefinition SacrificeEfficiencyDef;
		/** 当前进度的目标属性捕获；只用于执行前阻断 int32 溢出，绝不将进度夹到目标。 */
		FGameplayEffectAttributeCaptureDefinition QuotaProgressDef;

		FRunApplySacrificeStatics()
			: SacrificeEfficiencyDef(UCatRunAttributeSet::GetSacrificeEfficiencyAttribute(), EGameplayEffectAttributeCaptureSource::Target, false)
			, QuotaProgressDef(UCatRunAttributeSet::GetQuotaProgressAttribute(), EGameplayEffectAttributeCaptureSource::Target, false)
		{
		}
	};

	// 静态定义读取流程：复用唯一捕获描述，避免为单一公式建立共享查询层或运行时状态。
	const FRunApplySacrificeStatics& GetRunApplySacrificeStatics()
	{
		static const FRunApplySacrificeStatics Statics;
		return Statics;
	}
}

// 构造流程：登记献祭效率和当前进度的目标 Attribute 捕获，GE 应用时从当前 GameState Run ASC 读取倍率并先阻断整型溢出。
UCatRunApplySacrificeExecutionCalculation::UCatRunApplySacrificeExecutionCalculation()
{
	RelevantAttributesToCapture.Add(GetRunApplySacrificeStatics().SacrificeEfficiencyDef);
	RelevantAttributesToCapture.Add(GetRunApplySacrificeStatics().QuotaProgressDef);
}

// 贡献换算流程：先校验原始贡献和效率都是可结算正数，再用 double 完成乘法并限制到 int32，最后四舍五入成 Run 进度实际写入值。
bool UCatRunApplySacrificeExecutionCalculation::TryCalculateAppliedContribution(const float RawContribution,
	const float SacrificeEfficiency, int32& OutAppliedContribution)
{
	OutAppliedContribution = 0;
	const double AppliedContribution = static_cast<double>(RawContribution) * SacrificeEfficiency;
	if (!FMath::IsFinite(RawContribution) || !FMath::IsFinite(SacrificeEfficiency)
		|| RawContribution <= 0.0f || SacrificeEfficiency <= 0.0f
		|| !FMath::IsFinite(AppliedContribution) || AppliedContribution > MAX_int32)
	{
		return false;
	}
	OutAppliedContribution = FMath::RoundToInt(AppliedContribution);
	return OutAppliedContribution > 0;
}

// 计算流程：
// 1. 从 Spec 读取 Items 已冻结的原始贡献，并捕获当前献祭效率。
// 2. 拒绝非有限、非正或超出 int32 范围的结果，防止已提交事务把坏数值写入 Run Attribute。
// 3. 把四舍五入后的实际贡献以 Additive 写入 QuotaProgress，刻意不按 QuotaTarget 封顶以保留超额语义。
void UCatRunApplySacrificeExecutionCalculation::Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
	FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
	const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();
	FAggregatorEvaluateParameters EvaluationParameters;
	float SacrificeEfficiency = 1.0f;
	float CurrentProgress = 0.0f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(GetRunApplySacrificeStatics().SacrificeEfficiencyDef,
		EvaluationParameters, SacrificeEfficiency);
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(GetRunApplySacrificeStatics().QuotaProgressDef,
		EvaluationParameters, CurrentProgress);
	const float RawContribution = Spec.GetSetByCallerMagnitude(CatFishingAbilityTags::Data_Run_Sacrifice_RawContribution, false, -1.0f);
	int32 RoundedContribution = 0;
	if (!TryCalculateAppliedContribution(RawContribution, SacrificeEfficiency, RoundedContribution))
	{
		return;
	}
	if (static_cast<double>(CurrentProgress) + RoundedContribution > MAX_int32)
	{
		return;
	}
	OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(UCatRunAttributeSet::GetQuotaProgressAttribute(),
		EGameplayModOp::Additive, static_cast<float>(RoundedContribution)));
}
