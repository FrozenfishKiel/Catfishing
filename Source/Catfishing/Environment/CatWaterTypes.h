#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "Framework/Core/CatRunContracts.h"
#include "CatWaterTypes.generated.h"

/** WaterQuery 的只读拒绝语义；本模块不启用 UE Experimental Water，也不写回 Run。 */
UENUM(BlueprintType)
enum class ECatWaterQueryError : uint8
{
	/** 找到唯一合法水域并返回快照。 */
	None,
	/** Run 快照不允许当前阶段钓鱼。 */
	FishingClosed,
	/** 查询位置不是有限数值。 */
	InvalidLocation,
	/** 没有配置完整且包含该位置的水域。 */
	RegionNotFound,
	/** 多个水域同时命中；几何优先级未裁时拒绝猜测。 */
	AmbiguousRegion,
	/** 查询所依据的 Run Revision 已陈旧。 */
	RevisionConflict
};

/** 窝料的腥、香、酵三轴量；它只表达"投了多少味道"这一个概念，不包含吸引公式，也不包含谁被吸引。
 *  配置值与一次投掷的向量按策划口径是正整数，窝点池的运行值因为按 0.9 连乘衰减而是浮点数，所以这里统一用 double 承载两者。
 *  三轴没有互斥关系，也没有设计上限：策划明确由衰减机制收敛，代码侧只在窝点子系统留一个正常游玩碰不到的数值安全夹。
 *  三个字段开 EditAnywhere 是因为离线导入脚本要用 set_editor_property 反射写入这些值，
 *  而 USTRUCT 成员的编辑标记在所有使用点统一生效，没法只对 DataAsset 放开。 */
USTRUCT(BlueprintType)
struct FCatChumVector
{
	GENERATED_BODY()

	/** 腥味轴上的量；投掷时是装备或自然事件配置的正整数，落进窝点池后是被反复衰减的浮点余量。
	 *  编辑器可编辑字段：由离线导入脚本或编辑器写入，运行时只读取不回写；非有限或负值无效。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	double Fishy = 0.0;

	/** 香味轴上的量；投掷时是装备或自然事件配置的正整数，落进窝点池后是被反复衰减的浮点余量。
	 *  编辑器可编辑字段：由离线导入脚本或编辑器写入，运行时只读取不回写；非有限或负值无效。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	double Fragrant = 0.0;

	/** 发酵味轴上的量；投掷时是装备或自然事件配置的正整数，落进窝点池后是被反复衰减的浮点余量。
	 *  编辑器可编辑字段：由离线导入脚本或编辑器写入，运行时只读取不回写；非有限或负值无效。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	double Fermented = 0.0;

	/** 三轴之和，也就是策划口径的 Total；衰减归零判定和咬钩间隔公式都直接消费这个标量。 */
	double Total() const
	{
		return Fishy + Fragrant + Fermented;
	}

	/** 腥味在本池中的占比；Total 为零或非正时返回 0，让调用方拿不到"无窝料时腥占比 100%"这种伪事实。 */
	double FishyShare() const
	{
		const double Sum = Total();
		return Sum > 0.0 ? Fishy / Sum : 0.0;
	}

	/** 香味在本池中的占比；Total 为零或非正时返回 0，语义同 FishyShare。 */
	double FragrantShare() const
	{
		const double Sum = Total();
		return Sum > 0.0 ? Fragrant / Sum : 0.0;
	}

	/** 发酵味在本池中的占比；Total 为零或非正时返回 0，语义同 FishyShare。 */
	double FermentedShare() const
	{
		const double Sum = Total();
		return Sum > 0.0 ? Fermented / Sum : 0.0;
	}

	/** 校验一次投掷向量能否作为服务器窝料输入；负值、非有限值和全零均拒绝。
	 *  这里不强制整数：整数是策划对配置与投掷值的数据口径，而窝点池的运行值本来就是小数，
	 *  在同一个校验里强制整数会把合法的衰减余量也判成非法。鱼种偏好仍由数据表推导，不从本 DTO 反推。 */
	bool IsValidContribution() const
	{
		return FMath::IsFinite(Fishy) && FMath::IsFinite(Fragrant) && FMath::IsFinite(Fermented)
			&& Fishy >= 0.0 && Fragrant >= 0.0 && Fermented >= 0.0
			&& (Fishy > 0.0 || Fragrant > 0.0 || Fermented > 0.0);
	}
};

/** 鱼种被哪一味窝料吸引的闭合取值；它与 FCatChumVector 的腥、香、酵三轴一一对应，取值范围由鱼表格「喜爱窝料」列封闭。 */
UENUM(BlueprintType)
enum class ECatChumAffinity : uint8
{
	/** 鱼表格该行尚未给出窝料归属；它只表示数据缺口，落进正式目录必须被校验拦下。 */
	Unknown,
	/** 鱼表格标注该鱼吃腥味窝料，对应 FCatChumVector::Fishy。 */
	Fishy,
	/** 鱼表格标注该鱼吃香味窝料，对应 FCatChumVector::Fragrant。 */
	Fragrant,
	/** 鱼表格标注该鱼吃发酵味窝料，对应 FCatChumVector::Fermented。 */
	Fermented
};

/** 窝料的两个正式来源；二者必须提交同一个窝点写口，Source 只用于审计，不选择另一套状态或另一份池。 */
UENUM(BlueprintType)
enum class ECatAggregationSource : uint8
{
	/** 玩家把窝料投到某个坐标。 */
	PlayerChum,
	/** Environment 自然事件向某个坐标提交窝料输入。 */
	NaturalEvent
};

/** 玩家与自然事件共用的投窝命令；窝点由落点坐标解析，命令里不再出现关卡水域 ID。 */
USTRUCT(BlueprintType)
struct FCatAggregationCommand
{
	GENERATED_BODY()

