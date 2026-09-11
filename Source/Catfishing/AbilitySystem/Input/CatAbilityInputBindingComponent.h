#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "CatAbilityInputBindingComponent.generated.h"

class APawn;
class UCatAbilityInputConfig;
class UCatAbilitySystemComponent;
class UEnhancedInputComponent;
class UCatPhysicsGrabComponent;

/** Enhanced Input 路由：非主控的左右键控制抓握，主控及其他标签进入 ASC；每次按下锁定接收方。 */
UCLASS(ClassGroup=(Catfishing))
class CATFISHING_API UCatAbilityInputBindingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 建立不 Tick、不复制的本地输入路由组件；它只服务拥有它的 Controller 输入生命周期。 */
	UCatAbilityInputBindingComponent();

	/** 在指定 EnhancedInputComponent 上绑定 Ability 输入标签；重复传入同一组件时保持幂等，配置缺失时不安装半套输入。 */
	void BindAbilityActions(UEnhancedInputComponent& InputComponent, const UCatAbilityInputConfig* InputConfig);

	/** Pawn 变化先松开旧接收方，再切换 ASC 与身体；重复刷新同一 Pawn 不打断按住动作。 */
	void RefreshForPawn(APawn* Pawn);

	/** 消费当前 ASC 的本帧 Ability 输入；暂停时具体保留或丢弃策略仍由 ASC 自己决定。 */
	void ProcessAbilityInput(float DeltaTime, bool bGamePaused);

	/** 清空当前路由 ASC 的输入状态并忘记 ASC；输入组件绑定记录保留到组件销毁，防止占有重入重复绑定。 */
	void ResetAbilityInput();

	/** 松开按下时锁定的接收方；菜单、失焦和换 Pawn 共用，不修改物理速度。 */
	void ReleaseAllInputRoutes(FName Reason);

private:
	friend class FCatPhysicalInputRouteTest;
	struct FPressedRoute
	{
		TWeakObjectPtr<UCatAbilitySystemComponent> AbilitySystem;
		TWeakObjectPtr<UCatPhysicsGrabComponent> Grab;
		bool bLeft = false;
	};
	TMap<FGameplayTag, FPressedRoute> PressedRoutes;
	TWeakObjectPtr<APawn> RoutedPawn;
	/** 按当前鱼竿主控事实选择 Grab/ASC，只在按下边沿选择一次。 */
	void HandleAbilityInputTagPressed(FGameplayTag InputTag);

	/** 释放始终交给按下时的接收方，操作位变化、离竿或取消不会吞掉原抓握释放。 */
	void HandleAbilityInputTagReleased(FGameplayTag InputTag);
	/** Mapping Context 取消不是玩家主动松键；终止按住动作，避免取消瞄准被解释成抛钩。 */
	void HandleAbilityInputTagCanceled(FGameplayTag InputTag);

	/** 当前接收 Ability 输入边沿的 ASC；Refresh/Reset 独占读写，防止 Controller 再保存第二份路由状态。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatAbilitySystemComponent> RoutedAbilitySystem;

	/** 已安装 Ability 输入绑定的 EnhancedInputComponent；用于阻止 SetupInputComponent 重入产生重复绑定。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UEnhancedInputComponent> BoundInputComponent;
};
