#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Data/CatFishSelectionTypes.h"
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

	FCatFishSelectionResult SelectRuntimeDefinition(const FCatFishSelectionContext& Context) const;

	/** 正式 FishDefinition 软引用清单；默认空使 Fishing fail-closed，不扫描旧鱼种文档或测试资产。 */
	UPROPERTY(Config, EditAnywhere, Category = "Catalog")
	TArray<TSoftObjectPtr<UCatFishDefinition>> Definitions;

	UPROPERTY(Config, EditAnywhere, Category = "Selection")
	TSoftObjectPtr<class UCurveFloat> ChumSaturationCurve;

	UPROPERTY(Config, EditAnywhere, Category = "Selection")
	double ChumAffinityHalfSaturation = 0.0;

	UPROPERTY(Config, EditAnywhere, Category = "Selection")
	double MaximumChumModifier = 0.0;

	/** 挑战度不高于该值的鱼归入轻松带；挑战度以力量比为下限，并由力量/体力调和均值连续抬升。 */
	UPROPERTY(Config, EditAnywhere, Category = "Selection|Challenge", meta = (ClampMin = "0.0"))
	double ComfortChallengeMaximumRatio = 0.0;

	/** 挑战度不高于该值且高于轻松带上限的鱼归入势均力敌带。 */
	UPROPERTY(Config, EditAnywhere, Category = "Selection|Challenge", meta = (ClampMin = "0.0"))
	double MatchedChallengeMaximumRatio = 0.0;

	/** 可进入抽取池的挑战度安全上限；允许略强于当前玩家的鱼出现，超过此值仍 fail-closed。 */
	UPROPERTY(Config, EditAnywhere, Category = "Selection|Challenge", meta = (ClampMin = "0.0"))
	double MaximumChallengeRatio = 0.0;

	/** 连续挑战权重的峰值位置；越接近该比例，鱼在所属难度带内的相对权重越高。 */
	UPROPERTY(Config, EditAnywhere, Category = "Selection|Challenge", meta = (ClampMin = "0.0"))
	double TargetChallengeRatio = 0.0;

	/** 轻松带的抽取权重；只在该带存在候选时参与归一化。 */
	UPROPERTY(Config, EditAnywhere, Category = "Selection|Challenge", meta = (ClampMin = "0.0"))
	double ComfortChallengeBandWeight = 0.0;

	/** 势均力敌带的抽取权重；只在该带存在候选时参与归一化。 */
	UPROPERTY(Config, EditAnywhere, Category = "Selection|Challenge", meta = (ClampMin = "0.0"))
	double MatchedChallengeBandWeight = 0.0;

	/** 高风险带的抽取权重；只在该带存在候选时参与归一化。 */
	UPROPERTY(Config, EditAnywhere, Category = "Selection|Challenge", meta = (ClampMin = "0.0"))
	double RiskyChallengeBandWeight = 0.0;

	/** 难度离目标最远时仍保留的正倍率，避免挑战匹配抹掉鱼饵、窝料和稀有度的生态作用。 */
	UPROPERTY(Config, EditAnywhere, Category = "Selection|Challenge", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double MinimumChallengeWeightMultiplier = 0.0;
};
