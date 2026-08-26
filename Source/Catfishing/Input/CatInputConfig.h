#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "CatInputConfig.generated.h"

class UInputAction;

/**
 * 不经过 GAS 的输入映射。InputAction 只描述设备输入，InputTag 描述玩家意图；
 * Controller/组件按 Tag 分发，因而改键位不会修改玩法代码。
 */
USTRUCT(BlueprintType)
struct FCatNativeInputAction
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category="Input")
	TObjectPtr<const UInputAction> InputAction;

	UPROPERTY(EditDefaultsOnly, Category="Input")
	FGameplayTag InputTag;
};

/** Lyra 风格输入配置的通用层；Ability 配置在 AbilitySystem 中继承并追加 AbilityInputActions。 */
UCLASS(BlueprintType, Const)
class CATFISHING_API UCatInputConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 空数组保持向后兼容；一旦配置，每个 Action 与 Tag 都必须有效且唯一。 */
	bool IsNativeInputConfigurationValid() const;

	const UInputAction* FindNativeInputActionForTag(FGameplayTag InputTag) const;

	UPROPERTY(EditDefaultsOnly, Category="Input|Native")
	TArray<FCatNativeInputAction> NativeInputActions;
};
