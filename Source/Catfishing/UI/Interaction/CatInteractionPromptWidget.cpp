#include "UI/Interaction/CatInteractionPromptWidget.h"

#include "Components/TextBlock.h"

// 提示渲染流程：缓存只读投影，同步 Designer 绑定字段并触发蓝图扩展点；不解析或执行交互对象。
void UCatInteractionPromptWidget::RenderPrompt(const FCatInteractionPromptViewState& ViewState)
{
	LastPromptViewState = ViewState;
	BlueprintPromptText = ViewState.PromptText;
	bBlueprintVisible = ViewState.bVisible;
	if (PromptTextBlock)
	{
		PromptTextBlock->SetText(BlueprintPromptText);
	}
	BP_RenderPrompt(LastPromptViewState);
}

// 状态读取流程：返回最近提示投影；调用者只能展示，不获得交互对象生命周期。
const FCatInteractionPromptViewState& UCatInteractionPromptWidget::GetLastPromptViewState() const
{
	return LastPromptViewState;
}
