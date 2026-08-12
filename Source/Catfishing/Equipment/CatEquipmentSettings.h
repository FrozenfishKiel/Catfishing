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

	/** 一次 DamageRod 失败预算扣除的耐久；0 表示公式/数值未裁。 */
	UPROPERTY(Config, EditAnywhere, Category = "FailureBudget", meta = (ClampMin = "0.0"))
	double RodFailureDurabilityLoss = 0.0;

	/** 修竿点消费的浮木定义 ID；None 表示维修链未配置。 */
	UPROPERTY(Config, EditAnywhere, Category = "Repair")
	FName DriftwoodDefinitionId = NAME_None;
};
