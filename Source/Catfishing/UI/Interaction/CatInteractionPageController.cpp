#include "UI/Interaction/CatInteractionPageController.h"

#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/CatInteractable.h"
#include "Interaction/CatInteractionTargetingComponent.h"
#include "UI/CatUISettings.h"
#include "UI/Interaction/CatInteractionPromptWidget.h"

// 绑定流程：先解除旧页面，再验证本地控制器与视图；订阅唯一准星刷新并立即读取当前目标，不增加轮询计时器。
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
	TargetRefreshHandle = Targeting->OnTargetRefreshed.AddUObject(this, &ThisClass::HandleTargetRefreshed);
	RefreshFocusedTarget();
	return true;
}

// 解绑流程：先移除本页面订阅，再清目标并隐藏提示，最后释放控件与控制器弱引用。
void UCatInteractionPageController::Unbind()
{
	if (UCatInteractionTargetingComponent* Targeting = BoundTargetingComponent.Get())
	{
		Targeting->OnTargetRefreshed.Remove(TargetRefreshHandle);
	}
	TargetRefreshHandle.Reset();
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

// 刷新流程：以准星当前对象替换弱引用，再重读接口文本；不从旧人数或旧目标推导新提示。
void UCatInteractionPageController::HandleTargetRefreshed(AActor* PreviousTarget, AActor* CurrentTarget)
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
