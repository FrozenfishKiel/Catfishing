#include "Fishing/CatFishingGameplayTags.h"

namespace CatFishingGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG(CastLanded, "Cat.Fishing.Event.CastLanded");
	UE_DEFINE_GAMEPLAY_TAG(CastFailed, "Cat.Fishing.Event.CastFailed");
	UE_DEFINE_GAMEPLAY_TAG(ProbeTriggered, "Cat.Fishing.Event.ProbeTriggered");
	UE_DEFINE_GAMEPLAY_TAG(ProbeCompleted, "Cat.Fishing.Event.ProbeCompleted");
	UE_DEFINE_GAMEPLAY_TAG(FishSelectionFailed, "Cat.Fishing.Event.FishSelectionFailed");
	UE_DEFINE_GAMEPLAY_TAG(EarlyHook, "Cat.Fishing.Event.EarlyHook");
	UE_DEFINE_GAMEPLAY_TAG(HookAccepted, "Cat.Fishing.Event.HookAccepted");
	UE_DEFINE_GAMEPLAY_TAG(WindowExpired, "Cat.Fishing.Event.WindowExpired");
	UE_DEFINE_GAMEPLAY_TAG(FishStaminaDepleted, "Cat.Fishing.Event.FishStaminaDepleted");
	UE_DEFINE_GAMEPLAY_TAG(CatStaminaDepleted, "Cat.Fishing.Event.CatStaminaDepleted");
	UE_DEFINE_GAMEPLAY_TAG(CatOverpowered, "Cat.Fishing.Event.CatOverpowered");
	UE_DEFINE_GAMEPLAY_TAG(RodBroken, "Cat.Fishing.Event.RodBroken");
	UE_DEFINE_GAMEPLAY_TAG(AutoHaulReachedShore, "Cat.Fishing.Event.AutoHaulReachedShore");
	UE_DEFINE_GAMEPLAY_TAG(AutoHaulFailed, "Cat.Fishing.Event.AutoHaulFailed");
	UE_DEFINE_GAMEPLAY_TAG(ScoopCommitted, "Cat.Fishing.Event.ScoopCommitted");
	UE_DEFINE_GAMEPLAY_TAG(Interrupted, "Cat.Fishing.Event.Interrupted");
}
