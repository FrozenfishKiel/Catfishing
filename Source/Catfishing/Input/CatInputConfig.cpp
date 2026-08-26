#include "Input/CatInputConfig.h"

#include "InputAction.h"

bool UCatInputConfig::IsNativeInputConfigurationValid() const
{
	TSet<FGameplayTag> SeenTags;
	TSet<const UInputAction*> SeenActions;
	for (const FCatNativeInputAction& Entry : NativeInputActions)
	{
		if (!Entry.InputAction || !Entry.InputTag.IsValid() || SeenTags.Contains(Entry.InputTag)
			|| SeenActions.Contains(Entry.InputAction.Get()))
		{
			return false;
		}
		SeenTags.Add(Entry.InputTag);
		SeenActions.Add(Entry.InputAction.Get());
	}
	return true;
}

const UInputAction* UCatInputConfig::FindNativeInputActionForTag(const FGameplayTag InputTag) const
{
	if (!InputTag.IsValid())
	{
		return nullptr;
	}
	for (const FCatNativeInputAction& Entry : NativeInputActions)
	{
		if (Entry.InputAction && Entry.InputTag.MatchesTagExact(InputTag))
		{
			return Entry.InputAction;
		}
	}
	return nullptr;
}
