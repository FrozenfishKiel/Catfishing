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
};
