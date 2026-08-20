#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "CatFishingFightModel.generated.h"

/**
 * 鱼在遛鱼里的运动状态。飞书「鱼的行为」页（2026-08-18 正式方案）拍定鱼只有两个状态：向外游（发力）和向内游（休息）；
 * 僵持、翻肚都是判定结果，不是状态。状态按"段"推进，段内不换向，上钩瞬间一定是向外游。
 */
UENUM(BlueprintType)
enum class ECatFishSwimState : uint8
{
	/** 搏斗尚未开始或已经结束，没有运动状态可言。 */
	None,
	/** 向外游：鱼在发力往远处跑，D 有机会增加，也是唯一会触发 4.3 判定表①~④的状态。 */
	Outward,
	/** 向内游：鱼在休息往岸边漂，猫拖或松都只会让 D 减少。 */
	Inward
};

/** 钓手对鱼竿的即时操作；它是"现在按着哪个键"的状态，不是一次性命令，因此没有 RequestId。 */
UENUM(BlueprintType)
enum class ECatFishingFightIntent : uint8
{
	/** 什么都没按。向外游时按飞书表里"松"处理（工程暂定：不动视同松，见决策记录 D-17）。 */
	None,
	/** 拖（左键）：收线对抗。真咬期按它就是提竿。 */
	Pull,
	/** 松（右键）：放线让鱼跑，自己回体力。 */
	Release
};

/** 一次搏斗的终局；None 表示还在打。 */
UENUM(BlueprintType)
enum class ECatFishingFightOutcome : uint8
{
	/** 还没分出结果。 */
	None,
	/** 断竿：判定表①（竿强度 ≤ min(猫力,鱼力)）或消耗战把耐久磨到 0；失败，鱼逃。 */
	RodBroken,
	/** 猫被拖下水：判定表②（鱼力 ≥ 猫力）或猫体力归零；失败，鱼逃。 */
	CatDraggedIn,
	/** 绝对碾压：判定表③（猫力 ≥ 鱼力×2），D 直接归零、鱼甩上岸；成功，进 NearShore。 */
	Overpowered,
	/** 鱼体力耗尽翻白肚，D 归零；成功，进 NearShore 等抄或拖上岸。 */
	FishExhausted,
	/** 鱼被正常遛到离岸不足 1 米（工程暂定出口，见决策记录 D-18）；成功，进 NearShore。 */
	ReeledToShore
};

/** 飞书 4.3 判定表"向外游 + 拖"一行按 ①→④ 顺序得到的唯一出口。 */
UENUM()
enum class ECatFishingPullVerdict : uint8
{
	/** ① 竿强度 ≤ min(猫力, 鱼力)：断竿。 */
	RodBroken,
	/** ② 鱼力 ≥ 猫力（未命中①）：猫被拖下水。 */
	CatDraggedIn,
	/** ③ 猫力 ≥ 鱼力×2.0（未命中①②）：绝对碾压。 */
	Overpowered,
	/** ④ 其余组合：僵持，进入 4.4 消耗战。 */
	Stalemate
};

/**
 * 一次搏斗里不会变的输入：进入 HookedFight 时由会话从猫属性、鱼定义、竿定义和配置冻结一次。
 * 把它和逐帧变化的 FCatFishingFightState 分开，是为了让判定表和消耗战能脱离 Actor/World 被测试直接驱动。
 */
USTRUCT()
struct CATFISHING_API FCatFishingFightParams
{
	GENERATED_BODY()

	/** 猫力量：合法参与者 FishingStrength 合计（单人就是本猫的 50）；判定表的"猫力"和消耗战里鱼体力扣减的乘数。 */
	double CatStrength = 0.0;

	/** 鱼的基础力量 = 本条鱼实际重量 × 鱼种力量系数 K（完美中鱼已在这里削减过）；垂死挣扎期间实际生效值再 ×1.5。 */
	double FishStrengthBase = 0.0;

