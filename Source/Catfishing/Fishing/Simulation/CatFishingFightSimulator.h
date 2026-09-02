#pragma once

#include "CoreMinimal.h"
#include "Fishing/CatFishingTypes.h"
#include "Fishing/Simulation/CatFishingCooperativePowerModel.h"

/** 单步终局；由 Session 映射为公开 Outcome。除 None 外每步最多产生一个。 */
enum class ECatFightStepOutcome : uint8
{
	None,
	/** 鱼体力归零 → 在当前位置翻肚，可抄或继续收至竿尖水面投影。 */
	FishExhausted,
	/** 猫体力归零 → 力竭被拖下水（规格 4.4 归零优先级第 2 位）。 */
	CatStaminaExhausted,
	/** 本场鱼线耐久归零，或钓组承载能力不足而瞬间断线；不会损坏场景鱼竿/装备。 */
	LineBroken,
	/** 数值异常：鱼距超出线长上限 + 松弛裕度；正常规则下不应出现。 */
	Escaped,
	/** 判定②：向外游+拖 且 鱼力 ≥ 猫力 → 猫被拖下水。 */
	DraggedIntoWater,
	/** 判定③：向外游+拖 且 猫力 ≥ 鱼力×比 → 碾压，鱼在当前位置直接力竭侧翻。 */
	Overpowered
};

/** 断线的直接数值原因；用于日志和调试，避免把鱼线断裂误判为鱼竿永久损坏。 */
enum class ECatFightLineBreakCause : uint8
{
	None,
	/** 钓组承载力量不高于猫力与鱼力中的较小值，强对抗确认后瞬断。 */
	StrengthOverload,
	/** 本场累计鱼线负载达到配置的耐久上限。 */
	DurabilityDepleted
};

/** 猫本步的输入意图；左键=收线，右键=松开线杯，都不按=锁住当前已放出线长。 */
enum class ECatFightCatAction : uint8
{
	None,
	Pull,
	Slack
};

/** 冻结自资产/配置的确定性输入；全部为每秒速率或无量纲系数，Step 内按 FixedStepSeconds 折算。 */
struct CATFISHING_API FCatFightSimulationConfig
{
	double FixedStepSeconds = 0.0;

	/** 主操作猫本步按蓄力百分比折算后的力量贡献。 */
	double PrimaryOperatorCatStrength = 0.0;
	/** 所有辅助位本步按各自蓄力百分比折算后的力量贡献合计。 */
	double SecondCatStrength = 0.0;
	/** 猫的总体力量为主位贡献与所有辅助位贡献之和。 */
	double GetCombinedCatStrength() const
	{
		return PrimaryOperatorCatStrength + SecondCatStrength;
	}

	/** 三方力量中的另两项：鱼种力量（含完美折减）/ 钓组静态承载强度。 */
	double FishStrength = 0.0;
	double RodStrength = 0.0;

	/** 猫体力上限，松线喘息回复不超过它。 */
	double CatStaminaMaximum = 0.0;
	/** 多人蓄力和猫体力配置；鱼体力仍使用下面的既有张力公式。 */
	FCatFightPowerTuning PowerTuning;
	/** 主位本步力量百分比；用于按文档计算主位体力，而不是再按鱼力量反推。 */
	double PrimaryPowerAlpha = 1.0;
	/** 主位完全放线而辅助位仍发力时，本步额外追加给主位的每秒体力消耗。 */
	double PrimaryDisruptionStaminaDrainPerSecond = 0.0;
	/** 向内游+拖：鱼体力消耗 = 猫力 × 本系数 /秒。拖永远双方掉体力，顺从/挣扎只是系数档位不同。 */
	double InwardPullFishDrainPerCatStrength = 0.08;
	/** 鱼性格的平静/顺从期体力消耗倍率。 */
	double BaseDrainMultiplier = 1.0;
	/** 鱼性格的挣扎期体力消耗倍率；必须高于平静倍率。 */
	double StruggleDrainMultiplier = 2.0;
	/** 僵持每秒：本场鱼线耐久 -= 鱼力×0.1；鱼体力仍按猫的实时有效合力结算。 */
	double StalemateRodWearPerFishStrength = 0.1;
	double StalemateFishDrainPerCatStrength = 0.08;
	/** 主位力量归零后的猫体力回复 /秒；右键可主动清空主位蓄力并立即进入该状态。 */
	double SlackStaminaRegenPerSecond = 1.5;

