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

	/** 每种局内耗材的随身上限；0 表示暂不限制。商店在扣款前也会使用同一条规则预检。 */
	UPROPERTY(Config, EditAnywhere, Category = "Consumables", meta = (ClampMin = "0"))
	int32 RunConsumableStackCapacity = 0;

	/** 一次 DamageRod 失败预算扣除的耐久；0 表示公式/数值未裁。 */
	UPROPERTY(Config, EditAnywhere, Category = "FailureBudget", meta = (ClampMin = "0.0"))
	double RodFailureDurabilityLoss = 0.0;

	/** 修竿点消费的浮木定义 ID；None 表示维修链未配置。 */
	UPROPERTY(Config, EditAnywhere, Category = "Repair")
	FName DriftwoodDefinitionId = NAME_None;

	/**
	 * 受控 Starter 兜底开关；打开时服务器会在角色首次占有且 Loadout 为空时尝试写入配置的基础装备和窝料。
	 * 正式默认配置保持关闭，避免绕过商店或 Profile Grant 授权链；测试或诊断显式打开时仍经过目录、解锁和 Revision 校验。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "StarterFallback")
	bool bAutoConfigureStarterLoadout = false;

	/** Starter 兜底鱼竿定义 ID；只在兜底开关打开时读取，正式商店和解锁路径不会自动消费它。 */
	UPROPERTY(Config, EditAnywhere, Category = "StarterFallback")
	FName StarterRodDefinitionId = NAME_None;

	/** Starter 兜底鱼饵定义 ID；只为显式兜底装配提供完整 Loadout，不代表普通饵扣减策略。 */
	UPROPERTY(Config, EditAnywhere, Category = "StarterFallback")
	FName StarterBaitDefinitionId = NAME_None;

	/** Starter 兜底鱼漂定义 ID；只在显式兜底流程中写入个人 Equipment，正式获取仍走商店或解锁授权。 */
	UPROPERTY(Config, EditAnywhere, Category = "StarterFallback")
	FName StarterFloatDefinitionId = NAME_None;

	/** Starter 兜底抄网定义 ID；None 表示兜底装配不发抄网，正式抄网来源由后续内容或商店配置决定。 */
	UPROPERTY(Config, EditAnywhere, Category = "StarterFallback")
	FName StarterScoopNetDefinitionId = NAME_None;

	/** Starter 兜底窝料定义 ID；只在兜底装配成功后授予同一角色的局内消耗品栈。 */
	UPROPERTY(Config, EditAnywhere, Category = "StarterFallback")
	FName StarterChumDefinitionId = NAME_None;

	/** Starter 兜底窝料数量；0 表示即使兜底装配开启也不额外发放窝料。 */
	UPROPERTY(Config, EditAnywhere, Category = "StarterFallback", meta = (ClampMin = "0"))
	int32 StarterChumQuantity = 0;
};
