#include "Data/CatFishCatalogSettings.h"

#include "Data/CatFishDefinition.h"
#include "Curves/CurveFloat.h"

namespace CatFishCatalogSettingsPrivate
{
	enum class EChallengeBand : uint8
	{
		Comfort,
		Matched,
		Risky,
		Count
	};

	static bool PassesEcologicalGate(const UCatFishDefinition& Definition, const FName RegionId,
		const ECatEnvironmentTimeOfDay TimeOfDay, const ECatEnvironmentWeather Weather,
		const int32 ActivePlayerCount)
	{
		return Definition.IsRuntimeDefinitionReady() && Definition.RegionIds.Contains(RegionId)
			&& Definition.TimeOfDay.Contains(TimeOfDay) && Definition.Weather.Contains(Weather)
			&& Definition.MinimumFightParticipants <= ActivePlayerCount;
	}

	static bool IsChallengeSelectionReady(const UCatFishCatalogSettings& Settings)
	{
		const double BandWeightTotal = Settings.ComfortChallengeBandWeight
			+ Settings.MatchedChallengeBandWeight + Settings.RiskyChallengeBandWeight;
		return FMath::IsFinite(Settings.ComfortChallengeMaximumRatio)
			&& Settings.ComfortChallengeMaximumRatio > 0.0
			&& FMath::IsFinite(Settings.MatchedChallengeMaximumRatio)
			&& Settings.MatchedChallengeMaximumRatio > Settings.ComfortChallengeMaximumRatio
			&& FMath::IsFinite(Settings.MaximumChallengeRatio)
			&& Settings.MaximumChallengeRatio > Settings.MatchedChallengeMaximumRatio
			&& FMath::IsFinite(Settings.TargetChallengeRatio)
			&& Settings.TargetChallengeRatio > 0.0
			&& Settings.TargetChallengeRatio <= Settings.MaximumChallengeRatio
			&& FMath::IsFinite(Settings.ComfortChallengeBandWeight)
			&& Settings.ComfortChallengeBandWeight >= 0.0
			&& FMath::IsFinite(Settings.MatchedChallengeBandWeight)
			&& Settings.MatchedChallengeBandWeight >= 0.0
			&& FMath::IsFinite(Settings.RiskyChallengeBandWeight)
			&& Settings.RiskyChallengeBandWeight >= 0.0
			&& FMath::IsFinite(BandWeightTotal) && BandWeightTotal > 0.0
			&& FMath::IsFinite(Settings.MinimumChallengeWeightMultiplier)
			&& Settings.MinimumChallengeWeightMultiplier > 0.0
			&& Settings.MinimumChallengeWeightMultiplier <= 1.0;
	}

	static double CalculateChallengeRatio(const UCatFishDefinition& Definition,
		const double CombinedFishingStrength, const double CombinedFightStamina)
	{
		const double StrengthRatio = Definition.FishStrength / CombinedFishingStrength;
		const double StaminaRatio = Definition.FishFightStamina / CombinedFightStamina;
		// 力量决定瞬时拖落/碾压出口，不能被低体力稀释；体力只有在鱼也具备相称力量时才构成持续挑战。
		// 调和均值会把“高体力、极低力量”的耐打木桩压回轻松带，避免它占据势均力敌带后又被力量规则秒杀。
		const double BalancedRatio = (2.0 * StrengthRatio * StaminaRatio)
			/ (StrengthRatio + StaminaRatio);
		return FMath::Max(StrengthRatio, BalancedRatio);
	}

	static EChallengeBand ResolveChallengeBand(const double ChallengeRatio,
		const UCatFishCatalogSettings& Settings)
	{
		if (ChallengeRatio <= Settings.ComfortChallengeMaximumRatio)
		{
			return EChallengeBand::Comfort;
		}
		if (ChallengeRatio <= Settings.MatchedChallengeMaximumRatio)
		{
			return EChallengeBand::Matched;
		}
		return EChallengeBand::Risky;
	}

	static double CalculateChallengeModifier(const double ChallengeRatio,
		const UCatFishCatalogSettings& Settings)
	{
		const double AvailableDistance = ChallengeRatio <= Settings.TargetChallengeRatio
			? Settings.TargetChallengeRatio
			: Settings.MaximumChallengeRatio - Settings.TargetChallengeRatio;
		const double NormalizedDistance = AvailableDistance <= UE_DOUBLE_SMALL_NUMBER
			? 0.0
			: FMath::Clamp(FMath::Abs(ChallengeRatio - Settings.TargetChallengeRatio)
				/ AvailableDistance, 0.0, 1.0);
		return FMath::Lerp(1.0, Settings.MinimumChallengeWeightMultiplier, NormalizedDistance);
	}

	static double GetConfiguredBandWeight(const EChallengeBand Band,
		const UCatFishCatalogSettings& Settings)
	{
		switch (Band)
		{
		case EChallengeBand::Comfort:
			return Settings.ComfortChallengeBandWeight;
		case EChallengeBand::Matched:
			return Settings.MatchedChallengeBandWeight;
		case EChallengeBand::Risky:
			return Settings.RiskyChallengeBandWeight;
		default:
			return 0.0;
		}
	}

