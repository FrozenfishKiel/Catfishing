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

	const auto CreateFixture = [&](const TCHAR* Name)
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
		Runner->InitialFishStamina = Runner->State.FishStamina = 100.0;
		Runner->State.CatStamina = 100.0;
		Runner->State.FishWorldPosition = FVector(500.0, 0.0, 0.0);
		Runner->SteeringRandom.Initialize(2003);
		Runner->SteeringConfig.MinimumBehaviorDurationSeconds = 0.2;
		Runner->SteeringConfig.OutwardDurationRangeSeconds = FVector2D(0.8, 0.8);
		Runner->SteeringConfig.LateralDurationRangeSeconds = FVector2D(0.7, 0.7);
		Runner->SteeringConfig.EaseOffDurationRangeSeconds = FVector2D(0.6, 0.6);
		Runner->SteeringConfig.BlockedConfirmationSeconds = 0.15;
		Runner->SteeringConfig.LoadSmoothingSeconds = 0.0;
		if (!TestTrue(FString::Printf(TEXT("%s启动正式行为树"), Name), Fish->StartFishBehaviorFromAuthority(Tree, Runner)))
			return TPair<ACatFishEncounterActor*, UCatFishingFightRunner*>(nullptr, nullptr);
		return TPair<ACatFishEncounterActor*, UCatFishingFightRunner*>(Fish, Runner);
	};
	const auto Free = CreateFixture(TEXT("自由外游"));
	const auto Blocked = CreateFixture(TEXT("持续受阻"));
	const auto Tired = CreateFixture(TEXT("低体力外游"));
	if (!Free.Key || !Blocked.Key || !Tired.Key) return false;
	Tired.Value->State.FishStamina = 20.0;
	TestEqual(TEXT("正式树首次选择外冲叶子"), Free.Value->SteeringState.Behavior, ECatFishBehavior::OutwardRush);
	TestFalse(TEXT("鱼树不会注册独立组件Tick"), Free.Key->FishBehaviorStateTree->PrimaryComponentTick.bCanEverTick);
	Free.Value->SteeringState.BehaviorElapsedSeconds = 1.0;
	World->Tick(LEVELTICK_All, 0.5f);
	TestEqual(TEXT("即便已到期，普通WorldTick也不能替固定步转移"), Free.Value->SteeringState.Behavior, ECatFishBehavior::OutwardRush);
	Free.Value->SteeringState.BehaviorElapsedSeconds = 0.0;

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
		return FCatFishSteeringModel::AdvanceFeedback(Fixture.Value->SteeringConfig, Feedback,
			0.05, Fixture.Value->SteeringState) && Fixture.Key->TickFishBehaviorFromAuthority(0.05f);
	};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (!TestTrue(TEXT("自由路径固定步执行"), Advance(Free, false))
			|| !TestTrue(TEXT("受阻路径固定步执行"), Advance(Blocked, true))
			|| !TestTrue(TEXT("低体力路径固定步执行"), Advance(Tired, false))) return false;
		if (Index < 3)
		{
			TestEqual(TEXT("受阻也必须完成最短承诺"), Blocked.Value->SteeringState.Behavior, ECatFishBehavior::OutwardRush);
			TestEqual(TEXT("低体力不能跳过最短承诺"), Tired.Value->SteeringState.Behavior, ECatFishBehavior::OutwardRush);
		}
	}
	TestEqual(TEXT("相同种子下自由游仍在外冲"), Free.Value->SteeringState.Behavior, ECatFishBehavior::OutwardRush);
	TestEqual(TEXT("相同种子下受阻由真实树边转横切"), Blocked.Value->SteeringState.Behavior, ECatFishBehavior::LateralArc);
	TestEqual(TEXT("无受阻的低体力鱼由真实条件边提前缓游"), Tired.Value->SteeringState.Behavior, ECatFishBehavior::EaseOff);
	for (int32 Index = 4; Index < 17; ++Index)
		if (!Advance(Free, false)) return false;
	TestEqual(TEXT("外冲最长时限由树选择缓游叶子"), Free.Value->SteeringState.Behavior, ECatFishBehavior::EaseOff);

	Free.Key->StopFishBehaviorFromAuthority();
	Blocked.Key->StopFishBehaviorFromAuthority();
	Tired.Key->StopFishBehaviorFromAuthority();
	TestFalse(TEXT("停止后固定步不能推进树"), Free.Key->TickFishBehaviorFromAuthority(0.05f));
	TestFalse(TEXT("退出清除Runner行为代理"), Free.Key->AuthorityFightRunner.IsValid());
	Free.Value->bRunning = Blocked.Value->bRunning = Tired.Value->bRunning = false;
	return !HasAnyErrors();
}

#endif
