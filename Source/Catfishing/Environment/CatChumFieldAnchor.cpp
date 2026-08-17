#include "Environment/CatChumFieldAnchor.h"

ACatChumFieldAnchor::ACatChumFieldAnchor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetActorEnableCollision(false);
}
