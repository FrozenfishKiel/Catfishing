#include "UI/Inventory/CatInventoryWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/WrapBox.h"
#include "Engine/LocalPlayer.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "UI/CatUISettings.h"
#include "UI/Inventory/CatInventoryModel.h"
#include "UI/Inventory/CatInventoryPageController.h"
#include "UI/InventorySlot/CatInventorySlotWidget.h"

namespace
{
	// 格子身份比较流程：Widget 只把同一页自己的 Slots 标成本地选中；来源不同但局部下标相同的格子不能互相命中。
	bool IsSameWidgetSlotIdentity(const FCatInventorySlotView& Left, const FCatInventorySlotView& Right)
	{
		if (Left.SlotSource != Right.SlotSource)
		{
			return false;
		}
		switch (Left.SlotSource)
		{
		case ECatInventorySlotSource::InventoryObject:
			return Left.InventorySlotIndex != INDEX_NONE
				&& Left.InventorySlotIndex == Right.InventorySlotIndex;
		case ECatInventorySlotSource::ContainerObject:
			return Left.ContainerId.IsValid()
				&& Left.ContainerId == Right.ContainerId
				&& Left.ContainerKind == Right.ContainerKind
				&& Left.ContainerSlotIndex != INDEX_NONE
				&& Left.ContainerSlotIndex == Right.ContainerSlotIndex;
		case ECatInventorySlotSource::CampInventoryObject:
			return Left.CampInventorySlotIndex != INDEX_NONE
				&& Left.CampInventorySlotIndex == Right.CampInventorySlotIndex;
		case ECatInventorySlotSource::Unknown:
		default:
			return false;
		}
	}

	// 本页选择定位流程：只从当前 WBP 自己保存的格子身份里找高亮；Model 不再保存或广播任何 UI 选择。
	int32 FindLocalSelectionIndex(const TArray<FCatInventorySlotView>& Slots,
		const FCatInventorySlotView& SelectionIdentity, const bool bHasSelection)
	{
		if (!bHasSelection)
		{
			return INDEX_NONE;
		}
		for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
		{
			if (IsSameWidgetSlotIdentity(Slots[SlotIndex], SelectionIdentity))
			{
				return SlotIndex;
			}
		}
		return INDEX_NONE;
	}

	// 本地选择清理流程：Model 原始投影不带选择；页面每次渲染前先清空副本，避免上一格高亮混进新的数据源。
	void ClearLocalSelectionFromViewState(FCatInventoryViewState& State)
	{
		State.SelectedSlot = FCatInventorySlotView();
		State.bHasSelectedSlot = false;
		State.SelectedObject = FCatContainedObjectInstance();
		State.bHasSelectedObject = false;
		State.bSelectedObjectInFishGuard = false;
		State.SelectedFish = FCatFishInstance();
		State.bHasSelectedFish = false;
		State.bSelectedFishInFishGuard = false;
		State.bCanSubmitAction = false;
	}

	// 格子详情压缩流程：SlotView 已经带有 Model 投影好的显示文本，Widget 只把换行压成一行，避免再复制一套数据解释规则。
	FString MakeCompactSlotDisplayText(const FCatInventorySlotView& SelectedSlot)
	{
		FString SlotText = SelectedSlot.DisplayText.ToString();
		SlotText.ReplaceInline(TEXT("\r\n"), TEXT("，"));
		SlotText.ReplaceInline(TEXT("\n"), TEXT("，"));
		return SlotText.IsEmpty() ? FString(TEXT("未知格子")) : SlotText;
	}

	// 本地选择说明流程：只把当前页命中的 SlotView 文本展示出来；它不写 Model，也不会让同屏其他库存页跟着换选中态。
	FText MakeLocalSelectedSlotText(const FCatInventoryViewState& State, const FCatInventorySlotView& SelectedSlot)
	{
		const FString SlotText = MakeCompactSlotDisplayText(SelectedSlot);
		if (SelectedSlot.SlotSource == ECatInventorySlotSource::InventoryObject)
		{
			return SelectedSlot.bOccupied
				? FText::FromString(FString::Printf(TEXT("选中：%s；拖拽可整理或转移。"), *SlotText))
				: FText::FromString(FString::Printf(TEXT("选中：%s。"), *SlotText));
		}
		if (SelectedSlot.SlotSource == ECatInventorySlotSource::CampInventoryObject)
		{
			return SelectedSlot.bOccupied
				? FText::FromString(FString::Printf(TEXT("选中：%s；右键取到随身库存，也可拖到背包格。"), *SlotText))
				: FText::FromString(FString::Printf(TEXT("选中：%s。"), *SlotText));
		}
		if (SelectedSlot.SlotSource == ECatInventorySlotSource::ContainerObject)
		{
			const TCHAR* HintLabel = State.bHasExternalContainers
				? TEXT("拖到其他格子可整理或跨容器移动")
				: TEXT("拖到其他鱼护格子可整理");
			return SelectedSlot.bOccupied
				? FText::FromString(FString::Printf(TEXT("选中：%s；%s。"), *SlotText, HintLabel))
				: FText::FromString(FString::Printf(TEXT("选中：%s。"), *SlotText));
		}
		return State.SelectedFishText;
	}

