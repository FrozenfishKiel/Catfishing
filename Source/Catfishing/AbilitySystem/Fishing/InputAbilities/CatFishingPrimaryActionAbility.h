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

	/** WaitInputRelease 收到同一 AbilitySpec 的松开事件后提交 Released 边沿，并结束本次按住型 Ability。 */
	UFUNCTION()
	void HandleInputReleased(float ServerHeldSeconds);

	/** 外部取消也必须提交 PrimaryReleased；否则 GAS 停掉 Task 后服务器仍会把旧按键当成持续收线。 */
	virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	/** 当前激活是否已经向 Commands 提交释放边沿；Task 正常回调和外部取消共用它防止重复清持续输入。 */
	bool bReleaseSubmitted = false;

	/** 当前激活是否已成功提交按下边沿；取消只补偿已经进入 Commands 的输入，避免本地预检失败时制造无源 Release。 */
	bool bPressSubmitted = false;
};
