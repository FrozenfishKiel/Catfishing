#pragma once

#include "CoreMinimal.h"
#include "Fishing/CatFishingTypes.h"

/** 单步终局；由 Session 映射为公开 Outcome。除 None 外每步最多产生一个。 */
enum class ECatFightStepOutcome : uint8
{
	None,
	/** 鱼体力归零 → 翻肚（D 归零，可抄/可拖上岸）。 */
	FishExhausted,
	/** 猫体力归零 → 力竭被拖下水（规格 4.4 归零优先级第 2 位）。 */
	CatStaminaExhausted,
	/** 竿耐久归零（磨损断）或竿强度不足（瞬断，判定①）。 */
	RodBroken,
	/** 数值异常：鱼距超出线长上限 + 松弛裕度；正常规则下不应出现。 */
	Escaped,
	/** 鱼距 ≤ 近岸线长阈值 → 进入 NearShore。 */
	NearShore,
	/** 判定②：向外游+拖 且 鱼力 ≥ 猫力 → 猫被拖下水。 */
	DraggedIntoWater,
	/** 判定③：向外游+拖 且 猫力 ≥ 鱼力×比 → 碾压，鱼直接甩上岸（D 归零）。 */
	Overpowered
};

/** 猫本步的输入意图；左键=拖，右键=放线，都不按=不动。 */
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

	/** 三方力量（规格 4.2）：猫品种力量 / 鱼种力量（含完美折减）/ 竿静态强度。 */
	double CatStrength = 0.0;
	double FishStrength = 0.0;
	double RodStrength = 0.0;

	/** 猫体力上限，放线回复不超过它。 */
	double CatStaminaMaximum = 0.0;

	/** 向内游+拖：猫体力消耗 = 鱼力 × 本系数 /秒（规格 0.15）。 */
	double InwardPullCatDrainPerFishStrength = 0.15;
	/** 向内游+拖：鱼体力消耗 = 猫力 × 本系数 /秒。拖永远双方掉体力，顺从/挣扎只是系数档位不同。 */
	double InwardPullFishDrainPerCatStrength = 0.08;
	/** 僵持每秒：竿耐久 -= 鱼力×0.1；鱼体力 -= 猫力×0.08；猫体力 -= 鱼力×0.12。 */
	double StalemateRodWearPerFishStrength = 0.1;
	double StalemateFishDrainPerCatStrength = 0.08;
	double StalemateCatDrainPerFishStrength = 0.12;
	/** 向外游+放线：猫体力回复 /秒（规格 1.5）。 */
	double SlackStaminaRegenPerSecond = 1.5;

	/** 鱼挣扎（向外游）且猫既不拖也不放线：竿耐久基础磨损 /秒（来自鱼竿 DA 的 BaseDurabilityWearPerSecond；0=不磨）。 */
	double StruggleHoldRodWearPerSecond = 0.0;
	/** 线放尽绷紧时的磨损倍率（来自鱼竿 DA 的 HighTensionWearMultiplier，≥1）；乘在僵持磨损与基础磨损之上。 */
	double TautRodWearMultiplier = 1.0;
	/** 判定③碾压阈值：猫力 ≥ 鱼力 × 本比值（规格 2.0）。 */
	double OverpowerStrengthRatio = 2.0;

	/** 距离速率（临时值，规格 4.6 标 TODO）：拖回/鱼自游近/鱼外游。 */
	double ReelSpeedCentimetersPerSecond = 0.0;
	double FishCalmSpeedCentimetersPerSecond = 0.0;
	double FishStruggleSpeedCentimetersPerSecond = 0.0;

	double MaximumLineLengthCentimeters = 0.0;
	double RodDurability = TNumericLimits<double>::Max();
	double EscapeSlackCentimeters = 100.0;
	double NearShoreLineLengthCentimeters = 100.0;

	bool IsValid() const;
};

/** 可变数值状态；世界位置每步开始从 Encounter Actor 复制。 */
struct CATFISHING_API FCatFightSimulationState
{
	double CatStamina = 0.0;
	double FishStamina = 0.0;
	/** 已放出的线长 L；放线增加、拖回减少、不能超过 L_max。 */
	double LineLengthCentimeters = 0.0;
	double AbsoluteRodWear = 0.0;
	FVector FishWorldPosition = FVector::ZeroVector;
	ECatFightCatAction CatAction = ECatFightCatAction::None;
	ECatFishMotionIntent MotionIntent = ECatFishMotionIntent::None;
};

struct CATFISHING_API FCatFightStepResult
{
	bool bSucceeded = false;
	/** 正值=消耗，负值=回复（放线喘气）。 */
	double CatStaminaDrain = 0.0;
	double FishStaminaDrain = 0.0;
	double TensionCentimeters = 0.0;
	double LineLengthCentimeters = 0.0;
	double AbsoluteRodWear = 0.0;
	FVector ProposedFishWorldPosition = FVector::ZeroVector;
	/** 本步是否处于僵持消耗战（供表现/日志）。 */
	bool bStalemate = false;
	ECatFightStepOutcome Outcome = ECatFightStepOutcome::None;
};

/**
 * 无状态确定性遛鱼数学（规格 4.3/4.4）。不读 World 时间、Actor、资产或全局随机。
 * 向外游+拖 按 ①瞬断 → ②拖下水 → ③碾压 → ④僵持 顺序判定，先命中先生效，取等从严。
 */
class CATFISHING_API FCatFishingFightSimulator
{
public:
	static FCatFightStepResult Step(const FCatFightSimulationConfig& Config,
		const FCatFightSimulationState& State, const FVector& RodTipWorldPosition,
		const FVector& OutwardDirection);
};
