#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Condition/CatConditionTypes.h"
#include "Fishing/Integration/CatFishingCommandTypes.h"
#include "Growth/CatGrowthTypes.h"
#include "UI/CatFishingViewTypes.h"
#include "CatHUDWidget.generated.h"

class UButton;
class UProgressBar;
class UTextBlock;

/** HUD 入口按钮向协调层提交的纯界面意图；Widget 不直接创建菜单、图鉴或背包页面。 */
UENUM(BlueprintType)
enum class ECatHUDAction : uint8
{
	/** 打开局内主页或 ESC 菜单入口；正式页面由蓝图或上层控制器决定。 */
	OpenMainMenu,
	/** 打开鱼图鉴入口；图鉴记录和筛选逻辑仍归 Collection/Profile 链路。 */
	OpenCollection,
	/** 打开随身背包入口；当前原生协调层会把它转交给 Inventory PageController。 */
	OpenInventory
};

/** HUD View 向 LocalPlayer UI 协调层广播的纯入口意图；接收方决定具体页面和输入模式。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatHUDActionRequested, ECatHUDAction);

/** 状态 HUD 的只读显示投影；它聚合主界面常驻入口、猫状态、钓鱼反馈和短提示，不持有任何玩法写口。 */
USTRUCT(BlueprintType)
struct FCatHUDViewState
{
	GENERATED_BODY()

	/** 当前 Lake Run 的天序号；来源是 GameState 公开快照，HUD 只显示成“第 N 天”。 */
	UPROPERTY(BlueprintReadOnly)
	int32 DayIndex = 1;

	/** 给顶部天数入口直接显示的中文文本；点击行为仍走 HUD Action，不由文本本身承载。 */
	UPROPERTY(BlueprintReadOnly)
	FText DayText;

	/** 当前猫中毒值；来源是 Character ASC，HUD 只展示，不据此裁决倒地。 */
	UPROPERTY(BlueprintReadOnly)
	float Poison = 0.0f;

	/** 当前钓鱼力量；来源是 Character ASC，HUD 只展示，不作为 Fishing 命令参数。 */
	UPROPERTY(BlueprintReadOnly)
	float FishingStrength = 0.0f;

	/** 当前搏斗体力；来源是 Character ASC，HUD 只展示短周期资源。 */
	UPROPERTY(BlueprintReadOnly)
	float FightStamina = 0.0f;

	/** 当前猫体力条的上限基线；来源是猫种类配置或全局 Ability 设置，无法解析时保持 0。 */
	UPROPERTY(BlueprintReadOnly)
	float FightStaminaMaximum = 0.0f;

	/** 当前玩家体力条比例；由 FightStamina / FightStaminaMaximum 夹到 [0,1]，供 ProgressBar 直接绑定。 */
	UPROPERTY(BlueprintReadOnly)
	float NormalizedFightStamina = 0.0f;

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

	/** 当前是否显示主页菜单入口；布局可用它隐藏天数/菜单按钮而不改玩法状态。 */
	UPROPERTY(BlueprintReadOnly)
	bool bMainMenuEntryVisible = true;

	/** 当前是否显示鱼图鉴入口；布局可用它隐藏猫爪图标而不改 Collection 记录。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCollectionEntryVisible = true;

	/** 当前是否显示背包入口；布局可用它隐藏旅行包按钮而不改变背包控制器。 */
	UPROPERTY(BlueprintReadOnly)
	bool bInventoryEntryVisible = true;

	/** 主页菜单入口是否可点击；禁用只影响 UI 按钮，不代表 Run 或暂停状态改变。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCanOpenMainMenu = true;

	/** 鱼图鉴入口是否可点击；禁用只影响 UI 按钮，Collection 数据仍按正式链路更新。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCanOpenCollection = true;

	/** 背包入口是否可点击；禁用只影响 UI 按钮，背包打开仍由 Inventory PageController 裁决。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCanOpenInventory = true;

	/** 当前是否显示钓鱼阶段和浮漂反馈；无会话时 HUD 可以保持极简常驻界面。 */
	UPROPERTY(BlueprintReadOnly)
	bool bShowFishingState = false;

	/** 当前是否显示玩家体力和鱼状态条；只在遛鱼相关阶段为 true。 */
	UPROPERTY(BlueprintReadOnly)
	bool bShowFightMeters = false;

	/** 当前是否显示真咬钩提竿提示；来源是 FishingSession 公开阶段，不由 HUD 自行判定鱼漂。 */
	UPROPERTY(BlueprintReadOnly)
	bool bShowBitePrompt = false;

	/** 当前是否显示提竿窗口倒计时；进度只来自复制快照的服务器时间窗口。 */
	UPROPERTY(BlueprintReadOnly)
	bool bShowHookCountdown = false;

	/** 当前是否显示提竿成功反馈；短暂动画由 WBP 表现层消费该标记实现。 */
	UPROPERTY(BlueprintReadOnly)
	bool bShowHookSuccessFeedback = false;

	/** 鱼侧体力条比例；直接来自 Fishing ViewState 的归一化鱼体力，供 ProgressBar 绑定。 */
	UPROPERTY(BlueprintReadOnly)
	float NormalizedFishStamina = 0.0f;

	/** 提竿窗口剩余比例；1 表示窗口刚开始，0 表示已经到达服务器截止点。 */
	UPROPERTY(BlueprintReadOnly)
	float HookCountdownPercent = 0.0f;

	/** 鱼线受力展示比例；来源是 Fishing ViewState，供 WBP 选择是否显示压力条或颜色。 */
	UPROPERTY(BlueprintReadOnly)
	float LineLoadPercent = 0.0f;

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

