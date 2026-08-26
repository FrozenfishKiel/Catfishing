#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "CatSurvivalAttributeSet.generated.h"

/** Character-owned ASC 的唯一局内数值属性集；只复制当前需求仍为玩法真相的 Poison、FishingStrength 和 FightStamina。 */
UCLASS()
class CATFISHING_API UCatSurvivalAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	/** 向引擎声明三项局内身体/搏斗字段；全部使用 RepNotify Always 维护 GAS 客户端基值。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** FishingStrength 代表钓鱼搏斗中的猫力量；它只服务当前局 Fishing 公式，不进入 Profile 或装备授权。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_FishingStrength, Category = "Catfishing|Fishing")
	FGameplayAttributeData FishingStrength;
	ATTRIBUTE_ACCESSORS_BASIC(UCatSurvivalAttributeSet, FishingStrength)

	/** FightStamina 代表一次搏斗内的短周期体力；它不是疲惫演出，也不进入跨局 Profile。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_FightStamina, Category = "Catfishing|Fishing")
	FGameplayAttributeData FightStamina;
	ATTRIBUTE_ACCESSORS_BASIC(UCatSurvivalAttributeSet, FightStamina)

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

	/** Poison 到达客户端时交给 GAS 标准预测收敛；倒地与表现由服务器 ConditionComponent 单独裁决。 */
	UFUNCTION()
	void OnRep_Poison(const FGameplayAttributeData& OldPoison);
};
