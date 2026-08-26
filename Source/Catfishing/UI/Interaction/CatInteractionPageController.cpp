#include "UI/Interaction/CatInteractionPageController.h"

#include "EnhancedInputComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Logging/CatLog.h"
#include "TimerManager.h"
#include "UI/CatUISettings.h"
#include "UI/Interaction/CatInteractionPromptWidget.h"
#include "UI/Interaction/CatInteractionTargetComponent.h"

// 绑定流程：保存 Controller 和提示 View，安装确认键，再启动低频扫描；首帧立即刷新，避免靠近对象时提示等到下一次计时。
bool UCatInteractionPageController::Bind(APlayerController* InController, UCatInteractionPromptWidget* InPromptView)
{
	Unbind();
	if (!InController || !InPromptView || !InController->IsLocalController())
	{
		return false;
	}
	BoundPlayerController = InController;
	BoundPromptView = InPromptView;
	InstallInteractionInput();
	if (UWorld* World = InController->GetWorld())
	{
		World->GetTimerManager().SetTimer(
			InteractionScanTimerHandle,
			this,
			&ThisClass::RefreshFocusedTarget,
			0.2f,
			true);
	}
	RefreshFocusedTarget();
	return true;
}

// 解绑流程：先取消扫描和输入绑定，再清空目标并渲染隐藏提示，防止旧目标在 Controller 切换后继续响应确认键。
void UCatInteractionPageController::Unbind()
{
	if (APlayerController* Controller = BoundPlayerController.Get())
	{
		if (UWorld* World = Controller->GetWorld())
		{
			World->GetTimerManager().ClearTimer(InteractionScanTimerHandle);
		}
	}
	InteractionScanTimerHandle.Invalidate();
	RemoveInteractionInput();
	FocusedTarget.Reset();
	RenderPrompt();
	BoundPlayerController.Reset();
	BoundPromptView.Reset();
}

// 扫描流程：按当前 Pawn 位置遍历同 World 的交互目标组件，只选择 CanInteract 且距离最近的一项。
void UCatInteractionPageController::RefreshFocusedTarget()
{
	APlayerController* Controller = BoundPlayerController.Get();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	UWorld* World = Controller ? Controller->GetWorld() : nullptr;
	if (!Controller || !Pawn || !World)
	{
		FocusedTarget.Reset();
		RenderPrompt();
		return;
	}

	const FVector PawnLocation = Pawn->GetActorLocation();
	UCatInteractionTargetComponent* BestTarget = nullptr;
	double BestDistanceSq = TNumericLimits<double>::Max();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}
		TArray<UCatInteractionTargetComponent*> TargetComponents;
		Actor->GetComponents<UCatInteractionTargetComponent>(TargetComponents);
		for (UCatInteractionTargetComponent* Target : TargetComponents)
		{
			if (!Target || !Target->CanInteract(Controller))
			{
				continue;
			}
			const double Radius = Target->GetInteractionRadiusCentimeters();
			if (Radius <= 0.0)
			{
				continue;
			}
			const double DistanceSq = FVector::DistSquared(PawnLocation, Actor->GetActorLocation());
			if (DistanceSq <= FMath::Square(Radius) && DistanceSq < BestDistanceSq)
			{
				BestDistanceSq = DistanceSq;
				BestTarget = Target;
			}
		}
	}
	FocusedTarget = BestTarget;
	RenderPrompt();
}

// 确认流程：如果当前目标已经失效先刷新一次；仍有效时把交互调用交给目标组件自己处理。
void UCatInteractionPageController::InteractWithFocusedTarget()
{
	APlayerController* Controller = BoundPlayerController.Get();
	UCatInteractionTargetComponent* Target = FocusedTarget.Get();
	if (!Controller)
	{
		return;
	}
	if (!Target || !Target->CanInteract(Controller))
	{
		RefreshFocusedTarget();
		Target = FocusedTarget.Get();
	}
	if (Target && Target->Interact(Controller))
	{
		UE_LOG(LogCatUI, Log, TEXT("Event=ui_interaction_confirmed Target=%s Controller=%s"),
			*GetNameSafe(Target->GetOwner()),
			*GetNameSafe(Controller));
	}
	RefreshFocusedTarget();
}

// 输入安装流程：只加载 Settings 中的确认 Action 和既有 InputContext 作为接线校验；运行时不创建 Action、不安装新 IMC、不硬写按键。
void UCatInteractionPageController::InstallInteractionInput()
{
	RemoveInteractionInput();
	APlayerController* Controller = BoundPlayerController.Get();
	UEnhancedInputComponent* Input = Controller ? Cast<UEnhancedInputComponent>(Controller->InputComponent) : nullptr;
	const UCatUISettings* Settings = GetDefault<UCatUISettings>();
	UInputAction* ConfirmAction = Settings ? Settings->LoadInteractionConfirmAction() : nullptr;
	const UInputMappingContext* MappingContext = Settings ? Settings->LoadGameplayInputMappingContext() : nullptr;
	if (!Input || !Settings || !ConfirmAction || !MappingContext)
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_interaction_input_unavailable Controller=%s Action=%s Context=%s"),
			*GetNameSafe(Controller),
			Settings ? *Settings->InteractionConfirmAction.ToSoftObjectPath().ToString() : TEXT("None"),
			Settings ? *Settings->LakeMenuInputMappingContext.ToSoftObjectPath().ToString() : TEXT("None"));
		return;
	}
	AppliedInteractionAction = ConfirmAction;
	InteractionInputBindingHandle = Input->BindAction(
		AppliedInteractionAction, ETriggerEvent::Started, this, &ThisClass::InteractWithFocusedTarget).GetHandle();
	BoundInteractionInputComponent = Input;
}

// 输入移除流程：从安装时记录的 EnhancedInputComponent 删除精确绑定；InputContext 属于 PlayerController 基础输入层，本页不安装也不移除它。
void UCatInteractionPageController::RemoveInteractionInput()
{
	if (UEnhancedInputComponent* Input = BoundInteractionInputComponent.Get();
		Input && InteractionInputBindingHandle != 0)
	{
		Input->RemoveBindingByHandle(InteractionInputBindingHandle);
	}
	BoundInteractionInputComponent.Reset();
	InteractionInputBindingHandle = 0;
	AppliedInteractionAction = nullptr;
}

// 提示渲染流程：当前有目标时显示“靠近对象按键交互”；没有目标或 View 不可用时只写隐藏投影。
void UCatInteractionPageController::RenderPrompt()
{
	UCatInteractionPromptWidget* PromptView = BoundPromptView.Get();
	APlayerController* Controller = BoundPlayerController.Get();
	if (!PromptView)
	{
		return;
	}

	FCatInteractionPromptViewState PromptState;
	if (UCatInteractionTargetComponent* Target = FocusedTarget.Get())
	{
		const UCatUISettings* Settings = GetDefault<UCatUISettings>();
		PromptState.bVisible = true;
		PromptState.TargetText = Target->GetInteractionTargetText(Controller);
		PromptState.ConfirmKeyName = Settings ? Settings->ResolveInteractionConfirmKeyName() : NAME_None;
		const FString KeyText = PromptState.ConfirmKeyName.IsNone()
			? FString(TEXT("交互键"))
			: PromptState.ConfirmKeyName.ToString();
		PromptState.PromptText = FText::FromString(FString::Printf(TEXT("靠近%s：按 %s 交互"),
			*PromptState.TargetText.ToString(),
			*KeyText));
	}
	PromptView->RenderPrompt(PromptState);
}
