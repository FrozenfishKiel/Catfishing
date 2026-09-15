#pragma once
#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "CatItemEffectApplication.generated.h"

/** 单次同步物品效果的执行凭据；只在服务器调用栈期间存在，不复制也不保存累计属性。 */
UCLASS()
class CATFISHING_API UCatItemEffectApplication : public UObject
{
	GENERATED_BODY()
public:
	/** 本次库存或容器事务身份；创建者写入，属性回调用它向成长层去重。 */
	FGuid RequestId;
	/** 真正提供效果的物品实例或容器；供 GE 上下文保留来源身份。 */
	UPROPERTY() TObjectPtr<UObject> ItemSource;
	/** 属性回调的同步结果；效果申请者据此决定提交还是退回实物。 */
	FCatDomainCommandResult Result;
};
