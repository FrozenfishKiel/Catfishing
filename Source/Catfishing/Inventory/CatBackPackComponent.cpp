#include "Inventory/CatBackPackComponent.h"

#include "Inventory/CatInventorySettings.h"
#include "Growth/CatGrowthComponent.h"

// 构造流程：保留父类全部库存事实和复制行为，只声明背包是角色默认整批收货目标。
UCatBackPackComponent::UCatBackPackComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UnifiedInventoryIntakePriority = 100;
}

// 个人背包容量兼容流程：
// 1. 蓝图 CDO 可能仍带有旧的正数 NumSlots；若直接交给父类初始化会先生成旧格子，之后配置缩小又不能安全裁掉。
// 2. 因此只在库存数组尚为空且蓝图明确给过正数时，先把该默认值收束到项目基础容量与角色成长容量之和。
// 3. 随后完全复用父类创建槽位；动态测试或运行时显式容量从零开始的组件不在这里改写，保存恢复中的已有条目也不会受影响。
void UCatBackPackComponent::InitializeComponent()
{
	if (InventoryList.Entries.IsEmpty() && NumSlots > 0)
	{
		NumSlots = GetConfiguredPlayerSlotCapacity();
	}
	Super::InitializeComponent();
}

// 容量初始化流程：仅 authority 合并项目基础容量与角色成长加成，再交给父类建立或扩展槽位；已有物品的缩容边界仍由父类裁决。
void UCatBackPackComponent::InitializePlayerInventorySlotCapacityFromAuthority()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	SetInventorySlotCountFromAuthority(GetConfiguredPlayerSlotCapacity());
}

// 容量读取流程：读取项目基础格数与角色成长加成，缺失的一项按零处理；相加后限制到非负 int32 范围，供初始化和成长扩容共用。
int32 UCatBackPackComponent::GetConfiguredPlayerSlotCapacity() const
{
	const UCatInventorySettings* InventorySettings = GetDefault<UCatInventorySettings>();
	const auto* Growth = GetOwner() ? GetOwner()->FindComponentByClass<UCatGrowthComponent>() : nullptr;
	const double Bonus = Growth ? Growth->GetTotalMagnitude(ECatGrowthOptionId::InventorySlots) : 0.0;
	return static_cast<int32>(FMath::Clamp(
		double(InventorySettings ? InventorySettings->GetPlayerInventorySlotCapacity() : 0) + Bonus,
		0.0, double(MAX_int32)));
}
