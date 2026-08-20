#include "UI/CatCommandPanelWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"

namespace
{
	// 面板覆盖的意图总数；按钮数组、绑定循环和标签表都以它为界，枚举末尾加值时这里会跟着变，测试再按 StaticEnum 核对标签表是否漏项。
	constexpr int32 CatCommandPanelActionCount = static_cast<int32>(ECatCommandPanelAction::FightNeutral) + 1;

	// 客户端对“服务器玩法命令门是否开着”的猜测：Run 已经起来且没走到收口，且启动没失败。服务器真正的门是
	// GameMode::bRunCommandsOpen，这里猜错只会让按钮多开或少开，不会绕过服务器。
	bool IsRunLikelyAcceptingCommands(const FCatCommandPanelViewState& ViewState)
	{
		return ViewState.EndReason != ECatRunEndReason::StartupFailed
			&& ViewState.Phase != ECatRunPhase::NotStarted
			&& ViewState.Phase != ECatRunPhase::Ending
			&& ViewState.Phase != ECatRunPhase::Ended;
	}

	// 结算夜判定；进结算夜时服务器会把商店和团队装备库一起收摊，所以卖鱼、买东西和从公库取装备的按钮都跟着关。
	bool IsSettlementNight(const FCatCommandPanelViewState& ViewState)
	{
		return ViewState.Phase == ECatRunPhase::FailureSettlementNight
			|| ViewState.Phase == ECatRunPhase::SuccessSettlementNight;
	}
}

// 标签表：每个意图一条英文短标签（PIE 里字体只保证 ASCII 可读）；没有 default 分支是故意的——漏项返回 nullptr，让自动化测试能抓到。
const TCHAR* UCatCommandPanelWidget::GetActionLabel(const ECatCommandPanelAction Action)
{
	switch (Action)
	{
	case ECatCommandPanelAction::RunReady: return TEXT("Run: Ready for next day");
	case ECatCommandPanelAction::RunUnready: return TEXT("Run: Cancel ready");
	case ECatCommandPanelAction::SettlementComplete: return TEXT("Run: Settlement complete");
	case ECatCommandPanelAction::StartFishing: return TEXT("Fishing: Start session");
	case ECatCommandPanelAction::Scoop: return TEXT("Fishing: Scoop (first active session)");
	case ECatCommandPanelAction::CampRest: return TEXT("Camp: Rest");
	case ECatCommandPanelAction::TransferFirstFishToTank: return TEXT("Camp: First fish -> tank");
	case ECatCommandPanelAction::CampfirePlayback: return TEXT("Camp: Campfire playback");
	case ECatCommandPanelAction::RescueDownedToCamp: return TEXT("Camp: Rescue first downed");
	case ECatCommandPanelAction::SacrificeFirstFish: return TEXT("Sacrifice: First fish");
	case ECatCommandPanelAction::ShopBuyFirstPaidEntry: return TEXT("Shop: Buy first paid entry (RodT2)");
	case ECatCommandPanelAction::ShopClaimFreeBait: return TEXT("Shop: Claim free bait");
	case ECatCommandPanelAction::SellFirstFish: return TEXT("Shop: Sell first fish");
	case ECatCommandPanelAction::ManualHelp: return TEXT("Social: Manual help");
	case ECatCommandPanelAction::ShakeDry: return TEXT("Condition: Shake dry");
	case ECatCommandPanelAction::FieldSelfRecovery: return TEXT("Condition: Field self-recovery (downed)");
	case ECatCommandPanelAction::ConfigureStarterEquipment: return TEXT("Equipment: Configure starter loadout");
	case ECatCommandPanelAction::ShopBuyFirstChum: return TEXT("Shop: Buy first chum entry");
	case ECatCommandPanelAction::TakeFirstTeamEquipment: return TEXT("Equipment: Take first library item");
	case ECatCommandPanelAction::ContributeChum: return TEXT("Chum: Contribute first chum (at cat)");
	case ECatCommandPanelAction::FightPull: return TEXT("Fight: Pull (LMB) / set hook in true-bite");
	case ECatCommandPanelAction::FightRelease: return TEXT("Fight: Release line (RMB)");
	case ECatCommandPanelAction::FightNeutral: return TEXT("Fight: Neutral (no input)");
	}
	return nullptr;
}

