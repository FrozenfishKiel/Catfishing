#include "Character/CatCharacterDefinition.h"

// 就绪校验流程：与全局 CatAbilitySettings 五项初值同一套约束（身体三项非负、搏斗两项为正），保证 DA 与回退路径语义一致。
bool UCatCharacterDefinition::IsRuntimeDefinitionReady() const
{
	return bEnableRuntimeDefinition && !CatDefinitionId.IsNone()
		&& FMath::IsFinite(InitialHunger) && InitialHunger >= 0.0f
		&& FMath::IsFinite(InitialFatigue) && InitialFatigue >= 0.0f
		&& FMath::IsFinite(InitialPoison) && InitialPoison >= 0.0f
		&& FMath::IsFinite(FishingStrength) && FishingStrength > 0.0f
		&& FMath::IsFinite(FightStaminaMaximum) && FightStaminaMaximum > 0.0f;
}
