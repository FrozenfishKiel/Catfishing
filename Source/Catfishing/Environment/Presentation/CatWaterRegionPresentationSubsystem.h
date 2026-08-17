#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "Environment/CatWaterTypes.h"

#include "CatWaterRegionPresentationSubsystem.generated.h"

class ACatWaterRegion;
class ACatWaterRegionPresentationActor;

UCLASS()
class CATFISHING_API UCatWaterRegionPresentationSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	bool SetLocalWaterPreviewVisible(const FCatWaterRegionHandle& WaterRegion, bool bVisible);

private:
	friend class ACatWaterRegion;

	void RegisterPresentation(const FCatWaterRegionHandle& Handle, ACatWaterRegionPresentationActor* Actor);
	void UnregisterPresentation(const FCatWaterRegionHandle& Handle, const ACatWaterRegionPresentationActor* Actor);
	static FString MakeKey(const FCatWaterRegionHandle& Handle);

	TMap<FString, TWeakObjectPtr<ACatWaterRegionPresentationActor>> Presentations;
};
