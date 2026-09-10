#include "UI/Inventory/CatInventoryModel.h"

// 按参考项目的顺序先替换显示列表，再通知观察者；不等待命令回执、不判断是否允许下一次点击。
void UCatInventoryModel::SetInventoryList(const TArray<FCatInventoryEntry>& InInventoryList)
{
	InventoryList = InInventoryList;
	OnInventoryListChanged.Broadcast();
}

// 直接返回这份 Model 的列表；每个库存组件拥有自己的 Model，不在读取时选择背包、营地或鱼护分支。
const TArray<FCatInventoryEntry>& UCatInventoryModel::GetInventoryList() const
{
	return InventoryList;
}