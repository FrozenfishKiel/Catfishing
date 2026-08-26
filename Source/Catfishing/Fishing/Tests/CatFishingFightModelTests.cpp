#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Fishing/CatFishingFightModel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingFightJudgeTableTest,
	"Catfishing.Unit.Fishing.FightModel.JudgeTableFourExitsAndTieBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingFightStalemateTest,
	"Catfishing.Unit.Fishing.FightModel.StalematePerSecondCostsAndZeroPriority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingFightDistanceModelTest,
	"Catfishing.Unit.Fishing.FightModel.DistanceLineRatesAndInstantExits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingFightModifiersTest,
	"Catfishing.Unit.Fishing.FightModel.ProbabilitySpeedAndPerfectHookModifiers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatFishingFightModelTest
{
	/** 判定表用例：三个输入和期望出口，以及给人看的说明。 */
	struct FJudgeCase
	{
		double CatStrength;
		double FishStrength;
		double RodStrength;
		ECatFishingPullVerdict Expected;
		const TCHAR* Label;
	};

	// 参数构造流程：给一套不会触发近岸/体力出口的中性参数，各用例只改自己关心的字段。
	static FCatFishingFightParams MakeParams()
	{
		FCatFishingFightParams Params;
		Params.CatStrength = 50.0;
		Params.FishStrengthBase = 40.0;
		Params.RodStrength = 60.0;
		Params.LineMaxMeters = 60.0;
		Params.FishSpeedCoefficient = 1.0;
		Params.CatStaminaMax = 100.0;
		Params.NearShoreDistanceMeters = 1.0;
		Params.OutwardProbabilityBase = 0.5;
		Params.bGiant = false;
		return Params;
	}

	// 状态构造流程：直接摆好一段很长的指定方向段，避免 Step 里的段 roll 干扰对单行速率表/判定表的断言。
	static FCatFishingFightState MakeState(const ECatFishSwimState Swim, const double Distance, const double Line,
		const double FishStamina)
	{
		FCatFishingFightState State;
		State.SwimState = Swim;
		State.SegmentRemainingSeconds = 1000.0;
		State.DistanceMeters = Distance;
		State.LineMeters = Line;
		State.FishStamina = FishStamina;
		State.FishStaminaMax = FishStamina;
		return State;
	}
}

// 测试流程：表驱动覆盖飞书 4.3 "向外游+拖"四条出口，外加三条取等边界（竿强度==min(猫力,鱼力) 必须断竿、鱼力==猫力 必
// 须拖下水、猫力==鱼力×2 必须碾压），
// 以及"竿强度刚好比 min 大 1"不断竿、"猫力刚好差 1 不到 2 倍"只僵持这两条反向边界，证明等号全部落在更坏的一侧且判定顺序是 ①→④。
bool FCatFishingFightJudgeTableTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingFightModelTest;
	const FJudgeCase Cases[] =
	{
		{ 50.0, 40.0, 30.0, ECatFishingPullVerdict::LineBroken, TEXT("① 承载 30 < min(50,40)=40 → 断线") },
		{ 50.0, 40.0, 40.0, ECatFishingPullVerdict::LineBroken, TEXT("① 取等：承载 40 == min(50,40) → 断线") },
		{ 50.0, 40.0, 41.0, ECatFishingPullVerdict::Stalemate, TEXT("① 反向边界：承载 41 > min → 不断线，落到 ④ 僵持") },
		{ 50.0, 80.0, 100.0, ECatFishingPullVerdict::CatDraggedIn, TEXT("② 鱼 80 > 猫 50（竿够强）→ 拖下水") },
		{ 50.0, 50.0, 100.0, ECatFishingPullVerdict::CatDraggedIn, TEXT("② 取等：鱼 50 == 猫 50 → 拖下水") },
		{ 50.0, 50.0, 50.0, ECatFishingPullVerdict::LineBroken, TEXT("①优先于②：承载 50 == min(50,50) 先断线") },
		{ 50.0, 20.0, 100.0, ECatFishingPullVerdict::Overpowered, TEXT("③ 猫 50 > 鱼 20×2=40 → 碾压") },
		{ 50.0, 25.0, 100.0, ECatFishingPullVerdict::Overpowered, TEXT("③ 取等：猫 50 == 鱼 25×2 → 碾压") },
		{ 50.0, 25.0, 25.0, ECatFishingPullVerdict::LineBroken, TEXT("①优先于③：承载 25 == min(50,25) 先断线") },
		{ 49.0, 25.0, 100.0, ECatFishingPullVerdict::Stalemate, TEXT("③ 反向边界：猫 49 < 25×2 → 僵持") },
		{ 50.0, 40.0, 100.0, ECatFishingPullVerdict::Stalemate, TEXT("④ 其余组合 → 僵持") },
		{ 130.0, 120.0, 130.0, ECatFishingPullVerdict::Stalemate, TEXT("逗猫棒竿 130 > min(130,120)=120，猫 130 < 240 → 僵持") },
	};
	for (const FJudgeCase& Case : Cases)
	{
		const ECatFishingPullVerdict Actual = CatFishingFightModel::JudgeOutwardPull(Case.CatStrength, Case.FishStrength, Case.RodStrength);
		TestEqual(Case.Label, static_cast<int32>(Actual), static_cast<int32>(Case.Expected));
	}
	return !HasAnyErrors();
}

