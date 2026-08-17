#include "Environment/Presentation/CatWaterRegionPresentationSubsystem.h"

#include "Environment/Presentation/CatWaterRegionPresentationActor.h"

FString UCatWaterRegionPresentationSubsystem::MakeKey(const FCatWaterRegionHandle& Handle)
{
	return FString::Printf(TEXT("%s|%lld"), *Handle.RegionId.ToString(), Handle.GeometryRevision);
}

void UCatWaterRegionPresentationSubsystem::RegisterPresentation(
	const FCatWaterRegionHandle& Handle, ACatWaterRegionPresentationActor* Actor)
{
	if (Handle.IsValid() && IsValid(Actor)) Presentations.Add(MakeKey(Handle), Actor);
}

void UCatWaterRegionPresentationSubsystem::UnregisterPresentation(
	const FCatWaterRegionHandle& Handle, const ACatWaterRegionPresentationActor* Actor)
{
	const FString Key = MakeKey(Handle);
	if (const TWeakObjectPtr<ACatWaterRegionPresentationActor>* Existing = Presentations.Find(Key))
	{
		if (!Existing->IsValid() || Existing->Get() == Actor) Presentations.Remove(Key);
	}
}

bool UCatWaterRegionPresentationSubsystem::SetLocalWaterPreviewVisible(
	const FCatWaterRegionHandle& WaterRegion, const bool bVisible)
{
	if (!WaterRegion.IsValid()) return false;
	const FString Key = MakeKey(WaterRegion);
	TWeakObjectPtr<ACatWaterRegionPresentationActor>* Found = Presentations.Find(Key);
	if (!Found || !Found->IsValid())
	{
		Presentations.Remove(Key);
		return false;
	}
	Found->Get()->SetWaterPreviewVisible(bVisible);
	return true;
}
