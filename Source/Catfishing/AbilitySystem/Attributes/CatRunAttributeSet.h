#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Attributes/CatAttributeSet.h"
#include "CatRunAttributeSet.generated.h"

/** GameState Run ASC 的最终公开属性集；它保存每日供品目标、最近夜晚供品结果和世界进度，不混入来源倍率或容器事务。 */
UCLASS()
class CATFISHING_API UCatRunAttributeSet : public UCatAttributeSet
{
	GENERATED_BODY()

public:
	/** 注册最终 Run 数值的复制通知；客户端只用 GAS 标准通知收敛读模型，不能据此推进阶段或提交供品结算。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 在 GE 写入基础值前规整最终 Run 数值；目标、供品点和世界进度必须保持有限，非法值由上层 fail-closed。 */
	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;

	/** 在聚合后的当前值变化前规整最终 Run 数值；客户端和服务器观察到的属性都不能出现 NaN 或越界世界进度。 */
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

	/** 本日需要达到的供品点数；DayStart GE 写入，GameMode 投影到公开 DTO，夜晚结算 ExecCalc 读取它裁决是否达标。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_DailyOfferingTarget, Category = "Catfishing|Run")
	FGameplayAttributeData DailyOfferingTarget;
	ATTRIBUTE_ACCESSORS_BASIC(UCatRunAttributeSet, DailyOfferingTarget)

	/** 最近一次夜晚实际提交的供品点数；夜晚结算 GE 覆盖，UI 和存档摘要读取它解释上一晚结果。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_LastOfferingPoints, Category = "Catfishing|Run")
	FGameplayAttributeData LastOfferingPoints;
	ATTRIBUTE_ACCESSORS_BASIC(UCatRunAttributeSet, LastOfferingPoints)

	/** 当前世界进度，范围 0 到 100；开局由 GameMode 初始化，夜晚结算 GE 是日常玩法中唯一写者。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_WorldProgress, Category = "Catfishing|Run")
	FGameplayAttributeData WorldProgress = FGameplayAttributeData(10.0f);
	ATTRIBUTE_ACCESSORS_BASIC(UCatRunAttributeSet, WorldProgress)

	/** 最近一次夜晚结算对世界进度产生的变化量；结算 GE 覆盖，负数表示未达标扣减，正数表示达标增长。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_LastWorldProgressDelta, Category = "Catfishing|Run")
	FGameplayAttributeData LastWorldProgressDelta;
	ATTRIBUTE_ACCESSORS_BASIC(UCatRunAttributeSet, LastWorldProgressDelta)

protected:
	/** 收到每日目标复制时交给 GAS 通知属性委托；客户端不会在这里重算目标或改变公开阶段。 */
	UFUNCTION()
	void OnRep_DailyOfferingTarget(const FGameplayAttributeData& OldDailyOfferingTarget);

	/** 收到最近供品点复制时交给 GAS 通知属性委托；客户端只刷新读模型，不重放供品结算。 */
	UFUNCTION()
	void OnRep_LastOfferingPoints(const FGameplayAttributeData& OldLastOfferingPoints);

	/** 收到世界进度复制时交给 GAS 通知属性委托；终局仍由服务器 StateTree 事件推进。 */
	UFUNCTION()
	void OnRep_WorldProgress(const FGameplayAttributeData& OldWorldProgress);

	/** 收到最近世界进度变化复制时交给 GAS 通知属性委托；客户端只用于展示上一晚结果。 */
	UFUNCTION()
	void OnRep_LastWorldProgressDelta(const FGameplayAttributeData& OldLastWorldProgressDelta);
};
