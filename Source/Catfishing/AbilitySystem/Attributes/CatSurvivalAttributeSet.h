#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Attributes/CatAttributeSet.h"
#include "CatSurvivalAttributeSet.generated.h"

/**
 * Character-owned ASC 的唯一局内数值属性集；只复制当前仍是玩法真相的 FishingStrength、FightStamina、
 * 它的上限，以及黄色体力护盾段。
 *
 * 墓碑（2026-09-12，09-12 裁决「中毒按鱼各配、无渐进升级」）：这里原本还有一条 Poison 属性，
 * 用来做「跨鱼累加中毒值、到阈值 100 倒地」的渐进加重模型。该模型在 2026-08-21 已被设计砍掉
 * （猫册 v1.13），倒地改由单条鱼的食用结论直接裁决，Poison 因此失去全部消费者，连同
 * UCatGE_PoisonDelta、ApplyPoisonDelta/IsPoisonAtLeast、CatConditionSettings::PoisonDownedThreshold
 * 与三处 Recovery 清毒值一并删除。想找「中毒」的现行口径：轻档中毒是按鱼种配置的限时 buff
 * （吃鱼效果页 §4），重档＝吃下即倒地，由 CatConditionComponent 直接写 bDowned。
 */
UCLASS()
class CATFISHING_API UCatSurvivalAttributeSet : public UCatAttributeSet
{
	GENERATED_BODY()

public:
	/** 向引擎声明局内身体/搏斗字段；全部使用 RepNotify Always 维护 GAS 客户端基值。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 在 GE 或初始化写入基础值前规整身体数值；体力必须先落在当前上限以内。 */
	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;

	/** 在聚合后的当前值变化前规整身体数值；客户端和服务器观察到的体力都不能越过上限或变成非法值。 */
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

	/** 在体力上限变化后同步收紧当前体力；避免 Max 降低后变更前 Current 继续影响搏斗模拟。 */
	virtual void PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue) override;

	/** FishingStrength 代表钓鱼搏斗中的猫力量；它只服务当前局 Fishing 公式，不进入 Profile 或装备授权。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_FishingStrength, Category = "Catfishing|Fishing")
	FGameplayAttributeData FishingStrength;
	ATTRIBUTE_ACCESSORS_BASIC(UCatSurvivalAttributeSet, FishingStrength)

	/** FightStamina 仅代表绿色体力（点）；总可用体力另加 YellowFightStamina，不进入跨局 Profile。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_FightStamina, Category = "Catfishing|Fishing")
	FGameplayAttributeData FightStamina;
	ATTRIBUTE_ACCESSORS_BASIC(UCatSurvivalAttributeSet, FightStamina)

	/** MaxFightStamina 代表绿色体力的恢复上限（点）；总容量另加当前黄段，不把黄色纳入可恢复上限。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxFightStamina, Category = "Catfishing|Fishing")
	FGameplayAttributeData MaxFightStamina;
	ATTRIBUTE_ACCESSORS_BASIC(UCatSurvivalAttributeSet, MaxFightStamina)

	/**
	 * YellowFightStamina 代表加在体力条末端的黄色护盾段（数值成长页 §4）：
	 * 搏斗消耗先扣绿色段、扣完才动它；不自然回复、不吃任何回复效果，用掉即无；可叠加且无上限；过夜清空。
	 * 它跟局也跟天走，不进 Profile。
	 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_YellowFightStamina, Category = "Catfishing|Survival")
	FGameplayAttributeData YellowFightStamina;
	ATTRIBUTE_ACCESSORS_BASIC(UCatSurvivalAttributeSet, YellowFightStamina)

protected:
	/** FishingStrength 到达客户端时交给 GAS 标准预测收敛；不在此计算协作加成。 */
	UFUNCTION()
	void OnRep_FishingStrength(const FGameplayAttributeData& OldFishingStrength);

	/** FightStamina 到达客户端时交给 GAS 标准预测收敛；不与疲惫演出合并或互相覆盖。 */
	UFUNCTION()
	void OnRep_FightStamina(const FGameplayAttributeData& OldFightStamina);

	/** MaxFightStamina 到达客户端时交给 GAS 标准预测收敛；HUD 与会话投影读取复制后的属性值。 */
	UFUNCTION()
	void OnRep_MaxFightStamina(const FGameplayAttributeData& OldMaxFightStamina);

	/** 黄色体力到达客户端时交给 GAS 标准预测收敛；体力条的黄段渲染归钓鱼册，客户端不自行增减它。 */
	UFUNCTION()
	void OnRep_YellowFightStamina(const FGameplayAttributeData& OldYellowFightStamina);

};
