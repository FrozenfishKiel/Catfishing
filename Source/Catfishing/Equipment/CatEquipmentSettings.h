#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "CatEquipmentSettings.generated.h"

/** 装备运行策略与失败预算配置；装备定义目录由正式 InventorySettings 统一持有。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Equipment"))
class CATFISHING_API UCatEquipmentSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 是否启用服务器装配入口的可信证明校验流程；Enabled 仍必须逐项通过 PlayerState 授权，本地 Profile 选择本身永不提升权限。 */
	UPROPERTY(Config, EditAnywhere, Category = "Loadout")
	ECatDomainPolicy ProfileLoadoutTrustPolicy = ECatDomainPolicy::Unset;

	/** 一次 DamageRod 失败预算扣除的耐久；0 表示公式/数值未裁。 */
	UPROPERTY(Config, EditAnywhere, Category = "FailureBudget", meta = (ClampMin = "0.0"))
	double RodFailureDurabilityLoss = 0.0;

};
