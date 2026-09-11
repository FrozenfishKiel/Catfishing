#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CatDayTransitionWidget.generated.h"

class UBorder;
class UTextBlock;

/** 翻天的正式 UMG 视图基类；只消费公开快照绘制遮罩和结算文本，不推进 Run、结算或拥有操作锁。 */
UCLASS(Blueprintable)
class CATFISHING_API UCatDayTransitionWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	/** 本地 UI 在每帧消费服务器翻天时间轴；只更新正式 WBP 的遮罩、标题、结算摘要和输入焦点，不改变权威结算或锁状态。 */
	void RenderTransition(float BlackOpacity, const FText& Message, bool bBlockPointer, const FText& SettlementDetails = FText::GetEmpty());

protected:
	/** 翻天视图持有焦点时吞掉按键，阻止 Enter、Space 或导航键触发底层模态页面。 */
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	/** 锁定期吞掉指针按下，避免未处理的遮罩事件落到游戏视口；失败提示不阻断。 */
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	/** 锁定期吞掉指针松开，与按下成对阻断底层点击。 */
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	/** 锁定期吞掉滚轮，防止黑幕后页面或游戏处理滚动输入。 */
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	/** 接管前本用户的焦点；RenderTransition 获取焦点时记录弱引用，解除阻断且自己仍持焦点时尝试归还，原目标失效或恢复失败则回到本用户视口。 */
	TWeakPtr<SWidget> PreviousUserFocus;
	/** 正式 WBP 的全屏黑色遮罩，表示翻天时间轴的视觉遮挡；编辑器资产创建并绑定，RenderTransition 只写其不透明度。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UBorder> BlackOverlay;
	/** 正式 WBP 的单行阶段标题，表示服务器公开的翻天或失败消息；编辑器资产绑定，RenderTransition 每帧写入当前文本与淡入淡出透明度。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ResultText;
	/** 正式 WBP 的结算摘要文本，表示主 UI 组装出的中文多行结果；编辑器资产绑定，RenderTransition 只展示传入内容而不计算结算。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> SettlementText;
};
