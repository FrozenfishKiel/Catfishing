#include "Equipment/CatFishingResourceCustodian.h"
#include "Equipment/CatEquipmentComponent.h"

ACatFishingResourceCustodian::ACatFishingResourceCustodian()
{
	bReplicates = false;
	PrimaryActorTick.bCanEverTick = false;
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	Equipment = CreateDefaultSubobject<UCatEquipmentComponent>(TEXT("PreservedFishingEquipment"));
	Equipment->SetIsReplicated(false);
}