// 可用性推导：先看 Run 命令门是否大概率开着（关着时所有按钮全关），再按每条链自己的前置事实细分；所有分支只读 ViewState，不碰任何宿主。
bool UCatCommandPanelWidget::IsActionAvailable(const FCatCommandPanelViewState& ViewState, const ECatCommandPanelAction Action)
{
	if (!IsRunLikelyAcceptingCommands(ViewState))
	{
		return false;
	}
	const bool bHasFish = ViewState.GuardFishCount > 0;
	switch (Action)
	{
	case ECatCommandPanelAction::RunReady:
	case ECatCommandPanelAction::RunUnready:
		return ViewState.Phase == ECatRunPhase::NormalNight;
	case ECatCommandPanelAction::SettlementComplete:
		return IsSettlementNight(ViewState);
	case ECatCommandPanelAction::StartFishing:
		return ViewState.bFishingAllowed && !ViewState.bDowned && !ViewState.bHasActiveFishingSession;
	case ECatCommandPanelAction::Scoop:
		return ViewState.bHasActiveFishingSession && !ViewState.bDowned;
	case ECatCommandPanelAction::CampRest:
	case ECatCommandPanelAction::CampfirePlayback:
		return ViewState.bHasCamp;
	case ECatCommandPanelAction::TransferFirstFishToTank:
		return ViewState.bHasCamp && bHasFish;
	case ECatCommandPanelAction::RescueDownedToCamp:
		return ViewState.bHasCamp && ViewState.bHasRescueTarget;
	case ECatCommandPanelAction::SacrificeFirstFish:
		return ViewState.bQuotaOpen && bHasFish && !ViewState.bDowned;
	case ECatCommandPanelAction::ShopBuyFirstPaidEntry:
	case ECatCommandPanelAction::ShopClaimFreeBait:
	case ECatCommandPanelAction::ShopBuyFirstChum:
		return !IsSettlementNight(ViewState);
	case ECatCommandPanelAction::TakeFirstTeamEquipment:
		// 库里有东西还不够：结算夜装备库和商店一起收摊，这时按下去只会被服务器拒。
		return !IsSettlementNight(ViewState) && ViewState.bHasTeamEquipment;
	case ECatCommandPanelAction::ContributeChum:
		return ViewState.bHasChum && !ViewState.bDowned;
	case ECatCommandPanelAction::FightPull:
	case ECatCommandPanelAction::FightRelease:
	case ECatCommandPanelAction::FightNeutral:
		return ViewState.bHasActiveFishingSession && !ViewState.bDowned;
	case ECatCommandPanelAction::SellFirstFish:
		return !IsSettlementNight(ViewState) && bHasFish;
	case ECatCommandPanelAction::ManualHelp:
	case ECatCommandPanelAction::ConfigureStarterEquipment:
		return true;
	case ECatCommandPanelAction::ShakeDry:
		return ViewState.bWet;
	case ECatCommandPanelAction::FieldSelfRecovery:
		return ViewState.bDowned;
	}
	return false;
}

// 初始化流程：根节点用 Canvas 把整块面板锚到视口右上角（左上角已被 SurvivalWidget 的状态文本占用）；纵向容器里依次是
// 状态文本、按枚举顺序的一排按钮、反馈文本。
// 按钮保持 UButton 默认可聚焦（InitIsFocusable 是 protected，这里改不了）；点击后键盘焦点会落到按钮上，由协调器在处理
// 完点击后把焦点还给游戏视口，WASD 才不会断。
void UCatCommandPanelWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("CommandPanelRoot"));
	WidgetTree->RootWidget = Root;
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CommandPanelColumn"));
	if (UCanvasPanelSlot* ColumnSlot = Root->AddChildToCanvas(Column))
	{
		ColumnSlot->SetAnchors(FAnchors(1.0f, 0.0f));
		ColumnSlot->SetAlignment(FVector2D(1.0, 0.0));
		ColumnSlot->SetPosition(FVector2D(-16.0, 16.0));
		ColumnSlot->SetAutoSize(true);
	}

	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CommandPanelStatus"));
	StatusText->SetText(FText::FromString(TEXT("[DEV] Command panel (white-box, not final UI)")));
	Column->AddChildToVerticalBox(StatusText);

	Buttons.Reset(CatCommandPanelActionCount);
	for (int32 Index = 0; Index < CatCommandPanelActionCount; ++Index)
	{
		const ECatCommandPanelAction Action = static_cast<ECatCommandPanelAction>(Index);
		UButton* Button = WidgetTree->ConstructWidget<UButton>();
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
		const TCHAR* LabelText = GetActionLabel(Action);
		Label->SetText(FText::FromString(LabelText ? LabelText : TEXT("<unlabeled>")));
		Button->AddChild(Label);
		Column->AddChildToVerticalBox(Button);
		Buttons.Add(Button);
	}

	FeedbackText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CommandPanelFeedback"));
	FeedbackText->SetText(FText::FromString(TEXT("Last: (nothing sent yet)")));
	FeedbackText->SetAutoWrapText(true);
	Column->AddChildToVerticalBox(FeedbackText);
}

