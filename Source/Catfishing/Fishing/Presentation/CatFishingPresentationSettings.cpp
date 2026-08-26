#include "Fishing/Presentation/CatFishingPresentationSettings.h"

#include "Fishing/Presentation/CatRodSkinDefinition.h"

double UCatFishingPresentationSettings::ComputeFishUniformVisualScale(const double WeightKilograms) const
{
	if (!FMath::IsFinite(WeightKilograms) || WeightKilograms <= 0.0
		|| !FMath::IsFinite(FishMeshReferenceWeightKilograms) || FishMeshReferenceWeightKilograms <= 0.0
		|| !FMath::IsFinite(FishMeshMinimumUniformScale) || FishMeshMinimumUniformScale <= 0.0
		|| !FMath::IsFinite(FishMeshMaximumUniformScale)
		|| FishMeshMaximumUniformScale < FishMeshMinimumUniformScale)
	{
		return 1.0;
	}
	// 重量近似随体积变化，而体积随线性尺寸的三次方变化；因此使用立方根而非 Weight/Reference 线性放大。
	const double UnclampedScale = FMath::Pow(WeightKilograms / FishMeshReferenceWeightKilograms, 1.0 / 3.0);
	return FMath::Clamp(UnclampedScale, FishMeshMinimumUniformScale, FishMeshMaximumUniformScale);
}

const UCatRodSkinDefinition* UCatFishingPresentationSettings::FindRuntimeRodSkin(
	const FName RodSkinDefinitionId, const FName RodDefinitionId) const
{
	if (RodSkinDefinitionId.IsNone() || RodDefinitionId.IsNone()) return nullptr;
	for (const TSoftObjectPtr<UCatRodSkinDefinition>& Entry : RodSkinCatalog)
	{
		const UCatRodSkinDefinition* Skin = Entry.LoadSynchronous();
		if (Skin && Skin->RodSkinDefinitionId == RodSkinDefinitionId && Skin->IsRuntimeDefinitionReady()
			&& Skin->CompatibleRodDefinitionIds.Contains(RodDefinitionId))
		{
			return Skin;
		}
	}
	return nullptr;
}
