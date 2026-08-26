#include "Fishing/CatFishingFightModel.h"

namespace CatFishingFightModel
{
	// 下面这组常量全部照抄飞书「钓鱼规则」4.3/4.4 与「鱼的行为」页（2026-08-18 快照），不是工程自拟；改数请改飞书再同步这里。
	namespace Rules
	{
		/** 4.3 ③：猫力达到鱼力的这个倍数就是绝对碾压。 */
		constexpr double OverpowerRatio = 2.0;
		/** 4.3 向内游+拖：猫体力每秒 -= 鱼力量 × 0.15。 */
		constexpr double InwardPullCatStaminaPerFishStrength = 0.15;
		/** 4.3 向外游+松：猫体力每秒 +1.5。 */
		constexpr double OutwardReleaseCatStaminaRegenPerSecond = 1.5;
		/** 消耗战：本场鱼线耐久每秒 -= 鱼力量 × 0.1。 */
		constexpr double StalemateRodWearPerFishStrength = 0.1;
		/** 4.4 消耗战：鱼体力每秒 -= 猫力 × 0.08。 */
		constexpr double StalemateFishStaminaPerCatStrength = 0.08;
		/** 4.4 消耗战：猫体力每秒 -= 鱼力量 × 0.12。 */
		constexpr double StalemateCatStaminaPerFishStrength = 0.12;
		/** D/L 速率表：向内游+拖 D 与 L 每秒 -3。 */
		constexpr double InwardPullMetersPerSecond = 3.0;
		/** D/L 速率表：向内游+松/不动 D 每秒 -1，L 不变。 */
		constexpr double InwardReleaseMetersPerSecond = 1.0;
		/** D/L 速率表：向外游+松 D 与 L 每秒 +2.5。 */
		constexpr double OutwardReleaseMetersPerSecond = 2.5;
		/** 段长：发力（向外）段 3~6 秒。 */
		constexpr double OutwardSegmentMinSeconds = 3.0;
		constexpr double OutwardSegmentMaxSeconds = 6.0;
		/** 段长：休息（向内）段 2~5 秒。 */
		constexpr double InwardSegmentMinSeconds = 2.0;
		constexpr double InwardSegmentMaxSeconds = 5.0;
		/** 方向 roll 的概率夹取区间 [5%, 95%]。 */
		constexpr double OutwardProbabilityMin = 0.05;
		constexpr double OutwardProbabilityMax = 0.95;
		/** 垂死挣扎：鱼体力首次跌破 30% 后，每个向外段开始 roll 35%，一局一次；0.5 秒前摇 + 2 秒 ×1.5。 */
		constexpr double DeathStruggleStaminaRatio = 0.3;
		constexpr double DeathStruggleChance = 0.35;
		constexpr double DeathStruggleWindupSeconds = 0.5;
		constexpr double DeathStruggleActiveSeconds = 2.0;
		constexpr double DeathStruggleStrengthMultiplier = 1.5;
		/** 完美中鱼（普通鱼）：力量 -20%、体力 -15%。 */
		constexpr double PerfectHookStrengthKeep = 0.8;
		constexpr double PerfectHookStaminaKeep = 0.85;
	}

	// 段 roll 流程：巨影在体力耗尽前 P=1 且不吃修正、不触发挣扎；其他鱼按 P_base×M_体力×M_水深 夹取后 roll 方向，再按方向 roll 段长；
	// 向外段开始时若体力已跌破 30%、挣扎尚未用过，再 roll 一次 35% 决定是否进入垂死挣扎（前摇 0.5 秒 + 生效 2 秒）。
	static void RollNextSegment(FCatFishingFightState& State, const FCatFishingFightParams& Params, FRandomStream& Random)
	{
		const double StaminaRatio = State.FishStaminaMax > 0.0 ? State.FishStamina / State.FishStaminaMax : 0.0;
		const double OutwardProbability = Params.bGiant
			? 1.0
			: ComputeOutwardProbability(Params.OutwardProbabilityBase, StaminaRatio, State.DistanceMeters);
		// FRand 取值在 [0,1)，所以 P=1 时恒为向外，巨影不需要单独分支。
		const bool bOutward = Random.FRand() < OutwardProbability;
		State.SwimState = bOutward ? ECatFishSwimState::Outward : ECatFishSwimState::Inward;
		State.SegmentRemainingSeconds = bOutward
			? Random.FRandRange(Rules::OutwardSegmentMinSeconds, Rules::OutwardSegmentMaxSeconds)
			: Random.FRandRange(Rules::InwardSegmentMinSeconds, Rules::InwardSegmentMaxSeconds);
		if (bOutward && !Params.bGiant && !State.bDeathStruggleUsed && StaminaRatio < Rules::DeathStruggleStaminaRatio
			&& Random.FRand() < Rules::DeathStruggleChance)
		{
			State.bDeathStruggleUsed = true;
			State.DeathStruggleWindupRemainingSeconds = Rules::DeathStruggleWindupSeconds;
			State.DeathStruggleActiveRemainingSeconds = Rules::DeathStruggleActiveSeconds;
		}
	}

