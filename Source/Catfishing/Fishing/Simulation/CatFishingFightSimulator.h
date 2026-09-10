#pragma once

#include "CoreMinimal.h"
#include "Fishing/CatFishingTypes.h"
#include "Fishing/Simulation/CatFishingRodResistanceModel.h"

/** 单步终局；猫力竭先进入持续拖拽，由真实水深确认落水，不直接结束本场。 */
enum class ECatFightStepOutcome : uint8
{
	None,
	FishExhausted,
	RodBroken,
	Escaped
};

/** 线杯控制模式：不按=锁线，左键=收线，右键在未放尽时自由出线。 */
enum class ECatFightCatAction : uint8
{
	None,
	Pull,
	Slack
};

/**
 * 固定步被拒绝时的可检索原因。模拟器保持纯函数，不在这里写日志；Runner 将该值映射到
 * Development 日志，避免只看到一个 bSucceeded=false 却无法判断是配置、状态还是几何输入坏了。
 */
enum class ECatFightSimulationRejectReason : uint8
{
	None,
	InvalidConfig,
	InvalidState,
	InvalidRodConstraint,
	InvalidFishDirection,
	InvalidResolvedResult
};

/** 固定步的冻结参数。力使用 N、质量使用 kg；世界距离和速度保持 UE 的 cm 单位。 */
struct CATFISHING_API FCatFightSimulationConfig
{
	double FixedStepSeconds = 0.0;
	double PrimaryOperatorCatStrength = 0.0;
	double PrimaryOperatorMassKilograms = 0.0;
	double FishMassKilograms = 0.0;


	double FishStrength = 0.0;
	/** 鱼实际重量生成基础力量的换算；同时保留原玩法做功价格的标准强度。 */
	double StrengthPerKilogram = 10.0;
	/** 力量属性到推力/支撑力的显式换算，不能复用旧的加速度系数。 */
	double ForcePerStrengthNewtons = 1.0;

	/** 力竭鱼免耗体回收的有限辅助力，不依赖猫的剩余体力。 */
	double ExhaustedReelForceNewtons = 200.0;
	/** 零体力拖落水规则的辅助推力，以主控真实身体质量乘该加速度加入鱼端。 */
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
	/** 沿本步主动方向未完成的游动距离，每米消耗的体力点数；反向进展可使缺失超过意图距离。 */
	double FishStaminaPerUnfulfilledMeter = 5.0 / 3.0;
	double CatMovementStaminaMultiplier = 1.0;
	double CatReelStaminaMultiplier = 1.0;
	double CatRodStaminaMultiplier = 1.0;
	double CatHoldStaminaMultiplier = 1.0;
	double CatLoadStaminaMultiplier = 1.0;
	double SlackStaminaRegenPerSecond = 1.5;
	double StalemateRodWearPerFishStrength = 0.1;
	/** 鱼满主动出力的基础每秒磨损，来源为竿定义；按实际u²和方向负载缩放，不读取动画档位。 */
	double FishFullEffortRodWearPerSecond = 0.0;
	double TautRodWearMultiplier = 1.0;
	double ReelSpeedCentimetersPerSecond = 0.0;
	/** 满出力参考游速，校准固定水阻并生成 u*参考游速*dt 的本步主动意图。 */
	double FishFullEffortSpeedCentimetersPerSecond = 0.0;
	/** 主控零体力时的持续外冲速度，按满出力参考游速放大。 */
	double ExhaustedCatEscapeSpeedMultiplier = 2.0;
	double FishExhaustionThreshold = 0.5;
	/** 仅供强对抗/僵持表现分类，不参与位移、做功或终局裁决。 */
	double StrongConfrontationAlignmentThreshold = 0.55;
	double StrongConfrontationConfirmationSeconds = 0.2;
	double AngleStrengthExponent = 1.0;
	double MinimumRodLeverageMultiplier = 0.4;
	/** 静态锚点模型的历史鱼位置误差修正速度上限；物理端点使用实际响应求解。 */
	double MaximumFishConstraintCorrectionSpeedCentimetersPerSecond = 160.0;
	double MaximumLineLengthCentimeters = 0.0;
	double RodDurability = TNumericLimits<double>::Max();
	double EscapeSlackCentimeters = 100.0;

	bool IsValid() const;
};