// 构造流程：父类先恢复 Slate；然后按枚举顺序对每个按钮 Remove/Add 配对绑定对应的 Handle*Clicked，多次 Construct 也只剩一个回调。
void UCatCommandPanelWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (Buttons.Num() != CatCommandPanelActionCount)
	{
		return;
	}
#define CAT_BIND_PANEL_BUTTON(ActionName) \
	Buttons[static_cast<int32>(ECatCommandPanelAction::ActionName)]->OnClicked.RemoveDynamic(this, &ThisClass::Handle##ActionName##Clicked); \
	Buttons[static_cast<int32>(ECatCommandPanelAction::ActionName)]->OnClicked.AddDynamic(this, &ThisClass::Handle##ActionName##Clicked);
	CAT_BIND_PANEL_BUTTON(RunReady)
	CAT_BIND_PANEL_BUTTON(RunUnready)
	CAT_BIND_PANEL_BUTTON(SettlementComplete)
	CAT_BIND_PANEL_BUTTON(StartFishing)
	CAT_BIND_PANEL_BUTTON(Scoop)
	CAT_BIND_PANEL_BUTTON(CampRest)
	CAT_BIND_PANEL_BUTTON(TransferFirstFishToTank)
	CAT_BIND_PANEL_BUTTON(CampfirePlayback)
	CAT_BIND_PANEL_BUTTON(RescueDownedToCamp)
	CAT_BIND_PANEL_BUTTON(SacrificeFirstFish)
	CAT_BIND_PANEL_BUTTON(ShopBuyFirstPaidEntry)
	CAT_BIND_PANEL_BUTTON(ShopClaimFreeBait)
	CAT_BIND_PANEL_BUTTON(SellFirstFish)
	CAT_BIND_PANEL_BUTTON(ManualHelp)
	CAT_BIND_PANEL_BUTTON(ShakeDry)
	CAT_BIND_PANEL_BUTTON(FieldSelfRecovery)
	CAT_BIND_PANEL_BUTTON(ConfigureStarterEquipment)
	CAT_BIND_PANEL_BUTTON(ShopBuyFirstChum)
	CAT_BIND_PANEL_BUTTON(TakeFirstTeamEquipment)
	CAT_BIND_PANEL_BUTTON(ContributeChum)
	CAT_BIND_PANEL_BUTTON(FightPull)
	CAT_BIND_PANEL_BUTTON(FightRelease)
	CAT_BIND_PANEL_BUTTON(FightNeutral)
#undef CAT_BIND_PANEL_BUTTON
}

// 销毁流程：解除每个按钮对本对象的全部动态绑定（RemoveAll 覆盖 Construct 绑过的任何入口），再让父类释放 Slate。
void UCatCommandPanelWidget::NativeDestruct()
{
	for (UButton* Button : Buttons)
	{
		if (Button)
		{
			Button->OnClicked.RemoveAll(this);
		}
	}
	Super::NativeDestruct();
}

// 配置流程：按枚举下标逐个用 IsActionAvailable 设按钮可用性；状态文本显示相位/终局原因/Revision/鱼护条数/宿主存在性，
// 反馈文本原样显示协调器给的最近一次分派结果。
void UCatCommandPanelWidget::Configure(const FCatCommandPanelViewState& ViewState)
{
	for (int32 Index = 0; Index < Buttons.Num(); ++Index)
	{
		if (Buttons[Index])
		{
			Buttons[Index]->SetIsEnabled(IsActionAvailable(ViewState, static_cast<ECatCommandPanelAction>(Index)));
		}
	}
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(FString::Printf(
			TEXT("[DEV] Command panel (white-box, not final UI)\nRun: %s  End=%s  Rev=%lld\nFishingAllowed=%s QuotaOpen=%s Downed=%s Wet=%s\nGuardFish=%d Camp=%s ActiveSession=%s RescueTarget=%s\n%s"),
			*UEnum::GetValueAsString(ViewState.Phase), *UEnum::GetValueAsString(ViewState.EndReason), ViewState.RunRevision,
			ViewState.bFishingAllowed ? TEXT("true") : TEXT("false"), ViewState.bQuotaOpen ? TEXT("true") : TEXT("false"),
			ViewState.bDowned ? TEXT("true") : TEXT("false"), ViewState.bWet ? TEXT("true") : TEXT("false"),
			ViewState.GuardFishCount, ViewState.bHasCamp ? TEXT("yes") : TEXT("no"),
			ViewState.bHasActiveFishingSession ? TEXT("yes") : TEXT("no"), ViewState.bHasRescueTarget ? TEXT("yes") : TEXT("no"),
			ViewState.FishingSessionLine.IsEmpty() ? TEXT("Fishing: (no session)") : *ViewState.FishingSessionLine)));
	}
	if (FeedbackText)
	{
		FeedbackText->SetText(FText::FromString(ViewState.LastFeedback.IsEmpty()
			? TEXT("Last: (nothing sent yet)") : FString::Printf(TEXT("Last: %s"), *ViewState.LastFeedback)));
	}
}

