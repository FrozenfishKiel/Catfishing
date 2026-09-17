#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Data/CatFishDefinition.h"
#include "Data/CatFishSelectionTypes.h"
#include "Framework/Core/CatRunContracts.h"
#include "CatFishCatalogSettings.generated.h"

class UCatFishDefinition;

/** 逐鱼窗口的配置值，单位秒；分别供内部鱼 ID 覆盖表和旧档位回退表使用。 */
USTRUCT()
struct FCatFishBiteTimingDefaults
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Config, meta = (ClampMin = "0.0", Units = "s"))
	double ProbeDurationSeconds = 0.0;

	UPROPERTY(EditAnywhere, Config, meta = (ClampMin = "0.0", Units = "s"))
	double TrueBiteWindowSeconds = 0.0;
};

/**
 * 完美提竿的入场削减系数（钓鱼规则 §3.4）；按鱼册稀有度档取，入场时直接乘到本场实际值，
 * 此后运动求解、负载、消耗全用削后值。未配置的档位退回 1.0，只会不削减，绝不放大。
 */
USTRUCT(BlueprintType)
struct FCatPerfectHookReduction
{
	GENERATED_BODY()

	/** 鱼力量倍率；普通鱼 0.8、稀有鱼 0.85。 */
	UPROPERTY(BlueprintReadOnly)
	double FishStrengthMultiplier = 1.0;

	/** 鱼体力倍率；普通鱼 0.85、稀有鱼 0.9。 */
	UPROPERTY(BlueprintReadOnly)
	double FishStaminaMultiplier = 1.0;

	/** 初始线长倍率；三项削减的第三项，不按稀有度分档，单值挂参数页。 */
	UPROPERTY(BlueprintReadOnly)
	double InitialLineLengthMultiplier = 1.0;
};

/**
 * 无窝料基础池的一个名额（鱼表格子表「基础池」）。
 * 基础池是空窝、候选为空或总权重为零时的兜底名册（鱼册 §3.1.2、设计修改记录 2026-09-14④）：
 * 它不参与窝料/鱼饵/挑战度那套加权，只按自己的固定概率抽。
 */
USTRUCT()
struct FCatFishBasePoolEntry
{
	GENERATED_BODY()

	/** 名册成员的鱼种稳定 ID；必须能在 Definitions 里解析出唯一就绪鱼定义，否则拒绝名册。 */
	UPROPERTY(EditAnywhere, Config)
	int32  ItemId = 0;

	/** 旧英文物品身份，仅供旧资产和旧档案单向迁移读取；新运行逻辑不读写，转换后清空。 */
	UPROPERTY()
	FName FishDefinitionId = NAME_None;


	/** 该成员在基础池里的相对概率（子表「基础池概率」列，已提为暂定正式）；<= 0 拒绝名册。 */
	UPROPERTY(EditAnywhere, Config, meta = (ClampMin = "0.0"))
	double Probability = 0.0;
};

