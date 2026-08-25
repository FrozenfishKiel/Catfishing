#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Fishing/CatFishingGameplayAbility.h"
#include "CatFishingScoopAbility.generated.h"

/** 抄鱼输入 Ability；按下时提交一次抢抄意图，成功与否由服务器会话裁决。 */
UCLASS()
class CATFISHING_API UCatGA_FishingScoop : public UCatFishingGameplayAbility
{
	GENERATED_BODY()

public:
	/** 绑定抄鱼 Ability Tag，授予后由 Scoop 输入 Tag 激活。 */
	UCatGA_FishingScoop();

	/** Scoop 按下时播放本地挥网表现并提交抢抄命令；权威结果通过命令回包或会话快照返回。 */
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};
