#include "Data/CatFishCatalogSettings.h"

#include "Data/CatFishDefinition.h"

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

// 鱼种选择流程：先拒绝未裁环境或无效协作快照，再仅保留人数、团队总力量和总体力都能直接覆盖鱼定义下限的条目；这是可解释的最低可达门，不额外发明成功率公式。
UCatFishDefinition* UCatFishCatalogSettings::SelectRuntimeDefinition(const FName RegionId,
	const ECatEnvironmentTimeOfDay TimeOfDay, const ECatEnvironmentWeather Weather, const int32 ActivePlayerCount,
	const double CombinedFishingStrength, const double CombinedFightStamina, const int32 RandomSeed,
	double& OutWeightKilograms) const
{
	OutWeightKilograms = 0.0;
	if (RegionId.IsNone() || TimeOfDay == ECatEnvironmentTimeOfDay::Unknown
		|| Weather == ECatEnvironmentWeather::Unknown || ActivePlayerCount < 1 || ActivePlayerCount > 8
		|| !FMath::IsFinite(CombinedFishingStrength) || CombinedFishingStrength <= 0.0
		|| !FMath::IsFinite(CombinedFightStamina) || CombinedFightStamina <= 0.0)
	{
		return nullptr;
	}
	TArray<UCatFishDefinition*> Candidates;
	double TotalWeight = 0.0;
	for (const TSoftObjectPtr<UCatFishDefinition>& DefinitionRef : Definitions)
	{
		UCatFishDefinition* Definition = DefinitionRef.LoadSynchronous();
		if (Definition && Definition->IsRuntimeDefinitionReady() && Definition->RegionIds.Contains(RegionId)
			&& Definition->TimeOfDay.Contains(TimeOfDay) && Definition->Weather.Contains(Weather)
			&& Definition->MinimumFightParticipants <= ActivePlayerCount
			&& Definition->FishStrength <= CombinedFishingStrength
			&& Definition->FishFightStamina <= CombinedFightStamina)
		{
			Candidates.Add(Definition);
			TotalWeight += Definition->SpawnWeight;
		}
	}
	if (Candidates.IsEmpty() || !FMath::IsFinite(TotalWeight) || TotalWeight <= 0.0)
	{
		return nullptr;
	}
	FRandomStream Random(RandomSeed);
	double Cursor = Random.FRandRange(0.0f, static_cast<float>(TotalWeight));
	UCatFishDefinition* Selected = Candidates.Last();
	for (UCatFishDefinition* Candidate : Candidates)
	{
		Cursor -= Candidate->SpawnWeight;
		if (Cursor <= 0.0)
		{
			Selected = Candidate;
			break;
		}
	}
	OutWeightKilograms = Random.FRandRange(static_cast<float>(Selected->MinimumWeightKilograms),
		static_cast<float>(Selected->MaximumWeightKilograms));
	return Selected;
}
