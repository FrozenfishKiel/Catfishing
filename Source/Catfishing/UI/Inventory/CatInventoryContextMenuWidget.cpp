#include "UI/Inventory/CatInventoryContextMenuWidget.h"

#include "Components/Button.h"
#include "Components/SpinBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "UI/CatUISettings.h"
#include "UI/Inventory/CatInventoryPageController.h"

// 动作设置流程：保存定义给出的稳定标识并把点击委托绑定到本行；同一行不会重绑或推断其它动作。
void UCatInventoryContextActionButton::SetAction(const FGameplayTag InAction)
{
	Action = InAction;
	OnClicked.RemoveDynamic(this, &ThisClass::HandleClicked);
	OnClicked.AddDynamic(this, &ThisClass::HandleClicked);
}

// 动作读取流程：返回创建时冻结的定义标签；空值表示该动态行尚未完成菜单配置。
FGameplayTag UCatInventoryContextActionButton::GetAction() const
{
	return Action;
}

// 行点击流程：只在标识有效时通知菜单；数量和服务器提交仍由更高层统一处理。
void UCatInventoryContextActionButton::HandleClicked()
{
	if (Action.IsValid()) { OnActionRowChosen.ExecuteIfBound(Action); }
}

// 菜单构建流程：先折叠数量确认区，再绑定作者器提供的两个按钮；动态动作行由 Present 在有来源后创建。
void UCatInventoryContextMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	if (QuantityConfirmButton) { QuantityConfirmButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleQuantityConfirmed); }
	if (QuantityCancelButton) { QuantityCancelButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleQuantityCancelled); }
	ResetPresentation();
	SetVisibility(ESlateVisibility::Collapsed);
}

// 菜单销毁流程：先移除动态按钮和冻结数据，再解除设计器按钮回调，避免重建页面时保留旧来源的动作。
void UCatInventoryContextMenuWidget::NativeDestruct()
{
	ResetPresentation();
	if (QuantityConfirmButton) { QuantityConfirmButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleQuantityConfirmed); }
	if (QuantityCancelButton) { QuantityCancelButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleQuantityCancelled); }
	Super::NativeDestruct();
}