	/** 竿强度（静态）；判定表①用。来自竿定义 RodStrength。 */
	double RodStrength = 0.0;

	/** 放线上限 L_max，单位米；来自竿定义 MaximumLineLengthMeters。 */
	double LineMaxMeters = 0.0;

	/** 鱼种游速系数：按体重档 小 0.8／中 1.0／大 1.2／巨影 1.5，乘在 D/L 速率表的每一行上。 */
	double FishSpeedCoefficient = 1.0;

	/** 猫体力上限；向外游+松时每秒 +1.5 回体力不能超过它。飞书 §4.2 拍定 100，工程取 CatAbilitySettings 的初值作为上限（两者同为 100）。 */
	double CatStaminaMax = 0.0;

	/** 鱼-岸距离小到这个值（米）以内就算到了近岸：飞书 §5"离岸不足 1 米才可抄"，与 ScoopReachCentimeters 同源。 */
	double NearShoreDistanceMeters = 0.0;

	/** 段末方向 roll 的基础概率 P_base（按食性：肉 0.7／杂 0.5／素 0.35）；鱼表没有食性列，会话按工程暂定的杂食 0.5 传入。 */
	double OutwardProbabilityBase = 0.5;

	/** 是否巨影：体力耗尽前 P(向外)=100%，不吃体力/水深修正，不触发垂死挣扎，只有僵持消耗战一条路。 */
	bool bGiant = false;
};

/** 逐帧推进的搏斗运行态；服务器私有，会话把其中需要给客户端看的字段拷进 Snapshot。 */
USTRUCT()
struct CATFISHING_API FCatFishingFightState
{
	GENERATED_BODY()

	/** 鱼猫距离 D，单位米；初值 = 浮漂落点距离。 */
	double DistanceMeters = 0.0;

	/** 已放线长 L，单位米；初值 = D₀，模型保证 L ≥ D 且 L ≤ L_max。 */
	double LineMeters = 0.0;

	/** 鱼当前段的方向。 */
	ECatFishSwimState SwimState = ECatFishSwimState::None;

	/** 当前段还剩多少秒；≤0 时在下一次推进开头 roll 新段。 */
	double SegmentRemainingSeconds = 0.0;

	/** 鱼当前体力；僵持时每秒 -= 猫力×0.08，≤0 翻肚。 */
	double FishStamina = 0.0;

	/** 鱼体力起点（完美中鱼削减后）；体力修正 M_体力 和垂死挣扎的 30% 门槛都按它算比例。 */
	double FishStaminaMax = 0.0;

	/** 垂死挣扎是否已经触发过；一局限一次。 */
	bool bDeathStruggleUsed = false;

	/** 垂死挣扎前摇剩余秒数（0.5 秒预警，期间力量不加成）。 */
	double DeathStruggleWindupRemainingSeconds = 0.0;

	/** 垂死挣扎生效剩余秒数（2 秒，期间鱼力量 ×1.5）。 */
	double DeathStruggleActiveRemainingSeconds = 0.0;

	/** 本次搏斗的终局；一旦不是 None，Step 不再改变任何值。 */
	ECatFishingFightOutcome Outcome = ECatFishingFightOutcome::None;

	/** 当前是否处于垂死挣扎（前摇或生效期）；给 Snapshot 的预警位。 */
	bool IsDeathStruggling() const
	{
		return DeathStruggleWindupRemainingSeconds > 0.0 || DeathStruggleActiveRemainingSeconds > 0.0;
	}
};

/** 一次推进时模型需要读到的外部资源当前值；它们的真身在 ASC（猫体力）和 EquipmentComponent（竿耐久），模型只读不写。 */
struct FCatFishingFightResources
{
	/** 钓手当前 FightStamina。 */
	double CatStamina = 0.0;

	/** 钓手鱼竿当前耐久（已经扣掉会话尚未提交给 Equipment 的累计磨损）。 */
	double RodDurability = 0.0;
};

