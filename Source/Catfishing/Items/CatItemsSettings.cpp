#include "Items/CatItemsSettings.h"

#include "Items/CatItemTypes.h"

// 容量读取流程：按稳定容器枚举返回对应显式值；未知类型与非正配置统一返回 0，使写事务保留 PolicyUndecided。
int32 UCatItemsSettings::GetContainerCapacity(const uint8 ContainerKindValue) const
{
	switch (static_cast<ECatContainerKind>(ContainerKindValue))
	{
	case ECatContainerKind::PersonalGuard:
		return FMath::Max(0, PersonalGuardCapacity);
	case ECatContainerKind::SharedFishTank:
		return FMath::Max(0, SharedFishTankCapacity);
	default:
		return 0;
	}
}
