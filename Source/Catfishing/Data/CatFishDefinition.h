#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Environment/CatWaterTypes.h"
#include "CatFishDefinition.generated.h"

/** 鱼体型只表达协作档位，不携带任何力量、体力或几何公式。 */
UENUM(BlueprintType)
enum class ECatFishBodyClass : uint8
{
	/** 鱼表尚未裁决体型；Fishing 必须拒绝生成或结算。 */
	Unknown,
	/** 正式鱼表明确为无需多人搏斗的常规体型。 */
	Standard,
	/** 正式鱼表明确为可在搏斗阶段接受协作者的巨鱼；近岸仍按首个合法抢抄者归属。 */
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
	/** 检查该资产是否足以进入钓鱼/捕获目录；任一捕获必需字段 Unset 都返回 false，食用数值另由 HasRuntimeConsumptionEffect 裁决。 */
	bool IsRuntimeDefinitionReady() const;

	/** 判断该鱼是否已经拥有完整食用效果；Fishing/Items 只读捕获 readiness，吃鱼命令必须单独读取这个 gate。 */
	bool HasRuntimeConsumptionEffect() const;

	/** 鱼种稳定 ID；FishInstance、图鉴候选和日志只引用该值，不把资产对象当永久身份。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FName FishDefinitionId = NAME_None;

	/** 体型协作档位；只有 Giant 可在 Fishing 阶段接受搏斗协作者。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fishing")
	ECatFishBodyClass BodyClass = ECatFishBodyClass::Unknown;

	/**
	 * 这条鱼被献祭后对「世界进度」的增减量，而不是当日额度。额度已经拆成"献祭了几条鱼"这个独立计数，
	 * 与本字段互不换算：一条 -1 的臭臭鱼照样算一条达标鱼，只是把世界进度往回拉一格。
	 * 因此这里允许负值，也不设 ClampMin；0 仍然保留"这一行没填"的含义，readiness 会据此 fail-closed。
	 * 由鱼表格「供奉」列冻结，客户端命令不能覆盖它。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sacrifice")
	int32 SacrificeContribution = 0;

	/** 捕获后可选成像事件的正式语义 ID；None 只跳过 CapturePlan，不阻止实物鱼与 FishRecorded 提交。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Imprint")
	FName CaptureImprintEventId = NAME_None;

	/** 该鱼能出现在哪些 WaterRegion，取自鱼表格「出没地点」列；空数组表示该行地点未填，鱼不能进入任何池子。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Distribution")
	TArray<FName> RegionIds;

	/** 该鱼被哪几味窝料吸引，取自鱼表格「喜爱窝料」列；一条鱼可以同时吃两味（银月鳟为香与酵），所以是集合而不是单值。
	 *  本轮只做落盘与校验；按三轴占比决定鱼群构成的选鱼公式出自飞书「钓鱼规则」rev212 第二章的窝料气味占比模型
	 *  （池内三味占比即鱼群种族占比），该模型的消费端尚未开工，目录侧不预先实现。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Distribution")
	TArray<ECatChumAffinity> ChumAffinities;

	/** 该鱼真实重量的最小千克值；选择时由服务器在显式范围内抽取。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Physical", meta = (ClampMin = "0.0"))
	double MinimumWeightKilograms = 0.0;

	/** 该鱼真实重量的最大千克值；必须不小于 MinimumWeightKilograms。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Physical", meta = (ClampMin = "0.0"))
	double MaximumWeightKilograms = 0.0;

	/** 刷新该鱼需要的在场协作能力人数；单人局过滤任何大于 1 的定义。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fishing", meta = (ClampMin = "1", ClampMax = "8"))
	int32 MinimumFightParticipants = 0;

	/** 搏斗中的鱼力量基值；与 Character FishingStrength 比较，0 表示公式输入未裁。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fishing", meta = (ClampMin = "0.0"))
	double FishStrength = 0.0;

	/** 搏斗中的鱼短周期体力，来自飞书鱼表格「体力」列的真值；命中后作为搏斗快照 FishFightStaminaRemaining 的初值，
	 *  不参与猫的日常属性，0 表示公式输入未裁。HookedFight 内按飞书 §4.4 僵持逐秒扣减（猫力×0.08/秒），完美中鱼先削 15%。
	 *  它不参与选鱼筛选：飞书的体力是消耗资源不是准入资格，拿它和在场合计体力比较会让高体力鱼永远选不出来。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fishing", meta = (ClampMin = "0.0"))
	double FishFightStamina = 0.0;

	/** 该鱼偏好的特殊鱼饵定义 ID；普通饵无限且不要求出现在数组中。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Preference")
	TArray<FName> PreferredSpecialBaitIds;

	/** 食用安全结论；Unset 时不能通过吃鱼链修改身体状态。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use")
	ECatFishFoodSafety FoodSafety = ECatFishFoodSafety::Unset;

	/** Toxic 鱼直接食用后增加 Poison 的正值；Safe 必须保持 0。饥饿系统已删除（飞书猫咪状态册 v1.4），食用收益不再有 Hunger 一项。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use", meta = (ClampMin = "0.0"))
	double PoisonIncrease = 0.0;

	/** 该鱼是否允许在共享鱼缸展示；Camp 只消费该用途，不推导观赏价值。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use")
	bool bTankDisplayEligible = false;

	/** 数据人员对单条正式鱼表记录的显式启用 gate；默认关闭，避免占位 DataAsset 产生成功事务。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Prototype")
	bool bEnableRuntimeDefinition = false;
};
