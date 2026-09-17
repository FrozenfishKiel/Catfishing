#include "Equipment/CatEquipmentItemDefinition.h"

#include "Equipment/Fragments/CatEquipmentFragment_Rod.h"
#include "Equipment/Fragments/CatEquipmentFragment_Bait.h"
#include "Equipment/Fragments/CatEquipmentFragment_Float.h"
#include "Equipment/Fragments/CatEquipmentFragment_Scoop.h"
#include "Equipment/Fragments/CatEquipmentFragment_Chum.h"
#include "Equipment/CatEquipmentInventoryItemInstance.h"
#include "Inventory/CatInventorySettings.h"
#include "Inventory/Fragments/CatEquippableItemFragment.h"
#include "Equipment/CatEquippedDefinition.h"

// 装备关联读取流程：只读物品配置的组合片段；没有独立装备配置时不退回旧的能力数组。
const UCatEquippedDefinition* UCatEquipmentItemDefinition::GetEquipmentDefinition() const
{
	const auto* Fragment = FindFragment<UCatEquippableItemFragment>();
	return Fragment ? Fragment->EquipmentDefinition.Get() : nullptr;
}

namespace
{
	// 定义身份 gate 流程：运行目录只接收显式开启、拥有稳定 ID 和功能路线的定义，具体能力再由定义自己的字段判断。
	bool IsEquipmentIdentityReady(const UCatEquipmentItemDefinition& Definition)
	{
		return Definition.bEnableRuntimeDefinition
			&& !(Definition.ItemId == 0)
			&& !Definition.FunctionalRouteId.IsNone();
	}

	// 默认堆叠容量读取流程：数量物品的项目级容量由 InventorySettings 提供，装备定义只负责在未显式配置时委托它。
	int32 ResolveDefaultInventoryQuantityStackLimit()
	{
		const UCatInventorySettings* InventorySettings = GetDefault<UCatInventorySettings>();
		return InventorySettings != nullptr ? InventorySettings->GetDefaultQuantityStackLimit() : MAX_int32;
	}

}

// 库存展示名读取流程：装备资产自己的 DisplayName 是策划维护文本，空文本回退由背包和商店展示模型处理。
FText UCatEquipmentItemDefinition::GetInventoryDisplayName() const
{
	return DisplayName;
}

// 库存说明读取流程：装备资产自己的 Description 继续作为详情文本来源，玩法字段仍由下游系统读取。
FText UCatEquipmentItemDefinition::GetInventoryDescription() const
{
	return Description;
}

// 库存缩略图读取流程：装备资产自己的 Thumbnail 是当前 UI 资源来源，库存格只保存实例和数量。
TSoftObjectPtr<UTexture2D> UCatEquipmentItemDefinition::GetInventoryThumbnail() const
{
	return Thumbnail;
}

// 库存运行校验流程：装备资产入库时复用装备完整性 gate；普通片段物品必须使用 UCatInventoryItemDefinition。
bool UCatEquipmentItemDefinition::IsInventoryRuntimeDefinitionReady() const
{
	return IsRuntimeDefinitionReady() && GetPreferredInstanceType() != nullptr;
}

// 装备库存实例类型读取流程：显式配置只能收窄为装备实例子类，错误配置直接拒绝，避免普通库存实例丢失耐久等装备状态。
TSubclassOf<UCatInventoryItemInstance> UCatEquipmentItemDefinition::GetPreferredInstanceType() const
{
	if (PreferredInstanceType != nullptr)
	{
		return PreferredInstanceType->IsChildOf(UCatEquipmentInventoryItemInstance::StaticClass())
			? PreferredInstanceType : nullptr;
	}
	return UCatEquipmentInventoryItemInstance::StaticClass();
}

// 装备堆叠上限读取流程：显式 MaxStackSize 优先；非数量物一格一件，数量物未显式配置时使用库存项目默认堆叠容量。
int32 UCatEquipmentItemDefinition::GetMaxStackCount() const
{
	if (MaxStackSize > 0)
	{
		return FMath::Max(1, MaxStackSize);
	}
	if (!bRunConsumable)
	{
		return 1;
	}
	return ResolveDefaultInventoryQuantityStackLimit();
}

// 鱼竿槽 ID 读取流程：返回进程内稳定 FName，只表达既有钓具选择槽，不参与物品用途分类。
FName UCatEquipmentItemDefinition::FishingRodLoadoutSlotId()
{
	static const FName SlotId(TEXT("Rod"));
	return SlotId;
}

