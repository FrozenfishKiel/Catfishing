#include "UI/CatLakeReachWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"

// 初始化流程：先建立可聚焦的 UUserWidget，再创建常驻 HUD/Fishing/反馈文本和同一根树内的菜单区域；菜单默认折叠，所有子项只由 Render 更新。
void UCatLakeReachWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	if (WidgetTree->RootWidget)
	{
		return;
	}

	UVerticalBox* RootBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("LakeReachRoot"));
	UTextBlock* TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LakeReachTitle"));
	SurvivalText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LakeSurvivalStatus"));
	FishingText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LakeFishingStatus"));
	FeedbackText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LakeFishingFeedback"));
	MenuPanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("LakeReachMenu"));
	UTextBlock* MenuTitle = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LakeReachMenuTitle"));
	FishGuardText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PersonalFishGuard"));
	FishCollectionText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("FishCollection"));
	LeaveButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("LeaveLakeRun"));
	UTextBlock* LeaveText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LeaveLakeRunText"));
	CloseButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CloseLakeReachMenu"));
	UTextBlock* CloseText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CloseLakeReachMenuText"));

	TitleText->SetText(FText::FromString(TEXT("Catfishing Lake")));
	MenuTitle->SetText(FText::FromString(TEXT("Menu / Personal Catch / Collection")));
	LeaveText->SetText(FText::FromString(TEXT("Leave Run")));
	CloseText->SetText(FText::FromString(TEXT("Close Menu")));
	LeaveButton->AddChild(LeaveText);
	CloseButton->AddChild(CloseText);
	MenuPanel->AddChildToVerticalBox(MenuTitle);
	MenuPanel->AddChildToVerticalBox(FishGuardText);
	MenuPanel->AddChildToVerticalBox(FishCollectionText);
	MenuPanel->AddChildToVerticalBox(LeaveButton);
	MenuPanel->AddChildToVerticalBox(CloseButton);
	MenuPanel->SetVisibility(ESlateVisibility::Collapsed);
	RootBox->AddChildToVerticalBox(TitleText);
	RootBox->AddChildToVerticalBox(SurvivalText);
	RootBox->AddChildToVerticalBox(FishingText);
	RootBox->AddChildToVerticalBox(FeedbackText);
	RootBox->AddChildToVerticalBox(MenuPanel);
	WidgetTree->RootWidget = RootBox;
}

// 构造流程：先恢复父类 Slate 生命周期，再对菜单内关闭与离局按钮执行 Remove/Add 配对；重复进入视口仍只保留一组 UObject 回调。
void UCatLakeReachWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (CloseButton)
	{
		// 关闭按钮绑定先移除再添加，抵消 Widget 重建或重复进视口造成的动态委托重复。
		CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCloseClicked);
		CloseButton->OnClicked.AddDynamic(this, &ThisClass::HandleCloseClicked);
	}
	if (LeaveButton)
	{
		// 离局按钮遵守同一配对策略；View 只广播意图，不在按钮回调里直接改 Online/Run 状态。
		LeaveButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleLeaveClicked);
		LeaveButton->OnClicked.AddDynamic(this, &ThisClass::HandleLeaveClicked);
	}
}

// 销毁流程：解除按钮对本对象的动态绑定，再清除 View 自己的纯意图广播并交还父类；它不承担玩法、Session 或输入资源清理。
void UCatLakeReachWidget::NativeDestruct()
{
	if (CloseButton)
	{
		// Destruct 只解除本对象曾经添加的关闭按钮绑定，父级 UI 生命周期负责真正移除 Widget。
		CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCloseClicked);
	}
	if (LeaveButton)
	{
		// 离局按钮解绑后再清空本 View 的意图委托，避免外部对象在销毁后收到悬空通知。
		LeaveButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleLeaveClicked);
	}
	OnCloseRequested.Clear();
	OnLeaveRequested.Clear();
	Super::NativeDestruct();
}

