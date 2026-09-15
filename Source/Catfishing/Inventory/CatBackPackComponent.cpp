#include "Inventory/CatBackPackComponent.h"

#include "Inventory/CatInventorySettings.h"
#include "Growth/CatGrowthComponent.h"

// 构造流程：保留父类全部库存事实和复制行为，只声明背包是角色默认整批收货目标。
UCatBackPackComponent::UCatBackPackComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UnifiedInventoryIntakePriority = 100;
}

// 容量初始化流程：authority 读取唯一 InventorySettings 容量，交给父类建立或扩展空槽位；配置缺失时写入零而不伪造默认容量。
void UCatBackPackComponent::InitializePlayerInventorySlotCapacityFromAuthority()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	const UCatInventorySettings* InventorySettings = GetDefault<UCatInventorySettings>();
	const auto* Growth = GetOwner()->FindComponentByClass<UCatGrowthComponent>();
	const double Bonus = Growth ? Growth->GetTotalMagnitude(ECatGrowthOptionId::InventorySlots) : 0.0;
	SetInventorySlotCountFromAuthority(static_cast<int32>(FMath::Min(double(MAX_int32),
		double(InventorySettings ? InventorySettings->GetPlayerInventorySlotCapacity() : 0) + Bonus)));
}
