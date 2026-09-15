#include "Inventory/CatBackPackComponent.h"

#include "Inventory/CatInventorySettings.h"

// 构造流程：保留父类全部库存事实和复制行为，只声明背包是角色默认整批收货目标。
UCatBackPackComponent::UCatBackPackComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UnifiedInventoryIntakePriority = 100;
}

// 个人背包容量兼容流程：
// 1. 蓝图 CDO 可能仍带有旧的正数 NumSlots；若直接交给父类初始化会先生成旧格子，之后配置缩小又不能安全裁掉。
// 2. 因此只在库存数组尚为空且蓝图明确给过正数时，先把该默认值收束到项目设置的个人背包容量。
// 3. 随后完全复用父类创建槽位；动态测试或运行时显式容量从零开始的组件不在这里改写，保存恢复中的已有条目也不会受影响。
void UCatBackPackComponent::InitializeComponent()
{
	if (InventoryList.Entries.IsEmpty() && NumSlots > 0)
	{
		NumSlots = GetConfiguredPlayerSlotCapacity();
	}
	Super::InitializeComponent();
}

// 容量初始化流程：authority 读取唯一 InventorySettings 容量，交给父类建立或扩展空槽位；配置缺失时写入零而不伪造默认容量。
void UCatBackPackComponent::InitializePlayerInventorySlotCapacityFromAuthority()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	SetInventorySlotCountFromAuthority(GetConfiguredPlayerSlotCapacity());
}

// 容量读取流程：从唯一 InventorySettings 取得非负个人容量；设置缺失时返回零，让初始化保持空背包而不是重新引入历史默认值。
int32 UCatBackPackComponent::GetConfiguredPlayerSlotCapacity() const
{
	const UCatInventorySettings* InventorySettings = GetDefault<UCatInventorySettings>();
	return InventorySettings ? InventorySettings->GetPlayerInventorySlotCapacity() : 0;
}
