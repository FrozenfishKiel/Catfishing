#include "Fishing/Components/CatFishingLineComponent.h"

#include "Components/SceneComponent.h"

UCatFishingLineComponent::UCatFishingLineComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

void UCatFishingLineComponent::SetPresentationEndpoints(USceneComponent* InRodTip, USceneComponent* InHookAnchor)
{
	RodTip = InRodTip;
	HookAnchor = InHookAnchor;
}

FVector UCatFishingLineComponent::GetPresentationStart() const
{
	return RodTip.IsValid() ? RodTip->GetComponentLocation() : FVector::ZeroVector;
}

FVector UCatFishingLineComponent::GetPresentationEnd() const
{
	return HookAnchor.IsValid() ? HookAnchor->GetComponentLocation() : FVector::ZeroVector;
}
