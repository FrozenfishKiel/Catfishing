#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Fishing/CatFishingGameplayAbility.h"
#include "CatFishingCancelAbility.generated.h"

/** 取消输入 Ability；按下时提交一次取消意图，服务器会先取消 BodyAction 窗口再处理 Fishing 阶段取消。 */
UCLASS()
class CATFISHING_API UCatGA_FishingCancel : public UCatFishingGameplayAbility
{
	GENERATED_BODY()

public:
	/** 绑定取消 Ability Tag，授予后由 Cancel 输入 Tag 激活。 */
	UCatGA_FishingCancel();

	/** Cancel 按下时提交一次性取消命令；提交失败取消 Ability，提交成功立即结束。 */
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};
