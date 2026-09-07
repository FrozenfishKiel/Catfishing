#include "UI/HUD/CatHUDWidget.h"

#include "Components/Button.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Logging/CatLog.h"
#include "Rendering/DrawElementTypes.h"

// HUD 渲染流程：缓存 Model 生成的只读投影，按 Designer 真实绑定控件写入天数、调试文本、钓鱼反馈、入口按钮状态和进度条，再触发蓝图扩展点。
void UCatHUDWidget::RenderHUD(const FCatHUDViewState& ViewState)
{
	if (!bHasLoggedCrosshairVisibility || LastHUDViewState.bShowCrosshair != ViewState.bShowCrosshair)
	{
		const APlayerController* Controller = GetOwningPlayer();
		UE_LOG(LogCatUI, Log,
			TEXT("Event=ui_hud_crosshair_visibility World=%s NetMode=%d Controller=%s LocalRole=%d Widget=%s Visible=%d Result=ViewStateApplied"),
			*GetNameSafe(GetWorld()), GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : -1,
			*GetNameSafe(Controller), Controller ? static_cast<int32>(Controller->GetLocalRole()) : -1,
			*GetName(), ViewState.bShowCrosshair);
		bHasLoggedCrosshairVisibility = true;
	}
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
		CatStatusTextBlock->SetVisibility(ViewState.bShowCatStatusDebugText
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (FishingFeedbackTextBlock)
	{
		FishingFeedbackTextBlock->SetText(BlueprintFishingFeedbackText);
		FishingFeedbackTextBlock->SetVisibility(ViewState.bShowFishingFeedbackDebugText
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
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

// 构造流程：让父类完成 Slate 构建后，对两个主 HUD 入口按钮执行 Remove/Add 配对，保证重建时不会重复广播。
void UCatHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (MainMenuButton)
	{
		MainMenuButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestOpenMainMenu);
		MainMenuButton->OnClicked.AddDynamic(this, &ThisClass::RequestOpenMainMenu);
	}
	if (InventoryButton)
	{
		InventoryButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestOpenInventory);
		InventoryButton->OnClicked.AddDynamic(this, &ThisClass::RequestOpenInventory);
	}
}

// 销毁流程：解除两个主 HUD 入口按钮对本对象的动态绑定，再交还父类 Slate 生命周期；业务广播不保存 World 引用。
void UCatHUDWidget::NativeDestruct()
{
	if (MainMenuButton)
	{
		MainMenuButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestOpenMainMenu);
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

// 准星绘制流程：先让 WBP 和子控件完成绘制；只有 ViewState 明确要求时，才在最终层用本 HUD 的局部中心画四条灰色短线。
// 本 Widget 只会由 LocalPlayer UI 子系统为本地 Controller 创建，不读取 NetMode 或 HasAuthority，远端客户端不会依赖服务器生成 UI。
int32 UCatHUDWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, const int32 LayerId,
	const FWidgetStyle& InWidgetStyle, const bool bParentEnabled) const
{
	const int32 MaxLayer = Super::NativePaint(
		Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	if (!LastHUDViewState.bShowCrosshair
		|| LocalSize.X <= 0.0f || LocalSize.Y <= 0.0f
		|| CrosshairArmLength <= 0.0f || CrosshairThickness <= 0.0f)
	{
		return MaxLayer;
	}

	const FVector2D Center = LocalSize * 0.5f;
	const float Inner = FMath::Max(0.0f, CrosshairGap);
	const float Outer = Inner + CrosshairArmLength;
	const uint32 CrosshairLayer = static_cast<uint32>(MaxLayer + 1);
	const FPaintGeometry PaintGeometry = AllottedGeometry.ToPaintGeometry();

	auto DrawArm = [&](const FVector2D& Start, const FVector2D& End)
	{
		TArray<FVector2D> Points;
		Points.Reserve(2);
		Points.Add(Start);
		Points.Add(End);
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			CrosshairLayer,
			PaintGeometry,
			Points,
			ESlateDrawEffect::None,
			CrosshairColor,
			true,
			CrosshairThickness);
	};

	DrawArm(Center + FVector2D(-Outer, 0.0f), Center + FVector2D(-Inner, 0.0f));
	DrawArm(Center + FVector2D(Inner, 0.0f), Center + FVector2D(Outer, 0.0f));
	DrawArm(Center + FVector2D(0.0f, -Outer), Center + FVector2D(0.0f, -Inner));
	DrawArm(Center + FVector2D(0.0f, Inner), Center + FVector2D(0.0f, Outer));
	return static_cast<int32>(CrosshairLayer);
}

// 主页菜单入口流程：把点击转换为纯 UI 意图；HUD 不创建或持有菜单页面。
void UCatHUDWidget::RequestOpenMainMenu()
{
	SubmitHUDAction(ECatHUDAction::OpenMainMenu);
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
