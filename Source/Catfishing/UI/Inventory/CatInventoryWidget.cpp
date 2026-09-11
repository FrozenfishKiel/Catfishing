#include "UI/Inventory/CatInventoryWidget.h"

#include "Character/CatCharacter.h"
#include "Components/Button.h"
#include "Components/SpinBox.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Components/WrapBox.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/CatInventoryStatics.h"
#include "Logging/CatLog.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "UI/CatUISettings.h"
#include "UI/Inventory/CatInventoryModel.h"
#include "UI/Inventory/CatInventoryPageController.h"
#include "UI/InventorySlot/CatInventorySlotWidget.h"

// 先移除原 Model 监听；切换来源时清理旧请求与结果，再绑定新库存并重读列表，避免迟到回执污染另一份库存。
void UCatInventoryWidget::SetInventoryContext(UCatInventoryComponent* InInventory)
{
	UnbindInventoryModel();
	if (DisplayInventory.Get() != InInventory)
	{
		PendingCommandRequestId.Invalidate();
		if (InventoryActionResultText) { InventoryActionResultText->SetText(FText::GetEmpty()); }
	}
	DisplayInventory = InInventory;
	if (InInventory)
	{
		InventoryModelChangedHandle = InInventory->GetInventoryModel()->OnInventoryListChanged.AddUObject(
			this, &ThisClass::RefreshInventorySlots);
	}
	RefreshInventorySlots();
}

// 本页只暴露构建或打开时确定的库存组件；没有上下文就返回空，避免 UI 根据营地、鱼护等页面种类重新选择数据源。
UCatInventoryComponent* UCatInventoryWidget::GetInventoryContext() const
{
	return DisplayInventory.Get();
}

// 仅保存本页格子类；构建或库存数据通知到达时由同一刷新入口使用。
void UCatInventoryWidget::SetInventorySlotWidgetClass(const TSubclassOf<UCatInventorySlotWidget> InSlotWidgetClass)
{
	InventorySlotWidgetClass = InSlotWidgetClass;
}

// 先清理旧请求并从原控制器重绑回执，再绑定按钮与数据源；已有上下文时保留，未注入的背包只解析 owning Pawn，最后刷新列表。
void UCatInventoryWidget::NativeConstruct()
{
	Super::NativeConstruct();
	PendingCommandRequestId.Invalidate();
	if (InventoryActionResultText) { InventoryActionResultText->SetText(FText::GetEmpty()); }
	if (ACatfishingPlayerController* PreviousController = CommandResultController.Get())
	{
		PreviousController->OnCampCommandResultReceived.RemoveAll(this);
	}
	CommandResultController = Cast<ACatfishingPlayerController>(GetOwningPlayer());
	if (ACatfishingPlayerController* Controller = CommandResultController.Get())
	{
		Controller->OnCampCommandResultReceived.AddUObject(this, &ThisClass::HandleInventoryCommandResult);
	}
	if (CloseButton)
	{
		CloseButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleCloseClicked);
	}
	if (ConsumeFishButton)
	{
		ConsumeFishButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleConsumeClicked);
	}
	if (DropButton) { DropButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleDropClicked); }
	if (PlaceButton) { PlaceButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandlePlaceClicked); }
	if (ReleaseQuantityConfirmButton) { ReleaseQuantityConfirmButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleReleaseQuantityConfirmed); }
	if (ReleaseQuantityCancelButton) { ReleaseQuantityCancelButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleReleaseQuantityCancelled); }
	ResetPendingRelease();
	if (!InventorySlotWidgetClass)
	{
		InventorySlotWidgetClass = GetDefault<UCatUISettings>()->LoadInventorySlotWidgetClass();
	}
	UCatInventoryComponent* Inventory = DisplayInventory.Get();
	if (!Inventory)
	{
		if (const ACatCharacter* Character = Cast<ACatCharacter>(GetOwningPlayerPawn()))
		{
			Inventory = Character->GetInventoryComponent();
		}
	}
	SetInventoryContext(Inventory);
}

