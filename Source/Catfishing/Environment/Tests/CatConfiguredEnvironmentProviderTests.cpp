#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Environment/CatConfiguredEnvironmentProvider.h"
#include "Environment/CatEnvironmentSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatConfiguredEnvironmentProviderRuntimeGateTest,
	"Catfishing.Unit.Environment.ConfiguredProvider.RequiresRuntimeSettingsAndReturnsReadOnlySnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatConfiguredEnvironmentProviderTest
{
	/** 测试期间覆盖默认 Environment Settings 的守卫；Provider 使用 GetDefault，因此必须恢复默认对象。 */
	struct FEnvironmentSettingsOverride
	{
		/** 被临时改写的默认配置对象。 */
		UCatEnvironmentSettings* Settings = GetMutableDefault<UCatEnvironmentSettings>();

		/** 原始 runtime gate。 */
		bool bOldRuntime = false;

		/** 原始天气。 */
		ECatEnvironmentWeather OldWeather = ECatEnvironmentWeather::Unknown;

		/** 原始 Morning 分界。 */
		double OldMorning = 0.0;

		/** 原始 Dusk 分界。 */
		double OldDusk = 0.0;

		/** 原始事件 ID。 */
		FName OldEventId = NAME_None;

		// 默认对象覆盖流程：先保存旧值，再按用例写入关闭或完整配置；
		// Provider 读取 GetDefault，因此不能用瞬态设置对象绕过项目配置。
		explicit FEnvironmentSettingsOverride(const bool bRuntimeReady)
		{
			if (Settings)
			{
				bOldRuntime = Settings->bEnableEnvironmentRuntime;
				OldWeather = Settings->ConfiguredWeather;
				OldMorning = Settings->MorningEndFraction;
				OldDusk = Settings->DuskStartFraction;
				OldEventId = Settings->ActiveEventId;
				Settings->bEnableEnvironmentRuntime = bRuntimeReady;
				Settings->ConfiguredWeather = bRuntimeReady ? ECatEnvironmentWeather::Rain : ECatEnvironmentWeather::Unknown;
				Settings->MorningEndFraction = bRuntimeReady ? 0.25 : 0.0;
				Settings->DuskStartFraction = bRuntimeReady ? 0.75 : 0.0;
				Settings->ActiveEventId = bRuntimeReady ? FName(TEXT("StormBloom")) : NAME_None;
			}
		}

		// 恢复流程：测试结束还原默认对象，避免 Provider 测试影响后续 Settings 个案。
		~FEnvironmentSettingsOverride()
		{
			if (Settings)
			{
				Settings->bEnableEnvironmentRuntime = bOldRuntime;
				Settings->ConfiguredWeather = OldWeather;
				Settings->MorningEndFraction = OldMorning;
				Settings->DuskStartFraction = OldDusk;
				Settings->ActiveEventId = OldEventId;
			}
		}
	};

	// 快照流程：构造 DayActive 且带有效截止的 Run 输入；Provider 只能消费这份 DTO，不回写 Run。
	static FCatRunPhaseSnapshot MakeDaySnapshot()
	{
		FCatRunPhaseSnapshot Snapshot;
		Snapshot.RunId = FGuid::NewGuid();
		Snapshot.DayIndex = 1;
		Snapshot.Phase = ECatRunPhase::DayActive;
		Snapshot.ServerTimeAnchorSeconds = 0.0;
		Snapshot.DeadlineServerTimeSeconds = 100.0;
		Snapshot.bHasDeadline = true;
		Snapshot.bFishingAllowed = true;
		return Snapshot;
	}
}

// 测试流程：先在未配置状态下验证 Provider fail-closed，再临时启用默认环境设置，确认输出只来自 Run 快照和显式配置。
bool FCatConfiguredEnvironmentProviderRuntimeGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Environment Provider 测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建测试 World"), World);
	if (!World)
	{
		return false;
	}

	UCatConfiguredEnvironmentProvider* Provider = NewObject<UCatConfiguredEnvironmentProvider>(World);
	TestNotNull(TEXT("可创建配置环境 Provider"), Provider);
	if (!Provider)
	{
		return false;
	}

	const FCatRunPhaseSnapshot InputSnapshot = CatConfiguredEnvironmentProviderTest::MakeDaySnapshot();
	{
		CatConfiguredEnvironmentProviderTest::FEnvironmentSettingsOverride ClosedOverride(false);
		const FCatEnvironmentResult DefaultResult = Provider->EvaluateEnvironment(InputSnapshot, 1);
		TestFalse(TEXT("默认配置下 Provider 拒绝生成环境"), DefaultResult.bSucceeded);
		TestEqual(TEXT("默认配置失败时天气 Unknown"), DefaultResult.Snapshot.Weather, ECatEnvironmentWeather::Unknown);
	}

	{
		CatConfiguredEnvironmentProviderTest::FEnvironmentSettingsOverride Override(true);
		const FCatEnvironmentResult Result = Provider->EvaluateEnvironment(InputSnapshot, 7);
		TestTrue(TEXT("完整配置下 Provider 生成环境快照"), Result.bSucceeded);
		TestEqual(TEXT("天气来自显式配置"), Result.Snapshot.Weather, ECatEnvironmentWeather::Rain);
		TestEqual(TEXT("时段由 Run 时钟解析"), Result.Snapshot.TimeOfDay, ECatEnvironmentTimeOfDay::Morning);
		TestTrue(TEXT("显式自然事件进入快照"), Result.Snapshot.bHasActiveEvent);
		TestEqual(TEXT("事件 ID 来自配置"), Result.Snapshot.ActiveEventId, FName(TEXT("StormBloom")));
		TestEqual(TEXT("环境快照记录来源 Run Revision"), Result.Snapshot.SourceRunRevision, int64(7));
	}
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
