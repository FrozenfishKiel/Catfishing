#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "CatWorldItemSettings.generated.h"

/** 岸上世界物品的权威落位与通用拾取配置；鱼种美术只由 FishDefinition 的表现定义持有。 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Catfishing World Items"))
class CATFISHING_API UCatWorldItemSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category="LandedFish", meta=(ClampMin="1.0", Units="cm"))
	double LandingCompletionDistanceToRodCentimeters = 75.0;

	UPROPERTY(Config, EditAnywhere, Category="LandedFish")
	TEnumAsByte<ECollisionChannel> LandingGroundTraceChannel = ECC_Visibility;

	UPROPERTY(Config, EditAnywhere, Category="Pickup", meta=(ClampMin="1.0", Units="cm"))
	double PickupCollisionRadiusCentimeters = 45.0;

	/** 猫嘴上用于叼鱼的骨骼或 Socket；正式猫骨架统一使用 Mouth。 */
	UPROPERTY(Config, EditAnywhere, Category="Pickup")
	FName MouthCarrySocketName = TEXT("Mouth");

	/** 死鱼 Actor 根节点附着到嘴部 Socket 后的局部微调；美术可只调该值，不改变碰撞根或鱼重量缩放。 */
	UPROPERTY(Config, EditAnywhere, Category="Pickup")
	FTransform MouthCarryRelativeTransform = FTransform::Identity;
};
