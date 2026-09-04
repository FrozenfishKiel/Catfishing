#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "CatRunAttributeSet.generated.h"

/** GameState-owned Run ASC 的共享数值属性集；只保存能被 GE 组合、需要复制并投影给 RunPublicState 的额度事实。 */
UCLASS()
class CATFISHING_API UCatRunAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	/** 注册五项 Run 数值的无条件 RepNotify；客户端只用 GAS 标准通知收敛读模型，不能据此推进阶段或提交献祭。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 本日需要达到的公共献祭额度，DayStart GE 写入、GameMode 投影到 DTO，StateTree 达标判断和 UI 读取它的投影。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_QuotaTarget, Category = "Catfishing|Run")
	FGameplayAttributeData QuotaTarget;
	ATTRIBUTE_ACCESSORS_BASIC(UCatRunAttributeSet, QuotaTarget)

	/** 本日已经由权威献祭 GE 实际累计的公共进度，献祭 GE 写入且允许超过目标，GameMode 读取它决定是否发送 QuotaReached。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_QuotaProgress, Category = "Catfishing|Run")
	FGameplayAttributeData QuotaProgress;
	ATTRIBUTE_ACCESSORS_BASIC(UCatRunAttributeSet, QuotaProgress)

	/** 目标额度的局内倍率，服务器未来可用 GE 写入；DayStart ExecCalc 捕获它，默认 1 保持既有目标值。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_QuotaTargetMultiplier, Category = "Catfishing|Run")
	FGameplayAttributeData QuotaTargetMultiplier = FGameplayAttributeData(1.0f);
	ATTRIBUTE_ACCESSORS_BASIC(UCatRunAttributeSet, QuotaTargetMultiplier)

	/** 每日压力倍率，服务器未来可用 GE 写入；DayStart ExecCalc 捕获它，默认 1 且不会在本轮影响献祭效率。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_DailyPressure, Category = "Catfishing|Run")
	FGameplayAttributeData DailyPressure = FGameplayAttributeData(1.0f);
	ATTRIBUTE_ACCESSORS_BASIC(UCatRunAttributeSet, DailyPressure)

	/** 献祭原始贡献的局内效率倍率，服务器未来可用 GE 写入；献祭 ExecCalc 捕获它并将实际结果回给协调器，默认 1 保持原行为。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_SacrificeEfficiency, Category = "Catfishing|Run")
	FGameplayAttributeData SacrificeEfficiency = FGameplayAttributeData(1.0f);
	ATTRIBUTE_ACCESSORS_BASIC(UCatRunAttributeSet, SacrificeEfficiency)

protected:
	/** 收到目标额度复制时交给 GAS 通知属性委托；客户端不会在这里重算目标或改变公开阶段。 */
	UFUNCTION()
	void OnRep_QuotaTarget(const FGameplayAttributeData& OldQuotaTarget);
	/** 收到额度进度复制时交给 GAS 通知属性委托；超额值原样保留给 UI 与结算观察。 */
	UFUNCTION()
	void OnRep_QuotaProgress(const FGameplayAttributeData& OldQuotaProgress);
	/** 收到目标倍率复制时交给 GAS 通知属性委托；下一次 DayStart GE 才会消费新的倍率。 */
	UFUNCTION()
	void OnRep_QuotaTargetMultiplier(const FGameplayAttributeData& OldQuotaTargetMultiplier);
	/** 收到日压力复制时交给 GAS 通知属性委托；它只影响后续 DayStart 计算。 */
	UFUNCTION()
	void OnRep_DailyPressure(const FGameplayAttributeData& OldDailyPressure);
	/** 收到献祭效率复制时交给 GAS 通知属性委托；客户端不自行预估或修正实际贡献。 */
	UFUNCTION()
	void OnRep_SacrificeEfficiency(const FGameplayAttributeData& OldSacrificeEfficiency);
};
