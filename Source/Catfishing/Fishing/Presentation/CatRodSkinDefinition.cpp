#include "Fishing/Presentation/CatRodSkinDefinition.h"

bool UCatRodSkinDefinition::IsRuntimeDefinitionReady() const
{
	return !RodSkinDefinitionId.IsNone() && !VisualRelativeTransform.ContainsNaN()
		&& (!SkeletalMesh.IsNull() || !StaticMesh.IsNull()) && CompatibleRodItemIds.Num() > 0
		&& !CompatibleRodItemIds.Contains(0);
}
