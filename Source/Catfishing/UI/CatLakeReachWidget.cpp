#include "UI/CatLakeReachWidget.h"

#include "Components/Button.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"

// 初始化流程：只让 UUserWidget 可接收键盘焦点；正式布局和控件树由 WBP 资产提供，C++ 不再构造玩家可见白盒界面。
void UCatLakeReachWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
}

// 构造流程：先恢复父类 Slate 生命周期，再对 WBP 可选绑定按钮执行 Remove/Add 配对；没有绑定按钮时仍允许蓝图自己调用 RequestCloseMenu/RequestLeaveLake。
void UCatLakeReachWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (CloseButton)
	{
		CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCloseClicked);
		CloseButton->OnClicked.AddDynamic(this, &ThisClass::HandleCloseClicked);
	}
	if (LeaveButton)
	{
		LeaveButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleLeaveClicked);
		LeaveButton->OnClicked.AddDynamic(this, &ThisClass::HandleLeaveClicked);
	}
	if (PreviousFishGuardButton)
	{
		PreviousFishGuardButton->OnClicked.RemoveDynamic(this, &ThisClass::HandlePreviousFishGuardClicked);
		PreviousFishGuardButton->OnClicked.AddDynamic(this, &ThisClass::HandlePreviousFishGuardClicked);
	}
	if (NextFishGuardButton)
	{
		NextFishGuardButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleNextFishGuardClicked);
		NextFishGuardButton->OnClicked.AddDynamic(this, &ThisClass::HandleNextFishGuardClicked);
	}
	if (ConsumeFishButton)
	{
		ConsumeFishButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleConsumeFishClicked);
		ConsumeFishButton->OnClicked.AddDynamic(this, &ThisClass::HandleConsumeFishClicked);
	}
	if (TransferFishToTankButton)
	{
		TransferFishToTankButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleTransferFishToTankClicked);
		TransferFishToTankButton->OnClicked.AddDynamic(this, &ThisClass::HandleTransferFishToTankClicked);
	}
	if (SacrificeFishButton)
	{
		SacrificeFishButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleSacrificeFishClicked);
		SacrificeFishButton->OnClicked.AddDynamic(this, &ThisClass::HandleSacrificeFishClicked);
	}
}

// 销毁流程：解除 WBP 可选按钮对本对象的动态绑定，再清除 View 自己的纯意图广播；它不承担 Model、Session 或输入资源清理。
void UCatLakeReachWidget::NativeDestruct()
{
	if (CloseButton)
	{
		CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCloseClicked);
	}
	if (LeaveButton)
	{
		LeaveButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleLeaveClicked);
	}
	if (PreviousFishGuardButton)
	{
		PreviousFishGuardButton->OnClicked.RemoveDynamic(this, &ThisClass::HandlePreviousFishGuardClicked);
	}
	if (NextFishGuardButton)
	{
		NextFishGuardButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleNextFishGuardClicked);
	}
	if (ConsumeFishButton)
	{
		ConsumeFishButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleConsumeFishClicked);
	}
	if (TransferFishToTankButton)
	{
		TransferFishToTankButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleTransferFishToTankClicked);
	}
	if (SacrificeFishButton)
	{
		SacrificeFishButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleSacrificeFishClicked);
	}
	OnCloseRequested.Clear();
	OnLeaveRequested.Clear();
	OnFishGuardSelectionRequested.Clear();
	OnFishGuardActionRequested.Clear();
	Super::NativeDestruct();
}

