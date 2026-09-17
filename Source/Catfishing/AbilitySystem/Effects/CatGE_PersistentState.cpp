#include "AbilitySystem/Effects/CatGE_PersistentState.h"

// 状态配置流程：选择无限时长；不配置属性修饰器和叠层，使清理一个来源不会移除其他来源的状态。
UCatGE_PersistentState::UCatGE_PersistentState()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
}