// 测试流程：
// 1. 僵持一秒：竿耐久 -= 鱼力×0.1、鱼体力 -= 猫力×0.08、猫体力 -= 鱼力×0.12，逐项核对数值，且 D/L 不变。
// 2. 三个归零优先级：同一帧三者都会归零时必须报翻肚；鱼没归零但猫体力归零报拖下水；只有竿归零报断竿；三者都没归零继续打。
// 3. 翻肚把 D 归零，鱼体力不为负。
bool FCatFishingFightStalemateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingFightModelTest;
	FRandomStream Random(7);
	const FCatFishingFightParams Params = MakeParams(); // 猫 50 / 鱼 40 / 竿 60 → ④ 僵持
	FCatFishingFightState State = MakeState(ECatFishSwimState::Outward, 20.0, 20.0, 30.0);
	FCatFishingFightResources Resources;
	Resources.CatStamina = 100.0;
	Resources.RodDurability = 40.0;
	FCatFishingFightStepDelta Delta;
	CatFishingFightModel::Step(State, Params, ECatFishingFightIntent::Pull, 1.0, Resources, Random, Delta);
	TestEqual(TEXT("僵持 1 秒竿耐久 -= 鱼力×0.1 = 4"), Delta.RodDurabilityCost, 4.0, 1e-9);
	TestEqual(TEXT("僵持 1 秒鱼体力 -= 猫力×0.08 = 4（30→26）"), State.FishStamina, 26.0, 1e-9);
	TestEqual(TEXT("僵持 1 秒猫体力 -= 鱼力×0.12 = 4.8"), Delta.CatStaminaDelta, -4.8, 1e-9);
	TestEqual(TEXT("僵持时 D 不变"), State.DistanceMeters, 20.0);
	TestEqual(TEXT("僵持时 L 不变"), State.LineMeters, 20.0);
	TestEqual(TEXT("僵持 1 秒后仍在打"), static_cast<int32>(State.Outcome), static_cast<int32>(ECatFishingFightOutcome::None));

	// 优先级 ①：鱼体力、猫体力、竿耐久同一帧全部归零 → 翻肚。
	State = MakeState(ECatFishSwimState::Outward, 20.0, 20.0, 4.0);
	Resources.CatStamina = 4.8;
	Resources.RodDurability = 4.0;
	CatFishingFightModel::Step(State, Params, ECatFishingFightIntent::Pull, 1.0, Resources, Random, Delta);
	TestEqual(TEXT("三者同帧归零时优先报翻肚"), static_cast<int32>(State.Outcome), static_cast<int32>(ECatFishingFightOutcome::FishExhausted));
	TestEqual(TEXT("翻肚后 D 归零"), State.DistanceMeters, 0.0);
	TestEqual(TEXT("翻肚后鱼体力不为负"), State.FishStamina, 0.0);

	// 优先级 ②：鱼还有体力，猫体力与竿同帧归零 → 拖下水。
	State = MakeState(ECatFishSwimState::Outward, 20.0, 20.0, 100.0);
	Resources.CatStamina = 4.8;
	Resources.RodDurability = 4.0;
	CatFishingFightModel::Step(State, Params, ECatFishingFightIntent::Pull, 1.0, Resources, Random, Delta);
	TestEqual(TEXT("鱼未归零时猫体力归零先于竿报拖下水"), static_cast<int32>(State.Outcome), static_cast<int32>(ECatFishingFightOutcome::CatDraggedIn));

	// 优先级 ③：只有竿归零 → 断竿。
	State = MakeState(ECatFishSwimState::Outward, 20.0, 20.0, 100.0);
	Resources.CatStamina = 100.0;
	Resources.RodDurability = 4.0;
	CatFishingFightModel::Step(State, Params, ECatFishingFightIntent::Pull, 1.0, Resources, Random, Delta);
	TestEqual(TEXT("只有鱼线耐久归零时报断线"), static_cast<int32>(State.Outcome), static_cast<int32>(ECatFishingFightOutcome::LineBroken));

	// 都差一点：继续打。
	State = MakeState(ECatFishSwimState::Outward, 20.0, 20.0, 4.01);
	Resources.CatStamina = 4.81;
	Resources.RodDurability = 4.01;
	CatFishingFightModel::Step(State, Params, ECatFishingFightIntent::Pull, 1.0, Resources, Random, Delta);
	TestEqual(TEXT("三者都没归零就继续打"), static_cast<int32>(State.Outcome), static_cast<int32>(ECatFishingFightOutcome::None));
	return !HasAnyErrors();
}

