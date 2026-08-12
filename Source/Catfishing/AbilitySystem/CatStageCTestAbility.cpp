#include "AbilitySystem/CatStageCTestAbility.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/CatAbilitySettings.h"
#include "AbilitySystem/CatSurvivalAttributeSet.h"

// 构造流程：只锁定网络执行位置为服务器；实例化、输入、Tag、Cost、Cooldown 与 Cue 均保持引擎默认或空，等待后续产品裁决。
UCatStageCTestAbility::UCatStageCTestAbility()
{
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

// 激活流程：ActorInfo 是 GAS 调用合同，若引擎异常缺失则无法安全结束实例而直接返回；正常入口再校验 authority、ASC 与 fail-closed 临时数值，失败走标准 Cancel，成功由 ASC 修改 Hunger 并 End。
void UCatStageCTestAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	(void)TriggerEventData;
	if (!ActorInfo)
	{
		return;
	}
	UAbilitySystemComponent* AbilitySystem = ActorInfo->AbilitySystemComponent.Get();
	float HungerDelta = 0.0f;
	if (!ActorInfo->IsNetAuthority() || !AbilitySystem
		|| !GetDefault<UCatAbilitySettings>()->TryGetDiagnosticHungerDelta(HungerDelta))
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
		return;
	}

	AbilitySystem->ApplyModToAttribute(UCatSurvivalAttributeSet::GetHungerAttribute(), EGameplayModOp::Additive, HungerDelta);
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