// 展示流程：
// 1. 先丢弃上一次来源的动态控件和数量状态，避免右键另一格时串用旧动作。
// 2. 逐条创建普通原生按钮，保留定义顺序，并把当前可用性投影为禁用态和原因文本。
// 3. 设置输入上限与菜单位置，最后显示并取得键盘焦点，让 Escape 先收口当前上下文。
void UCatInventoryContextMenuWidget::PresentActions(const TArray<FCatInventoryActionDefinition>& Actions,
	const TArray<FText>& UnavailableReasons, const int32 MaximumQuantity, const FVector2D& ScreenPosition)
{
	const FGameplayTag PreviousQuantityAction = PendingQuantityAction;
	const float PreviousQuantity = QuantitySpinBox ? QuantitySpinBox->GetValue() : 1.0f;
	const bool bRefresh = bMenuOpen;
	ResetPresentation();
	if (!ActionList || Actions.IsEmpty() || Actions.Num() != UnavailableReasons.Num())
	{
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	PendingMaximumQuantity = FMath::Max(1, MaximumQuantity);
	for (int32 Index = 0; Index < Actions.Num(); ++Index)
	{
		const FCatInventoryActionDefinition& Definition = Actions[Index];
		if (!Definition.Action.IsValid())
		{
			continue;
		}
		UCatInventoryContextActionButton* ActionButton = NewObject<UCatInventoryContextActionButton>(ActionList);
		UTextBlock* Label = NewObject<UTextBlock>(ActionButton);
		const bool bAvailable = UnavailableReasons[Index].IsEmpty();
		Label->SetText(bAvailable ? Definition.Label : FText::Format(NSLOCTEXT("CatInventory", "UnavailableAction", "{0}（{1}）"), Definition.Label, UnavailableReasons[Index]));
		Label->SetColorAndOpacity(bAvailable ? FSlateColor(FLinearColor::White) : FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f, 1.0f)));
		ActionButton->AddChild(Label);
		ActionButton->SetBackgroundColor(bAvailable ? FLinearColor(0.08f, 0.11f, 0.16f, 0.96f) : FLinearColor(0.06f, 0.06f, 0.06f, 0.7f));
		ActionButton->SetIsEnabled(bAvailable);
		const bool bRequiresQuantity = Definition.QuantityMode == ECatInventoryActionQuantityMode::Select && PendingMaximumQuantity > 1;
		ActionButton->SetAction(Definition.Action);
		ActionButton->OnActionRowChosen.BindLambda([this, bRequiresQuantity](const FGameplayTag ChosenAction) { ChooseAction(ChosenAction, bRequiresQuantity); });
		if (UVerticalBoxSlot* ActionSlot = ActionList->AddChildToVerticalBox(ActionButton)) { ActionSlot->SetPadding(FMargin(6.0f, 3.0f)); }
	}
	// 同一实例的复制刷新保持已选动作和数量；能力失效时退回置灰清单，数量减少只裁剪到当前上限。
	if (bRefresh && PreviousQuantityAction.IsValid())
	{
		const int32 ActionIndex = Actions.IndexOfByPredicate([&](const FCatInventoryActionDefinition& Item) { return Item.Action == PreviousQuantityAction; });
		if (ActionIndex != INDEX_NONE && UnavailableReasons[ActionIndex].IsEmpty())
		{
			bMenuOpen = true;
			ChooseAction(PreviousQuantityAction, true);
			QuantitySpinBox->SetValue(FMath::Clamp(PreviousQuantity, 1.0f, static_cast<float>(PendingMaximumQuantity)));
		}
	}
	SetVisibility(ESlateVisibility::Visible);
	ForceLayoutPrepass();
	// 鼠标事件已经给出桌面绝对坐标；使用所属玩家的真实窗口几何转换，避免双端 PIE 错用另一个窗口原点或重复计算 DPI。
	const FGeometry PlayerGeometry = UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry(GetOwningPlayer());
	FVector2D ViewportPosition = PlayerGeometry.AbsoluteToLocal(ScreenPosition);
	const FVector2D ViewportSize = PlayerGeometry.GetLocalSize();
	const FVector2D DesiredSize = GetDesiredSize();
	ViewportPosition.X = FMath::Clamp(ViewportPosition.X, 0.0f, FMath::Max(0.0f, ViewportSize.X - DesiredSize.X));
	ViewportPosition.Y = FMath::Clamp(ViewportPosition.Y, 0.0f, FMath::Max(0.0f, ViewportSize.Y - DesiredSize.Y));
	SetDesiredSizeInViewport(DesiredSize);
	SetAlignmentInViewport(FVector2D::ZeroVector);
	SetPositionInViewport(ViewportPosition, false);
	bMenuOpen = ActionList->GetChildrenCount() > 0;
	SetVisibility(bMenuOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (bMenuOpen && !bRefresh)
	{
		SetKeyboardFocus();
	}
}

// 关闭流程：先折叠视图并清理本地冻结状态；不广播取消，调用者决定此次关闭是否需要恢复 Tooltip。
void UCatInventoryContextMenuWidget::Dismiss()
{
	ResetPresentation();
	SetVisibility(ESlateVisibility::Collapsed);
}

// 状态读取流程：只返回本 View 是否仍持有一份菜单上下文，不从可见性推断避免淡出或父页移除造成双口径。
bool UCatInventoryContextMenuWidget::IsMenuOpen() const
{
	return bMenuOpen;
}

// 菜单及数量输入的预览键共用现有关闭和祭坛入口；先处理祭坛键，再让 Escape 取消本菜单，背包键关闭整页。
FReply UCatInventoryContextMenuWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	return NativeOnKeyDown(InGeometry, InKeyEvent);
}

