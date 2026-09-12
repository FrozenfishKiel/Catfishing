#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GameplayTagContainer.h"
#include "CatFightStaminaRegenEffect.generated.h"

/**
 * 搏斗外体力自然恢复的唯一写口：无限期周期 GE，每个周期按 SetByCaller 给的点数补 FightStamina。
 * 设计口径（钓鱼规则 §6.2 基础自然恢复）是搏斗外一律 5 点/秒、无场景乘数、搏斗中一律不恢复，
 * 所以速率不写在 GE 里，由提交方按「点/秒 × 周期」折算一次，避免速率在两处各留一份。
 */
UCLASS()
class CATFISHING_API UCatGE_FightStaminaRegen : public UGameplayEffect
{
	GENERATED_BODY()

public:
	/** 构造周期 GE；幅度由提交方填入，恢复条件由持有方开关本效果，不在 GE 里再写一套闸门。 */
	UCatGE_FightStaminaRegen();

	/** 周期长度（秒）。周期只决定补体的颗粒度，不是速率；速率仍是每秒点数。 */
	static constexpr float PeriodSeconds = 0.2f;

	/** 返回每周期补体点数使用的稳定 SetByCaller 标签；GE modifier 与提交方共用它，避免字符串分叉。 */
	static FGameplayTag GetRegenPerPeriodTag();
};
