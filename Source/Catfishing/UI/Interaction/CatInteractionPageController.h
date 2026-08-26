#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CatInteractionPageController.generated.h"

class APlayerController;
class UCatInteractionPromptWidget;
class UCatInteractionTargetComponent;
class UEnhancedInputComponent;
class UInputAction;

/** 本地交互提示和确认键控制器；它只扫描通用交互目标，不知道商店、鱼缸或祭坛的具体 UI。 */
UCLASS()
class CATFISHING_API UCatInteractionPageController : public UObject
{
	GENERATED_BODY()

public:
	/** 绑定当前本地 Controller 和提示 View；成功后安装确认键并开始扫描最近交互目标。 */
	bool Bind(APlayerController* InController, UCatInteractionPromptWidget* InPromptView);

	/** 停止扫描、移除确认键绑定、隐藏提示并清空当前目标。 */
	void Unbind();

	/** 主动刷新最近交互目标和提示文本；Pawn 移动、目标开关或输入后都可调用。 */
	void RefreshFocusedTarget();

	/** 对当前最近目标执行确认交互；没有目标时先刷新一次再决定是否调用。 */
	void InteractWithFocusedTarget();

private:
	/** 安装交互确认 Action；只 BindAction，不 AddMappingContext、不 MapKey。 */
	void InstallInteractionInput();

	/** 移除交互确认 Action；从安装时记录的 EnhancedInputComponent 精确删除绑定。 */
	void RemoveInteractionInput();

	/** 用当前目标和 Settings 中的确认键名渲染提示；无目标时保持隐藏。 */
	void RenderPrompt();

	/** 当前本地 Controller；扫描、输入和交互调用都只作用于它。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> BoundPlayerController;

	/** 当前交互提示 View；它只显示文本，不拥有世界目标。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatInteractionPromptWidget> BoundPromptView;

	/** 当前距离最近且可用的交互目标；目标自身拥有实际交互逻辑。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatInteractionTargetComponent> FocusedTarget;

	/** 当前绑定的确认 Action；保存强引用是为了成对移除输入绑定。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> AppliedInteractionAction;

	/** 确认 Action 实际绑定的 EnhancedInputComponent；换 Controller 时从原组件精确移除。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UEnhancedInputComponent> BoundInteractionInputComponent;

	/** EnhancedInput 返回的绑定句柄；0 表示没有有效绑定。 */
	uint32 InteractionInputBindingHandle = 0;

	/** 定时扫描最近交互目标的句柄；Unbind 和 World teardown 时清掉。 */
	FTimerHandle InteractionScanTimerHandle;
};
