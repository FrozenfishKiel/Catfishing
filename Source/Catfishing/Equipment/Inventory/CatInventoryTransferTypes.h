#pragma once

#include "CoreMinimal.h"
#include "Equipment/CatEquipmentTypes.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "Inventory/CatInventoryComponent.h"

class AActor;

/** 原生服务端端点：Channel 由宿主解释，服务不依赖具体库存类型。 */
struct CATFISHING_API FCatInventoryEndpointRef
{
	TWeakObjectPtr<UObject> Host;
	FName Channel = TEXT("Stored");
	FGuid EntryId;
};

enum class ECatInventoryTransferMode : uint8
{
	/** 整格拖放：允许同定义部分合并及不同定义交换。 */
	DragToSlot,
	/** 精确数量转移：自动寻找空格，或放入显式目标格；不交换不同物品。 */
	TransferQuantity
};

struct CATFISHING_API FCatInventoryTransferRequest
{
	FGuid RequestId;
	AActor* Initiator = nullptr;
	FCatInventoryEndpointRef Source;
	FCatInventoryEndpointRef Target;
	/** -1 仅供原生服务器的旧 UnUse 适配，表示首次执行时读取当前版本；不可暴露为 RPC 选项。 */
	int64 ExpectedSourceRevision = 0;
	int64 ExpectedTargetRevision = 0;
	int32 SourceSlotIndex = INDEX_NONE;
	int32 TargetSlotIndex = INDEX_NONE;
	ECatInventoryTransferMode Mode = ECatInventoryTransferMode::TransferQuantity;
	int32 Quantity = 1;
	/** 旧槽位 RPC 可不提供；不可在每次重放时从变化后的源格重建。 */
	FGuid ExpectedSourceItemId;
};

struct CATFISHING_API FCatInventoryEndpointSnapshot
{
	TArray<FCatRunInventorySlot> Slots;
	int64 Revision = 0;
	int32 Capacity = 0;
	bool bCanReceive = true;
	bool bCanExtract = true;
	bool bAllowPartial = true;
	bool bAllowSwap = true;
};

/** 同宿主的多个端点一起静默写入，宿主版本只推进一次。 */
struct CATFISHING_API FCatInventoryEndpointWrite
{
	FName Channel = TEXT("Stored");
	FGuid EntryId;
	TArray<FCatRunInventorySlot> Slots;
	/** 提交前由通道从两端正式实例准备；投影不能反向生成另一份同身份物品。 */
	TArray<FCatInventoryEntry> FormalEntries;
};

struct CATFISHING_API FCatInventoryTransferResult
{
	FGuid RequestId;
	ECatDomainCommandError Error = ECatDomainCommandError::InvalidPayload;
	bool bCommitted = false;
	bool bReplayed = false;
	int64 SourceRevision = 0;
	int64 TargetRevision = 0;
	int32 MovedQuantity = 0;
	FCatRunInventorySlot Item;
};
