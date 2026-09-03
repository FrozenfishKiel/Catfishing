#pragma once

#include "CoreMinimal.h"
#include "Fishing/CatFishingTypes.h"

/** 单步终局；僵持、猫力竭和鱼被压制都只是连续物理结果，不再是瞬时结局。 */
enum class ECatFightStepOutcome : uint8
{
	None,
	FishExhausted,
	LineBroken,
	Escaped
};

enum class ECatFightLineBreakCause : uint8
{
	None,
	StrengthOverload,
	DurabilityDepleted
};

/** 线杯控制模式：不按=锁线，左键=收线，右键=自由出线。 */
enum class ECatFightCatAction : uint8
{
	None,
	Pull,
	Slack
};

/** 固定步所需的冻结参数。力量决定意图驱动力，质量只分配鱼占优后产生的双端运动。 */
struct CATFISHING_API FCatFightSimulationConfig
{
	double FixedStepSeconds = 0.0;
	double PrimaryOperatorCatStrength = 0.0;
	double SecondCatStrength = 0.0;
	double PrimaryOperatorMassKilograms = 0.0;
	double HelperMassKilograms = 0.0;
	double FishMassKilograms = 0.0;

	double GetCombinedCatStrength() const { return PrimaryOperatorCatStrength + SecondCatStrength; }
	double GetCombinedCatMass() const
	{
		return PrimaryOperatorMassKilograms + HelperMassKilograms;
	}

	double FishStrength = 0.0;
	/** 鱼实际重量与猫等效系统质量共用的力量换算。 */
	double StrengthPerKilogram = 10.0;
	/** 双方共用的力量到绷线对抗加速度换算；不限制鱼的自由游速。 */
	double AccelerationPerStrength = 5.0;
	/** 将猫端对抗加速度投影为收线/牵引响应速度的时长。 */
	double DriveResponseSeconds = 1.0;
	double RodStrength = 0.0;
	/** 转矩模型的玩法杆长；来自鱼竿定义，不读取 Mesh 或锚点间距。 */
	double RodPhysicsLengthCentimeters = 200.0;
	double CatStaminaMaximum = 0.0;
	/** 双方体力都按 StrengthPerKilogram 这一标准努力强度与沿线有效距离结算，不再乘各自绝对力量。 */
	double CatStaminaCostPerStrengthCentimeter = 0.002;
	double FishStaminaCostPerStrengthCentimeter = 0.002;
	double IsometricEffortMultiplier = 1.0;
	double BaseDrainMultiplier = 1.0;
	double StruggleDrainMultiplier = 2.0;
	double SlackStaminaRegenPerSecond = 1.5;
	double StalemateRodWearPerFishStrength = 0.1;
	double StruggleHoldRodWearPerSecond = 0.0;
	double TautRodWearMultiplier = 1.0;
	double ReelSpeedCentimetersPerSecond = 0.0;
	double FishCalmSpeedCentimetersPerSecond = 0.0;
	double FishStruggleSpeedCentimetersPerSecond = 0.0;
	double FishExhaustionThreshold = 0.5;
	double StrongConfrontationAlignmentThreshold = 0.55;
	double StrongConfrontationConfirmationSeconds = 0.2;
	double AngleStrengthExponent = 1.0;
	double TensionResponseRangeCentimeters = 10.0;
	double MinimumRodLeverageMultiplier = 0.4;
	/** 鱼端和猫端各自每秒允许承担的最大约束速度修正。 */
	double MaximumFishConstraintCorrectionSpeedCentimetersPerSecond = 160.0;
	double MinimumCarrierAwaySpeedMultiplier = 0.15;
	double MaximumLineLengthCentimeters = 0.0;
	double RodDurability = TNumericLimits<double>::Max();
	double EscapeSlackCentimeters = 100.0;

	bool IsValid() const;
};

/** 权威端点事实与玩家本步移动意图。移动改端点，收线只改约束长度。 */
struct CATFISHING_API FCatFightRodConstraintInput
{
	FVector RodTipWorldPosition = FVector::ZeroVector;
	FVector RodForwardWorld = FVector::ForwardVector;
	FVector RodTipVelocityCentimetersPerSecond = FVector::ZeroVector;
	FVector CarrierVelocityCentimetersPerSecond = FVector::ZeroVector;
	FVector CarrierDesiredVelocityCentimetersPerSecond = FVector::ZeroVector;
	bool bRodHeld = false;
};

