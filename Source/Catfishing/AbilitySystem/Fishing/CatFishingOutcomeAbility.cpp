#include "AbilitySystem/Fishing/CatFishingOutcomeAbility.h"

UCatGA_FishingOutcomeBase::UCatGA_FishingOutcomeBase()
{
	// 构造流程：结果类技能只能由服务器发起，并为每个 Owner Actor 保留实例，保证表现状态和权威结果保持同一生命周期。
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}
