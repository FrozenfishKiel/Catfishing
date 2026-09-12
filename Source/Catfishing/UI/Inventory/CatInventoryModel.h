#pragma once

#include "CoreMinimal.h"
#include "Inventory/CatInventoryComponent.h"
#include "UObject/Object.h"
#include "CatInventoryModel.generated.h"

/** 一份库存列表变化的本地通知；绑定这份 Model 的界面收到后重读列表。 */
DECLARE_MULTICAST_DELEGATE(FCatInventoryModelChanged);

/** 对应 AegisOdyssey 的 InventoryMenu Model：由一份库存组件持有，只接收该库存列表并通知观察它的 UI。 */
/*
 * 关于「背包装备栏四格」（主界面.md:97，2026-09-12 记）：本 Model 只有一份 InventoryList，没有装备栏投影，这是有意的。
 *
 * 两条口径同时开着，且互相不兼容：
 * - 设计参考稿画的是四格「鱼竿／鱼饵／窝料／鱼护」；
 * - 领域侧真实存在的四槽是 `FCatEquipmentLoadoutSnapshot` 的 Rod／Bait／Float／ScoopNet——只对上两格，
 *   另两格（浮漂、抄网）不是设计画的那两格（窝料、鱼护）。
 * - 09-09 晚已裁的方向又是第三种形态：「装备栏每槽一道具＋使用键」，并明写「代码按装备栏重构另排」。
 *
 * 三者对不上时把哪一套渲染出来都是替策划做决定，所以这里什么也不渲染，等装备栏重构那一轮一起做。
 * 要改的时候改的是这里加一份 loadout 投影＋背包 WBP 加四个槽控件，不是在 Equipment 侧另建一套槽。
 */
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
