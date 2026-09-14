#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Attributes/CatAttributeSet.h"
#include "CatSurvivalAttributeSet.generated.h"

/** Character-owned ASC 的局内身体属性：Poison、力量、跨竿绿色体力及其上限、当天黄色储备。 */
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

	/** FightStamina 代表当前身体跨竿保留的绿色体力；它不是疲惫演出，也不进入跨局 Profile。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_FightStamina, Category = "Catfishing|Fishing")
	FGameplayAttributeData FightStamina;
	ATTRIBUTE_ACCESSORS_BASIC(UCatSurvivalAttributeSet, FightStamina)

	/** MaxFightStamina 是绿色段的恢复上限，不限制黄色储备；由角色播种，ASC、会话和 HUD 读取。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxFightStamina, Category = "Catfishing|Fishing")
	FGameplayAttributeData MaxFightStamina;
	ATTRIBUTE_ACCESSORS_BASIC(UCatSurvivalAttributeSet, MaxFightStamina)

	/** 黄色储备：无上限、绿先扣、不接受恢复；新身体为零，翻天清空。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_YellowFightStamina, Category="Catfishing|Fishing")
	FGameplayAttributeData YellowFightStamina;
	ATTRIBUTE_ACCESSORS_BASIC(UCatSurvivalAttributeSet, YellowFightStamina)

	/** Poison 代表当前猫身体的局内中毒累积；来源只读 FishDefinition，局末随 Character 销毁且不会造成死亡。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Poison, Category = "Catfishing|Survival")
	FGameplayAttributeData Poison;
	ATTRIBUTE_ACCESSORS_BASIC(UCatSurvivalAttributeSet, Poison)

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

	/** Poison 到达客户端时交给 GAS 标准预测收敛；倒地与表现由服务器 ConditionComponent 单独裁决。 */
	UFUNCTION()
	void OnRep_Poison(const FGameplayAttributeData& OldPoison);

	UFUNCTION()
	void OnRep_YellowFightStamina(const FGameplayAttributeData& OldYellowFightStamina);

};
