#include "Fishing/Simulation/CatFishingFightSimulator.h"

namespace
{
	// 三个分量都必须是有限数（非 NaN/Inf），否则视为脏数据，Step 会直接拒绝本次输入。
	bool IsFiniteFightVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	// 用于校验"允许为 0 但不能为负、不能非法"的系数字段（比如磨损速率可以配置成 0 表示不磨）。
	bool IsFiniteNonNegative(const double Value)
	{
		return FMath::IsFinite(Value) && Value >= 0.0;
	}
}

// 配置合法性一次性校验：Step() 每步都会先调用它，任何一项不满足就整份配置视为无效（Step 直接返回空结果）。
bool FCatFightSimulationConfig::IsValid() const
{
	return FMath::IsFinite(FixedStepSeconds) && FixedStepSeconds > 0.0 // 步长必须严格为正，否则无法积分距离
		&& FMath::IsFinite(CatStrength) && CatStrength > 0.0 // 猫（钓手）力量，用于比力量判定和消耗系数
		&& FMath::IsFinite(FishStrength) && FishStrength > 0.0 // 鱼力量（已按性格模板折减过）
		&& FMath::IsFinite(RodStrength) && RodStrength > 0.0 // 竿的静态强度，决定瞬断判定
		&& FMath::IsFinite(CatStaminaMaximum) && CatStaminaMaximum > 0.0 // 猫体力上限，放线回体力不能超过它
		&& IsFiniteNonNegative(InwardPullCatDrainPerFishStrength) // 向内游+拖：猫体力消耗系数
		&& IsFiniteNonNegative(InwardPullFishDrainPerCatStrength) // 向内游+拖：鱼体力消耗系数
		&& IsFiniteNonNegative(StalemateRodWearPerFishStrength) // 僵持：竿耐久磨损系数
		&& IsFiniteNonNegative(StalemateFishDrainPerCatStrength) // 僵持：鱼体力消耗系数
		&& IsFiniteNonNegative(StalemateCatDrainPerFishStrength) // 僵持：猫体力消耗系数
		&& IsFiniteNonNegative(SlackStaminaRegenPerSecond) // 放线时猫体力回复速率
		&& IsFiniteNonNegative(StruggleHoldRodWearPerSecond) // 鱼挣扎但猫不拖不放时的竿基础磨损速率（可配置为 0）
		&& FMath::IsFinite(TautRodWearMultiplier) && TautRodWearMultiplier >= 1.0 // 线绷紧时的磨损放大倍率，至少 1 倍
		&& FMath::IsFinite(OverpowerStrengthRatio) && OverpowerStrengthRatio >= 1.0 // 碾压判定的力量比阈值，至少 1 倍
		&& FMath::IsFinite(ReelSpeedCentimetersPerSecond) && ReelSpeedCentimetersPerSecond > 0.0 // 拖线收线速度
		&& IsFiniteNonNegative(FishCalmSpeedCentimetersPerSecond) // 鱼平静时自主靠近速度（允许为 0）
		&& FMath::IsFinite(FishStruggleSpeedCentimetersPerSecond) && FishStruggleSpeedCentimetersPerSecond > 0.0 // 鱼挣扎外游速度
		&& FMath::IsFinite(MaximumLineLengthCentimeters) && MaximumLineLengthCentimeters > 0.0 // 线长上限 L_max
		&& FMath::IsFinite(RodDurability) && RodDurability > 0.0 // 竿耐久总量
		&& IsFiniteNonNegative(EscapeSlackCentimeters) // 判定脱钩前允许超出 L_max 的松弛裕度
		&& IsFiniteNonNegative(NearShoreLineLengthCentimeters); // 触发近岸阶段的线长阈值
}

