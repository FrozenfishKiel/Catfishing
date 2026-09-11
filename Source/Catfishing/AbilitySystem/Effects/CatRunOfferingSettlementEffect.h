#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "CatRunOfferingSettlementEffect.generated.h"

/** 夜晚供品结算的即时 Run GE；它承载供品计数和当日日程奖惩，并委托 ExecCalc 写世界进度结果。 */
UCLASS()
class CATFISHING_API UCatGE_RunSettleOffering : public UGameplayEffect
{
	GENERATED_BODY()

public:
	/** 配置即时执行计算，应用后不保留持续 Modifier，最终供品点和世界进度由 AttributeSet 保存和复制。 */
	UCatGE_RunSettleOffering();
};
