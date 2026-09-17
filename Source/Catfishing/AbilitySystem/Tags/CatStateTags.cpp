#include "AbilitySystem/Tags/CatStateTags.h"

namespace CatStateTags
{
	UE_DEFINE_GAMEPLAY_TAG(AbilityInterruptOnDowned, "Cat.Ability.Behavior.InterruptOnDowned");
	UE_DEFINE_GAMEPLAY_TAG(State, "Cat.State");
	UE_DEFINE_GAMEPLAY_TAG(Downed, "Cat.State.Condition.Downed");
	UE_DEFINE_GAMEPLAY_TAG(Wet, "Cat.State.Condition.Wet");
	UE_DEFINE_GAMEPLAY_TAG(WaterShallow, "Cat.State.Condition.Water.Shallow");
	UE_DEFINE_GAMEPLAY_TAG(WaterDangerous, "Cat.State.Condition.Water.Dangerous");
	UE_DEFINE_GAMEPLAY_TAG(Fatigue, "Cat.State.Condition.Fatigue");
	UE_DEFINE_GAMEPLAY_TAG(RecoveryBlocked, "Cat.State.Stamina.RecoveryBlocked");
	UE_DEFINE_GAMEPLAY_TAG(FishingFight, "Cat.State.Fishing.Fight");
}
