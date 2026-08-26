#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Condition/CatConditionTypes.h"
#include "Fishing/Integration/CatFishingCommandTypes.h"
#include "Growth/CatGrowthTypes.h"
#include "UI/CatFishingViewTypes.h"
#include "CatHUDWidget.generated.h"

class UTextBlock;

/** 状态 HUD 的只读显示投影；它只包含猫状态、钓鱼反馈和短提示，不包含背包或商店操作。 */
USTRUCT(BlueprintType)
struct FCatHUDViewState
{
	GENERATED_BODY()

	/** 当前猫中毒值；来源是 Character ASC，HUD 只展示，不据此裁决倒地。 */
	UPROPERTY(BlueprintReadOnly)
	float Poison = 0.0f;

	/** 当前钓鱼力量；来源是 Character ASC，HUD 只展示，不作为 Fishing 命令参数。 */
	UPROPERTY(BlueprintReadOnly)
	float FishingStrength = 0.0f;

	/** 当前搏斗体力；来源是 Character ASC，HUD 只展示短周期资源。 */
	UPROPERTY(BlueprintReadOnly)
	float FightStamina = 0.0f;

	/** Character 离散状态快照；Wet、Downed 和恢复仍由 Condition 模块拥有。 */
	UPROPERTY(BlueprintReadOnly)
	FCatConditionSnapshot Condition;

	/** Character 成长快照；经验槽和待选次数仍由 Growth 组件拥有，HUD 只展示当前事实。 */
	UPROPERTY(BlueprintReadOnly)
	FCatGrowthSnapshot Growth;

	/** 当前玩家钓鱼会话投影；只有 bHasFishingSession 为 true 时代表有效反馈。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishingViewState Fishing;

	/** 当前是否存在属于本玩家的钓鱼会话投影。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasFishingSession = false;

	/** 最近一条钓鱼命令终态；HUD 只用它提示成功或错误。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishingCommandResult LastFishingCommandResult;

	/** 最近是否收到过可展示的钓鱼命令终态。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasFishingCommandResult = false;

	/** 给 TextBlock 直接绑定的猫状态中文摘要。 */
	UPROPERTY(BlueprintReadOnly)
	FText CatStatusText;

	/** 给 TextBlock 直接绑定的钓鱼反馈中文摘要。 */
	UPROPERTY(BlueprintReadOnly)
	FText FishingFeedbackText;
};

/** 状态 HUD 的 WBP 基类；它只渲染状态和反馈，不提供背包、商店或图鉴按钮。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 接收 HUD Model 的只读投影并同步蓝图绑定字段；具体布局和表现交给 WBP。 */
	void RenderHUD(const FCatHUDViewState& ViewState);

	/** 暴露最近一次 HUD 投影给蓝图表现；它不持有 Character 或 ASC，因此不能被蓝图拿去改状态。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|HUD")
	const FCatHUDViewState& GetLastHUDViewState() const;

protected:
	/** WBP 可选渲染扩展点；Designer 也可以直接绑定 BlueprintCatStatusText 和 BlueprintFishingFeedbackText。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|HUD")
	void BP_RenderHUD(const FCatHUDViewState& ViewState);

private:
	/** 最近一次 Model 输入的 HUD 投影；本对象不持有 Character、ASC 或 FishingSession。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|HUD", meta = (AllowPrivateAccess = "true"))
	FCatHUDViewState LastHUDViewState;

	/** 给 WBP TextBlock 直接绑定的猫状态文本。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|HUD", meta = (AllowPrivateAccess = "true"))
	FText BlueprintCatStatusText;

	/** 给 WBP TextBlock 直接绑定的钓鱼反馈文本。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|HUD", meta = (AllowPrivateAccess = "true"))
	FText BlueprintFishingFeedbackText;

	/** WBP Designer 中的猫状态文本控件；存在时 RenderHUD 会直接写入当前中文摘要。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CatStatusTextBlock;

	/** WBP Designer 中的钓鱼反馈文本控件；存在时 RenderHUD 会直接写入当前中文反馈。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FishingFeedbackTextBlock;
};
