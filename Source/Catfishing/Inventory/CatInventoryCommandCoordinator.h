#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "CatInventoryCommandCoordinator.generated.h"

class AController;
class ACatCharacter;

/** 随身库存服务器命令协调器；Controller 只交出玩家意图，正式库存事实由 InventoryComponent 重读，移动和物品使用都从这里进入领域命令。 */
UCLASS()
class CATFISHING_API UCatInventoryCommandCoordinator : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 只在服务器 Game World 创建；客户端不能本地整理正式随身库存或推进库存版本。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** 整理当前玩家正式随身库存中的两个格子；成功后只刷新 Equipment 的迁移期投影，不让 Equipment 裁决库存移动。 */
	FCatDomainCommandResult MoveInventorySlot(AController* RequestingController, ACatCharacter* ControlledCharacter,
		FGuid RequestId, int64 ExpectedRevision, int32 SourceSlotIndex, int32 TargetSlotIndex);

	/** 使用当前玩家正式随身库存里的指定槽位；外部调用方不需要知道物品是钓具、耗材还是未来其他库存行为。 */
	FCatDomainCommandResult UseInventoryItemFromSlot(AController* RequestingController,
		ACatCharacter* ControlledCharacter, FGuid RequestId, int64 ExpectedInventoryRevision,
		int32 InventorySlotIndex);

	/** 旧版按槽位设为钓鱼选择入口；保留给历史 RPC，旧 EquipmentRevision 参数不再参与库存 Use 裁决。 */
	FCatDomainCommandResult SelectFishingItemFromInventorySlot(AController* RequestingController,
		ACatCharacter* ControlledCharacter, FGuid RequestId, int64 ExpectedInventoryRevision,
		int64 ExpectedEquipmentRevision, int32 InventorySlotIndex);

private:
	/** 执行库存物品使用的内部流程；无论新旧入口都只用正式库存版本保护被点击槽位。 */
	FCatDomainCommandResult UseInventoryItemFromSlotInternal(AController* RequestingController,
		ACatCharacter* ControlledCharacter, FGuid RequestId, int64 ExpectedInventoryRevision,
		int32 InventorySlotIndex);
};
