#include "AbilitySystem/Effects/CatRunSacrificeContributionEffect.h"

#include "AbilitySystem/Executions/CatRunApplySacrificeExecutionCalculation.h"

// 构造流程：声明 Instant GE 并挂接唯一献祭 ExecCalc；原始贡献由协调器通过 GameMode 的既有事务写口填入 Spec。
UCatGE_RunApplySacrifice::UCatGE_RunApplySacrifice()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayEffectExecutionDefinition& Execution = Executions.AddDefaulted_GetRef();
	Execution.CalculationClass = UCatRunApplySacrificeExecutionCalculation::StaticClass();
}
