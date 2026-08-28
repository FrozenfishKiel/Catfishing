#include "Equipment/CatRunInventorySlotOperations.h"

// 同源格整理流程：
// 1. 先校验源/目标下标和源格内容，调用方已经处理 RequestId、Revision、authority 与容量补齐。
// 2. 目标空格时直接搬过去；同定义时按调用方提供的堆叠上限合并；不同定义时交换两个格。
// 3. 函数只改传入数组并返回是否变化，不推进任何 Actor/Component 的版本，避免公共仓库和随身库存各写一套移动规则。
CatRunInventorySlotOperations::FMoveSlotsResult CatRunInventorySlotOperations::MoveItemBetweenSlots(
	TArray<FCatRunInventorySlot>& InventorySlots, const int32 SourceSlotIndex, const int32 TargetSlotIndex,
	TFunctionRef<int32(FName)> ResolveStackLimit)
{
	FMoveSlotsResult Result;
	if (SourceSlotIndex < 0 || TargetSlotIndex < 0 || SourceSlotIndex == TargetSlotIndex
		|| !InventorySlots.IsValidIndex(SourceSlotIndex) || !InventorySlots.IsValidIndex(TargetSlotIndex))
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}

	FCatRunInventorySlot& SourceSlot = InventorySlots[SourceSlotIndex];
	FCatRunInventorySlot& TargetSlot = InventorySlots[TargetSlotIndex];
	if (SourceSlot.DefinitionId.IsNone() || SourceSlot.Quantity <= 0)
	{
		Result.Error = ECatDomainCommandError::NotFound;
		return Result;
	}
	if (TargetSlot.DefinitionId.IsNone() || TargetSlot.Quantity <= 0)
	{
		TargetSlot = SourceSlot;
		SourceSlot = FCatRunInventorySlot();
		Result.bChanged = true;
		Result.Error = ECatDomainCommandError::None;
		return Result;
	}
	if (TargetSlot.DefinitionId == SourceSlot.DefinitionId)
	{
		const int32 StackLimit = FMath::Max(1, ResolveStackLimit(SourceSlot.DefinitionId));
		const int32 Room = FMath::Max(0, StackLimit - TargetSlot.Quantity);
		const int32 MovedQuantity = FMath::Min(Room, SourceSlot.Quantity);
		if (MovedQuantity <= 0)
		{
			Result.Error = ECatDomainCommandError::AlreadyResolved;
			return Result;
		}
		TargetSlot.Quantity += MovedQuantity;
		SourceSlot.Quantity -= MovedQuantity;
		if (SourceSlot.Quantity <= 0)
		{
			SourceSlot = FCatRunInventorySlot();
		}
		Result.bChanged = true;
		Result.Error = ECatDomainCommandError::None;
		return Result;
	}

	const FCatRunInventorySlot PreviousTarget = TargetSlot;
	TargetSlot = SourceSlot;
	SourceSlot = PreviousTarget;
	Result.bChanged = true;
	Result.Error = ECatDomainCommandError::None;
	return Result;
}

// 跨源格转移流程：
// 1. 先校验两份数组和槽位；调用方已经确认这次操作允许同时改两个数据源。
// 2. 目标空格时搬过去；同定义时按堆叠上限合并；不同定义时交换，让玩家拖到哪个格子就落到哪个格子。
// 3. 函数只改传入的两份数组，不发布任何快照，避免背包和营地公共仓库的广播顺序藏在工具函数里。
CatRunInventorySlotOperations::FMoveSlotsResult CatRunInventorySlotOperations::MoveItemBetweenSlotArrays(
	TArray<FCatRunInventorySlot>& SourceInventorySlots, const int32 SourceSlotIndex,
	TArray<FCatRunInventorySlot>& TargetInventorySlots, const int32 TargetSlotIndex,
	TFunctionRef<int32(FName)> ResolveStackLimit)
{
	if (&SourceInventorySlots == &TargetInventorySlots)
	{
		return MoveItemBetweenSlots(SourceInventorySlots, SourceSlotIndex, TargetSlotIndex, ResolveStackLimit);
	}

	FMoveSlotsResult Result;
	if (SourceSlotIndex < 0 || TargetSlotIndex < 0
		|| !SourceInventorySlots.IsValidIndex(SourceSlotIndex)
		|| !TargetInventorySlots.IsValidIndex(TargetSlotIndex))
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}

	FCatRunInventorySlot& SourceSlot = SourceInventorySlots[SourceSlotIndex];
	FCatRunInventorySlot& TargetSlot = TargetInventorySlots[TargetSlotIndex];
	if (SourceSlot.DefinitionId.IsNone() || SourceSlot.Quantity <= 0)
	{
		Result.Error = ECatDomainCommandError::NotFound;
		return Result;
	}
	if (TargetSlot.DefinitionId.IsNone() || TargetSlot.Quantity <= 0)
	{
		TargetSlot = SourceSlot;
		SourceSlot = FCatRunInventorySlot();
		Result.bChanged = true;
		Result.Error = ECatDomainCommandError::None;
		return Result;
	}
	if (TargetSlot.DefinitionId == SourceSlot.DefinitionId)
	{
		const int32 StackLimit = FMath::Max(1, ResolveStackLimit(SourceSlot.DefinitionId));
		const int32 Room = FMath::Max(0, StackLimit - TargetSlot.Quantity);
		const int32 MovedQuantity = FMath::Min(Room, SourceSlot.Quantity);
		if (MovedQuantity <= 0)
		{
			Result.Error = ECatDomainCommandError::AlreadyResolved;
			return Result;
		}
		TargetSlot.Quantity += MovedQuantity;
		SourceSlot.Quantity -= MovedQuantity;
		if (SourceSlot.Quantity <= 0)
		{
			SourceSlot = FCatRunInventorySlot();
		}
		Result.bChanged = true;
		Result.Error = ECatDomainCommandError::None;
		return Result;
	}

	const FCatRunInventorySlot PreviousTarget = TargetSlot;
	TargetSlot = SourceSlot;
	SourceSlot = PreviousTarget;
	Result.bChanged = true;
	Result.Error = ECatDomainCommandError::None;
	return Result;
}
