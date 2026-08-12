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

/** 同一水域共享窝料池的三轴向量；腥、香、酵只表达输入维度，不包含吸引公式。 */
USTRUCT(BlueprintType)
struct FCatChumVector
{
	GENERATED_BODY()

	/** 腥味贡献；非有限或负值无效。 */
	UPROPERTY(BlueprintReadOnly)
	double Fishy = 0.0;

	/** 香味贡献；非有限或负值无效。 */
	UPROPERTY(BlueprintReadOnly)
	double Fragrant = 0.0;

	/** 发酵味贡献；非有限或负值无效。 */
	UPROPERTY(BlueprintReadOnly)
	double Fermented = 0.0;

	/** 校验共享聚鱼三轴能否作为服务器环境事实；负值、非有限值和全零均拒绝，鱼种偏好仍由数据表而非本 DTO 推导。 */
	bool IsValidContribution() const
	{
		return FMath::IsFinite(Fishy) && FMath::IsFinite(Fragrant) && FMath::IsFinite(Fermented)
			&& Fishy >= 0.0 && Fragrant >= 0.0 && Fermented >= 0.0
			&& (Fishy > 0.0 || Fragrant > 0.0 || Fermented > 0.0);
	}
};

/** 聚鱼机制的两个正式触发源；二者必须提交同一 WaterRegion 写口。 */
UENUM(BlueprintType)
enum class ECatAggregationSource : uint8
{
	/** 玩家向水域投入窝料。 */
	PlayerChum,
	/** Environment 自然事件向同一池提交聚鱼输入。 */
	NaturalEvent
};

/** 玩家与自然事件共用的聚鱼命令；Source 只用于审计，不选择另一套状态。 */
USTRUCT(BlueprintType)
struct FCatAggregationCommand
{
	GENERATED_BODY()

	/** RequestId、WaterRegion ExpectedRevision 与服务器身份；自然事件使用服务私有身份键。 */
	UPROPERTY(BlueprintReadWrite)
	FCatDomainCommandContext Context;

	/** 目标 WaterRegion 稳定 ID。 */
	UPROPERTY(BlueprintReadWrite)
	FName RegionId = NAME_None;

	/** 本次写入同一 ChumPool 的三轴增量。 */
	UPROPERTY(BlueprintReadWrite)
	FCatChumVector Contribution;

	/** 触发来源；不改变预算、Revision 或池所有权。 */
	UPROPERTY(BlueprintReadWrite)
	ECatAggregationSource Source = ECatAggregationSource::PlayerChum;
};

/** WaterRegion 聚鱼提交结果；两个来源返回相同结构。 */
USTRUCT(BlueprintType)
struct FCatAggregationResult
{
	GENERATED_BODY()

	/** 公共命令终态；Revision 是 ChumPool/Region 提交后的版本。 */
	UPROPERTY(BlueprintReadOnly)
	FCatDomainCommandResult Command;

	/** 提交后的同一共享三轴池快照。 */
	UPROPERTY(BlueprintReadOnly)
	FCatChumVector ChumPool;
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

/** 单个合法水域的公开读模型；不复制 Actor 引用、碰撞组件或可写环境状态。 */
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

	/** 当前服务器共享窝料池的只读值；玩家和自然事件都写这一份。 */
	UPROPERTY(BlueprintReadOnly)
	FCatChumVector ChumPool;
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
