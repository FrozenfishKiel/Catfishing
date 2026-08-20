#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "CatLocalPlayerInputSubsystem.generated.h"

class APlayerController;
class UEnhancedInputLocalPlayerSubsystem;
class UInputMappingContext;

/** 每个 LocalPlayer 的全局 EnhancedInput 装配器；只添加/移除配置好的 MappingContext，不绑定具体玩法动作。 */
UCLASS()
class CATFISHING_API UCatLocalPlayerInputSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	/** LocalPlayer 初始化时尝试装配显式配置的全局 MappingContext；默认配置关闭时不触碰输入系统。 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** LocalPlayer 销毁时只移除本子系统曾添加的 MappingContext，不调用 ClearAllMappings。 */
	virtual void Deinitialize() override;

	/** Controller 变化时重新按当前 LocalPlayer 输入子系统装配；旧上下文先成对移除。 */
	virtual void PlayerControllerChanged(APlayerController* NewController) override;

	/** 重新读取设置并装配 MappingContext；返回 false 表示配置或 EnhancedInput 依赖尚未就绪。 */
	bool RefreshConfiguredInputContexts();

private:
	/** 精确移除本子系统记录过的 MappingContext；重复调用保持幂等，不影响 Character 诊断输入。 */
	void RemoveAppliedInputContexts();

	/** 当前实际接收 AddMappingContext 的 EnhancedInput 子系统；移除时必须回到同一个实例。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> AppliedInputSubsystem;

	/** 本子系统已经添加的 MappingContext；只用于成对移除，不作为正式输入资产清单。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputMappingContext>> AppliedMappingContexts;
};