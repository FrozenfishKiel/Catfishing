#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "CatEquipmentCommandCoordinator.generated.h"

class AController;
class ACatCampHubActor;
class ACatCharacter;

/** 钓具选择与随身背包服务器命令协调器；Controller 只提交玩家意图，背包整理写入正式库存，Equipment 只保留钓鱼选择。 */
UCLASS()
class CATFISHING_API UCatEquipmentCommandCoordinator : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 只在服务器 Game World 创建；客户端不能本地修改装备选择、正式随身库存或鱼竿耐久。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** 按当前玩家的随身库存和解锁事实配置钓具选择；Controller 不解释装备定义、实例或选择规则。 */
	FCatDomainCommandResult ConfigureLoadout(AController* RequestingController, ACatCharacter* ControlledCharacter,
		FGuid RequestId, int64 ExpectedRevision, FName RodDefinitionId, FName BaitDefinitionId,
		FName FloatDefinitionId, FName ScoopNetDefinitionId, FGuid RodItemInstanceId,
		FGuid BaitItemInstanceId, FGuid FloatItemInstanceId, FGuid ScoopNetItemInstanceId);

	/** 整理当前玩家正式随身库存中的两个格子；成功后再刷新 Equipment 的旧库存投影给未迁移消费者。 */
	FCatDomainCommandResult MoveInventorySlot(AController* RequestingController, ACatCharacter* ControlledCharacter,
		FGuid RequestId, int64 ExpectedRevision, int32 SourceSlotIndex, int32 TargetSlotIndex);

	/** 在固定营地维修当前鱼竿；营地触达由 Camp 验证，耐久和浮木消耗由装备组件提交。 */
	FCatDomainCommandResult RepairRodAtCamp(AController* RequestingController, ACatCharacter* ControlledCharacter,
		ACatCampHubActor* Camp, FGuid RequestId, int64 ExpectedEquipmentRevision);
};