/** 权威端点观察与本步操竿努力。Chaos 决定端点位置，卷线器只改变线长。 */
struct CATFISHING_API FCatFightRodConstraintInput
{
	FVector RodTipWorldPosition = FVector::ZeroVector;
	FVector RodForwardWorld = FVector::ForwardVector;
	FVector RodTipVelocityCentimetersPerSecond = FVector::ZeroVector;
	FVector CarrierVelocityCentimetersPerSecond = FVector::ZeroVector;
	/** Actual endpoint acceleration and last applied load; sampled once per world time, reset on topology/teleport. */
	FVector RodTipAccelerationCentimetersPerSecondSquared = FVector::ZeroVector;
	FVector PreviousLineForceNewtons = FVector::ZeroVector;
	/** Columns of the actual locked-linear-constraint point mobility (1/kg); soft drives/contact reactions are observed. */
	FVector RodPointInverseMassX = FVector::ZeroVector;
	FVector RodPointInverseMassY = FVector::ZeroVector;
	FVector RodPointInverseMassZ = FVector::ZeroVector;
	double PhysicsStepSeconds = 0.0;
	/** Accepted force segments awaiting Chaos; predict only the future force endpoint, never a body transform. */
	double PendingLineResponseSeconds = 0;
	FVector PendingLineImpulseNewtonSeconds = FVector::ZeroVector;
	FVector PendingLinePositionMomentNewtonSecondsSquared = FVector::ZeroVector;
	/** Owner movement alignment with line resistance, within [-1, 1]. */
	double CatSupportAlignment = 1.0;
	/** False means a genuinely static anchor. True predicts force response only; Chaos owns all endpoint poses. */
	bool bPhysicalRodEndpoint = false;
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
	/** 行为层已平滑的实际主动出力，范围[0,1]；零出力仍保留活鱼惯性。 */
	double FishEffortRatio = 1.0;
	double LineLengthCentimeters = 0.0;
	double AbsoluteRodWear = 0.0;
	FVector FishWorldPosition = FVector::ZeroVector;
	FVector FishVelocityCentimetersPerSecond = FVector::ZeroVector;
	ECatFightCatAction CatAction = ECatFightCatAction::None;
	ECatFishMotionIntent MotionIntent = ECatFishMotionIntent::None;
	double StrongConfrontationBuildUpSeconds = 0.0;
};

/**
 * 供诊断与回归测试消费的单步中间量。
 *
 * 单位约定：世界距离 cm，速度 cm/s，加速度 cm/s²，质量 kg，力 N，时间 s。
 * 这些字段是求解过程的只读快照，不参与下一步状态，也不构成第二份玩法状态。
 * 公式主链为：
 *   Alignment = dot(FishDirection, HorizontalOutward)
 *   LineLoad = max(Alignment, 0)^AngleStrengthExponent
 *   Force = Strength * ForcePerStrengthNewtons
 *   a = 100 * Force / Mass                         // m/s² 转 UE cm/s²
 *   T = SolveUnilateralConstraint(FishEnd(T), ObservedEndpointResponse(T), LineLength)
 *   RodForceUE = 100 * RodLineForceNewtons; Chaos integrates cat, hand and rod motion.
 */
struct CATFISHING_API FCatFightSimulationTrace
{
	ECatFightSimulationRejectReason RejectReason = ECatFightSimulationRejectReason::None;
	double FixedStepSeconds = 0.0;
	double DistanceBeforeCentimeters = 0.0;
	double HorizontalDistanceCentimeters = 0.0;
	double VerticalDistanceCentimeters = 0.0;
	double FishAlignment = 0.0;
	double NormalizedLineLoad = 0.0;
	double RodLineAlignment = 1.0;
	double RodLeverageMultiplier = 1.0;
	double OperatorCatStrength = 0.0;
	double EffectiveCatStrength = 0.0;
	double ActiveFishStrength = 0.0;
	double CatForceNewtons = 0.0;
	double FishThrustNewtons = 0.0;
	double OperatorBodyMassKilograms = 0.0;
	double CatDriveAccelerationCentimetersPerSecondSquared = 0.0;
	double FishDriveAccelerationCentimetersPerSecondSquared = 0.0;
	double FishFullEffortSpeedCentimetersPerSecond = 0.0;
	double FishEffortRatio = 0.0;
	double FishFullEffortThrustNewtons = 0.0;
	double FishLinearDragKilogramsPerSecond = 0.0;
	double FishIntendedDistanceCentimeters = 0.0;
	double FishActualIntentProgressCentimeters = 0.0;
	double FishUnfulfilledDistanceCentimeters = 0.0;
	double FishStaminaPerUnfulfilledMeter = 0.0;
	double SwimSpeedCentimetersPerSecond = 0.0;
	double MobilityCentimetersPerNewton = 0.0;
	double ExistingPositionErrorCentimeters = 0.0;
	double RequiredTensionAtCurrentLengthNewtons = 0.0;
	double RequiredTensionAtPaidOutLengthNewtons = 0.0;
	double ReelForceLimitNewtons = 0.0;
	double FishCorrectionCentimeters = 0.0;
	double FishPositionCorrectionCentimeters = 0.0;
	FVector ConstraintRodEndWorldPosition = FVector::ZeroVector;
	double LineTensionNewtons = 0.0;
	double FishLineForceNewtons = 0.0;
	double CatLineForceNewtons = 0.0;
	double HorizontalLineFactor = 1.0;
	double CatStaminaAfterStep = 0.0;
	double FishStaminaAfterStep = 0.0;
	double FishStaminaDrainBeforeClamp = 0.0;
	double CatReelPositiveWorkUnits = 0.0;
	double CatRodPositiveWorkUnits = 0.0;
	double CatHoldNormalizedLoad = 0.0;
	double CatRodNormalizedLoad = 0.0;
	double CatRodSupportBeforeHoldDeduction = 0.0;
	double WearLoad = 0.0;
	double RodWearDelta = 0.0;
	bool bInputAccepted = false;
	bool bFreeSpool = false;
	bool bLineRestraining = false;
	bool bReeling = false;
	bool bStruggling = false;
	bool bFinalizeInputAccepted = false;
};

