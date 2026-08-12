#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatRunContracts.h"
#include "CatConfiguredEnvironmentProvider.generated.h"

/** 正式 Environment 只读实现；从局内 Run 时钟与显式设置产生天气/时段/事件，不反向写 Run。 */
UCLASS()
class CATFISHING_API UCatConfiguredEnvironmentProvider : public UObject, public ICatEnvironmentProvider
{
	GENERATED_BODY()

public:
	/** 消费不可变 Run 快照并按当前服务器世界时间返回环境轴；设置未就绪时给出结构化失败。 */
	virtual FCatEnvironmentResult EvaluateEnvironment(const FCatRunPhaseSnapshot& RunSnapshot, int64 RunRevision) const override;
};
