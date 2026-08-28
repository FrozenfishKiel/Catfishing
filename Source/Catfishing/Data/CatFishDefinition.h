#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/CatFishSelectionTypes.h"
#include "Framework/Core/CatRunContracts.h"
#include "CatFishDefinition.generated.h"

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

/** FishDefinition 的食用安全结论；猫状态只消费该结论，不按名字猜有毒鱼。 */
UENUM(BlueprintType)
enum class ECatFishFoodSafety : uint8
{
	/** 食用结论或数值尚未配置，进食命令必须 fail-closed。 */
	Unset,
	/** 可直接食用且不会增加 Poison。 */
	Safe,
	/** 可直接食用但会增加 Poison；具体倒地阈值由 Character 设置拥有。 */
	Toxic
};

/** 鱼种运行定义的最小 SSOT 接缝；无显式启用、稳定 ID、体型和价值时不能伪造可捕获鱼。 */
UCLASS(BlueprintType)
class CATFISHING_API UCatFishDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 检查该资产是否足以进入阶段 E 事务；任一必需字段 Unset 都返回 false。 */
	bool IsRuntimeDefinitionReady() const;
	double FindBaitMultiplierOrNeutral(FName BaitDefinitionId) const;

	/** 鱼种稳定 ID；FishInstance、图鉴候选和日志只引用该值，不把资产对象当永久身份。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FName FishDefinitionId = NAME_None;

	/** 体型协作档位；只有 Giant 可在 Fishing 阶段接受搏斗协作者。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fishing")
	ECatFishBodyClass BodyClass = ECatFishBodyClass::Unknown;

	/** 献祭提交后贡献的额度值；0 表示 Unset，客户端命令不能覆盖它。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sacrifice", meta = (ClampMin = "0"))
	int32 SacrificeContribution = 0;

	/** 捕获后可选成像事件的正式语义 ID；None 只跳过 CapturePlan，不阻止实物鱼与 FishRecorded 提交。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Imprint")
	FName CaptureImprintEventId = NAME_None;

	/** 稀有度轴的稳定内容 ID；它只控制出现/收集权重，与 BodyClass 协作轴完全独立。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Distribution")
	FName RarityTierId = NAME_None;

	/** 该鱼可出现的 WaterRegion ID；钓点不是机制字段，空数组表示未配置。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Distribution")
	TArray<FName> RegionIds;

	/** 该鱼可出现的局内白天时段；夜晚不会进入选择器。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Distribution")
	TArray<ECatEnvironmentTimeOfDay> TimeOfDay;

	/** 该鱼可出现的 Environment 天气；天气定义权仍在 Environment。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Distribution")
	TArray<ECatEnvironmentWeather> Weather;

	/** 候选选择的正权重；它由内容数据表达稀有度，不在代码硬编码档位概率。 */
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

	/** 搏斗中的鱼力量基值；与 Character FishingStrength 比较，0 表示公式输入未裁。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fishing", meta = (ClampMin = "0.0"))
	double FishStrength = 0.0;

	/** 搏斗中的鱼短周期体力；与日常属性/稀有度独立，0 表示未裁。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fishing", meta = (ClampMin = "0.0"))
	double FishFightStamina = 0.0;

	/** 试探/真咬节奏的稳定内容模板 ID；Fishing StateTree 消费模板而不改变三阶段规则。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Personality")
	FName BitePersonalityId = NAME_None;

	/** 搏斗冲刺/体力节奏的稳定内容模板 ID；不包含 C++ 转移拓扑。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Personality")
	FName FightPersonalityId = NAME_None;

	/** 该鱼偏好的特殊鱼饵定义 ID；普通饵无限且不要求出现在数组中。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Preference")
	FCatChumVector ChumPreference;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Preference")
	TArray<FCatBaitWeightMultiplier> BaitWeightMultipliers;

	/** 食用安全结论；Unset 时不能通过吃鱼链修改身体状态。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use")
	ECatFishFoodSafety FoodSafety = ECatFishFoodSafety::Unset;

	/** 直接食用后授予的局内成长经验；值来自当前鱼表体重档，0 表示吃鱼成长收益未裁。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use", meta = (ClampMin = "0.0"))
	double EatingExperience = 0.0;

	/** Toxic 鱼直接食用后增加 Poison 的正值；Safe 必须保持 0。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use", meta = (ClampMin = "0.0"))
	double PoisonIncrease = 0.0;

	/** 该鱼是否允许在共享鱼缸展示；Camp 只消费该用途，不推导观赏价值。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use")
	bool bTankDisplayEligible = false;

	/** 数据人员对单条正式鱼表记录的显式启用 gate；默认关闭，避免占位 DataAsset 产生成功事务。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Prototype")
	bool bEnableRuntimeDefinition = false;
};
