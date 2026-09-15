#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatRunContracts.h"
#include "Blueprint/UserWidget.h"
#include "CatAltarConfirmationWidget.generated.h"

class UBorder;
class UTextBlock;

/** 祭坛确认的正式 UMG 视图基类；只把 GameState 的公开确认快照投影为顶部提示，不持有确认名单或改变服务器裁决。 */
UCLASS(Blueprintable)
class CATFISHING_API UCatAltarConfirmationWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	/** 写入当前公开祭坛确认快照；显示发起者、人数、本人状态和取消原因，并以本机获取的服务器时间刷新倒计时。 */
	void RenderConfirmation(const FCatAltarConfirmationSnapshot& Confirmation);

protected:
	/** 窗口可见时只用 GameState 同步服务器时间刷新剩余秒数和 0.2 秒滑入表现，不发送网络请求或改写确认结果。 */
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	/** 将当前 Deadline 与服务器时间差格式化为显示秒数；等待状态以外清空倒计时，避免取消原因停留期显示过期数字。 */
	void RefreshCountdownText(double ServerTimeSeconds);
	/** 应用首次展示的 0.2 秒顶部滑入；只写确认面板的渲染位移，不抢焦点、不改输入模式。 */
	void RefreshSlideInPresentation();

	/** 正式 WBP 顶部确认面板；Render 写入短暂滑入位移，资产负责其位置、大小、颜色和圆角。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UBorder> ConfirmationPanel;
	/** 正式 WBP 的发起者文本；表示本轮公开快照里的 Initiator PlayerState，不从本地玩家猜测。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> InitiatorTextBlock;
	/** 正式 WBP 的倒计时文本；表示服务器截止时间减去 GameState 同步服务器时间得到的剩余秒数。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CountdownTextBlock;
	/** 正式 WBP 的确认人数文本；由公开 Participants 数组即时统计，Widget 不维护独立计数器。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ConfirmationCountTextBlock;
	/** 正式 WBP 的本人确认文本；从 owning Controller 的 PlayerState 在公开 Participants 中找到状态，提示 F8/F9 的下一步。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> OwnConfirmationTextBlock;
	/** 同一正式 WBP 的操作提示；渲染时按服务器发起者身份写入仅取消或确认/撤回，结束后隐藏，不负责裁决权限。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> InputHintsTextBlock;
	/** 正式 WBP 的取消原因文本；只在服务器发布 Cancelled 时显示，本机两秒停留由 UI Subsystem 管理。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CancelReasonTextBlock;

	/** 当前显示请求的关联键；新 Waiting 请求到达时重置滑入起点，取消或接受不会把它当作权威状态。 */
	FGuid DisplayRequestId;
	/** 当前等待窗口的服务器截止时间，单位秒；仅供本机文字倒计时使用，下一份公开快照会整体覆盖。 */
	double DisplayDeadlineServerTimeSeconds = 0.0;
	/** 当前视图是否正在显示服务器 Waiting 状态；Render 写入，Tick 只据此更新文字，不推断超时或改变 Widget 存活。 */
	bool bDisplayingWaitingConfirmation = false;
	/** 最近一次写入文本的整数剩余秒数；防止 Tick 在同一秒重复设置 TextBlock，不代表服务器剩余时长。 */
	int32 LastDisplayedRemainingSeconds = INDEX_NONE;
	/** 本机单调时间中的滑入起点；只影响 0.2 秒视觉插值，不与服务器截止时间或确认资格耦合。 */
	double SlideInStartedAtSeconds = 0.0;
};