// 先从原控制器解绑回执并清理请求，再移除 Model、格子和按钮监听及数量状态；显示库存保留，最后交给 UMG 结束生命周期。
void UCatInventoryWidget::NativeDestruct()
{
	if (ACatfishingPlayerController* Controller = CommandResultController.Get())
	{
		Controller->OnCampCommandResultReceived.RemoveAll(this);
	}
	CommandResultController.Reset();
	PendingCommandRequestId.Invalidate();
	UnbindInventoryModel();
	UnbindSlotWidgets();
	if (CloseButton)
	{
		CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCloseClicked);
	}
	if (ConsumeFishButton)
	{
		ConsumeFishButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleConsumeClicked);
	}
	if (DropButton) { DropButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleDropClicked); }
	if (PlaceButton) { PlaceButton->OnClicked.RemoveDynamic(this, &ThisClass::HandlePlaceClicked); }
	if (ReleaseQuantityConfirmButton) { ReleaseQuantityConfirmButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleReleaseQuantityConfirmed); }
	if (ReleaseQuantityCancelButton) { ReleaseQuantityCancelButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleReleaseQuantityCancelled); }
	ResetPendingRelease();
	Super::NativeDestruct();
}

// 按 AOBackPackUI 的列表刷新方式清空本页格子，再逐条设置库存、下标和条目；子库存面板有自己的 Model 和 WrapBox。
void UCatInventoryWidget::RefreshInventorySlots()
{
	// 库存通知意味着槽位可能已移动、合并或换物；先废弃数量面板的旧快照，再按最新 Model 重建显示。
	ResetPendingRelease();
	UnbindSlotWidgets();
	SelectedSlotIndex = INDEX_NONE;
	if (ConsumeFishButton)
	{
		ConsumeFishButton->SetIsEnabled(false);
	}
	if (DropButton) { DropButton->SetIsEnabled(false); }
	if (PlaceButton) { PlaceButton->SetIsEnabled(false); }
	if (!InventorySlotWrapBox)
	{
		return;
	}
	InventorySlotWrapBox->ClearChildren();
	UCatInventoryComponent* Inventory = DisplayInventory.Get();
	if (!Inventory || !InventorySlotWidgetClass)
	{
		return;
	}
	const TArray<FCatInventoryEntry>& Entries = Inventory->GetInventoryModel()->GetInventoryList();
	// 槽位下标来自 Model；个别 WBP 创建失败仍保留空位，不能让后续格位整体错位。
	SlotWidgets.SetNum(Entries.Num());
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		UCatInventorySlotWidget* SlotWidget = CreateWidget<UCatInventorySlotWidget>(GetOwningPlayer(), InventorySlotWidgetClass);
		if (!SlotWidget)
		{
			continue;
		}
		SlotWidget->SetSlotContext(Index, Inventory, Entries[Index]);
		SlotWidget->OnSlotSelected.AddUObject(this, &ThisClass::RequestSelectSlot);
		InventorySlotWrapBox->AddChildToWrapBox(SlotWidget);
		SlotWidgets[Index] = SlotWidget;
	}
}

// 句柄始终从原显示库存的 Model 移除；库存已销毁时只清句柄，不创建替代数据源。
void UCatInventoryWidget::UnbindInventoryModel()
{
	if (InventoryModelChangedHandle.IsValid())
	{
		if (UCatInventoryComponent* Inventory = DisplayInventory.Get())
		{
			Inventory->GetInventoryModel()->OnInventoryListChanged.Remove(InventoryModelChangedHandle);
		}
		InventoryModelChangedHandle.Reset();
	}
}

// 逐个撤销本页格子的悬停来源和选择监听，再释放引用；先撤销提示才能保证重建期间不显示旧实例。
void UCatInventoryWidget::UnbindSlotWidgets()
{
	for (UCatInventorySlotWidget* SlotWidget : SlotWidgets)
	{
		if (SlotWidget)
		{
			SlotWidget->CancelTooltip();
			SlotWidget->OnSlotSelected.RemoveAll(this);
		}
	}
	SlotWidgets.Reset();
}

