#pragma once

#include "CoreMinimal.h"
#include "Equipment/Inventory/CatInventoryTransferTypes.h"
#include "UObject/Interface.h"
#include "CatInventoryTransferEndpoint.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UCatInventoryTransferEndpoint : public UInterface
{
	GENERATED_BODY()
};

/** 库存仍由宿主持有；通道只协调副本预演、双端提交与提交后的发布。 */
class CATFISHING_API ICatInventoryTransferEndpoint
{
	GENERATED_BODY()

public:
	virtual const AActor* GetInventoryTransferAuthorityActor() const = 0;
	virtual UCatInventoryComponent* GetInventoryTransferInventory() const = 0;
	/** 只读预检；活动记录宿主在此裁决 Definition.UnUse、释放状态与使用锁。 */
	virtual ECatDomainCommandError ReadInventoryTransferEndpoint(FName Channel, FGuid EntryId,
		FCatInventoryEndpointSnapshot& OutSnapshot) const = 0;
	virtual int32 GetInventoryTransferStackLimit(FName DefinitionId) const = 0;
	/** 已通过预演的原生提交，不得失败、广播或调用可重入的玩法事件。 */
	virtual void ApplyInventoryTransferWritesSilently(TConstArrayView<FCatInventoryEndpointWrite> Writes,
		int64 NewRevision) = 0;
	virtual void PublishInventoryTransfer() = 0;
};
