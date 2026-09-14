#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "CatFishingStaminaEffect.generated.h"

UCLASS()
class CATFISHING_API UCatGE_FishingStaminaDelta : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UCatGE_FishingStaminaDelta();
	static FGameplayTag GetYellowDeltaTag();
};

/** 黄色体力授予/跨天清除。自然恢复不能使用此效果。 */
UCLASS()
class CATFISHING_API UCatGE_YellowFightStaminaDelta : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UCatGE_YellowFightStaminaDelta();
};