// 测试流程：逐行核对 D/L 速率表（向内+拖 -3/-3 且猫体力 -鱼力×0.15；向内+松 -1/不变；向外+松 +2.5/+2.5 且猫 +1.5；L 到顶后松无效且不回体力），
// 再核对三条瞬时出口（断竿、拖下水、碾压 D=0）与"遛到近岸"出口，以及体重档系数会乘在速率上。
bool FCatFishingFightDistanceModelTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingFightModelTest;
	FRandomStream Random(11);
	FCatFishingFightParams Params = MakeParams();
	FCatFishingFightResources Resources;
	Resources.CatStamina = 100.0;
	Resources.RodDurability = 40.0;
	FCatFishingFightStepDelta Delta;

	FCatFishingFightState State = MakeState(ECatFishSwimState::Inward, 20.0, 25.0, 30.0);
	CatFishingFightModel::Step(State, Params, ECatFishingFightIntent::Pull, 1.0, Resources, Random, Delta);
	TestEqual(TEXT("向内+拖：D -3"), State.DistanceMeters, 17.0, 1e-9);
	TestEqual(TEXT("向内+拖：L -3"), State.LineMeters, 22.0, 1e-9);
	TestEqual(TEXT("向内+拖：猫体力 -= 鱼力 40×0.15 = 6"), Delta.CatStaminaDelta, -6.0, 1e-9);

	State = MakeState(ECatFishSwimState::Inward, 20.0, 25.0, 30.0);
	CatFishingFightModel::Step(State, Params, ECatFishingFightIntent::Release, 1.0, Resources, Random, Delta);
	TestEqual(TEXT("向内+松：D -1"), State.DistanceMeters, 19.0, 1e-9);
	TestEqual(TEXT("向内+松：L 不变"), State.LineMeters, 25.0, 1e-9);
	TestEqual(TEXT("向内+松：无体力消耗"), Delta.CatStaminaDelta, 0.0, 1e-9);

	State = MakeState(ECatFishSwimState::Outward, 20.0, 20.0, 30.0);
	CatFishingFightModel::Step(State, Params, ECatFishingFightIntent::Release, 1.0, Resources, Random, Delta);
	TestEqual(TEXT("向外+松：D +2.5"), State.DistanceMeters, 22.5, 1e-9);
	TestEqual(TEXT("向外+松：L +2.5"), State.LineMeters, 22.5, 1e-9);
	TestEqual(TEXT("向外+松：猫体力 +1.5"), Delta.CatStaminaDelta, 1.5, 1e-9);

	// 不动视同松（工程暂定 D-17）。
	State = MakeState(ECatFishSwimState::Outward, 20.0, 20.0, 30.0);
	CatFishingFightModel::Step(State, Params, ECatFishingFightIntent::None, 1.0, Resources, Random, Delta);
	TestEqual(TEXT("向外+不动：与松相同 D +2.5"), State.DistanceMeters, 22.5, 1e-9);

	// L 到顶：右键失效，D/L 不变也不回体力。
	State = MakeState(ECatFishSwimState::Outward, 60.0, 60.0, 30.0);
	CatFishingFightModel::Step(State, Params, ECatFishingFightIntent::Release, 1.0, Resources, Random, Delta);
	TestEqual(TEXT("L 到顶后松：D 不变"), State.DistanceMeters, 60.0, 1e-9);
	TestEqual(TEXT("L 到顶后松：L 不超过 L_max"), State.LineMeters, 60.0, 1e-9);
	TestEqual(TEXT("L 到顶后松：不回体力"), Delta.CatStaminaDelta, 0.0, 1e-9);

	// L 接近上限时鱼带线被 L_max 截断，D 不得超过 L。
	State = MakeState(ECatFishSwimState::Outward, 59.0, 59.0, 30.0);
	CatFishingFightModel::Step(State, Params, ECatFishingFightIntent::Release, 1.0, Resources, Random, Delta);
	TestEqual(TEXT("鱼带线被 L_max 截断：L = 60"), State.LineMeters, 60.0, 1e-9);
	TestEqual(TEXT("鱼带线被 L_max 截断：D 不超过 L"), State.DistanceMeters, 60.0, 1e-9);

	// 体重档系数乘在速率上（大鱼 1.2）。
	Params.FishSpeedCoefficient = 1.2;
	State = MakeState(ECatFishSwimState::Inward, 20.0, 25.0, 30.0);
	CatFishingFightModel::Step(State, Params, ECatFishingFightIntent::Pull, 1.0, Resources, Random, Delta);
	TestEqual(TEXT("游速系数 1.2：向内+拖 D -3.6"), State.DistanceMeters, 16.4, 1e-9);
	Params.FishSpeedCoefficient = 1.0;

	// 三条瞬时出口。
	Params.RodStrength = 40.0; // == min(50,40) → 断竿
	State = MakeState(ECatFishSwimState::Outward, 20.0, 20.0, 30.0);
	CatFishingFightModel::Step(State, Params, ECatFishingFightIntent::Pull, 0.016, Resources, Random, Delta);
	TestEqual(TEXT("向外+拖命中①：同帧断线"), static_cast<int32>(State.Outcome), static_cast<int32>(ECatFishingFightOutcome::LineBroken));
	Params.RodStrength = 100.0;
	Params.FishStrengthBase = 50.0; // == 猫 50 → 拖下水
	State = MakeState(ECatFishSwimState::Outward, 20.0, 20.0, 30.0);
	CatFishingFightModel::Step(State, Params, ECatFishingFightIntent::Pull, 0.016, Resources, Random, Delta);
	TestEqual(TEXT("向外+拖命中②：同帧拖下水"), static_cast<int32>(State.Outcome), static_cast<int32>(ECatFishingFightOutcome::CatDraggedIn));
	Params.FishStrengthBase = 25.0; // 猫 50 == 25×2 → 碾压
	State = MakeState(ECatFishSwimState::Outward, 20.0, 20.0, 30.0);
	CatFishingFightModel::Step(State, Params, ECatFishingFightIntent::Pull, 0.016, Resources, Random, Delta);
	TestEqual(TEXT("向外+拖命中③：同帧碾压"), static_cast<int32>(State.Outcome), static_cast<int32>(ECatFishingFightOutcome::Overpowered));
	TestEqual(TEXT("碾压后 D 直接归零"), State.DistanceMeters, 0.0);
	TestEqual(TEXT("碾压不动鱼体力"), State.FishStamina, 30.0);

	// 向外游时松绝不会触发判定表。
	Params.FishStrengthBase = 50.0;
	State = MakeState(ECatFishSwimState::Outward, 20.0, 20.0, 30.0);
	CatFishingFightModel::Step(State, Params, ECatFishingFightIntent::Release, 1.0, Resources, Random, Delta);
	TestEqual(TEXT("向外+松不走判定表"), static_cast<int32>(State.Outcome), static_cast<int32>(ECatFishingFightOutcome::None));

	// 遛到近岸：向内+拖把 D 从 3.5 拉到 0.5 ≤ 1 → ReeledToShore。
	Params.FishStrengthBase = 40.0;
	State = MakeState(ECatFishSwimState::Inward, 3.5, 10.0, 30.0);
	CatFishingFightModel::Step(State, Params, ECatFishingFightIntent::Pull, 1.0, Resources, Random, Delta);
	TestEqual(TEXT("D ≤ 近岸距离报遛到岸边"), static_cast<int32>(State.Outcome), static_cast<int32>(ECatFishingFightOutcome::ReeledToShore));

	// 有终局后 Step 不再改任何值。
	const FCatFishingFightState Frozen = State;
	CatFishingFightModel::Step(State, Params, ECatFishingFightIntent::Pull, 1.0, Resources, Random, Delta);
	TestEqual(TEXT("终局后 D 不变"), State.DistanceMeters, Frozen.DistanceMeters);
	TestEqual(TEXT("终局后不再产生增量"), Delta.CatStaminaDelta, 0.0);
	return !HasAnyErrors();
}

