#pragma once

#include "CoreMinimal.h"
#include "Items/CatItemTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "CatFishConsumptionCoordinator.generated.h"

class ACatCharacter;
class AController;

/** 直接吃鱼服务器协调器；把可触达容器校验、Items 不可逆消费和 Condition 食用效果收在 Items 侧的一条小接口后面。 */
UCLASS()
class CATFISHING_API UCatFishConsumptionCoordinator : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 只在服务器 Game World 创建；客户端只能通过容器复制和 owning-client 回执观察结果。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** 从地面鱼护箱子或共享鱼缸吃一条可触达的鱼；成功移除实物后才把食用效果提交给目标 Character。 */
	FCatFishConsumeResult ConsumeReachableFish(AController* RequestingController, ACatCharacter* EatingCharacter,
		FCatFishConsumeCommand Command);
};
