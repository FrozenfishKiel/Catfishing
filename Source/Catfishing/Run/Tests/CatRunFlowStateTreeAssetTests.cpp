#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Components/StateTreeComponent.h"
#include "Engine/World.h"
#include "Framework/Game/CatfishingGameMode.h"
#include "Run/CatRunSettings.h"
#include "Run/CatRunStateTreeEvents.h"

namespace CatRunFlowStateTreeAssetTest
{
	/**
	 * Run 配置覆盖守卫：打开 runtime、给出白天参数，并把最终天压到第 2 天，让成功结算夜在两次翻天内可达；析构整体恢复
	 * 项目配置。RunFlowStateTree 软引用不覆盖，测试要吃的就是项目 ini 里那条真实资产路径。
	 */
	struct FScopedRunSettings
	{
		/** 被临时覆盖的全局 Run Settings 默认对象；为空时不改也不恢复。 */
		UCatRunSettings* Settings = nullptr;

		/** 用例开始前的整份 Run 配置快照；析构时按字段逐一放回。 */
		bool bSavedEnableRunRuntime = false;
		float SavedDayLengthSeconds = 0.0f;
		int32 SavedQuotaTarget = 0;
		ECatRunScalingPolicy SavedScalingPolicy = ECatRunScalingPolicy::Undecided;
		ECatRunPolicyDecision SavedSuccessSettlementPolicy = ECatRunPolicyDecision::Undecided;
		int32 SavedFinalDayIndex = 0;

		/** 构造流程：保存原值后写入 runtime 开、白天 1200 秒（计时器在用例内不会触发）、固定额度 3、成功结算开且最终天=2。 */
		FScopedRunSettings()
		{
			Settings = GetMutableDefault<UCatRunSettings>();
			if (!Settings)
			{
				return;
			}
			bSavedEnableRunRuntime = Settings->bEnableRunRuntime;
			SavedDayLengthSeconds = Settings->DayLengthSeconds;
			SavedQuotaTarget = Settings->QuotaTarget;
			SavedScalingPolicy = Settings->PlayerScalingPolicy;
			SavedSuccessSettlementPolicy = Settings->SuccessSettlementPolicy;
			SavedFinalDayIndex = Settings->FinalDayIndex;
			Settings->bEnableRunRuntime = true;
			Settings->DayLengthSeconds = 1200.0f;
			Settings->QuotaTarget = 3;
			Settings->PlayerScalingPolicy = ECatRunScalingPolicy::FixedQuotaTarget;
			Settings->SuccessSettlementPolicy = ECatRunPolicyDecision::Enabled;
			Settings->FinalDayIndex = 2;
		}

		/** 析构流程：把全部字段放回用例前的值，避免最终天=2 泄漏给其他 GameMode 用例。 */
		~FScopedRunSettings()
		{
			if (!Settings)
			{
				return;
			}
			Settings->bEnableRunRuntime = bSavedEnableRunRuntime;
			Settings->DayLengthSeconds = SavedDayLengthSeconds;
			Settings->QuotaTarget = SavedQuotaTarget;
			Settings->PlayerScalingPolicy = SavedScalingPolicy;
			Settings->SuccessSettlementPolicy = SavedSuccessSettlementPolicy;
			Settings->FinalDayIndex = SavedFinalDayIndex;
		}
	};

	/** 在测试 World 里注册并启动真实 Lake GameMode，返回 GameMode 与它的 Run StateTree 组件；任一步失败返回 false。 */
	static bool StartRealGameMode(FAutomationTestBase& Test, UWorld& World, ACatfishingGameModeBase*& OutGameMode, UStateTreeComponent*& OutComponent)
	{
		const FString GameModeOption = FString::Printf(TEXT("?game=%s"), *ACatfishingGameModeBase::StaticClass()->GetPathName());
		FURL GameModeUrl(nullptr, *GameModeOption, TRAVEL_Absolute);
		Test.TestTrue(TEXT("测试 World 可注册项目 Lake GameMode"), World.SetGameMode(GameModeUrl));
		OutGameMode = World.GetAuthGameMode<ACatfishingGameModeBase>();
		Test.TestNotNull(TEXT("可取得项目 Lake authority GameMode"), OutGameMode);
		if (!OutGameMode)
		{
			return false;
		}
		World.InitializeActorsForPlay(GameModeUrl);
		OutGameMode->StartPlay();
		OutComponent = OutGameMode->FindComponentByClass<UStateTreeComponent>();
		Test.TestNotNull(TEXT("GameMode 持有 Run StateTree 组件"), OutComponent);
		return OutComponent != nullptr;
	}

