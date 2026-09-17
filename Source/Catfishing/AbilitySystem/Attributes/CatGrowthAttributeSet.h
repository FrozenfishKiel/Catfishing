#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Attributes/CatAttributeSet.h"
#include "CatGrowthAttributeSet.generated.h"

/** 成长效果的输入属性集；经验槽仍由 Growth 持有，元属性仅把 GE 的求值结果送到成长规则。 */
UCLASS()
class CATFISHING_API UCatGrowthAttributeSet : public UCatAttributeSet
{
	GENERATED_BODY()
public:
	/** 本次效果给予的经验点数；仅服务器短暂存在，消费即清零，不作为第二份余额复制或保存。 */
	UPROPERTY() FGameplayAttributeData IncomingExperience;
	ATTRIBUTE_ACCESSORS_BASIC(UCatGrowthAttributeSet, IncomingExperience)
	/** GE 计算后清空元属性，再把有效经验交给成长规则；不接收或返回库存事务结果。 */
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;
};