// 鱼饵槽 ID 读取流程：返回进程内稳定 FName，只表达既有钓具选择槽。
FName UCatEquipmentItemDefinition::FishingBaitLoadoutSlotId()
{
	static const FName SlotId(TEXT("Bait"));
	return SlotId;
}

// 鱼漂槽 ID 读取流程：返回进程内稳定 FName，只表达既有钓具选择槽。
FName UCatEquipmentItemDefinition::FishingFloatLoadoutSlotId()
{
	static const FName SlotId(TEXT("Float"));
	return SlotId;
}

// 抄网槽 ID 读取流程：返回进程内稳定 FName，只表达既有钓具选择槽。
FName UCatEquipmentItemDefinition::ScoopNetLoadoutSlotId()
{
	static const FName SlotId(TEXT("ScoopNet"));
	return SlotId;
}

// 鱼竿能力查询流程：钓具槽和部署类必须有效，再读取鱼竿片段验证长度、磨损和锚点；不再推测其他用途字段的零值。
bool UCatEquipmentItemDefinition::CanServeFishingRod() const
{
	const UCatEquipmentFragment_Rod* Rod = FindFragment<UCatEquipmentFragment_Rod>();
	return IsEquipmentIdentityReady(*this) && LoadoutSlotId == FishingRodLoadoutSlotId()
		&& !bRunConsumable && GetEquipmentDefinition() && !GetEquipmentDefinition()->ActorClass.IsNull() && Rod != nullptr && Rod->IsRuntimeReady();
}

// 鱼饵能力查询流程：现有 Bait 选择要求数量物与饵料片段；特殊饵身份只归饵料片段。
bool UCatEquipmentItemDefinition::CanServeFishingBait() const
{
	const UCatEquipmentFragment_Bait* Bait = FindFragment<UCatEquipmentFragment_Bait>();
	return IsEquipmentIdentityReady(*this) && LoadoutSlotId == FishingBaitLoadoutSlotId()
		&& bRunConsumable && Bait != nullptr && Bait->IsRuntimeReady();
}

// 浮漂能力查询流程：现有 Float 选择要求单件装备与合法的抛投片段；没有片段则拒绝该槽位。
bool UCatEquipmentItemDefinition::CanServeFishingFloat() const
{
	const UCatEquipmentFragment_Float* Float = FindFragment<UCatEquipmentFragment_Float>();
	return IsEquipmentIdentityReady(*this) && LoadoutSlotId == FishingFloatLoadoutSlotId()
		&& !bRunConsumable && Float != nullptr && Float->IsRuntimeReady();
}

// 抄网能力查询流程：先核对现有槽位和单件语义，再由范围片段判断能否进入捕获流程。
bool UCatEquipmentItemDefinition::CanServeScoopNet() const
{
	const UCatEquipmentFragment_Scoop* Scoop = FindFragment<UCatEquipmentFragment_Scoop>();
	return IsEquipmentIdentityReady(*this) && LoadoutSlotId == ScoopNetLoadoutSlotId()
		&& !bRunConsumable && Scoop != nullptr && Scoop->IsRuntimeReady();
}

// 窝料能力查询流程：当前投放规则只接受不占钓具槽的数量物，再验证水域影响片段；查询本身不扣库存，数量由 Use 提交阶段消耗。
bool UCatEquipmentItemDefinition::CanServeChumPlacement() const
{
	const UCatEquipmentFragment_Chum* Chum = FindFragment<UCatEquipmentFragment_Chum>();
	return IsEquipmentIdentityReady(*this) && LoadoutSlotId.IsNone()
		&& bRunConsumable && Chum != nullptr && Chum->IsRuntimeReady();
}

// 钓具选择兼容校验流程：槽位是既有四槽读模型的契约，只在装备领域映射到对应能力；不限制库存可接受的其他片段类型。
bool UCatEquipmentItemDefinition::CanServeFishingLoadoutSlot(const FName SlotId) const
{
	if (SlotId == FishingRodLoadoutSlotId()) { return CanServeFishingRod(); }
	if (SlotId == FishingBaitLoadoutSlotId()) { return CanServeFishingBait(); }
	if (SlotId == FishingFloatLoadoutSlotId()) { return CanServeFishingFloat(); }
	if (SlotId == ScoopNetLoadoutSlotId()) { return CanServeScoopNet(); }
	return false;
}

// 装备运行校验流程：稳定目录身份与全部片段都必须有效；新装备能力由片段及实例子类接入，不需要改中央枚举或补一个分支。
bool UCatEquipmentItemDefinition::IsRuntimeDefinitionReady() const
{
	return IsEquipmentIdentityReady(*this) && Super::IsInventoryRuntimeDefinitionReady();
}
