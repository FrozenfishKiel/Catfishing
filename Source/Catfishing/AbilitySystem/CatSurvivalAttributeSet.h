#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "CatSurvivalAttributeSet.generated.h"

/** Character-owned ASC 的唯一局内属性集；复制 Hunger/Fatigue/Poison 与独立的 FishingStrength/FightStamina，不把任一项上移到 PlayerState/Profile。 */
UCLASS()
class CATFISHING_API UCatSurvivalAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	/** 向引擎声明五个局内身体/搏斗字段；全部使用 RepNotify Always 维护 GAS 客户端基值。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Hunger 代表当前猫身体的局内饥饿状态；ASC、Ability 与只读 UI 通过生成的访问器读写或订阅它。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Hunger, Category = "Catfishing|Survival")
	FGameplayAttributeData Hunger;
	ATTRIBUTE_ACCESSORS_BASIC(UCatSurvivalAttributeSet, Hunger)

	/** Fatigue 代表当前猫身体的局内疲惫状态；它随 Character 复制，不进入 PlayerState 或跨局 Profile。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Fatigue, Category = "Catfishing|Survival")
	FGameplayAttributeData Fatigue;
	ATTRIBUTE_ACCESSORS_BASIC(UCatSurvivalAttributeSet, Fatigue)

	/** FishingStrength 代表钓鱼搏斗中的猫力量；它与日常 Fatigue 独立，公式未裁时默认 0 并阻止巨鱼成功。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_FishingStrength, Category = "Catfishing|Fishing")
	FGameplayAttributeData FishingStrength;
	ATTRIBUTE_ACCESSORS_BASIC(UCatSurvivalAttributeSet, FishingStrength)

	/** FightStamina 代表一次搏斗内的短周期体力；它不是日常疲惫值，也不进入跨局 Profile。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_FightStamina, Category = "Catfishing|Fishing")
	FGameplayAttributeData FightStamina;
	ATTRIBUTE_ACCESSORS_BASIC(UCatSurvivalAttributeSet, FightStamina)

	/** Poison 代表当前猫身体的局内中毒累积；来源只读 FishDefinition，局末随 Character 销毁且不会造成死亡。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Poison, Category = "Catfishing|Survival")
	FGameplayAttributeData Poison;
	ATTRIBUTE_ACCESSORS_BASIC(UCatSurvivalAttributeSet, Poison)

protected:
	/** Hunger 到达客户端时把旧基值交回 ASC 预测系统；Always 保证相同数值通知也能收敛预测状态。 */
	UFUNCTION()
	void OnRep_Hunger(const FGameplayAttributeData& OldHunger);

	/** Fatigue 到达客户端时把旧基值交回 ASC 预测系统；本阶段不在 RepNotify 中附加产品表现或阈值。 */
	UFUNCTION()
	void OnRep_Fatigue(const FGameplayAttributeData& OldFatigue);

	/** FishingStrength 到达客户端时交给 GAS 标准预测收敛；不在此计算协作加成。 */
	UFUNCTION()
	void OnRep_FishingStrength(const FGameplayAttributeData& OldFishingStrength);

	/** FightStamina 到达客户端时交给 GAS 标准预测收敛；不与 Fatigue 合并或互相覆盖。 */
	UFUNCTION()
	void OnRep_FightStamina(const FGameplayAttributeData& OldFightStamina);

	/** Poison 到达客户端时交给 GAS 标准预测收敛；倒地与表现由服务器 ConditionComponent 单独裁决。 */
	UFUNCTION()
	void OnRep_Poison(const FGameplayAttributeData& OldPoison);
};
