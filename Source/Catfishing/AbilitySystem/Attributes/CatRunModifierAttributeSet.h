#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Attributes/CatAttributeSet.h"
#include "CatRunModifierAttributeSet.generated.h"

/** GameState Run ASC 的来源倍率属性集；它保存会影响额度公式的局内调节量，但不保存最终目标或进度。 */
UCLASS()
class CATFISHING_API UCatRunModifierAttributeSet : public UCatAttributeSet
{
	GENERATED_BODY()

public:
	/** 注册 Run 公式来源属性的复制通知；客户端只能观察当前倍率，不能用复制回调重新结算额度。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 在 GE 写入基础值前规整倍率；非法或负数会变成 0，使后续 ExecCalc 按 fail-closed 口径拒绝结算。 */
	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;

	/** 在聚合后的当前值变化前规整倍率；防止来源 GE 把公共 Run 公式输入推进负数或 NaN。 */
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

	/** 目标额度的局内来源倍率；Run 规则、难度或事件 GE 可写入它，DayStart ExecCalc 捕获后派生当天 QuotaTarget。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_QuotaTargetMultiplier, Category = "Catfishing|Run|Modifiers")
	FGameplayAttributeData QuotaTargetMultiplier = FGameplayAttributeData(1.0f);
	ATTRIBUTE_ACCESSORS_BASIC(UCatRunModifierAttributeSet, QuotaTargetMultiplier)

	/** 每日压力来源倍率；Environment 或天数规则可写入它，DayStart ExecCalc 读取它但不保存天气、事件或天数枚举。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_DailyPressure, Category = "Catfishing|Run|Modifiers")
	FGameplayAttributeData DailyPressure = FGameplayAttributeData(1.0f);
	ATTRIBUTE_ACCESSORS_BASIC(UCatRunModifierAttributeSet, DailyPressure)

	/** 献祭效率来源倍率；祭坛、状态或全局事件 GE 可写入它，献祭 ExecCalc 用它把冻结鱼贡献换成实际进度。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_SacrificeEfficiency, Category = "Catfishing|Run|Modifiers")
	FGameplayAttributeData SacrificeEfficiency = FGameplayAttributeData(1.0f);
	ATTRIBUTE_ACCESSORS_BASIC(UCatRunModifierAttributeSet, SacrificeEfficiency)

protected:
	/** 收到目标倍率复制时交给 GAS 通知属性观察者；本回调不重算已经发布的当天目标。 */
	UFUNCTION()
	void OnRep_QuotaTargetMultiplier(const FGameplayAttributeData& OldQuotaTargetMultiplier);

	/** 收到每日压力复制时交给 GAS 通知属性观察者；本回调不改变 Run Phase 或截止时间。 */
	UFUNCTION()
	void OnRep_DailyPressure(const FGameplayAttributeData& OldDailyPressure);

	/** 收到献祭效率复制时交给 GAS 通知属性观察者；客户端仍等待服务器献祭结果，不自行预估实际贡献。 */
	UFUNCTION()
	void OnRep_SacrificeEfficiency(const FGameplayAttributeData& OldSacrificeEfficiency);
};
