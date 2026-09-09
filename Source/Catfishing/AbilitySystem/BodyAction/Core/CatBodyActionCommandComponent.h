#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "CatBodyActionCommandComponent.generated.h"

/** BodyAction 命令投递基类；它只拥有 GameplayEvent 投递细节，具体动作请求由各领域目录里的派生组件创建。 */
UCLASS(Abstract, ClassGroup=(Catfishing))
class CATFISHING_API UCatBodyActionCommandComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 建立事件驱动组件默认状态；基类不 Tick、不复制状态，只给派生命令组件复用一次性 GameplayEvent 投递。 */
	UCatBodyActionCommandComponent();

protected:
	/** 把已构造的专用载荷和固定事件标签投给拥有者当前 Pawn 的 ASC；无权威 ASC、空标签或无 Ability 响应时返回 false。 */
	bool SubmitPayload(UObject* Payload, FGameplayTag BodyActionEventTag) const;
};
