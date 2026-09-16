#pragma once
#include "CoreMinimal.h"

/** 浓度不是鱼数；K 与采样三轴总贡献使用同一单位。 */
struct FCatFishingBiteTimingParameters
{
    double UnchummedIntervalSeconds = 120.0;
    double ChummedBaseIntervalSeconds = 15.0;
    double ConcentrationScale = 17.0 / 3.0;
    double WarningSeconds = 1.5;
    bool IsValid() const;
};

/** 确定性等待，无随机分布、库存扣减、浓度饱和或玩法等待下限。 */
class FCatFishingBiteTimingModel
{
public:
    static bool TryComputeInterval(const FCatFishingBiteTimingParameters& Parameters,
        double EffectiveConcentration, double IntervalMultiplier, double& OutSeconds);
};

/** 每漂独立进度；LastServerTime 可以是未来的落水时刻，飞行期间不累计。 */
struct FCatFishingBiteWaitProgress
{
    double RemainingFraction = 1.0;
    double IntervalSeconds = 0.0;
    double LastServerTime = 0.0;
    bool Advance(double NowServerTime, double NewIntervalSeconds);
    double RemainingSeconds(double NowServerTime) const;
};
