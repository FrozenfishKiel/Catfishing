#pragma once

#include "CoreMinimal.h"
#include "CatFishBodyModel.generated.h"

/** 烘焙到鱼定义的几何。坐标为 Encounter Actor 局部 cm，VisualScale=1；不运行动画骨骼。 */
USTRUCT(BlueprintType)
struct CATFISHING_API FCatFishBodyGeometry
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Body", meta=(Units="cm"))
	FVector MouthLocalPositionCentimeters = FVector::ZeroVector;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Body", meta=(Units="cm"))
	FVector CenterOfMassLocalPositionCentimeters = FVector::ZeroVector;
	/** 网格局部缩放围绕的 Actor 局部点；对应已标定的 EncounterMeshRelativeTransform 平移。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Body", meta=(Units="cm"))
	FVector ScaleOriginLocalCentimeters = FVector::ZeroVector;
	/** 平面均匀体的回转半径；I = mass * radius^2，不能直接把 cm 代入 kg·m²。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Body", meta=(ClampMin="0", Units="cm"))
	double YawRadiusOfGyrationCentimeters = 0.0;

	bool IsValid() const;
	bool HasMouthLever() const;
	FCatFishBodyGeometry Scaled(double VisualScale) const;
};

/** 本场的物理转向参数。水的附加惯量和角阻尼平滑外力；AI 只有有限转矩。 */
struct CATFISHING_API FCatFishBodyConfig
{
	FCatFishBodyGeometry Geometry;
	double MaximumSwimTurnRateDegreesPerSecond = 120.0;
	double TurnResponseSeconds = 0.25;
	double SwimTurnTorqueFraction = 0.35;
	double MaximumBodyTurnRateDegreesPerSecond = 240.0;
	bool IsValid() const;
};

struct CATFISHING_API FCatFishBodyState
{
	/** 零向量只表示尚未播种；Runner 在入战时从权威 Actor 朝向播种。 */
	FVector Heading = FVector::ZeroVector;
	double AngularVelocityRadiansPerSecond = 0.0;
};

struct CATFISHING_API FCatFishBodyTurn
{
	FCatFishBodyState State;
	double SwimTorqueNewtonMeters = 0.0;
	double LineTorqueNewtonMeters = 0.0;
	double EffectiveInertiaKilogramMetersSquared = 0.0;
};

class CATFISHING_API FCatFishBodyModel
{
public:
	static FVector RotateLocal(const FVector& Local, const FVector& Heading);
	static FVector MouthPosition(const FCatFishBodyGeometry& Geometry, const FVector& RootPosition, const FVector& Heading);
	static FVector CenterPosition(const FCatFishBodyGeometry& Geometry, const FVector& RootPosition, const FVector& Heading);
	/** 只读候选：重试卷线/张力时不会推进状态或消耗随机数。力为作用在鱼嘴上的 N。 */
	static FCatFishBodyTurn PredictTurn(const FCatFishBodyConfig& Config, const FCatFishBodyState& State,
		const FVector& DesiredHeading, double Effort, double MassKilograms, double FullThrustNewtons,
		const FVector& MouthForceNewtons, double DeltaSeconds);
};