	/** 给咬钩提示控件显示的短文本；默认文案不绑定具体按键，按键图标可由 WBP 覆盖。 */
	UPROPERTY(BlueprintReadOnly)
	FText BitePromptText;

	/** 给提竿倒计时控件显示的短文本；数值单位为秒，只作为提示。 */
	UPROPERTY(BlueprintReadOnly)
	FText HookCountdownText;

	/** 给提竿成功反馈控件显示的短文本；WBP 可以用它触发一次性出现动画。 */
	UPROPERTY(BlueprintReadOnly)
	FText HookSuccessFeedbackText;

	/** 给钓鱼状态控件显示的阶段摘要；它来自 Fishing Phase，不替代 FishingSession 状态机。 */
	UPROPERTY(BlueprintReadOnly)
	FText FishingStateText;

	/** 给鱼漂反馈控件显示的动作摘要；它把阶段转换为玩家可读的浮漂表现提示。 */
	UPROPERTY(BlueprintReadOnly)
	FText BobberFeedbackText;

	/** 给鱼状态控件显示的挣扎/疲劳摘要；它来自鱼运动意图和强对抗标记。 */
	UPROPERTY(BlueprintReadOnly)
	FText FishStateText;

	/** 给玩家体力条旁显示的短文本；数值来自 ASC 当前体力和配置基线。 */
	UPROPERTY(BlueprintReadOnly)
	FText CatStaminaText;

	/** 给鱼体力条旁显示的短文本；比例来自 FishingSession 已归一化的鱼体力。 */
	UPROPERTY(BlueprintReadOnly)
	FText FishStaminaText;
};

/** 状态 HUD 的 WBP 基类；它渲染常驻主界面、钓鱼反馈和入口意图，不直接修改玩法状态。 */
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

	/** 提交主页菜单入口意图；具体打开 ESC 菜单、主页页签或暂停层由订阅者和蓝图决定。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|HUD")
	void RequestOpenMainMenu();

	/** 提交鱼图鉴入口意图；HUD 不读取 Profile 图鉴，也不自行创建 Collection 页面。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|HUD")
	void RequestOpenCollection();

	/** 提交背包入口意图；原生协调层会转交现有 Inventory PageController。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|HUD")
	void RequestOpenInventory();

	/** 用户点击 HUD 入口后的原生广播；LocalPlayer UI 子系统和外部控制器只接收意图。 */
	FCatHUDActionRequested OnActionRequested;

protected:
	/** Slate 构造完成后对可选入口按钮去重绑定；没有对应按钮的 WBP 仍保持可用。 */
	virtual void NativeConstruct() override;

	/** 离开视口时解除可选入口按钮绑定，避免重建 Slate 后重复广播同一点击。 */
	virtual void NativeDestruct() override;

	/** 每帧只刷新本地倒计时表现；提竿窗口裁决仍以服务器 FishingSession 命令结果为准。 */
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** WBP 可选渲染扩展点；Designer 也可以直接绑定 BlueprintCatStatusText 和 BlueprintFishingFeedbackText。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|HUD")
	void BP_RenderHUD(const FCatHUDViewState& ViewState);

	/** WBP 可选入口扩展点；用于接主页菜单或图鉴页面，原生 HUD 不替蓝图决定页面栈。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|HUD")
	void BP_HandleHUDAction(ECatHUDAction Action);

private:
	/** 统一广播 HUD 入口意图；先通知原生协调层，再给蓝图表现层处理页面或动画。 */
	void SubmitHUDAction(ECatHUDAction Action);

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

	/** WBP Designer 中的天数文本控件；存在时 RenderHUD 会写入“第 N 天”。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DayTextBlock;

	/** WBP Designer 中的真咬钩提示控件；存在时只在 TrueBiteWindow 阶段显示。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BitePromptTextBlock;

	/** WBP Designer 中的提竿倒计时文本控件；存在时跟随服务器窗口剩余时间刷新。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HookCountdownTextBlock;

	/** WBP Designer 中的提竿成功反馈控件；存在时由最近成功提竿命令驱动显示。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HookSuccessFeedbackTextBlock;

	/** WBP Designer 中的钓鱼阶段文本控件；存在时显示等待、试探、搏斗或终态摘要。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FishingStateTextBlock;

	/** WBP Designer 中的鱼漂反馈文本控件；存在时显示浮漂安静、晃动或下沉等提示。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BobberFeedbackTextBlock;

	/** WBP Designer 中的鱼状态文本控件；存在时显示挣扎、疲劳、强对抗或近岸等状态。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FishStateTextBlock;

	/** WBP Designer 中的玩家体力文本控件；存在时显示当前体力和可解析上限。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CatStaminaTextBlock;

	/** WBP Designer 中的鱼体力文本控件；存在时显示鱼体力剩余百分比。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FishStaminaTextBlock;

	/** WBP Designer 中的主页/天数入口按钮；点击时只广播 OpenMainMenu 意图。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> MainMenuButton;

	/** WBP Designer 中的猫爪图鉴入口按钮；点击时只广播 OpenCollection 意图。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> CollectionButton;

	/** WBP Designer 中的旅行包入口按钮；点击时只广播 OpenInventory 意图。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> InventoryButton;

	/** WBP Designer 中的玩家体力条；存在时直接绑定 NormalizedFightStamina。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> CatStaminaProgressBar;

	/** WBP Designer 中的鱼体力条；存在时直接绑定 NormalizedFishStamina。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> FishStaminaProgressBar;

	/** WBP Designer 中的提竿倒计时条；存在时直接绑定 HookCountdownPercent。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> HookCountdownProgressBar;
};
