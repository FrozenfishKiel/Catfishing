#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "CatFishPickupSettings.generated.h"

/** 岸上鱼的权威落位、拾取和叼鱼配置；鱼种美术只由 FishDefinition 的表现定义持有。 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Catfishing Fish Pickup"))
class CATFISHING_API UCatFishPickupSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 墓碑（2026-09-14，T17；钓鱼规则 §5.3）：触岸即交付，仅保留旧配置序列化入口。 */
	UPROPERTY(Config, EditAnywhere, Category="LandedFish", meta=(DeprecatedProperty, DeprecationMessage="触岸处立即生成 Pickup；旧距竿尖门槛不再使用。"))
	double LandingCompletionDistanceToRodCentimeters = 75.0;

	/** 上岸落点向下探测使用的碰撞通道；FishingSession 和拾取 Actor 用它把鱼贴到可站立表面。 */
	UPROPERTY(Config, EditAnywhere, Category="LandedFish")
	TEnumAsByte<ECollisionChannel> LandingGroundTraceChannel = ECC_Visibility;

	/** 地面鱼可交互碰撞半径，单位厘米；拾取 Actor 创建碰撞体时读取它，不由 UI 自行扩大触达范围。 */
	UPROPERTY(Config, EditAnywhere, Category="Pickup", meta=(ClampMin="1.0", Units="cm"))
	double PickupCollisionRadiusCentimeters = 45.0;

	/** 猫嘴上用于叼鱼的骨骼或 Socket；正式猫骨架统一使用 Mouth。 */
	UPROPERTY(Config, EditAnywhere, Category="Pickup")
	FName MouthCarrySocketName = TEXT("Mouth");

	/** 死鱼根节点相对嘴部 Socket 的位置与朝向；附着和复制纠正只读取这两项，忽略缩放分量以保留原世界尺寸。 */
	UPROPERTY(Config, EditAnywhere, Category="Pickup")
	FTransform MouthCarryRelativeTransform = FTransform::Identity;
};