struct CATFISHING_API FCatFightSimulationState
{
	bool bOperatorPresent = true;
	bool bFishExhausted = false;
	double CatStamina = 0.0;
	double FishStamina = 0.0;
	double LineLengthCentimeters = 0.0;
	double AbsoluteRodWear = 0.0;
	FVector FishWorldPosition = FVector::ZeroVector;
	ECatFightCatAction CatAction = ECatFightCatAction::None;
	ECatFishMotionIntent MotionIntent = ECatFishMotionIntent::None;
	double StrongConfrontationBuildUpSeconds = 0.0;
};

struct CATFISHING_API FCatFightStepResult
{
	bool bSucceeded = false;
	double IntendedSwimSpeedCentimetersPerSecond = 0.0;
	double CatStaminaDrain = 0.0;
	double FishStaminaDrain = 0.0;
	double CatIntendedLineDistanceCentimeters = 0.0;
	double CatActualLineDistanceCentimeters = 0.0;
	double FishIntendedLineDistanceCentimeters = 0.0;
	double FishActualLineDistanceCentimeters = 0.0;
	double RequestedReelDistanceCentimeters = 0.0;
	double ActualReelDistanceCentimeters = 0.0;
	double TensionCentimeters = 0.0;
	double StraightLineDistanceCentimeters = 0.0;
	double SlackLineLengthCentimeters = 0.0;
	double NormalizedTension = 0.0;
	bool bLineTaut = false;
	double LineLengthCentimeters = 0.0;
	double AbsoluteRodWear = 0.0;
	FVector ProposedFishWorldPosition = FVector::ZeroVector;
	double FishLineAlignment = 0.0;
	double NormalizedLineLoad = 0.0;
	double RodLineAlignment = 1.0;
	double RodLeverageMultiplier = 1.0;
	double EffectiveCatStrength = 0.0;
	double CombinedCatStrength = 0.0;
	double CatDriveAccelerationCentimetersPerSecondSquared = 0.0;
	double FishDriveAccelerationCentimetersPerSecondSquared = 0.0;
	double NetFishPullAccelerationCentimetersPerSecondSquared = 0.0;
	double FishForceDominance = 0.0;
	int32 ActiveHelperCount = 0;
	/** 猫端在下一个固定步内需要达到的向鱼目标速度；运行时平滑追赶，不作为累积冲量。 */
	double CarrierTargetPullSpeedCentimetersPerSecond = 0.0;
	/** 兼容现有表现字段；由目标速度除以固定步长得到，不直接用于角色移动。 */
	double CarrierPullAccelerationCentimetersPerSecondSquared = 0.0;
	double CarrierAwaySpeedMultiplier = 1.0;
	double ConstraintErrorCentimeters = 0.0;
	double RelativeConstraintSpeedCentimetersPerSecond = 0.0;
	double FishConstraintCorrectionCentimeters = 0.0;
	double CarrierConstraintCorrectionCentimeters = 0.0;
	double StrongConfrontationBuildUpSeconds = 0.0;
	bool bStalemate = false;
	bool bStrongConfrontation = false;
	/** Runner 确认本步由猫端牵引越过真实岸线；鱼会复用 FishExhausted 终局进入鱼干拖拽。 */
	bool bFishBeached = false;
	ECatFightLineBreakCause LineBreakCause = ECatFightLineBreakCause::None;
	ECatFightStepOutcome Outcome = ECatFightStepOutcome::None;
};

/** 无状态双端约束。输入只表达意图；输出是猫、鱼和线长同一步复合后的唯一事实。 */
class CATFISHING_API FCatFishingFightSimulator
{
public:
	static FCatFightStepResult Step(const FCatFightSimulationConfig& Config,
		const FCatFightSimulationState& State, const FVector& RodTipWorldPosition,
		const FVector& DesiredFishDirection);
	static FCatFightStepResult Step(const FCatFightSimulationConfig& Config,
		const FCatFightSimulationState& State, const FCatFightRodConstraintInput& RodConstraint,
		const FVector& DesiredFishDirection);
};