	static bool IsSaturationReady(const UCurveFloat* Curve, const double HalfSaturation,
		const double MaximumModifier)
	{
		if (!Curve || !FMath::IsFinite(HalfSaturation) || HalfSaturation <= 0.0
			|| !FMath::IsFinite(MaximumModifier) || MaximumModifier < 1.0)
		{
			return false;
		}
		double Previous = Curve->GetFloatValue(0.0f);
		if (!FMath::IsFinite(Previous) || !FMath::IsNearlyEqual(Previous, 1.0, UE_DOUBLE_SMALL_NUMBER)
			|| Previous < 0.0 || Previous > MaximumModifier)
		{
			return false;
		}
		for (int32 Index = 1; Index <= 64; ++Index)
		{
			const double Value = Curve->GetFloatValue(static_cast<float>(Index) / 64.0f);
			if (!FMath::IsFinite(Value) || Value + UE_DOUBLE_SMALL_NUMBER < Previous
				|| Value < 0.0 || Value > MaximumModifier)
			{
				return false;
			}
			Previous = Value;
		}
		return true;
	}
}

// ID 查询流程：遍历显式清单并同步解析定义；只接受唯一完整 ID，重复命中立即返回空以阻止数据冲突进入事务。
UCatFishDefinition* UCatFishCatalogSettings::FindRuntimeDefinition(const FName FishDefinitionId) const
{
	UCatFishDefinition* Match = nullptr;
	for (const TSoftObjectPtr<UCatFishDefinition>& DefinitionRef : Definitions)
	{
		UCatFishDefinition* Definition = DefinitionRef.LoadSynchronous();
		if (!Definition || !Definition->IsRuntimeDefinitionReady() || Definition->FishDefinitionId != FishDefinitionId)
		{
			continue;
		}
		if (Match)
		{
			return nullptr;
		}
		Match = Definition;
	}
	return Match;
}