	/**
	 * 向 Run 树投递一个事件并手动 tick 组件两帧；测试 World 不跑引擎 tick，所以直接调用组件 TickComponent。两帧是为了
	 * 让“事件边→新状态进入即完成→完成边”这种连锁（Ending→Ended）在同一次调用内走完。
	 */
	static void SendEventAndTick(UStateTreeComponent& Component, const FGameplayTag& Tag)
	{
		Component.SendStateTreeEvent(Tag, FConstStructView(), FName(TEXT("CatRunFlowTest")));
		Component.TickComponent(0.016f, LEVELTICK_All, nullptr);
		Component.TickComponent(0.016f, LEVELTICK_All, nullptr);
	}

	/** 用额度只读校验口探测 bRunCommandsOpen：命令门关着时返回 CommandsClosed，开着时走到阶段检查（DayActive 下为 InvalidPhase）。 */
	static ECatRunCommandError ProbeRunCommandGate(const ACatfishingGameModeBase& GameMode)
	{
		FCatQuotaContributionCommand Command;
		Command.Context.RequestId = FGuid::NewGuid();
		Command.Context.ExpectedRevision = GameMode.GetRunPublicState().Revision;
		Command.Context.StableNetId = TEXT("RunFlowAssetProbe");
		Command.QuotaCount = 1;
		return GameMode.ValidateCommittedQuotaContributionFromCoordinator(Command).Error;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatRunFlowStateTreeAssetStartsRunAndWalksToSuccessTest,
	"Catfishing.Unit.Run.StateTreeAsset.StartPlayEntersDayActiveAndWalksToSuccessSettlement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：用项目 ini 里的真实 ST_RunFlow 资产启动 Lake GameMode，断言 Run 不再 StartupFailed、命令门打开、首阶段
// DayActive；再用组件事件口逐条走 DayElapsed→NormalNight、AllEligibleReady(非最终天)→DayActive 第 2 天、
// DayElapsed→NormalNight、AllEligibleReady(最终天)→SuccessSettlementNight、SettlementComplete→Ending→Ended，并确认
// Ended 后命令门关闭。
bool FCatRunFlowStateTreeAssetStartsRunAndWalksToSuccessTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatRunFlowStateTreeAssetTest::FScopedRunSettings SettingsGuard;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 RunFlow 资产测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("RunFlow 资产测试 World 可用"), World);
	if (!World)
	{
		return false;
	}
	ACatfishingGameModeBase* GameMode = nullptr;
	UStateTreeComponent* Component = nullptr;
	if (!CatRunFlowStateTreeAssetTest::StartRealGameMode(*this, *World, GameMode, Component))
	{
		return false;
	}

	const FCatRunPublicState& State = GameMode->GetRunPublicState();
	TestNotEqual(TEXT("StartPlay 后 Run 不再是 StartupFailed"), State.EndReason, ECatRunEndReason::StartupFailed);
	TestTrue(TEXT("StartPlay 后 Run StateTree 组件在运行"), Component->IsRunning());
	TestEqual(TEXT("首个阶段是 DayActive"), State.Phase.Phase, ECatRunPhase::DayActive);
	TestEqual(TEXT("首日 DayIndex=1"), State.Phase.DayIndex, 1);
	TestTrue(TEXT("白天允许钓鱼"), State.Phase.bFishingAllowed);
	TestEqual(TEXT("命令门已打开：额度校验走到阶段检查而不是 CommandsClosed"),
		CatRunFlowStateTreeAssetTest::ProbeRunCommandGate(*GameMode), ECatRunCommandError::InvalidPhase);

	CatRunFlowStateTreeAssetTest::SendEventAndTick(*Component, CatRunStateTreeEvents::DayElapsed.GetTag());
	TestEqual(TEXT("DayElapsed 后进入 NormalNight"), State.Phase.Phase, ECatRunPhase::NormalNight);
	TestTrue(TEXT("普通夜晚打开额度写口"), State.Phase.bQuotaOpen);

