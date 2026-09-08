#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "CatAttributeSet.generated.h"

#define ATTRIBUTE_ACCESSORS_BASIC(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

/** 项目所有 AttributeSet 的最小公共基类；它只集中 GAS 访问器宏，避免每个属性集各自声明一套属性访问口径。 */
UCLASS(Abstract)
class CATFISHING_API UCatAttributeSet : public UAttributeSet
{
	GENERATED_BODY()
};
