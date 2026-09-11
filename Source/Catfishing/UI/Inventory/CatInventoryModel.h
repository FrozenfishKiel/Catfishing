#pragma once

#include "CoreMinimal.h"
#include "Inventory/CatInventoryComponent.h"
#include "UObject/Object.h"
#include "CatInventoryModel.generated.h"

/** 一份库存列表变化的本地通知；绑定这份 Model 的界面收到后重读列表。 */
DECLARE_MULTICAST_DELEGATE(FCatInventoryModelChanged);

/** 对应 AegisOdyssey 的 InventoryMenu Model：由一份库存组件持有，只接收该库存列表并通知观察它的 UI。 */
UCLASS()
class CATFISHING_API UCatInventoryModel : public UObject
{
	GENERATED_BODY()

public:
	/** 写入库存组件提供的最新列表并通知 UI；列表只用于显示，服务器库存组件仍负责物品变更。 */
	void SetInventoryList(const TArray<FCatInventoryEntry>& InInventoryList);

	/** 读取本库存组件自己的显示列表；这个 Model 只代表一个库存源，调用方必须按槽位原序显示，避免重新拼回旧聚合页或 pending 状态。 */
	const TArray<FCatInventoryEntry>& GetInventoryList() const;

	/** 当前库存列表已更新；组件写入后广播，背包与外部库存各自的 UI 独立订阅和解绑。 */
	FCatInventoryModelChanged OnInventoryListChanged;

private:
	/** 所属库存最新的显示数据；组件提交或接收复制后写入，UI 只读它，列表随 Model 一起释放。 */
	UPROPERTY(Transient)
	TArray<FCatInventoryEntry> InventoryList;
};
