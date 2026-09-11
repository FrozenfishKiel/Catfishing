#include "AbilitySystem/Effects/CatRunOfferingSettlementEffect.h"

#include "AbilitySystem/Executions/CatRunSettleOfferingExecutionCalculation.h"

// 构造流程：声明 Instant GE 并挂接唯一夜晚供品结算 ExecCalc；供品计数和本日日程由 GameMode 在创建 Spec 时填入。
UCatGE_RunSettleOffering::UCatGE_RunSettleOffering()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayEffectExecutionDefinition& Execution = Executions.AddDefaulted_GetRef();
	Execution.CalculationClass = UCatRunSettleOfferingExecutionCalculation::StaticClass();
}
