#include "UI/Items/CatHornMessageWidget.h"
#include "AbilitySystem/Items/CatItemAbilityComponent.h"
#include "Inventory/CatInventoryComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Engine/World.h"
#include "TimerManager.h"

// 来源设置流程：保存弱引用和身份，不启动 GAS、不扣次数。
void UCatHornMessageWidget::SetSource(UCatItemAbilityComponent* Items, UCatInventoryComponent* Inventory, FGuid ItemId)
{ SourceItems = Items; SourceInventory = Inventory; SourceItemId = ItemId; }
// 布局流程：原生控件提供可用的输入窗口；确认走同一个方法，取消只移除自身。
TSharedRef<SWidget> UCatHornMessageWidget::RebuildWidget()
{
	return SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
	[SNew(SBox).WidthOverride(460)[SNew(SBorder).Padding(20)
	[SNew(SVerticalBox)
	+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)[SNew(STextBlock).Text(FText::FromString(TEXT("响响筒 · 对队友说句话（最多120字）")))]
	+ SVerticalBox::Slot().AutoHeight()[SAssignNew(Input, SEditableTextBox)
		.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type Type) { if (Type == ETextCommit::OnEnter) Confirm(); })]
	+ SVerticalBox::Slot().AutoHeight().Padding(0,12,0,0)[SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1)[SNew(SButton).Text(FText::FromString(TEXT("喊出去"))).OnClicked_UObject(this, &ThisClass::Confirm)]
		+ SHorizontalBox::Slot().FillWidth(1)[SNew(SButton).Text(FText::FromString(TEXT("取消"))).OnClicked_Lambda([this]() { RemoveFromParent(); return FReply::Handled(); })]]]]];
}
// 输入接管流程：使用项目既有锁计数，焦点转到文本框，移动和视角不会同时响应打字。
void UCatHornMessageWidget::NativeConstruct()
{
	Super::NativeConstruct(); CatUIModalInputMode::Open(GetOwningPlayer(), this, InputState);
	if (Input) FSlateApplication::Get().SetKeyboardFocus(Input);
}
// 析构流程：无论取消还是旅行都成对释放输入锁，然后交回父类生命周期。
void UCatHornMessageWidget::NativeDestruct()
{ CatUIModalInputMode::Close(GetOwningPlayer(), InputState); Super::NativeDestruct(); }
// 键盘流程：Escape 没有使用副作用；其他按键保留常规文本编辑行为。
FReply UCatHornMessageWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (Event.GetKey() == EKeys::Escape) { RemoveFromParent(); return FReply::Handled(); }
	return Super::NativeOnKeyDown(Geometry, Event);
}
// 确认流程：先取冻结来源与文本，关闭窗口释放输入锁，再经物品组件发送一次新请求；取消从不走此路径。
FReply UCatHornMessageWidget::Confirm()
{
	const FString Text = Input ? Input->GetText().ToString().TrimStartAndEnd() : FString();
	if (Text.IsEmpty() || Text.Len() > 120) return FReply::Handled();
	auto* Items = SourceItems.Get(); auto* Inventory = SourceInventory.Get(); const FGuid ItemId = SourceItemId;
	RemoveFromParent();
	if (Items && Inventory) Items->RequestUse(Inventory, ItemId, FGuid::NewGuid(), false, false, Text);
	return FReply::Handled();
}
// 喊话布局流程：顶部居中显示服务器文字，宽度限制使长文本自然换行。
TSharedRef<SWidget> UCatHornAnnouncementWidget::RebuildWidget()
{
	return SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(FMargin(0,70,0,0))
	[SNew(SBox).WidthOverride(640)[SNew(SBorder).Padding(16)[SNew(STextBlock).Text(Message).AutoWrapText(true)]]];
}
// 展示生命周期：窗口不接管输入，弱定时回调只移除本条消息，旅行销毁后不会再访问它。
void UCatHornAnnouncementWidget::NativeConstruct()
{
	Super::NativeConstruct(); SetVisibility(ESlateVisibility::HitTestInvisible);
	FTimerHandle Timer; GetWorld()->GetTimerManager().SetTimer(Timer, FTimerDelegate::CreateWeakLambda(this, [this]() { RemoveFromParent(); }), 5.f, false);
}
