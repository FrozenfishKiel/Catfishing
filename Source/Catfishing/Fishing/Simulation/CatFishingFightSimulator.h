#pragma once

#include "CoreMinimal.h"
#include "Fishing/CatFishingTypes.h"

/** 单步终局；猫力竭先进入持续拖拽，由真实水深确认落水，不直接结束本场。 */
enum class ECatFightStepOutcome : uint8
{
	None,
	FishExhausted,
	RodBroken,
	Escaped
};

/** 线杯控制模式：不按=锁线，左键=收线，右键=自由出线。 */
enum class ECatFightCatAction : uint8
{
	None,
	Pull,
	Slack
};

/** 固定步的冻结参数。力使用 N、质量使用 kg；世界距离和速度保持 UE 的 cm 单位。 */
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
	/** 鱼实际重量生成基础力量的换算；同时保留原玩法做功价格的标准强度。 */
	double StrengthPerKilogram = 10.0;
	/** 力量属性到推力/支撑力的显式换算，不能复用旧的加速度系数。 */
	double ForcePerStrengthNewtons = 1.0;
	double CatBodyMassKilograms = 5.0;
	/** 力竭鱼免耗体回收的有限辅助力，不依赖猫的剩余体力。 */
	double ExhaustedReelForceNewtons = 200.0;
	/** 零体力拖落水规则的辅助推力，以猫系统质量乘该加速度加入鱼端。 */
	double ExhaustedCatTowAccelerationCentimetersPerSecondSquared = 300.0;
	/** UI 满张力对应的牛顿数；不参与约束求解。 */
	double DisplayTensionNewtons = 50.0;
	/** 转矩模型的玩法杆长；来自鱼竿定义，不读取 Mesh 或锚点间距。 */
	double RodPhysicsLengthCentimeters = 200.0;
	double CatStaminaMaximum = 0.0;
	/** 猫线性正功使用标准力量与已完成主动距离；受阻支撑独立按时间收费。 */
	double CatStaminaCostPerStrengthCentimeter = 0.002;
	/** 转杆使用弧度正功单价，不将最大转速折算为一米弧长。 */
	double CatRodStaminaCostPerStrengthRadian = 0.03;
	/** 无负载实际动作的基础价格；负载部分继续由 CatLoadStaminaMultiplier 调节。 */
	double CatUnloadedWorkMultiplier = 0.15;
	/** 满负载/满用力的每秒支撑费用，按用力比例平方缩放。 */
	double CatSupportStaminaPerSecond = 2.0;
	/** 鱼仅结算有对抗负载的努力，自由游动不产生基础费用。 */
	double FishStaminaCostPerStrengthCentimeter = 0.002;
	double IsometricEffortMultiplier = 1.0;
	double CatMovementStaminaMultiplier = 1.0;
	double CatReelStaminaMultiplier = 1.0;
	double CatRodStaminaMultiplier = 1.0;
	double CatHoldStaminaMultiplier = 1.0;
	double CatLoadStaminaMultiplier = 1.0;
	double FishLoadStaminaMultiplier = 1.0;
	double BaseDrainMultiplier = 1.0;
	double StruggleDrainMultiplier = 2.0;
	double SlackStaminaRegenPerSecond = 1.5;
	double StalemateRodWearPerFishStrength = 0.1;
	double StruggleHoldRodWearPerSecond = 0.0;
	double TautRodWearMultiplier = 1.0;
	double ReelSpeedCentimetersPerSecond = 0.0;
	double FishCalmSpeedCentimetersPerSecond = 0.0;
	double FishStruggleSpeedCentimetersPerSecond = 0.0;
	/** 无可用猫合力时的持续外冲速度，按鱼较快的配置游速放大。 */
	double ExhaustedCatEscapeSpeedMultiplier = 2.0;
	double FishExhaustionThreshold = 0.5;
	/** 仅供强对抗/僵持表现分类，不参与位移、做功或终局裁决。 */
	double StrongConfrontationAlignmentThreshold = 0.55;
	double StrongConfrontationConfirmationSeconds = 0.2;
	double AngleStrengthExponent = 1.0;
	double MinimumRodLeverageMultiplier = 0.4;
	/** 鱼端和猫端各自每秒允许承担的最大约束速度修正。 */
	double MaximumFishConstraintCorrectionSpeedCentimetersPerSecond = 160.0;
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
	/** 从权威转矩积分采集本步用力平方时间和真实正功转角；不含身体平移。 */
	double CatRodExertionSquaredSeconds = 0.0;
	double CatRodPositiveWorkRadians = 0.0;
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
	FVector FishVelocityCentimetersPerSecond = FVector::ZeroVector;
	ECatFightCatAction CatAction = ECatFightCatAction::None;
	ECatFishMotionIntent MotionIntent = ECatFishMotionIntent::None;
	double StrongConfrontationBuildUpSeconds = 0.0;
};

