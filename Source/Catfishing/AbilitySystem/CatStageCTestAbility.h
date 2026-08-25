#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "CatStageCTestAbility.generated.h"

/** 阶段 C 的真实诊断 Ability；只在服务器执行，通过 ASC 修改 Poison，用完立即 End，配置未就绪则正常 Cancel。 */
UCLASS()
class CATFISHING_API UCatStageCTestAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	/** 固定 ServerOnly 执行策略；不设置 Tag、Cost、Cooldown 或 Cue，避免临时原型形成产品规则。 */
	UCatStageCTestAbility();

	/** 激活后校验 authority、ASC 与临时 tuning，再通过 ASC 加法修改 Poison；成功 End，任一 gate 失败则 Cancel。 */
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};
