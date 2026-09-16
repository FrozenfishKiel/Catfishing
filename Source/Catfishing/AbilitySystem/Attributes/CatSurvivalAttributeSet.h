#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Attributes/CatAttributeSet.h"
#include "CatSurvivalAttributeSet.generated.h"

/** Character-owned ASC 的唯一搏斗属性集；复制力量、绿段体力及上限、黄色储备，消费和恢复共用同一事实源。 */
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

 /** GE 执行后消费一次经验元属性；先清零再交给 Growth，防止回调重入累计本次数值。 */
 virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;
 /** 本次 GE 提供的经验点数；仅作为服务器执行中间值，消费即清零，不复制、不持久化。 */
 UPROPERTY() FGameplayAttributeData IncomingFishExperience;
 ATTRIBUTE_ACCESSORS_BASIC(UCatSurvivalAttributeSet, IncomingFishExperience)

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

private:
	/** 仅用于客户端复制诊断限频；不参与体力、恢复或复制裁决。 */
	double NextFightStaminaDiagnosticWorldSeconds = 0.0;
	bool bHasFightStaminaDiagnostic = false;
};
