#include "Fishing/Presentation/CatFishingPresentationSettings.h"

#include "Fishing/Presentation/CatRodSkinDefinition.h"

const UCatRodSkinDefinition* UCatFishingPresentationSettings::FindRuntimeRodSkin(
	const FName RodSkinDefinitionId, const int32  RodItemId) const
{
	if (RodSkinDefinitionId.IsNone() || (RodItemId == 0)) return nullptr;
	for (const TSoftObjectPtr<UCatRodSkinDefinition>& Entry : RodSkinCatalog)
	{
		const UCatRodSkinDefinition* Skin = Entry.LoadSynchronous();
		if (Skin && Skin->RodSkinDefinitionId == RodSkinDefinitionId && Skin->IsRuntimeDefinitionReady()
			&& Skin->CompatibleRodItemIds.Contains(RodItemId))
		{
			return Skin;
		}
	}
	return nullptr;
}
