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
		&& FMath::IsFinite(BaseDrainMultiplier) && BaseDrainMultiplier > 0.0 // 平静/顺从期体力消耗倍率
		&& FMath::IsFinite(StruggleDrainMultiplier) && StruggleDrainMultiplier > BaseDrainMultiplier // 挣扎必须更费力
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

// [FishLogic 3/5：自由游动与鱼线约束]
// 单步流程：先按游向/游速生成不受竿尖坐标系影响的水平候选位置，再由 L_paid 限制最终落点。
// 鱼线夹角只用于计算负载/资源交换，不再反向决定鱼是否真的产生位移。
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
	const FVector HorizontalFromRod(FromRod.X, FromRod.Y, 0.0);
	const double HorizontalRadius0 = HorizontalFromRod.Size2D();
	// 水平半径过小时“水平向外”没有几何定义，不得用世界 Forward 伪造受力方向。
	const bool bHasHorizontalRadialBasis = HorizontalRadius0 > 1.0;
	const FVector HorizontalOutward = bHasHorizontalRadialBasis
		? HorizontalFromRod / HorizontalRadius0 : FVector::ZeroVector;
	const FVector HorizontalDesiredDirection(DesiredFishDirection.X, DesiredFishDirection.Y, 0.0);
	if (HorizontalDesiredDirection.IsNearlyZero())
	{
		return Result;
	}
	const FVector FishDirection = HorizontalDesiredDirection.GetSafeNormal();
	const double Distance0 = FVector::Distance(RodTipWorldPosition, State.FishWorldPosition); // 本步开始时竿尖到鱼的距离
	const double VerticalDistance = FMath::Abs(State.FishWorldPosition.Z - RodTipWorldPosition.Z);
	const bool bStruggling = State.MotionIntent == ECatFishMotionIntent::StrugglingOutward; // StateTree 的高层发力意图，只决定游速/重大判定资格
	const bool bPulling = State.CatAction == ECatFightCatAction::Pull; // 玩家本步是否按住左键拖线
	const bool bSlacking = State.CatAction == ECatFightCatAction::Slack; // 玩家本步是否按住右键松开线杯
	const double SwimSpeed = bStruggling ? Config.FishStruggleSpeedCentimetersPerSecond
		: Config.FishCalmSpeedCentimetersPerSecond;
	// 鱼先在水平面按自身速度游出候选点；这个位移不因为鱼恰好在竿尖 XY 投影下就消失。
	FVector FreeFishPosition = State.FishWorldPosition + FishDirection * SwimSpeed * Dt;
	FreeFishPosition.Z = State.FishWorldPosition.Z;
	const double FreeSwimDistance1 = FVector::Distance(RodTipWorldPosition, FreeFishPosition);
	// 只有存在真实水平径向时才发布夹角负载；投影奇点本步负载为 0，离开奇点后下一步自然恢复。
	const double Alignment = bHasHorizontalRadialBasis
		? FMath::Clamp(FVector::DotProduct(FishDirection, HorizontalOutward), -1.0, 1.0) : 0.0;
	const double OutwardLoad = FMath::Pow(FMath::Max(0.0, Alignment), Config.AngleStrengthExponent);
	// 鱼线的三个事实分开记账：L_paid=已放出长度、D=直线距离、Slack=max(L_paid-D,0)。
	// 左键主动减少 L_paid；右键只解除线端约束，让鱼外游时被动带出线；无输入时 L_paid 固定。
	const double PaidOutLine0 = FMath::Clamp(State.LineLengthCentimeters, 0.0,
		Config.MaximumLineLengthCentimeters);
	const double Slack0 = FMath::Max(0.0, PaidOutLine0 - Distance0);
	const double RequestedReelStep = bPulling ? Config.ReelSpeedCentimetersPerSecond * Dt : 0.0;
	const double PotentialOutwardStep = FMath::Max(0.0, FreeSwimDistance1 - Distance0);
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

	double CatDrain = 0.0; // 本步猫体力变化，正值=消耗、负值=回复（下面会按各分支重新赋值）
	double FishDrain = 0.0; // 本步鱼体力消耗
	double RodWearDelta = 0.0; // 本步鱼线负载新增磨损量（沿用旧字段名，累加到 AbsoluteRodWear）
	ECatFightStepOutcome Instant = ECatFightStepOutcome::None; // 向外游分支里可能立即产生的终局（瞬断/拖下水/碾压）
	// 有余线时先免费收掉余线；碰到线端后的剩余输入只形成有限牵引，并按鱼的向外力量投影衰减。
	// 两部分必须分开：收余线不会移动鱼，带载牵引才会给鱼叠加一个朝竿尖投影的速度分量。
	double FreeSlackReelStep = 0.0;
	double EffectiveLoadedReelStep = 0.0;
	if (bPulling)
	{
		FreeSlackReelStep = FMath::Min(RequestedReelStep, Slack0);
		const double LoadedReelStep = RequestedReelStep - FreeSlackReelStep;
		EffectiveLoadedReelStep = LoadedReelStep * (1.0 - OutwardLoad);
	}

	if (!bStruggling)
	{
		// 平静期仍按真实游向运动；体力概率会让高体力鱼更多慢速向外、低体力鱼更多向内。
		if (bPulling)
		{
			// 收余线本身不作用到鱼；一旦本步进入带载牵引，猫与鱼都要做功。平静/顺从只使用较低档系数，
			// 不再让朝内或横向游动因 LineLoad=0 而把双方消耗一并清零。
			if (bPullReachesTaut)
			{
				CatDrain = Config.FishStrength * Config.InwardPullCatDrainPerFishStrength
					* Config.BaseDrainMultiplier * Dt;
				FishDrain = Config.CatStrength * Config.InwardPullFishDrainPerCatStrength
					* Config.BaseDrainMultiplier * Dt;
			}
		}
		else
		{
			if (bLineRestraining)
			{
				// 即使 StateTree 叫“平静”，只要实际游向正在把线绷紧，三方就必须承担连续张力。
				RodWearDelta = (Config.FishStrength * Config.StalemateRodWearPerFishStrength
					+ Config.StruggleHoldRodWearPerSecond) * OutwardLoad * Dt
					* Config.TautRodWearMultiplier;
				FishDrain = Config.CatStrength * OutwardLoad
					* Config.StalemateFishDrainPerCatStrength * Config.BaseDrainMultiplier * Dt;
				CatDrain = Config.FishStrength * OutwardLoad
					* Config.StalemateCatDrainPerFishStrength * Config.BaseDrainMultiplier * Dt;
			}
		}
	}
	else if (bStrongConfrontation)
	{
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
		}
		else
		{
			// 判定④：双方僵持，进入消耗战。
			Result.bStalemate = true;
			// 力量消耗和磨损连续乘夹角投影；正对外冲=旧公式，斜向冲会按 cos(夹角) 衰减。
			RodWearDelta = Config.FishStrength * OutwardLoad * Config.StalemateRodWearPerFishStrength * Dt
				* Config.TautRodWearMultiplier; // 此分支已经确认鱼线形成约束，直接使用高张力倍率。
			const double StaminaExchangeLoad = bPullReachesTaut ? 1.0 : OutwardLoad;
			FishDrain = Config.CatStrength * StaminaExchangeLoad * Config.StalemateFishDrainPerCatStrength
				* Config.StruggleDrainMultiplier * Dt;
			CatDrain = Config.FishStrength * StaminaExchangeLoad * Config.StalemateCatDrainPerFishStrength
				* Config.StruggleDrainMultiplier * Dt;
		}
	}
	else if (bLineRestraining)
	{
		// 鱼虽然处于挣扎阶段，但当前在横切/回头，尚未形成强对抗。左键可以趁角度窗口收线；
		// 若已有部分向外分量，收线速度、体力与磨损都按同一个连续投影比例过渡。
		RodWearDelta = Config.FishStrength * OutwardLoad * Config.StalemateRodWearPerFishStrength * Dt
			* Config.TautRodWearMultiplier;
		if (!bPulling)
		{
			RodWearDelta += Config.StruggleHoldRodWearPerSecond * OutwardLoad * Dt
				* Config.TautRodWearMultiplier;
		}
		// 带载左键时方向只影响牵引效率和线负载，不决定双方是否做功；没有主动拉时仍按真实向外负载缩放。
		const double StaminaExchangeLoad = bPullReachesTaut ? 1.0 : OutwardLoad;
		FishDrain = Config.CatStrength * StaminaExchangeLoad * Config.StalemateFishDrainPerCatStrength
			* Config.StruggleDrainMultiplier * Dt;
		CatDrain = Config.FishStrength * StaminaExchangeLoad * Config.StalemateCatDrainPerFishStrength
			* Config.StruggleDrainMultiplier * Dt;
	}
	else
	{
		// 鱼在挣扎但鱼线未形成对抗：按实际方向自由游，可能向外、横切或假动作向内。
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

	// 右键本身不吐线，只在鱼确实向外游到现有线端之外时让 L_paid 跟随增长。
	// 不按键时 L_paid 固定；左键先收余线，再以有限水平位移牵引鱼，不能把缩短后的三维线长直接当成瞬移目标。
	const double UnconstrainedDistance1 = Instant == ECatFightStepOutcome::Overpowered
		? VerticalDistance : FreeSwimDistance1;
	double LineLength = PaidOutLine0;
	FVector ProposedFishWorldPosition = FreeFishPosition;
	// 把候选位置限制到指定线长内。这里只处理“鱼想越过线端”的硬约束，不主动缩小球面来拖拽鱼。
	const auto ConstrainCandidateToLine = [&](FVector& Candidate, const double ConstraintLineLength)
	{
		if (FVector::Distance(RodTipWorldPosition, Candidate)
			<= ConstraintLineLength + UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			return;
		}
		const FVector CandidateHorizontalFromRod(Candidate.X - RodTipWorldPosition.X,
			Candidate.Y - RodTipWorldPosition.Y, 0.0);
		const FVector ConstrainedDirection = CandidateHorizontalFromRod.GetSafeNormal(
			UE_DOUBLE_SMALL_NUMBER, FishDirection);
		const double AllowedHorizontalRadius = FMath::Sqrt(FMath::Max(0.0,
			FMath::Square(ConstraintLineLength) - FMath::Square(VerticalDistance)));
		Candidate.X = RodTipWorldPosition.X
			+ ConstrainedDirection.X * AllowedHorizontalRadius;
		Candidate.Y = RodTipWorldPosition.Y
			+ ConstrainedDirection.Y * AllowedHorizontalRadius;
		Candidate.Z = State.FishWorldPosition.Z;
	};

	if (Instant == ECatFightStepOutcome::Overpowered)
	{
		LineLength = VerticalDistance;
	}
	else if (bSlacking)
	{
		LineLength = FMath::Max(LineLength,
			FMath::Min(UnconstrainedDistance1, Config.MaximumLineLengthCentimeters));
		LineLength = FMath::Clamp(LineLength, VerticalDistance, Config.MaximumLineLengthCentimeters);
		ConstrainCandidateToLine(ProposedFishWorldPosition, LineLength);
	}
	else if (bPulling)
	{
		// 先收掉已有余线。这个阶段当前鱼位置仍在新线长内，因此只会阻止本步自由游动继续越线，不会把鱼吸向新球面。
		LineLength = FMath::Clamp(PaidOutLine0 - FreeSlackReelStep,
			VerticalDistance, Config.MaximumLineLengthCentimeters);
		ConstrainCandidateToLine(ProposedFishWorldPosition, LineLength);

		// 带载收线是叠加在鱼自由游动结果上的有限牵引速度；一帧最多移动 EffectiveLoadedReelStep，
		// 不再通过 sqrt(L^2-H^2) 把几厘米收线放大成几十厘米水平吸附。
		FVector TowardRodProjection(RodTipWorldPosition.X - ProposedFishWorldPosition.X,
			RodTipWorldPosition.Y - ProposedFishWorldPosition.Y, 0.0);
		const double HorizontalDistanceToProjection = TowardRodProjection.Size2D();
		const double PullDisplacement = FMath::Min(EffectiveLoadedReelStep,
			HorizontalDistanceToProjection);
		if (PullDisplacement > UE_DOUBLE_SMALL_NUMBER)
		{
			ProposedFishWorldPosition += TowardRodProjection / HorizontalDistanceToProjection
				* PullDisplacement;
			ProposedFishWorldPosition.Z = State.FishWorldPosition.Z;
		}

		// 收线是请求，不是已经发生的事实。如果有限牵引尚未把鱼带到请求线长内，就以实际鱼距回填 L_paid；
		// 这表示带载卷线器暂时顶住/打滑，而不是让 Actor 为迁就账面线长瞬移。
		const double RequestedLineLength = FMath::Max(VerticalDistance,
			LineLength - EffectiveLoadedReelStep);
		LineLength = FMath::Clamp(FMath::Max(RequestedLineLength,
			FVector::Distance(RodTipWorldPosition, ProposedFishWorldPosition)),
			VerticalDistance, Config.MaximumLineLengthCentimeters);
	}
	else
	{
		LineLength = FMath::Clamp(LineLength, VerticalDistance, Config.MaximumLineLengthCentimeters);
		ConstrainCandidateToLine(ProposedFishWorldPosition, LineLength);
	}
	const double Tension = FMath::Max(0.0, UnconstrainedDistance1 - LineLength);
	const double Distance1 = Instant == ECatFightStepOutcome::Overpowered
		? VerticalDistance : FVector::Distance(RodTipWorldPosition, ProposedFishWorldPosition);

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
	Result.ProposedFishWorldPosition = ProposedFishWorldPosition;
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
