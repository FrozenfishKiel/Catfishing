#include "Data/CatFishCatalogSettings.h"

#include "Data/CatFishDefinition.h"
#include "Logging/CatLog.h"

namespace CatFishCatalogSettingsPrivate
{
	// 2026-09-15：正式抽鱼不再读取挑战度。

	static bool PassesWaterRegionGate(const UCatFishDefinition& Definition, const FName RegionId)
	{
		return Definition.IsRuntimeDefinitionReady() && Definition.RegionIds.Contains(RegionId);
	}

	static double SampleIndividualWeight(const UCatFishDefinition& Definition,
		const FCatFishSelectionContext& Context)
	{
		// 每个鱼种使用独立稳定随机流，避免增删其他候选时改变本鱼个体重量；主随机流只负责鱼种抽取。
		const uint32 Seed = HashCombineFast(GetTypeHash(Context.RandomSeed),
			GetTypeHash(Definition.ItemId));
		FRandomStream WeightRandom(static_cast<int32>(Seed));
		return FMath::Min(Definition.MaximumWeightKilograms, (1.0 + Context.CatchWeightBonus)
			* WeightRandom.FRandRange(static_cast<float>(Definition.MinimumWeightKilograms),
			static_cast<float>(Definition.MaximumWeightKilograms)));
	}

	// 完美削减只允许落在 (0,1]：未配置、非有限或越界一律退回 1.0，保证配置缺失时只是"不削减"，不会放大鱼或把值清零。
	static double SanitizePerfectMultiplier(const double Multiplier)
	{
		return FMath::IsFinite(Multiplier) && Multiplier > 0.0 && Multiplier <= 1.0 ? Multiplier : 1.0;
	}


}

