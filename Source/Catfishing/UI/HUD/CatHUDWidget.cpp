#include "UI/HUD/CatHUDWidget.h"
#include "Styling/CoreStyle.h"
#include "Fishing/Integration/CatFishingAimLibrary.h"

#include "Components/Button.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Logging/CatLog.h"
#include "Rendering/DrawElementTypes.h"

// 新鱼种播报流程：只写文字并开始本地计时；控件缺失时记一条诊断即可，别人解锁这件事本来就不阻塞任何人。
// 它刻意不走 ViewState——ViewState 是「当前事实的投影」，而这是一次性事件，投影里存它会让它随下一次刷新复现。
void UCatHUDWidget::AnnounceFishSpeciesDiscovery(const FText& BroadcastText)
{
	if (!FishDiscoveryBroadcastTextBlock)
	{
		UE_LOG(LogCatUI, Warning,
			TEXT("Event=ui_hud_fish_discovery_slot_missing Widget=%s World=%s Text=\"%s\" Result=FormalWidgetNeedsMigration"),
			*GetName(), *GetNameSafe(GetWorld()), *BroadcastText.ToString());
		return;
	}
	FishDiscoveryBroadcastTextBlock->SetText(BroadcastText);
	FishDiscoveryBroadcastTextBlock->SetVisibility(ESlateVisibility::HitTestInvisible);
	FishDiscoveryBroadcastUntilSeconds = FPlatformTime::Seconds() + CatHUDFishDiscoveryBroadcastLimits::VisibleSeconds;
}