struct CATFISHING_API FCatFightStepResult
{
	bool bSucceeded = false;
	/** 失败时保留 fail-closed 原因；成功时为 None。 */
	ECatFightSimulationRejectReason RejectReason = ECatFightSimulationRejectReason::None;
	/** 纯模拟器的可回放中间量；Runner 只读并写入限频诊断日志。 */
	FCatFightSimulationTrace Trace;
	bool bExhaustedCatEscape = false;
	/** 主位右键且尚有线杯容量时回体并屏蔽双方耗体；满线和强制力竭拖拽不回体。 */
	bool bSlackRecoveryActive = false;
	double IntendedSwimSpeedCentimetersPerSecond = 0.0;
	double CatStaminaDrain = 0.0;
	double FishStaminaDrain = 0.0;
	/** 四类努力由同一主控独立承担；Runner按主控真实输入另结移动费。 */
	double CatReelStaminaDrain = 0.0;
	double CatRodStaminaDrain = 0.0;
	double CatRodWorkStaminaDrain = 0.0;
	/** 超出沿线持竿支撑的转杆支撑费用；由主控承担，同一负载不重复收取。 */
	double CatRodSupportStaminaDrain = 0.0;
	double CatHoldStaminaDrain = 0.0;
	double CatRodExertionSquaredSeconds = 0.0;
	double CatRodPositiveWorkRadians = 0.0;
	double CatHoldIntentCentimeters = 0.0;
	double CatNormalizedEffortLoad = 0.0;
	double CatRodNormalizedEffortLoad = 0.0;
	double FishIntendedDistanceCentimeters = 0.0;
	/** 沿主动方向的有符号实际进展；已剔除历史位置纠偏。 */
	double FishActualIntentProgressCentimeters = 0.0;
	double FishUnfulfilledDistanceCentimeters = 0.0;
	double FishUncappedStaminaDrain = 0.0;
	double GetRodActionStaminaDrain() const { return CatReelStaminaDrain + CatRodStaminaDrain + CatHoldStaminaDrain; }
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
	/** 受力积分的鱼速度；几何纠偏不注入惯性，地形碰撞再修正该速度。 */
	FVector ResolvedFishVelocityCentimetersPerSecond = FVector::ZeroVector;
	/** 本步历史位置误差修正；不计入惯性或鱼主动做功。 */
	FVector FishPositionCorrectionWorldDisplacement = FVector::ZeroVector;
	/** 本步实际提交的游动努力方向；地形反馈更新下步转向时不能改写本步费用。 */
	FVector FishEffortDirection = FVector::ZeroVector;
	double FishLineAlignment = 0.0;
	double NormalizedLineLoad = 0.0;
	double RodLineAlignment = 1.0;
	double RodLeverageMultiplier = 1.0;
	double EffectiveCatStrength = 0.0;
	double OperatorCatStrength = 0.0;
	double CatDriveAccelerationCentimetersPerSecondSquared = 0.0;
	double FishDriveAccelerationCentimetersPerSecondSquared = 0.0;
	/** 同一根鱼线两端的张力；竿尖接收一次，身体只通过真实约束受力。 */
	double LineTensionNewtons = 0.0;
	/** The exact force vector used in the fish solve; the physical shaft receives this vector once. */
	FVector RodLineForceNewtons = FVector::ZeroVector;
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

/** 无副作用的鱼/线固定步求解。真实竿尖由物理接收方提供，猫的运动与个人费用由身体/Runner处理。 */
class CATFISHING_API FCatFishingFightSimulator
{
public:
	/** 已放线长耗尽线杯容量；鱼游近产生的余线不会恢复容量。距离单位为 cm。 */
	static bool IsLineAtMaximum(const FCatFightSimulationConfig& Config, double LineLengthCentimeters);
	/** 对最终落点/线长重算费用与终局，幂等且没有 ASC/装备副作用。 */
	static bool FinalizeResolvedStep(const FCatFightSimulationConfig& Config,
		const FCatFightSimulationState& State, const FCatFightRodConstraintInput& RodConstraint,
		FCatFightStepResult& Result);
	/** 活鱼趁持竿主控力竭时持续外冲；无人持竿/力竭鱼不进入。 */
	static bool ShouldEscapeExhaustedCat(const FCatFightSimulationConfig& Config,
		const FCatFightSimulationState& State, bool bRodHeld);
	static FCatFightStepResult Step(const FCatFightSimulationConfig& Config,
		const FCatFightSimulationState& State, const FVector& RodTipWorldPosition,
		const FVector& DesiredFishDirection);
	static FCatFightStepResult Step(const FCatFightSimulationConfig& Config,
		const FCatFightSimulationState& State, const FCatFightRodConstraintInput& RodConstraint,
		const FVector& DesiredFishDirection);
};
