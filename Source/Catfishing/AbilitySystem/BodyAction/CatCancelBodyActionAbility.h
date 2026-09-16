#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "CatCancelBodyActionAbility.generated.h"

/** 默认 X 取消 Ability；它只终止正在前摇的 BodyAction，不拥有 BodyAction 标签，避免取消操作把自己选为目标。 */
UCLASS()
class CATFISHING_API UCatGA_CancelBodyAction : public UGameplayAbility
{
	GENERATED_BODY()
public:
	/** 配置服务器权威和按 Actor 实例化；默认 AbilitySet 用 Cancel 输入 Tag 激活它。 */
	UCatGA_CancelBodyAction();
	/** 按下 X 时定位当前 ASC，取消带 BodyAction 标签的活动能力后立即结束自身，不发送 Fishing 命令。 */
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};
