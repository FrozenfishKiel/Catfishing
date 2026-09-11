#include "AbilitySystem/Attributes/CatRunAttributeSet.h"

#include "Net/UnrealNetwork.h"

namespace
{
	// 非负 Run 数值规整流程：每日目标和供品点只要求有限且非负；是否达标交给夜晚结算公式判断。
	float ClampRunNonNegativeValue(const float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : 0.0f;
	}

	// 世界进度规整流程：世界进度是 0 到 100 的终局刻度；越界值会在属性层夹回可公开范围。
	float ClampRunWorldProgressValue(const float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.0f, 100.0f) : 0.0f;
	}

	// 进度变化规整流程：最近变化量只解释上一晚结算，范围限制在一次结算最多清零或补满世界进度。
	float ClampRunWorldProgressDeltaValue(const float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, -100.0f, 100.0f) : 0.0f;
	}

	// 属性规整分派流程：最终 Run 属性集拥有四个公开数值，来源倍率由独立 AttributeSet 约束。
	void ClampRunAttributeValue(const FGameplayAttribute& Attribute, float& NewValue)
	{
		if (Attribute == UCatRunAttributeSet::GetDailyOfferingTargetAttribute()
			|| Attribute == UCatRunAttributeSet::GetLastOfferingPointsAttribute())
		{
			NewValue = ClampRunNonNegativeValue(NewValue);
			return;
		}
		if (Attribute == UCatRunAttributeSet::GetWorldProgressAttribute())
		{
			NewValue = ClampRunWorldProgressValue(NewValue);
			return;
		}
		if (Attribute == UCatRunAttributeSet::GetLastWorldProgressDeltaAttribute())
		{
			NewValue = ClampRunWorldProgressDeltaValue(NewValue);
		}
	}
}

// 复制声明流程：先保留父类字段，再把 Run 的目标、供品结果和世界进度按 Always RepNotify 注册；复制层只同步权威事实，不派生阶段或事务结果。
void UCatRunAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatRunAttributeSet, DailyOfferingTarget, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatRunAttributeSet, LastOfferingPoints, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatRunAttributeSet, WorldProgress, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatRunAttributeSet, LastWorldProgressDelta, COND_None, REPNOTIFY_Always);
}

// 基础值变化流程：GE 覆盖最终 Run 属性前按字段语义规整，防止 RunPublicState 投影到不可复制或越界的坏状态。
void UCatRunAttributeSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);
	ClampRunAttributeValue(Attribute, NewValue);
}

// 当前值变化流程：聚合值同样按最终字段语义规整；是否达标、归零或毕业仍由 GameMode 读取投影后按 Run 事务规则处理。
void UCatRunAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	ClampRunAttributeValue(Attribute, NewValue);
}

// 每日目标复制通知流程：把变更前值交给 ASC，保证依赖供品目标的客户端属性委托能按 GAS 规则收敛。
void UCatRunAttributeSet::OnRep_DailyOfferingTarget(const FGameplayAttributeData& OldDailyOfferingTarget)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCatRunAttributeSet, DailyOfferingTarget, OldDailyOfferingTarget);
}

// 最近供品点复制通知流程：把变更前值交给 ASC，客户端只观察上一晚提交结果，不在本地重算鱼价值。
void UCatRunAttributeSet::OnRep_LastOfferingPoints(const FGameplayAttributeData& OldLastOfferingPoints)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCatRunAttributeSet, LastOfferingPoints, OldLastOfferingPoints);
}

// 世界进度复制通知流程：把变更前值交给 ASC；服务器仍是进入失败或成功结算夜的唯一裁决者。
void UCatRunAttributeSet::OnRep_WorldProgress(const FGameplayAttributeData& OldWorldProgress)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCatRunAttributeSet, WorldProgress, OldWorldProgress);
}

// 最近进度变化复制通知流程：把变更前值交给 ASC，UI 可用它展示上一晚结果但不能推进 StateTree。
void UCatRunAttributeSet::OnRep_LastWorldProgressDelta(const FGameplayAttributeData& OldLastWorldProgressDelta)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCatRunAttributeSet, LastWorldProgressDelta, OldLastWorldProgressDelta);
}
