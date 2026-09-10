#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CatDayTransitionWidget.generated.h"

class SBorder;
class STextBlock;

/** 翻天的原生全屏表现；只画遮罩和结果文字，允许普通派生，不推进 Run 或拥有操作锁。 */
UCLASS(Blueprintable)
class CATFISHING_API UCatDayTransitionWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	/** 本地 UI 每帧提交服务器时间轴的投影；分别更新黑色透明度、文字和鼠标阻断。 */
	void RenderTransition(float BlackOpacity, const FText& Message, bool bBlockPointer);
	/** UMG 拆卸时释放原生控件引用，防止旅行后继续持有旧 Slate 树。 */
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
	/** 构建铺满所属玩家视口的黑底与居中文字，作为此功能的真实原生 UI。 */
	virtual TSharedRef<SWidget> RebuildWidget() override;
	/** 翻天视图持有焦点时吞掉按键，阻止 Enter、Space 或导航键触发底层模态页面。 */
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	/** 全屏黑底控件；重建时创建，渲染入口写透明度，释放时清空，不参与玩法状态。 */
	TSharedPtr<SBorder> BlackOverlay;
	/** 服务器结果的居中文字控件；重建时创建，渲染入口写文案，释放时清空。 */
	TSharedPtr<STextBlock> ResultText;
	/** 本视图接管前的 Slate 焦点；首次获焦写入，解除阻断时归还，弱引用不延长旧页面生命周期。 */
	TWeakPtr<SWidget> PreviousUserFocus;
};
