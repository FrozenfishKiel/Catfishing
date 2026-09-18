#pragma once
#include "AbilitySystem/Attributes/CatAttributeSet.h"
#include "CatItemBonusAttributeSet.generated.h"

/** 角色当前的结算修正；GE 聚合这些属性，抽鱼、掉落及体力消费者只读最终值，不识别物品名称。 */
UCLASS()
class CATFISHING_API UCatItemBonusAttributeSet : public UCatAttributeSet
{
 GENERATED_BODY()
public:
 /** 注册加成复制，使客户端展示和服务器使用同一 GAS 属性语义。 */
 virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
 /** 稀有候选权重倍率；四叶草等 GE 修改，选鱼在归一化之前读取，中性值为一。 */
 UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_RareFishWeightMultiplier, Category="结算")
 FGameplayAttributeData RareFishWeightMultiplier = 1.f;
 ATTRIBUTE_ACCESSORS_BASIC(UCatItemBonusAttributeSet, RareFishWeightMultiplier)
 /** 额外掉落概率的加值；奖励结算与基础概率合成后夹到零至一，零表示不加成。 */
 UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_DropChanceBonus, Category="结算")
 FGameplayAttributeData DropChanceBonus = 0.f;
 ATTRIBUTE_ACCESSORS_BASIC(UCatItemBonusAttributeSet, DropChanceBonus)
 /** 猫实际体力消耗倍率；减耗 GE 修改，主控和帮手支付时读取，不改变恢复量。 */
 UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_StaminaCostMultiplier, Category="结算")
 FGameplayAttributeData StaminaCostMultiplier = 1.f;
 ATTRIBUTE_ACCESSORS_BASIC(UCatItemBonusAttributeSet, StaminaCostMultiplier)
private:
 /** 权重倍率到达客户端后交给 GAS 更新聚合观察。 */
 UFUNCTION() void OnRep_RareFishWeightMultiplier(const FGameplayAttributeData& OldValue);
 /** 掉落加值到达客户端后交给 GAS 更新聚合观察。 */
 UFUNCTION() void OnRep_DropChanceBonus(const FGameplayAttributeData& OldValue);
 /** 体力倍率到达客户端后交给 GAS 更新聚合观察。 */
 UFUNCTION() void OnRep_StaminaCostMultiplier(const FGameplayAttributeData& OldValue);
};
