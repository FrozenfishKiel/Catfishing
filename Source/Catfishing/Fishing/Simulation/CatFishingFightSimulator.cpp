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
		&& FMath::IsFinite(RodStrength) && RodStrength > 0.0 // 钓组静态承载强度，决定瞬间断线判定
		&& FMath::IsFinite(CatStaminaMaximum) && CatStaminaMaximum > 0.0 // 猫体力上限，松线喘息回复不能超过它
		&& IsFiniteNonNegative(InwardPullCatDrainPerFishStrength) // 向内游+拖：猫体力消耗系数
		&& IsFiniteNonNegative(InwardPullFishDrainPerCatStrength) // 向内游+拖：鱼体力消耗系数
		&& IsFiniteNonNegative(StalemateRodWearPerFishStrength) // 僵持：本场鱼线负载磨损系数
		&& IsFiniteNonNegative(StalemateFishDrainPerCatStrength) // 僵持：鱼体力消耗系数
		&& IsFiniteNonNegative(StalemateCatDrainPerFishStrength) // 僵持：猫体力消耗系数
		&& IsFiniteNonNegative(SlackStaminaRegenPerSecond) // 松开线杯时猫体力回复速率
		&& IsFiniteNonNegative(StruggleHoldRodWearPerSecond) // 鱼挣扎但猫不拖不放时的竿基础磨损速率（可配置为 0）
		&& FMath::IsFinite(TautRodWearMultiplier) && TautRodWearMultiplier >= 1.0 // 线绷紧时的磨损放大倍率，至少 1 倍
		&& FMath::IsFinite(OverpowerStrengthRatio) && OverpowerStrengthRatio >= 1.0 // 碾压判定的力量比阈值，至少 1 倍
		&& FMath::IsFinite(ReelSpeedCentimetersPerSecond) && ReelSpeedCentimetersPerSecond > 0.0 // 拖线收线速度
		&& IsFiniteNonNegative(FishCalmSpeedCentimetersPerSecond) // 鱼平静时自主靠近速度（允许为 0）
		&& FMath::IsFinite(FishStruggleSpeedCentimetersPerSecond) && FishStruggleSpeedCentimetersPerSecond > 0.0 // 鱼挣扎外游速度
		&& FMath::IsFinite(FishExhaustionThreshold)
		&& FishExhaustionThreshold >= 0.0 && FishExhaustionThreshold <= 1.0
		&& FMath::IsFinite(StrongConfrontationAlignmentThreshold)
		&& StrongConfrontationAlignmentThreshold > 0.0 && StrongConfrontationAlignmentThreshold <= 1.0
		&& FMath::IsFinite(StrongConfrontationConfirmationSeconds)
		&& StrongConfrontationConfirmationSeconds >= 0.0 && StrongConfrontationConfirmationSeconds <= 2.0
		&& FMath::IsFinite(AngleStrengthExponent) && AngleStrengthExponent >= 0.1 && AngleStrengthExponent <= 4.0
		&& FMath::IsFinite(TensionResponseRangeCentimeters) && TensionResponseRangeCentimeters > 0.0
		&& FMath::IsFinite(MaximumLineLengthCentimeters) && MaximumLineLengthCentimeters > 0.0 // 线长上限 L_max
		&& FMath::IsFinite(RodDurability) && RodDurability > 0.0 // 本场鱼线耐久总量（新会话重置）
		&& IsFiniteNonNegative(EscapeSlackCentimeters) // 判定脱钩前允许超出 L_max 的松弛裕度
		&& IsFiniteNonNegative(NearShoreLineLengthCentimeters); // 触发近岸阶段的线长阈值
}

