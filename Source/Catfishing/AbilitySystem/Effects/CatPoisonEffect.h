#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GameplayTagContainer.h"
#include "CatPoisonEffect.generated.h"

/** Poison 的正式即时增减效果；调用方只传入 SetByCaller 数值，不直接认识 Survival AttributeSet 的写入细节。 */
UCLASS()
class CATFISHING_API UCatGE_PoisonDelta : public UGameplayEffect
{
	GENERATED_BODY()

public:
	/** 构造 Poison 加法 GE；实际增减幅度由 ASC 提交时填入，资产表和 Condition 不持有另一份公式。 */
	UCatGE_PoisonDelta();

	/** 返回 Poison 增减使用的稳定 SetByCaller 标签；GE modifier 与提交方共用它，避免字符串分叉。 */
	static FGameplayTag GetPoisonDeltaTag();
};
