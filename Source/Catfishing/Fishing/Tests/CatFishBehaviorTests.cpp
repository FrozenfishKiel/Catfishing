#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Fishing/Simulation/CatFishSteeringModel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishBehaviorContinuousControlTest,
	"Catfishing.Unit.Fishing.Behavior.ContinuousEffortHeadingAndPersistentArcSide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishBehaviorContinuousControlTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFishSteeringConfig Config;
	Config.EaseOffEffortRange = FVector2D(0.2, 0.2);
	Config.LateralEffortRange = FVector2D(0.65, 0.65);
	Config.EffortFallPerSecond = 0.5;
	Config.MaximumTurnRateDegreesPerSecond = 90.0;
	FRandomStream Random(391);
	FCatFishSteeringState State;
	if (!TestTrue(TEXT("外冲初始化"), FCatFishSteeringModel::Initialize(Config, FVector::ForwardVector,
		ECatFishBehavior::OutwardRush, 1.0, Random, State))) return false;
	const FVector BeforeEase = State.CurrentDirection;
	if (!TestTrue(TEXT("树提交缓游命令"), FCatFishSteeringModel::BeginBehavior(Config, FVector::ForwardVector,
		ECatFishBehavior::EaseOff, 1.0, Random, State))) return false;
	TestEqual(TEXT("切状态不会跳变实际出力"), State.CurrentEffortRatio, 1.0);
	TestTrue(TEXT("切状态不瞬间掉头"), State.CurrentDirection.Equals(BeforeEase, 1e-9));
	FVector Direction;
	if (!TestTrue(TEXT("执行缓游固定步"), FCatFishSteeringModel::Step(Config, FVector::ForwardVector,
		0.1, Random, State, Direction))) return false;
	TestEqual(TEXT("出力以每秒比例下降"), State.CurrentEffortRatio, 0.95, 1e-9);
	TestTrue(TEXT("实际方向每步不超过最大角速"), FVector::DotProduct(Direction, BeforeEase)
		>= FMath::Cos(FMath::DegreesToRadians(9.0)) - 1e-9);
	TestEqual(TEXT("运动执行器不另推进策略时钟"), State.BehaviorElapsedSeconds, 0.0);

	if (!TestTrue(TEXT("树提交横切命令"), FCatFishSteeringModel::BeginBehavior(Config, FVector::ForwardVector,
		ECatFishBehavior::LateralArc, 1.0, Random, State))) return false;
	const double ChosenSide = State.LateralSign;
	for (int32 Index = 0; Index < 100; ++Index)
	{
		const FVector Outward = FVector::ForwardVector.RotateAngleAxis(Index * 0.75, FVector::UpVector);
		if (!TestTrue(TEXT("移动端点下执行连续横切"), FCatFishSteeringModel::Step(Config, Outward,
			0.05, Random, State, Direction))) return false;
		const FVector Tangent(-Outward.Y, Outward.X, 0.0);
		TestTrue(TEXT("目标保持同一横切侧而非逐帧随机"), FVector::DotProduct(State.TargetDirection, Tangent) * ChosenSide > 0.5);
	}
	TestEqual(TEXT("横切趋近独立目标出力"), State.CurrentEffortRatio, 0.65, 1e-9);
	TestEqual(TEXT("执行器始终不自行换策略"), State.Behavior, ECatFishBehavior::LateralArc);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishBehaviorFeedbackTest,
	"Catfishing.Unit.Fishing.Behavior.BlockageUsesCommittedLoadAndActualProgress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishBehaviorFeedbackTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFishSteeringConfig Config;
	Config.LoadSmoothingSeconds = 0.0;
	Config.MinimumBehaviorDurationSeconds = 0.5;
	Config.BlockedConfirmationSeconds = 0.3;
	FRandomStream Random(72);
	FCatFishSteeringState State;
	if (!FCatFishSteeringModel::Initialize(Config, FVector::ForwardVector,
		ECatFishBehavior::OutwardRush, 1.0, Random, State)) return false;
	FCatFishBehaviorFeedback Feedback;
	Feedback.bLineTaut = true;
	Feedback.NormalizedLineLoad = 1.0;
	Feedback.ExpectedFreeSpeedCentimetersPerSecond = 100.0;
	for (int32 Index = 0; Index < 2; ++Index)
		TestTrue(TEXT("反馈累计固定步"), FCatFishSteeringModel::AdvanceFeedback(Config, Feedback, 0.1, State));
	TestFalse(TEXT("一瞬间受阻不足以换路线"), FCatFishSteeringModel::TestCondition(Config, State,
		ECatFishBehaviorCondition::SustainedBlocked));
	for (int32 Index = 0; Index < 3; ++Index)
		FCatFishSteeringModel::AdvanceFeedback(Config, Feedback, 0.1, State);
	TestTrue(TEXT("持续有载无进展可被树读出"), FCatFishSteeringModel::TestCondition(Config, State,
		ECatFishBehaviorCondition::SustainedBlocked));
	TestTrue(TEXT("最短承诺由同一固定步时钟产生"), FCatFishSteeringModel::TestCondition(Config, State,
		ECatFishBehaviorCondition::MinimumDurationElapsed));
	TestEqual(TEXT("反馈本身不会将外冲换成横切"), State.Behavior, ECatFishBehavior::OutwardRush);

	Feedback.ActiveSwimDirection = FVector::RightVector;
	Feedback.ActualFishVelocityCentimetersPerSecond = FVector(0.0, 100.0, 0.0);
	FCatFishSteeringModel::AdvanceFeedback(Config, Feedback, 0.1, State);
	TestFalse(TEXT("横切有主动进展时张力不冒充受阻"), FCatFishSteeringModel::TestCondition(Config, State,
		ECatFishBehaviorCondition::SustainedBlocked));
	Feedback.ActualFishVelocityCentimetersPerSecond = FVector::ZeroVector;
	Feedback.bLineTaut = false;
	for (int32 Index = 0; Index < 8; ++Index)
		FCatFishSteeringModel::AdvanceFeedback(Config, Feedback, 0.1, State);
	TestFalse(TEXT("无负载起步不误判为玩家限制路线"), FCatFishSteeringModel::TestCondition(Config, State,
		ECatFishBehaviorCondition::SustainedBlocked));
	Feedback.bLineTaut = true;
	Feedback.ExpectedFreeSpeedCentimetersPerSecond = 0.0;
	FCatFishSteeringModel::AdvanceFeedback(Config, Feedback, 0.5, State);
	TestEqual(TEXT("零主动速度不会因除零变成受阻"), State.BlockedSeconds, 0.0);
	Feedback.FishStaminaRatio = 0.2;
	FCatFishSteeringModel::AdvanceFeedback(Config, Feedback, 0.1, State);
	TestTrue(TEXT("低体力只发布条件，不自行改状态"), FCatFishSteeringModel::TestCondition(Config, State,
		ECatFishBehaviorCondition::LowStamina));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishBehaviorShoreContinuityTest,
	"Catfishing.Unit.Fishing.Behavior.ShoreFeedbackKeepsContinuousHeadingAndDeterministicRandom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishBehaviorShoreContinuityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFishSteeringConfig Config;
	Config.MaximumTurnRateDegreesPerSecond = 120.0;
	FRandomStream FirstRandom(981);
	FRandomStream SecondRandom(981);
	FCatFishSteeringState First;
	FCatFishSteeringState Second;
	FCatFishSteeringModel::Initialize(Config, FVector::ForwardVector, ECatFishBehavior::LateralArc, 0.5, FirstRandom, First);
	FCatFishSteeringModel::Initialize(Config, FVector::ForwardVector, ECatFishBehavior::LateralArc, 0.5, SecondRandom, Second);
	FVector DirectionA;
	FVector DirectionB;
	for (int32 Index = 0; Index < 50; ++Index)
	{
		FCatFishSteeringModel::Step(Config, FVector::ForwardVector, 0.05, FirstRandom, First, DirectionA);
		FCatFishSteeringModel::Step(Config, FVector::ForwardVector, 0.05, SecondRandom, Second, DirectionB);
	}
	TestTrue(TEXT("同种子同输入产生同一连续方向"), DirectionA.Equals(DirectionB, 1e-9));
	TestEqual(TEXT("同种子同输入保持随机流一致"), FirstRandom.GetCurrentSeed(), SecondRandom.GetCurrentSeed());
	const FVector Previous = First.CurrentDirection;
	const int32 BeforeShoreSeed = FirstRandom.GetCurrentSeed();
	TestTrue(TEXT("岸线反馈有效"), FCatFishSteeringModel::RedirectFromWaterBoundary(Config,
		-Previous, FirstRandom, First));
	TestTrue(TEXT("岸线不瞬间反射身体主动朝向"), First.CurrentDirection.Equals(Previous, 1e-9));
	TestTrue(TEXT("岸线目标落在水内半平面"), FVector::DotProduct(First.TargetDirection, -Previous) > 0.0);
	TestEqual(TEXT("岸线反馈不重新抽左右侧"), FirstRandom.GetCurrentSeed(), BeforeShoreSeed);
	FCatFishSteeringModel::Step(Config, FVector::ForwardVector, 0.05, FirstRandom, First, DirectionA);
	TestTrue(TEXT("岸线反馈后仍受最大角速度约束"), FVector::DotProduct(Previous, DirectionA)
		>= FMath::Cos(FMath::DegreesToRadians(6.0)) - 1e-9);
	const double BeforeRejectedEffort = First.CurrentEffortRatio;
	TestFalse(TEXT("非法负步长被拒绝"), FCatFishSteeringModel::Step(Config, FVector::ForwardVector,
		-0.1, FirstRandom, First, DirectionA));
	TestEqual(TEXT("被拒步骤不改主动出力"), First.CurrentEffortRatio, BeforeRejectedEffort);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishBehaviorEscapeResumeTest,
	"Catfishing.Unit.Fishing.Behavior.ExhaustedCatOverrideFreezesPlanAndResumesEffortContinuously",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishBehaviorEscapeResumeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFishSteeringConfig Config;
	Config.EaseOffEffortRange = FVector2D(0.2, 0.2);
	Config.EffortFallPerSecond = 0.6;
	FRandomStream Random(3157);
	FCatFishSteeringState State;
	if (!TestTrue(TEXT("初始化缓游策略"), FCatFishSteeringModel::Initialize(Config, FVector::ForwardVector,
		ECatFishBehavior::EaseOff, 0.2, Random, State))) return false;
	FVector Direction;
	if (!TestTrue(TEXT("先实际降低到两成出力"), FCatFishSteeringModel::Step(Config, FVector::ForwardVector,
		2.0, Random, State, Direction))) return false;
	TestEqual(TEXT("强拖开始前实际为两成出力"), State.CurrentEffortRatio, 0.2, 1e-9);
	FCatFishBehaviorFeedback Feedback;
	Feedback.FishStaminaRatio = 0.2;
	if (!FCatFishSteeringModel::AdvanceFeedback(Config, Feedback, 0.3, State)) return false;
	const double BeforeElapsed = State.BehaviorElapsedSeconds;
	const double BeforeDuration = State.BehaviorDurationSeconds;
	const double BeforeRetarget = State.RetargetSecondsRemaining;
	const double BeforeTargetEffort = State.TargetEffortRatio;
	const int32 BeforeSeed = Random.GetCurrentSeed();
	for (int32 Index = 0; Index < 200; ++Index)
	{
		if (!TestTrue(TEXT("强拖覆盖仍可执行"), FCatFishSteeringModel::Step(Config, FVector::ForwardVector,
			0.05, Random, State, Direction, true))) return false;
		TestEqual(TEXT("包括强拖第一步在内，执行记忆与物理满力一致"), State.CurrentEffortRatio, 1.0);
	}
	TestEqual(TEXT("长时间强拖没有消耗普通行为随机流"), Random.GetCurrentSeed(), BeforeSeed);
	TestEqual(TEXT("长时间强拖没有提前耗尽普通策略时长"), State.BehaviorElapsedSeconds, BeforeElapsed);
	TestEqual(TEXT("强拖不重抽原策略期限"), State.BehaviorDurationSeconds, BeforeDuration);
	TestEqual(TEXT("强拖不推进普通方向重选时钟"), State.RetargetSecondsRemaining, BeforeRetarget);
	TestEqual(TEXT("强拖保留待恢复的目标出力"), State.TargetEffortRatio, BeforeTargetEffort);
	TestEqual(TEXT("强拖保留被暂停的缓游策略"), State.Behavior, ECatFishBehavior::EaseOff);
	if (!TestTrue(TEXT("助手恢复出力后执行原策略"), FCatFishSteeringModel::Step(Config, FVector::ForwardVector,
		0.05, Random, State, Direction))) return false;
	TestEqual(TEXT("恢复第一步从满力按下降速率衔接，不跳回旧两成"), State.CurrentEffortRatio, 0.97, 1e-9);
	// 活跃对抗也必须暂停整轮预算；仅检查缓游的零计时不能证明这一契约。
	if (!FCatFishSteeringModel::BeginBehavior(Config, FVector::ForwardVector,
		ECatFishBehavior::OutwardRush, 0.2, Random, State)
		|| !FCatFishSteeringModel::AdvanceFeedback(Config, Feedback, 0.5, State)) return false;
	const double BeforeBoutElapsed = State.ActiveBoutElapsedSeconds;
	const double BeforeBoutDuration = State.ActiveBoutDurationSeconds;
	const int32 BeforeActiveSeed = Random.GetCurrentSeed();
	for (int32 Index = 0; Index < 100; ++Index)
		if (!FCatFishSteeringModel::Step(Config, FVector::ForwardVector, 0.05, Random, State, Direction, true)) return false;
	TestTrue(TEXT("夹具确实已累计一段活跃对抗时间"), BeforeBoutElapsed > 0.0);
	TestEqual(TEXT("强拖不会耗尽整轮对抗时间"), State.ActiveBoutElapsedSeconds, BeforeBoutElapsed);
	TestEqual(TEXT("强拖不会重采整轮对抗时限"), State.ActiveBoutDurationSeconds, BeforeBoutDuration);
	TestEqual(TEXT("活跃对抗强拖也不推进随机流"), Random.GetCurrentSeed(), BeforeActiveSeed);
	return !HasAnyErrors();
}

#endif