/** 一次推进要求会话对外部资源做的改动；会话负责把它们真正写进 ASC 与 Equipment。 */
struct FCatFishingFightStepDelta
{
	/** 猫体力增量：负数是消耗，正数是向外游+松时的回复；会话写入时再按 [0, CatStaminaMax] 夹取。 */
	double CatStaminaDelta = 0.0;

	/** 本次要从竿上磨掉的耐久（≥0）；断竿出口时会话会把它补成"把剩余耐久全磨光"。 */
	double RodDurabilityCost = 0.0;
};

/**
 * 遛鱼/搏斗的纯规则层：飞书钓鱼规则 4.3 判定表、4.4 消耗战、D/L 距离模型、鱼的两状态运动与垂死挣扎、完美中鱼削减。
 * 这里没有 Actor、World、ASC 或 Equipment，全部输入显式传入、全部副作用写在 State/Delta 里，会话只是它的宿主。
 */
namespace CatFishingFightModel
{
	/**
	 * 飞书 4.3 判定表"向外游 + 拖"按 ①→④ 顺序判定，取等归属从严（等号全部落在更坏的一侧）：
	 * ① 竿强度 ≤ min(猫力,鱼力) → 断竿；② 鱼力 ≥ 猫力 → 拖下水；③ 猫力 ≥ 鱼力×2.0 → 碾压；④ 其余 → 僵持。任意组合有且仅一个出口。
	 */
	CATFISHING_API ECatFishingPullVerdict JudgeOutwardPull(double CatStrength, double FishStrength, double RodStrength);

	/**
	 * 段末方向 roll 的 P(向外) = P_base × M_体力 × M_水深，结果夹在 [5%, 95%]。
	 * M_体力：体力比例 >50% ×1.0；30%~50% ×0.8；<30% ×0.6。M_水深（鱼-岸距离）：>10m ×1.0；3~10m 从 ×1.0 线性过渡到 ×1.5；<3m ×1.8。
	 */
	CATFISHING_API double ComputeOutwardProbability(double BaseProbability, double FishStaminaRatio, double DistanceMeters);

	/** 按飞书鱼册拍定的四档体重轴（小 <1kg／中 1~8／大 8~15／巨影 >15，三条链共用阈值）给出游速系数 0.8／1.0／1.2／1.5。 */
	CATFISHING_API double ComputeFishSpeedCoefficient(double WeightKilograms);

	/**
	 * 把完美中鱼的削减作用在鱼的基础力量和体力上：普通鱼力量 -20%、体力 -15%（飞书 §4）。
	 * 鱼表稀有度列为空，所以稀有鱼档（-15%/-10%）拿不到数据，一律按普通鱼处理（决策记录 D-16）。
	 */
	CATFISHING_API void ApplyPerfectHookReduction(double& InOutFishStrength, double& InOutFishStamina);

	/** 开始一次搏斗：D=L=初始距离，鱼体力满，第一段强制向外游并 roll 段长；Outcome 清回 None。 */
	CATFISHING_API void BeginFight(FCatFishingFightState& State, const FCatFishingFightParams& Params,
		double InitialDistanceMeters, double FishStaminaInitial, FRandomStream& Random);

	/**
	 * 推进 DeltaSeconds 的搏斗：先结算段（段到期就按概率 roll 新方向和段长，向外段开始时可能触发垂死挣扎），
	 * 再按"鱼状态 × 猫操作"查速率表和判定表改 D/L 并算出对猫体力和竿耐久的增量，最后按 4.4 的归零优先级
	 * （鱼体力 → 猫体力 → 竿耐久）和近岸距离决定终局。已经有终局时什么都不做。
	 */
	CATFISHING_API void Step(FCatFishingFightState& State, const FCatFishingFightParams& Params,
		ECatFishingFightIntent Intent, double DeltaSeconds, const FCatFishingFightResources& Resources,
		FRandomStream& Random, FCatFishingFightStepDelta& OutDelta);
}