FCatFishSelectionResult UCatFishCatalogSettings::SelectRuntimeDefinition(
	const FCatFishSelectionContext& Context) const
{
	FCatFishSelectionResult Result;
	if (!Context.WaterRegion.IsValid() || !Context.ChumSample.bSucceeded
		|| !(Context.ChumSample.WaterRegion == Context.WaterRegion)
		|| Context.TimeOfDay == ECatEnvironmentTimeOfDay::Unknown
		|| Context.Weather == ECatEnvironmentWeather::Unknown
		|| Context.ActivePlayerCount < 1 || Context.ActivePlayerCount > 8
		|| !FMath::IsFinite(Context.CombinedFishingStrength) || Context.CombinedFishingStrength <= 0.0
		|| !FMath::IsFinite(Context.CombinedFightStamina) || Context.CombinedFightStamina <= 0.0)
	{
		return Result;
	}
	UCurveFloat* SaturationCurve = ChumSaturationCurve.LoadSynchronous();
	if (!CatFishCatalogSettingsPrivate::IsSaturationReady(SaturationCurve, ChumAffinityHalfSaturation,
		MaximumChumModifier) || !CatFishCatalogSettingsPrivate::IsChallengeSelectionReady(*this))
	{
		return Result;
	}
	struct FCandidate
	{
		UCatFishDefinition* Definition = nullptr;
		double FinalWeight = 0.0;
		CatFishCatalogSettingsPrivate::EChallengeBand ChallengeBand =
			CatFishCatalogSettingsPrivate::EChallengeBand::Comfort;
	};
	TArray<FCandidate> Candidates;
	for (const TSoftObjectPtr<UCatFishDefinition>& DefinitionRef : Definitions)
	{
		UCatFishDefinition* Definition = DefinitionRef.LoadSynchronous();
		if (!Definition || !CatFishCatalogSettingsPrivate::PassesEcologicalGate(*Definition,
			Context.WaterRegion.RegionId, Context.TimeOfDay, Context.Weather, Context.ActivePlayerCount))
		{
			continue;
		}
		const double ChallengeRatio = CatFishCatalogSettingsPrivate::CalculateChallengeRatio(*Definition,
			Context.CombinedFishingStrength, Context.CombinedFightStamina);
		if (!FMath::IsFinite(ChallengeRatio) || ChallengeRatio <= 0.0
			|| ChallengeRatio > MaximumChallengeRatio)
		{
			continue;
		}
		const double RawAffinity = Context.ChumSample.EffectiveChumVector.Fishy * Definition->ChumPreference.Fishy
			+ Context.ChumSample.EffectiveChumVector.Fragrant * Definition->ChumPreference.Fragrant
			+ Context.ChumSample.EffectiveChumVector.Fermented * Definition->ChumPreference.Fermented;
		if (!FMath::IsFinite(RawAffinity))
		{
			return FCatFishSelectionResult();
		}
		const double Normalized = RawAffinity <= 0.0 ? 0.0
			: RawAffinity / (RawAffinity + ChumAffinityHalfSaturation);
		const double ChumModifier = FMath::Clamp(
			static_cast<double>(SaturationCurve->GetFloatValue(static_cast<float>(Normalized))),
			0.0, MaximumChumModifier);
		const double BaitModifier = Definition->FindBaitMultiplierOrNeutral(Context.BaitDefinitionId);
		const double ChallengeModifier = CatFishCatalogSettingsPrivate::CalculateChallengeModifier(
			ChallengeRatio, *this);
		const double FinalWeight = Definition->SpawnWeight * ChumModifier * BaitModifier * ChallengeModifier;
		if (!FMath::IsFinite(FinalWeight) || FinalWeight <= 0.0)
		{
			continue;
		}
		Candidates.Add({Definition, FinalWeight,
			CatFishCatalogSettingsPrivate::ResolveChallengeBand(ChallengeRatio, *this)});
	}
	Candidates.Sort([](const FCandidate& Left, const FCandidate& Right)
	{
		return Left.Definition->FishDefinitionId.LexicalLess(Right.Definition->FishDefinitionId);
	});
	bool bBandHasCandidates[static_cast<uint8>(CatFishCatalogSettingsPrivate::EChallengeBand::Count)] = {};
	for (const FCandidate& Candidate : Candidates)
	{
		bBandHasCandidates[static_cast<uint8>(Candidate.ChallengeBand)] = true;
	}
	if (Candidates.IsEmpty())
	{
		return Result;
	}
	FRandomStream Random(Context.RandomSeed);
	double AvailableBandWeights[static_cast<uint8>(CatFishCatalogSettingsPrivate::EChallengeBand::Count)] = {};
	double TotalBandWeight = 0.0;
	for (uint8 Index = 0; Index < static_cast<uint8>(CatFishCatalogSettingsPrivate::EChallengeBand::Count);
		++Index)
	{
		if (bBandHasCandidates[Index])
		{
			AvailableBandWeights[Index] = CatFishCatalogSettingsPrivate::GetConfiguredBandWeight(
				static_cast<CatFishCatalogSettingsPrivate::EChallengeBand>(Index), *this);
			TotalBandWeight += AvailableBandWeights[Index];
		}
	}
	// 若当前生态上下文里只存在配置权重为 0 的带，仍回退到现有带，避免有合法鱼却空钩。
	if (!FMath::IsFinite(TotalBandWeight) || TotalBandWeight <= 0.0)
	{
		TotalBandWeight = 0.0;
		for (uint8 Index = 0; Index < static_cast<uint8>(CatFishCatalogSettingsPrivate::EChallengeBand::Count);
			++Index)
		{
			AvailableBandWeights[Index] = bBandHasCandidates[Index] ? 1.0 : 0.0;
			TotalBandWeight += AvailableBandWeights[Index];
		}
	}
	double BandCursor = Random.FRandRange(0.0f, static_cast<float>(TotalBandWeight));
	CatFishCatalogSettingsPrivate::EChallengeBand SelectedBand =
		CatFishCatalogSettingsPrivate::EChallengeBand::Comfort;
	for (uint8 Index = 0; Index < static_cast<uint8>(CatFishCatalogSettingsPrivate::EChallengeBand::Count);
		++Index)
	{
		BandCursor -= AvailableBandWeights[Index];
		if (BandCursor <= 0.0 && AvailableBandWeights[Index] > 0.0)
		{
			SelectedBand = static_cast<CatFishCatalogSettingsPrivate::EChallengeBand>(Index);
			break;
		}
	}
	double TotalCandidateWeight = 0.0;
	for (const FCandidate& Candidate : Candidates)
	{
		if (Candidate.ChallengeBand == SelectedBand)
		{
			TotalCandidateWeight += Candidate.FinalWeight;
		}
	}
	if (!FMath::IsFinite(TotalCandidateWeight) || TotalCandidateWeight <= 0.0)
	{
		return Result;
	}
	double Cursor = Random.FRandRange(0.0f, static_cast<float>(TotalCandidateWeight));
	const FCandidate* Selected = nullptr;
	for (const FCandidate& Candidate : Candidates)
	{
		if (Candidate.ChallengeBand != SelectedBand)
		{
			continue;
		}
		Selected = &Candidate; // 同带最后一个候选也是浮点游标落在尾端时的确定性回退。
		Cursor -= Candidate.FinalWeight;
		if (Cursor <= 0.0)
		{
			break;
		}
	}
	if (!Selected)
	{
		return Result;
	}
	Result.bSelected = true;
	Result.FishDefinitionId = Selected->Definition->FishDefinitionId;
	Result.SelectedFinalWeight = Selected->FinalWeight;
	Result.WeightKilograms = Random.FRandRange(
		static_cast<float>(Selected->Definition->MinimumWeightKilograms),
		static_cast<float>(Selected->Definition->MaximumWeightKilograms));
	return Result;
}
