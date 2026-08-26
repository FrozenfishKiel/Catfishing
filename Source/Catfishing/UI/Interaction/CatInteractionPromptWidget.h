#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CatInteractionPromptWidget.generated.h"

class UTextBlock;

/** 交互提示的只读显示投影；它只描述当前靠近对象和确认键，不直接打开商店、鱼缸或祭坛。 */
USTRUCT(BlueprintType)
struct FCatInteractionPromptViewState
{
	GENERATED_BODY()

	/** 当前是否有可展示交互目标；false 时 WBP 应隐藏提示。 */
	UPROPERTY(BlueprintReadOnly)
	bool bVisible = false;

	/** 当前可交互对象的短名称；例如商店、鱼缸、祭坛或修竿点。 */
	UPROPERTY(BlueprintReadOnly)
	FText TargetText;

	/** 当前确认交互的键名；它来自正式输入资产或交互系统投影，不在 Widget 内硬写。 */
	UPROPERTY(BlueprintReadOnly)
	FName ConfirmKeyName = NAME_None;

	/** 给 TextBlock 直接绑定的完整中文提示。 */
	UPROPERTY(BlueprintReadOnly)
	FText PromptText;
};

/** 交互提示 WBP 基类；它只负责“靠近某对象可按键”的提示，不直接拥有任何对象 UI。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatInteractionPromptWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 接收交互系统或 LocalPlayer 传入的只读提示投影，并同步给 WBP 表现。 */
	void RenderPrompt(const FCatInteractionPromptViewState& ViewState);

	/** 暴露最近一次交互提示投影给蓝图表现；它只描述提示内容，不能替代交互组件执行打开逻辑。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Interaction")
	const FCatInteractionPromptViewState& GetLastPromptViewState() const;

protected:
	/** WBP 可选渲染扩展点；Designer 也可以直接绑定 BlueprintPromptText 和 bBlueprintVisible。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|Interaction")
	void BP_RenderPrompt(const FCatInteractionPromptViewState& ViewState);

private:
	/** 最近一次提示投影；本 Widget 不持有交互对象或执行命令。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Interaction", meta = (AllowPrivateAccess = "true"))
	FCatInteractionPromptViewState LastPromptViewState;

	/** 给 WBP TextBlock 直接绑定的提示文本。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Interaction", meta = (AllowPrivateAccess = "true"))
	FText BlueprintPromptText;

	/** 给 WBP 可见性绑定的提示显隐。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Interaction", meta = (AllowPrivateAccess = "true"))
	bool bBlueprintVisible = false;

	/** WBP Designer 中的提示文本控件；存在时 RenderPrompt 会直接写入当前靠近对象提示。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PromptTextBlock;
};
