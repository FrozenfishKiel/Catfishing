#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Fishing/CatFishingGameplayAbility.h"
#include "CatFishingPrimaryActionAbility.generated.h"

/** Primary 输入 Ability；按下建立按住状态，松开时让服务器按当前阶段解释为抛竿、提竿或收线。 */
UCLASS()
class CATFISHING_API UCatGA_FishingPrimaryAction : public UCatFishingGameplayAbility
{
	GENERATED_BODY()

public:
	/** 绑定 Primary Ability Tag，授予后由 Primary 输入 Tag 进入按住型激活流程。 */
	UCatGA_FishingPrimaryAction();

	/** Primary 按下时提交 Pressed 边沿；成功后保持 Ability 存活直到输入松开。 */
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	/** Primary 松开时提交 Released 边沿并结束 Ability；服务器继续裁决真实钓鱼语义。 */
	virtual void InputReleased(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo) override;
};