struct CATFISHING_API FCatFightStepResult
{
	bool bSucceeded = false;
	bool bExhaustedCatEscape = false;
	/** 正常主位右键回体：屏蔽双方耗体；强制力竭拖拽仍优先。 */
	bool bSlackRecoveryActive = false;
	double IntendedSwimSpeedCentimetersPerSecond = 0.0;
	double CatStaminaDrain = 0.0;
	double FishStaminaDrain = 0.0;
	/** 猫的四类努力独立计价；移动/转杆归主位，收线/保持才由实际合力者分担。 */
	double CatMovementStaminaDrain = 0.0;
	double CatReelStaminaDrain = 0.0;
	double CatRodStaminaDrain = 0.0;
	double CatRodWorkStaminaDrain = 0.0;
	/** 超出共享持竿支撑的主位转杆支撑费用；同一负载不重复收取。 */
	double CatRodSupportStaminaDrain = 0.0;
	double CatHoldStaminaDrain = 0.0;
	double CatMovementIntentCentimeters = 0.0;
	double CatMovementActualCentimeters = 0.0;
	double CatRodExertionSquaredSeconds = 0.0;
	double CatRodPositiveWorkRadians = 0.0;
	double CatHoldIntentCentimeters = 0.0;
	double CatNormalizedEffortLoad = 0.0;
	double CatRodNormalizedEffortLoad = 0.0;
	double FishNormalizedEffortLoad = 0.0;
	double FishUncappedStaminaDrain = 0.0;
	double GetSharedCatStaminaDrain() const { return CatReelStaminaDrain + CatHoldStaminaDrain; }
	double GetPrimaryCatStaminaDrain() const { return CatMovementStaminaDrain + CatRodStaminaDrain; }
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
	/** 本固定步新增的鱼竿磨损；由 Session 写回同一装备实例。 */
	double RodWearDelta = 0.0;
	FVector ProposedFishWorldPosition = FVector::ZeroVector;
	/** 本步实际提交的游动努力方向；地形反馈更新下步转向时不能改写本步费用。 */
	FVector FishEffortDirection = FVector::ZeroVector;
	double FishLineAlignment = 0.0;
	double NormalizedLineLoad = 0.0;
	double RodLineAlignment = 1.0;
	double RodLeverageMultiplier = 1.0;
	double EffectiveCatStrength = 0.0;
	double CombinedCatStrength = 0.0;
	double CatDriveAccelerationCentimetersPerSecondSquared = 0.0;
	double FishDriveAccelerationCentimetersPerSecondSquared = 0.0;
	double NetFishPullAccelerationCentimetersPerSecondSquared = 0.0;
	/** 鱼端约束冲量除以步长所得的共同张力，供猫、杆及负载观察共用。 */
	double LineTensionNewtons = 0.0;
	int32 ActiveHelperCount = 0;
	/** 猫端向鱼速度上限；实际速度按共同张力产生的加速度逐步接近。 */
	double CarrierTargetPullSpeedCentimetersPerSecond = 0.0;
	/** 共同张力减去有限支撑后，按猫系统质量计算的真实加速度。 */
	double CarrierPullAccelerationCentimetersPerSecondSquared = 0.0;
	double ConstraintErrorCentimeters = 0.0;
	double RelativeConstraintSpeedCentimetersPerSecond = 0.0;
	double FishConstraintCorrectionCentimeters = 0.0;
	double StrongConfrontationBuildUpSeconds = 0.0;
	bool bStalemate = false;
	bool bStrongConfrontation = false;
	/** Runner 确认本步由猫端牵引越过真实岸线；鱼会复用 FishExhausted 终局进入鱼干拖拽。 */
	bool bFishBeached = false;
	ECatFightStepOutcome Outcome = ECatFightStepOutcome::None;
};

/** 无副作用的固定步求解。真实猫端点由 CharacterMovement 提供；不预支尚未通过碰撞的移动。 */
class CATFISHING_API FCatFishingFightSimulator
{
public:
	/** 对最终落点/线长重算费用与终局，幂等且没有 ASC/装备副作用。 */
	static bool FinalizeResolvedStep(const FCatFightSimulationConfig& Config,
		const FCatFightSimulationState& State, const FCatFightRodConstraintInput& RodConstraint,
		FCatFightStepResult& Result);
	/** 活鱼趁持竿主猫力竭且无助手出力时持续外冲；无人持竿/力竭鱼不进入。 */
	static bool ShouldEscapeExhaustedCat(const FCatFightSimulationConfig& Config,
		const FCatFightSimulationState& State, bool bRodHeld);
	static FCatFightStepResult Step(const FCatFightSimulationConfig& Config,
		const FCatFightSimulationState& State, const FVector& RodTipWorldPosition,
		const FVector& DesiredFishDirection);
	static FCatFightStepResult Step(const FCatFightSimulationConfig& Config,
		const FCatFightSimulationState& State, const FCatFightRodConstraintInput& RodConstraint,
		const FVector& DesiredFishDirection);
};
