#pragma once

#include "CoreMinimal.h"
#include "Data/CatFishSelectionTypes.h"
#include "Fishing/Simulation/CatFishBodyModel.h"
#include "Framework/Core/CatRunContracts.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "CatFishDefinition.generated.h"

class UTexture2D;
class UCatFishPresentationDefinition;

/** 鱼体型只表达协作档位，不携带任何力量、体力或几何公式。 */
UENUM(BlueprintType)
enum class ECatFishBodyClass : uint8
{
	/** 鱼表尚未裁决体型；Fishing 必须拒绝生成或结算。 */
	Unknown,
	/** 正式鱼表明确为无需多人搏斗的常规体型。 */
	Standard,
	/** 正式鱼表明确为可在搏斗阶段接受协作者的巨鱼；抄网后由首个合法抄手携带世界鱼。 */
	Giant
};

/** 鱼的食性；决定这条鱼往外冲的基础倾向（鱼表格「食性」列，2026-09-09 晚裁定逐鱼配、不再走性格模板）。 */
UENUM(BlueprintType)
enum class ECatFishDiet : uint8
{
	/** 鱼表尚未填该列；行为链退回测试期性格模板，不自行猜档。 */
	Unset,
	/** 食肉。 */
	Carnivore,
	/** 杂食。 */
	Omnivore,
	/** 素食。 */
	Herbivore
};

/** 投掷一条鱼命中后产生的效果族；具体规则归道具／联机册，鱼册只承载逐鱼取哪一族与量。 */
UENUM(BlueprintType)
enum class ECatFishThrowEffectKind : uint8
{
	/** 这条鱼没有投掷效果（绝大多数鱼）；投掷仍可作为普通落地物品，但不产生任何对猫的后果。 */
	None,
	/** 命中者被击退一个趔趄并炸毛（咸鱼）。 */
	KnockbackStartle,
	/** 命中点附近产生驱散区，短时间内别的猫无法靠近（臭臭鱼）。 */
	RepelAura
};

/**
 * 一条鱼的投掷效果数据。
 * 只描述「哪一族效果、多大范围、持续多久」，不描述怎么飞、谁能扔、命中怎么判——那三件归投掷规则本身。
 * 任一数值为 0 表示该项未裁：消费方必须 fail-closed，不得自行取默认值。
 */
USTRUCT(BlueprintType)
struct FCatFishThrowEffect
{
	GENERATED_BODY()
	/** 正式猫反应动画（惊吓/捂鼻），不从鱼动画或任意现有 Montage 猜选；缺配时投掷效果拒绝并告警。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Throw")
	TSoftObjectPtr<class UAnimMontage> ReactionMontage;

	/** 效果族；None 表示这条鱼投出去只是掉在地上。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Throw")
	ECatFishThrowEffectKind Kind = ECatFishThrowEffectKind::None;

	/** 命中点起算的作用半径（厘米）；0 表示未裁。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Throw", meta = (ClampMin = "0.0", Units = "cm"))
	double EffectRadiusCentimeters = 0.0;

	/** 效果持续秒数（驱散区存在多久、炸毛表现持续多久）；0 表示未裁。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Throw", meta = (ClampMin = "0.0", Units = "s"))
	double DurationSeconds = 0.0;

	/** 数据是否完整到可以执行：None 永远算完整（什么也不做），其余两族要求半径与时长都已裁。 */
	bool IsRuntimeEffectReady() const
	{
		return Kind == ECatFishThrowEffectKind::None
			|| (FMath::IsFinite(EffectRadiusCentimeters) && EffectRadiusCentimeters > 0.0
				&& FMath::IsFinite(DurationSeconds) && DurationSeconds > 0.0);
	}
};

