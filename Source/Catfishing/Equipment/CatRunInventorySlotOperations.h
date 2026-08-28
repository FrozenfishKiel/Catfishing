#pragma once

#include "CoreMinimal.h"
#include "Equipment/CatEquipmentTypes.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "Templates/Function.h"

namespace CatRunInventorySlotOperations
{
	/** 运行库存格整理的纯结果；调用方用 bChanged 决定是否推进自己的 Revision。 */
	struct FMoveSlotsResult
	{
		/** 本次整理的领域错误；None 表示格子数组已经发生有效变化，AlreadyResolved 表示目标格已满等无变化成功态。 */
		ECatDomainCommandError Error = ECatDomainCommandError::None;

		/** 格子数组是否已经被修改；随身库存和营地仓库据此统一决定是否发布新快照。 */
		bool bChanged = false;
	};

	/** 在同一份 FCatRunInventorySlot 数组里移动、合并或交换物品；调用方负责 authority、RequestId、Revision 和容量补齐。 */
	FMoveSlotsResult MoveItemBetweenSlots(TArray<FCatRunInventorySlot>& InventorySlots,
		int32 SourceSlotIndex, int32 TargetSlotIndex, TFunctionRef<int32(FName)> ResolveStackLimit);

	/** 在两份不同 FCatRunInventorySlot 数组之间移动、合并或交换物品；调用方负责双方 authority、Revision、容量补齐和发布。 */
	FMoveSlotsResult MoveItemBetweenSlotArrays(TArray<FCatRunInventorySlot>& SourceInventorySlots,
		int32 SourceSlotIndex, TArray<FCatRunInventorySlot>& TargetInventorySlots,
		int32 TargetSlotIndex, TFunctionRef<int32(FName)> ResolveStackLimit);
}
