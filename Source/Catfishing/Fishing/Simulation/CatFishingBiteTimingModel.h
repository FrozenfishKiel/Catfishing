#pragma once

#include "CoreMinimal.h"

/** 等待配置的瞬时输入；均值包含慢浮与完整预警，基准鱼饵的两个倍率均为 1。 */
struct FCatFishingBiteTimingParameters
{
	double NoChumMeanSeconds = 0.0;
	double SingleChumMeanSeconds = 0.0;
	double FullChumMeanSeconds = 0.0;
	double SingleChumContribution = 0.0;
	double FullChumContribution = 0.0;
	double MinimumCalmSeconds = 0.0;
	double MaximumWaitSeconds = 0.0;
	double WarningSeconds = 0.0;

	bool IsValid() const;
};

/** 已校准并施加鱼饵倍率的分布，不持有随机流、World、计时器或复制状态。 */
struct FCatFishingBiteTimingDistribution
{
	double NeutralMeanSeconds = 0.0;
	double ExpectedMeanSeconds = 0.0;
	double RatePerSecond = 0.0;
	double MinimumCalmSeconds = 0.0;
	double MaximumWaitSeconds = 0.0;
	double WarningSeconds = 0.0;

	bool TrySample(double UnitRandom, double& OutWaitSeconds) const;
};

/** 有效窝料贡献 → 目标均值 → 截顶指数分布；调用者拥有采样时间与随机种子。 */
class FCatFishingBiteTimingModel
{
public:
	static bool BuildDistribution(const FCatFishingBiteTimingParameters& Parameters,
		double EffectiveChumContribution, double BaitRateMultiplier, double BaitMinimumDelayMultiplier,
		FCatFishingBiteTimingDistribution& OutDistribution);
};
