#include "Fishing/Simulation/CatFishingBiteTimingModel.h"

bool FCatFishingBiteTimingParameters::IsValid() const
{
    return FMath::IsFinite(UnchummedIntervalSeconds) && UnchummedIntervalSeconds > 0.0
        && FMath::IsFinite(ChummedBaseIntervalSeconds) && ChummedBaseIntervalSeconds > 0.0
        && FMath::IsFinite(ConcentrationScale) && ConcentrationScale > 0.0
        && FMath::IsFinite(WarningSeconds) && WarningSeconds > 0.0;
}

bool FCatFishingBiteTimingModel::TryComputeInterval(const FCatFishingBiteTimingParameters& Parameters,
    const double EffectiveConcentration, const double IntervalMultiplier, double& OutSeconds)
{
    OutSeconds = 0.0;
    if (!Parameters.IsValid() || !FMath::IsFinite(EffectiveConcentration) || EffectiveConcentration < 0.0
        || !FMath::IsFinite(IntervalMultiplier) || IntervalMultiplier <= 0.0) return false;
    const double Base = EffectiveConcentration > 0.0
        ? Parameters.ChummedBaseIntervalSeconds / (1.0 + EffectiveConcentration / Parameters.ConcentrationScale)
        : Parameters.UnchummedIntervalSeconds;
    OutSeconds = Base * IntervalMultiplier;
    return FMath::IsFinite(OutSeconds) && OutSeconds > 0.0;
}

bool FCatFishingBiteWaitProgress::Advance(const double NowServerTime, const double NewIntervalSeconds)
{
    if (!FMath::IsFinite(RemainingFraction) || RemainingFraction < 0.0 || RemainingFraction > 1.0
        || !FMath::IsFinite(NowServerTime) || !FMath::IsFinite(LastServerTime)
        || !FMath::IsFinite(IntervalSeconds) || IntervalSeconds <= 0.0
        || !FMath::IsFinite(NewIntervalSeconds) || NewIntervalSeconds <= 0.0) return false;
    RemainingFraction = FMath::Clamp(RemainingFraction
        - FMath::Max(0.0, NowServerTime - LastServerTime) / IntervalSeconds, 0.0, 1.0);
    LastServerTime = FMath::Max(LastServerTime, NowServerTime);
    IntervalSeconds = NewIntervalSeconds;
    return true;
}

double FCatFishingBiteWaitProgress::RemainingSeconds(const double NowServerTime) const
{
    return FMath::Max(0.0, LastServerTime - NowServerTime) + RemainingFraction * IntervalSeconds;
}
