#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Fishing/CatFishingGameplayAbility.h"
#include "CatFishingRodInteractAbility.generated.h"

/** 鱼竿互动输入 Ability；按下时提交“与当前鱼竿状态互动”的一次性玩家意图。 */
UCLASS()
class CATFISHING_API UCatGA_FishingRodInteract : public UCatFishingGameplayAbility
{
	GENERATED_BODY()

public:
	/** 绑定鱼竿互动 Ability Tag，授予后由对应输入 Tag 激活。 */
	UCatGA_FishingRodInteract();

	/** 输入按下时触发鱼竿互动命令；提交失败会取消本次 Ability，成功后立即结束。 */
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};
