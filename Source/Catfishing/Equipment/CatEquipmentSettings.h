#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "CatEquipmentSettings.generated.h"

class UCatEquipmentDefinition;

/** 装备目录与失败预算调参；默认空目录/零损耗使装备命令 fail-closed。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Equipment"))
class CATFISHING_API UCatEquipmentSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 按稳定 ID 查唯一完整定义；重复或缺失返回空。 */
	UCatEquipmentDefinition* FindRuntimeDefinition(FName DefinitionId) const;

	/** 正式装备/道具清单；默认空且不扫描资产目录。 */
	UPROPERTY(Config, EditAnywhere, Category = "Catalog")
	TArray<TSoftObjectPtr<UCatEquipmentDefinition>> Definitions;

	/** 是否启用服务器装配入口的可信证明校验流程；Enabled 仍必须逐项通过 PlayerState 授权，本地 Profile 选择本身永不提升权限。 */
	UPROPERTY(Config, EditAnywhere, Category = "Loadout")
	ECatDomainPolicy ProfileLoadoutTrustPolicy = ECatDomainPolicy::Unset;

	/** 随身库存固定格数；后端按它限制入库，Inventory Model 按它渲染空格和已有物品。 */
	UPROPERTY(Config, EditAnywhere, Category = "Inventory", meta = (ClampMin = "0"))
	int32 InventorySlotCapacity = 24;

	/** 数量型物品在单个随身库存格里的堆叠上限；鱼饵、窝料、草药等共享这条库存格规则，0 表示同类物品可尽量堆在一个格子里。 */
	UPROPERTY(Config, EditAnywhere, Category = "Inventory", meta = (ClampMin = "0"))
	int32 InventoryQuantityStackCapacity = 0;

	/** 一次 DamageRod 失败预算扣除的耐久；0 表示公式/数值未裁。 */
	UPROPERTY(Config, EditAnywhere, Category = "FailureBudget", meta = (ClampMin = "0.0"))
	double RodFailureDurabilityLoss = 0.0;

	/** 修竿点消费的浮木定义 ID；None 表示维修链未配置。 */
	UPROPERTY(Config, EditAnywhere, Category = "Repair")
	FName DriftwoodDefinitionId = NAME_None;

	/**
	 * 受控 Starter 兜底开关；打开时服务器会在角色首次占有且选择为空时尝试选择库存已有的基础装备，并可额外发放窝料。
	 * 正式默认配置保持关闭，避免绕过商店或 Profile Grant 授权链；测试或诊断显式打开时仍经过目录、解锁和 Revision 校验。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "StarterFallback")
	bool bAutoConfigureStarterLoadout = false;

	/** Starter 兜底鱼竿定义 ID；只在兜底开关打开时读取，且该定义必须已经存在于角色随身库存。 */
	UPROPERTY(Config, EditAnywhere, Category = "StarterFallback")
	FName StarterRodDefinitionId = NAME_None;

	/** Starter 兜底鱼饵定义 ID；只为显式兜底选择提供完整组合，且至少需要库存中已有 1 份对应饵。 */
	UPROPERTY(Config, EditAnywhere, Category = "StarterFallback")
	FName StarterBaitDefinitionId = NAME_None;

	/** Starter 兜底鱼漂定义 ID；只在显式兜底流程中选择库存已有鱼漂，正式获取仍走商店或解锁授权。 */
	UPROPERTY(Config, EditAnywhere, Category = "StarterFallback")
	FName StarterFloatDefinitionId = NAME_None;

	/** Starter 兜底希望选择的抄网定义；Character 兜底流程只用它匹配随身库存已有物品，不把它当成获取来源。 */
	UPROPERTY(Config, EditAnywhere, Category = "StarterFallback")
	FName StarterScoopNetDefinitionId = NAME_None;

	/** Starter 兜底窝料定义 ID；只在兜底选择成功后授予同一角色的库存数量型物品。 */
	UPROPERTY(Config, EditAnywhere, Category = "StarterFallback")
	FName StarterChumDefinitionId = NAME_None;

	/** Starter 兜底窝料数量；0 表示即使兜底选择开启也不额外发放窝料。 */
	UPROPERTY(Config, EditAnywhere, Category = "StarterFallback", meta = (ClampMin = "0"))
	int32 StarterChumQuantity = 0;
};
