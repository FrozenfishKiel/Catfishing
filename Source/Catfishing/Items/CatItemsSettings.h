#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatItemsSettings.generated.h"

/** 阶段 E Items 容量的 fail-closed 配置；容量属于产品数值，默认 0 时注册成功但所有新增鱼事务返回 PolicyUndecided。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Items"))
class CATFISHING_API UCatItemsSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 把容器种类映射到已裁容量供注册使用；未知种类或非正配置返回 0，使新增鱼事务拒绝而非默认无限容量。 */
	int32 GetContainerCapacity(uint8 ContainerKindValue) const;

	/** 个人鱼护容量；0 表示 Unset，不能用无限容量替代产品决定。 */
	UPROPERTY(Config, EditAnywhere, Category = "Capacity", meta = (ClampMin = "0"))
	int32 PersonalGuardCapacity = 0;

	/** 一局共享鱼缸容量；0 表示 Unset，营地 Actor 不会自行选择默认。 */
	UPROPERTY(Config, EditAnywhere, Category = "Capacity", meta = (ClampMin = "0"))
	int32 SharedFishTankCapacity = 0;
};
