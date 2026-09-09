#include "AbilitySystem/Effects/CatRunStartDayEffect.h"

#include "AbilitySystem/Executions/CatRunStartDayExecutionCalculation.h"

// 构造流程：声明 Instant GE 并挂接唯一 DayStart ExecCalc；SetByCaller 标签由 GameMode 在创建 Spec 时填入。
UCatGE_RunStartDay::UCatGE_RunStartDay()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayEffectExecutionDefinition& Execution = Executions.AddDefaulted_GetRef();
	Execution.CalculationClass = UCatRunStartDayExecutionCalculation::StaticClass();
}
