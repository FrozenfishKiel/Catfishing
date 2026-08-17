#include "Environment/Presentation/CatChumFieldPresentationActor.h"

#include "Components/SceneComponent.h"

ACatChumFieldPresentationActor::ACatChumFieldPresentationActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	SetRootComponent(VisualRoot);
	SetActorEnableCollision(false);
}

void ACatChumFieldPresentationActor::ApplyPublicState(
	const FCatChumFieldPublicItem& NewState, const bool bAdded)
{
	PublicState = NewState;
	SetActorLocation(PublicState.CenterWorldPoint);
	if (bAdded) BP_OnFieldAdded(PublicState); else BP_OnFieldChanged(PublicState);
}

void ACatChumFieldPresentationActor::NotifyFieldRemoved()
{
	BP_OnFieldRemoved(PublicState);
}
