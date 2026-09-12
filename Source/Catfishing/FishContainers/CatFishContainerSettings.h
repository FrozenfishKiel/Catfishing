#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatFishContainerSettings.generated.h"

/**
 * 共享鱼缸的一档容量升级；一档对应商店出售表里的一行设施类商品。
 * 设施类商品不进团队装备库，成交后直接作用在营地那口缸上（商店册 §3.1.2「鱼缸容量升级＝唯一设施类商品」）。
 */
USTRUCT()
struct FCatSharedFishTankCapacityUpgrade
{
	GENERATED_BODY()

	/** 这一档在商店出售表里的 DefinitionId；它不指向任何库存定义资产，交付路由按它认出「这是设施升级、不是入库物」。 */
	UPROPERTY(Config, EditAnywhere, Category = "Capacity")
	FName UpgradeDefinitionId = NAME_None;

	/** 买下这一档之后鱼缸的槽位容量；必须比前一档大，否则整张升级表按未配置处理。 */
	UPROPERTY(Config, EditAnywhere, Category = "Capacity", meta = (ClampMin = "1"))
	int32 Capacity = 0;
};

/** 鱼缸与鱼护容量的 fail-closed 配置；容量属于产品数值，默认 0 时注册成功但所有新增鱼事务返回 PolicyUndecided。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Fish Containers"))
class CATFISHING_API UCatFishContainerSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 把容器种类映射到已裁容量供注册使用；未知种类或非正配置返回 0，使新增鱼事务拒绝而非默认无限容量。 */
	int32 GetContainerCapacity(uint8 ContainerKindValue) const;

	/**
	 * 共享鱼缸的**初始**容量（设计：10 条）；0 表示 Unset，鱼缸 Actor 回退到自己的编辑器容量并记一条 Warning。
	 * 升级后的容量不写在这里，走 SharedFishTankCapacityUpgrades 的档位表。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Capacity", meta = (ClampMin = "0"))
	int32 SharedFishTankCapacity = 0;

	/**
	 * 共享鱼缸的容量升级档，按购买顺序排列（设计：20、30 两档）。
	 * 空数组＝本局不提供升级，鱼缸停在初始档；这是「没配置就没有这条玩法」，不是把鱼缸判死。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Capacity")
	TArray<FCatSharedFishTankCapacityUpgrade> SharedFishTankCapacityUpgrades;

	/** 读取指定档位的鱼缸容量；档位 0 是初始档，越界或配置非法时返回 0，让调用方回退而不是自行猜一个容量。 */
	int32 GetSharedFishTankCapacityForTier(int32 TierIndex) const;

	/**
	 * 把一个商店 DefinitionId 解析成它对应的升级档序号（从 1 开始，1 表示第一档升级）。
	 * 不是升级商品时返回 INDEX_NONE；商店交付路由用它区分「设施升级」和「入库物」。
	 */
	int32 FindSharedFishTankUpgradeTierByDefinitionId(FName UpgradeDefinitionId) const;

	/** 一只可交互鱼护箱子的容量；0 表示 Unset，箱子不会自行选择默认。 */
	UPROPERTY(Config, EditAnywhere, Category = "Capacity", meta = (ClampMin = "0"))
	int32 FishGuardCapacity = 0;
};