// 以下 17 个点击入口一一对应枚举值，只广播、不读状态、不判可用性（可用性由 SetIsEnabled 在 Slate 层拦截）。
void UCatCommandPanelWidget::HandleRunReadyClicked() { OnActionRequested.Broadcast(ECatCommandPanelAction::RunReady); }
void UCatCommandPanelWidget::HandleRunUnreadyClicked() { OnActionRequested.Broadcast(ECatCommandPanelAction::RunUnready); }
void UCatCommandPanelWidget::HandleSettlementCompleteClicked() { OnActionRequested.Broadcast(ECatCommandPanelAction::SettlementComplete); }
void UCatCommandPanelWidget::HandleStartFishingClicked() { OnActionRequested.Broadcast(ECatCommandPanelAction::StartFishing); }
void UCatCommandPanelWidget::HandleScoopClicked() { OnActionRequested.Broadcast(ECatCommandPanelAction::Scoop); }
void UCatCommandPanelWidget::HandleCampRestClicked() { OnActionRequested.Broadcast(ECatCommandPanelAction::CampRest); }
void UCatCommandPanelWidget::HandleTransferFirstFishToTankClicked() { OnActionRequested.Broadcast(ECatCommandPanelAction::TransferFirstFishToTank); }
void UCatCommandPanelWidget::HandleCampfirePlaybackClicked() { OnActionRequested.Broadcast(ECatCommandPanelAction::CampfirePlayback); }
void UCatCommandPanelWidget::HandleRescueDownedToCampClicked() { OnActionRequested.Broadcast(ECatCommandPanelAction::RescueDownedToCamp); }
void UCatCommandPanelWidget::HandleSacrificeFirstFishClicked() { OnActionRequested.Broadcast(ECatCommandPanelAction::SacrificeFirstFish); }
void UCatCommandPanelWidget::HandleShopBuyFirstPaidEntryClicked() { OnActionRequested.Broadcast(ECatCommandPanelAction::ShopBuyFirstPaidEntry); }
void UCatCommandPanelWidget::HandleShopClaimFreeBaitClicked() { OnActionRequested.Broadcast(ECatCommandPanelAction::ShopClaimFreeBait); }
void UCatCommandPanelWidget::HandleSellFirstFishClicked() { OnActionRequested.Broadcast(ECatCommandPanelAction::SellFirstFish); }
void UCatCommandPanelWidget::HandleManualHelpClicked() { OnActionRequested.Broadcast(ECatCommandPanelAction::ManualHelp); }
void UCatCommandPanelWidget::HandleShakeDryClicked() { OnActionRequested.Broadcast(ECatCommandPanelAction::ShakeDry); }
void UCatCommandPanelWidget::HandleFieldSelfRecoveryClicked() { OnActionRequested.Broadcast(ECatCommandPanelAction::FieldSelfRecovery); }
void UCatCommandPanelWidget::HandleConfigureStarterEquipmentClicked() { OnActionRequested.Broadcast(ECatCommandPanelAction::ConfigureStarterEquipment); }
void UCatCommandPanelWidget::HandleShopBuyFirstChumClicked() { OnActionRequested.Broadcast(ECatCommandPanelAction::ShopBuyFirstChum); }
void UCatCommandPanelWidget::HandleTakeFirstTeamEquipmentClicked() { OnActionRequested.Broadcast(ECatCommandPanelAction::TakeFirstTeamEquipment); }
void UCatCommandPanelWidget::HandleContributeChumClicked() { OnActionRequested.Broadcast(ECatCommandPanelAction::ContributeChum); }
void UCatCommandPanelWidget::HandleFightPullClicked() { OnActionRequested.Broadcast(ECatCommandPanelAction::FightPull); }
void UCatCommandPanelWidget::HandleFightReleaseClicked() { OnActionRequested.Broadcast(ECatCommandPanelAction::FightRelease); }
void UCatCommandPanelWidget::HandleFightNeutralClicked() { OnActionRequested.Broadcast(ECatCommandPanelAction::FightNeutral); }
