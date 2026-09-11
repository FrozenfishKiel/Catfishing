#pragma once

#include "CoreMinimal.h"
#include "Inventory/CatInventoryComponent.h"
#include "CatFishOnlyInventoryComponent.generated.h"

/** 只能接收实物鱼的正式库存组件；鱼护和鱼缸用它表达容量与槽位，移动、复制和使用仍完全沿用 InventoryComponent。 */
UCLASS(ClassGroup = (Catfishing), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class CATFISHING_API UCatFishOnlyInventoryComponent : public UCatInventoryComponent
{
	GENERATED_BODY()

public:
	/** 构造鱼专用库存组件；不增加第二套状态，只继承正式库存的复制和变化通知。 */
	UCatFishOnlyInventoryComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** 正式写入或交换槽位前复核进入目标格的运行时物品；返回 false 时父类会拒绝本次写入并保持原槽位不变。 */
	virtual bool CanAcceptInventoryEntryAtSlot(const FCatInventoryEntry& IncomingEntry,
		int32 TargetSlotIndex) const override;

protected:
	/** 批量入库预演时用静态定义判断目标格能否接收；返回 false 会让容量预检提前失败，避免稍后创建出不能进入鱼容器的实例。 */
	virtual bool CanAcceptInventoryDefinitionAtSlot(const UCatInventoryItemDefinition& IncomingDefinition,
		int32 TargetSlotIndex) const override;
};