	CatRunFlowStateTreeAssetTest::SendEventAndTick(*Component, CatRunStateTreeEvents::AllEligibleReady.GetTag());
	TestEqual(TEXT("非最终天 AllEligibleReady 再翻一天回到 DayActive"), State.Phase.Phase, ECatRunPhase::DayActive);
	TestEqual(TEXT("翻天后 DayIndex=2"), State.Phase.DayIndex, 2);

	CatRunFlowStateTreeAssetTest::SendEventAndTick(*Component, CatRunStateTreeEvents::DayElapsed.GetTag());
	TestEqual(TEXT("第 2 天到点再次进入 NormalNight"), State.Phase.Phase, ECatRunPhase::NormalNight);

	CatRunFlowStateTreeAssetTest::SendEventAndTick(*Component, CatRunStateTreeEvents::AllEligibleReady.GetTag());
	TestEqual(TEXT("最终天 AllEligibleReady 进入 SuccessSettlementNight"), State.Phase.Phase, ECatRunPhase::SuccessSettlementNight);
	TestEqual(TEXT("成功结算夜写入 Success 终局原因"), State.EndReason, ECatRunEndReason::Success);

	CatRunFlowStateTreeAssetTest::SendEventAndTick(*Component, CatRunStateTreeEvents::SettlementComplete.GetTag());
	TestEqual(TEXT("SettlementComplete 后经 Ending 落到 Ended"), State.Phase.Phase, ECatRunPhase::Ended);
	TestEqual(TEXT("Ended 后命令门关闭"),
		CatRunFlowStateTreeAssetTest::ProbeRunCommandGate(*GameMode), ECatRunCommandError::CommandsClosed);
	TestTrue(TEXT("Ended 状态用 Wait 任务把树保持在 Running，等 EndPlay 停"), Component->IsRunning());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatRunFlowStateTreeAssetQuotaFailedPathTest,
	"Catfishing.Unit.Run.StateTreeAsset.QuotaFailedWalksToFailureSettlementAndEnded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：同样用真实资产启动 GameMode，走 DayElapsed→NormalNight、QuotaFailed→FailureSettlementNight、
// SettlementComplete→Ended，覆盖成功用例没走到的失败结算边。
bool FCatRunFlowStateTreeAssetQuotaFailedPathTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatRunFlowStateTreeAssetTest::FScopedRunSettings SettingsGuard;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 RunFlow 失败路径测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("RunFlow 失败路径测试 World 可用"), World);
	if (!World)
	{
		return false;
	}
	ACatfishingGameModeBase* GameMode = nullptr;
	UStateTreeComponent* Component = nullptr;
	if (!CatRunFlowStateTreeAssetTest::StartRealGameMode(*this, *World, GameMode, Component))
	{
		return false;
	}

	const FCatRunPublicState& State = GameMode->GetRunPublicState();
	TestEqual(TEXT("启动后处于 DayActive"), State.Phase.Phase, ECatRunPhase::DayActive);
	CatRunFlowStateTreeAssetTest::SendEventAndTick(*Component, CatRunStateTreeEvents::DayElapsed.GetTag());
	TestEqual(TEXT("DayElapsed 后进入 NormalNight"), State.Phase.Phase, ECatRunPhase::NormalNight);
	CatRunFlowStateTreeAssetTest::SendEventAndTick(*Component, CatRunStateTreeEvents::QuotaFailed.GetTag());
	TestEqual(TEXT("QuotaFailed 后进入 FailureSettlementNight"), State.Phase.Phase, ECatRunPhase::FailureSettlementNight);
	TestEqual(TEXT("失败结算夜写入 QuotaFailed 终局原因"), State.EndReason, ECatRunEndReason::QuotaFailed);
	CatRunFlowStateTreeAssetTest::SendEventAndTick(*Component, CatRunStateTreeEvents::SettlementComplete.GetTag());
	TestEqual(TEXT("SettlementComplete 后经 Ending 落到 Ended"), State.Phase.Phase, ECatRunPhase::Ended);
	TestEqual(TEXT("Ended 后命令门关闭"),
		CatRunFlowStateTreeAssetTest::ProbeRunCommandGate(*GameMode), ECatRunCommandError::CommandsClosed);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
