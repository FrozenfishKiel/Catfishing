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

// 就绪校验流程：只核 Mesh/动画/缩放/Transform 这些「没有就没法生成鱼」的合同。
// 三个漂讯与水面槽位刻意不在其中——本轮只开槽位、VFX 资产还没做，若把它们列进合同，
// 现有 16 份 Fish_*.uasset 会全部变成未就绪，整份鱼表当场消失。
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
