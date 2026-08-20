#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Environment/CatWaterTypes.h"
#include "CatWaterQuerySubsystem.generated.h"

/** Environment 的只读水域查询入口；按需扫描当前 World 的配置 Actor，不缓存或写入第二份环境状态。 */
UCLASS()
class CATFISHING_API UCatWaterQuerySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 校验 Run Phase/Revision 后唯一命中水域；重叠区域在优先级未裁时返回 AmbiguousRegion。 */
	FCatWaterQueryResult QueryWaterRegion(const FCatWaterQuery& Query) const;

	/**
	 * 按稳定 RegionId 找出关卡里那一片水域的世界中心。
	 *
	 * 它与 QueryWaterRegion 是反方向的两件事：那边问"这个坐标算不算水"，这里问"那片水在哪儿"。
	 * 自然事件需要一个落在水里的投窝点，而写死坐标会在关卡一改就失效，所以由几何自己回答。
	 * 关卡里同一个 RegionId 出现多次时返回 false——重名意味着关卡配置本身有歧义，随便挑一个会让落点悄悄漂到另一片水。
	 */
	bool TryGetRegionCenter(FName RegionId, FVector& OutWorldCenter) const;
};
