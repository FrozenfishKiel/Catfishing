#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CatCharacterDefinition.generated.h"

/**
 * 猫种类运行定义：按种类差异化初始中毒值与搏斗数值（规格 4.2 三方力量中的"猫"侧）。
 * 数值只在 Character 属性播种与搏斗体力基线两处被读取并冻结进 ASC/会话，运行中改资产不影响已开始的搏斗。
 * Character 的 CatDefinitionId 为 None 时回退全局 CatAbilitySettings 初值，指定但缺失时保持 fail-closed。
 */
UCLASS(BlueprintType)
class CATFISHING_API UCatCharacterDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 检查该资产是否足以参与正式属性播种；ID/启用位缺失或搏斗两项非正都返回 false。 */
	bool IsRuntimeDefinitionReady() const;

	/** 猫种类稳定 ID；Character 与日志只引用该值，不把资产对象当身份。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FName CatDefinitionId = NAME_None;

	/** 表现用显示名；不参与任何数值裁决。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	/** 初始 Poison；负值表示 Unset。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attributes", meta = (ClampMin = "-1.0"))
	float InitialPoison = -1.0f;

	/** 猫力量（规格 4.2：与鱼力量/竿强度比较的钓力）；必须为正。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fight", meta = (ClampMin = "-1.0"))
	float FishingStrength = -1.0f;

	/** 猫搏斗体力上限（规格 4.3 消耗/放线回复的基线）；必须为正。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fight", meta = (ClampMin = "-1.0"))
	float FightStaminaMaximum = -1.0f;

	/** 数据人员显式启用 gate；默认关闭，避免占位资产被正式播种采用。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Prototype")
	bool bEnableRuntimeDefinition = false;
};
