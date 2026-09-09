#pragma once

#include "CoreMinimal.h"
#include "Items/CatItemTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "CatContainerCommandCoordinator.generated.h"

class AController;
class ACatCharacter;
class ACatCampInventoryActor;

/** 容器类库存服务器协调器；拥有容器宿主复核、触达判断和 Items 转移提交，Controller 只负责 RPC 与回执。 */
UCLASS()
class CATFISHING_API UCatContainerCommandCoordinator : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 只在服务器 Game World 创建；客户端只能通过容器复制和 owning-client 结果观察库存事务。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** 在服务器重读两个可触达容器后提交物体转移；地面鱼护和共享鱼缸都按 Items 容器事实裁决。 */
	FCatDomainCommandResult TransferReachableObject(AController* RequestingController,
		ACatCharacter* ControlledCharacter, FGuid RequestId, ECatContainedObjectKind ObjectKind,
		FGuid ObjectInstanceId, FGuid SourceContainerId, ECatContainerKind SourceContainerKind,
		int32 SourceContainerSlotIndex, int64 ExpectedSourceRevision, FGuid TargetContainerId,
		ECatContainerKind TargetContainerKind, int32 TargetContainerSlotIndex,
		int64 ExpectedTargetRevision);

	/** 从可触达的地面鱼护把指定鱼放入服务器选出的首个共享鱼缸空格；客户端不能指定目标鱼缸或目标格。 */
	FCatDomainCommandResult StoreFishInReachableSharedTank(AController* RequestingController,
		ACatCharacter* ControlledCharacter, FGuid RequestId, FGuid FishInstanceId,
		FGuid SourceContainerId, int32 SourceContainerSlotIndex, int64 ExpectedSourceRevision);

	/** 从可触达营地公共仓库取指定数量到当前玩家随身库存；服务器重读仓库、距离和玩家正式 InventoryComponent，ExpectedInventoryRevision 不再代表 Equipment 快照。 */
	FCatDomainCommandResult WithdrawCampInventoryItem(AController* RequestingController,
		ACatCharacter* ControlledCharacter, ACatCampInventoryActor* CampInventory, FGuid RequestId,
		int64 ExpectedCampInventoryRevision, int32 SourceSlotIndex, int32 Quantity,
		int64 ExpectedInventoryRevision);

	/** 在可触达营地公共仓库内部移动、合并或交换两个格子；Controller 不参与公共仓库槽位规则。 */
	FCatDomainCommandResult MoveCampInventorySlot(AController* RequestingController,
		ACatCharacter* ControlledCharacter, ACatCampInventoryActor* CampInventory, FGuid RequestId,
		int64 ExpectedCampInventoryRevision, int32 SourceSlotIndex, int32 TargetSlotIndex);

	/** 把当前玩家随身库存格提交到可触达营地公共仓库目标格；SourceInventorySlotIndex 指向正式库存槽位，旧 Equipment 只在提交后跟随投影刷新。 */
	FCatDomainCommandResult DepositInventorySlotToCampInventory(AController* RequestingController,
		ACatCharacter* ControlledCharacter, ACatCampInventoryActor* CampInventory, FGuid RequestId,
		int64 ExpectedCampInventoryRevision, int32 TargetCampSlotIndex, int64 ExpectedInventoryRevision,
		int32 SourceInventorySlotIndex);

	/** 把可触达营地公共仓库格提交到当前玩家随身库存目标格；TargetInventorySlotIndex 指向正式库存槽位，旧 Equipment 只在提交后跟随投影刷新。 */
	FCatDomainCommandResult WithdrawCampInventoryItemToInventorySlot(AController* RequestingController,
		ACatCharacter* ControlledCharacter, ACatCampInventoryActor* CampInventory, FGuid RequestId,
		int64 ExpectedCampInventoryRevision, int32 SourceCampSlotIndex, int64 ExpectedInventoryRevision,
		int32 TargetInventorySlotIndex);
};