// 点击只保存本页使用按钮所指的格位，并按是否有物品更新按钮；不修改 Model 或重建格子，因此不会中断后续拖放。
void UCatInventoryWidget::RequestSelectSlot(const int32 SlotIndex)
{
	// 新的手动选择会改变用户操作对象；数量面板若仍指向旧格必须立即失效，不能把确认意图带到新选择上。
	if (PendingReleaseInventory.IsValid())
	{
		ResetPendingRelease();
	}
	SelectedSlotIndex = SlotWidgets.IsValidIndex(SlotIndex) && SlotWidgets[SlotIndex] ? SlotIndex : INDEX_NONE;
	FCatInventoryEntry Entry;
	int32 EntryIndex = INDEX_NONE;
	const bool bCanAct = !PendingCommandRequestId.IsValid() && GetSelectedInventoryEntry(Entry, EntryIndex);
	if (ConsumeFishButton)
	{
		ConsumeFishButton->SetIsEnabled(bCanAct);
	}
	if (DropButton) { DropButton->SetIsEnabled(bCanAct); }
	if (PlaceButton) { PlaceButton->SetIsEnabled(bCanAct); }
}

// 先取消尚未确认的数量选择；本页没有离库或售鱼请求等待回执且格子有效时，使用按钮转入与右键相同的格子方法。
void UCatInventoryWidget::RequestUseSelectedItem()
{
	ResetPendingRelease();
	if (!PendingCommandRequestId.IsValid() && SlotWidgets.IsValidIndex(SelectedSlotIndex) && SlotWidgets[SelectedSlotIndex])
	{
		SlotWidgets[SelectedSlotIndex]->RequestUseItem();
	}
}

// 丢弃请求流程：读取当前选中格；堆叠物冻结在同页数量面板，单件立即交给统一世界落地 RPC。
void UCatInventoryWidget::RequestDropSelectedItem()
{
	BeginReleaseSelectedItem(ECatInventoryWorldAction::Drop);
}

// 放置请求流程：读取当前选中格；数量选择沿用丢弃面板，服务器据动作类型验证落点并决定是否固定。
void UCatInventoryWidget::RequestPlaceSelectedItem()
{
	BeginReleaseSelectedItem(ECatInventoryWorldAction::Place);
}

// 先取消尚未提交的数量选择，再向 PageController 提交关闭意图；窗口和输入恢复仍由页面控制器负责。
void UCatInventoryWidget::RequestCloseInventory()
{
	ResetPendingRelease();
	if (UCatInventoryPageController* Controller = ResolveInventoryPageController())
	{
		Controller->RequestCloseInventoryFromWidget();
	}
}

// 只为页面关闭解析 owning LocalPlayer 的控制器；Model 数据源由 DisplayInventory 独立确定。
UCatInventoryPageController* UCatInventoryWidget::ResolveInventoryPageController() const
{
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	UCatLocalPlayerUISubsystem* UI = LocalPlayer ? LocalPlayer->GetSubsystem<UCatLocalPlayerUISubsystem>() : nullptr;
	return UI ? UI->GetInventoryPageController() : nullptr;
}

// 关闭条件读取页面控制器的唯一打开状态；外部库存额外接受交互键，普通背包保持背包键与 Escape。
bool UCatInventoryWidget::ShouldCloseInventoryFromKey(const FKeyEvent& InKeyEvent) const
{
	const UCatInventoryPageController* Controller = ResolveInventoryPageController();
	if (!Controller || !Controller->IsInventoryOpen())
	{
		return false;
	}
	const FName Key = InKeyEvent.GetKey().GetFName();
	const UCatUISettings* Settings = GetDefault<UCatUISettings>();
	if (Key == EKeys::Escape.GetFName() || Key == Settings->ResolveInventoryToggleKeyName())
	{
		return true;
	}
	const UCatInventoryComponent* Inventory = DisplayInventory.Get();
	return Inventory && Inventory->GetOwner() != GetOwningPlayerPawn() && Key == Settings->ResolveInteractionConfirmKeyName();
}

