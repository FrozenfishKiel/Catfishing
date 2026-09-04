#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectExecutionCalculation.h"
#include "CatRunStartDayExecutionCalculation.generated.h"

/** 新一天额度的唯一数值计算器；读取目标输入与 Run 倍率，输出目标覆盖和零进度，不拥有阶段或 Revision。 */
UCLASS()
class CATFISHING_API UCatRunStartDayExecutionCalculation : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	/** 注册 DayStart 所需的目标倍率和日压力捕获，确保 GE Spec 在应用时读取 GameState Run ASC 的当前值。 */
	UCatRunStartDayExecutionCalculation();

	/** 将基础额度、目标倍率和每日压力规整成当天目标；GameMode 应用前预演它，ExecCalc 执行时复用它。 */
	static bool TryCalculateQuotaTarget(float BaseQuotaTarget, float QuotaTargetMultiplier, float DailyPressure, int32& OutQuotaTarget);

	/** 读取 SetByCaller 基础目标与捕获倍率，输出覆盖后的目标和零进度；非法输入不输出 modifier，由调用方 fail-closed。 */
	virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
		FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;
};
