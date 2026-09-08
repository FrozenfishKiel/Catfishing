#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "CatEquipmentCommandCoordinator.generated.h"

class AController;
class ACatCampHubActor;
class ACatCharacter;

/** 钓具选择与营地修竿服务器命令协调器；Controller 只提交玩家意图，Equipment 只裁决钓鱼选择和鱼竿维护。 */
UCLASS()
class CATFISHING_API UCatEquipmentCommandCoordinator : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 只在服务器 Game World 创建；客户端不能本地修改钓具选择或鱼竿耐久。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** 按当前玩家的随身库存和解锁事实配置钓具选择；Controller 不解释装备定义、实例或选择规则。 */
	FCatDomainCommandResult ConfigureLoadout(AController* RequestingController, ACatCharacter* ControlledCharacter,
		FGuid RequestId, int64 ExpectedRevision, FName RodDefinitionId, FName BaitDefinitionId,
		FName FloatDefinitionId, FName ScoopNetDefinitionId, FGuid RodItemInstanceId,
		FGuid BaitItemInstanceId, FGuid FloatItemInstanceId, FGuid ScoopNetItemInstanceId);

	/** 在固定营地维修当前鱼竿；营地触达由 Camp 验证，耐久和浮木消耗由装备组件提交。 */
	FCatDomainCommandResult RepairRodAtCamp(AController* RequestingController, ACatCharacter* ControlledCharacter,
		ACatCampHubActor* Camp, FGuid RequestId, int64 ExpectedEquipmentRevision);
};
