#include "FishContainers/CatFishContainerSettings.h"

#include "FishContainers/CatFishContainerTypes.h"

// 容量读取流程：按稳定容器枚举返回对应显式值；未知类型与非正配置统一返回 0，使写事务保留 PolicyUndecided。
int32 UCatFishContainerSettings::GetContainerCapacity(const uint8 ContainerKindValue) const
{
	switch (static_cast<ECatContainerKind>(ContainerKindValue))
	{
	case ECatContainerKind::SharedFishTank:
		return FMath::Max(0, SharedFishTankCapacity);
	case ECatContainerKind::FishGuard:
		return FMath::Max(0, FishGuardCapacity);
	default:
		return 0;
	}
}
