#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "CatFishingScoopCooldownEffect.generated.h"

/** 抄网的 GAS 冷却载体；实际秒数由 UCatFishingSettings 在 Ability::ApplyCooldown 时写入 Spec。 */
UCLASS()
class CATFISHING_API UCatGE_FishingScoopCooldown : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UCatGE_FishingScoopCooldown();
};