	/** 鱼挣扎且线杯未松开、鱼线绷紧时的本场鱼线基础磨损 /秒。 */
	double StruggleHoldRodWearPerSecond = 0.0;
	/** 线放尽绷紧时的磨损倍率（来自鱼竿 DA 的 HighTensionWearMultiplier，≥1）；乘在僵持磨损与基础磨损之上。 */
	double TautRodWearMultiplier = 1.0;
	/** 判定③碾压阈值：猫力 ≥ 鱼力 × 本比值（规格 2.0）。 */
	double OverpowerStrengthRatio = 2.0;

	/** 距离速率（临时值，规格 4.6 标 TODO）：拖回/鱼自游近/鱼外游。 */
	double ReelSpeedCentimetersPerSecond = 0.0;
	double FishCalmSpeedCentimetersPerSecond = 0.0;
	double FishStruggleSpeedCentimetersPerSecond = 0.0;
	/** 本步结算后的鱼体力 <= 此值时吸附到 0；与只显示整数的 UI 使用同一“耗尽”语义。 */
	double FishExhaustionThreshold = 0.5;
	/** max(cos(鱼游向与鱼线向外方向夹角),0) 达到此值才允许触发瞬时强对抗结局。 */
	double StrongConfrontationAlignmentThreshold = 0.55;
	/** 达到角度阈值后至少持续此时间才确认强对抗；0=立即确认。 */
	double StrongConfrontationConfirmationSeconds = 0.2;
	/** 对夹角投影做幂变换；1=线性。 */
	double AngleStrengthExponent = 1.0;
	/** 将“本步鱼试图超过线长的距离”归一化为表现张力时使用的响应范围。 */
	double TensionResponseRangeCentimeters = 10.0;

	/** 手持约束参数：方向决定杠杆，反向移动决定额外发力，线负载由鱼和持竿者共同承担。 */
	double MinimumRodLeverageMultiplier = 0.4;
	double MovementStrengthBoost = 0.35;
	double MovementReferenceSpeedCentimetersPerSecond = 300.0;
	double MaximumCarrierPullAccelerationCentimetersPerSecondSquared = 1200.0;
	/** 手持双端约束每秒最多修正到鱼端的水平距离；防止走动和收线叠成鱼的瞬移。 */
	double MaximumFishConstraintCorrectionSpeedCentimetersPerSecond = 160.0;
	/** 满负载且鱼不弱于猫时，持竿者沿远离鱼方向仍允许保留的最小速度比例。 */
	double MinimumCarrierAwaySpeedMultiplier = 0.15;

	double MaximumLineLengthCentimeters = 0.0;
	/** 本场鱼线耐久总量；旧字段名为兼容现有配置保留，每次新会话都会重置。 */
	double RodDurability = TNumericLimits<double>::Max();
	double EscapeSlackCentimeters = 100.0;

	bool IsValid() const;
};

/** 每个固定步从权威 Rod Actor/Pawn 采样的运动约束；客户端只能提交普通移动/视角，不能提交这些结果。 */
struct CATFISHING_API FCatFightRodConstraintInput
{
	FVector RodTipWorldPosition = FVector::ZeroVector;
	/** 规范握把到竿尖方向；不读取客户端动画 Socket。 */
	FVector RodForwardWorld = FVector::ForwardVector;
	FVector RodTipVelocityCentimetersPerSecond = FVector::ZeroVector;
	FVector CarrierVelocityCentimetersPerSecond = FVector::ZeroVector;
	bool bRodHeld = false;
};

/** 可变数值状态；世界位置每步开始从 Encounter Actor 复制。 */
struct CATFISHING_API FCatFightSimulationState
{
	/** 当前是否有主操作手；无人接管时 CatAction 固定为 Slack，但不结算任何玩家力量或体力。 */
	bool bOperatorPresent = true;
	double CatStamina = 0.0;
	double FishStamina = 0.0;
	/** 已放出的线长 L_paid；左键收短，右键松线时只随鱼实际外游被动增长，不按键时固定。 */
	double LineLengthCentimeters = 0.0;
	/** 本场累计鱼线负载；旧字段名为兼容现有测试/配置保留，不写入装备永久耐久。 */
	double AbsoluteRodWear = 0.0;
	FVector FishWorldPosition = FVector::ZeroVector;
	ECatFightCatAction CatAction = ECatFightCatAction::None;
	ECatFishMotionIntent MotionIntent = ECatFishMotionIntent::None;
	/** 连续满足强对抗角度和受力条件的时间；不满足任一条件立即清零。 */
	double StrongConfrontationBuildUpSeconds = 0.0;
};

