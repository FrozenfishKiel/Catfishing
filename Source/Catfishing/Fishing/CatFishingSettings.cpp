#include "Fishing/CatFishingSettings.h"

#include "Data/CatFishDefinition.h"
#include "Data/CatFishPersonalityDefinition.h"
#include "Fishing/Config/CatFishingFightBalanceDefinition.h"
#include "Fishing/Simulation/CatFishBehaviorProfile.h"
#include "Fishing/Simulation/CatFishingBiteTimingModel.h"
#include "Logging/CatLog.h"

namespace
{
	void WarnInvalidFishingTuningOnce(const FName Property)
	{
		static TSet<FName> WarnedProperties;
		if (!WarnedProperties.Contains(Property))
		{
			WarnedProperties.Add(Property);
			UE_LOG(LogCatFishing, Warning,
				TEXT("Event=fishing_tuning_invalid Property=%s Source=CatFishingSettings Result=LegacyDefault"),
				*Property.ToString());
		}
	}
	double ResolveFishingTuning(const FName Property, const double Value, const double Minimum, const double Fallback)
	{
		if (FMath::IsFinite(Value) && Value >= Minimum) return Value;
		WarnInvalidFishingTuningOnce(Property);
		return Fallback;
	}
}

double UCatFishingSettings::GetExhaustedFishRevivalSeconds() const
{
	return ResolveFishingTuning(TEXT("ExhaustedFishRevivalSeconds"), ExhaustedFishRevivalSeconds, UE_SMALL_NUMBER, 30.0);
}
double UCatFishingSettings::GetCatchCompletionRodWearPoints() const
{
	return ResolveFishingTuning(TEXT("CatchCompletionRodWearPoints"), CatchCompletionRodWearPoints, 0.0, 1.0);
}
double UCatFishingSettings::GetOverpowerFlingDistanceCentimeters() const
{
	return ResolveFishingTuning(TEXT("OverpowerFlingDistanceCentimeters"), OverpowerFlingDistanceCentimeters, 0.0, 250.0);
}
double UCatFishingSettings::GetOverpowerStrengthRatio() const
{
	return ResolveFishingTuning(TEXT("OverpowerStrengthRatio"), OverpowerStrengthRatio, 1.0, 2.0);
}
int32 UCatFishingSettings::GetMaximumDeployedRodsPerPlayer() const
{
	if (MaximumDeployedRodsPerPlayer > 0) return MaximumDeployedRodsPerPlayer;
	WarnInvalidFishingTuningOnce(TEXT("MaximumDeployedRodsPerPlayer"));
	return 2;
}
const TArray<double>& UCatFishingSettings::GetOverpowerLandingDistanceFractions() const
{
	bool bValid = OverpowerLandingDistanceFractions.Num() >= 2
		&& OverpowerLandingDistanceFractions[0] == 1.0 && OverpowerLandingDistanceFractions.Last() == 0.0;
	double Previous = 1.0;
	for (const double Fraction : OverpowerLandingDistanceFractions)
	{
		bValid &= FMath::IsFinite(Fraction) && Fraction >= 0.0 && Fraction <= Previous;
		Previous = Fraction;
	}
	if (bValid) return OverpowerLandingDistanceFractions;
	WarnInvalidFishingTuningOnce(TEXT("OverpowerLandingDistanceFractions"));
	static const TArray<double> LegacyDefaults = {1.0, 2.0 / 3.0, 1.0 / 3.0, 0.0};
	return LegacyDefaults;
}

// 运行 gate 流程：要求产品显式开启总开关、提供 StateTree 软引用、有限正响应窗/终态复制窗与近岸验证；任一为 Unset 都阻止会话创建。
bool UCatFishingSettings::IsRuntimeReady() const
{
	return bEnableFishingRuntime && !FishingSessionStateTree.IsNull() && !FishBehaviorStateTree.IsNull()
		&& FMath::IsFinite(TrueBiteWindowSeconds) && TrueBiteWindowSeconds > 0.0
		&& LoadFightBalanceDefinition()
		&& FMath::IsFinite(HeldRodMaximumAngularSpeedDegreesPerSecond)
		&& HeldRodMaximumAngularSpeedDegreesPerSecond > 0.0
		&& FMath::IsFinite(HeldRodAngularResistanceResponseSeconds)
		&& HeldRodAngularResistanceResponseSeconds > 0.0
		&& FMath::IsFinite(HeldRodFishPullSmoothingSeconds)
		&& HeldRodFishPullSmoothingSeconds > 0.0
		&& bEnableNearShoreValidation
		&& FMath::IsFinite(ScoopReachCentimeters) && ScoopReachCentimeters > 0.0
		&& FMath::IsFinite(TerminalReplicationWindowSeconds) && TerminalReplicationWindowSeconds > 0.0;
}

const UCatFishingFightBalanceDefinition* UCatFishingSettings::LoadFightBalanceDefinition() const
{
	const UCatFishingFightBalanceDefinition* Definition = FightBalanceDefinition.LoadSynchronous();
	return Definition && Definition->IsRuntimeDefinitionReady() ? Definition : nullptr;
}