// 根焦点与子控件的预览路由共用：不新绑快捷键，不改变角色输入模式；未识别的输入仍由 UMG 处理。
FReply UCatInventoryContextMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(GetOwningPlayer());
	if (Controller && Controller->TrySetAltarConfirmationFromKey(InKeyEvent.GetKey())) return FReply::Handled();
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		OnMenuCancelled.ExecuteIfBound();
		return FReply::Handled();
	}
	if (InKeyEvent.GetKey().GetFName() == GetDefault<UCatUISettings>()->ResolveInventoryToggleKeyName())
	{
		ULocalPlayer* Player = GetOwningLocalPlayer();
		UCatLocalPlayerUISubsystem* UI = Player ? Player->GetSubsystem<UCatLocalPlayerUISubsystem>() : nullptr;
		if (UI && UI->GetInventoryPageController()) UI->GetInventoryPageController()->RequestCloseInventoryFromWidget();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

// 动作选择流程：数量动作先冻结标识并显示同一菜单内的输入区；其它动作立即以数量一交给页面控制器。
void UCatInventoryContextMenuWidget::ChooseAction(const FGameplayTag Action, const bool bRequiresQuantity)
{
	if (!bMenuOpen || !Action.IsValid())
	{
		return;
	}
	if (!bRequiresQuantity)
	{
		OnActionChosen.ExecuteIfBound(Action, 1);
		return;
	}
	if (!QuantityPanel || !QuantitySpinBox || !QuantityConfirmButton || !QuantityCancelButton)
	{
		OnMenuCancelled.ExecuteIfBound();
		return;
	}
	PendingQuantityAction = Action;
	QuantitySpinBox->SetMinValue(1.0f);
	QuantitySpinBox->SetMaxValue(static_cast<float>(PendingMaximumQuantity));
	QuantitySpinBox->SetMinSliderValue(1.0f);
	QuantitySpinBox->SetMaxSliderValue(static_cast<float>(PendingMaximumQuantity));
	QuantitySpinBox->SetDelta(1.0f);
	QuantitySpinBox->SetMinFractionalDigits(0);
	QuantitySpinBox->SetMaxFractionalDigits(0);
	QuantitySpinBox->SetValue(1.0f);
	ActionList->SetVisibility(ESlateVisibility::Collapsed);
	QuantityPanel->SetVisibility(ESlateVisibility::Visible);
	// 数量页与动作清单高度不同；重新测量并限制在玩家窗口内，避免沿用旧尺寸裁掉确认按钮。
	ForceLayoutPrepass();
	const FGeometry PlayerGeometry = UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry(GetOwningPlayer());
	FVector2D Position = PlayerGeometry.AbsoluteToLocal(GetCachedGeometry().GetAbsolutePosition());
	const FVector2D Size = GetDesiredSize();
	const FVector2D Available = PlayerGeometry.GetLocalSize();
	Position.X = FMath::Clamp(Position.X, 0.0f, FMath::Max(0.0f, Available.X - Size.X));
	Position.Y = FMath::Clamp(Position.Y, 0.0f, FMath::Max(0.0f, Available.Y - Size.Y));
	SetDesiredSizeInViewport(Size);
	SetPositionInViewport(Position, false);
}

// 数量确认流程：先验证当前冻结动作和输入控件，再将浮点输入压到整数范围，最后只上交给页面控制器。
void UCatInventoryContextMenuWidget::HandleQuantityConfirmed()
{
	if (!bMenuOpen || !PendingQuantityAction.IsValid() || !QuantitySpinBox || PendingMaximumQuantity <= 0)
	{
		OnMenuCancelled.ExecuteIfBound();
		return;
	}
	const float RequestedQuantity = QuantitySpinBox->GetValue();
	if (!FMath::IsFinite(RequestedQuantity))
	{
		OnMenuCancelled.ExecuteIfBound();
		return;
	}
	const int32 Quantity = FMath::FloorToInt(FMath::Clamp(RequestedQuantity, 1.0f, static_cast<float>(PendingMaximumQuantity)));
	OnActionChosen.ExecuteIfBound(PendingQuantityAction, Quantity);
}

// 数量取消流程：用户放弃的是整次菜单操作，通知页面控制器清理来源和 Tooltip 抑制状态。
void UCatInventoryContextMenuWidget::HandleQuantityCancelled()
{
	OnMenuCancelled.ExecuteIfBound();
}

// 重置流程：先清空动态行，再废弃数量动作和上限并折叠数量区；该函数不改变库存或调用任何网络接口。
void UCatInventoryContextMenuWidget::ResetPresentation()
{
	if (ActionList)
	{
		ActionList->ClearChildren();
		ActionList->SetVisibility(ESlateVisibility::Visible);
	}
	PendingQuantityAction = FGameplayTag();
	PendingMaximumQuantity = 0;
	if (QuantitySpinBox) { QuantitySpinBox->SetValue(1.0f); }
	if (QuantityPanel) { QuantityPanel->SetVisibility(ESlateVisibility::Collapsed); }
	bMenuOpen = false;
}
