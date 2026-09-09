#pragma once

#include "Inventory/CatInventoryComponent.h"

/** 让多份正式库存及其玩法锁全部落定后再发布；可嵌套，不另存库存事实。 */
class FCatInventoryMutationScope
{
public:
	explicit FCatInventoryMutationScope(UCatInventoryComponent* InInventory) : Inventory(InInventory)
	{
		if (Inventory.IsValid()) Inventory->BeginDeferredInventoryPublication();
	}
	~FCatInventoryMutationScope()
	{
		if (Inventory.IsValid()) Inventory->EndDeferredInventoryPublication();
	}
	FCatInventoryMutationScope(const FCatInventoryMutationScope&) = delete;
	FCatInventoryMutationScope& operator=(const FCatInventoryMutationScope&) = delete;
private:
	TWeakObjectPtr<UCatInventoryComponent> Inventory;
};
