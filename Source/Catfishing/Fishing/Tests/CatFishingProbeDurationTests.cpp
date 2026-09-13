#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Data/CatFishDefinition.h"
#include "Data/CatFishPersonalityDefinition.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/CatFishingSettings.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingProbeDurationOverrideTest,
	"Catfishing.Unit.Fishing.BiteTiming.OptionalProbeOverrideUsesSeededSettingsRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingProbeDurationOverrideTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
	UCatFishingSettings* Settings = GetMutableDefault<UCatFishingSettings>();
	if (!TestFalse(TEXT("正式 Bite 目录已配置"), Settings->BitePersonalities.IsEmpty())) return false;
	auto* FormalBite = Settings->BitePersonalities[0].LoadSynchronous();
	if (!TestNotNull(TEXT("正式 Bite 可加载"), FormalBite)) return false;
	auto* Bite = NewObject<UCatBitePersonalityDefinition>();
	Bite->BitePersonalityId = FormalBite->BitePersonalityId;
	Bite->TrueBiteWindowSeconds = FormalBite->TrueBiteWindowSeconds;
	Bite->PerfectHookWindowSeconds = FormalBite->PerfectHookWindowSeconds;
	// 新资产只配置仍有效的响应窗；已退役完美倍率全部保持默认 0，不能再阻断选鱼。
	auto* Fish = NewObject<UCatFishDefinition>();
	Fish->BitePersonalityId = Bite->BitePersonalityId;
	TGuardValue<TArray<TSoftObjectPtr<UCatBitePersonalityDefinition>>> CatalogGuard(Settings->BitePersonalities, {Bite});
	TGuardValue<FVector2D> RangeGuard(Settings->ProbeDurationRangeSeconds, FVector2D(2.0, 4.0));
	auto* First = Wrapper.GetTestWorld()->SpawnActor<ACatFishingSession>();
	auto* Replay = Wrapper.GetTestWorld()->SpawnActor<ACatFishingSession>();
	if (!First || !Replay) return false;
	First->FishDefinition = Fish;
	Replay->FishDefinition = Fish;

	// 正值刻意放在区间外，证明生产解析器真的使用覆盖值，而不是刚好落在随机区间。
	Bite->ProbeDurationSeconds = 6.25;
	TestTrue(TEXT("正覆盖通过资产就绪校验"), Bite->IsRuntimeDefinitionReady());
	TestTrue(TEXT("正式目录查找不会过滤正覆盖"), Settings->FindBitePersonality(Fish->BitePersonalityId) == Bite);
	double FirstSeconds = 0.0;
	TestTrue(TEXT("试探停留秒数可解析"), First->TryResolveProbeDurationSeconds(FirstSeconds));
	TestEqual(TEXT("传给试探计时器的秒数等于逐鱼正覆盖"), FirstSeconds, 6.25);

	Bite->ProbeDurationSeconds = 0.0;
	TestTrue(TEXT("未配覆盖仍是就绪资产"), Bite->IsRuntimeDefinitionReady());
	TestTrue(TEXT("正式目录保留未配覆盖资产，区间回退可达"), Settings->FindBitePersonality(Fish->BitePersonalityId) == Bite);
	for (const uint64 Seed : {0ull, 1ull, 42ull, 78629ull, 0x123456789abcdef0ull})
	{
		First->CurrentBiteRandomSeed = Seed;
		Replay->CurrentBiteRandomSeed = Seed;
		double ReplaySeconds = 0.0;
		TestTrue(TEXT("未配覆盖按参数区间解析"), First->TryResolveProbeDurationSeconds(FirstSeconds));
		TestTrue(TEXT("相同冻结种子的另一会话可以解析"), Replay->TryResolveProbeDurationSeconds(ReplaySeconds));
		TestTrue(TEXT("停留秒数在参数区间内"), FirstSeconds >= 2.0 && FirstSeconds <= 4.0);
		TestEqual(TEXT("同一冻结种子跨会话解析完全一致"), FirstSeconds, ReplaySeconds);
		double RepeatedSeconds = 0.0;
		First->TryResolveProbeDurationSeconds(RepeatedSeconds);
		TestEqual(TEXT("同会话重读不推进随机流"), FirstSeconds, RepeatedSeconds);
	}
	for (const double Invalid : {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
	{
		Bite->ProbeDurationSeconds = Invalid;
		TestFalse(TEXT("负数与非有限覆盖拒绝"), Bite->IsRuntimeDefinitionReady());
		TestNull(TEXT("错误覆盖不能进入选鱼目录"), Settings->FindBitePersonality(Fish->BitePersonalityId));
	}
	return !HasAnyErrors();
}

#endif
