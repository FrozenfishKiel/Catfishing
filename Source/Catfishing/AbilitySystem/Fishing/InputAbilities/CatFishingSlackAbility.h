#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Fishing/CatFishingGameplayAbility.h"
#include "CatFishingSlackAbility.generated.h"

/** 右键放线 Ability；向服务器发送输入边沿，由 HookedFight / ExhaustedReel 的 Runner 裁决放线与回体。 */
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

	/** WaitInputRelease 收到同一 AbilitySpec 的松开事件后提交 SlackReleased，并结束本次按住型 Ability。 */
	UFUNCTION()
	void HandleInputReleased(float ServerHeldSeconds);

	/** 外部取消也必须提交 SlackReleased；否则 GAS 停掉 Task 后服务器仍会把旧右键当成持续放线。 */
	virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	/** 当前激活是否已经向 Commands 提交释放边沿；Task 正常回调和外部取消共用它防止重复清持续输入。 */
	bool bReleaseSubmitted = false;

	/** 当前激活是否已成功提交按下边沿；取消只补偿已经进入 Commands 的输入，避免本地预检失败时制造无源 Release。 */
	bool bPressSubmitted = false;
};
