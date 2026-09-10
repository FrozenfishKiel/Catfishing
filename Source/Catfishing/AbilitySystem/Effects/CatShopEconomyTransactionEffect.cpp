#include "AbilitySystem/Effects/CatShopEconomyTransactionEffect.h"

#include "AbilitySystem/Executions/CatShopEconomyTransactionExecutionCalculation.h"

// 构造流程：声明即时 GE 并挂接唯一交易计算器；购买与售鱼都由同一条 GAS 写口修改团队余额。
UCatGE_ShopEconomyTransaction::UCatGE_ShopEconomyTransaction()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayEffectExecutionDefinition& Execution = Executions.AddDefaulted_GetRef();
	Execution.CalculationClass = UCatShopEconomyTransactionExecutionCalculation::StaticClass();
}
