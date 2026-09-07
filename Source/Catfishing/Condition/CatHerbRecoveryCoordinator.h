#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "CatHerbRecoveryCoordinator.generated.h"

class ACatCharacter;
class AController;

/** 草药救援服务器协调器；把正式库存扣草药和 Condition 恢复串成一条先扣实物、再改身体事实的命令。 */
UCLASS()
class CATFISHING_API UCatHerbRecoveryCoordinator : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 只在服务器 Game World 创建；客户端不能本地扣草药或恢复倒地目标。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** 消费当前玩家指定草药实例并恢复目标 Character；正式库存存在时以它验证草药事实，库存提交失败时不会修改目标身体状态。 */
	FCatDomainCommandResult UseHerbOnCharacter(AController* HelpingController, ACatCharacter* TargetCharacter,
		FGuid RequestId, int64 ExpectedEquipmentRevision, FGuid HerbItemInstanceId);
};
