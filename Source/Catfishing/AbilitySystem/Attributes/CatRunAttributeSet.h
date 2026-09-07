#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Attributes/CatAttributeSet.h"
#include "CatRunAttributeSet.generated.h"

/** GameState Run ASC 的最终公开属性集；它只保存当天额度目标和当前进度，不混入来源倍率或环境语义。 */
UCLASS()
class CATFISHING_API UCatRunAttributeSet : public UCatAttributeSet
{
	GENERATED_BODY()

public:
	/** 注册最终 Run 数值的复制通知；客户端只用 GAS 标准通知收敛读模型，不能据此推进阶段或提交献祭。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 在 GE 写入基础值前规整最终额度；目标和进度必须保持有限且非负，非法值归零后由上层 fail-closed。 */
	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;

	/** 在聚合后的当前值变化前规整最终额度；客户端和服务器观察到的目标/进度都不能出现负数或 NaN。 */
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

	/** 本日需要达到的公共献祭额度，DayStart GE 写入、GameMode 投影到 DTO，StateTree 达标判断和 UI 读取它的投影。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_QuotaTarget, Category = "Catfishing|Run")
	FGameplayAttributeData QuotaTarget;
	ATTRIBUTE_ACCESSORS_BASIC(UCatRunAttributeSet, QuotaTarget)

	/** 本日已经由权威献祭 GE 实际累计的公共进度，献祭 GE 写入且允许超过目标，GameMode 读取它决定是否发送 QuotaReached。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_QuotaProgress, Category = "Catfishing|Run")
	FGameplayAttributeData QuotaProgress;
	ATTRIBUTE_ACCESSORS_BASIC(UCatRunAttributeSet, QuotaProgress)

protected:
	/** 收到目标额度复制时交给 GAS 通知属性委托；客户端不会在这里重算目标或改变公开阶段。 */
	UFUNCTION()
	void OnRep_QuotaTarget(const FGameplayAttributeData& OldQuotaTarget);
	/** 收到额度进度复制时交给 GAS 通知属性委托；超额值原样保留给 UI 与结算观察。 */
	UFUNCTION()
	void OnRep_QuotaProgress(const FGameplayAttributeData& OldQuotaProgress);
};
