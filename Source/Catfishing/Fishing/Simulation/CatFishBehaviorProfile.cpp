#include "Fishing/Simulation/CatFishBehaviorProfile.h"

#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Data/CatFishPersonalityDefinition.h"
#include "Logging/CatLog.h"

namespace CatFishBehaviorProfilePrivate
{
	// 段长区间合法性：两个端点都必须有限、下限为正、上限不小于下限。不合法即「该列还没填」。
	static bool IsConfiguredRange(const FVector2D& Range)
	{
		return FMath::IsFinite(Range.X) && FMath::IsFinite(Range.Y) && Range.X > 0.0 && Range.Y >= Range.X;
	}
}

// 行为参数合成流程：先以测试期性格模板作底（模板缺失时就是结构默认值），再把鱼表已填的四列逐列覆盖上去。
// 逐列覆盖而不是整份二选一，是因为鱼表四列是分批落的：填了哪列就以哪列为准，没填的那列不该跟着一起回退。
bool FCatFishBehaviorProfileResolver::Resolve(const UCatFishDefinition& Fish,
	const UCatFightPersonalityDefinition* TestingTemplate, FCatFishResolvedBehavior& OutBehavior)
{
	OutBehavior = FCatFishResolvedBehavior();
	if (TestingTemplate)
	{
		OutBehavior.SteeringConfig = TestingTemplate->AdaptiveSteeringConfig;
		OutBehavior.FullEffortSpeedCentimetersPerSecond =
			TestingTemplate->FullEffortMovementSpeedCentimetersPerSecond;
	}

	// 发力段长（鱼表格「发力段长」列）→ 向外冲段的时长区间。
	if (CatFishBehaviorProfilePrivate::IsConfiguredRange(Fish.OutwardSegmentDurationRangeSeconds))
	{
		OutBehavior.SteeringConfig.OutwardDurationRangeSeconds = Fish.OutwardSegmentDurationRangeSeconds;
		++OutBehavior.FieldsTakenFromFishTable;
	}
	// 休息段长（鱼表格「休息段长」列）→ 缓游段的时长区间。
	// 横切段（LateralArc）不在设计的「向外游／向内游」两段模型里，鱼表也没有对应列，
	// 因此它的段长仍读测试模板，不拿发力段长顶替——那会替策划决定横切算不算发力。
	if (CatFishBehaviorProfilePrivate::IsConfiguredRange(Fish.RestSegmentDurationRangeSeconds))
	{
		OutBehavior.SteeringConfig.EaseOffDurationRangeSeconds = Fish.RestSegmentDurationRangeSeconds;
		++OutBehavior.FieldsTakenFromFishTable;
	}
	// 游速系数（鱼表格「游速系数」列，2026-09-10 定按鱼种、不按体重档）→ 乘在模板满力游速上。
	if (FMath::IsFinite(Fish.SwimSpeedCoefficient) && Fish.SwimSpeedCoefficient > 0.0)
	{
		OutBehavior.FullEffortSpeedCentimetersPerSecond *= Fish.SwimSpeedCoefficient;
		++OutBehavior.FieldsTakenFromFishTable;
	}
	// 食性（鱼表格「食性」列）→ 段末向外概率 P_base。档位对应的三个数尚未进裁决账本，
	// 由目录设置给；没给就返回 0，行为拓扑保持 StateTree 资产现状。
	if (Fish.Diet != ECatFishDiet::Unset)
	{
		const UCatFishCatalogSettings* Catalog = GetDefault<UCatFishCatalogSettings>();
		const double OutwardProbability = Catalog
			? Catalog->ResolveDietOutwardSegmentProbability(Fish.Diet) : 0.0;
		if (OutwardProbability > 0.0)
		{
			OutBehavior.SteeringConfig.OutwardSegmentProbability = OutwardProbability;
			++OutBehavior.FieldsTakenFromFishTable;
		}
	}

	if (!FMath::IsFinite(OutBehavior.FullEffortSpeedCentimetersPerSecond)
		|| OutBehavior.FullEffortSpeedCentimetersPerSecond <= 0.0)
	{
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fish_behavior_profile_unresolved Fish=%s HasTemplate=%s SwimSpeedCoefficient=%.3f ")
			TEXT("Result=NoUsableFullEffortSpeed"),
			*Fish.FishDefinitionId.ToString(), TestingTemplate ? TEXT("true") : TEXT("false"),
			Fish.SwimSpeedCoefficient);
		return false;
	}
	if (OutBehavior.FieldsTakenFromFishTable < 4 && !Fish.bLoggedBehaviorTemplateFallback)
	{
		// 四列分批迁移；默认落盘且每资产一次，避免 Development 包里只有 Verbose 而无法判断回退。
		Fish.bLoggedBehaviorTemplateFallback = true;
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fish_behavior_profile_partial_fish_table Fish=%s FieldsFromFishTable=%d/4 ")
			TEXT("FallbackTemplate=%s"),
			*Fish.FishDefinitionId.ToString(), OutBehavior.FieldsTakenFromFishTable,
			TestingTemplate ? *TestingTemplate->FightPersonalityId.ToString() : TEXT("None"));
	}
	return true;
}
