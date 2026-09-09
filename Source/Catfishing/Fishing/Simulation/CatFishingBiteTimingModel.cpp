#include "Fishing/Simulation/CatFishingBiteTimingModel.h"

namespace CatFishingBiteTimingPrivate
{
	// E[min(Exp(rate), cap)] / cap = (1-exp(-x))/x，x=rate*cap。
	// 小 x 用展开式避免相减消去；x=0 也是二分求解的连续边界。
	static double MeanFraction(const double X)
	{
		if (X < 1.e-5) return 1.0 - X / 2.0 + X * X / 6.0 - X * X * X / 24.0;
		return (1.0 - FMath::Exp(-X)) / X;
	}
}

bool FCatFishingBiteTimingParameters::IsValid() const
{
	const double Floor = MinimumCalmSeconds + WarningSeconds;
	return FMath::IsFinite(NoChumMeanSeconds) && FMath::IsFinite(SingleChumMeanSeconds)
		&& FMath::IsFinite(FullChumMeanSeconds)
		&& FMath::IsFinite(SingleChumContribution) && SingleChumContribution > 0.0
		&& FMath::IsFinite(FullChumContribution) && FullChumContribution > SingleChumContribution
		&& FMath::IsFinite(MinimumCalmSeconds) && MinimumCalmSeconds >= 0.0
		&& FMath::IsFinite(WarningSeconds) && WarningSeconds > 0.0 && FMath::IsFinite(Floor)
		&& FMath::IsFinite(MaximumWaitSeconds) && MaximumWaitSeconds > NoChumMeanSeconds
		&& NoChumMeanSeconds >= SingleChumMeanSeconds && SingleChumMeanSeconds >= FullChumMeanSeconds
		&& FullChumMeanSeconds > Floor;
}

bool FCatFishingBiteTimingModel::BuildDistribution(const FCatFishingBiteTimingParameters& Parameters,
	const double EffectiveChumContribution, const double BaitRateMultiplier,
	const double BaitMinimumDelayMultiplier, FCatFishingBiteTimingDistribution& OutDistribution)
{
	OutDistribution = {};
	if (!Parameters.IsValid() || !FMath::IsFinite(EffectiveChumContribution) || EffectiveChumContribution < 0.0
		|| !FMath::IsFinite(BaitRateMultiplier) || BaitRateMultiplier <= 0.0
		|| !FMath::IsFinite(BaitMinimumDelayMultiplier) || BaitMinimumDelayMultiplier <= 0.0) return false;

	// 锚点使用采样贡献单位而非背包份数；衰减、重叠以及未来不同配方均走相同连续映射。
	const double Contribution = FMath::Min(EffectiveChumContribution, Parameters.FullChumContribution);
	const double TargetMean = Contribution <= Parameters.SingleChumContribution
		? FMath::Lerp(Parameters.NoChumMeanSeconds, Parameters.SingleChumMeanSeconds,
			Contribution / Parameters.SingleChumContribution)
		: FMath::Lerp(Parameters.SingleChumMeanSeconds, Parameters.FullChumMeanSeconds,
			(Contribution - Parameters.SingleChumContribution)
			/ (Parameters.FullChumContribution - Parameters.SingleChumContribution));
	const double NeutralFloor = Parameters.MinimumCalmSeconds + Parameters.WarningSeconds;
	const double NeutralCap = Parameters.MaximumWaitSeconds - NeutralFloor;
	const double TargetExtra = TargetMean - NeutralFloor;
	const double TargetFraction = TargetExtra / NeutralCap;
	double Lower = 0.0;
	double Upper = NeutralCap / TargetExtra;
	if (!FMath::IsFinite(Upper)) return false;
	// 每个咬钩机会只求解一次，避免把 1/目标均值误当成有固定下限和截顶时的频率。
	for (int32 Iteration = 0; Iteration < 64; ++Iteration)
	{
		const double Mid = (Lower + Upper) * 0.5;
		if (CatFishingBiteTimingPrivate::MeanFraction(Mid) > TargetFraction) Lower = Mid;
		else Upper = Mid;
	}
	FCatFishingBiteTimingDistribution Candidate;
	Candidate.NeutralMeanSeconds = TargetMean;
	Candidate.RatePerSecond = ((Lower + Upper) * 0.5 / NeutralCap) * BaitRateMultiplier;
	Candidate.MinimumCalmSeconds = Parameters.MinimumCalmSeconds * BaitMinimumDelayMultiplier;
	Candidate.WarningSeconds = Parameters.WarningSeconds;
	Candidate.MaximumWaitSeconds = Parameters.MaximumWaitSeconds;
	const double ActualFloor = Candidate.MinimumCalmSeconds + Candidate.WarningSeconds;
	const double ActualCap = Candidate.MaximumWaitSeconds - ActualFloor;
	if (!FMath::IsFinite(Candidate.RatePerSecond) || Candidate.RatePerSecond <= 0.0
		|| !FMath::IsFinite(ActualFloor) || !FMath::IsFinite(ActualCap) || ActualCap < 0.0) return false;
	Candidate.ExpectedMeanSeconds = ActualFloor
		+ ActualCap * CatFishingBiteTimingPrivate::MeanFraction(Candidate.RatePerSecond * ActualCap);
	if (!FMath::IsFinite(Candidate.ExpectedMeanSeconds)) return false;
	OutDistribution = Candidate;
	return true;
}

bool FCatFishingBiteTimingDistribution::TrySample(const double UnitRandom, double& OutWaitSeconds) const
{
	OutWaitSeconds = 0.0;
	const double Floor = MinimumCalmSeconds + WarningSeconds;
	if (!FMath::IsFinite(UnitRandom) || UnitRandom < 0.0 || UnitRandom > 1.0
		|| !FMath::IsFinite(RatePerSecond) || RatePerSecond <= 0.0
		|| !FMath::IsFinite(MinimumCalmSeconds) || MinimumCalmSeconds < 0.0
		|| !FMath::IsFinite(WarningSeconds) || WarningSeconds <= 0.0
		|| !FMath::IsFinite(Floor) || !FMath::IsFinite(MaximumWaitSeconds) || MaximumWaitSeconds < Floor) return false;
	const double Cap = MaximumWaitSeconds - Floor;
	const double Extra = UnitRandom == 1.0 ? Cap : -FMath::Loge(1.0 - UnitRandom) / RatePerSecond;
	OutWaitSeconds = FMath::Min(MaximumWaitSeconds, Floor + FMath::Min(Extra, Cap));
	return true;
}
