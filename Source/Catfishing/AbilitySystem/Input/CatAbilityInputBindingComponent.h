#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "CatAbilityInputBindingComponent.generated.h"

class APawn;
class UCatAbilityInputConfig;
class UCatAbilitySystemComponent;
class UEnhancedInputComponent;

/** Ability 输入绑定组件；把 Enhanced Input 的 Ability Tag 边沿集中送入当前 Pawn ASC，Controller 只负责把输入组件和 Pawn 生命周期交给它。 */
UCLASS(ClassGroup=(Catfishing))
class CATFISHING_API UCatAbilityInputBindingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 建立不 Tick、不复制的本地输入路由组件；它只服务拥有它的 Controller 输入生命周期。 */
	UCatAbilityInputBindingComponent();

	/** 在指定 EnhancedInputComponent 上绑定 Ability 输入标签；重复传入同一组件时保持幂等，配置缺失时不安装半套输入。 */
	void BindAbilityActions(UEnhancedInputComponent& InputComponent, const UCatAbilityInputConfig* InputConfig);

	/** Pawn 变化时切换当前 ASC 输入目标；上一个 ASC 和新 ASC 都会清空边沿状态，避免按住输入穿过重生或旅行。 */
	void RefreshForPawn(APawn* Pawn);

	/** 消费当前 ASC 的本帧 Ability 输入；暂停时具体保留或丢弃策略仍由 ASC 自己决定。 */
	void ProcessAbilityInput(float DeltaTime, bool bGamePaused);

	/** 清空当前路由 ASC 的输入状态并忘记 ASC；输入组件绑定记录保留到组件销毁，防止占有重入重复绑定。 */
	void ResetAbilityInput();

private:
	/** Ability 按下标签回调；只转交当前 ASC，不在组件里解释具体技能或领域命令。 */
	void HandleAbilityInputTagPressed(FGameplayTag InputTag);

	/** Ability 释放标签回调；只转交当前 ASC，由 ASC 决定持续激活 Ability 的释放事件。 */
	void HandleAbilityInputTagReleased(FGameplayTag InputTag);

	/** 当前接收 Ability 输入边沿的 ASC；Refresh/Reset 独占读写，防止 Controller 再保存第二份路由状态。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatAbilitySystemComponent> RoutedAbilitySystem;

	/** 已安装 Ability 输入绑定的 EnhancedInputComponent；用于阻止 SetupInputComponent 重入产生重复绑定。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UEnhancedInputComponent> BoundInputComponent;
};
