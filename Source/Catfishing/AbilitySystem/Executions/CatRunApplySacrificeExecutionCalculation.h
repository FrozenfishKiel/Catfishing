#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectExecutionCalculation.h"
#include "CatRunApplySacrificeExecutionCalculation.generated.h"

/** 献祭贡献的唯一数值计算器；读取冻结原始贡献与 Run 效率，向额度进度追加实际结果，不处理 Items 事务。 */
UCLASS()
class CATFISHING_API UCatRunApplySacrificeExecutionCalculation : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	/** 注册献祭效率捕获，保证贡献在 GE 应用瞬间按 Run ASC 当前倍率计算。 */
	UCatRunApplySacrificeExecutionCalculation();

	/** 将冻结原始贡献和当前献祭效率规整成写入额度的整数值；GameMode 预检和 GE 执行共用它，保证鱼提交前后只有一套公式。 */
	static bool TryCalculateAppliedContribution(float RawContribution, float SacrificeEfficiency, int32& OutAppliedContribution);

	/** 读取 SetByCaller 原始贡献与效率，输出 Additive 进度；非法输入不输出 modifier，不封顶超额进度。 */
	virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
		FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;
};