	// 判定表流程：三个输入逐条按 ①②③ 比较，每条都用 ≤ / ≥ 让取等落到更坏的一侧；都没命中才是 ④ 僵持。
	ECatFishingPullVerdict JudgeOutwardPull(const double CatStrength, const double FishStrength, const double RodStrength)
	{
		if (RodStrength <= FMath::Min(CatStrength, FishStrength))
		{
			return ECatFishingPullVerdict::LineBroken;
		}
		if (FishStrength >= CatStrength)
		{
			return ECatFishingPullVerdict::CatDraggedIn;
		}
		if (CatStrength >= FishStrength * Rules::OverpowerRatio)
		{
			return ECatFishingPullVerdict::Overpowered;
		}
		return ECatFishingPullVerdict::Stalemate;
	}

	// 概率流程：先按体力比例选 M_体力（>50% 1.0；30%~50% 0.8；<30% 0.6），再按鱼-岸距离选 M_水深（>10m 1.0；3~10m 从
	// 1.0 线性升到 1.5；<3m 1.8），乘完夹到 [5%,95%]。
	double ComputeOutwardProbability(const double BaseProbability, const double FishStaminaRatio, const double DistanceMeters)
	{
		double StaminaModifier = 1.0;
		if (FishStaminaRatio < Rules::DeathStruggleStaminaRatio)
		{
			StaminaModifier = 0.6;
		}
		else if (FishStaminaRatio <= 0.5)
		{
			StaminaModifier = 0.8;
		}
		double DepthModifier = 1.0;
		if (DistanceMeters < 3.0)
		{
			DepthModifier = 1.8;
		}
		else if (DistanceMeters <= 10.0)
		{
			// 10m 处 ×1.0，3m 处 ×1.5，中间线性。
			DepthModifier = 1.0 + 0.5 * (10.0 - DistanceMeters) / 7.0;
		}
		return FMath::Clamp(BaseProbability * StaminaModifier * DepthModifier,
			Rules::OutwardProbabilityMin, Rules::OutwardProbabilityMax);
	}

	// 体重档流程：小 <1kg ×0.8；中 1~8kg ×1.0；大 8~15kg ×1.2；巨影 >15kg ×1.5。档边界按飞书写法取闭区间归到较小一档（1kg 与 8kg 算中，15kg 算大）。
	double ComputeFishSpeedCoefficient(const double WeightKilograms)
	{
		if (WeightKilograms < 1.0)
		{
			return 0.8;
		}
		if (WeightKilograms <= 8.0)
		{
			return 1.0;
		}
		if (WeightKilograms <= 15.0)
		{
			return 1.2;
		}
		return 1.5;
	}

	// 完美中鱼流程：按普通鱼系数把力量乘 0.8、体力乘 0.85；稀有鱼没有数据来源，所以没有第二档分支。
	void ApplyPerfectHookReduction(double& InOutFishStrength, double& InOutFishStamina)
	{
		InOutFishStrength *= Rules::PerfectHookStrengthKeep;
		InOutFishStamina *= Rules::PerfectHookStaminaKeep;
	}

	// 开局流程：D₀=L₀=初始距离，体力满值并记为上限，挣扎标记清空；第一段不 roll 方向、强制向外（飞书：上钩瞬间初始状态=向外游），只 roll 段长。
	void BeginFight(FCatFishingFightState& State, const FCatFishingFightParams& Params, const double InitialDistanceMeters,
		const double FishStaminaInitial, FRandomStream& Random)
	{
		(void)Params;
		State = FCatFishingFightState();
		State.DistanceMeters = FMath::Max(0.0, InitialDistanceMeters);
		State.LineMeters = State.DistanceMeters;
		State.FishStamina = FishStaminaInitial;
		State.FishStaminaMax = FishStaminaInitial;
		State.SwimState = ECatFishSwimState::Outward;
		State.SegmentRemainingSeconds = Random.FRandRange(Rules::OutwardSegmentMinSeconds, Rules::OutwardSegmentMaxSeconds);
	}

