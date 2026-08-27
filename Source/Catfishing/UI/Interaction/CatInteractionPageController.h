#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CatInteractionPageController.generated.h"

class APlayerController;
class UCatInteractionPromptWidget;
class UCatInteractionTargetingComponent;

/**
 * 本地交互提示控制器。E 键只由 PlayerController 的 Native Input Tag 绑定；
 * 本对象只订阅同一个准星目标组件并把 ICatInteractable 的提示投影到正式 WBP。
 */
UCLASS()
class CATFISHING_API UCatInteractionPageController : public UObject
{
	GENERATED_BODY()

public:
	/** 绑定当前本地 Controller 的唯一 TargetingComponent 与提示 View。 */
	bool Bind(APlayerController* InController, UCatInteractionPromptWidget* InPromptView);

	/** 移除目标变化订阅，隐藏提示并释放弱引用。 */
	void Unbind();

	/** 主动刷新准星目标；不扫描第二套靠近式组件。 */
	void RefreshFocusedTarget();

	/** 便于 UI/自动化显式请求同一次交互；正常 E 键由 PlayerController 直接调用 TargetingComponent。 */
	void InteractWithFocusedTarget();

private:
	void HandleTargetChanged(AActor* PreviousTarget, AActor* CurrentTarget);
	void RenderPrompt();

	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> BoundPlayerController;

	UPROPERTY(Transient)
	TWeakObjectPtr<UCatInteractionPromptWidget> BoundPromptView;

	UPROPERTY(Transient)
	TWeakObjectPtr<UCatInteractionTargetingComponent> BoundTargetingComponent;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> FocusedTarget;

	FDelegateHandle TargetChangedHandle;
};
