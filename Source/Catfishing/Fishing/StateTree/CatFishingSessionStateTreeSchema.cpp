#include "Fishing/StateTree/CatFishingSessionStateTreeSchema.h"

#include "Fishing/CatFishingSession.h"

UCatFishingSessionStateTreeSchema::UCatFishingSessionStateTreeSchema()
{
	ContextActorClass = ACatFishingSession::StaticClass();
}
