#include "Data/CatFishDefinition.h"

// 定义可用性检查流程：验证显式 gate、身份、独立稀有/体型轴、分布三轴、权重/重量、协作人数、性格与食用结论；可选成像事件不属于实物鱼可用性的前置条件。
bool UCatFishDefinition::IsRuntimeDefinitionReady() const
{
	const bool bFoodReady = FoodSafety == ECatFishFoodSafety::Safe
		? FMath::IsFinite(HungerRelief) && HungerRelief > 0.0 && FMath::IsNearlyZero(PoisonIncrease)
		: FoodSafety == ECatFishFoodSafety::Toxic && FMath::IsFinite(HungerRelief) && HungerRelief > 0.0
			&& FMath::IsFinite(PoisonIncrease) && PoisonIncrease > 0.0;
	return bEnableRuntimeDefinition && !FishDefinitionId.IsNone() && !RarityTierId.IsNone()
		&& BodyClass != ECatFishBodyClass::Unknown && SacrificeContribution > 0
		&& RegionIds.Num() > 0 && TimeOfDay.Num() > 0 && Weather.Num() > 0
		&& FMath::IsFinite(SpawnWeight) && SpawnWeight > 0.0
		&& FMath::IsFinite(MinimumWeightKilograms) && MinimumWeightKilograms > 0.0
		&& FMath::IsFinite(MaximumWeightKilograms) && MaximumWeightKilograms >= MinimumWeightKilograms
		&& MinimumFightParticipants >= 1 && MinimumFightParticipants <= 8
		&& FMath::IsFinite(FishStrength) && FishStrength > 0.0
		&& FMath::IsFinite(FishFightStamina) && FishFightStamina > 0.0
		&& !BitePersonalityId.IsNone() && !FightPersonalityId.IsNone() && bFoodReady;
}
