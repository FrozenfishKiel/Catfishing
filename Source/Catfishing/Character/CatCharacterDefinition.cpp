#include "Character/CatCharacterDefinition.h"

// 就绪校验流程：与全局 CatAbilitySettings 初值同一套搏斗约束，保证 DA 与回退路径语义一致。
bool UCatCharacterDefinition::IsRuntimeDefinitionReady() const
{
	return bEnableRuntimeDefinition && !CatDefinitionId.IsNone()
		&& FMath::IsFinite(FishingStrength) && FishingStrength > 0.0f
		&& FMath::IsFinite(FightStaminaMaximum) && FightStaminaMaximum > 0.0f;
}