const UCatBitePersonalityDefinition* UCatFishingSettings::FindBitePersonality(const FName PersonalityId) const
{
	if (PersonalityId.IsNone()) return nullptr;
	const UCatBitePersonalityDefinition* Match = nullptr;
	for (const TSoftObjectPtr<UCatBitePersonalityDefinition>& Entry : BitePersonalities)
	{
		const UCatBitePersonalityDefinition* Candidate = Entry.LoadSynchronous();
		if (Candidate && Candidate->BitePersonalityId == PersonalityId && Candidate->IsRuntimeDefinitionReady())
		{
			if (Match) return nullptr;
			Match = Candidate;
		}
	}
	return Match;
}

// 逐鱼行为参数解析流程：先取这条鱼的测试期模板（可能为空），再交给 Resolver 用鱼表四列逐列覆盖。
// 这里不做任何数值判断，只保证「鱼表优先、模板兜底」这条口径只有一个实现。
bool UCatFishingSettings::TryResolveFishBehavior(const UCatFishDefinition& FishDefinition,
	FCatFishResolvedBehavior& OutBehavior) const
{
	const UCatFightPersonalityDefinition* TestingTemplate = FindFightPersonality(FishDefinition.FightPersonalityId);
	return FCatFishBehaviorProfileResolver::Resolve(FishDefinition, TestingTemplate, OutBehavior);
}

const UCatFightPersonalityDefinition* UCatFishingSettings::FindFightPersonality(const FName PersonalityId) const
{
	if (PersonalityId.IsNone()) return nullptr;
	const UCatFightPersonalityDefinition* Match = nullptr;
	for (const TSoftObjectPtr<UCatFightPersonalityDefinition>& Entry : FightPersonalities)
	{
		const UCatFightPersonalityDefinition* Candidate = Entry.LoadSynchronous();
		if (Candidate && Candidate->FightPersonalityId == PersonalityId && Candidate->IsRuntimeDefinitionReady())
		{
			if (Match) return nullptr;
			Match = Candidate;
		}
	}
	return Match;
}

// 抢抄距离读取流程：先清输出，再复用完整 runtime gate 并读取有限正厘米值；调用方只能比较服务器 Character 与 StateTree 提供的权威目标。
bool UCatFishingSettings::TryGetScoopReach(double& OutReachCentimeters) const
{
	OutReachCentimeters = 0.0;
	if (!IsRuntimeReady())
	{
		return false;
	}
	OutReachCentimeters = ScoopReachCentimeters;
	return true;
}

bool UCatFishingSettings::TryGetScoopCooldown(double& OutCooldownSeconds) const
{
	OutCooldownSeconds = 0.0;
	if (!FMath::IsFinite(ScoopCooldownSeconds) || ScoopCooldownSeconds <= 0.0)
	{
		return false;
	}
	OutCooldownSeconds = ScoopCooldownSeconds;
	return true;
}

bool UCatFishingSettings::TryGetBiteWarning(double& OutWarningSeconds) const
{
	OutWarningSeconds = 0.0;
	if (!FMath::IsFinite(BiteWarningSeconds) || BiteWarningSeconds <= 0.0
		|| !FMath::IsFinite(MinimumBiteDelaySeconds) || MinimumBiteDelaySeconds < 0.0
		|| !FMath::IsFinite(MaximumBiteDelaySeconds)
		|| MaximumBiteDelaySeconds < MinimumBiteDelaySeconds + BiteWarningSeconds)
	{
		return false;
	}
	OutWarningSeconds = BiteWarningSeconds;
	return true;
}

bool UCatFishingSettings::TryGetBiteTimingParameters(FCatFishingBiteTimingParameters& OutParameters) const
{
	OutParameters = {};
	FCatFishingBiteTimingParameters Candidate;
	Candidate.NoChumMeanSeconds = NoChumMeanBiteDelaySeconds;
	Candidate.SingleChumMeanSeconds = SingleChumMeanBiteDelaySeconds;
	Candidate.FullChumMeanSeconds = FullChumMeanBiteDelaySeconds;
	Candidate.SingleChumContribution = SingleChumContribution;
	Candidate.FullChumContribution = FullChumContribution;
	Candidate.MinimumCalmSeconds = MinimumBiteDelaySeconds;
	Candidate.MaximumWaitSeconds = MaximumBiteDelaySeconds;
	if (!TryGetBiteWarning(Candidate.WarningSeconds) || !Candidate.IsValid()) return false;
	OutParameters = Candidate;
	return true;
}

// 终态留存读取流程：先清输出，再复用完整 runtime gate；只返回有限正秒数，调用方可用同一值设置 Actor lifespan。
bool UCatFishingSettings::TryGetTerminalReplicationWindow(double& OutWindowSeconds) const
{
	OutWindowSeconds = 0.0;
	if (!IsRuntimeReady())
	{
		return false;
	}
	OutWindowSeconds = TerminalReplicationWindowSeconds;
	return true;
}
