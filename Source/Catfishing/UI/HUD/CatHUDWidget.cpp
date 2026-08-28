#include "UI/HUD/CatHUDWidget.h"

#include "Components/Button.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"

// HUD 渲染流程：缓存只读投影，复制 Designer 绑定文本和进度条，再触发蓝图扩展点；不访问任何玩法对象或写口。
void UCatHUDWidget::RenderHUD(const FCatHUDViewState& ViewState)
{
	LastHUDViewState = ViewState;
	BlueprintCatStatusText = ViewState.CatStatusText;
	BlueprintFishingFeedbackText = ViewState.FishingFeedbackText;
	if (DayTextBlock)
	{
		DayTextBlock->SetText(ViewState.DayText);
	}
	if (CatStatusTextBlock)
	{
		CatStatusTextBlock->SetText(BlueprintCatStatusText);
	}
	if (FishingFeedbackTextBlock)
	{
		FishingFeedbackTextBlock->SetText(BlueprintFishingFeedbackText);
	}
	if (BitePromptTextBlock)
	{
		BitePromptTextBlock->SetText(ViewState.BitePromptText);
		BitePromptTextBlock->SetVisibility(ViewState.bShowBitePrompt
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (HookCountdownTextBlock)
	{
		HookCountdownTextBlock->SetText(ViewState.HookCountdownText);
		HookCountdownTextBlock->SetVisibility(ViewState.bShowHookCountdown
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (HookSuccessFeedbackTextBlock)
	{
		HookSuccessFeedbackTextBlock->SetText(ViewState.HookSuccessFeedbackText);
		HookSuccessFeedbackTextBlock->SetVisibility(ViewState.bShowHookSuccessFeedback
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (FishingStateTextBlock)
	{
		FishingStateTextBlock->SetText(ViewState.FishingStateText);
		FishingStateTextBlock->SetVisibility(ViewState.bShowFishingState
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (BobberFeedbackTextBlock)
	{
		BobberFeedbackTextBlock->SetText(ViewState.BobberFeedbackText);
		BobberFeedbackTextBlock->SetVisibility(ViewState.bShowFishingState
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (FishStateTextBlock)
	{
		FishStateTextBlock->SetText(ViewState.FishStateText);
		FishStateTextBlock->SetVisibility(ViewState.bShowFightMeters
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (CatStaminaTextBlock)
	{
		CatStaminaTextBlock->SetText(ViewState.CatStaminaText);
		CatStaminaTextBlock->SetVisibility(ViewState.bShowFightMeters
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (FishStaminaTextBlock)
	{
		FishStaminaTextBlock->SetText(ViewState.FishStaminaText);
		FishStaminaTextBlock->SetVisibility(ViewState.bShowFightMeters
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (MainMenuButton)
	{
		MainMenuButton->SetIsEnabled(ViewState.bCanOpenMainMenu);
		MainMenuButton->SetVisibility(ViewState.bMainMenuEntryVisible
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (CollectionButton)
	{
		CollectionButton->SetIsEnabled(ViewState.bCanOpenCollection);
		CollectionButton->SetVisibility(ViewState.bCollectionEntryVisible
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (InventoryButton)
	{
		InventoryButton->SetIsEnabled(ViewState.bCanOpenInventory);
		InventoryButton->SetVisibility(ViewState.bInventoryEntryVisible
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (CatStaminaProgressBar)
	{
		CatStaminaProgressBar->SetPercent(ViewState.NormalizedFightStamina);
		CatStaminaProgressBar->SetVisibility(ViewState.bShowFightMeters
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (FishStaminaProgressBar)
	{
		FishStaminaProgressBar->SetPercent(ViewState.NormalizedFishStamina);
		FishStaminaProgressBar->SetVisibility(ViewState.bShowFightMeters
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (HookCountdownProgressBar)
	{
		HookCountdownProgressBar->SetPercent(ViewState.HookCountdownPercent);
		HookCountdownProgressBar->SetVisibility(ViewState.bShowHookCountdown
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	BP_RenderHUD(LastHUDViewState);
}

// 状态读取流程：返回最近 HUD 投影；调用者只能展示，不获得后端订阅或写入口。
const FCatHUDViewState& UCatHUDWidget::GetLastHUDViewState() const
{
	return LastHUDViewState;
}

// 构造流程：让父类完成 Slate 构建后，对三个可选入口按钮执行 Remove/Add 配对，保证重建时不会重复广播。
void UCatHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (MainMenuButton)
	{
		MainMenuButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestOpenMainMenu);
		MainMenuButton->OnClicked.AddDynamic(this, &ThisClass::RequestOpenMainMenu);
	}
	if (CollectionButton)
	{
		CollectionButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestOpenCollection);
		CollectionButton->OnClicked.AddDynamic(this, &ThisClass::RequestOpenCollection);
	}
	if (InventoryButton)
	{
		InventoryButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestOpenInventory);
		InventoryButton->OnClicked.AddDynamic(this, &ThisClass::RequestOpenInventory);
	}
}

// 销毁流程：解除三个可选入口按钮对本对象的动态绑定，再交还父类 Slate 生命周期；业务广播不保存 World 引用。
void UCatHUDWidget::NativeDestruct()
{
	if (MainMenuButton)
	{
		MainMenuButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestOpenMainMenu);
	}
	if (CollectionButton)
	{
		CollectionButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestOpenCollection);
	}
	if (InventoryButton)
	{
		InventoryButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestOpenInventory);
	}
	Super::NativeDestruct();
}

// Tick 流程：只在真咬钩窗口期间用服务器时间锚点刷新倒计时控件；窗口过期时本地收起提示，正式失败仍等命令/会话事实。
void UCatHUDWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!LastHUDViewState.bShowHookCountdown)
	{
		return;
	}
	UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	const double ServerNowSeconds = GameState ? GameState->GetServerWorldTimeSeconds()
		: (World ? World->GetTimeSeconds() : 0.0);
	const double WindowDuration = FMath::Max(
		LastHUDViewState.Fishing.WindowEndsServerTime - LastHUDViewState.Fishing.PhaseStartedServerTime, 0.01);
	const double RemainingSeconds = FMath::Max(
		LastHUDViewState.Fishing.WindowEndsServerTime - ServerNowSeconds, 0.0);
	const float CountdownPercent = FMath::Clamp(
		static_cast<float>(RemainingSeconds / WindowDuration), 0.0f, 1.0f);
	const ESlateVisibility CountdownVisibility = RemainingSeconds > 0.0
		? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
	if (HookCountdownProgressBar)
	{
		HookCountdownProgressBar->SetPercent(CountdownPercent);
		HookCountdownProgressBar->SetVisibility(CountdownVisibility);
	}
	if (HookCountdownTextBlock)
	{
		HookCountdownTextBlock->SetText(FText::FromString(FString::Printf(
			TEXT("提竿倒计时 %.1f 秒"), RemainingSeconds)));
		HookCountdownTextBlock->SetVisibility(CountdownVisibility);
	}
	if (BitePromptTextBlock)
	{
		BitePromptTextBlock->SetVisibility(CountdownVisibility);
	}
}

// 主页菜单入口流程：把点击转换为纯 UI 意图；HUD 不创建或持有菜单页面。
void UCatHUDWidget::RequestOpenMainMenu()
{
	SubmitHUDAction(ECatHUDAction::OpenMainMenu);
}

// 鱼图鉴入口流程：把点击转换为纯 UI 意图；图鉴内容和解锁事实仍在 Collection/Profile 链路。
void UCatHUDWidget::RequestOpenCollection()
{
	SubmitHUDAction(ECatHUDAction::OpenCollection);
}

// 背包入口流程：把点击转换为纯 UI 意图；实际开关背包由 LocalPlayer UI 协调层转交库存控制器。
void UCatHUDWidget::RequestOpenInventory()
{
	SubmitHUDAction(ECatHUDAction::OpenInventory);
}

// 意图提交流程：先广播给原生协调层处理已有页面，再通知蓝图扩展点处理未接原生控制器的页面或动画。
void UCatHUDWidget::SubmitHUDAction(const ECatHUDAction Action)
{
	OnActionRequested.Broadcast(Action);
	BP_HandleHUDAction(Action);
}
