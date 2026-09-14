#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Fishing/CatFishingGameplayAbility.h"
#include "CatFishingCancelAbility.generated.h"

/** 收竿输入 Ability；按下提交保持，松开或生命周期中断提交释放；权威计时决定搏斗放弃。 */
UCLASS()
class CATFISHING_API UCatGA_FishingCancel : public UCatFishingGameplayAbility
{
	GENERATED_BODY()

public:
	/** 绑定取消 Ability Tag，授予后由 Cancel 输入 Tag 激活。 */
	UCatGA_FishingCancel();

	/** 保持实例直至释放，供服务器完成 1.5 秒收竿与客户端进度反馈。 */
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void InputReleased(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayAbilityActivationInfo ActivationInfo) override;
	virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
};
