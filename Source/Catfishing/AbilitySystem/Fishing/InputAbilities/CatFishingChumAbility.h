#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Fishing/CatFishingGameplayAbility.h"
#include "CatFishingChumAbility.generated.h"

/** 打窝输入 Ability；按下开始蓄力，松开时提交投放边沿让服务器按时长结算。 */
UCLASS()
class CATFISHING_API UCatGA_FishingChum : public UCatFishingGameplayAbility
{
	GENERATED_BODY()

public:
	/** 绑定打窝 Ability Tag，授予后由 Chum 输入 Tag 进入按住型激活流程。 */
	UCatGA_FishingChum();

	/** Chum 按下时提交蓄力开始边沿；提交成功后保持 Ability 存活直到输入松开。 */
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	/** Chum 松开时提交蓄力结束边沿并结束 Ability；投放距离和窝料量由服务器按时长裁决。 */
	virtual void InputReleased(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo) override;
};
