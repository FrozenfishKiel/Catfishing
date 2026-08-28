#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "CatWorldItemSettings.generated.h"

class USkeletalMesh;

/** 岸上世界物品的权威落位与临时美术配置；软 Mesh 只在非 Dedicated Server 端加载。 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Catfishing World Items"))
class CATFISHING_API UCatWorldItemSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category="LandedFish", meta=(ClampMin="1.0", Units="cm"))
	double LandingInlandDistanceCentimeters = 75.0;

	UPROPERTY(Config, EditAnywhere, Category="LandedFish", meta=(ClampMin="1.0", Units="cm"))
	double LandingCompletionDistanceToRodCentimeters = 75.0;

	UPROPERTY(Config, EditAnywhere, Category="LandedFish", meta=(ClampMin="0.0", Units="cm"))
	double LandingGroundTraceUpCentimeters = 250.0;

	UPROPERTY(Config, EditAnywhere, Category="LandedFish", meta=(ClampMin="0.0", Units="cm"))
	double LandingGroundTraceDownCentimeters = 600.0;

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

	UPROPERTY(Config, EditAnywhere, Category="Presentation")
	TSoftObjectPtr<USkeletalMesh> LandedFishMesh;

	/** 岸上鱼 Actor 的服务器权威侧翻角度；随 ReplicatedMovement 同步给所有客户端。 */
	UPROPERTY(Config, EditAnywhere, Category="Presentation", meta=(ClampMin="-180.0", ClampMax="180.0", Units="deg"))
	double LandedFishRollDegrees = 90.0;

	/** 只用于 Available 落地状态的 Mesh 局部变换；嘴叼状态会清除其位置和旋转，只复用基础缩放。 */
	UPROPERTY(Config, EditAnywhere, Category="Presentation")
	FTransform LandedFishMeshRelativeTransform = FTransform::Identity;
};
