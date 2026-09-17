#include "FishContainers/CatFishContainerSettings.h"

#include "FishContainers/CatFishContainerTypes.h"

// 容量读取流程：按稳定容器枚举返回对应显式值；未知类型与非正配置统一返回 0，使写事务保留 PolicyUndecided。
int32 UCatFishContainerSettings::GetContainerCapacity(const uint8 ContainerKindValue) const
{
	switch (static_cast<ECatContainerKind>(ContainerKindValue))
	{
	case ECatContainerKind::SharedFishTank:
		// 这里返回的是**初始档**。升级后的实际容量由鱼缸 Actor 自己按当前档位持有，
		// 容器服务只需要知道「这类容器配没配」，不参与逐口缸的运行档位。
		return FMath::Max(0, SharedFishTankCapacity);
	case ECatContainerKind::FishGuard:
		return FMath::Max(0, FishGuardCapacity);
	default:
		return 0;
	}
}

// 档位容量读取流程：0 档是初始档，其余按升级表顺序取；任何一档非正或不严格递增都判整张表未配置，返回 0 让调用方回退。
int32 UCatFishContainerSettings::GetSharedFishTankCapacityForTier(const int32 TierIndex) const
{
	const int32 BaseCapacity = FMath::Max(0, SharedFishTankCapacity);
	if (TierIndex <= 0)
	{
		return BaseCapacity;
	}
	if (BaseCapacity <= 0 || !SharedFishTankCapacityUpgrades.IsValidIndex(TierIndex - 1))
	{
		return 0;
	}
	int32 PreviousCapacity = BaseCapacity;
	for (int32 Index = 0; Index < TierIndex; ++Index)
	{
		const FCatSharedFishTankCapacityUpgrade& Upgrade = SharedFishTankCapacityUpgrades[Index];
		if ((Upgrade.UpgradeItemId == 0) || Upgrade.Capacity <= PreviousCapacity)
		{
			return 0;
		}
		PreviousCapacity = Upgrade.Capacity;
	}
	return PreviousCapacity;
}

// 升级商品识别流程：按稳定 ID 在升级表里查序号；重复 ID 与空 ID 都当成没配，返回 INDEX_NONE 让交付按普通入库物处理。
int32 UCatFishContainerSettings::FindSharedFishTankUpgradeTierByItemId(const int32  UpgradeItemId) const
{
	if ((UpgradeItemId == 0))
	{
		return INDEX_NONE;
	}
	int32 FoundTier = INDEX_NONE;
	for (int32 Index = 0; Index < SharedFishTankCapacityUpgrades.Num(); ++Index)
	{
		if (SharedFishTankCapacityUpgrades[Index].UpgradeItemId != UpgradeItemId)
		{
			continue;
		}
		if (FoundTier != INDEX_NONE)
		{
			return INDEX_NONE;
		}
		FoundTier = Index + 1;
	}
	return FoundTier;
}
