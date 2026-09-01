#include "Fishing/Presentation/CatFishingPresentationSettings.h"

#include "Fishing/Presentation/CatRodSkinDefinition.h"

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
