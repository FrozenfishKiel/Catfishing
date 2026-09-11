#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "CatRunStartDayEffect.generated.h"

/** 新一天的即时 Run GE；它只承载 SetByCaller 基础供品目标并委托 ExecCalc 写每日目标和清空上一晚结果。 */
UCLASS()
class CATFISHING_API UCatGE_RunStartDay : public UGameplayEffect
{
	GENERATED_BODY()

public:
	/** 配置即时执行计算，应用后不会留下持续 Effect 或第二份 Run 状态。 */
	UCatGE_RunStartDay();
};