/** 正式鱼表资产目录；它是运行时鱼定义的唯一枚举入口，不从文件名或 Content 扫描猜内容。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Fish Catalog"))
class CATFISHING_API UCatFishCatalogSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 按稳定 ID 查找完整且启用的鱼定义；重复 ID 或加载失败返回空。 */
	UCatFishDefinition* FindRuntimeDefinition(int32  ItemId) const;

	FCatFishSelectionResult SelectRuntimeDefinition(const FCatFishSelectionContext& Context) const;

	/** 两字段独立解析：资产为 0 时先取逐鱼覆盖，再取档位默认；非法值原样交由会话拒绝，0 仍表示缺配。 */
	FCatFishBiteTimingDefaults ResolveBiteTiming(const UCatFishDefinition& Definition) const;

	/** 设计表逐鱼覆盖；键必须是资产内部 ItemId，资产正值优先，不写回资产。 */
	UPROPERTY(Config, EditAnywhere, Category = "Bite")
	TMap<int32, FCatFishBiteTimingDefaults> BiteTimingOverridesByItemId;

	/** 未配置逐鱼值时的旧档位折中回退；不代表正式鱼表，也不包含完美窗。 */
	UPROPERTY(Config, EditAnywhere, Category = "Bite")
	TMap<FName, FCatFishBiteTimingDefaults> BiteTimingDefaultsByRarityTier;

	/**
	 * 按本鱼的 RarityTierId 取完美提竿削减系数；命中 RarePerfectHookRarityTierIds 的按稀有鱼取，其余档按普通鱼取。
	 * 削减系数不挂性格模板（四套 Bite_* 模板 2026-09-09 裁为测试用），也不逐鱼配。
	 */
	FCatPerfectHookReduction ResolvePerfectHookReduction(const UCatFishDefinition& Definition) const;

	/**
	 * 按食性取段末向外概率；未裁（配置为 0 或越界）与 Unset 食性都返回 0，
	 * 调用方据此退回测试期性格模板，不得把 0 当成「这条鱼永远向内游」。
	 */
	double ResolveDietOutwardSegmentProbability(ECatFishDiet Diet) const;

	/**
	 * 从无窝料基础池抽一条鱼；SelectRuntimeDefinition 在空窝、候选为空、总权重为零时调用它，
	 * 是这些情形下唯一的出鱼路径。它不读窝料/鱼饵/挑战度，只按名册自己的概率抽。
	 */
	FCatFishSelectionResult SelectFromBasePool(const FCatFishSelectionContext& Context,
		const TCHAR* FallbackReason) const;

	/** 正式 FishDefinition 软引用清单；默认空使 Fishing fail-closed，只读取显式登记的鱼种资产。 */
	UPROPERTY(Config, EditAnywhere, Category = "Catalog")
	TArray<TSoftObjectPtr<UCatFishDefinition>> Definitions;

	/**
	 * 测试期默认关闭；开启后才按 FishDefinition.TimeOfDay 过滤候选鱼。
	 * T23：开启后鱼定义 TimeOfDay 为空数组＝未配置，拒绝并 Warning（不得当作不受约束），
	 * 候选被筛空时还有基础池兜底。默认值仍是 False——开启节点待策划给（台账 D-31）。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Selection|Eligibility")
	bool bEnableTimeOfDayEligibilityFilter = false;

	/** 测试期默认关闭；开启后才按 FishDefinition.Weather 过滤候选鱼。空数组语义与时段门相同。 */
	UPROPERTY(Config, EditAnywhere, Category = "Selection|Eligibility")
	bool bEnableWeatherEligibilityFilter = false;

	/**
	 * 无窝料基础池名册（鱼表格子表「基础池」）。
	 * 空窝、候选为空或总权重为零时从这里抽一条，保证「空窝不空钩」（2026-09-08 李前臻裁）。
	 * 2026-09-14 暂定正式四条已填入 Game ini；空名册仍返回未选中并记 Warning。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Selection|BasePool")
	TArray<FCatFishBasePoolEntry> BasePool;

	/**
	 * 食性 → 段末「下一段向外」的基础概率（鱼的行为 §2 的 P_base）。三个值都在 (0,1) 内才生效。
	 * 默认 0 ＝ 未裁：食性列仍然读，但不改变行为，鱼按测试期性格模板跑。
	 * 为什么不直接写死 70/50/35：那三个数只出现在「鱼的行为」页，该页页头写明「待整页重写，实现不读本页」，
	 * 也没有进裁决账本——由策划落到本配置或参数页之后再启用。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Selection|Behavior", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double CarnivoreOutwardSegmentProbability = 0.0;

	UPROPERTY(Config, EditAnywhere, Category = "Selection|Behavior", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double OmnivoreOutwardSegmentProbability = 0.0;

	UPROPERTY(Config, EditAnywhere, Category = "Selection|Behavior", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double HerbivoreOutwardSegmentProbability = 0.0;

	/**
	 * 取「稀有鱼」完美系数的稀有度档 ID 清单；不在清单里的档一律按「普通鱼」取（钓鱼规则 §3.4 末句）。
	 * 墓碑（2026-09-14）：五档含事件的旧口径退役（设计修改记录 2026-09-13 裁决⑤）。
	 * 现行四档最高为珍稀，迁移后资产 ID 为 VeryRare；巨影按普通系数，清单为空时也全部按普通鱼取。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Perfect Hook")
	TArray<FName> RarePerfectHookRarityTierIds;

	/** 普通鱼完美提竿的鱼力量倍率（设计 0.8）；0 或越界表示未配置，取值时退回 1.0。 */
	UPROPERTY(Config, EditAnywhere, Category = "Perfect Hook", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double CommonPerfectFishStrengthMultiplier = 0.0;

	/** 普通鱼完美提竿的鱼体力倍率（设计 0.85）；0 或越界表示未配置，取值时退回 1.0。 */
	UPROPERTY(Config, EditAnywhere, Category = "Perfect Hook", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double CommonPerfectFishStaminaMultiplier = 0.0;

	/** 稀有鱼完美提竿的鱼力量倍率（设计 0.85）；0 或越界表示未配置，取值时退回 1.0。 */
	UPROPERTY(Config, EditAnywhere, Category = "Perfect Hook", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double RarePerfectFishStrengthMultiplier = 0.0;

	/** 稀有鱼完美提竿的鱼体力倍率（设计 0.9）；0 或越界表示未配置，取值时退回 1.0。 */
	UPROPERTY(Config, EditAnywhere, Category = "Perfect Hook", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double RarePerfectFishStaminaMultiplier = 0.0;

	/** 完美线长系数：完美中鱼时初始线长再乘它，不按稀有度分档；0 或越界表示未配置，取值时退回 1.0（即不缩线）。 */
	UPROPERTY(Config, EditAnywhere, Category = "Perfect Hook", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double PerfectInitialLineLengthMultiplier = 0.0;
};