// 测试流程：P(向外) 的体力/水深修正与夹取、四档体重游速系数（含档边界）、完美中鱼削减系数，以及开局状态（D=L=D₀、强制向外、体力满）。
bool FCatFishingFightModifiersTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingFightModel;
	TestEqual(TEXT("体力>50%、>10m：P=P_base"), ComputeOutwardProbability(0.5, 0.8, 20.0), 0.5, 1e-9);
	TestEqual(TEXT("体力 50% 归 30~50% 档 ×0.8"), ComputeOutwardProbability(0.5, 0.5, 20.0), 0.4, 1e-9);
	TestEqual(TEXT("体力 <30% ×0.6"), ComputeOutwardProbability(0.5, 0.2, 20.0), 0.3, 1e-9);
	TestEqual(TEXT("10m 处水深修正 ×1.0"), ComputeOutwardProbability(0.5, 1.0, 10.0), 0.5, 1e-9);
	TestEqual(TEXT("3m 处水深修正 ×1.5"), ComputeOutwardProbability(0.5, 1.0, 3.0), 0.75, 1e-9);
	TestEqual(TEXT("6.5m 处线性插值 ×1.25"), ComputeOutwardProbability(0.5, 1.0, 6.5), 0.625, 1e-9);
	TestEqual(TEXT("<3m ×1.8"), ComputeOutwardProbability(0.5, 1.0, 2.0), 0.9, 1e-9);
	TestEqual(TEXT("上夹 95%"), ComputeOutwardProbability(0.7, 1.0, 2.0), 0.95, 1e-9);
	TestEqual(TEXT("下夹 5%"), ComputeOutwardProbability(0.05, 0.1, 20.0), 0.05, 1e-9);

	TestEqual(TEXT("<1kg 小鱼 0.8"), ComputeFishSpeedCoefficient(0.5), 0.8, 1e-9);
	TestEqual(TEXT("1kg 归中档 1.0"), ComputeFishSpeedCoefficient(1.0), 1.0, 1e-9);
	TestEqual(TEXT("8kg 归中档 1.0"), ComputeFishSpeedCoefficient(8.0), 1.0, 1e-9);
	TestEqual(TEXT("8.1kg 大鱼 1.2"), ComputeFishSpeedCoefficient(8.1), 1.2, 1e-9);
	TestEqual(TEXT("15kg 归大档 1.2"), ComputeFishSpeedCoefficient(15.0), 1.2, 1e-9);
	TestEqual(TEXT(">15kg 巨影 1.5"), ComputeFishSpeedCoefficient(20.0), 1.5, 1e-9);

	double Strength = 100.0;
	double Stamina = 100.0;
	ApplyPerfectHookReduction(Strength, Stamina);
	TestEqual(TEXT("完美中鱼：力量 -20%"), Strength, 80.0, 1e-9);
	TestEqual(TEXT("完美中鱼：体力 -15%"), Stamina, 85.0, 1e-9);

	FRandomStream Random(3);
	FCatFishingFightParams Params = CatFishingFightModelTest::MakeParams();
	FCatFishingFightState State;
	BeginFight(State, Params, 5.0, 30.0, Random);
	TestEqual(TEXT("开局 D=D₀"), State.DistanceMeters, 5.0);
	TestEqual(TEXT("开局 L=D₀"), State.LineMeters, 5.0);
	TestEqual(TEXT("开局强制向外游"), static_cast<int32>(State.SwimState), static_cast<int32>(ECatFishSwimState::Outward));
	TestTrue(TEXT("开局向外段长 3~6 秒"), State.SegmentRemainingSeconds >= 3.0 && State.SegmentRemainingSeconds <= 6.0);
	TestEqual(TEXT("开局鱼体力满"), State.FishStamina, 30.0);
	TestFalse(TEXT("开局没有垂死挣扎"), State.IsDeathStruggling());

	// 巨影：段 roll 永远向外。
	Params.bGiant = true;
	State.SegmentRemainingSeconds = 0.0;
	State.FishStamina = 1.0; // 体力比例极低也不改方向、不触发挣扎
	FCatFishingFightResources Resources;
	Resources.CatStamina = 100.0;
	Resources.RodDurability = 100.0;
	FCatFishingFightStepDelta Delta;
	for (int32 Index = 0; Index < 20; ++Index)
	{
		State.SegmentRemainingSeconds = 0.0;
		Step(State, Params, ECatFishingFightIntent::Release, 0.01, Resources, Random, Delta);
		TestEqual(TEXT("巨影段 roll 恒为向外"), static_cast<int32>(State.SwimState), static_cast<int32>(ECatFishSwimState::Outward));
		TestFalse(TEXT("巨影不触发垂死挣扎"), State.bDeathStruggleUsed);
	}
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