// 在子控件消费之前处理关闭键，避免焦点落在格子上后无法关闭整个库存窗口。
FReply UCatInventoryWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (ShouldCloseInventoryFromKey(InKeyEvent))
	{
		RequestCloseInventory();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

// 根控件直接收到按键时复用同一关闭判断；其余输入保持默认传播。
FReply UCatInventoryWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (ShouldCloseInventoryFromKey(InKeyEvent))
	{
		RequestCloseInventory();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

// 选中格读取流程：先清空输出，再按本页选择下标重读当前 Model；不依赖旧格子副本，空格或失效实例不能成为提交目标。
bool UCatInventoryWidget::GetSelectedInventoryEntry(FCatInventoryEntry& OutEntry, int32& OutSlotIndex) const
{
	OutEntry = FCatInventoryEntry();
	OutSlotIndex = INDEX_NONE;
	UCatInventoryComponent* Inventory = DisplayInventory.Get();
	if (!Inventory)
	{
		return false;
	}
	const TArray<FCatInventoryEntry>& Entries = Inventory->GetInventoryModel()->GetInventoryList();
	if (!Entries.IsValidIndex(SelectedSlotIndex) || !IsValid(Entries[SelectedSlotIndex].Instance)
		|| Entries[SelectedSlotIndex].StackCount <= 0)
	{
		return false;
	}
	OutEntry = Entries[SelectedSlotIndex];
	OutSlotIndex = SelectedSlotIndex;
	return true;
}

// 关闭按钮使用公开关闭入口，和键盘关闭共享输入恢复时序。
void UCatInventoryWidget::HandleCloseClicked()
{
	RequestCloseInventory();
}

// 使用按钮直接使用当前选中格，和该格右键共享服务器请求。
void UCatInventoryWidget::HandleConsumeClicked()
{
	RequestUseSelectedItem();
}

// Drop 按钮流程：不自行读取或扣除库存，只把选中格交给统一准备入口决定是否需要数量确认。
void UCatInventoryWidget::HandleDropClicked()
{
	RequestDropSelectedItem();
}

// Place 按钮流程：不生成客户端预览，只把选中格交给统一准备入口冻结本次离库意图。
void UCatInventoryWidget::HandlePlaceClicked()
{
	RequestPlaceSelectedItem();
}

// 数量确认流程：先验证冻结实例仍在原槽位并把输入向下取整到可用范围，再复制来源并清空面板后提交；不匹配时直接取消。
void UCatInventoryWidget::HandleReleaseQuantityConfirmed()
{
	int32 Quantity = 0;
	if (!ResolvePendingRelease(Quantity))
	{
		ResetPendingRelease();
		return;
	}
	UCatInventoryComponent* SourceInventory = PendingReleaseInventory.Get();
	const int32 SourceSlotIndex = PendingReleaseSlotIndex;
	const FGuid ItemInstanceId = PendingReleaseItemInstanceId;
	const ECatInventoryWorldAction Action = PendingReleaseAction;
	ResetPendingRelease();
	SubmitReleaseItem(SourceInventory, SourceSlotIndex, ItemInstanceId, Quantity, Action);
}

// 数量取消流程：只撤销 UI 冻结选择和面板显示；从未向库存或服务器提交任何变更。
void UCatInventoryWidget::HandleReleaseQuantityCancelled()
{
	ResetPendingRelease();
}

// 先取消旧数量并拒绝等待回执期间的新请求，再读取选中实例；单件直接提交，堆叠物仅在确认控件齐备时冻结来源并初始化整数输入。
void UCatInventoryWidget::BeginReleaseSelectedItem(const ECatInventoryWorldAction Action)
{
	ResetPendingRelease();
	if (PendingCommandRequestId.IsValid()) { return; }
	FCatInventoryEntry Entry;
	int32 SlotIndex = INDEX_NONE;
	UCatInventoryComponent* SourceInventory = DisplayInventory.Get();
	if (!SourceInventory || !GetSelectedInventoryEntry(Entry, SlotIndex) || !Entry.Instance || !Entry.Instance->GetItemInstanceId().IsValid())
	{
		return;
	}
	if (Entry.StackCount == 1)
	{
		SubmitReleaseItem(SourceInventory, SlotIndex, Entry.Instance->GetItemInstanceId(), 1, Action);
		return;
	}
	if (!ReleaseQuantityPanel || !ReleaseQuantitySpinBox || !ReleaseQuantityConfirmButton || !ReleaseQuantityCancelButton)
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_inventory_release_rejected World=%s Reason=QuantityControlsMissing View=%s"),
			*GetPathNameSafe(GetWorld()), *GetName());
		return;
	}
	PendingReleaseInventory = SourceInventory;
	PendingReleaseSlotIndex = SlotIndex;
	PendingReleaseItemInstanceId = Entry.Instance->GetItemInstanceId();
	PendingReleaseMaximumQuantity = Entry.StackCount;
	PendingReleaseAction = Action;
	ReleaseQuantitySpinBox->SetMinValue(1.0f);
	ReleaseQuantitySpinBox->SetMaxValue(static_cast<float>(Entry.StackCount));
	ReleaseQuantitySpinBox->SetMinSliderValue(1.0f);
	ReleaseQuantitySpinBox->SetMaxSliderValue(static_cast<float>(Entry.StackCount));
	ReleaseQuantitySpinBox->SetDelta(1.0f);
	ReleaseQuantitySpinBox->SetMinFractionalDigits(0);
	ReleaseQuantitySpinBox->SetMaxFractionalDigits(0);
	ReleaseQuantitySpinBox->SetValue(1.0f);
	ReleaseQuantityPanel->SetVisibility(ESlateVisibility::Visible);
}

