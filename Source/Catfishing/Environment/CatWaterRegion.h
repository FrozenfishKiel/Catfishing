#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Environment/CatWaterTypes.h"
#include "CatWaterRegion.generated.h"

/** 关卡里一个显式配置的中性水域；只提供只读 prototype 包围盒，不依赖 Experimental Water 插件。 */
UCLASS(BlueprintType)
class CATFISHING_API ACatWaterRegion : public AActor
{
	GENERATED_BODY()

public:
	/** 关闭 Tick 与复制；区域是关卡 authority 配置，客户端不把它当环境真相副本。 */
	ACatWaterRegion();

	/** 验证显式 gate、稳定 ID、正 Revision 和有限正包围盒；默认对象因此不可被查询命中。 */
	bool IsRuntimeConfigured() const;

	/** 在显式 axis-aligned prototype 几何中判断世界点；配置无效时始终返回 false。 */
	bool ContainsWorldPoint(const FVector& WorldPoint) const;

	/** 构造只读查询快照；仅在 IsRuntimeConfigured 成立后调用。 */
	FCatWaterRegionSnapshot MakeSnapshot() const;

	/** 玩家窝料与自然事件共用的唯一 ChumPool 写口；按 RequestId/Revision/预算原子提交。 */
	FCatAggregationResult ContributeAggregation(const FCatAggregationCommand& Command);

	/** 只读验证一条聚鱼命令是否可立即提交；跨 Equipment→WaterRegion 协调先调用它，避免扣除耗材后才发现池拒绝。 */
	ECatDomainCommandError ValidateAggregation(const FCatAggregationCommand& Command) const;

	/** 鱼表与环境 DTO 使用的稳定区域 ID；默认 None 表示未接线。 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water")
	FName RegionId = NAME_None;

	/** 关卡作者对临时 AABB 查询的显式 gate；默认关闭，不把 Actor 位置误当最终岸线。 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water|Prototype")
	bool bEnablePrototypeBounds = false;

	/** 相对 Actor 位置的 prototype 包围盒中心；仅在 gate 开启后读取。 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water|Prototype")
	FVector LocalCenterOffset = FVector::ZeroVector;

	/** prototype 包围盒半尺寸；任一轴非正或非有限均视为 Unset。 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water|Prototype")
	FVector HalfExtent = FVector::ZeroVector;

	/** 关卡几何版本；WaterQuery 把它写入快照，聚鱼写入不会改变它。 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water|Prototype", meta = (ClampMin = "1"))
	int64 GeometryRevision = 0;

	/** 聚鱼 Aggregate 显式启用 gate；默认关闭。 */
	UPROPERTY(EditInstanceOnly, Category = "Water|Aggregation")
	bool bEnableAggregation = false;

	/** 三轴共享总预算；0 表示 Unset，不接受任何来源写入。 */
	UPROPERTY(EditInstanceOnly, Category = "Water|Aggregation", meta = (ClampMin = "0"))
	double AggregationBudget = 0.0;

private:
	/** 当前共享 ChumPool 的运行时版本，只用于聚鱼命令的并发校验和诊断。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Water|Aggregation", meta = (AllowPrivateAccess = "true"))
	int64 AggregationRevision = 1;

	/** 当前玩家与自然事件共用的服务器 ChumPool。 */
	FCatChumVector ChumPool;

	/** 聚鱼命令首次终态缓存；同 RequestId 不重复消耗预算。 */
	TMap<FString, FCatAggregationResult> AggregationTerminalCache;
};