// 单步流程（规格 4.3 判定表 + 4.4 消耗战）：
//   向内游：拖 → 大幅拉近并按鱼力扣猫体力；放/不动 → 鱼自己小幅靠近，无消耗。
//   向外游：拖（或线已放到 L_max 而绷紧）→ 按 ①瞬断 ②拖下水 ③碾压 ④僵持 顺序判定；
//           放线且 L<L_max → 鱼外游、线放出、猫回体力；不动且 L<L_max → 鱼外游、线自由放出、不回体力（临时口径，TODO）。
//   之后按 鱼体力 → 猫体力 → 竿耐久 的优先级检查归零。近岸判定不在此处（需要水域查询，由 Runner 负责）。
FCatFightStepResult FCatFishingFightSimulator::Step(const FCatFightSimulationConfig& Config,
	const FCatFightSimulationState& State, const FVector& RodTipWorldPosition, const FVector& OutwardDirection)
{
	// 默认返回 bSucceeded=false 的空结果；下面任一输入非法都提前返回它，调用方据此判断本步作废。
	FCatFightStepResult Result;
	if (!Config.IsValid() || !FMath::IsFinite(State.CatStamina) || State.CatStamina < 0.0
		|| !FMath::IsFinite(State.FishStamina) || State.FishStamina < 0.0
		|| !FMath::IsFinite(State.LineLengthCentimeters) || State.LineLengthCentimeters < 0.0
		|| !FMath::IsFinite(State.AbsoluteRodWear) || State.AbsoluteRodWear < 0.0
		|| !IsFiniteFightVector(State.FishWorldPosition) || !IsFiniteFightVector(RodTipWorldPosition)
		|| !IsFiniteFightVector(OutwardDirection))
	{
		return Result;
	}

	const double Dt = Config.FixedStepSeconds; // 本步时长，所有速率型系数都乘以它折算成本步增量
	const FVector SafeOutward = OutwardDirection.GetSafeNormal(); // 归一化外游方向，避免零向量导致的除零/NaN
	const double Distance0 = FVector::Distance(RodTipWorldPosition, State.FishWorldPosition); // 本步开始时竿尖到鱼的距离
	const bool bOutward = State.MotionIntent == ECatFishMotionIntent::StrugglingOutward; // 鱼本步是否处于挣扎外游意图
	const bool bPulling = State.CatAction == ECatFightCatAction::Pull; // 玩家本步是否按住左键拖线
	const bool bSlacking = State.CatAction == ECatFightCatAction::Slack; // 玩家本步是否按住右键放线
	// 线已全部放出且鱼顶在线端：右键失效，鱼的外游力直接压在竿上，按「拖」各分支判定。
	const bool bLineTaut = State.LineLengthCentimeters >= Config.MaximumLineLengthCentimeters - UE_DOUBLE_KINDA_SMALL_NUMBER
		&& Distance0 >= Config.MaximumLineLengthCentimeters - UE_DOUBLE_KINDA_SMALL_NUMBER;

	double DistanceDelta = 0.0; // 本步竿尖-鱼距离的变化量，负值=靠近、正值=远离
	double CatDrain = 0.0; // 本步猫体力变化，正值=消耗、负值=回复（下面会按各分支重新赋值）
	double FishDrain = 0.0; // 本步鱼体力消耗
	double RodWearDelta = 0.0; // 本步竿耐久新增磨损量（累加到 AbsoluteRodWear）
	ECatFightStepOutcome Instant = ECatFightStepOutcome::None; // 向外游分支里可能立即产生的终局（瞬断/拖下水/碾压）

	if (!bOutward)
	{
		// 鱼当前不挣扎（平静或向内游）。
		if (bPulling)
		{
			// 拖永远双方掉体力：顺从（向内游）用低档系数，挣扎僵持用高档系数。
			DistanceDelta = -Config.ReelSpeedCentimetersPerSecond * Dt; // 按收线速度把距离拉近
			CatDrain = Config.FishStrength * Config.InwardPullCatDrainPerFishStrength * Dt; // 鱼越强，猫拖线越费体力
			FishDrain = Config.CatStrength * Config.InwardPullFishDrainPerCatStrength * Dt; // 猫越强，鱼被拖动越费体力
		}
		else
		{
			// 猫不拖线：鱼自己小幅游近，无人消耗体力（临时口径）。
			DistanceDelta = -Config.FishCalmSpeedCentimetersPerSecond * Dt;
		}
	}
	else if (bPulling || bLineTaut)
	{
		// 鱼在挣扎外游，且（猫在拖线 或 线已放尽绷紧）——外游的力道会直接顶在竿/猫身上，按四级判定表处理。
		// 取等从严：≤ / ≥ 都归入更严厉的分支。
		if (Config.RodStrength <= FMath::Min(Config.CatStrength, Config.FishStrength))
		{
			// 判定①：竿强度撑不住双方中较弱的一方也不行 → 瞬间断竿。
			Instant = ECatFightStepOutcome::RodBroken;
		}
		else if (Config.FishStrength >= Config.CatStrength)
		{
			// 判定②：竿没断，但鱼力不弱于猫力 → 猫被拖下水。
			Instant = ECatFightStepOutcome::DraggedIntoWater;
		}
		else if (Config.CatStrength >= Config.FishStrength * Config.OverpowerStrengthRatio)
		{
			// 判定③绝对碾压：猫力达到鱼力的指定倍数以上 → D 直接归零，鱼被甩上岸，无消耗。
			Instant = ECatFightStepOutcome::Overpowered;
			DistanceDelta = -Distance0;
		}
		else
		{
			// 判定④：双方僵持，进入消耗战。
			Result.bStalemate = true;
			// 僵持角力磨损；线绷紧（放尽）时鱼的外游力全压在竿上，按 DA 的高张力倍率放大。
			RodWearDelta = Config.FishStrength * Config.StalemateRodWearPerFishStrength * Dt
				* (bLineTaut ? Config.TautRodWearMultiplier : 1.0); // 竿耐久消耗：鱼力越大磨损越快，线绷紧时再乘倍率
			FishDrain = Config.CatStrength * Config.StalemateFishDrainPerCatStrength * Dt; // 鱼体力消耗：猫力越大鱼掉体力越快
			CatDrain = Config.FishStrength * Config.StalemateCatDrainPerFishStrength * Dt; // 猫体力消耗：鱼力越大猫掉体力越快
		}
	}
	else
	{
		// 鱼在挣扎外游，但猫没拖线且线还没放尽——鱼可以自由外游，线随之放出。
		DistanceDelta = Config.FishStruggleSpeedCentimetersPerSecond * Dt;
		if (bSlacking)
		{
			// 猫主动放线：借机回体力，回复量不能超过体力上限。
			const double Regen = Config.SlackStaminaRegenPerSecond * Dt;
			const double Capped = FMath::Min(Config.CatStaminaMaximum, State.CatStamina + Regen);
			CatDrain = -(Capped - State.CatStamina); // 负数代表体力回复
		}
		else
		{
			// 鱼挣扎外游而猫既不拖也不放线：竿硬扛鱼的拉力，按鱼竿 DA 的基础磨损速率掉耐久（放线即可避免）。
			RodWearDelta = Config.StruggleHoldRodWearPerSecond * Dt;
		}
	}

	// 体力消耗不能透支：即使公式算出的消耗量更大，也只扣到 0 为止。
	CatDrain = FMath::Min(CatDrain, State.CatStamina);
	FishDrain = FMath::Min(FishDrain, State.FishStamina);

	// 按本步位移更新距离，夹在 [0, L_max] 之间，避免数值越界。
	double Distance1 = FMath::Clamp(Distance0 + DistanceDelta, 0.0, Config.MaximumLineLengthCentimeters);
	// 线长记账：拖回时线跟着鱼收；否则线只会放出到鱼所在距离，永不超过 L_max。
	double LineLength = bPulling
		? FMath::Min(State.LineLengthCentimeters, Distance1) // 拖线：线长不能比鱼距更长（跟着收）
		: FMath::Max(State.LineLengthCentimeters, Distance1); // 不拖：线长只会被鱼拉得更远，不会自己变短
	LineLength = FMath::Clamp(LineLength, 0.0, Config.MaximumLineLengthCentimeters);

	// 把本步计算结果写入返回值，供 Runner/Session 应用到实际状态。
	Result.CatStaminaDrain = CatDrain;
	Result.FishStaminaDrain = FishDrain;
	Result.LineLengthCentimeters = LineLength;
	Result.AbsoluteRodWear = State.AbsoluteRodWear + RodWearDelta; // 磨损是累加量，不会自然恢复
	Result.TensionCentimeters = FMath::Max(0.0, Distance1 - LineLength); // 张力＝鱼距超出已放线长的部分（绷紧才有张力）
	Result.ProposedFishWorldPosition = RodTipWorldPosition + SafeOutward * Distance1; // 按新距离沿外游方向重新摆放鱼的建议位置

	// 归零优先级判定（规格 4.4）：先看本步是否已经产生瞬时终局，否则按 鱼体力 → 猫体力 → 竿耐久 → 脱钩 的顺序检查。
	const double CatRemaining = State.CatStamina - CatDrain;
	const double FishRemaining = State.FishStamina - FishDrain;
	if (Instant != ECatFightStepOutcome::None)
	{
		// 向外游分支已经产生了瞬时终局（瞬断/拖下水/碾压），优先级最高，直接采用。
		Result.Outcome = Instant;
	}
	else if (FishRemaining <= UE_DOUBLE_SMALL_NUMBER)
	{
		// 鱼体力先耗尽：翻肚，可以被抄/拖上岸。
		Result.Outcome = ECatFightStepOutcome::FishExhausted;
	}
	else if (CatRemaining <= UE_DOUBLE_SMALL_NUMBER)
	{
		// 猫体力耗尽（鱼还没耗尽）：猫力竭，视为被拖下水一类的失败。
		Result.Outcome = ECatFightStepOutcome::CatStaminaExhausted;
	}
	else if (Result.AbsoluteRodWear >= Config.RodDurability)
	{
		// 累计磨损达到竿耐久上限：断竿（不同于判定①的瞬断，这是磨损型断竿）。
		Result.Outcome = ECatFightStepOutcome::RodBroken;
	}
	else if (Distance1 > Config.MaximumLineLengthCentimeters + Config.EscapeSlackCentimeters)
	{
		// 距离超出线长上限加松弛裕度：数值异常/脱钩，正常规则下不应触发。
		Result.Outcome = ECatFightStepOutcome::Escaped;
	}

	Result.bSucceeded = true; // 标记本步计算成功，调用方可以放心应用 Result 里的增量
	return Result;
}