	// 本地选择叠加流程：页面把自己的选择写进 ViewState 副本和按钮状态；共享 Model 仍保持无选择。
	void ApplyLocalSelectionToViewState(FCatInventoryViewState& State, const FCatInventorySlotView& SelectedSlot)
	{
		ClearLocalSelectionFromViewState(State);
		State.SelectedSlot = SelectedSlot;
		State.bHasSelectedSlot = true;
		State.bHasSelectedObject = SelectedSlot.SlotSource == ECatInventorySlotSource::ContainerObject
			&& SelectedSlot.bOccupied
			&& SelectedSlot.ObjectKind != ECatContainedObjectKind::Unknown
			&& SelectedSlot.ObjectInstanceId.IsValid();
		State.bSelectedObjectInFishGuard = State.bHasSelectedObject
			&& SelectedSlot.ContainerKind == ECatContainerKind::FishGuard;
		if (State.bHasSelectedObject)
		{
			State.SelectedObject = SelectedSlot.Object;
		}
		State.bHasSelectedFish = SelectedSlot.SlotSource == ECatInventorySlotSource::ContainerObject
			&& SelectedSlot.bOccupied
			&& SelectedSlot.ObjectKind == ECatContainedObjectKind::Fish
			&& SelectedSlot.Fish.FishInstanceId.IsValid();
		State.bSelectedFishInFishGuard = State.bHasSelectedFish
			&& SelectedSlot.ContainerKind == ECatContainerKind::FishGuard;
		if (State.bHasSelectedFish)
		{
			State.SelectedFish = SelectedSlot.Fish;
		}
		State.bCanSubmitAction = State.bOpen && State.bSelectedFishInFishGuard && !State.bActionPending;
		State.SelectedFishText = MakeLocalSelectedSlotText(State, SelectedSlot);
	}
}

// 进入视口绑定流程：
// 1. 先对可选按钮执行 Remove/Add 配对，保证蓝图重建后点击出口仍只有一份。
// 2. 再兜底解析库存格 WBP 类，让每个库存页面都能独立创建自己的格子。
// 3. 最后绑定当前 LocalPlayer 的库存 Model 并直接读取最新 ViewState 刷新本页。
void UCatInventoryWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	if (CloseButton)
	{
		CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCloseClicked);
		CloseButton->OnClicked.AddDynamic(this, &ThisClass::HandleCloseClicked);
	}
	if (ConsumeFishButton)
	{
		ConsumeFishButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleConsumeClicked);
		ConsumeFishButton->OnClicked.AddDynamic(this, &ThisClass::HandleConsumeClicked);
	}
	if (SacrificeFishButton)
	{
		SacrificeFishButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleSacrificeClicked);
		SacrificeFishButton->OnClicked.AddDynamic(this, &ThisClass::HandleSacrificeClicked);
	}
	if (!InventorySlotWidgetClass)
	{
		if (const UCatUISettings* Settings = GetDefault<UCatUISettings>())
		{
			InventorySlotWidgetClass = Settings->LoadInventorySlotWidgetClass();
		}
	}
	BindInventoryModel(ResolveInventoryModel());
}

// 销毁流程：
// 1. 先解除动态格子委托，避免已移除槽位在本次移出视口后继续回调本页。
// 2. 再从当前 Model 广播上移除本页监听；页面下次构建会重新解析当前 LocalPlayer 的 Model。
// 3. 最后解除本 View 绑定到可选按钮上的 UMG 点击事件，外部 PageController 不再保存本页委托句柄。
void UCatInventoryWidget::NativeDestruct()
{
	UnbindSlotWidgets();
	UnbindInventoryModel();
	if (CloseButton)
	{
		CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCloseClicked);
	}
	if (ConsumeFishButton)
	{
		ConsumeFishButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleConsumeClicked);
	}
	if (SacrificeFishButton)
	{
		SacrificeFishButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleSacrificeClicked);
	}
	Super::NativeDestruct();
}

