#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GameplayTagContainer.h"
#include "CatGrowthAttributeEffect.generated.h"

/**
 * 身体属性的正式即时增减效果族。
 *
 * 墓碑（2026-09-12）：本文件取代原 CatPoisonEffect.h/.cpp 的 UCatGE_PoisonDelta。
 * 那条 GE 服务的是「跨鱼累加 Poison ＋ 阈值 100 倒地」的渐进中毒模型，
 * 09-12 裁决「中毒按鱼各配、无渐进升级，最重一档＝吃下即倒地」之后它没有调用方了。
 * 现在这里放的是三选一成长真正需要写进 ASC 的两项，以及黄色体力护盾段。
 */

/** 力量的即时加法 GE；三选一「力量 +10」用它写进 FishingStrength，不直接写属性基值。 */
UCLASS()
class CATFISHING_API UCatGE_FishingStrengthDelta : public UGameplayEffect
{
	GENERATED_BODY()

public:
	/** 构造力量加法 GE；实际增量由 ASC 提交时填入 SetByCaller。 */
	UCatGE_FishingStrengthDelta();

	/** 返回力量增减使用的稳定 SetByCaller 标签；GE modifier 与提交方共用它，避免字符串分叉。 */
	static FGameplayTag GetFishingStrengthDeltaTag();
};

/** 搏斗体力上限的即时加法 GE；三选一「搏斗体力上限 +20」用它写进 MaxFightStamina。 */
UCLASS()
class CATFISHING_API UCatGE_MaxFightStaminaDelta : public UGameplayEffect
{
	GENERATED_BODY()

public:
	/** 构造体力上限加法 GE；实际增量由 ASC 提交时填入 SetByCaller。 */
	UCatGE_MaxFightStaminaDelta();

	/** 返回体力上限增减使用的稳定 SetByCaller 标签。 */
	static FGameplayTag GetMaxFightStaminaDeltaTag();
};

/** 黄色体力护盾段的即时加法 GE；吃鱼与猫神的祝福授予的护盾、搏斗扣盾与过夜清空都走它。 */
UCLASS()
class CATFISHING_API UCatGE_YellowFightStaminaDelta : public UGameplayEffect
{
	GENERATED_BODY()

public:
	/** 构造黄色体力加法 GE；实际增减量由 ASC 提交时填入 SetByCaller。 */
	UCatGE_YellowFightStaminaDelta();

	/** 返回黄色体力增减使用的稳定 SetByCaller 标签。 */
	static FGameplayTag GetYellowFightStaminaDeltaTag();
};
