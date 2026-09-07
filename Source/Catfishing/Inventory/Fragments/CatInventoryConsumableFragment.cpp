#include "Inventory/Fragments/CatInventoryConsumableFragment.h"

// 可用性判断流程：库存层只关心是否允许发起扣量和扣量是否为正，不在这里判定玩法效果是否成功。
bool UCatInventoryConsumableFragment::HasUsableInventoryUse() const
{
	return bAllowUseFromInventory && ConsumeCount > 0;
}

// 扣量读取流程：把编辑器异常输入收束到至少 1，保证使用链不会提交零扣减事务。
int32 UCatInventoryConsumableFragment::GetConsumeCount() const
{
	return FMath::Max(1, ConsumeCount);
}