	// 推进流程：
	// 1. 已有终局或 dt 非法直接返回；段已到期先 roll 新段（方向、段长、可能的垂死挣扎）。
	// 2. 推进挣扎计时：前摇期不加成，生效期鱼力量 ×1.5。
	// 3. 按"鱼状态 × 猫操作"分支：向内+拖 D/L 各 -3×系数、猫体力 -= 鱼力×0.15；向内+松/不动 D -1×系数；
	//    向外+拖查判定表：①②③三个瞬时出口直接写终局（③顺带 D=0），④僵持逐秒扣鱼线耐久/鱼体力/猫体力；
	//    向外+松：L 未到顶时鱼带动 D/L 各 +2.5×系数、猫体力 +1.5，L 到顶后无法继续带线（旧规格模型，见 D-17）。
	// 4. 夹取：D ≥ 0，L ≤ L_max 且 L ≥ D；段剩余时间扣掉 dt。
	// 5. 终局优先级：鱼体力 ≤0 翻肚（D=0）→ 猫体力 ≤0 拖下水 → 鱼线耐久 ≤0 断线；都没有时 D ≤ 近岸距离记为遛到岸边。
	//    猫体力和鱼线耐久用"当前值 + 本次增量"判断，所以模型不需要持有它们的真身。
	void Step(FCatFishingFightState& State, const FCatFishingFightParams& Params, const ECatFishingFightIntent Intent,
		const double DeltaSeconds, const FCatFishingFightResources& Resources, FRandomStream& Random,
		FCatFishingFightStepDelta& OutDelta)
	{
		OutDelta = FCatFishingFightStepDelta();
		if (State.Outcome != ECatFishingFightOutcome::None || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0)
		{
			return;
		}
		if (State.SegmentRemainingSeconds <= 0.0 || State.SwimState == ECatFishSwimState::None)
		{
			RollNextSegment(State, Params, Random);
		}

		bool bStruggleActive = false;
		if (State.DeathStruggleWindupRemainingSeconds > 0.0)
		{
			State.DeathStruggleWindupRemainingSeconds = FMath::Max(0.0, State.DeathStruggleWindupRemainingSeconds - DeltaSeconds);
		}
		else if (State.DeathStruggleActiveRemainingSeconds > 0.0)
		{
			bStruggleActive = true;
			State.DeathStruggleActiveRemainingSeconds = FMath::Max(0.0, State.DeathStruggleActiveRemainingSeconds - DeltaSeconds);
		}
		const double FishStrength = Params.FishStrengthBase * (bStruggleActive ? Rules::DeathStruggleStrengthMultiplier : 1.0);
		const double Speed = Params.FishSpeedCoefficient;

		if (State.SwimState == ECatFishSwimState::Inward)
		{
			if (Intent == ECatFishingFightIntent::Pull)
			{
				const double Reel = Rules::InwardPullMetersPerSecond * Speed * DeltaSeconds;
				State.DistanceMeters -= Reel;
				State.LineMeters -= Reel;
				OutDelta.CatStaminaDelta -= FishStrength * Rules::InwardPullCatStaminaPerFishStrength * DeltaSeconds;
			}
			else
			{
				State.DistanceMeters -= Rules::InwardReleaseMetersPerSecond * Speed * DeltaSeconds;
			}
		}
		else if (Intent == ECatFishingFightIntent::Pull)
		{
			switch (JudgeOutwardPull(Params.CatStrength, FishStrength, Params.RodStrength))
			{
			case ECatFishingPullVerdict::RodBroken: // 旧序列化值按新语义收敛
			case ECatFishingPullVerdict::LineBroken:
				State.Outcome = ECatFishingFightOutcome::LineBroken;
				return;
			case ECatFishingPullVerdict::CatDraggedIn:
				State.Outcome = ECatFishingFightOutcome::CatDraggedIn;
				return;
			case ECatFishingPullVerdict::Overpowered:
				State.DistanceMeters = 0.0;
				State.Outcome = ECatFishingFightOutcome::Overpowered;
				return;
			case ECatFishingPullVerdict::Stalemate:
				OutDelta.RodDurabilityCost += FishStrength * Rules::StalemateRodWearPerFishStrength * DeltaSeconds;
				State.FishStamina -= Params.CatStrength * Rules::StalemateFishStaminaPerCatStrength * DeltaSeconds;
				OutDelta.CatStaminaDelta -= FishStrength * Rules::StalemateCatStaminaPerFishStrength * DeltaSeconds;
				break;
			}
		}
		else if (State.LineMeters < Params.LineMaxMeters)
		{
			const double Run = Rules::OutwardReleaseMetersPerSecond * Speed * DeltaSeconds;
			State.DistanceMeters += Run;
			State.LineMeters += Run;
			OutDelta.CatStaminaDelta += Rules::OutwardReleaseCatStaminaRegenPerSecond * DeltaSeconds;
		}

		// 先把 D 压回非负，再让 L 落在 [D, L_max]，最后 D 不能超过被 L_max 压下来的 L；三步顺序保证 0 ≤ D ≤ L ≤ L_max。
		State.DistanceMeters = FMath::Max(0.0, State.DistanceMeters);
		State.LineMeters = FMath::Clamp(FMath::Max(State.LineMeters, State.DistanceMeters), 0.0, Params.LineMaxMeters);
		State.DistanceMeters = FMath::Min(State.DistanceMeters, State.LineMeters);
		State.SegmentRemainingSeconds -= DeltaSeconds;

		if (State.FishStamina <= 0.0)
		{
			State.FishStamina = 0.0;
			State.DistanceMeters = 0.0;
			State.Outcome = ECatFishingFightOutcome::FishExhausted;
		}
		else if (Resources.CatStamina + OutDelta.CatStaminaDelta <= 0.0)
		{
			State.Outcome = ECatFishingFightOutcome::CatDraggedIn;
		}
		else if (Resources.RodDurability - OutDelta.RodDurabilityCost <= 0.0)
		{
			State.Outcome = ECatFishingFightOutcome::LineBroken;
		}
		else if (State.DistanceMeters <= Params.NearShoreDistanceMeters)
		{
			State.Outcome = ECatFishingFightOutcome::ReeledToShore;
		}
	}
}
