#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Framework/Core/CatRunContracts.h"
#include "CatFishCatalogSettings.generated.h"

class UCatFishDefinition;

/** 正式鱼表资产目录；它是运行时鱼定义的唯一枚举入口，不从文件名或 Content 扫描猜内容。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Fish Catalog"))
class CATFISHING_API UCatFishCatalogSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 按稳定 ID 查找完整且启用的鱼定义；重复 ID 或加载失败返回空。 */
	UCatFishDefinition* FindRuntimeDefinition(FName FishDefinitionId) const;

	/** 按环境与权威协作人数/总力量/总体力筛选可达鱼种，再用数据权重确定性抽取并返回服务器重量。 */
	UCatFishDefinition* SelectRuntimeDefinition(FName RegionId, ECatEnvironmentTimeOfDay TimeOfDay,
		ECatEnvironmentWeather Weather, int32 ActivePlayerCount, double CombinedFishingStrength,
		double CombinedFightStamina, int32 RandomSeed, double& OutWeightKilograms) const;

	/** 正式 FishDefinition 软引用清单；默认空使 Fishing fail-closed，不扫描旧鱼种文档或测试资产。 */
	UPROPERTY(Config, EditAnywhere, Category = "Catalog")
	TArray<TSoftObjectPtr<UCatFishDefinition>> Definitions;
};