/** 鱼种运行定义的最小 SSOT 接缝；同时也是鱼物品静态定义，实物鱼进入鱼护、鱼缸和商店时不再走第二套容器物品表。 */
UCLASS(BlueprintType)
class CATFISHING_API UCatFishDefinition : public UCatInventoryItemDefinition
{
	GENERATED_BODY()

public:
	/** 正式水中鱼身标定；保存静态嘴点/质心/回转半径，服务器无需加载动画或网格。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Fight|Body")
	FCatFishBodyGeometry FightBodyGeometry;

	/** 鱼表「试探期」，秒；0 先取鱼目录档位默认，仍缺配才随机兜底；非法值拒绝。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bite", meta=(ClampMin="0", Units="s"))
	double ProbeDurationSeconds = 0.0;

	/** 鱼种普通响应窗，秒；0 先取鱼目录档位默认，仍缺配才带 Warning 使用旧全局值。与试探/完美窗独立。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bite", meta=(ClampMin="0", Units="s"))
	double TrueBiteWindowSeconds = 0.0;

	/** 构造鱼定义资产；库存侧的展示、ID 和实例类型都从鱼表字段覆盖读取。 */
	UCatFishDefinition(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** 检查该资产是否足以进入运行时捕获与容器事务；任一必需字段 Unset 都返回 false。 */
	bool IsRuntimeDefinitionReady() const;
	/** 选鱼时按鱼饵的数字物品编号读取本鱼的权重倍率；未配置该鱼饵时返回中性倍率 1.0，不修改配置。 */
	double FindBaitMultiplierOrNeutral(int32  BaitItemId) const;

	/** 判断该鱼是否具备有限且为正的食用成长系数；进食链在扣鱼前查询，实际重量与运行定义仍由后续预检验证。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Fish")
	bool IsEdible() const;

	/**
	 * 吃掉本条鱼给多少局内成长经验 ＝ 经验系数 × 实际重量（钓鱼/猫册同一口径）。
	 * 不可食用、系数未裁或重量非法一律返回 0，调用方按「这条鱼吃不出经验」处理，不要自己补默认值。
	 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Fish")
	double ResolveEatingExperiencePoints(double ActualWeightKilograms) const;

	/** 鱼表重量区间的中点；过渡期换算与体重相关的占位口径都用它，别在调用方各算一遍。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Fish")
	double GetWeightMidpointKilograms() const;

	/** 取本鱼体力系数（点/千克）；正式资产已迁移，非法值返回 0。 */
	double ResolveFightStaminaPerKilogram() const;

	/** 本场鱼体力初值 ＝ 体力系数 × 实际重量；系数直接读鱼表，重量由本次抽取冻结。 */
	double ResolveInitialFightStamina(double ActualWeightKilograms) const;
	/** 解析本鱼直接引用且合同完整的表现定义；不会扫描目录或按 ID 查询第二张表。 */
	UCatFishPresentationDefinition* LoadRuntimePresentationDefinition() const;

	/** 鱼物品展示名直接复用鱼种展示名；UI 不需要为鱼再查第二份物品表。 */
	virtual FText GetInventoryDisplayName() const override;

	/** 鱼物品说明直接复用鱼种说明；库存详情和图鉴共享同一份内容配置。 */
	virtual FText GetInventoryDescription() const override;

	/** 鱼物品缩略图直接复用鱼种缩略图；表现层仍从鱼定义取资源。 */
	virtual TSoftObjectPtr<UTexture2D> GetInventoryThumbnail() const override;

	/** 鱼定义只有能进入 Fishing 运行时且能生成鱼物品实例时才允许进正式库存。 */
	virtual bool IsInventoryRuntimeDefinitionReady() const override;

	/** 鱼物品默认生成鱼专用实例，重量、来源会话和捕获者身份都落在实例上。 */
	virtual TSubclassOf<UCatInventoryItemInstance> GetPreferredInstanceType() const override;

	/** 实物鱼不能堆叠；每条鱼都必须保留自己的实例 ID、重量和来源会话。 */
	virtual int32 GetMaxStackCount() const override;

	/** 鱼运行槽归一化会验证鱼专属字段，防止普通空格或鱼竿状态混入实物鱼。 */

	/** 不允许两条鱼按定义合并；同鱼种不同个体也必须保持两个实例。 */
	virtual bool CanStackWith(const UCatInventoryItemDefinition& Other) const override;

	/** 旧英文物品身份，仅供旧资产和旧档案单向迁移读取；新运行逻辑不读写，转换后清空。 */
	UPROPERTY()
	FName FishDefinitionId = NAME_None;


	/** 玩家可见鱼名；鱼护格、图鉴和提示优先读取它，未配置时回退到 ItemId。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	FText DisplayName;

	/** 玩家可见鱼种说明；库存详情和图鉴可以复用，玩法事务不读取它。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation", meta = (MultiLine = "true"))
	FText Description;

	/** 鱼护/库存格使用的鱼缩略图；捕获实例只保存鱼定义 ID，表现资源由定义资产提供。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UTexture2D> Thumbnail;

	/**
	 * 本鱼唯一的 Mesh / Skeleton / AnimBP / 动画资源入口。
	 * 运行时表现不得绕过该引用按 ItemId 维护平行映射。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UCatFishPresentationDefinition> PresentationDefinition;

	/** 体型协作档位；只有 Giant 可在 Fishing 阶段接受搏斗协作者。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fishing")
	ECatFishBodyClass BodyClass = ECatFishBodyClass::Unknown;

	/** 捕获后可选成像事件的正式语义 ID；None 只跳过 CapturePlan，不阻止实物鱼与 FishRecorded 提交。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Imprint")
	FName CaptureImprintEventId = NAME_None;

	/**
	 * 稀有度轴的内容 ID，与 BodyClass 协作轴独立。资产仍为 Common/Uncommon/Rare/Event 旧口径，
	 * 尚未迁移到鱼表的普通／少见／稀有／珍稀四档，不能按字面翻译。
	 * 它是价值判断，不进抽鱼概率（分布由区域、窝料类别与鱼饵偏好表达，鱼册 §2）；
	 * 鱼目录用它解析完美提竿削减分档及两项窗口的配置默认值。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Distribution")
	FName RarityTierId = NAME_None;

	/** 是否接受稀有鱼概率加成；策划按鱼种设置，选择器用执行者 GAS 倍率调整权重后统一归一化。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Distribution")
	bool bReceivesRarityBonus = false;

	/** 该鱼可出现的 WaterRegion ID；钓点不是机制字段，空数组表示未配置。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Distribution")
	TArray<FName> RegionIds;

	/** 该鱼可出现的局内白天时段；夜晚不会进入选择器。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Distribution")
	TArray<ECatEnvironmentTimeOfDay> TimeOfDay;

	/** 该鱼可出现的 Environment 天气；天气定义权仍在 Environment。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Distribution")
	TArray<ECatEnvironmentWeather> Weather;

	/** 旧资产的基础权重载荷；正式两步抽鱼不消费，待资产引用审计后移除。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Distribution", meta = (ClampMin = "0.0"))
	double SpawnWeight = 0.0;

	/** 该鱼真实重量的最小千克值；选择时由服务器在显式范围内抽取。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Physical", meta = (ClampMin = "0.0"))
	double MinimumWeightKilograms = 0.0;

	/** 该鱼真实重量的最大千克值；必须不小于 MinimumWeightKilograms。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Physical", meta = (ClampMin = "0.0"))
	double MaximumWeightKilograms = 0.0;

	/**
	 * 抄网可捞圆圈的半径（厘米），圆心随鱼的权威位置移动。
	 * 抄手向正前方水平发射一条长度 = 抄网 ScoopReachCentimeters 的线段，与这个圆相交即判定够得着。
	 * 判定纯水平（俯视投影），高度差另由 UCatFishingSettings::MaximumScoopVerticalDeltaCentimeters 单独限制。
	 * 语义是"这条鱼有多好捞"：小鱼给小圈、巨鱼给大圈以降低多人抢抄难度。0 表示未裁，服务器一律拒绝抢抄。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fishing", meta = (ClampMin = "0.0", Units = "cm"))
	double ScoopTargetRadiusCentimeters = 0.0;

	/** 刷新该鱼需要的在场协作能力人数；单人局过滤任何大于 1 的定义。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fishing", meta = (ClampMin = "1", ClampMax = "8"))
	int32 MinimumFightParticipants = 0;

	/**
	 * 力量系数 K：鱼表格「力量系数K」列，本条鱼的实例力量 ＝ 实际重量 × K（钓鱼规则 §4.1「鱼力量 F_fish」行）。
	 * 逐鱼配，不走全局常数（2026-09-09 八问④撤回工程自补的全局 StrengthPerKilogram）；湖心巨影 K＝5，与竿强 210 配对。
	 * 0 或非法值使该鱼退出候选并告警，不再借用猫方全局换算系数。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fishing", meta = (ClampMin = "0.0", DisplayName = "力量系数K"))
	double FishStrengthPerKilogram = 0.0;

	/**
	 * 体力系数：本场鱼体力初值 ＝ 该系数 × 实际重量（鱼表格「体力系数」列，2026-09-08 李前臻代拍，
	 * 设计修改记录.md:275；原「每鱼种一份定额体力」作废）。单位是「体力点/千克」，不是体力点本身。
	 * 0 表示未裁：选鱼链 fail-closed 跳过该候选。
	 * 通过 ResolveFightStaminaPerKilogram() 校验后乘本条实际重量；不再接受旧定额语义。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fishing", meta = (ClampMin = "0.0", DisplayName = "体力系数"))
	double FishFightStaminaPerKilogram = 0.0;

	/** T10：只保留旧鱼资产的 Bite 模板身份反射；试探与普通响应读本鱼秒数字段，完美基础 1 秒。 */




	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Personality")
	FName BitePersonalityId = NAME_None;

	/**
	 * 搏斗节奏的测试期模板 ID；不包含 C++ 转移拓扑。
	 * 2026-09-09 晚裁「四套性格模板是测试用，正式口径逐鱼配（鱼表格食性、发力段长、休息段长、游速系数四列）」，
	 * 台账 D-16「不恢复旧模型」被同批推翻。下面 Behavior 分组的四列就是正式来源，本模板只在某列未填时兜底。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Personality")
	FName FightPersonalityId = NAME_None;

	/**
	 * 食性（鱼表格「食性」列）：决定这条鱼往外冲的基础倾向。
	 * Unset ＝ 该列未填，行为链退回 FightPersonalityId 指向的测试模板，不在代码里猜档位。
	 * 档位对应的向外概率由 UCatFishCatalogSettings 的 DietOutwardSegmentProbability 三个配置位给，
	 * 三个数尚未进裁决账本（「鱼的行为」页页头写明「实现不读本页」），未配置时同样退回模板。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behavior", meta = (DisplayName = "食性"))
	ECatFishDiet Diet = ECatFishDiet::Unset;

	/**
	 * 发力段长区间（秒，鱼表格「发力段长」列，快照 3~6）。X＝下限、Y＝上限。
	 * X <= 0 或 Y < X 表示未填，段长退回测试模板的 OutwardDurationRangeSeconds。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behavior", meta = (DisplayName = "发力段长"))
	FVector2D OutwardSegmentDurationRangeSeconds = FVector2D::ZeroVector;

	/**
	 * 休息段长区间（秒，鱼表格「休息段长」列，快照 2~5）。X＝下限、Y＝上限。
	 * X <= 0 或 Y < X 表示未填，段长退回测试模板的 EaseOffDurationRangeSeconds。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behavior", meta = (DisplayName = "休息段长"))
	FVector2D RestSegmentDurationRangeSeconds = FVector2D::ZeroVector;

	/**
	 * 游速系数（鱼表格「游速系数」列）：本鱼满力游速 ＝ 测试模板的满力游速 × 本系数。
	 * 2026-09-10 李前臻定「按鱼种取值，不按体重档」，原「小 0.8／中 1.0／大 1.2／巨影 1.5」的体重档口径作废。
	 * 0 表示该列未填，游速退回模板原值（等价于系数 1）。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behavior", meta = (ClampMin = "0.0", DisplayName = "游速系数"))
	double SwimSpeedCoefficient = 0.0;

	/** 团队库存展示的推荐鱼饵数字 ID；策划填写、图鉴 Model 经总表读图，0 留空，不参与选鱼计算。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Collection", meta = (ClampMin = "0", DisplayName = "推荐鱼饵 ID"))
	int32 RecommendedBaitItemId = 0;

	/** 团队库存展示的推荐窝料数字 ID；策划填写、图鉴 Model 经总表读图，0 留空，不改变窝料计算。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Collection", meta = (ClampMin = "0", DisplayName = "推荐窝料 ID"))
	int32 RecommendedChumItemId = 0;

	/** 鱼的窝料类别：正式资产为腥/香/酵单一轴 1，其余轴 0；不作为连续亲和倍率。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Preference")
	FCatChumVector ChumPreference;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Preference")
	TArray<FCatBaitWeightMultiplier> BaitWeightMultipliers;

	/** 每千克鱼肉提供的成长经验；服务器按实例实际重量计算，零表示该鱼不提供食用成长。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use", meta = (ClampMin = "0.0", DisplayName = "经验系数"))
	double EatingExperiencePerKilogram = 0.0;

	/**
	 * 投掷这条鱼命中后的效果（鱼表格「吃鱼效果」列里写成投掷规格的那两条：咸鱼击退炸毛、臭臭鱼驱散并短时屏蔽靠近）。
	 * 逐鱼数据在鱼表、由鱼册承载；投掷动作本身、命中判定与后果施加归道具／联机册，本字段只是数据侧接缝。
	 * 默认 Kind=None＝这条鱼投出去只是掉在地上，不是「未裁」——绝大多数鱼本来就没有投掷效果。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use")
	FCatFishThrowEffect ThrowEffect;

	/** 该鱼是否允许在共享鱼缸展示；Camp 只消费该用途，不推导观赏价值。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use")
	bool bTankDisplayEligible = false;

	/** 数据人员对单条正式鱼表记录的显式启用 gate；默认关闭，避免占位 DataAsset 产生成功事务。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Prototype")
	bool bEnableRuntimeDefinition = false;

private:
	friend struct FCatFishBehaviorProfileResolver;
	/** 只用于迁移诊断去重，不序列化、不复制，也不参与行为决策。 */
	mutable bool bLoggedBehaviorTemplateFallback = false;
};
