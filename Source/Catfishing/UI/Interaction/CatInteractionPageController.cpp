#include "UI/Interaction/CatInteractionPageController.h"

#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/CatInteractable.h"
#include "Interaction/CatInteractionTargetingComponent.h"
#include "UI/CatUISettings.h"
#include "UI/Interaction/CatInteractionPromptWidget.h"

bool UCatInteractionPageController::Bind(APlayerController* InController,
	UCatInteractionPromptWidget* InPromptView)
{
	Unbind();
	ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(InController);
	UCatInteractionTargetingComponent* Targeting = CatController
		? CatController->GetInteractionTargetingComponent() : nullptr;
	if (!CatController || !CatController->IsLocalController() || !InPromptView || !Targeting)
	{
		return false;
	}

	BoundPlayerController = CatController;
	BoundPromptView = InPromptView;
	BoundTargetingComponent = Targeting;
	TargetChangedHandle = Targeting->OnTargetChanged.AddUObject(this, &ThisClass::HandleTargetChanged);
	RefreshFocusedTarget();
	return true;
}

void UCatInteractionPageController::Unbind()
{
	if (UCatInteractionTargetingComponent* Targeting = BoundTargetingComponent.Get())
	{
		Targeting->OnTargetChanged.Remove(TargetChangedHandle);
	}
	TargetChangedHandle.Reset();
	FocusedTarget.Reset();
	RenderPrompt();
	BoundTargetingComponent.Reset();
	BoundPlayerController.Reset();
	BoundPromptView.Reset();
}

void UCatInteractionPageController::RefreshFocusedTarget()
{
	UCatInteractionTargetingComponent* Targeting = BoundTargetingComponent.Get();
	if (!Targeting)
	{
		FocusedTarget.Reset();
		RenderPrompt();
		return;
	}
	Targeting->RefreshTargetFromCrosshair();
	FocusedTarget = Targeting->GetCurrentTarget();
	RenderPrompt();
}

void UCatInteractionPageController::InteractWithFocusedTarget()
{
	if (UCatInteractionTargetingComponent* Targeting = BoundTargetingComponent.Get())
	{
		Targeting->TryInteract();
		RefreshFocusedTarget();
	}
}

void UCatInteractionPageController::HandleTargetChanged(AActor* PreviousTarget, AActor* CurrentTarget)
{
	(void)PreviousTarget;
	FocusedTarget = CurrentTarget;
	RenderPrompt();
}

void UCatInteractionPageController::RenderPrompt()
{
	UCatInteractionPromptWidget* PromptView = BoundPromptView.Get();
	APlayerController* Controller = BoundPlayerController.Get();
	if (!PromptView)
	{
		return;
	}

	FCatInteractionPromptViewState PromptState;
	AActor* Target = FocusedTarget.Get();
	if (Controller && IsValid(Target)
		&& Target->GetClass()->ImplementsInterface(UCatInteractable::StaticClass())
		&& ICatInteractable::Execute_CanInteract(Target, Controller))
	{
		const FText ActionText = ICatInteractable::Execute_GetInteractionPrompt(Target);
		if (!ActionText.IsEmpty())
		{
			const UCatUISettings* Settings = GetDefault<UCatUISettings>();
			PromptState.bVisible = true;
			PromptState.TargetText = ActionText;
			PromptState.ConfirmKeyName = Settings ? Settings->ResolveInteractionConfirmKeyName() : NAME_None;
			const FText KeyText = PromptState.ConfirmKeyName.IsNone()
				? NSLOCTEXT("Catfishing", "GenericInteractionKey", "交互键")
				: FText::FromName(PromptState.ConfirmKeyName);
			PromptState.PromptText = FText::Format(
				NSLOCTEXT("Catfishing", "GenericInteractionPrompt", "按 {0} {1}"), KeyText, ActionText);
		}
	}
	PromptView->RenderPrompt(PromptState);
}