// HUD 渲染流程：缓存 Model 生成的只读投影，按 Designer 真实绑定控件写入天数、调试文本、钓鱼反馈、入口按钮状态和进度条，再触发蓝图扩展点。
void UCatHUDWidget::RenderHUD(const FCatHUDViewState& ViewState)
{
	if (ViewState.bShowPhysicalControls && (!PhysicalControlTextBlock || !PhysicalHandStateTextBlock)
		&& !bHasLoggedMissingPhysicalControls)
	{
		UE_LOG(LogCatUI, Warning,
			TEXT("Event=ui_hud_physical_controls_missing Widget=%s World=%s Result=FormalWidgetNeedsMigration"),
			*GetName(), *GetNameSafe(GetWorld()));
		bHasLoggedMissingPhysicalControls = true;
	}
	if (ViewState.bShowPhysicalControls != LastHUDViewState.bShowPhysicalControls
		|| ViewState.bPrimaryRodOperator != LastHUDViewState.bPrimaryRodOperator
		|| ViewState.bLeftHandGripped != LastHUDViewState.bLeftHandGripped
		|| ViewState.bRightHandGripped != LastHUDViewState.bRightHandGripped)
	{
		const APlayerController* Controller = GetOwningPlayer();
		UE_LOG(LogCatUI, Log,
			TEXT("Event=ui_hud_physical_controls_applied World=%s NetMode=%d Authority=%d LocalRole=%d PlayerId=%d Operator=%d LeftGrip=%d RightGrip=%d Result=ViewStateApplied"),
			*GetNameSafe(GetWorld()), GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : INDEX_NONE,
			Controller && Controller->HasAuthority(), Controller ? static_cast<int32>(Controller->GetLocalRole()) : INDEX_NONE,
			Controller && Controller->PlayerState ? Controller->PlayerState->GetPlayerId() : INDEX_NONE,
			ViewState.bPrimaryRodOperator, ViewState.bLeftHandGripped, ViewState.bRightHandGripped);
	}
	if (PhysicalControlTextBlock)
	{
		PhysicalControlTextBlock->SetText(ViewState.PhysicalControlText);
		PhysicalControlTextBlock->SetVisibility(ViewState.bShowPhysicalControls ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (PhysicalHandStateTextBlock)
	{
		PhysicalHandStateTextBlock->SetText(ViewState.PhysicalHandStateText);
		PhysicalHandStateTextBlock->SetVisibility(ViewState.bShowPhysicalControls ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if ((ViewState.bShowFightMeters || ViewState.bShowPersonalStamina) && (!CatStaminaTextBlock || !CatStaminaProgressBar)
		&& !bHasLoggedMissingFishingMeter)
	{
		const APlayerController* Controller = GetOwningPlayer();
		UE_LOG(LogCatUI, Warning,
			TEXT("Event=ui_hud_fishing_meter_missing World=%s NetMode=%d Authority=%d LocalRole=%d PlayerId=%d Widget=%s SessionId=%s TextBound=%d BarBound=%d Result=FormalWidgetNeedsMigration"),
			*GetNameSafe(GetWorld()), GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : INDEX_NONE,
			Controller && Controller->HasAuthority(), Controller ? static_cast<int32>(Controller->GetLocalRole()) : INDEX_NONE,
			Controller && Controller->PlayerState ? Controller->PlayerState->GetPlayerId() : INDEX_NONE,
			*GetName(), *ViewState.Fishing.FishingSessionId.ToString(), CatStaminaTextBlock != nullptr, CatStaminaProgressBar != nullptr);
		bHasLoggedMissingFishingMeter = true;
	}
	if (ViewState.bHasFishingSession && (!LastHUDViewState.bHasFishingSession
		|| LastHUDViewState.Fishing.FishingSessionId != ViewState.Fishing.FishingSessionId))
	{
		const APlayerController* Controller = GetOwningPlayer();
		UE_LOG(LogCatUI, Log,
			TEXT("Event=ui_hud_fishing_operator_applied World=%s NetMode=%d Authority=%d LocalRole=%d PlayerId=%d SessionId=%s FightStamina=%.3f FightStaminaMaximum=%.3f Result=ViewStateApplied"),
			*GetNameSafe(GetWorld()), GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : INDEX_NONE,
			Controller && Controller->HasAuthority(), Controller ? static_cast<int32>(Controller->GetLocalRole()) : INDEX_NONE,
			Controller && Controller->PlayerState ? Controller->PlayerState->GetPlayerId() : INDEX_NONE,
			*ViewState.Fishing.FishingSessionId.ToString(), ViewState.FightStamina, ViewState.FightStaminaMaximum);
	}
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
	BlueprintFishingFeedbackText = ViewState.FishingFeedbackText;
	if (DayTextBlock)
	{
		DayTextBlock->SetText(ViewState.DayText);
	}
	if (TimeOfDayTextBlock)
	{
		TimeOfDayTextBlock->SetText(ViewState.TimeOfDayText);
		TimeOfDayTextBlock->SetVisibility(ViewState.bShowTimeOfDay
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	// 献祭要区分的三个量（交互册 §42）：前两个白天常驻，世界进度平时隐藏、打开界面时才露面。
	if (DailyOfferingTargetTextBlock)
	{
		DailyOfferingTargetTextBlock->SetText(ViewState.DailyOfferingTargetText);
		DailyOfferingTargetTextBlock->SetVisibility(ViewState.bShowOfferingCounters
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (TankOfferableTextBlock)
	{
		TankOfferableTextBlock->SetText(ViewState.TankOfferableText);
		TankOfferableTextBlock->SetVisibility(ViewState.bShowOfferingCounters
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (WorldProgressTextBlock)
	{
		WorldProgressTextBlock->SetText(ViewState.WorldProgressText);
		WorldProgressTextBlock->SetVisibility(ViewState.bShowWorldProgress
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (WorldProgressBar)
	{
		WorldProgressBar->SetPercent(ViewState.NormalizedWorldProgress);
		WorldProgressBar->SetVisibility(ViewState.bShowWorldProgress && ViewState.bHasWorldProgress
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (ViewState.bShowRodDurability && !RodDurabilityTextBlock && !bHasLoggedMissingRodDurability)
	{
		// 竿耐久此前只在背包悬停框里出现（ui 与交互对表第 43 行判「量有、位置相反」）；正式 WBP 补齐控件之前先落一条可查诊断。
		UE_LOG(LogCatUI, Warning,
			TEXT("Event=ui_hud_rod_durability_slot_missing Widget=%s World=%s Result=FormalWidgetNeedsMigration"),
			*GetName(), *GetNameSafe(GetWorld()));
		bHasLoggedMissingRodDurability = true;
	}
	if (RodDurabilityTextBlock)
	{
		RodDurabilityTextBlock->SetText(ViewState.RodDurabilityText);
		RodDurabilityTextBlock->SetVisibility(ViewState.bShowRodDurability
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (RodDurabilityProgressBar)
	{
		RodDurabilityProgressBar->SetPercent(ViewState.NormalizedRodDurability);
		RodDurabilityProgressBar->SetVisibility(ViewState.bShowRodDurability && ViewState.bHasRodDurability
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (NearDeathTextBlock)
	{
		NearDeathTextBlock->SetText(ViewState.NearDeathText);
		NearDeathTextBlock->SetVisibility(ViewState.bNearDeath
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (ViewState.bShowTeammates && !bHasLoggedTeammatePanelData)
	{
		// 队友条是逐行布局，原生只给数据不建行；这条诊断证明数据已经到了 WBP 手上（多人钓鱼附篇 §4.3:129）。
		UE_LOG(LogCatUI, Log,
			TEXT("Event=ui_hud_teammate_panel_data_available Widget=%s World=%s Teammates=%d Result=ViewStateApplied"),
			*GetName(), *GetNameSafe(GetWorld()), ViewState.Teammates.Num());
		bHasLoggedTeammatePanelData = true;
	}
	if ((!TeamWalletTextBlock || !PurchaseBroadcastTextBlock) && !bHasLoggedMissingShopHUD)
	{
		// 公款是常驻位、购买是全场事件，两者都不该只在商店页里存在；正式 WBP 补齐控件之前先落一条可查诊断。
		UE_LOG(LogCatUI, Warning,
			TEXT("Event=ui_hud_shop_slots_missing Widget=%s World=%s WalletBound=%d BroadcastBound=%d Result=FormalWidgetNeedsMigration"),
			*GetName(), *GetNameSafe(GetWorld()),
			TeamWalletTextBlock != nullptr, PurchaseBroadcastTextBlock != nullptr);
		bHasLoggedMissingShopHUD = true;
	}
	if (TeamWalletTextBlock)
	{
		TeamWalletTextBlock->SetText(ViewState.TeamWalletText);
		TeamWalletTextBlock->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	if (PurchaseBroadcastTextBlock)
	{
		PurchaseBroadcastTextBlock->SetText(ViewState.PurchaseBroadcastText);
		PurchaseBroadcastTextBlock->SetVisibility(ViewState.bShowPurchaseBroadcast
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
		CatStaminaTextBlock->SetVisibility((ViewState.bShowFightMeters || ViewState.bShowPersonalStamina)
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
	if (CollectionButton)
	{
		CollectionButton->SetIsEnabled(ViewState.bCanOpenCollection);
		CollectionButton->SetVisibility(ViewState.bCollectionEntryVisible
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (CatStaminaProgressBar)
	{
		CatStaminaProgressBar->SetPercent(ViewState.NormalizedFightStamina);
		CatStaminaProgressBar->SetVisibility((ViewState.bShowFightMeters || ViewState.bShowPersonalStamina)
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

// 构造流程：让父类完成 Slate 构建后，对三个主 HUD 入口按钮执行 Remove/Add 配对，保证重建时不会重复广播。
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
	if (CollectionButton)
	{
		CollectionButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestOpenCollection);
		CollectionButton->OnClicked.AddDynamic(this, &ThisClass::RequestOpenCollection);
	}
}

// 销毁流程：解除三个主 HUD 入口按钮对本对象的动态绑定，再交还父类 Slate 生命周期；业务广播不保存 World 引用。
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
	if (CollectionButton)
	{
		CollectionButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestOpenCollection);
	}
	Super::NativeDestruct();
}

// Tick 流程：只在真咬钩窗口期间用服务器时间锚点刷新倒计时控件；窗口过期时本地收起提示，正式失败仍等命令/会话事实。
// 全场购买广播同样只在这里做本地淡出：投影只在公开流水变化时重建，靠它自己收不起过期的提示。
void UCatHUDWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	UWorld* TickWorld = GetWorld();
	const AGameStateBase* TickGameState = TickWorld ? TickWorld->GetGameState() : nullptr;
	const double TickServerNowSeconds = TickGameState ? TickGameState->GetServerWorldTimeSeconds()
		: (TickWorld ? TickWorld->GetTimeSeconds() : 0.0);
	if (FishDiscoveryBroadcastTextBlock && FishDiscoveryBroadcastUntilSeconds > 0.0
		&& FPlatformTime::Seconds() >= FishDiscoveryBroadcastUntilSeconds)
	{
		FishDiscoveryBroadcastUntilSeconds = 0.0;
		FishDiscoveryBroadcastTextBlock->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (PurchaseBroadcastTextBlock && LastHUDViewState.bShowPurchaseBroadcast
		&& !LastHUDViewState.PurchaseBroadcasts.IsEmpty())
	{
		const double AnnouncedServerTime = LastHUDViewState.PurchaseBroadcasts.Last().AnnouncedServerTime;
		const bool bStillVisible =
			TickServerNowSeconds - AnnouncedServerTime <= CatHUDPurchaseBroadcastLimits::VisibleSeconds;
		PurchaseBroadcastTextBlock->SetVisibility(bStillVisible
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (!LastHUDViewState.bShowHookCountdown)
	{
		return;
	}
	const double WindowDuration = FMath::Max(
		LastHUDViewState.Fishing.WindowEndsServerTime - LastHUDViewState.Fishing.PhaseStartedServerTime, 0.01);
	const double RemainingSeconds = FMath::Max(
		LastHUDViewState.Fishing.WindowEndsServerTime - TickServerNowSeconds, 0.0);
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
	// T16，钓鱼规则 §4.7：原生进度条消费服务器复制的保持区间，不依赖本轮不可编辑的 WBP 新插槽。
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	const double Now = GameState ? GameState->GetServerWorldTimeSeconds() : World ? World->GetTimeSeconds() : 0.0;
	const double HoldEnd = LastHUDViewState.Fishing.CancelHoldEndsServerTime;
	if (LastHUDViewState.bHasFishingSession && HoldEnd > 0.0)
	{
		const double Start = LastHUDViewState.Fishing.CancelHoldStartedServerTime;
		const float Alpha = float(FMath::Clamp((Now - Start) / FMath::Max(0.01, HoldEnd - Start), 0.0, 1.0));
		const FVector2D Left(LocalSize.X * 0.5f - 90.0f, LocalSize.Y * 0.65f);
		const FPaintGeometry Geometry = AllottedGeometry.ToPaintGeometry();
		TArray<FVector2D> Track{Left, Left + FVector2D(180.0f, 0.0f)};
		FSlateDrawElement::MakeLines(OutDrawElements, MaxLayer + 1, Geometry, Track, ESlateDrawEffect::None, FLinearColor(0.15f, 0.15f, 0.15f), true, 8.0f);
		Track[1] = Left + FVector2D(180.0f * Alpha, 0.0f);
		FSlateDrawElement::MakeLines(OutDrawElements, MaxLayer + 2, Geometry, Track, ESlateDrawEffect::None, FLinearColor(1.0f, 0.7f, 0.25f), true, 8.0f);
		FSlateDrawElement::MakeText(OutDrawElements, MaxLayer + 2,
			AllottedGeometry.ToPaintGeometry(FVector2D(240, 28), FSlateLayoutTransform(Left + FVector2D(0, 12))),
			FText::FromString(TEXT("收竿放弃 · 松手取消")), FCoreStyle::GetDefaultFontStyle("Regular", 14), ESlateDrawEffect::None, FLinearColor::White);
	}
	if (LastHUDViewState.bShowCrosshair)
	{
		FVector Origin, Direction;
		APlayerController* Controller = GetOwningPlayer();
		if (UCatFishingAimLibrary::TryGetLocalCastViewRay(Controller, Origin, Direction)
			&& UCatFishingAimLibrary::ResolveFishingViewTarget(Controller, Origin, Direction))
			FSlateDrawElement::MakeText(OutDrawElements, MaxLayer + 2,
				AllottedGeometry.ToPaintGeometry(FVector2D(200, 28), FSlateLayoutTransform(LocalSize * 0.5f + FVector2D(16, 16))),
				FText::FromString(TEXT("F 收鱼")), FCoreStyle::GetDefaultFontStyle("Regular", 14), ESlateDrawEffect::None, FLinearColor::White);
	}

	if (!LastHUDViewState.bShowCrosshair
		|| LocalSize.X <= 0.0f || LocalSize.Y <= 0.0f
		|| CrosshairArmLength <= 0.0f || CrosshairThickness <= 0.0f)
	{
		return MaxLayer + 2;
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
	return FMath::Max(static_cast<int32>(CrosshairLayer), MaxLayer + 2);
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

// 图鉴入口流程：左上角猫爪印只提交纯 UI 意图；实际开关图鉴由 LocalPlayer UI 协调层转交图鉴页面控制器。
void UCatHUDWidget::RequestOpenCollection()
{
	SubmitHUDAction(ECatHUDAction::OpenCollection);
}

// 意图提交流程：先广播给原生协调层处理已有页面，再通知蓝图扩展点处理未接原生控制器的页面或动画。
void UCatHUDWidget::SubmitHUDAction(const ECatHUDAction Action)
{
	OnActionRequested.Broadcast(Action);
	BP_HandleHUDAction(Action);
}
