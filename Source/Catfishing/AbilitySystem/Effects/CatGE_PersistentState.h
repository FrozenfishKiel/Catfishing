#pragma once
#include "GameplayEffect.h"
#include "CatGE_PersistentState.generated.h"

/** 来源持有的无限状态效果；只授予 Tag，不修改属性，不与其他来源合并。 */
UCLASS()
class CATFISHING_API UCatGE_PersistentState : public UGameplayEffect
{
	GENERATED_BODY()
public:
	/** 状态持续到来源主动撤销；每个来源拥有独立句柄。 */
	UCatGE_PersistentState();
};