// 预览键盘流程：先于子按钮和格子消费关闭键；命中后把关闭意图交给当前库存 PageController 处理输入恢复。
FReply UCatInventoryWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (ShouldCloseInventoryFromKey(InKeyEvent))
	{
		RequestCloseInventory();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

// 键盘流程：库存根页拿到焦点时复用同一关闭键判断；预览未命中的按键继续保持默认传播。
FReply UCatInventoryWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (ShouldCloseInventoryFromKey(InKeyEvent))
	{
		RequestCloseInventory();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

// 渲染流程：
// 1. 接收 Model 的最新只读投影并保存成蓝图可读状态。
// 2. 从本 WBP 对应的数据源取自己的 DisplayedSlots，并先清掉上一轮本地高亮。
// 3. 用本页保存的格子身份在自己的 Slots 中复核；命中时只改本页 ViewState 副本，失效时只清本页选择。
// 4. 把本页渲染后的 Slots 写回对应数组，让蓝图扩展和 Slot Widget 看到同一份本地结果。
// 5. 把文本和按钮状态写入本页 Designer 字段，再刷新自己的 WrapBox；其他库存 WBP 各自处理自己的选择。
void UCatInventoryWidget::RenderInventory(const FCatInventoryViewState& ViewState)
{
	LastInventoryViewState = ViewState;
	DisplayedSlots = GetInventorySlotsForWidget(ViewState);
	for (FCatInventorySlotView& DisplayedSlot : DisplayedSlots)
	{
		DisplayedSlot.bSelected = false;
	}
	ClearLocalSelectionFromViewState(LastInventoryViewState);
	const int32 LocalSelectionIndex = FindLocalSelectionIndex(DisplayedSlots,
		LocalSelectedSlotIdentity, bHasLocalSelectedSlotIdentity);
	if (DisplayedSlots.IsValidIndex(LocalSelectionIndex))
	{
		DisplayedSlots[LocalSelectionIndex].bSelected = true;
		LocalSelectedSlotIdentity = DisplayedSlots[LocalSelectionIndex];
		ApplyLocalSelectionToViewState(LastInventoryViewState, DisplayedSlots[LocalSelectionIndex]);
	}
	else
	{
		LocalSelectedSlotIdentity = FCatInventorySlotView();
		bHasLocalSelectedSlotIdentity = false;
	}
	StoreDisplayedSlotsInViewState(LastInventoryViewState, DisplayedSlots);
	BlueprintSummaryText = LastInventoryViewState.SummaryText;
	BlueprintEquipmentText = LastInventoryViewState.EquipmentText;
	BlueprintInventoryItemsText = LastInventoryViewState.InventoryItemsText;
	BlueprintSelectedFishText = LastInventoryViewState.SelectedFishText;
	BlueprintResultText = LastInventoryViewState.ResultText;
	if (SummaryTextBlock)
	{
		SummaryTextBlock->SetText(BlueprintSummaryText);
	}
	if (EquipmentTextBlock)
	{
		EquipmentTextBlock->SetText(BlueprintEquipmentText);
	}
	if (InventoryItemsTextBlock)
	{
		InventoryItemsTextBlock->SetText(BlueprintInventoryItemsText);
	}
	if (SelectedFishTextBlock)
	{
		SelectedFishTextBlock->SetText(BlueprintSelectedFishText);
	}
	if (ResultTextBlock)
	{
		ResultTextBlock->SetText(BlueprintResultText);
	}
	const bool bActionEnabled = LastInventoryViewState.bCanSubmitAction;
	if (ConsumeFishButton)
	{
		ConsumeFishButton->SetIsEnabled(bActionEnabled);
	}
	if (SacrificeFishButton)
	{
		SacrificeFishButton->SetIsEnabled(bActionEnabled);
	}
	RebuildSlotWidgets();
	BP_RenderInventory(LastInventoryViewState);
}

// 格子 WBP 类设置流程：保存外部指定的 Slot 类；如果本页已经有显示数据，立即用同一份 ViewState 重建自己的格子。
void UCatInventoryWidget::SetInventorySlotWidgetClass(TSubclassOf<UCatInventorySlotWidget> InSlotWidgetClass)
{
	if (InventorySlotWidgetClass == InSlotWidgetClass)
	{
		return;
	}
	InventorySlotWidgetClass = InSlotWidgetClass;
	RebuildSlotWidgets();
}

// Model 绑定流程：
// 1. 同一个 Model 重复绑定时只重读当前 ViewState，覆盖 WBP 先构建后收到外部绑定的顺序。
// 2. 换 Model 时先从上一广播解绑，再监听新 Model 的变化广播。
// 3. 绑定完成后直接从新 Model 读取当前投影刷新本页，避免等待下一次库存变化。
void UCatInventoryWidget::BindInventoryModel(UCatInventoryModel* InModel)
{
	if (BoundInventoryModel.Get() == InModel)
	{
		RefreshInventoryViewFromModel();
		return;
	}
	UnbindInventoryModel();
	BoundInventoryModel = InModel;
	if (InModel)
	{
		InventoryModelViewChangedHandle = InModel->OnViewStateChanged.AddUObject(
			this, &ThisClass::HandleInventoryModelViewStateChanged);
		RefreshInventoryViewFromModel();
	}
}

// Model 解绑流程：只移除本页注册的广播句柄并清弱引用；Model 生命周期仍由 LocalPlayer UI 子系统维护。
void UCatInventoryWidget::UnbindInventoryModel()
{
	if (UCatInventoryModel* Model = BoundInventoryModel.Get())
	{
		Model->OnViewStateChanged.Remove(InventoryModelViewChangedHandle);
	}
	InventoryModelViewChangedHandle.Reset();
	BoundInventoryModel.Reset();
}

// 关闭请求流程：库存 Widget 只表达玩家意图；输入模式、鼠标和页面状态仍由当前 PageController 成对处理。
void UCatInventoryWidget::RequestCloseInventory()
{
	if (UCatInventoryPageController* PageController = ResolveInventoryPageController())
	{
		PageController->RequestCloseInventoryFromWidget();
	}
}

// 选择请求流程：蓝图传来的数字只在本页 DisplayedSlots 中查找；找到后进入本页本地选择，避免跨库存复用下标。
void UCatInventoryWidget::RequestSelectSlot(const int32 SlotIndex)
{
	if (DisplayedSlots.IsValidIndex(SlotIndex))
	{
		RequestSelectSlotView(DisplayedSlots[SlotIndex]);
	}
}

// 吃鱼请求流程：Widget 只提交本页当前选择身份；鱼实例、容器 ID 和 Revision 仍由 PageController 从最新 Model 快照复核。
void UCatInventoryWidget::RequestConsumeSelectedFish()
{
	if (UCatInventoryPageController* PageController = ResolveInventoryPageController())
	{
		const FCatInventorySlotView SelectedSlot = LastInventoryViewState.bHasSelectedSlot
			? LastInventoryViewState.SelectedSlot : FCatInventorySlotView();
		PageController->RequestInventoryActionFromWidget(ECatInventoryAction::ConsumeSelectedFish, SelectedSlot);
	}
}

// 献祭请求流程：Widget 只提交本页当前选择身份；献祭命令不在蓝图或 Widget 里组装。
void UCatInventoryWidget::RequestSacrificeSelectedFish()
{
	if (UCatInventoryPageController* PageController = ResolveInventoryPageController())
	{
		const FCatInventorySlotView SelectedSlot = LastInventoryViewState.bHasSelectedSlot
			? LastInventoryViewState.SelectedSlot : FCatInventorySlotView();
		PageController->RequestInventoryActionFromWidget(ECatInventoryAction::SacrificeSelectedFish, SelectedSlot);
	}
}

// 状态读取流程：返回本页最近一次渲染后的 ViewState 副本；蓝图只读展示当前 WBP 自己的选择和动作表现。
const FCatInventoryViewState& UCatInventoryWidget::GetLastInventoryViewState() const
{
	return LastInventoryViewState;
}

// 数据源选择流程：普通库存页只读取随身背包 Slots；营地仓库和鱼护页面通过派生类改成自己的独立 Slots。
const TArray<FCatInventorySlotView>& UCatInventoryWidget::GetInventorySlotsForWidget(
	const FCatInventoryViewState& ViewState) const
{
	return ViewState.InventorySlots;
}

// 本页 Slots 回写流程：普通库存页把本地高亮后的数组写回随身库存副本；派生页覆盖到自己的数据源数组。
void UCatInventoryWidget::StoreDisplayedSlotsInViewState(FCatInventoryViewState& ViewState,
	const TArray<FCatInventorySlotView>& Slots) const
{
	ViewState.InventorySlots = Slots;
}

// WrapBox 格子刷新流程：
// 1. 如果格子数量、Widget 类和所在位置都没变，就只把最新 SlotView 写回现有 Slot Widget，避免普通选择时重建 WrapBox 打断同一次鼠标输入。
// 2. 数量变化或已存在 Widget 缺失时，才解绑并清空本页的单一 WrapBox，再按本页 DisplayedSlots 创建格子。
// 3. 本页只管理自己 Designer 里绑定的 WrapBox；同屏多个库存 WBP 也各自刷新各自的格子。
void UCatInventoryWidget::RebuildSlotWidgets()
{
	if (!InventorySlotWidgetClass || !InventorySlotWrapBox)
	{
		UnbindSlotWidgets();
		return;
	}
	bool bCanRefreshExistingSlots = BoundSlotWidgets.Num() == DisplayedSlots.Num()
		&& InventorySlotWrapBox->GetChildrenCount() == BoundSlotWidgets.Num();
	const UClass* ExpectedSlotClass = InventorySlotWidgetClass.Get();
	for (int32 WidgetIndex = 0; WidgetIndex < BoundSlotWidgets.Num(); ++WidgetIndex)
	{
		const UCatInventorySlotWidget* SlotWidget = BoundSlotWidgets[WidgetIndex];
		if (!SlotWidget || !SlotWidget->IsA(ExpectedSlotClass) || InventorySlotWrapBox->GetChildAt(WidgetIndex) != SlotWidget)
		{
			bCanRefreshExistingSlots = false;
			break;
		}
	}
	if (bCanRefreshExistingSlots)
	{
		for (int32 SlotIndex = 0; SlotIndex < DisplayedSlots.Num(); ++SlotIndex)
		{
			BoundSlotWidgets[SlotIndex]->RenderSlot(DisplayedSlots[SlotIndex]);
		}
		return;
	}
	UnbindSlotWidgets();
	InventorySlotWrapBox->ClearChildren();
	for (const FCatInventorySlotView& SlotView : DisplayedSlots)
	{
		UCatInventorySlotWidget* SlotWidget = CreateWidget<UCatInventorySlotWidget>(GetOwningPlayer(), InventorySlotWidgetClass);
		if (!SlotWidget)
		{
			continue;
		}
		SlotWidget->OnSlotSelected.AddUObject(this, &ThisClass::HandleSlotSelected);
		SlotWidget->OnContextRequested.AddUObject(this, &ThisClass::HandleSlotContextRequested);
		SlotWidget->OnSlotDropRequested.AddUObject(this, &ThisClass::HandleSlotDropRequested);
		SlotWidget->RenderSlot(SlotView);
		InventorySlotWrapBox->AddChildToWrapBox(SlotWidget);
		BoundSlotWidgets.Add(SlotWidget);
	}
}

// Model 解析流程：每个库存 WBP 实例都从 owning LocalPlayer 找当前正式库存 Model。
UCatInventoryModel* UCatInventoryWidget::ResolveInventoryModel() const
{
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	UCatLocalPlayerUISubsystem* UISubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<UCatLocalPlayerUISubsystem>() : nullptr;
	return UISubsystem ? UISubsystem->GetInventoryModel() : nullptr;
}

// Model 刷新流程：从本页已绑定 Model 读取当前投影并渲染；没有 Model 时不构造空白状态覆盖蓝图预览。
void UCatInventoryWidget::RefreshInventoryViewFromModel()
{
	if (const UCatInventoryModel* Model = BoundInventoryModel.Get())
	{
		RenderInventory(Model->GetViewState());
	}
}

// Model 广播流程：广播本身不携带额外数据，本页收到后只重读当前 Model 的最新 ViewState。
void UCatInventoryWidget::HandleInventoryModelViewStateChanged()
{
	RefreshInventoryViewFromModel();
}

// PageController 解析流程：Widget 不保存 Controller；玩家动作发生时从 owning LocalPlayer 找当前库存页面控制器。
UCatInventoryPageController* UCatInventoryWidget::ResolveInventoryPageController() const
{
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	UCatLocalPlayerUISubsystem* UISubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<UCatLocalPlayerUISubsystem>() : nullptr;
	return UISubsystem ? UISubsystem->GetInventoryPageController() : nullptr;
}

// 格子解绑流程：逐个移除本对象绑定的原生委托，再清本地数组；格子对象释放交给 UMG 生命周期处理。
void UCatInventoryWidget::UnbindSlotWidgets()
{
	for (UCatInventorySlotWidget* SlotWidget : BoundSlotWidgets)
	{
		if (!SlotWidget)
		{
			continue;
		}
		SlotWidget->OnSlotSelected.RemoveAll(this);
		SlotWidget->OnContextRequested.RemoveAll(this);
		SlotWidget->OnSlotDropRequested.RemoveAll(this);
	}
	BoundSlotWidgets.Reset();
}

// 选择转交流程：只在本页 DisplayedSlots 中复核并保存本地身份，然后用本页缓存数据重渲染自己；共享 Model 不知道这次点击。
void UCatInventoryWidget::RequestSelectSlotView(const FCatInventorySlotView& SlotView)
{
	const int32 LocalSelectionIndex = FindLocalSelectionIndex(DisplayedSlots, SlotView, true);
	if (!DisplayedSlots.IsValidIndex(LocalSelectionIndex))
	{
		return;
	}
	LocalSelectedSlotIdentity = DisplayedSlots[LocalSelectionIndex];
	bHasLocalSelectedSlotIdentity = true;
	RenderInventory(LastInventoryViewState);
}

// 关闭键判断流程：
// 1. 先要求库存处于打开投影，避免构造或移出视口期间的迟到按键误触发关闭。
// 2. Escape 始终作为模态 UI 兜底关闭键；普通背包再接受配置里的背包开关键。
// 3. 鱼护、鱼缸和营地公共仓库这类由交互键打开的页面，也接受同一个交互键再次关闭。
bool UCatInventoryWidget::ShouldCloseInventoryFromKey(const FKeyEvent& InKeyEvent) const
{
	if (!LastInventoryViewState.bOpen)
	{
		return false;
	}
	const FName PressedKeyName = InKeyEvent.GetKey().GetFName();
	if (PressedKeyName == EKeys::Escape.GetFName())
	{
		return true;
	}
	if (!LastInventoryViewState.ToggleKeyName.IsNone() && PressedKeyName == LastInventoryViewState.ToggleKeyName)
	{
		return true;
	}
	if (LastInventoryViewState.bHasExternalContainers || LastInventoryViewState.bHasCampInventory)
	{
		const UCatUISettings* Settings = GetDefault<UCatUISettings>();
		const FName InteractionKeyName = Settings ? Settings->ResolveInteractionConfirmKeyName() : NAME_None;
		return !InteractionKeyName.IsNone() && PressedKeyName == InteractionKeyName;
	}
	return false;
}

// 格子点击流程：动态 Slot Widget 直接交出自己的来源身份，本页不把局部下标扩展成全局概念。
void UCatInventoryWidget::HandleSlotSelected(const FCatInventorySlotView& SlotView)
{
	RequestSelectSlotView(SlotView);
}

// 格子上下文流程：右键意图直接交给 PageController；选择同步和服务器命令分流都基于格子所属数据源完成。
void UCatInventoryWidget::HandleSlotContextRequested(const FCatInventorySlotView& SlotView)
{
	if (UCatInventoryPageController* PageController = ResolveInventoryPageController())
	{
		PageController->RequestInventorySlotContextFromWidget(SlotView);
	}
}

// 格子 Drop 流程：只复制源/目标快照并提交移动意图；不在 Drop 中先选中或刷新，避免同一次鼠标事件里重建同屏库存格。
void UCatInventoryWidget::HandleSlotDropRequested(const FCatInventorySlotView& SourceSlot,
	const FCatInventorySlotView& TargetSlot)
{
	const FCatInventorySlotView SourceSlotCopy = SourceSlot;
	const FCatInventorySlotView TargetSlotCopy = TargetSlot;
	if (UCatInventoryPageController* PageController = ResolveInventoryPageController())
	{
		PageController->RequestInventorySlotDropFromWidget(SourceSlotCopy, TargetSlotCopy);
	}
}

// 关闭按钮流程：收口到统一关闭请求，避免 WBP 图表和按钮绑定产生两套出口。
void UCatInventoryWidget::HandleCloseClicked()
{
	RequestCloseInventory();
}

// 吃鱼按钮流程：收口到统一吃鱼请求。
void UCatInventoryWidget::HandleConsumeClicked()
{
	RequestConsumeSelectedFish();
}

// 献祭按钮流程：收口到统一献祭请求。
void UCatInventoryWidget::HandleSacrificeClicked()
{
	RequestSacrificeSelectedFish();
}
