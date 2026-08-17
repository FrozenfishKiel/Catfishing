#include "Data/CatFishCatalogSettings.h"

#include "Data/CatFishDefinition.h"
#include "Curves/CurveFloat.h"

namespace CatFishCatalogSettingsPrivate
{
	static bool PassesGate(const UCatFishDefinition& Definition, const FName RegionId,
		const ECatEnvironmentTimeOfDay TimeOfDay, const ECatEnvironmentWeather Weather,
		const int32 ActivePlayerCount, const double CombinedFishingStrength,
		const double CombinedFightStamina)
	{
		return Definition.IsRuntimeDefinitionReady() && Definition.RegionIds.Contains(RegionId)
			&& Definition.TimeOfDay.Contains(TimeOfDay) && Definition.Weather.Contains(Weather)
			&& Definition.MinimumFightParticipants <= ActivePlayerCount
			&& Definition.FishStrength <= CombinedFishingStrength
			&& Definition.FishFightStamina <= CombinedFightStamina;
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
		MaximumChumModifier))
	{
		return Result;
	}
	struct FCandidate
	{
		UCatFishDefinition* Definition = nullptr;
		double FinalWeight = 0.0;
	};
	TArray<FCandidate> Candidates;
	for (const TSoftObjectPtr<UCatFishDefinition>& DefinitionRef : Definitions)
	{
		UCatFishDefinition* Definition = DefinitionRef.LoadSynchronous();
		if (!Definition || !CatFishCatalogSettingsPrivate::PassesGate(*Definition,
			Context.WaterRegion.RegionId, Context.TimeOfDay, Context.Weather, Context.ActivePlayerCount,
			Context.CombinedFishingStrength, Context.CombinedFightStamina))
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
		const double FinalWeight = Definition->SpawnWeight * ChumModifier * BaitModifier;
		if (!FMath::IsFinite(FinalWeight) || FinalWeight <= 0.0)
		{
			continue;
		}
		Candidates.Add({Definition, FinalWeight});
	}
	Candidates.Sort([](const FCandidate& Left, const FCandidate& Right)
	{
		return Left.Definition->FishDefinitionId.LexicalLess(Right.Definition->FishDefinitionId);
	});
	double TotalWeight = 0.0;
	for (const FCandidate& Candidate : Candidates)
	{
		TotalWeight += Candidate.FinalWeight;
	}
	if (Candidates.IsEmpty() || !FMath::IsFinite(TotalWeight) || TotalWeight <= 0.0)
	{
		return Result;
	}
	FRandomStream Random(Context.RandomSeed);
	double Cursor = Random.FRandRange(0.0f, static_cast<float>(TotalWeight));
	const FCandidate* Selected = &Candidates.Last();
	for (const FCandidate& Candidate : Candidates)
	{
		Cursor -= Candidate.FinalWeight;
		if (Cursor <= 0.0)
		{
			Selected = &Candidate;
			break;
		}
	}
	Result.bSelected = true;
	Result.FishDefinitionId = Selected->Definition->FishDefinitionId;
	Result.SelectedFinalWeight = Selected->FinalWeight;
	Result.WeightKilograms = Random.FRandRange(
		static_cast<float>(Selected->Definition->MinimumWeightKilograms),
		static_cast<float>(Selected->Definition->MaximumWeightKilograms));
	return Result;
}
