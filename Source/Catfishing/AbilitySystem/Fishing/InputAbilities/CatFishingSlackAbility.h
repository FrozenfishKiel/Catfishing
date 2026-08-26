#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Fishing/CatFishingGameplayAbility.h"
#include "CatFishingSlackAbility.generated.h"

/** 右键放线 Ability；按住期间向服务器发送放线边沿，仅 HookedFight 阶段会被服务器解释成有效放线。 */
UCLASS()
class CATFISHING_API UCatGA_FishingSlack : public UCatFishingGameplayAbility
{
	GENERATED_BODY()

public:
	/** 绑定放线 Ability Tag，授予后由 Slack 输入 Tag 进入按住型激活流程。 */
	UCatGA_FishingSlack();

	/** Slack 按下时提交放线 Pressed 边沿；成功后保持 Ability 存活直到输入松开。 */
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	/** Slack 松开时提交放线 Released 边沿并结束 Ability，让服务器清除放线输入状态。 */
	virtual void InputReleased(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo) override;
};