// 键盘流程：仅在菜单已展开且输入键名等于协调器最近投影的菜单键时广播关闭并消费；其他输入保持 UUserWidget 默认传播。
FReply UCatLakeReachWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (bRenderedMenuOpen && !RenderedMenuToggleKeyName.IsNone()
		&& InKeyEvent.GetKey().GetFName() == RenderedMenuToggleKeyName)
	{
		OnCloseRequested.Broadcast();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

// 渲染流程：先验证所有文本、菜单和离局按钮都存在，缺任一节点就直接返回，避免半棵 WidgetTree 展示旧数据。
// 然后一次写入身体状态；Fishing 有会话时按阶段选择 Hook/Fight/Scoop/Ended/等待提示，无会话时显示可开始钓鱼的空态。
// 结构化命令结果只在协调器显式带入时显示，否则保持等待动作；鱼护总是按快照列出，Profile 不可用时图鉴显示降级文案。
// 最后写入菜单打开状态、关闭快捷键缓存和离局按钮的启用/可见性，Widget 不反向推导菜单或 Online 权限。
void UCatLakeReachWidget::Render(const FCatUIReachViewState& ViewState)
{
	if (!SurvivalText || !FishingText || !FeedbackText || !MenuPanel || !FishGuardText || !FishCollectionText
		|| !LeaveButton)
	{
		return;
	}

	const FString Survival = FString::Printf(
		TEXT("Poison %.1f  Strength %.1f  Stamina %.1f\n")
		TEXT("Growth XP %d  Slot %d  Pending Choices %d\n")
		TEXT("Wet %s  Downed %s  Recovery %s\nRod %s  Durability %.1f  Day %d  Phase %s  Weather %s  Help %s"),
		ViewState.Poison, ViewState.FishingStrength, ViewState.FightStamina,
		ViewState.Growth.TotalExperience, ViewState.Growth.ExperienceInCurrentSlot, ViewState.Growth.PendingChoiceCount,
		ViewState.Condition.bWet ? TEXT("true") : TEXT("false"),
		ViewState.Condition.bDowned ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(ViewState.Condition.RecoveryMode),
		*ViewState.Equipment.RodDefinitionId.ToString(), ViewState.Equipment.RodDurability,
		ViewState.Run.Phase.DayIndex, *UEnum::GetValueAsString(ViewState.Run.Phase.Phase),
		*UEnum::GetValueAsString(ViewState.Run.Environment.Weather),
		*UEnum::GetValueAsString(ViewState.HelpSignal.Kind));
	SurvivalText->SetText(FText::FromString(Survival));

	FString Prompt = TEXT("Place or operate a rod to begin fishing");
	if (ViewState.bHasFishingSession)
	{
		switch (ViewState.Fishing.Phase)
		{
		case ECatFishingPhase::TrueBiteWindow:
			Prompt = TEXT("Hook now");
			break;
		case ECatFishingPhase::HookedFight:
			Prompt = TEXT("Reel or slack according to fish motion");
			break;
		case ECatFishingPhase::NearShore:
			Prompt = TEXT("Scoop the fish near shore");
			break;
		case ECatFishingPhase::Resolved:
		case ECatFishingPhase::Terminated:
			Prompt = TEXT("Fishing session ended");
			break;
		default:
			Prompt = TEXT("Wait for the bite or cancel fishing");
			break;
		}
		FishingText->SetText(FText::FromString(FString::Printf(
			TEXT("Fishing %s  Fish %s  Fish stamina %.0f%%  Reeling %s  Slacking %s\nPrompt: %s"),
			*UEnum::GetValueAsString(ViewState.Fishing.Phase), *ViewState.Fishing.FishDefinitionId.ToString(),
			ViewState.Fishing.NormalizedFishStamina * 100.0,
			ViewState.Fishing.bReeling ? TEXT("true") : TEXT("false"),
			ViewState.Fishing.bSlacking ? TEXT("true") : TEXT("false"), *Prompt)));
	}
	else
	{
		FishingText->SetText(FText::FromString(FString::Printf(TEXT("Fishing: no active session\nPrompt: %s"), *Prompt)));
	}

	if (ViewState.bHasFishingCommandResult)
	{
		FeedbackText->SetText(FText::FromString(FString::Printf(TEXT("Last action %s: %s (Committed %s)"),
			*UEnum::GetValueAsString(ViewState.LastFishingCommandResult.CommandType),
			*UEnum::GetValueAsString(ViewState.LastFishingCommandResult.Error),
			ViewState.LastFishingCommandResult.bCommitted ? TEXT("true") : TEXT("false"))));
	}
	else
	{
		FeedbackText->SetText(FText::FromString(TEXT("Fishing feedback: waiting for an action")));
	}

	FString GuardLines = FString::Printf(TEXT("Personal fish guard: %d fish (Revision %lld)"),
		ViewState.PersonalFishGuard.Fish.Num(), ViewState.PersonalFishGuard.Revision);
	for (const FCatFishInstance& Fish : ViewState.PersonalFishGuard.Fish)
	{
		GuardLines += FString::Printf(TEXT("\n- %s  %.2f kg"), *Fish.FishDefinitionId.ToString(), Fish.WeightKilograms);
	}
	FishGuardText->SetText(FText::FromString(GuardLines));

	FString CollectionLines = ViewState.bFishCollectionAvailable
		? FString::Printf(TEXT("Fish collection: %d records"), ViewState.FishCollection.Num())
		: FString(TEXT("Fish collection: profile unavailable"));
	for (const FCatFishCollectionRecord& Record : ViewState.FishCollection)
	{
		CollectionLines += FString::Printf(TEXT("\n- %s  %s  Best %.2f kg  Encounters %d"),
			*Record.FishDefinitionId.ToString(), *UEnum::GetValueAsString(Record.State),
			Record.BestWeightKilograms, Record.EncounterCount);
	}
	FishCollectionText->SetText(FText::FromString(CollectionLines));

	bRenderedMenuOpen = ViewState.bMenuOpen;
	bRenderedCanRequestOnlineLeave = ViewState.bCanRequestOnlineLeave;
	RenderedMenuToggleKeyName = ViewState.MenuToggleKeyName;
	LeaveButton->SetIsEnabled(bRenderedMenuOpen && bRenderedCanRequestOnlineLeave);
	LeaveButton->SetVisibility(bRenderedCanRequestOnlineLeave ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	MenuPanel->SetVisibility(bRenderedMenuOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

// 点击流程：只广播关闭意图；LocalPlayer 协调器负责判断当前菜单状态并恢复 Enhanced Input、焦点和鼠标。
void UCatLakeReachWidget::HandleCloseClicked()
{
	OnCloseRequested.Broadcast();
}

// 离局点击流程：只在菜单仍展开且最近完整投影允许离局时广播纯意图；实际 RequestLeave、Run teardown 与旅行都留给 Online 管线。
void UCatLakeReachWidget::HandleLeaveClicked()
{
	if (bRenderedMenuOpen && bRenderedCanRequestOnlineLeave)
	{
		OnLeaveRequested.Broadcast();
	}
}