// 离库提交流程：本地只生成请求 ID 并交出来源事实；服务器负责重读库存、计算位置、生成世界 Actor 和扣量的原子性。
void UCatInventoryWidget::SubmitReleaseItem(UCatInventoryComponent* SourceInventory, const int32 SourceSlotIndex,
	const FGuid& ItemInstanceId, const int32 Quantity, const ECatInventoryWorldAction Action)
{
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(GetOwningPlayer());
	AActor* SourceHost = SourceInventory ? SourceInventory->GetOwner() : nullptr;
	if (PendingCommandRequestId.IsValid() || !Controller || !IsValid(SourceHost) || SourceHost->IsActorBeingDestroyed()
		|| SourceInventory != DisplayInventory.Get() || SourceSlotIndex < 0 || !ItemInstanceId.IsValid() || Quantity <= 0)
	{
		return;
	}
	const FGuid RequestId = FGuid::NewGuid();
	BeginInventoryCommand(RequestId);
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_inventory_release_submitted World=%s NetMode=%d Authority=%d LocalRole=%d Controller=%s RequestId=%s Source=%s Slot=%d ItemInstanceId=%s Quantity=%d Action=%s"),
		*GetPathNameSafe(GetWorld()), static_cast<int32>(Controller->GetNetMode()), Controller->HasAuthority(),
		static_cast<int32>(Controller->GetLocalRole()), *GetNameSafe(Controller), *RequestId.ToString(),
		*GetPathNameSafe(SourceHost), SourceSlotIndex, *ItemInstanceId.ToString(), Quantity, *UEnum::GetValueAsString(Action));
	Controller->ServerReleaseInventoryItemToWorld(RequestId, SourceHost, SourceSlotIndex, ItemInstanceId, Quantity, Action);
}