// 键盘流程：仅在菜单已展开且输入键名等于 PageController 最近投影的菜单键时请求关闭并消费；其余输入保持 UUserWidget 默认传播。
FReply UCatLakeReachWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (bRenderedMenuOpen && !RenderedMenuToggleKeyName.IsNone()
		&& InKeyEvent.GetKey().GetFName() == RenderedMenuToggleKeyName)
	{
		RequestCloseMenu();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

// 渲染流程：
// 1. 缓存 native DTO，保留 C++ 测试和 native-only FishCollection 副本。
// 2. 转换为蓝图安全 DTO，其中 Profile 图鉴记录只复制成 UI 专用条目，不暴露 SaveGame 结构。
// 3. 更新给 WBP Designer 属性绑定读取的只读标量与 Visibility 投影；C++ 不写任何 TextBlock 或菜单容器。
// 4. 更新键盘关闭和离局 gate 缓存，再触发可选 BP_RenderViewState，让蓝图图表补动画或复杂表现。
void UCatLakeReachWidget::Render(const FCatUIReachViewState& ViewState)
{
	LastNativeViewState = ViewState;
	LastBlueprintViewState = MakeBlueprintViewState(ViewState);
	BlueprintPoisonValue = LastBlueprintViewState.Poison;
	BlueprintFishingStrengthValue = LastBlueprintViewState.FishingStrength;
	BlueprintFightStaminaValue = LastBlueprintViewState.FightStamina;
	BlueprintFishGuardCount = LastBlueprintViewState.PersonalFishGuard.Fish.Num();
	BlueprintSelectedFishGuardIndex = LastBlueprintViewState.SelectedFishGuardIndex;
	BlueprintSelectedFishDefinitionId = LastBlueprintViewState.SelectedFishGuardFish.FishDefinitionId;
	BlueprintSelectedFishText = LastBlueprintViewState.bHasSelectedFishGuardFish
		? FText::FromString(FString::Printf(TEXT("%s %.2f kg"),
			*LastBlueprintViewState.SelectedFishGuardFish.FishDefinitionId.ToString(),
			LastBlueprintViewState.SelectedFishGuardFish.WeightKilograms))
		: FText::GetEmpty();
	BlueprintSelectedFishWeightKilograms = LastBlueprintViewState.SelectedFishGuardFish.WeightKilograms;
	bBlueprintHasSelectedFishGuardFish = LastBlueprintViewState.bHasSelectedFishGuardFish;
	bBlueprintCanSelectPreviousFishGuardEntry = LastBlueprintViewState.bCanSelectPreviousFishGuardEntry;
	bBlueprintCanSelectNextFishGuardEntry = LastBlueprintViewState.bCanSelectNextFishGuardEntry;
	bBlueprintFishGuardActionEnabled = LastBlueprintViewState.bCanSubmitSelectedFishGuardAction;
	BlueprintFishGuardActionVisibility = LastBlueprintViewState.bHasSelectedFishGuardFish
		? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	bBlueprintFishGuardActionPending = LastBlueprintViewState.bFishGuardActionPending;
	BlueprintFishGuardResultRevision = LastBlueprintViewState.LastFishGuardCommandResult.Revision;
	BlueprintFishGuardResultError = LastBlueprintViewState.LastFishGuardCommandResult.Error;
	BlueprintFishGuardResultText = LastBlueprintViewState.bHasFishGuardCommandResult
		? FText::FromString(UEnum::GetValueAsString(LastBlueprintViewState.LastFishGuardCommandResult.Error))
		: FText::GetEmpty();
	BlueprintFishCollectionCount = LastBlueprintViewState.FishCollectionEntries.Num();
	BlueprintMenuVisibility = LastBlueprintViewState.bMenuOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	BlueprintLeaveVisibility = LastBlueprintViewState.bCanRequestOnlineLeave ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	bBlueprintLeaveEnabled = LastBlueprintViewState.bMenuOpen && LastBlueprintViewState.bCanRequestOnlineLeave;
	bRenderedMenuOpen = ViewState.bMenuOpen;
	bRenderedCanRequestOnlineLeave = ViewState.bCanRequestOnlineLeave;
	bRenderedCanSelectPreviousFishGuardEntry = ViewState.bCanSelectPreviousFishGuardEntry;
	bRenderedCanSelectNextFishGuardEntry = ViewState.bCanSelectNextFishGuardEntry;
	bRenderedCanSubmitSelectedFishGuardAction = ViewState.bCanSubmitSelectedFishGuardAction;
	RenderedMenuToggleKeyName = ViewState.MenuToggleKeyName;
	BP_RenderViewState(LastBlueprintViewState);
}

// 关闭请求流程：只广播关闭意图；PageController 负责判断当前菜单状态并恢复 Enhanced Input、焦点和鼠标。
void UCatLakeReachWidget::RequestCloseMenu()
{
	OnCloseRequested.Broadcast();
}

// 离局请求流程：只在菜单仍展开且最近完整投影允许离局时广播纯意图；实际 RequestLeave、Run teardown 与旅行都留给 Online 管线。
void UCatLakeReachWidget::RequestLeaveLake()
{
	if (bRenderedMenuOpen && bRenderedCanRequestOnlineLeave)
	{
		OnLeaveRequested.Broadcast();
	}
}

// 选择前移流程：只有菜单仍展开且最近投影允许前移时才广播偏移；Model 会按最新鱼护快照重新裁剪下标。
void UCatLakeReachWidget::RequestSelectPreviousFishGuardEntry()
{
	if (bRenderedMenuOpen && bRenderedCanSelectPreviousFishGuardEntry)
	{
		OnFishGuardSelectionRequested.Broadcast(-1);
	}
}

// 选择后移流程：只有菜单仍展开且最近投影允许后移时才广播偏移；Widget 不读取鱼数组或缓存目标鱼 ID。
void UCatLakeReachWidget::RequestSelectNextFishGuardEntry()
{
	if (bRenderedMenuOpen && bRenderedCanSelectNextFishGuardEntry)
	{
		OnFishGuardSelectionRequested.Broadcast(1);
	}
}

// 吃鱼请求流程：复用统一鱼护动作过滤；服务器结果回来前 View 不移除本地鱼条目。
void UCatLakeReachWidget::RequestConsumeSelectedFish()
{
	RequestFishGuardAction(ECatUIReachFishGuardAction::ConsumeSelectedFish);
}

// 转缸请求流程：复用统一鱼护动作过滤；共享鱼缸和版本号由 PageController 重读正式来源。
void UCatLakeReachWidget::RequestTransferSelectedFishToTank()
{
	RequestFishGuardAction(ECatUIReachFishGuardAction::TransferSelectedFishToTank);
}

// 献祭请求流程：复用统一鱼护动作过滤；献祭协议载荷由 PageController 从当前 Model 选择构造。
void UCatLakeReachWidget::RequestSacrificeSelectedFish()
{
	RequestFishGuardAction(ECatUIReachFishGuardAction::SacrificeSelectedFish);
}

// 蓝图状态读取流程：返回最近一次 Render 生成的蓝图安全副本；延迟动画读取它不会触碰 Model 或玩法对象。
FCatUIReachBlueprintViewState UCatLakeReachWidget::GetLastBlueprintViewState() const
{
	return LastBlueprintViewState;
}

// native 状态读取流程：返回最近一次 Render 输入；测试用它确认 native-only 图鉴记录仍被 Model 保留。
const FCatUIReachViewState& UCatLakeReachWidget::GetLastNativeViewState() const
{
	return LastNativeViewState;
}

// 点击流程：把 WBP 可选按钮点击收口到同一个蓝图可调用入口；View 不直接访问 PlayerController。
void UCatLakeReachWidget::HandleCloseClicked()
{
	RequestCloseMenu();
}

// 离局点击流程：把 WBP 可选按钮点击收口到同一个蓝图可调用入口；Online gate 由最近 ViewState 缓存过滤。
void UCatLakeReachWidget::HandleLeaveClicked()
{
	RequestLeaveLake();
}

// 上一条点击流程：把 WBP 点击收口到蓝图可调用入口；按钮重建或蓝图直接调用都经过同一 gate。
void UCatLakeReachWidget::HandlePreviousFishGuardClicked()
{
	RequestSelectPreviousFishGuardEntry();
}

// 下一条点击流程：把 WBP 点击收口到蓝图可调用入口；按钮重建或蓝图直接调用都经过同一 gate。
void UCatLakeReachWidget::HandleNextFishGuardClicked()
{
	RequestSelectNextFishGuardEntry();
}

// 吃鱼点击流程：只表达玩家想吃当前选中鱼；不在 View 中修改鱼护数量或身体数值。
void UCatLakeReachWidget::HandleConsumeFishClicked()
{
	RequestConsumeSelectedFish();
}

// 转缸点击流程：只表达玩家想把当前选中鱼转入共享鱼缸；不在 View 中访问 Camp Actor。
void UCatLakeReachWidget::HandleTransferFishToTankClicked()
{
	RequestTransferSelectedFishToTank();
}

// 献祭点击流程：只表达玩家想献祭当前选中鱼；不在 View 中访问 SacrificeCoordinator。
void UCatLakeReachWidget::HandleSacrificeFishClicked()
{
	RequestSacrificeSelectedFish();
}

// 鱼护动作过滤流程：先拒绝空动作、关闭菜单、无选择或 pending 期间的迟到点击；通过后只广播动作枚举，让 PageController 从 Model 重建可信命令。
void UCatLakeReachWidget::RequestFishGuardAction(const ECatUIReachFishGuardAction Action)
{
	if (Action != ECatUIReachFishGuardAction::None && bRenderedMenuOpen && bRenderedCanSubmitSelectedFishGuardAction)
	{
		OnFishGuardActionRequested.Broadcast(Action);
	}
}

// DTO 转换流程：复制所有蓝图支持字段；Profile 的 native-only FishCollection 逐条转为 UI 专用条目，避免为展示修改 durable 合同。
FCatUIReachBlueprintViewState UCatLakeReachWidget::MakeBlueprintViewState(const FCatUIReachViewState& ViewState)
{
	FCatUIReachBlueprintViewState BlueprintState;
	BlueprintState.Poison = ViewState.Poison;
	BlueprintState.FishingStrength = ViewState.FishingStrength;
	BlueprintState.FightStamina = ViewState.FightStamina;
	BlueprintState.Condition = ViewState.Condition;
	BlueprintState.Growth = ViewState.Growth;
	BlueprintState.Equipment = ViewState.Equipment;
	BlueprintState.Run = ViewState.Run;
	BlueprintState.HelpSignal = ViewState.HelpSignal;
	BlueprintState.Fishing = ViewState.Fishing;
	BlueprintState.bHasFishingSession = ViewState.bHasFishingSession;
	BlueprintState.LastFishingCommandResult = ViewState.LastFishingCommandResult;
	BlueprintState.bHasFishingCommandResult = ViewState.bHasFishingCommandResult;
	BlueprintState.PersonalFishGuard = ViewState.PersonalFishGuard;
	BlueprintState.SelectedFishGuardIndex = ViewState.SelectedFishGuardIndex;
	BlueprintState.SelectedFishGuardFish = ViewState.SelectedFishGuardFish;
	BlueprintState.bHasSelectedFishGuardFish = ViewState.bHasSelectedFishGuardFish;
	BlueprintState.bCanSelectPreviousFishGuardEntry = ViewState.bCanSelectPreviousFishGuardEntry;
	BlueprintState.bCanSelectNextFishGuardEntry = ViewState.bCanSelectNextFishGuardEntry;
	BlueprintState.bCanSubmitSelectedFishGuardAction = ViewState.bCanSubmitSelectedFishGuardAction;
	BlueprintState.bFishGuardActionPending = ViewState.bFishGuardActionPending;
	BlueprintState.PendingFishGuardAction = ViewState.PendingFishGuardAction;
	BlueprintState.PendingFishGuardRequestId = ViewState.PendingFishGuardRequestId;
	BlueprintState.LastFishGuardAction = ViewState.LastFishGuardAction;
	BlueprintState.LastFishGuardCommandResult = ViewState.LastFishGuardCommandResult;
	BlueprintState.bHasFishGuardCommandResult = ViewState.bHasFishGuardCommandResult;
	BlueprintState.LastFishGuardSacrificeResult = ViewState.LastFishGuardSacrificeResult;
	BlueprintState.LastFishGuardConsumeResult = ViewState.LastFishGuardConsumeResult;
	BlueprintState.FishCollectionEntries = ViewState.FishCollectionEntries;
	if (BlueprintState.FishCollectionEntries.IsEmpty() && !ViewState.FishCollection.IsEmpty())
	{
		BlueprintState.FishCollectionEntries.Reserve(ViewState.FishCollection.Num());
		for (const FCatFishCollectionRecord& Record : ViewState.FishCollection)
		{
			FCatUIReachFishCollectionEntry Entry;
			Entry.FishDefinitionId = Record.FishDefinitionId;
			Entry.State = Record.State;
			Entry.BestWeightKilograms = Record.BestWeightKilograms;
			Entry.EncounterCount = Record.EncounterCount;
			BlueprintState.FishCollectionEntries.Add(Entry);
		}
	}
	BlueprintState.bFishCollectionAvailable = ViewState.bFishCollectionAvailable;
	BlueprintState.bMenuOpen = ViewState.bMenuOpen;
	BlueprintState.MenuToggleKeyName = ViewState.MenuToggleKeyName;
	BlueprintState.bCanRequestOnlineLeave = ViewState.bCanRequestOnlineLeave;
	return BlueprintState;
}
