#include "Data/CatFishCatalogSettings.h"

#include "Data/CatFishDefinition.h"
#include "Curves/CurveFloat.h"
#include "Logging/CatLog.h"

namespace CatFishCatalogSettingsPrivate
{
	enum class EChallengeBand : uint8
	{
		Comfort,
		Matched,
		Risky,
		Count
	};

	static bool PassesWaterRegionGate(const UCatFishDefinition& Definition, const FName RegionId)
	{
		return Definition.IsRuntimeDefinitionReady() && Definition.RegionIds.Contains(RegionId);
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

	static double SampleIndividualWeight(const UCatFishDefinition& Definition,
		const FCatFishSelectionContext& Context)
	{
		// 每个鱼种使用独立稳定随机流，避免增删其他候选时改变本鱼个体重量；主随机流只负责难度带和鱼种抽取。
		const uint32 Seed = HashCombineFast(GetTypeHash(Context.RandomSeed),
			GetTypeHash(Definition.FishDefinitionId));
		FRandomStream WeightRandom(static_cast<int32>(Seed));
		return WeightRandom.FRandRange(static_cast<float>(Definition.MinimumWeightKilograms),
			static_cast<float>(Definition.MaximumWeightKilograms));
	}

	static double CalculateChallengeRatio(const double FishStrength,
		const double FishStamina, const double CombinedFishingStrength,
		const double CombinedFightStamina)
	{
		const double StrengthRatio = FishStrength / CombinedFishingStrength;
		const double StaminaRatio = FishStamina / CombinedFightStamina;
		// 力量是持续约束对抗的危险下限，不能被低体力稀释；体力只有在鱼也具备相称力量时才构成持续挑战。
		// 调和均值会把“高体力、极低力量”的耐打木桩压回轻松带，避免选择器把它误判为高强度运动对手。
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

	// 完美削减只允许落在 (0,1]：未配置、非有限或越界一律退回 1.0，保证配置缺失时只是"不削减"，不会放大鱼或把值清零。
	static double SanitizePerfectMultiplier(const double Multiplier)
	{
		return FMath::IsFinite(Multiplier) && Multiplier > 0.0 && Multiplier <= 1.0 ? Multiplier : 1.0;
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

FCatFishSelectionResult UCatFishCatalogSettings::SelectRuntimeDefinition(
	const FCatFishSelectionContext& Context) const
{
	FCatFishSelectionResult Result;
	if (!Context.WaterRegion.IsValid() || !Context.ChumSample.bSucceeded
		|| !(Context.ChumSample.WaterRegion == Context.WaterRegion)
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
		double WeightKilograms = 0.0;
		double BaseFishStrength = 0.0;
		double ChallengeRatio = 0.0;
		double FinalWeight = 0.0;
		CatFishCatalogSettingsPrivate::EChallengeBand ChallengeBand =
			CatFishCatalogSettingsPrivate::EChallengeBand::Comfort;
	};
	TArray<FCandidate> Candidates;
	// 只统计"力量系数K 尚未落到资产"这一种跳过原因：它是内容缺口而不是生态条件不合，必须能被单独看见。
	int32 UnsetStrengthCoefficientCount = 0;
	for (const TSoftObjectPtr<UCatFishDefinition>& DefinitionRef : Definitions)
	{
		UCatFishDefinition* Definition = DefinitionRef.LoadSynchronous();
		if (!Definition || !CatFishCatalogSettingsPrivate::PassesWaterRegionGate(*Definition,
			Context.WaterRegion.RegionId))
		{
			continue;
		}
		// 先确定本鱼种在本次咬钩机会里的个体重量，再用同一重量推导力量和挑战度；选中后复用该重量。
		const double WeightKilograms = CatFishCatalogSettingsPrivate::SampleIndividualWeight(
			*Definition, Context);
		// 鱼力量按逐鱼「力量系数K」换算（钓鱼规则 §4.1）：没有全局换算常数可回退，K 未配置的鱼直接 fail-closed 跳过，
		// 不能让它带着 0 力量混进抽取池（0 力量会被挑战度判成"最轻松"的候选）。
		const double StrengthPerKilogram = Definition->FishStrengthPerKilogram;
		if (!FMath::IsFinite(StrengthPerKilogram) || StrengthPerKilogram <= 0.0)
		{
			++UnsetStrengthCoefficientCount;
			continue;
		}
		const double BaseFishStrength = WeightKilograms * StrengthPerKilogram;
		// 挑战度是第一道实际玩法门：超出安全上限的个体不会再进入任何生态条件或权重计算。
		// 鱼体力同样是逐鱼系数 × 实际重量（2026-09-08 改口径）；挑战度必须拿同一个量纲去比，
		// 否则「体力系数」会被当成体力点直接和猫的体力总量比较，轻重鱼一律错档。
		const double FishFightStamina = Definition->ResolveInitialFightStamina(WeightKilograms);
		const double ChallengeRatio = CatFishCatalogSettingsPrivate::CalculateChallengeRatio(
			BaseFishStrength, FishFightStamina,
			Context.CombinedFishingStrength, Context.CombinedFightStamina);
		if (!FMath::IsFinite(WeightKilograms) || WeightKilograms <= 0.0
			|| !FMath::IsFinite(BaseFishStrength) || BaseFishStrength <= 0.0
			|| !FMath::IsFinite(FishFightStamina) || FishFightStamina <= 0.0
			|| !FMath::IsFinite(ChallengeRatio) || ChallengeRatio <= 0.0
			|| ChallengeRatio > MaximumChallengeRatio)
		{
			continue;
		}
		// 时段和天气已有稳定扩展接缝，但测试期默认旁路；人数门继续保护多人鱼不会进入人数不足的局。
		if (!FCatFishEligibilityPolicy::PassesTimeOfDay(*Definition, Context.TimeOfDay,
			bEnableTimeOfDayEligibilityFilter)
			|| !FCatFishEligibilityPolicy::PassesWeather(*Definition, Context.Weather,
				bEnableWeatherEligibilityFilter)
			|| !FCatFishEligibilityPolicy::PassesActivePlayerCount(*Definition, Context.ActivePlayerCount))
		{
			continue;
		}
		FCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.Definition = Definition;
		Candidate.WeightKilograms = WeightKilograms;
		Candidate.BaseFishStrength = BaseFishStrength;
		Candidate.ChallengeRatio = ChallengeRatio;
		Candidate.ChallengeBand = CatFishCatalogSettingsPrivate::ResolveChallengeBand(
			ChallengeRatio, *this);
	}
	if (UnsetStrengthCoefficientCount > 0)
	{
		// 鱼表「力量系数K」列还没落到 Fish_*.uasset；这条鱼不会出现，直到数据侧补值。
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fish_selection_strength_coefficient_unset Region=%s SkippedCandidates=%d"),
			*Context.WaterRegion.RegionId.ToString(), UnsetStrengthCoefficientCount);
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
		// 候选为空＝落基础池（2026-09-08 李前臻裁「候选为空或总权重为零落基础池」）。
		// 空窝、条件门全筛掉、名册没填都走这一条，绝不静默空钩。
		return SelectFromBasePool(Context, TEXT("NoEligibleCandidate"));
	}
	Result.EligibleCandidateCount = Candidates.Num();
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
	// 所有条件门和挑战档都已经确定后，才计算剩余鱼种的窝料/鱼饵权重并做最终归一化。
	double TotalCandidateWeight = 0.0;
	for (FCandidate& Candidate : Candidates)
	{
		if (Candidate.ChallengeBand != SelectedBand)
		{
			continue;
		}
		const double RawAffinity = Context.ChumSample.EffectiveChumVector.Fishy
				* Candidate.Definition->ChumPreference.Fishy
			+ Context.ChumSample.EffectiveChumVector.Fragrant
				* Candidate.Definition->ChumPreference.Fragrant
			+ Context.ChumSample.EffectiveChumVector.Fermented
				* Candidate.Definition->ChumPreference.Fermented;
		if (!FMath::IsFinite(RawAffinity))
		{
			return FCatFishSelectionResult();
		}
		const double NormalizedAffinity = RawAffinity <= 0.0 ? 0.0
			: RawAffinity / (RawAffinity + ChumAffinityHalfSaturation);
		const double ChumModifier = FMath::Clamp(
			static_cast<double>(SaturationCurve->GetFloatValue(static_cast<float>(NormalizedAffinity))),
			0.0, MaximumChumModifier);
		const double BaitModifier = Candidate.Definition->FindBaitMultiplierOrNeutral(Context.BaitDefinitionId);
		const double ChallengeModifier = CatFishCatalogSettingsPrivate::CalculateChallengeModifier(
			Candidate.ChallengeRatio, *this);
		Candidate.FinalWeight = Candidate.Definition->SpawnWeight * ChumModifier
			* BaitModifier * ChallengeModifier;
		if (FMath::IsFinite(Candidate.FinalWeight) && Candidate.FinalWeight > 0.0)
		{
			TotalCandidateWeight += Candidate.FinalWeight;
			++Result.SelectedBandCandidateCount;
		}
	}
	if (!FMath::IsFinite(TotalCandidateWeight) || TotalCandidateWeight <= 0.0)
	{
		// 总权重为零＝落基础池（同一条裁决的另一半）：有合法候选但窝料/鱼饵/挑战度把它们全乘成 0。
		return SelectFromBasePool(Context, TEXT("ZeroTotalWeight"));
	}
	double Cursor = Random.FRandRange(0.0f, static_cast<float>(TotalCandidateWeight));
	const FCandidate* Selected = nullptr;
	for (const FCandidate& Candidate : Candidates)
	{
		if (Candidate.ChallengeBand != SelectedBand || !FMath::IsFinite(Candidate.FinalWeight)
			|| Candidate.FinalWeight <= 0.0)
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
	Result.WeightKilograms = Selected->WeightKilograms;
	Result.BaseFishStrength = Selected->BaseFishStrength;
	Result.SelectedFinalWeight = Selected->FinalWeight;
	Result.SelectedNormalizedProbability = Selected->FinalWeight / TotalCandidateWeight;
	return Result;
}

// 食性档位取值流程：只在配置落在 (0,1) 开区间时才认；0 与越界都返回 0，代表「这一档未裁」，
// 由行为侧退回测试期性格模板——不能把 0 当成合法的「永不向外」。
double UCatFishCatalogSettings::ResolveDietOutwardSegmentProbability(const ECatFishDiet Diet) const
{
	const double Configured = Diet == ECatFishDiet::Carnivore ? CarnivoreOutwardSegmentProbability
		: Diet == ECatFishDiet::Omnivore ? OmnivoreOutwardSegmentProbability
		: Diet == ECatFishDiet::Herbivore ? HerbivoreOutwardSegmentProbability : 0.0;
	return FMath::IsFinite(Configured) && Configured > 0.0 && Configured < 1.0 ? Configured : 0.0;
}

// 基础池抽取流程：只按名册自己的固定概率抽，不读窝料、鱼饵、挑战度与稀有度——它是兜底名册不是第二套生态。
// 仍然保留三道客观门：鱼定义必须就绪、必须属于本水域、必须满足在场协作人数（单人局不会兜出需要多人的鱼）。
// 时段/天气两门跟随各自开关，与主链一致。名册没填或全被门挡掉时返回未选中，并记一条 Warning 指出是哪一种。
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
	double TotalProbability = 0.0;
	for (const FCatFishBasePoolEntry& Entry : BasePool)
	{
		if (Entry.FishDefinitionId.IsNone() || !FMath::IsFinite(Entry.Probability) || Entry.Probability <= 0.0)
		{
			continue;
		}
		UCatFishDefinition* Definition = FindRuntimeDefinition(Entry.FishDefinitionId);
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
		TotalProbability += Entry.Probability;
	}
	if (Candidates.IsEmpty() || !FMath::IsFinite(TotalProbability) || TotalProbability <= 0.0)
	{
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fish_selection_base_pool_unavailable Region=%s Reason=%s ConfiguredEntries=%d ")
			TEXT("UsableEntries=%d Note=BasePoolRosterIsStillADesignTodo"),
			*Context.WaterRegion.RegionId.ToString(), FallbackReason, BasePool.Num(), Candidates.Num());
		return Result;
	}
	// 名册顺序不参与随机：按 ID 排序后再抽，保证同一种子在增删无关名额时抽到同一条。
	Candidates.Sort([](const FBasePoolCandidate& Left, const FBasePoolCandidate& Right)
	{
		return Left.Definition->FishDefinitionId.LexicalLess(Right.Definition->FishDefinitionId);
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
	Result.FishDefinitionId = Selected->Definition->FishDefinitionId;
	Result.WeightKilograms = Selected->WeightKilograms;
	Result.BaseFishStrength = Selected->BaseFishStrength;
	Result.SelectedFinalWeight = Selected->Probability;
	Result.SelectedNormalizedProbability = Selected->Probability / TotalProbability;
	Result.EligibleCandidateCount = Candidates.Num();
	Result.SelectedBandCandidateCount = Candidates.Num();
	UE_LOG(LogCatFishing, Display,
		TEXT("Event=fish_selection_base_pool_used Region=%s Reason=%s Fish=%s WeightKg=%.3f Probability=%.4f ")
		TEXT("PoolCandidates=%d"),
		*Context.WaterRegion.RegionId.ToString(), FallbackReason, *Result.FishDefinitionId.ToString(),
		Result.WeightKilograms, Result.SelectedNormalizedProbability, Candidates.Num());
	return Result;
}
