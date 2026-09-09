#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Attributes/CatAttributeSet.h"
#include "CatSurvivalAttributeSet.generated.h"

/** Character-owned ASC 的唯一局内数值属性集；只复制当前需求仍为玩法真相的 Poison、FishingStrength、FightStamina 和它的上限。 */
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

	/** 在体力上限变化后同步收紧当前体力；避免 Max 降低后旧 Current 继续影响搏斗模拟。 */
	virtual void PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue) override;

	/** FishingStrength 代表钓鱼搏斗中的猫力量；它只服务当前局 Fishing 公式，不进入 Profile 或装备授权。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_FishingStrength, Category = "Catfishing|Fishing")
	FGameplayAttributeData FishingStrength;
	ATTRIBUTE_ACCESSORS_BASIC(UCatSurvivalAttributeSet, FishingStrength)

	/** FightStamina 代表一次搏斗内的短周期体力；它不是疲惫演出，也不进入跨局 Profile。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_FightStamina, Category = "Catfishing|Fishing")
	FGameplayAttributeData FightStamina;
	ATTRIBUTE_ACCESSORS_BASIC(UCatSurvivalAttributeSet, FightStamina)

	/** MaxFightStamina 代表当前猫本次搏斗允许恢复到的体力上限；角色播种写入，ASC、会话模拟和 HUD 只读取这份真相。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxFightStamina, Category = "Catfishing|Fishing")
	FGameplayAttributeData MaxFightStamina;
	ATTRIBUTE_ACCESSORS_BASIC(UCatSurvivalAttributeSet, MaxFightStamina)

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

	/** MaxFightStamina 到达客户端时交给 GAS 标准预测收敛；HUD 与会话投影不再从配置反查体力上限。 */
	UFUNCTION()
	void OnRep_MaxFightStamina(const FGameplayAttributeData& OldMaxFightStamina);

	/** Poison 到达客户端时交给 GAS 标准预测收敛；倒地与表现由服务器 ConditionComponent 单独裁决。 */
	UFUNCTION()
	void OnRep_Poison(const FGameplayAttributeData& OldPoison);

private:
	/** 仅用于客户端复制诊断限频；不参与体力、恢复或复制裁决。 */
	double NextFightStaminaDiagnosticWorldSeconds = 0.0;
	bool bHasFightStaminaDiagnostic = false;
};