// ID 查询流程：遍历显式清单并同步解析定义；只接受唯一完整 ID，重复命中立即返回空以阻止数据冲突进入事务。
UCatFishDefinition* UCatFishCatalogSettings::FindRuntimeDefinition(const int32  ItemId) const
{
	UCatFishDefinition* Match = nullptr;
	for (const TSoftObjectPtr<UCatFishDefinition>& DefinitionRef : Definitions)
	{
		UCatFishDefinition* Definition = DefinitionRef.LoadSynchronous();
		if (!Definition || !Definition->IsRuntimeDefinitionReady() || Definition->ItemId != ItemId)
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

// 完美削减取值流程：只按本鱼稀有度在"稀有"与"普通"两套系数之间二选一；线长系数不分档，三项都不接受越界配置。
FCatPerfectHookReduction UCatFishCatalogSettings::ResolvePerfectHookReduction(
	const UCatFishDefinition& Definition) const
{
	const bool bRareTier = !Definition.RarityTierId.IsNone()
		&& RarePerfectHookRarityTierIds.Contains(Definition.RarityTierId);
	FCatPerfectHookReduction Reduction;
	Reduction.FishStrengthMultiplier = CatFishCatalogSettingsPrivate::SanitizePerfectMultiplier(
		bRareTier ? RarePerfectFishStrengthMultiplier : CommonPerfectFishStrengthMultiplier);
	Reduction.FishStaminaMultiplier = CatFishCatalogSettingsPrivate::SanitizePerfectMultiplier(
		bRareTier ? RarePerfectFishStaminaMultiplier : CommonPerfectFishStaminaMultiplier);
	Reduction.InitialLineLengthMultiplier = CatFishCatalogSettingsPrivate::SanitizePerfectMultiplier(
		PerfectInitialLineLengthMultiplier);
	return Reduction;
}

FCatFishBiteTimingDefaults UCatFishCatalogSettings::ResolveBiteTiming(const UCatFishDefinition& Definition) const
{
	FCatFishBiteTimingDefaults Result;
	Result.ProbeDurationSeconds = Definition.ProbeDurationSeconds;
	Result.TrueBiteWindowSeconds = Definition.TrueBiteWindowSeconds;
	if (const FCatFishBiteTimingDefaults* Override = BiteTimingOverridesByItemId.Find(Definition.ItemId))
	{
		if (Result.ProbeDurationSeconds == 0.0) Result.ProbeDurationSeconds = Override->ProbeDurationSeconds;
		if (Result.TrueBiteWindowSeconds == 0.0) Result.TrueBiteWindowSeconds = Override->TrueBiteWindowSeconds;
	}
	if (const FCatFishBiteTimingDefaults* Defaults = BiteTimingDefaultsByRarityTier.Find(Definition.RarityTierId))
	{
		if (Result.ProbeDurationSeconds == 0.0) Result.ProbeDurationSeconds = Defaults->ProbeDurationSeconds;
		if (Result.TrueBiteWindowSeconds == 0.0) Result.TrueBiteWindowSeconds = Defaults->TrueBiteWindowSeconds;
	}
	return Result;
}

// 两步抽鱼：先按窝料三轴占比冻结类别，再只在该类的合法候选中按鱼饵权重抽鱼。
// 人数与生态条件仍是准入门；猫的力量/体力、鱼的稀有度和旧 SpawnWeight 不参与概率。
FCatFishSelectionResult UCatFishCatalogSettings::SelectRuntimeDefinition(
    const FCatFishSelectionContext& Context) const
{
    FCatFishSelectionResult Result;
    const FCatChumVector& Chum = Context.ChumSample.EffectiveChumVector;
    if (!FMath::IsFinite(Context.CatchWeightBonus) || Context.CatchWeightBonus < 0.0
        || !Context.WaterRegion.IsValid() || !Context.ChumSample.bSucceeded
        || !(Context.ChumSample.WaterRegion == Context.WaterRegion)
        || Context.ActivePlayerCount < 1 || Context.ActivePlayerCount > 8
        || !FMath::IsFinite(Chum.Fishy) || Chum.Fishy < 0.0
        || !FMath::IsFinite(Chum.Fragrant) || Chum.Fragrant < 0.0
        || !FMath::IsFinite(Chum.Fermented) || Chum.Fermented < 0.0)
        return Result;
    const double Axes[] = {Chum.Fishy, Chum.Fragrant, Chum.Fermented};
    const double TotalChum = Chum.Fishy + Chum.Fragrant + Chum.Fermented;
    if (!FMath::IsFinite(TotalChum)) return Result;
    if (TotalChum <= 0.0) return SelectFromBasePool(Context, TEXT("EmptyChum"));
    FRandomStream Random(Context.RandomSeed);
    double ClassCursor = Random.GetFraction() * TotalChum;
    int32 SelectedClass = INDEX_NONE;
    for (int32 Index = 0; Index < 3; ++Index)
    {
        if (Axes[Index] <= 0.0) continue;
        SelectedClass = Index;
        ClassCursor -= Axes[Index];
        if (ClassCursor < 0.0) break;
    }
    const double ClassProbability = Axes[SelectedClass] / TotalChum;
    struct FCandidate
    {
        UCatFishDefinition* Definition = nullptr;
        double WeightKilograms = 0.0;
        double BaseFishStrength = 0.0;
        double Weight = 0.0;
    };
    TArray<FCandidate> Candidates;
    TSet<int32> Seen;
    double TotalWeight = 0.0;
    int32 InvalidStrengthCoefficientCount = 0;
    for (const TSoftObjectPtr<UCatFishDefinition>& Ref : Definitions)
    {
        UCatFishDefinition* Fish = Ref.LoadSynchronous();
        if (!Fish || !CatFishCatalogSettingsPrivate::PassesWaterRegionGate(*Fish, Context.WaterRegion.RegionId)
            || !FCatFishEligibilityPolicy::PassesActivePlayerCount(*Fish, Context.ActivePlayerCount)
            || !FCatFishEligibilityPolicy::PassesTimeOfDay(*Fish, Context.TimeOfDay, bEnableTimeOfDayEligibilityFilter)
            || !FCatFishEligibilityPolicy::PassesWeather(*Fish, Context.Weather, bEnableWeatherEligibilityFilter)) continue;
        const double Membership[] = {Fish->ChumPreference.Fishy, Fish->ChumPreference.Fragrant, Fish->ChumPreference.Fermented};
        if (Membership[SelectedClass] <= 0.0) continue;
        if (Seen.Contains(Fish->ItemId))
        {
            UE_LOG(LogCatFishing, Warning, TEXT("Event=fish_selection_duplicate_id Fish=%s Region=%s Result=Rejected"),
                *FString::FromInt(Fish->ItemId), *Context.WaterRegion.RegionId.ToString());
            return Result;
        }
        Seen.Add(Fish->ItemId);
        const double K = Fish->FishStrengthPerKilogram;
        if (!FMath::IsFinite(K) || K <= 0.0) { ++InvalidStrengthCoefficientCount; continue; }
        const double Kg = CatFishCatalogSettingsPrivate::SampleIndividualWeight(*Fish, Context);
        const double Weight = Fish->FindBaitMultiplierOrNeutral(Context.BaitItemId);
        if (!FMath::IsFinite(K) || K <= 0.0 || !FMath::IsFinite(Kg) || Kg <= 0.0
            || !FMath::IsFinite(K * Kg) || !FMath::IsFinite(Weight) || Weight <= 0.0) continue;
        Candidates.Add({Fish, Kg, K * Kg, Weight});
        TotalWeight += Weight;
    }
    if (InvalidStrengthCoefficientCount > 0)
        UE_LOG(LogCatFishing, Warning, TEXT("Event=fish_selection_strength_coefficient_unset Region=%s RejectedCandidates=%d"),
            *Context.WaterRegion.RegionId.ToString(), InvalidStrengthCoefficientCount);
    // 已选类别没有合法鱼时走基础池，不悄悄重抽其他类别、改变窝料占比。
    if (Candidates.IsEmpty()) return SelectFromBasePool(Context, TEXT("SelectedClassEmpty"));
    if (!FMath::IsFinite(TotalWeight) || TotalWeight <= 0.0) return SelectFromBasePool(Context, TEXT("ZeroTotalWeight"));
    Candidates.Sort([](const FCandidate& A, const FCandidate& B)
    { return A.Definition->ItemId < B.Definition->ItemId; });
    double Cursor = Random.GetFraction() * TotalWeight;
    const FCandidate* Selected = &Candidates.Last();
    for (const FCandidate& Candidate : Candidates)
    {
        Cursor -= Candidate.Weight;
        if (Cursor < 0.0) { Selected = &Candidate; break; }
    }
    Result.bSelected = true;
    Result.ItemId = Selected->Definition->ItemId;
    Result.WeightKilograms = Selected->WeightKilograms;
    Result.BaseFishStrength = Selected->BaseFishStrength;
    Result.SelectedFinalWeight = Selected->Weight;
    Result.SelectedNormalizedProbability = ClassProbability * Selected->Weight / TotalWeight;
    Result.EligibleCandidateCount = Result.PositiveWeightCandidateCount = Candidates.Num();
    Result.SelectedChumClass = SelectedClass;
    Result.SelectedChumClassProbability = ClassProbability;
    return Result;
}

// 食性概率尚未裁定时返回 0，由行为解析器沿已记录的模板接缝处理。
double UCatFishCatalogSettings::ResolveDietOutwardSegmentProbability(const ECatFishDiet Diet) const
{
    const double Configured = Diet == ECatFishDiet::Carnivore ? CarnivoreOutwardSegmentProbability
        : Diet == ECatFishDiet::Omnivore ? OmnivoreOutwardSegmentProbability
        : Diet == ECatFishDiet::Herbivore ? HerbivoreOutwardSegmentProbability : 0.0;
    return FMath::IsFinite(Configured) && Configured > 0.0 && Configured < 1.0 ? Configured : 0.0;
}

FCatFishSelectionResult UCatFishCatalogSettings::SelectFromBasePool(const FCatFishSelectionContext& Context,
	const TCHAR* FallbackReason) const
{
	FCatFishSelectionResult Result;
	Result.bFromBasePool = true;
	struct FBasePoolCandidate
	{
		UCatFishDefinition* Definition = nullptr;
		double Probability = 0.0;
		double WeightKilograms = 0.0;
		double BaseFishStrength = 0.0;
	};
	TArray<FBasePoolCandidate> Candidates;
	int32 InvalidStrengthCoefficientCount = 0;
	double TotalProbability = 0.0;
	TSet<int32> SeenIds;
	for (const FCatFishBasePoolEntry& Entry : BasePool)
	{
		if ((Entry.ItemId == 0) || !FMath::IsFinite(Entry.Probability) || Entry.Probability <= 0.0
			|| SeenIds.Contains(Entry.ItemId) || !FindRuntimeDefinition(Entry.ItemId))
		{
			UE_LOG(LogCatFishing, Warning, TEXT("Event=fish_selection_base_pool_invalid Region=%s Fish=%s Reason=InvalidOrDuplicateMapping Result=Rejected"),
				*Context.WaterRegion.RegionId.ToString(), *FString::FromInt(Entry.ItemId));
			return Result;
		}
		SeenIds.Add(Entry.ItemId);
	}
	for (const FCatFishBasePoolEntry& Entry : BasePool)
	{
		if ((Entry.ItemId == 0) || !FMath::IsFinite(Entry.Probability) || Entry.Probability <= 0.0)
		{
			continue;
		}
		UCatFishDefinition* Definition = FindRuntimeDefinition(Entry.ItemId);
		if (!Definition
			|| !CatFishCatalogSettingsPrivate::PassesWaterRegionGate(*Definition, Context.WaterRegion.RegionId)
			|| !FCatFishEligibilityPolicy::PassesActivePlayerCount(*Definition, Context.ActivePlayerCount)
			|| !FCatFishEligibilityPolicy::PassesTimeOfDay(*Definition, Context.TimeOfDay,
				bEnableTimeOfDayEligibilityFilter)
			|| !FCatFishEligibilityPolicy::PassesWeather(*Definition, Context.Weather,
				bEnableWeatherEligibilityFilter))
		{
			continue;
		}
		const double WeightKilograms = CatFishCatalogSettingsPrivate::SampleIndividualWeight(*Definition, Context);
		const double StrengthPerKilogram = Definition->FishStrengthPerKilogram;
		if (!FMath::IsFinite(StrengthPerKilogram) || StrengthPerKilogram <= 0.0) { ++InvalidStrengthCoefficientCount; continue; }
		if (!FMath::IsFinite(WeightKilograms) || WeightKilograms <= 0.0
			|| !FMath::IsFinite(StrengthPerKilogram) || StrengthPerKilogram <= 0.0)
		{
			continue;
		}
		FBasePoolCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.Definition = Definition;
		Candidate.Probability = Entry.Probability;
		Candidate.WeightKilograms = WeightKilograms;
		Candidate.BaseFishStrength = WeightKilograms * StrengthPerKilogram;
		if (!FMath::IsFinite(Candidate.BaseFishStrength)) { Candidates.Pop(); continue; }
		TotalProbability += Entry.Probability;
	}
	if (InvalidStrengthCoefficientCount > 0)
	{
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fish_selection_strength_coefficient_unset Region=%s RandomSeed=%d Source=BasePool RejectedCandidates=%d"),
			*Context.WaterRegion.RegionId.ToString(), Context.RandomSeed, InvalidStrengthCoefficientCount);
	}
	if (Candidates.IsEmpty() || !FMath::IsFinite(TotalProbability) || TotalProbability <= 0.0)
	{
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fish_selection_base_pool_unavailable Region=%s Reason=%s ConfiguredEntries=%d ")
			TEXT("UsableEntries=%d Note=NoUsableBasePoolEntries"),
			*Context.WaterRegion.RegionId.ToString(), FallbackReason, BasePool.Num(), Candidates.Num());
		return Result;
	}
	// 名册顺序不参与随机：按 ID 排序后再抽，保证同一种子在增删无关名额时抽到同一条。
	Candidates.Sort([](const FBasePoolCandidate& Left, const FBasePoolCandidate& Right)
	{
		return Left.Definition->ItemId < Right.Definition->ItemId;
	});
	FRandomStream Random(Context.RandomSeed);
	double Cursor = Random.FRandRange(0.0f, static_cast<float>(TotalProbability));
	const FBasePoolCandidate* Selected = &Candidates.Last(); // 浮点游标落在尾端时的确定性回退。
	for (const FBasePoolCandidate& Candidate : Candidates)
	{
		Cursor -= Candidate.Probability;
		if (Cursor <= 0.0)
		{
			Selected = &Candidate;
			break;
		}
	}
	Result.bSelected = true;
	Result.ItemId = Selected->Definition->ItemId;
	Result.WeightKilograms = Selected->WeightKilograms;
	Result.BaseFishStrength = Selected->BaseFishStrength;
	Result.SelectedFinalWeight = Selected->Probability;
	Result.SelectedNormalizedProbability = Selected->Probability / TotalProbability;
	Result.EligibleCandidateCount = Candidates.Num();
	Result.PositiveWeightCandidateCount = Candidates.Num();
	UE_LOG(LogCatFishing, Display,
		TEXT("Event=fish_selection_base_pool_used Region=%s Reason=%s Fish=%s WeightKg=%.3f Probability=%.4f ")
		TEXT("PoolCandidates=%d RandomSeed=%d"),
		*Context.WaterRegion.RegionId.ToString(), FallbackReason, *FString::FromInt(Result.ItemId),
		Result.WeightKilograms, Result.SelectedNormalizedProbability, Candidates.Num(), Context.RandomSeed);
	return Result;
}
