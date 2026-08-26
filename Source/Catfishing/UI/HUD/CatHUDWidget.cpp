#include "UI/HUD/CatHUDWidget.h"

#include "Components/TextBlock.h"

// HUD 渲染流程：缓存只读投影，复制 Designer 绑定文本，然后触发蓝图扩展点；不访问任何玩法对象或按钮逻辑。
void UCatHUDWidget::RenderHUD(const FCatHUDViewState& ViewState)
{
	LastHUDViewState = ViewState;
	BlueprintCatStatusText = ViewState.CatStatusText;
	BlueprintFishingFeedbackText = ViewState.FishingFeedbackText;
	if (CatStatusTextBlock)
	{
		CatStatusTextBlock->SetText(BlueprintCatStatusText);
	}
	if (FishingFeedbackTextBlock)
	{
		FishingFeedbackTextBlock->SetText(BlueprintFishingFeedbackText);
	}
	BP_RenderHUD(LastHUDViewState);
}

// 状态读取流程：返回最近 HUD 投影；调用者只能展示，不获得后端订阅或写入口。
const FCatHUDViewState& UCatHUDWidget::GetLastHUDViewState() const
{
	return LastHUDViewState;
}
