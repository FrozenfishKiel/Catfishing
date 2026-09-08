#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/StateTreeComponent.h"
#include "Fishing/Actors/CatFishEncounterActor.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Simulation/CatFishingFightRunner.h"
#include "StateTree.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishBehaviorStateTreeRuntimeTest,
	"Catfishing.Unit.Fishing.Behavior.FormalTreeSelectsFeedbackBranchesOnlyAtFixedSteps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishBehaviorStateTreeRuntimeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UStateTree* Tree = LoadObject<UStateTree>(nullptr, TEXT("/Game/Data/StateTrees/ST_FishFight.ST_FishFight"));
	if (!TestNotNull(TEXT("正式鱼树可加载"), Tree)) return false;
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Wrapper.GetTestWorld();

	const auto CreateFixture = [&](const TCHAR* Name, const FCatFishSteeringConfig& SteeringConfig,
		const double StaminaRatio)
	{
		ACatFishingSession* Session = World->SpawnActor<ACatFishingSession>();
		ACatFishingRodActor* Rod = World->SpawnActor<ACatFishingRodActor>();
		ACatFishEncounterActor* Fish = World->SpawnActor<ACatFishEncounterActor>();
		UCatFishingFightRunner* Runner = NewObject<UCatFishingFightRunner>(Session);
		if (!Session || !Rod || !Fish || !Runner) return TPair<ACatFishEncounterActor*, UCatFishingFightRunner*>(nullptr, nullptr);
		// 此夹具只旁路与行为树无关的中鱼/物品事务，真实 Actor、Runner、正式树与条件仍运行。
		Fish->bIdentityInitialized = true;
		Runner->Session = Session;
		Runner->RodActor = Rod;
		Runner->FishActor = Fish;
		Runner->bInitialized = Runner->bRunning = true;
		Runner->InitialFishStamina = 100.0;
		Runner->State.FishStamina = 100.0 * StaminaRatio;
		Runner->State.CatStamina = 100.0;
		Runner->State.FishWorldPosition = FVector(500.0, 0.0, 0.0);
		Runner->SteeringRandom.Initialize(2003);
		Runner->SteeringConfig = SteeringConfig;
		if (!TestTrue(FString::Printf(TEXT("%s启动正式行为树"), Name), Fish->StartFishBehaviorFromAuthority(Tree, Runner)))
			return TPair<ACatFishEncounterActor*, UCatFishingFightRunner*>(nullptr, nullptr);
		return TPair<ACatFishEncounterActor*, UCatFishingFightRunner*>(Fish, Runner);
	};
	FCatFishSteeringConfig FreeConfig;
	FreeConfig.MinimumBehaviorDurationSeconds = 0.2;
	FreeConfig.OutwardDurationRangeSeconds = FVector2D(0.8, 0.8);
	FreeConfig.LateralDurationRangeSeconds = FVector2D(0.7, 0.7);
	FreeConfig.EaseOffDurationRangeSeconds = FVector2D(0.6, 0.6);
	FreeConfig.ActiveBoutDurationRangeSeconds = FVector2D(4.0, 4.0);
	FreeConfig.BlockedConfirmationSeconds = 0.15;
	FreeConfig.LoadSmoothingSeconds = 0.0;
	FCatFishSteeringConfig BlockedConfig = FreeConfig;
	BlockedConfig.OutwardDurationRangeSeconds = FVector2D(10.0, 10.0);
	BlockedConfig.LateralDurationRangeSeconds = FVector2D(10.0, 10.0);
	// 预算必定在刚换向、局部承诺尚未完成时耗尽，用真实树检验恢复的优先级。
	BlockedConfig.ActiveBoutDurationRangeSeconds = FVector2D(1.21, 1.29);
	FCatFishSteeringConfig TiredConfig = FreeConfig;
	TiredConfig.MinimumBehaviorDurationSeconds = 1.25;
	TiredConfig.OutwardDurationRangeSeconds = FVector2D(5.0, 5.0);
	TiredConfig.EaseOffDurationRangeSeconds = FVector2D(1.0, 1.0);
	TiredConfig.OutwardEffortRange = FVector2D(0.9, 0.9);
	TiredConfig.EaseOffEffortRange = FVector2D(0.35, 0.35);
	TiredConfig.OutwardAngularSpreadDegrees = 0.0;
	TiredConfig.EaseOffInwardBias = 0.0;
	const auto Free = CreateFixture(TEXT("自由外游"), FreeConfig, 1.0);
	const auto Blocked = CreateFixture(TEXT("持续受阻"), BlockedConfig, 1.0);
	const auto Tired = CreateFixture(TEXT("低体力外游"), TiredConfig, 0.2);
	if (!Free.Key || !Blocked.Key || !Tired.Key) return false;
	TestEqual(TEXT("正式树首次选择外冲叶子"), Free.Value->SteeringState.Behavior, ECatFishBehavior::OutwardRush);
	TestFalse(TEXT("鱼树不会注册独立组件Tick"), Free.Key->FishBehaviorStateTree->PrimaryComponentTick.bCanEverTick);
	Free.Value->SteeringState.BehaviorElapsedSeconds = 1.0;
	const double BeforeWorldTickBoutElapsed = Free.Value->SteeringState.ActiveBoutElapsedSeconds;
	const int32 BeforeWorldTickSeed = Free.Value->SteeringRandom.GetCurrentSeed();
	World->Tick(LEVELTICK_All, 0.5f);
	TestEqual(TEXT("即便已到期，普通WorldTick也不能替固定步转移"), Free.Value->SteeringState.Behavior, ECatFishBehavior::OutwardRush);
	TestEqual(TEXT("普通WorldTick不推进连续对抗预算"), Free.Value->SteeringState.ActiveBoutElapsedSeconds, BeforeWorldTickBoutElapsed);
	TestEqual(TEXT("普通WorldTick不消耗策略随机流"), Free.Value->SteeringRandom.GetCurrentSeed(), BeforeWorldTickSeed);
	Free.Value->SteeringState.BehaviorElapsedSeconds = 0.0;

	constexpr double FixedStepSeconds = 0.05;
	bool bContinuousControl = true;
	FCatFishBehaviorFeedback Feedback;
	Feedback.ExpectedFreeSpeedCentimetersPerSecond = 100.0;
	const auto Advance = [&](const TPair<ACatFishEncounterActor*, UCatFishingFightRunner*>& Fixture, const bool bBlocked)
	{
		Feedback.NormalizedLineLoad = bBlocked ? 1.0 : 0.0;
		Feedback.bLineTaut = bBlocked;
		Feedback.ActiveSwimDirection = Fixture.Value->SteeringState.CurrentDirection;
		Feedback.FishStaminaRatio = Fixture.Value->State.FishStamina / Fixture.Value->InitialFishStamina;
		Feedback.ActualFishVelocityCentimetersPerSecond = bBlocked ? FVector::ZeroVector
			: Feedback.ActiveSwimDirection * 100.0;
		const FVector BeforeDirection = Fixture.Value->SteeringState.CurrentDirection;
		const double BeforeEffort = Fixture.Value->SteeringState.CurrentEffortRatio;
		FVector ActualDirection;
		if (!FCatFishSteeringModel::AdvanceFeedback(Fixture.Value->SteeringConfig, Feedback,
			FixedStepSeconds, Fixture.Value->SteeringState)
			|| !Fixture.Key->TickFishBehaviorFromAuthority(static_cast<float>(FixedStepSeconds))
			|| !FCatFishSteeringModel::Step(Fixture.Value->SteeringConfig, FVector::ForwardVector,
				FixedStepSeconds, Fixture.Value->SteeringRandom, Fixture.Value->SteeringState, ActualDirection)) return false;
		const double TurnDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
			FVector::DotProduct(BeforeDirection, ActualDirection), -1.0, 1.0)));
		const double EffortDelta = Fixture.Value->SteeringState.CurrentEffortRatio - BeforeEffort;
		bContinuousControl &= TurnDegrees <= Fixture.Value->SteeringConfig.MaximumTurnRateDegreesPerSecond * FixedStepSeconds + 0.001
			&& EffortDelta <= Fixture.Value->SteeringConfig.EffortRisePerSecond * FixedStepSeconds + 0.000001
			&& -EffortDelta <= Fixture.Value->SteeringConfig.EffortFallPerSecond * FixedStepSeconds + 0.000001;
		return true;
	};
	const auto IsActive = [](const ECatFishBehavior Behavior)
	{
		return Behavior == ECatFishBehavior::OutwardRush || Behavior == ECatFishBehavior::LateralArc;
	};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (!TestTrue(TEXT("自由路径固定步执行"), Advance(Free, false))
			|| !TestTrue(TEXT("受阻路径固定步执行"), Advance(Blocked, true))) return false;
		if (Index < 3)
		{
			TestEqual(TEXT("受阻也必须完成最短承诺"), Blocked.Value->SteeringState.Behavior, ECatFishBehavior::OutwardRush);
		}
	}
	TestEqual(TEXT("相同种子下自由游仍在外冲"), Free.Value->SteeringState.Behavior, ECatFishBehavior::OutwardRush);
	TestEqual(TEXT("相同种子下受阻由真实树边转横切"), Blocked.Value->SteeringState.Behavior, ECatFishBehavior::LateralArc);
	for (int32 Index = 4; Index < 17; ++Index)
		if (!Advance(Free, false)) return false;
	TestEqual(TEXT("外冲最长时限由树选择缓游叶子"), Free.Value->SteeringState.Behavior, ECatFishBehavior::EaseOff);

	const double FirstBoutDuration = Blocked.Value->SteeringState.ActiveBoutDurationSeconds;
	int32 ActiveChanges = 1;
	bool bRecoveryBypassedLocalMinimum = false;
	for (int32 Index = 0; Index < 40 && IsActive(Blocked.Value->SteeringState.Behavior); ++Index)
	{
		const ECatFishBehavior BeforeBehavior = Blocked.Value->SteeringState.Behavior;
		const double BeforeLocalElapsed = Blocked.Value->SteeringState.BehaviorElapsedSeconds;
		const double BeforeBoutElapsed = Blocked.Value->SteeringState.ActiveBoutElapsedSeconds;
		if (!TestTrue(TEXT("反复受阻仍由正式树推进"), Advance(Blocked, true))) return false;
		if (IsActive(Blocked.Value->SteeringState.Behavior))
		{
			TestTrue(TEXT("主动换向不会重置已消耗的对抗预算"),
				Blocked.Value->SteeringState.ActiveBoutElapsedSeconds > BeforeBoutElapsed);
			TestEqual(TEXT("主动换向沿用本轮已抽取的预算"), Blocked.Value->SteeringState.ActiveBoutDurationSeconds, FirstBoutDuration);
			if (Blocked.Value->SteeringState.Behavior != BeforeBehavior) ++ActiveChanges;
		}
		else
		{
			bRecoveryBypassedLocalMinimum = BeforeLocalElapsed + FixedStepSeconds < BlockedConfig.MinimumBehaviorDurationSeconds;
		}
	}
	TestTrue(TEXT("持续受阻会在外冲和横切之间多次重试"), ActiveChanges >= 4);
	TestEqual(TEXT("多次受阻换向最终仍按总预算恢复"), Blocked.Value->SteeringState.Behavior, ECatFishBehavior::EaseOff);
	TestTrue(TEXT("总预算到期恢复不被刚进入行为的局部承诺挡住"), bRecoveryBypassedLocalMinimum);
	const double FinishedBoutElapsed = Blocked.Value->SteeringState.ActiveBoutElapsedSeconds;
	TestTrue(TEXT("预算到期在一个固定步内得到处理"), FinishedBoutElapsed <= FirstBoutDuration + FixedStepSeconds + 0.000001);
	for (int32 Index = 0; Index < 20 && Blocked.Value->SteeringState.Behavior == ECatFishBehavior::EaseOff; ++Index)
	{
		if (!Advance(Blocked, true)) return false;
		if (Blocked.Value->SteeringState.Behavior == ECatFishBehavior::EaseOff)
			TestEqual(TEXT("缓游不继续消耗连续对抗预算"), Blocked.Value->SteeringState.ActiveBoutElapsedSeconds, FinishedBoutElapsed);
	}
	TestEqual(TEXT("恢复后仍受阻时由正式树选择横切重试"), Blocked.Value->SteeringState.Behavior, ECatFishBehavior::LateralArc);
	TestEqual(TEXT("恢复结束才开始新一轮连续对抗计时"), Blocked.Value->SteeringState.ActiveBoutElapsedSeconds, 0.0);

	TestTrue(TEXT("低体力在进入首轮时缩短对抗预算"), FMath::IsNearlyEqual(
		Tired.Value->SteeringState.ActiveBoutDurationSeconds, 2.8, 0.000001));
	for (int32 Index = 0; Index < 26; ++Index)
		if (!TestTrue(TEXT("低体力也执行真实运动步"), Advance(Tired, false))) return false;
	TestEqual(TEXT("低体力越过最短承诺后仍能继续有效外冲"), Tired.Value->SteeringState.Behavior, ECatFishBehavior::OutwardRush);
	for (int32 Index = 0; Index < 40 && IsActive(Tired.Value->SteeringState.Behavior); ++Index)
		if (!Advance(Tired, false)) return false;
	TestEqual(TEXT("低体力主动阶段按缩短后的预算进入恢复"), Tired.Value->SteeringState.Behavior, ECatFishBehavior::EaseOff);
	for (int32 Index = 0; Index < 40 && Tired.Value->SteeringState.Behavior == ECatFishBehavior::EaseOff; ++Index)
		if (!Advance(Tired, false)) return false;
	TestEqual(TEXT("低体力恢复后仍有下一次外冲"), Tired.Value->SteeringState.Behavior, ECatFishBehavior::OutwardRush);
	TestTrue(TEXT("从缓游重新出力时保留实际力度爬升过程"), Tired.Value->SteeringState.CurrentEffortRatio < 0.5);
	TestTrue(TEXT("从缓游重新外冲时保留实际转向过程"),
		FVector::DotProduct(Tired.Value->SteeringState.CurrentDirection, FVector::ForwardVector) < 0.5);
	for (int32 Index = 0; Index < 26; ++Index)
		if (!Advance(Tired, false)) return false;
	TestEqual(TEXT("缓游后的外冲不会刚完成转向就因低体力被截断"), Tired.Value->SteeringState.Behavior, ECatFishBehavior::OutwardRush);
	const double ActiveOutwardFraction = Tired.Value->SteeringState.CurrentEffortRatio
		* FVector::DotProduct(Tired.Value->SteeringState.CurrentDirection, FVector::ForwardVector);
	TestTrue(TEXT("低体力仍有完成转向与爬升后的实际向外主动推力窗口"), ActiveOutwardFraction > 0.85);
	TestTrue(TEXT("真实树所有切换的运动输出遵守转向和力度变化上限"), bContinuousControl);

	Free.Key->StopFishBehaviorFromAuthority();
	Blocked.Key->StopFishBehaviorFromAuthority();
	Tired.Key->StopFishBehaviorFromAuthority();
	TestFalse(TEXT("停止后固定步不能推进树"), Free.Key->TickFishBehaviorFromAuthority(0.05f));
	TestFalse(TEXT("退出清除Runner行为代理"), Free.Key->AuthorityFightRunner.IsValid());
	Free.Value->bRunning = Blocked.Value->bRunning = Tired.Value->bRunning = false;
	return !HasAnyErrors();
}

#endif
