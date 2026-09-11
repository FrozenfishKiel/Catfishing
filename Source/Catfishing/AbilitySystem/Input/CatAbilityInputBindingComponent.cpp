#include "AbilitySystem/Input/CatAbilityInputBindingComponent.h"

#include "AbilitySystem/Config/CatAbilityInputConfig.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "EnhancedInputComponent.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "GameFramework/Pawn.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/Presentation/CatFishingCameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Logging/CatLog.h"
#include "Logging/CatLogContext.h"

UCatAbilityInputBindingComponent::UCatAbilityInputBindingComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

void UCatAbilityInputBindingComponent::BindAbilityActions(UEnhancedInputComponent& InputComponent,
	const UCatAbilityInputConfig* InputConfig)
{
	if (!InputConfig || !InputConfig->IsRuntimeReady() || BoundInputComponent.Get() == &InputComponent) return;
	for (const FCatAbilityInputAction& Entry : InputConfig->AbilityInputActions)
	{
		if (!Entry.InputAction || !Entry.InputTag.IsValid()) continue;
		InputComponent.BindAction(Entry.InputAction, ETriggerEvent::Started,
			this, &ThisClass::HandleAbilityInputTagPressed, Entry.InputTag);
		InputComponent.BindAction(Entry.InputAction, ETriggerEvent::Completed,
			this, &ThisClass::HandleAbilityInputTagReleased, Entry.InputTag);
		InputComponent.BindAction(Entry.InputAction, ETriggerEvent::Canceled,
			this, &ThisClass::HandleAbilityInputTagCanceled, Entry.InputTag);
	}
	BoundInputComponent = &InputComponent;
}

void UCatAbilityInputBindingComponent::RefreshForPawn(APawn* Pawn)
{
	UCatAbilitySystemComponent* NewAbilitySystem = UCatAbilitySystemComponent::FindCatAbilitySystemFromActor(Pawn);
	if (RoutedPawn.Get() == Pawn && RoutedAbilitySystem.Get() == NewAbilitySystem) return;
	ReleaseAllInputRoutes(TEXT("PawnChanged"));
	RoutedPawn = Pawn;
	RoutedAbilitySystem = NewAbilitySystem;
	if (NewAbilitySystem) NewAbilitySystem->ResetAbilityInput();
}

void UCatAbilityInputBindingComponent::ProcessAbilityInput(const float DeltaTime, const bool bGamePaused)
{
	// 帧处理流程：先读取当前 Pawn 的 ASC 路由；翻天锁生效时清掉已按住和边沿输入，防止锁前能力在物理控制被清理后继续激活。
	// 未锁定时才把时长和暂停状态交给 ASC；路由尚未就绪则不缓存输入，等待 Controller 的 Pawn 切换重新建立。
	if (UCatAbilitySystemComponent* AbilitySystem = RoutedAbilitySystem.Get())
	{
		const ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(GetOwner());
		if (Controller && Controller->IsDayTransitionInputBlocked())
		{
			AbilitySystem->ResetAbilityInput();
			return;
		}
		AbilitySystem->ProcessAbilityInput(DeltaTime, bGamePaused);
	}
}

void UCatAbilityInputBindingComponent::ReleaseAllInputRoutes(const FName Reason)
{
	// 生命周期取消不能经过 Primary InputReleased（瞄准时该方法会抛钩）。
	// 先丢弃边沿，再取消对应活跃 Spec；领域持续输入另由 Controller 的统一清理入口收口。
	TSet<UCatAbilitySystemComponent*> Systems;
	for (const TPair<FGameplayTag, FPressedRoute>& Pair : PressedRoutes)
	{
		if (UCatAbilitySystemComponent* AbilitySystem = Pair.Value.AbilitySystem.Get()) Systems.Add(AbilitySystem);
	}
	for (UCatAbilitySystemComponent* AbilitySystem : Systems) AbilitySystem->ResetAbilityInput();
	TArray<FGameplayTag> Tags;
	PressedRoutes.GetKeys(Tags);
	for (const FGameplayTag Tag : Tags)
	{
		FPressedRoute Route;
		if (!PressedRoutes.RemoveAndCopyValue(Tag, Route)) continue;
		if (UCatPhysicsGrabComponent* Grab = Route.Grab.Get()) Grab->SetGrabInput(Route.bLeft, false);
		if (UCatAbilitySystemComponent* AbilitySystem = Route.AbilitySystem.Get())
		{
			TArray<FGameplayAbilitySpecHandle> Handles;
			for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
				if (Spec.IsActive() && Spec.GetDynamicSpecSourceTags().HasTagExact(Tag)) Handles.Add(Spec.Handle);
			for (const FGameplayAbilitySpecHandle Handle : Handles) AbilitySystem->CancelAbilityHandle(Handle);
		}
		UE_LOG(LogCatfishing, Display, TEXT("Event=physical_input_route_canceled InputTag=%s Route=%s Reason=%s Result=OriginalRecipientCanceled %s"),
			*Tag.ToString(), Route.Grab.IsValid() ? TEXT("Grab") : TEXT("Ability"), *Reason.ToString(),
			*CatLogContext::BuildControllerFields(Cast<APlayerController>(GetOwner())));
	}
	if (const ACatCharacter* Character = Cast<ACatCharacter>(RoutedPawn.Get()))
	{
		if (UCatPhysicalBodyComponent* Body = Character->GetPhysicalBodyComponent())
		{
			if (UCatPhysicsGrabComponent* Grab = Body->GetGrab())
			{
				Grab->SetGrabInput(true, false);
				Grab->SetGrabInput(false, false);
			}
		}
	}
	if (!Tags.IsEmpty())
	{
		UE_LOG(LogCatfishing, Display, TEXT("Event=physical_input_routes_cleared Reason=%s RouteCount=%d %s"),
			*Reason.ToString(), Tags.Num(), *CatLogContext::BuildControllerFields(Cast<APlayerController>(GetOwner())));
	}
}

