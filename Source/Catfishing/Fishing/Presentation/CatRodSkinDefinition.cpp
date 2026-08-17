#include "Fishing/Presentation/CatRodSkinDefinition.h"

bool UCatRodSkinDefinition::IsRuntimeDefinitionReady() const
{
	return !RodSkinDefinitionId.IsNone() && !VisualRelativeTransform.ContainsNaN()
		&& (!SkeletalMesh.IsNull() || !StaticMesh.IsNull()) && CompatibleRodDefinitionIds.Num() > 0
		&& !CompatibleRodDefinitionIds.Contains(NAME_None);
}