// [FishLogic 3/5：夹角受力]
// 单步流程：游向决定径向/横向位移；鱼在线方向的有效力量=max(cos夹角,0)^Exponent。
// 达到角度阈值并持续确认时间后，才按 ①瞬断 ②拖下水 ③碾压 ④僵持裁决重大强对抗；
// 体力/磨损始终按连续有效力量缩放。之后按 鱼体力 → 猫体力 → 鱼线耐久 检查归零。
FCatFightStepResult FCatFishingFightSimulator::Step(const FCatFightSimulationConfig& Config,
	const FCatFightSimulationState& State, const FVector& RodTipWorldPosition, const FVector& DesiredFishDirection)
{
	// 默认返回 bSucceeded=false 的空结果；下面任一输入非法都提前返回它，调用方据此判断本步作废。
	FCatFightStepResult Result;
	if (!Config.IsValid() || !FMath::IsFinite(State.CatStamina) || State.CatStamina < 0.0
		|| !FMath::IsFinite(State.FishStamina) || State.FishStamina < 0.0
		|| !FMath::IsFinite(State.LineLengthCentimeters) || State.LineLengthCentimeters < 0.0
		|| !FMath::IsFinite(State.AbsoluteRodWear) || State.AbsoluteRodWear < 0.0
		|| !FMath::IsFinite(State.StrongConfrontationBuildUpSeconds)
		|| State.StrongConfrontationBuildUpSeconds < 0.0
		|| !IsFiniteFightVector(State.FishWorldPosition) || !IsFiniteFightVector(RodTipWorldPosition)
		|| !IsFiniteFightVector(DesiredFishDirection) || DesiredFishDirection.IsNearlyZero())
	{
		return Result;
	}

	const double Dt = Config.FixedStepSeconds; // 本步时长，所有速率型系数都乘以它折算成本步增量
	const FVector FromRod = State.FishWorldPosition - RodTipWorldPosition;
	const FVector SafeOutward = FromRod.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector::ForwardVector);
	const FVector HorizontalOutward = FVector(FromRod.X, FromRod.Y, 0.0)
		.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector::ForwardVector);
	const FVector FishDirection = FVector(DesiredFishDirection.X, DesiredFishDirection.Y, 0.0)
		.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, HorizontalOutward);
	const double Distance0 = FVector::Distance(RodTipWorldPosition, State.FishWorldPosition); // 本步开始时竿尖到鱼的距离
	const double VerticalDistance = FMath::Abs(State.FishWorldPosition.Z - RodTipWorldPosition.Z);
	const double Alignment = FMath::Clamp(FVector::DotProduct(FishDirection, HorizontalOutward), -1.0, 1.0);
	const double OutwardLoad = FMath::Pow(FMath::Max(0.0, Alignment), Config.AngleStrengthExponent);
	const bool bStruggling = State.MotionIntent == ECatFishMotionIntent::StrugglingOutward; // StateTree 的高层发力意图，只决定游速/重大判定资格
	const bool bPulling = State.CatAction == ECatFightCatAction::Pull; // 玩家本步是否按住左键拖线
	const bool bSlacking = State.CatAction == ECatFightCatAction::Slack; // 玩家本步是否按住右键松开线杯
	const double SwimSpeed = bStruggling ? Config.FishStruggleSpeedCentimetersPerSecond
		: Config.FishCalmSpeedCentimetersPerSecond;
	const double FishRadialSpeed = FVector::DotProduct(FishDirection * SwimSpeed, SafeOutward);
	const FVector RightTangent(-HorizontalOutward.Y, HorizontalOutward.X, 0.0);
	const double FishTangentialSpeed = FVector::DotProduct(FishDirection * SwimSpeed, RightTangent);
	// 鱼线的三个事实分开记账：L_paid=已放出长度、D=直线距离、Slack=max(L_paid-D,0)。
	// 左键主动减少 L_paid；右键只解除线端约束，让鱼外游时被动带出线；无输入时 L_paid 固定。
	const double PaidOutLine0 = FMath::Clamp(State.LineLengthCentimeters, 0.0,
		Config.MaximumLineLengthCentimeters);
	const double Slack0 = FMath::Max(0.0, PaidOutLine0 - Distance0);
	const double RequestedReelStep = bPulling ? Config.ReelSpeedCentimetersPerSecond * Dt : 0.0;
	const double PotentialOutwardStep = FMath::Max(0.0, FishRadialSpeed * Dt);
	const double FreeSwimDistance1 = FMath::Max(VerticalDistance, Distance0 + FishRadialSpeed * Dt);
	// 收线和鱼外游都会吃掉余线；只有本步会碰到线端时才形成资源对抗。
	const bool bPullReachesTaut = bPulling
		&& RequestedReelStep + PotentialOutwardStep >= Slack0 - UE_DOUBLE_KINDA_SMALL_NUMBER;
	const bool bHoldReachesTaut = !bPulling && !bSlacking
		&& PotentialOutwardStep > Slack0 + UE_DOUBLE_KINDA_SMALL_NUMBER;
	// 松开线杯后，在 L_max 以内鱼不会受到线端限制；只有整根线被鱼带完后继续外冲才重新形成张力。
	const bool bSlackBlockedAtMaximum = bSlacking
		&& FreeSwimDistance1 > Config.MaximumLineLengthCentimeters + UE_DOUBLE_KINDA_SMALL_NUMBER;
	// 是否碰到线端只看真实游向/收线器状态，不看 StateTree 状态名：平静状态随机到向外方向也会形成张力。
	const bool bLineRestraining = bPullReachesTaut || bHoldReachesTaut || bSlackBlockedAtMaximum;
	const bool bAtMaximumLine = PaidOutLine0 >= Config.MaximumLineLengthCentimeters
		- UE_DOUBLE_KINDA_SMALL_NUMBER;
	// 普通“没有松开线杯而绷紧”会持续消耗三方资源，但不会自动触发落水/瞬断；重大判定仍要求玩家硬拉，
	// 或真的已经放到整根线的末端，避免一根短余线在任何距离都立刻判重大失败。
	const bool bStrongConfrontationCandidate = bStruggling && bLineRestraining
		&& (bPulling || bAtMaximumLine || bSlackBlockedAtMaximum)
		&& OutwardLoad >= Config.StrongConfrontationAlignmentThreshold;
	const double StrongConfrontationBuildUp = bStrongConfrontationCandidate
		? State.StrongConfrontationBuildUpSeconds + Dt : 0.0;
	const bool bStrongConfrontation = bStrongConfrontationCandidate
		&& StrongConfrontationBuildUp + UE_DOUBLE_KINDA_SMALL_NUMBER
			>= Config.StrongConfrontationConfirmationSeconds;

	double DistanceDelta = 0.0; // 本步竿尖-鱼距离的变化量，负值=靠近、正值=远离
	double CatDrain = 0.0; // 本步猫体力变化，正值=消耗、负值=回复（下面会按各分支重新赋值）
	double FishDrain = 0.0; // 本步鱼体力消耗
	double RodWearDelta = 0.0; // 本步鱼线负载新增磨损量（沿用旧字段名，累加到 AbsoluteRodWear）
	ECatFightStepOutcome Instant = ECatFightStepOutcome::None; // 向外游分支里可能立即产生的终局（瞬断/拖下水/碾压）
	// 有余线时先免费收掉余线；碰到线端后的剩余收线量才按鱼的向外力量投影衰减。
	double EffectiveReelStep = 0.0;
	if (bPulling)
	{
		const double FreeSlackReel = FMath::Min(RequestedReelStep, Slack0);
		const double LoadedReel = RequestedReelStep - FreeSlackReel;
		EffectiveReelStep = FreeSlackReel + LoadedReel * (1.0 - OutwardLoad);
	}

	if (!bStruggling)
	{
		// 平静期仍按真实游向运动；体力概率会让高体力鱼更多慢速向外、低体力鱼更多向内。
		if (bPulling)
		{
			DistanceDelta = -EffectiveReelStep; // 平静阶段按收线速度拉近；余线由同一笔 L_paid 变化消费。
			// 只有鱼游向中真正朝外的力量分量与收线对抗；朝内/横向时不会凭空制造正面拉力。
			CatDrain = Config.FishStrength * OutwardLoad * Config.InwardPullCatDrainPerFishStrength * Dt;
			FishDrain = Config.CatStrength * OutwardLoad * Config.InwardPullFishDrainPerCatStrength * Dt;
		}
		else
		{
			DistanceDelta = FishRadialSpeed * Dt;
			if (bLineRestraining)
			{
				// 即使 StateTree 叫“平静”，只要实际游向正在把线绷紧，三方就必须承担连续张力。
				RodWearDelta = (Config.FishStrength * Config.StalemateRodWearPerFishStrength
					+ Config.StruggleHoldRodWearPerSecond) * OutwardLoad * Dt
					* Config.TautRodWearMultiplier;
				FishDrain = Config.CatStrength * OutwardLoad
					* Config.StalemateFishDrainPerCatStrength * Dt;
				CatDrain = Config.FishStrength * OutwardLoad
					* Config.StalemateCatDrainPerFishStrength * Dt;
			}
		}
	}
	else if (bStrongConfrontation)
	{
		// 先保留鱼本步试图向外游的距离；后面的线长约束会把实际 D 截住，并把超出量记为 Tension。
		DistanceDelta = FishRadialSpeed * Dt - EffectiveReelStep;
		// 只有游向与鱼线足够同向时才进入强对抗；重大结局继续用鱼种基础力量，避免随机夹角一帧跳变力量档位。
		// 取等从严：≤ / ≥ 都归入更严厉的分支。
		if (Config.RodStrength <= FMath::Min(Config.CatStrength, Config.FishStrength))
		{
			// 判定①：钓组承载能力不足 → 鱼线瞬间断裂；鱼竿本体不损坏。
			Instant = ECatFightStepOutcome::LineBroken;
			Result.LineBreakCause = ECatFightLineBreakCause::StrengthOverload;
		}
		else if (Config.FishStrength >= Config.CatStrength)
		{
			// 判定②：竿没断，但鱼力不弱于猫力 → 猫被拖下水。
			Instant = ECatFightStepOutcome::DraggedIntoWater;
		}
		else if (Config.CatStrength >= Config.FishStrength * Config.OverpowerStrengthRatio)
		{
			// 判定③绝对碾压：猫力达到鱼力的指定倍数以上 → D 直接归零，鱼立刻力竭侧翻，无消耗。
			Instant = ECatFightStepOutcome::Overpowered;
			DistanceDelta = -Distance0;
		}
		else
		{
			// 判定④：双方僵持，进入消耗战。
			Result.bStalemate = true;
			// 力量消耗和磨损连续乘夹角投影；正对外冲=旧公式，斜向冲会按 cos(夹角) 衰减。
			RodWearDelta = Config.FishStrength * OutwardLoad * Config.StalemateRodWearPerFishStrength * Dt
				* Config.TautRodWearMultiplier; // 此分支已经确认鱼线形成约束，直接使用高张力倍率。
			FishDrain = Config.CatStrength * OutwardLoad * Config.StalemateFishDrainPerCatStrength * Dt;
			CatDrain = Config.FishStrength * OutwardLoad * Config.StalemateCatDrainPerFishStrength * Dt;
		}
	}
	else if (bLineRestraining)
	{
		// 鱼虽然处于挣扎阶段，但当前在横切/回头，尚未形成强对抗。左键可以趁角度窗口收线；
		// 若已有部分向外分量，收线速度、体力与磨损都按同一个连续投影比例过渡。
		DistanceDelta = FishRadialSpeed * Dt - EffectiveReelStep;
		RodWearDelta = Config.FishStrength * OutwardLoad * Config.StalemateRodWearPerFishStrength * Dt
			* Config.TautRodWearMultiplier;
		if (!bPulling)
		{
			RodWearDelta += Config.StruggleHoldRodWearPerSecond * OutwardLoad * Dt
				* Config.TautRodWearMultiplier;
		}
		FishDrain = Config.CatStrength * OutwardLoad * Config.StalemateFishDrainPerCatStrength * Dt;
		CatDrain = Config.FishStrength * OutwardLoad * Config.StalemateCatDrainPerFishStrength * Dt;
	}
	else
	{
		// 鱼在挣扎但鱼线未形成对抗：按实际方向自由游，可能向外、横切或假动作向内。
		DistanceDelta = FishRadialSpeed * Dt;
		if (bSlacking)
		{
			// 松开线杯让鱼自由带线：猫借机喘息，回复量不能超过体力上限。
			const double Regen = Config.SlackStaminaRegenPerSecond * Dt;
			const double Capped = FMath::Min(Config.CatStaminaMaximum, State.CatStamina + Regen);
			CatDrain = -(Capped - State.CatStamina); // 负数代表体力回复
		}
		// 有余线时不产生张力，也不凭空磨损鱼竿；鱼先消费余线，碰到线端的下一步才进入上面的资源交换。
	}

	// [FishLogic 3/5：资源结算]
	// 体力消耗不能透支：即使公式算出的消耗量更大，也只扣到 0 为止。
	CatDrain = FMath::Min(CatDrain, State.CatStamina);
	FishDrain = FMath::Min(FishDrain, State.FishStamina);
	// 旧调试 HUD 以整数百分比表现鱼体力。若服务器还残留 0.x，玩家可能看到“0% 却不翻肚”；
	// 因此把结算后落入阈值的尾数一次性吸附为真正的 0，让显示、玩法和阶段切换使用同一事实。
	if (State.FishStamina - FishDrain <= Config.FishExhaustionThreshold)
	{
		FishDrain = State.FishStamina;
	}

	// 左键主动收短 L_paid；右键本身不吐线，只在鱼确实向外游到现有线端之外时让 L_paid 跟随增长。
	// 不按键时 L_paid 固定；若鱼想去的距离超过它，实际 D 被截在线端，超出量成为张力。
	const double UnconstrainedDistance1 = FMath::Max(VerticalDistance, Distance0 + DistanceDelta);
	double LineLength = PaidOutLine0;
	if (bPulling && Instant != ECatFightStepOutcome::Overpowered)
	{
		LineLength -= EffectiveReelStep;
	}
	else if (bSlacking)
	{
		LineLength = FMath::Max(LineLength,
			FMath::Min(UnconstrainedDistance1, Config.MaximumLineLengthCentimeters));
	}
	// 鱼始终留在当前水面 Z；线不可能被收得比竿尖到水面的垂直落差还短。
	LineLength = FMath::Clamp(LineLength, VerticalDistance, Config.MaximumLineLengthCentimeters);
	const double Tension = FMath::Max(0.0, UnconstrainedDistance1 - LineLength);
	double Distance1 = FMath::Max(VerticalDistance, FMath::Min(UnconstrainedDistance1, LineLength));
	Distance1 = FMath::Clamp(Distance1, 0.0, Config.MaximumLineLengthCentimeters);

	// 把本步计算结果写入返回值，供 Runner/Session 应用到实际状态。
	Result.CatStaminaDrain = CatDrain;
	Result.FishStaminaDrain = FishDrain;
	Result.LineLengthCentimeters = LineLength;
	Result.AbsoluteRodWear = State.AbsoluteRodWear + RodWearDelta; // 磨损是累加量，不会自然恢复
	Result.TensionCentimeters = Tension;
	Result.StraightLineDistanceCentimeters = Distance1;
	Result.SlackLineLengthCentimeters = FMath::Max(0.0, LineLength - Distance1);
	Result.NormalizedTension = FMath::Clamp(Tension / Config.TensionResponseRangeCentimeters, 0.0, 1.0);
	Result.bLineTaut = Result.SlackLineLengthCentimeters <= UE_DOUBLE_KINDA_SMALL_NUMBER;
	Result.FishLineAlignment = Alignment;
	Result.NormalizedLineLoad = OutwardLoad;
	Result.StrongConfrontationBuildUpSeconds = StrongConfrontationBuildUp;
	Result.bStrongConfrontation = bStrongConfrontation;
	// 横向速度转换为绕竿尖的角位移：旋转鱼线方向而不凭空增加鱼距，因此鱼能绕圈且不会因切线移动偷偷超线长。
	const double HorizontalRadius = FVector::Dist2D(State.FishWorldPosition, RodTipWorldPosition);
	const double AngularDelta = HorizontalRadius > 1.0
		? FMath::Clamp(FishTangentialSpeed * Dt / HorizontalRadius, -PI, PI) : 0.0;
	const FVector RotatedOutward = HorizontalOutward.RotateAngleAxis(
		FMath::RadiansToDegrees(AngularDelta), FVector::UpVector);
	const double HorizontalDistance1 = FMath::Sqrt(FMath::Max(0.0,
		FMath::Square(Distance1) - FMath::Square(VerticalDistance)));
	Result.ProposedFishWorldPosition = RodTipWorldPosition + RotatedOutward * HorizontalDistance1;
	Result.ProposedFishWorldPosition.Z = State.FishWorldPosition.Z;
	if (Instant == ECatFightStepOutcome::Overpowered)
	{
		Result.ProposedFishWorldPosition = RodTipWorldPosition;
	}

	// 归零优先级判定：先看本步是否已经产生瞬时终局，否则按 鱼体力 → 猫体力 → 鱼线耐久 → 脱钩 的顺序检查。
	const double CatRemaining = State.CatStamina - CatDrain;
	const double FishRemaining = State.FishStamina - FishDrain;
	if (Instant != ECatFightStepOutcome::None)
	{
		// 向外游分支已经产生了瞬时终局（瞬断/拖下水/碾压），优先级最高，直接采用。
		Result.Outcome = Instant;
	}
	else if (FishRemaining <= UE_DOUBLE_SMALL_NUMBER)
	{
		// 鱼体力先耗尽：翻肚，可以被抄或继续收至竿尖水面投影。
		Result.Outcome = ECatFightStepOutcome::FishExhausted;
	}
	else if (CatRemaining <= UE_DOUBLE_SMALL_NUMBER)
	{
		// 猫体力耗尽（鱼还没耗尽）：猫力竭，视为被拖下水一类的失败。
		Result.Outcome = ECatFightStepOutcome::CatStaminaExhausted;
	}
	else if (Result.AbsoluteRodWear >= Config.RodDurability)
	{
		// 本场累计负载达到鱼线耐久上限：断线。它只结束本次会话，不写坏装备或场景鱼竿。
		Result.LineBreakCause = ECatFightLineBreakCause::DurabilityDepleted;
		Result.Outcome = ECatFightStepOutcome::LineBroken;
	}
	else if (UnconstrainedDistance1 > Config.MaximumLineLengthCentimeters + Config.EscapeSlackCentimeters)
	{
		// 距离超出线长上限加松弛裕度：数值异常/脱钩，正常规则下不应触发。
		Result.Outcome = ECatFightStepOutcome::Escaped;
	}

	Result.bSucceeded = true; // 标记本步计算成功，调用方可以放心应用 Result 里的增量
	return Result;
}
