#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Attributes/CatAttributeSet.h"
#include "CatRunModifierAttributeSet.generated.h"

/** GameState Run ASC 的来源倍率属性集；它保存会影响每日目标与世界进度公式的局内调节量，但不保存最终结算属性。 */
UCLASS()
class CATFISHING_API UCatRunModifierAttributeSet : public UCatAttributeSet
{
	GENERATED_BODY()

public:
	/** 注册 Run 公式来源属性的复制通知；客户端只能观察当前倍率，不能用复制回调重新结算供品目标或世界进度。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 在 GE 写入基础值前规整倍率；非法或负数会变成 0，使后续 ExecCalc 按 fail-closed 口径拒绝结算。 */
	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;

	/** 在聚合后的当前值变化前规整倍率；防止来源 GE 把公共 Run 公式输入推进负数或 NaN。 */
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

	/** 每日供品目标的局内来源倍率；Run 规则、难度或事件 GE 可写入它，DayStart ExecCalc 捕获后派生当天目标。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_DailyOfferingTargetMultiplier, Category = "Catfishing|Run|Modifiers")
	FGameplayAttributeData DailyOfferingTargetMultiplier = FGameplayAttributeData(1.0f);
	ATTRIBUTE_ACCESSORS_BASIC(UCatRunModifierAttributeSet, DailyOfferingTargetMultiplier)

	/** 每日压力来源倍率；Environment 或天数规则可写入它，DayStart ExecCalc 读取它但不保存天气、事件或天数枚举。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_DailyPressure, Category = "Catfishing|Run|Modifiers")
	FGameplayAttributeData DailyPressure = FGameplayAttributeData(1.0f);
	ATTRIBUTE_ACCESSORS_BASIC(UCatRunModifierAttributeSet, DailyPressure)

	/** 世界进度成功增益倍率；祭坛、状态或全局事件 GE 可写入它，夜晚结算 ExecCalc 用它调整达标奖励。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_WorldProgressGainMultiplier, Category = "Catfishing|Run|Modifiers")
	FGameplayAttributeData WorldProgressGainMultiplier = FGameplayAttributeData(1.0f);
	ATTRIBUTE_ACCESSORS_BASIC(UCatRunModifierAttributeSet, WorldProgressGainMultiplier)

	/** 世界进度失败损失倍率；祭坛、状态或全局事件 GE 可写入它，夜晚结算 ExecCalc 用它调整未达标扣减。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_WorldProgressLossMultiplier, Category = "Catfishing|Run|Modifiers")
	FGameplayAttributeData WorldProgressLossMultiplier = FGameplayAttributeData(1.0f);
	ATTRIBUTE_ACCESSORS_BASIC(UCatRunModifierAttributeSet, WorldProgressLossMultiplier)

protected:
	/** 收到目标倍率复制时交给 GAS 通知属性观察者；本回调不重算已经发布的当天目标。 */
	UFUNCTION()
	void OnRep_DailyOfferingTargetMultiplier(const FGameplayAttributeData& OldDailyOfferingTargetMultiplier);

	/** 收到每日压力复制时交给 GAS 通知属性观察者；本回调不改变 Run Phase 或截止时间。 */
	UFUNCTION()
	void OnRep_DailyPressure(const FGameplayAttributeData& OldDailyPressure);

	/** 收到世界进度增益倍率复制时交给 GAS 通知属性观察者；客户端仍等待服务器结算结果，不自行预估世界进度。 */
	UFUNCTION()
	void OnRep_WorldProgressGainMultiplier(const FGameplayAttributeData& OldWorldProgressGainMultiplier);

	/** 收到世界进度损失倍率复制时交给 GAS 通知属性观察者；客户端只刷新读模型，不自行判定失败。 */
	UFUNCTION()
	void OnRep_WorldProgressLossMultiplier(const FGameplayAttributeData& OldWorldProgressLossMultiplier);
};