// 冻结选择校验流程：先读取冻结库存和槽位，再比对实例 ID 与当前数量；任何库存变化都会让确认失效而非猜测替代物品。
bool UCatInventoryWidget::ResolvePendingRelease(int32& OutQuantity) const
{
	OutQuantity = 0;
	UCatInventoryComponent* SourceInventory = PendingReleaseInventory.Get();
	if (PendingCommandRequestId.IsValid() || !SourceInventory || SourceInventory != DisplayInventory.Get()
		|| PendingReleaseSlotIndex == INDEX_NONE || !PendingReleaseItemInstanceId.IsValid()
		|| PendingReleaseMaximumQuantity <= 0 || !ReleaseQuantitySpinBox)
	{
		return false;
	}
	const TArray<FCatInventoryEntry>& Entries = SourceInventory->GetInventoryModel()->GetInventoryList();
	if (!Entries.IsValidIndex(PendingReleaseSlotIndex))
	{
		return false;
	}
	const FCatInventoryEntry& CurrentEntry = Entries[PendingReleaseSlotIndex];
	if (!CurrentEntry.Instance || CurrentEntry.Instance->GetItemInstanceId() != PendingReleaseItemInstanceId
		|| CurrentEntry.StackCount <= 0)
	{
		return false;
	}
	const float RequestedQuantity = ReleaseQuantitySpinBox->GetValue();
	if (!FMath::IsFinite(RequestedQuantity)) { return false; }
	// 先按双精度裁剪再取整，避免 SpinBox 的 float 在大堆叠上越过 int32 边界。
	OutQuantity = static_cast<int32>(FMath::FloorToDouble(FMath::Clamp(static_cast<double>(RequestedQuantity), 1.0,
		static_cast<double>(FMath::Min(PendingReleaseMaximumQuantity, CurrentEntry.StackCount)))));
	return true;
}

// 先清空来源事实并恢复默认动作与数量一，再折叠可选数量面板；重复调用保持安全，不触碰真实库存。
void UCatInventoryWidget::ResetPendingRelease()
{
	PendingReleaseInventory.Reset();
	PendingReleaseSlotIndex = INDEX_NONE;
	PendingReleaseItemInstanceId.Invalidate();
	PendingReleaseMaximumQuantity = 0;
	PendingReleaseAction = ECatInventoryWorldAction::Drop;
	if (ReleaseQuantitySpinBox) { ReleaseQuantitySpinBox->SetValue(1.0f); }
	if (ReleaseQuantityPanel)
	{
		ReleaseQuantityPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
}

// 提交前先登记关联 ID 并清除上次结果，再重读当前列表以撤销选择和数量；必须早于 RPC，房主可能在调用中同步收到回执。
void UCatInventoryWidget::BeginInventoryCommand(const FGuid& RequestId)
{
	PendingCommandRequestId = RequestId;
	if (InventoryActionResultText) { InventoryActionResultText->SetText(FText::GetEmpty()); }
	RefreshInventorySlots();
}

// 回执先匹配本页请求，忽略其他操作；匹配后解除等待、显示服务器结果并刷新 Model，复制若稍后到达仍由原通知再次刷新。
void UCatInventoryWidget::HandleInventoryCommandResult(const FCatDomainCommandResult& Result)
{
	if (!PendingCommandRequestId.IsValid() || Result.RequestId != PendingCommandRequestId) { return; }
	PendingCommandRequestId.Invalidate();
	const bool bAccepted = CatIsAcceptedDomainCommandResult(Result);
	if (InventoryActionResultText)
	{
		InventoryActionResultText->SetText(bAccepted
			? NSLOCTEXT("Catfishing", "InventoryActionSucceeded", "操作成功")
			: NSLOCTEXT("Catfishing", "InventoryActionRejected", "操作未完成，请重新选择物品"));
	}
	const ACatfishingPlayerController* Controller = CommandResultController.Get();
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_inventory_command_result World=%s NetMode=%d Authority=%d LocalRole=%d Controller=%s RequestId=%s Accepted=%d Error=%s"),
		*GetPathNameSafe(GetWorld()), Controller ? static_cast<int32>(Controller->GetNetMode()) : -1,
		Controller && Controller->HasAuthority(), Controller ? static_cast<int32>(Controller->GetLocalRole()) : -1,
		*GetNameSafe(Controller), *Result.RequestId.ToString(), bAccepted, *UEnum::GetValueAsString(Result.Error));
	RefreshInventorySlots();
}
