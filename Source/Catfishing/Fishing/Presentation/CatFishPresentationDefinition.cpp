#include "Fishing/Presentation/CatFishPresentationDefinition.h"

namespace CatFishPresentationDefinitionPrivate
{
	static bool HasPositiveFiniteScale(const FVector& Scale)
	{
		return !Scale.ContainsNaN() && Scale.X > 0.0 && Scale.Y > 0.0 && Scale.Z > 0.0;
	}

	static bool IsTransformReady(const FTransform& Transform)
	{
		return !Transform.ContainsNaN() && Transform.IsRotationNormalized()
			&& HasPositiveFiniteScale(Transform.GetScale3D());
	}
}

bool UCatFishPresentationDefinition::IsRuntimeDefinitionReady() const
{
	return !SkeletalMesh.IsNull() && !AnimInstanceClass.IsNull()
		&& !CalmAnimation.IsNull() && !StruggleAnimation.IsNull()
		&& !ExhaustedAnimation.IsNull() && !LandedAnimation.IsNull()
		&& FMath::IsFinite(MeshReferenceWeightKilograms) && MeshReferenceWeightKilograms > 0.0
		&& FMath::IsFinite(MinimumUniformScale) && MinimumUniformScale > 0.0
		&& FMath::IsFinite(MaximumUniformScale) && MaximumUniformScale >= MinimumUniformScale
		&& FMath::IsFinite(ExhaustedVisualRollDegrees)
		&& ExhaustedVisualRollDegrees >= -180.0 && ExhaustedVisualRollDegrees <= 180.0
		&& FMath::IsFinite(LandedActorRollDegrees)
		&& LandedActorRollDegrees >= -180.0 && LandedActorRollDegrees <= 180.0
		&& CatFishPresentationDefinitionPrivate::IsTransformReady(EncounterMeshRelativeTransform)
		&& CatFishPresentationDefinitionPrivate::IsTransformReady(LandedMeshRelativeTransform)
		&& CatFishPresentationDefinitionPrivate::IsTransformReady(CarriedMeshRelativeTransform);
}

double UCatFishPresentationDefinition::ComputeUniformVisualScale(const double WeightKilograms) const
{
	if (!FMath::IsFinite(WeightKilograms) || WeightKilograms <= 0.0 || !IsRuntimeDefinitionReady())
	{
		return 1.0;
	}
	const double UnclampedScale = FMath::Pow(WeightKilograms / MeshReferenceWeightKilograms, 1.0 / 3.0);
	return FMath::Clamp(UnclampedScale, MinimumUniformScale, MaximumUniformScale);
}