struct CATFISHING_API FCatFightStepResult
{
	bool bSucceeded = false;
	/** 本步鱼主动选择的自由游速，单位 cm/s；在线长、岸线和水域约束前产生，仅供表现层表达游动意图。 */
	double IntendedSwimSpeedCentimetersPerSecond = 0.0;
	/** 正值=消耗，负值=回复（松开线杯喘气）。 */
	double CatStaminaDrain = 0.0;
	double FishStaminaDrain = 0.0;
	double TensionCentimeters = 0.0;
	/** 竿尖到鱼的实际直线距离 D。 */
	double StraightLineDistanceCentimeters = 0.0;
	/** max(L_paid-D,0)；大于 0 时 Cable 应出现垂坠。 */
	double SlackLineLengthCentimeters = 0.0;
	double NormalizedTension = 0.0;
	bool bLineTaut = false;
	double LineLengthCentimeters = 0.0;
	/** 本场累计鱼线负载；只参与断线判定。 */
	double AbsoluteRodWear = 0.0;
	FVector ProposedFishWorldPosition = FVector::ZeroVector;
	/** 鱼游向与“竿尖→鱼”的水平鱼线方向点积，范围 [-1,1]；负值表示鱼正在朝竿尖游。 */
	double FishLineAlignment = 0.0;
	/** pow(max(FishLineAlignment,0), AngleStrengthExponent)，用于体力/磨损的连续力量比例。 */
	double NormalizedLineLoad = 0.0;
	/** 竿身方向与当前鱼线方向的点积，[0,1]。 */
	double RodLineAlignment = 1.0;
	/** 最低杠杆到 1 之间的实际有效倍率。 */
	double RodLeverageMultiplier = 1.0;
	/** 持竿者/竿尖沿反鱼线方向移动速度的归一化贡献。 */
	double CarrierMovementAlpha = 0.0;
	/** 本步把基础猫力、竿向和玩家移动合成后的力量。 */
	double EffectiveCatStrength = 0.0;
	/** 当前主位蓄力百分比与多人合力，供 Session/HUD 使用同一权威固定步结果。 */
	double PrimaryPowerAlpha = 0.0;
	double CombinedCatStrength = 0.0;
	int32 ActiveHelperCount = 0;
	/** 本步鱼线约束反向施加给持竿角色的水平加速度。 */
	double CarrierPullAccelerationCentimetersPerSecondSquared = 0.0;
	/** 双端约束允许持竿者沿远离鱼方向保留的速度比例；1 表示无约束。 */
	double CarrierAwaySpeedMultiplier = 1.0;
	/** 本步进入双端求解前的线长误差；包含鱼游动、竿尖移动和收线改变的共同结果。 */
	double ConstraintErrorCentimeters = 0.0;
	/** 鱼相对竿尖沿鱼线向外的速度，再加上线杯收短速率；正值表示约束正在收紧。 */
	double RelativeConstraintSpeedCentimetersPerSecond = 0.0;
	/** 同一份约束误差最终分配给鱼端的水平位置修正。 */
	double FishConstraintCorrectionCentimeters = 0.0;
	double StrongConfrontationBuildUpSeconds = 0.0;
	/** 本步是否处于僵持消耗战（供表现/日志）。 */
	bool bStalemate = false;
	/** 已达到性格配置的强对抗角度阈值；瞬断/拖下水/碾压只在这里裁决。 */
	bool bStrongConfrontation = false;
	/** Outcome 为 LineBroken 时给出具体来源；其余终局保持 None。 */
	ECatFightLineBreakCause LineBreakCause = ECatFightLineBreakCause::None;
	ECatFightStepOutcome Outcome = ECatFightStepOutcome::None;
};

/**
 * 无状态确定性遛鱼数学。不读 World 时间、Actor、资产或全局随机。
 * 用鱼游向在鱼线方向的投影计算连续力量；确认强对抗后按 ①断线 → ②拖下水 → ③碾压 → ④僵持裁决。
 */
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
