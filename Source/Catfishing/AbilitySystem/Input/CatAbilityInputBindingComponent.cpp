#include "AbilitySystem/Input/CatAbilityInputBindingComponent.h"

#include "AbilitySystem/Config/CatAbilityInputConfig.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/Pawn.h"

UCatAbilityInputBindingComponent::UCatAbilityInputBindingComponent()
{
	// 构造流程：关闭 Tick 和复制，只让拥有它的本地 Controller 在输入生命周期节点显式驱动，避免组件自己建立第二条运行时循环。
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

void UCatAbilityInputBindingComponent::BindAbilityActions(UEnhancedInputComponent& InputComponent,
	const UCatAbilityInputConfig* InputConfig)
{
	// 绑定流程：
	// 1. 先拒绝缺失或运行时未就绪的 Ability 输入配置，防止半套绑定绕过 AbilitySettings 的正式 gate。
	// 2. 同一个 EnhancedInputComponent 只绑定一次；UE 输入组件没有被本类拥有，不能通过重复 SetupInputComponent 堆叠回调。
	// 3. 对每个 Ability Action 同时绑定按下和两类释放边沿，后续具体 Ability 查找、激活策略和预测事件都交给 ASC。
	if (!InputConfig || !InputConfig->IsRuntimeReady() || BoundInputComponent.Get() == &InputComponent)
	{
		return;
	}
	for (const FCatAbilityInputAction& Entry : InputConfig->AbilityInputActions)
	{
		if (!Entry.InputAction || !Entry.InputTag.IsValid())
		{
			continue;
		}
		InputComponent.BindAction(Entry.InputAction, ETriggerEvent::Started,
			this, &ThisClass::HandleAbilityInputTagPressed, Entry.InputTag);
		InputComponent.BindAction(Entry.InputAction, ETriggerEvent::Completed,
			this, &ThisClass::HandleAbilityInputTagReleased, Entry.InputTag);
		InputComponent.BindAction(Entry.InputAction, ETriggerEvent::Canceled,
			this, &ThisClass::HandleAbilityInputTagReleased, Entry.InputTag);
	}
	BoundInputComponent = &InputComponent;
}

void UCatAbilityInputBindingComponent::RefreshForPawn(APawn* Pawn)
{
	// 路由刷新流程：
	// 1. 从 Pawn 的 AbilitySystemInterface 解析项目 ASC，不依赖具体 Character 类型。
	// 2. 旧 ASC 若不同于新 ASC，先清掉仍按住的输入，防止旧身体继续消费边沿。
	// 3. 新 ASC 也清一次输入，保证重生、复制 Pawn 和旅行后不会继承上一具身体的输入状态。
	UCatAbilitySystemComponent* NewAbilitySystem = UCatAbilitySystemComponent::FindCatAbilitySystemFromActor(Pawn);
	if (UCatAbilitySystemComponent* PreviousAbilitySystem = RoutedAbilitySystem.Get();
		PreviousAbilitySystem && PreviousAbilitySystem != NewAbilitySystem)
	{
		PreviousAbilitySystem->ResetAbilityInput();
	}
	if (NewAbilitySystem)
	{
		NewAbilitySystem->ResetAbilityInput();
	}
	RoutedAbilitySystem = NewAbilitySystem;
}

void UCatAbilityInputBindingComponent::ProcessAbilityInput(const float DeltaTime, const bool bGamePaused)
{
	// 帧处理流程：只把当前帧时长和暂停状态交给已路由 ASC；没有 Pawn/ASC 时不缓存输入，等待下一次 RefreshForPawn 建立目标。
	if (UCatAbilitySystemComponent* AbilitySystem = RoutedAbilitySystem.Get())
	{
		AbilitySystem->ProcessAbilityInput(DeltaTime, bGamePaused);
	}
}

void UCatAbilityInputBindingComponent::ResetAbilityInput()
{
	// 重置流程：当前 ASC 仍存在时先清输入状态，再忘记 ASC；已安装的输入组件绑定不在占有变化时撤销，避免下次 SetupInputComponent 重入重复绑定。
	if (UCatAbilitySystemComponent* AbilitySystem = RoutedAbilitySystem.Get())
	{
		AbilitySystem->ResetAbilityInput();
	}
	RoutedAbilitySystem.Reset();
}

void UCatAbilityInputBindingComponent::HandleAbilityInputTagPressed(const FGameplayTag InputTag)
{
	// 按下转发流程：回调只认当前 ASC；目标缺失时静默丢弃边沿，避免组件替 Ability 或领域命令保存补发状态。
	if (UCatAbilitySystemComponent* AbilitySystem = RoutedAbilitySystem.Get())
	{
		AbilitySystem->AbilityInputTagPressed(InputTag);
	}
}

void UCatAbilityInputBindingComponent::HandleAbilityInputTagReleased(const FGameplayTag InputTag)
{
	// 释放转发流程：回调只认当前 ASC；持续技能释放、复制事件和清理策略继续由 UCatAbilitySystemComponent 统一处理。
	if (UCatAbilitySystemComponent* AbilitySystem = RoutedAbilitySystem.Get())
	{
		AbilitySystem->AbilityInputTagReleased(InputTag);
	}
}
