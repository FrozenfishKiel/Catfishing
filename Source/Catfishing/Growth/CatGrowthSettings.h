#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatGrowthSettings.generated.h"

/** 吃鱼成长的当前已裁规则；只配置槽长度，鱼种经验和 Buff 内容仍来自鱼表/后续内容。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Character Growth"))
class CATFISHING_API UCatGrowthSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 判断本局成长结算是否可运行；关闭或槽长度非法时吃鱼成长保持 fail-closed。 */
	bool IsRuntimeReady() const;

	/** 吃鱼成长运行总 gate；默认关闭，防止未接线项目悄悄制造经验。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnableGrowthRuntime = false;

	/** 触发一次三选一需要的经验槽长度；当前需求锁定为 10。 */
	UPROPERTY(Config, EditAnywhere, Category = "Experience", meta = (ClampMin = "1"))
	int32 ExperiencePerChoiceSlot = 0;
};
