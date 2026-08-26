#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "CatInteractionSettings.generated.h"

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Catfishing Interaction"))
class CATFISHING_API UCatInteractionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 本地准星检测频率；20 Hz 足以稳定跟随目标，同时不制造每帧接口调用。 */
	UPROPERTY(Config, EditAnywhere, Category="Targeting", meta=(ClampMin="0.016", Units="s"))
	double TargetingIntervalSeconds = 0.05;

	UPROPERTY(Config, EditAnywhere, Category="Targeting", meta=(ClampMin="1.0", Units="cm"))
	double MaximumTargetingDistanceCentimeters = 300.0;

	UPROPERTY(Config, EditAnywhere, Category="Targeting")
	TEnumAsByte<ECollisionChannel> TargetingTraceChannel = ECC_Visibility;

	UPROPERTY(Config, EditAnywhere, Category="Targeting")
	bool bTraceComplex = true;

	/** 服务器允许的少量网络/视点误差；目标实现仍可使用更严格的专属距离。 */
	UPROPERTY(Config, EditAnywhere, Category="Authority", meta=(ClampMin="1.0", Units="cm"))
	double MaximumServerInteractionDistanceCentimeters = 350.0;

	UPROPERTY(Config, EditAnywhere, Category="Authority")
	bool bRequireServerLineOfSight = true;

	/** 1 预留为普通白色交互描边。 */
	UPROPERTY(Config, EditAnywhere, Category="Presentation", meta=(ClampMin="1", ClampMax="255"))
	int32 FocusStencilValue = 1;
};
