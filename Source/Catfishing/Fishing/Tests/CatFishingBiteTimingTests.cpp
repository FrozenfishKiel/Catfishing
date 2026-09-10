#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/Simulation/CatFishingBiteTimingModel.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingBiteTimingMeansTest,
	"Catfishing.Unit.Fishing.BiteTiming.ConfiguredMeansAndSeededDistribution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingBiteTimingMeansTest::RunTest(const FString& Parameters)
{
	FCatFishingBiteTimingParameters Timing;
	if (!TestTrue(TEXT("正式配置可读取"), GetDefault<UCatFishingSettings>()->TryGetBiteTimingParameters(Timing))) return false;
	const double Means[] = {20.0, 14.0, 12.0, 10.0, 8.0, 6.0};
	for (int32 Portions = 0; Portions <= 5; ++Portions)
	{
		FCatFishingBiteTimingDistribution Distribution;
		if (!TestTrue(TEXT("份数对应有效贡献可构建"), FCatFishingBiteTimingModel::BuildDistribution(
			Timing, Portions * 1.7, 1.0, 1.0, Distribution))) return false;
		TestEqual(TEXT("目标包含慢浮和预警"), Distribution.ExpectedMeanSeconds, Means[Portions], 1.e-8);
		FRandomStream Random(74193), Replay(74193);
		double Sum = 0.0, Minimum = 40.0, Maximum = 0.0;
		bool bSamplesValid = true, bDeterministic = true;
		constexpr int32 SampleCount = 100000;
		for (int32 Sample = 0; Sample < SampleCount; ++Sample)
		{
			double Wait = 0.0, ReplayedWait = 0.0;
			bSamplesValid &= Distribution.TrySample(Random.FRand(), Wait);
			bSamplesValid &= Distribution.TrySample(Replay.FRand(), ReplayedWait);
			bDeterministic &= Wait == ReplayedWait;
			Sum += Wait;
			Minimum = FMath::Min(Minimum, Wait);
			Maximum = FMath::Max(Maximum, Wait);
		}
		TestTrue(TEXT("全部随机样本合法且可重放"), bSamplesValid && bDeterministic);
		TestTrue(TEXT("每次保留三秒慢浮和完整预警"), Minimum >= 4.5);
		TestTrue(TEXT("任何样本均不超过四十秒"), Maximum <= 40.0);
		TestEqual(TEXT("十万竿实采均值接近批准目标"), Sum / SampleCount, Means[Portions], 0.15);
		AddInfo(FString::Printf(TEXT("Portions=%d Samples=%d ObservedMean=%.6f Target=%.1f Min=%.6f Max=%.6f"),
			Portions, SampleCount, Sum / SampleCount, Means[Portions], Minimum, Maximum));
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingBiteTimingCurveTest,
	"Catfishing.Unit.Fishing.BiteTiming.DecaySaturationAndBaitContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingBiteTimingCurveTest::RunTest(const FString& Parameters)
{
	FCatFishingBiteTimingParameters Timing;
	if (!GetDefault<UCatFishingSettings>()->TryGetBiteTimingParameters(Timing)) return false;
	double PreviousMean = 20.0;
	bool bMonotone = true;
	for (int32 Step = 0; Step <= 1000; ++Step)
	{
		FCatFishingBiteTimingDistribution Distribution;
		if (!FCatFishingBiteTimingModel::BuildDistribution(Timing, Step * 0.01, 1.0, 1.0, Distribution)) return false;
		bMonotone &= Distribution.ExpectedMeanSeconds <= PreviousMean + 1.e-9;
		PreviousMean = Distribution.ExpectedMeanSeconds;
	}
	TestTrue(TEXT("衰减连续回退、加窝不会变慢"), bMonotone);
	TestEqual(TEXT("超满窝饱和为六秒"), PreviousMean, 6.0, 1.e-8);
	FCatFishingBiteTimingDistribution Neutral, Bait, Left, Right;
	FCatFishingBiteTimingModel::BuildDistribution(Timing, 1.7, 1.0, 1.0, Neutral);
	FCatFishingBiteTimingModel::BuildDistribution(Timing, 1.7, 2.0, 0.5, Bait);
	TestEqual(TEXT("鱼饵仍乘到校准后的频率"), Bait.RatePerSecond, Neutral.RatePerSecond * 2.0);
	TestEqual(TEXT("鱼饵仍单独缩放慢浮下限"), Bait.MinimumCalmSeconds, 1.5);
	TestEqual(TEXT("鱼饵不缩短预警"), Bait.WarningSeconds, 1.5);
	TestTrue(TEXT("提速鱼饵进一步减少等待均值"), Bait.ExpectedMeanSeconds < Neutral.ExpectedMeanSeconds);
	for (const double Knot : {1.7, 8.5})
	{
		FCatFishingBiteTimingModel::BuildDistribution(Timing, Knot - 1.e-6, 1.0, 1.0, Left);
		FCatFishingBiteTimingModel::BuildDistribution(Timing, Knot + 1.e-6, 1.0, 1.0, Right);
		TestEqual(TEXT("贡献跨过锚点不会跳变"), Left.ExpectedMeanSeconds, Right.ExpectedMeanSeconds, 1.e-5);
	}
	double Wait = 0.0;
	TestTrue(TEXT("随机零端点合法"), Neutral.TrySample(0.0, Wait));
	TestEqual(TEXT("零端点保留完整下限"), Wait, 4.5);
	TestTrue(TEXT("随机一端点合法"), Neutral.TrySample(1.0, Wait));
	TestEqual(TEXT("一端点安全截顶"), Wait, 40.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingBiteTimingInvalidTest,
	"Catfishing.Unit.Fishing.BiteTiming.InvalidInputsFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingBiteTimingInvalidTest::RunTest(const FString& Parameters)
{
	FCatFishingBiteTimingParameters Timing;
	if (!GetDefault<UCatFishingSettings>()->TryGetBiteTimingParameters(Timing)) return false;
	FCatFishingBiteTimingDistribution Distribution;
	for (const double Invalid : {-1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
	{
		TestFalse(TEXT("非法贡献拒绝"), FCatFishingBiteTimingModel::BuildDistribution(Timing, Invalid, 1.0, 1.0, Distribution));
		TestFalse(TEXT("非法鱼饵频率拒绝"), FCatFishingBiteTimingModel::BuildDistribution(Timing, 0.0, Invalid, 1.0, Distribution));
		TestFalse(TEXT("非法鱼饵下限拒绝"), FCatFishingBiteTimingModel::BuildDistribution(Timing, 0.0, 1.0, Invalid, Distribution));
		TestEqual(TEXT("失败不泄漏上一次分布"), Distribution.RatePerSecond, 0.0);
	}
	for (const double InvalidMean : {4.5, 0.0, 21.0, std::numeric_limits<double>::quiet_NaN()})
	{
		auto InvalidTiming = Timing;
		InvalidTiming.FullChumMeanSeconds = InvalidMean;
		TestFalse(TEXT("不可达或不单调目标拒绝"), InvalidTiming.IsValid());
	}
	TestFalse(TEXT("零鱼饵频率拒绝"), FCatFishingBiteTimingModel::BuildDistribution(Timing, 0.0, 0.0, 1.0, Distribution));
	TestFalse(TEXT("下限加预警超过上限拒绝"), FCatFishingBiteTimingModel::BuildDistribution(Timing, 0.0, 1.0, 20.0, Distribution));
	FCatFishingBiteTimingModel::BuildDistribution(Timing, 0.0, 1.0, 1.0, Distribution);
	for (const double Invalid : {-0.1, 1.1, std::numeric_limits<double>::quiet_NaN()})
	{
		double Wait = 99.0;
		TestFalse(TEXT("非法随机输入拒绝"), Distribution.TrySample(Invalid, Wait));
		TestEqual(TEXT("非法样本输出清零"), Wait, 0.0);
	}
	return !HasAnyErrors();
}

#endif
