#include "Items/CatEquipmentItem.h"

#include "Equipment/CatEquipmentDefinition.h"
#include "Logging/CatLog.h"

// 定义写入流程：生成器把唯一装备资产写入本 Actor；不在这里创建实例，避免未通过库存预检就产生孤立运行对象。
void ACatEquipmentItem::SetEquipmentDefinition(UCatEquipmentDefinition* InEquipmentDefinition) { EquipmentDefinition = InEquipmentDefinition; }

// 定义读取流程：返回当前静态装备资产，调用方只能读取身份和配置，库存实例仍由收货事务创建。
UCatEquipmentDefinition* ACatEquipmentItem::GetEquipmentDefinition() const { return EquipmentDefinition; }

// 生成配置流程：读取装备运行就绪状态并记录异常；不禁用拾取碰撞，未部署装备仍必须走通用世界物拾取链。
void ACatEquipmentItem::InitializeActorSpawnConfig()
{
	if (!EquipmentDefinition || !EquipmentDefinition->IsInventoryRuntimeDefinitionReady())
	{
		UE_LOG(LogCatfishing, Warning, TEXT("Event=equipment_item_spawn_config_invalid Item=%s Definition=%s"),
			*GetNameSafe(this), *GetNameSafe(EquipmentDefinition));
	}
}

// 收货批次流程：存在装备定义时生成数量为一的条目，就绪状态由库存收货检查；仅未指定定义时使用基础物的显式批次配置。
FCatInventoryReceiveBatch ACatEquipmentItem::GetPickupInventory() const
{
	if (!EquipmentDefinition)
	{
		return Super::GetPickupInventory();
	}
	FCatInventoryReceiveBatch PickupBatch;
	FCatInventoryDefinitionEntry& Entry = PickupBatch.DefinitionEntries.AddDefaulted_GetRef();
	Entry.Count = 1;
	Entry.ItemDefinition = EquipmentDefinition;
	Entry.ItemInstanceClass = EquipmentDefinition->GetPreferredInstanceType();
	return PickupBatch;
}