void UCatAbilityInputBindingComponent::ResetAbilityInput()
{
	ReleaseAllInputRoutes(TEXT("InputReset"));
	if (UCatAbilitySystemComponent* AbilitySystem = RoutedAbilitySystem.Get()) AbilitySystem->ResetAbilityInput();
	RoutedAbilitySystem.Reset();
	RoutedPawn.Reset();
}

void UCatAbilityInputBindingComponent::HandleAbilityInputTagPressed(const FGameplayTag InputTag)
{
	if (PressedRoutes.Contains(InputTag)) return;
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (Controller && Controller->IsMoveInputIgnored()) return;
	if (InputTag == CatFishingAbilityTags::Input_Fishing_Cancel) ReleaseAllInputRoutes(TEXT("CancelInput"));
	const bool bLeft = InputTag == CatFishingAbilityTags::Input_Fishing_Primary;
	const bool bHandInput = bLeft || InputTag == CatFishingAbilityTags::Input_Fishing_Slack;
	const ACatFishingRodActor* Rod = UCatFishingCameraComponent::FindHeldRodOperatedBy(Controller);
	const bool bPrimary = Rod && Rod->IsPrimaryOperator(Controller ? Controller->PlayerState : nullptr);
	FPressedRoute Route;
	if (bHandInput && !bPrimary)
	{
		const ACatCharacter* Character = Cast<ACatCharacter>(RoutedPawn.Get());
		UCatPhysicalBodyComponent* Body = Character ? Character->GetPhysicalBodyComponent() : nullptr;
		if (Body && Body->GetGrab())
		{
			Route.Grab = Body->GetGrab();
			Route.bLeft = bLeft;
			PressedRoutes.Add(InputTag, Route);
			Route.Grab->SetGrabInput(bLeft, true);
		}
		else return;
	}
	else if (UCatAbilitySystemComponent* AbilitySystem = RoutedAbilitySystem.Get())
	{
		Route.AbilitySystem = AbilitySystem;
		PressedRoutes.Add(InputTag, Route);
		AbilitySystem->AbilityInputTagPressed(InputTag);
	}
	else return;
	UE_LOG(LogCatfishing, Display, TEXT("Event=physical_input_route_pressed InputTag=%s Route=%s Pawn=%s %s"),
		*InputTag.ToString(), Route.Grab.IsValid() ? TEXT("Grab") : TEXT("Ability"),
		*GetNameSafe(RoutedPawn.Get()), *CatLogContext::BuildControllerFields(Controller));
}

void UCatAbilityInputBindingComponent::HandleAbilityInputTagReleased(const FGameplayTag InputTag)
{
	FPressedRoute Route;
	if (!PressedRoutes.RemoveAndCopyValue(InputTag, Route)) return;
	if (UCatPhysicsGrabComponent* Grab = Route.Grab.Get()) Grab->SetGrabInput(Route.bLeft, false);
	if (UCatAbilitySystemComponent* AbilitySystem = Route.AbilitySystem.Get()) AbilitySystem->AbilityInputTagReleased(InputTag);
	UE_LOG(LogCatfishing, Display, TEXT("Event=physical_input_route_released InputTag=%s Route=%s Result=OriginalRecipientReleased %s"),
		*InputTag.ToString(), Route.Grab.IsValid() ? TEXT("Grab") : TEXT("Ability"),
		*CatLogContext::BuildControllerFields(Cast<APlayerController>(GetOwner())));
}

void UCatAbilityInputBindingComponent::HandleAbilityInputTagCanceled(const FGameplayTag InputTag)
{
	if (!PressedRoutes.Contains(InputTag)) return;
	if (ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(GetOwner()))
		Controller->ClearPhysicalControlInput(TEXT("InputCanceled"));
	else ReleaseAllInputRoutes(TEXT("InputCanceled"));
}