	/** RequestId、窝点集合 ExpectedRevision 与服务器身份；自然事件使用服务私有身份键。 */
	UPROPERTY(BlueprintReadWrite)
	FCatDomainCommandContext Context;

	/** 窝料落点的世界坐标；它是解析并池还是新建窝点的唯一依据，也是新建窝点时的圆心。 */
	UPROPERTY(BlueprintReadWrite)
	FVector DropLocation = FVector::ZeroVector;

	/** 本次加进窝点池的三轴增量。 */
	UPROPERTY(BlueprintReadWrite)
	FCatChumVector Contribution;

	/** 触发来源；不改变 Revision、窝点归属或衰减节奏。 */
	UPROPERTY(BlueprintReadWrite)
	ECatAggregationSource Source = ECatAggregationSource::PlayerChum;
};

/** 一个窝点在某一时刻的只读事实；它是选鱼、咬钩间隔和诊断消费窝料池的唯一形态，不暴露可写运行态。 */
USTRUCT(BlueprintType)
struct FCatChumSpotSnapshot
{
	GENERATED_BODY()

	/** 查询坐标是否真的落在某个窝点圆内；false 时其余字段保持默认零值，代表"这里没有窝"。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasSpot = false;

	/** 命中窝点的圆心世界坐标；它由第一次投料的落点固定，并池不会移动它。 */
	UPROPERTY(BlueprintReadOnly)
	FVector Center = FVector::ZeroVector;

	/** 命中窝点的半径，单位与世界坐标一致；它由建窝那一刻的配置快照固定，之后改配置不影响已有窝点。 */
	UPROPERTY(BlueprintReadOnly)
	double Radius = 0.0;

	/** 命中窝点当前的三轴池值；占比与 Total 由 FCatChumVector 自带的访问器推导，不在快照里存第二份。 */
	UPROPERTY(BlueprintReadOnly)
	FCatChumVector Pool;

	/** 读到本快照时窝点集合的 Revision；调用方要提交投窝命令时必须原样带回，用来拒绝陈旧写入。 */
	UPROPERTY(BlueprintReadOnly)
	int64 AggregationRevision = 0;
};

/** 投窝提交结果；玩家与自然事件返回相同结构。 */
USTRUCT(BlueprintType)
struct FCatAggregationResult
{
	GENERATED_BODY()

	/** 公共命令终态；Revision 是窝点集合提交后的版本。 */
	UPROPERTY(BlueprintReadOnly)
	FCatDomainCommandResult Command;

	/** 本次落点所在窝点的最新快照；提交成功是并池或新建后的窝，拒绝时是落点当前所在的窝（可能是"无窝"）。 */
	UPROPERTY(BlueprintReadOnly)
	FCatChumSpotSnapshot Spot;
};

/** Fishing 发给 Environment 的纯读查询；Phase 快照来自 Run Core DTO，不携带 GameMode 写引用。 */
USTRUCT(BlueprintType)
struct FCatWaterQuery
{
	GENERATED_BODY()

	/** 希望判断的世界坐标；WaterRegion 只执行显式配置的 prototype 几何。 */
	UPROPERTY(BlueprintReadWrite)
	FVector WorldLocation = FVector::ZeroVector;

	/** 调用方消费的 Run 阶段快照；WaterQuery 只读 bFishingAllowed，不推导第二份阶段。 */
	UPROPERTY(BlueprintReadWrite)
	FCatRunPhaseSnapshot RunPhase;

	/** 与组合公开快照对应的 Run Revision；0 或与环境结果不一致时拒绝。 */
	UPROPERTY(BlueprintReadWrite)
	int64 RunRevision = 0;
};

/** 单个合法水域的公开读模型；只描述关卡几何，不携带窝料池或其他可写环境状态。 */
USTRUCT(BlueprintType)
struct FCatWaterRegionSnapshot
{
	GENERATED_BODY()

	/** 由关卡水域 Actor 显式配置的稳定区域 ID；鱼表以后只引用该值。 */
	UPROPERTY(BlueprintReadOnly)
	FName RegionId = NAME_None;

	/** WaterRegion 当前几何配置的服务器 Revision；Actor 配置变化后旧查询不得继续提交。 */
	UPROPERTY(BlueprintReadOnly)
	int64 RegionRevision = 0;

	/** 被命中的世界空间中心，只用于后续几何校验和诊断，不授权客户端写入。 */
	UPROPERTY(BlueprintReadOnly)
	FVector WorldCenter = FVector::ZeroVector;

	/** 显式 prototype 包围盒半尺寸；零向量表示未配置且不会返回成功快照。 */
	UPROPERTY(BlueprintReadOnly)
	FVector HalfExtent = FVector::ZeroVector;
};

/** WaterQuery 的只读结果；成功只证明位置属于已配置区域，不代表鱼可生成或抢抄合法。 */
USTRUCT(BlueprintType)
struct FCatWaterQueryResult
{
	GENERATED_BODY()

	/** 查询是否唯一命中合法水域。 */
	UPROPERTY(BlueprintReadOnly)
	bool bSucceeded = false;

	/** 查询失败原因；成功时为 None。 */
	UPROPERTY(BlueprintReadOnly)
	ECatWaterQueryError Error = ECatWaterQueryError::RegionNotFound;

	/** 成功时的不可变区域快照；失败时保持默认 Unset。 */
	UPROPERTY(BlueprintReadOnly)
	FCatWaterRegionSnapshot Region;

	/** 结果对应的 Run Revision；调用方提交事务前需继续核对。 */
	UPROPERTY(BlueprintReadOnly)
	int64 RunRevision = 0;
};
