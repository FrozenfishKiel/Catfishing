#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Growth/CatGrowthTypes.h"
#include "CatGrowthSettings.generated.h"

/**
 * 三选一选项池的一条配表记录；字段与「升级效果」页 §2 表的列一一对应。
 * 数值全部是快照，改数走参数页流程，不在代码里手调。
 */
USTRUCT()
struct FCatGrowthOptionConfig
{
	GENERATED_BODY()

	/** 选项身份；None 表示该行无效，抽取时跳过。 */
	UPROPERTY(Config, EditAnywhere, Category = "Growth")
	ECatGrowthOptionId OptionId = ECatGrowthOptionId::None;

	/** 「每次选中」列；百分比类按小数写（+10% 写 0.10，-5% 写 -0.05）。 */
	UPROPERTY(Config, EditAnywhere, Category = "Growth")
	double MagnitudePerPick = 0.0;

	/** 「叠加与上限」列的上限绝对值；0 表示无上限。减益类（咬钩间隔、竿磨损）写正数，按绝对值夹。 */
	UPROPERTY(Config, EditAnywhere, Category = "Growth", meta = (ClampMin = "0.0"))
	double MaxTotalMagnitude = 0.0;

	/** 「出现次序」列：本局完成多少次三选一之后它才进抽取池；1＝开局即可抽到。 */
	UPROPERTY(Config, EditAnywhere, Category = "Growth", meta = (ClampMin = "1"))
	int32 UnlockAtChoiceOrdinal = 1;
};

/** 吃鱼成长的已裁规则；槽长度与三选一配表都在这里，鱼种经验仍来自鱼表。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Character Growth"))
class CATFISHING_API UCatGrowthSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 判断本局成长结算是否可运行；关闭或槽长度非法时吃鱼成长保持 fail-closed。 */
	bool IsRuntimeReady() const;

	/** 判断三选一是否可运行：成长 runtime 就绪、每组抽取数为正、池里至少有够抽一组的有效行。 */
	bool IsChoiceRuntimeReady() const;

	/** 按身份取配表行；找不到或身份为 None 时返回空，调用方必须当作「该项未配」处理。 */
	const FCatGrowthOptionConfig* FindOptionConfig(ECatGrowthOptionId OptionId) const;

	/** 吃鱼成长运行总 gate；默认关闭，防止未接线项目悄悄制造经验。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnableGrowthRuntime = false;

	/**
	 * 触发一次三选一需要的经验槽长度。正式值 250（2026-09-07 校准，数值成长页 §2／参数页 v0.22 为准）。
	 * 旧注释写的「当前需求锁定为 10」已作废：10 点是「按体重档 小1/中3/大8」时代的槽长，
	 * 连续模型（经验＝鱼种经验系数×实际重量）下一条中大型鱼几十点，10 会让一条鱼连开好几次。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Experience", meta = (ClampMin = "1"))
	int32 ExperiencePerChoiceSlot = 0;

	/** 每次槽满供选的项数；升级效果页 §1 定为 3，同次互不相同。 */
	UPROPERTY(Config, EditAnywhere, Category = "Choice", meta = (ClampMin = "1"))
	int32 OptionsPerOffer = 3;

	/** 池上限；升级效果页 §1 准入三闸之一（16），超出时超编行被拒绝入池而不是悄悄生效。 */
	UPROPERTY(Config, EditAnywhere, Category = "Choice", meta = (ClampMin = "1"))
	int32 MaxOptionPoolSize = 16;

	/** 三选一选项池配表；每行＝升级效果页 §2 表的一行。项数与内容以该页为准。 */
	UPROPERTY(Config, EditAnywhere, Category = "Choice")
	TArray<FCatGrowthOptionConfig> OptionPool;
};
