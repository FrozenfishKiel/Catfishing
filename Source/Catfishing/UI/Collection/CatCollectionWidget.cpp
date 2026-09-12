#include "UI/Collection/CatCollectionWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/LocalPlayer.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "UI/CatUISettings.h"
#include "UI/Collection/CatCollectionPageController.h"

// 图鉴渲染流程：缓存只读投影并同步摘要和简单列表文本；复杂列表控件仍可由 WBP 根据 Entries 表现。
void UCatCollectionWidget::RenderCollection(const FCatCollectionViewState& ViewState)
{
	LastCollectionViewState = ViewState;
	BlueprintSummaryText = ViewState.SummaryText;
	TArray<FString> Lines;
	Lines.Reserve(ViewState.Entries.Num());
	for (const FCatCollectionEntryView& Entry : ViewState.Entries)
	{
		Lines.Add(Entry.DisplayText.ToString());
	}
	BlueprintEntriesText = FText::FromString(FString::Join(Lines, TEXT("\n")));
	if (SummaryTextBlock)
	{
		SummaryTextBlock->SetText(BlueprintSummaryText);
	}
	if (EntriesTextBlock)
	{
		EntriesTextBlock->SetText(BlueprintEntriesText);
	}
	BP_RenderCollection(LastCollectionViewState);
}

// 状态读取流程：返回最近图鉴投影；调用者不能通过它修改 Profile 或实物鱼容器。
const FCatCollectionViewState& UCatCollectionWidget::GetLastCollectionViewState() const
{
	return LastCollectionViewState;
}

// 关闭请求流程：只向 PageController 提交意图；视口移除和输入恢复仍由页面控制器成对负责。
void UCatCollectionWidget::RequestCloseCollection()
{
	if (UCatCollectionPageController* Controller = ResolveCollectionPageController())
	{
		Controller->RequestCloseCollectionFromWidget();
	}
}

// 印记隐藏流程：把意图交给页面控制器，由它转给 Model 的 Profile 写口；Widget 不持有 Profile 引用，
// 也不自己维护隐藏状态——成功后 Model 会重发投影，本页面照常整份重绘。
bool UCatCollectionWidget::RequestSetImprintHidden(const FGuid ImprintId, const bool bHidden)
{
	UCatCollectionPageController* Controller = ResolveCollectionPageController();
	return Controller && Controller->RequestSetImprintHiddenFromWidget(ImprintId, bHidden);
}

// 构造流程：让父类完成 Slate 构建后，对可选关闭按钮执行 Remove/Add 配对，保证重建时不会重复提交。
void UCatCollectionWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (CloseButton)
	{
		CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestCloseCollection);
		CloseButton->OnClicked.AddDynamic(this, &ThisClass::RequestCloseCollection);
	}
}

// 销毁流程：解除关闭按钮对本对象的动态绑定，再交还父类 Slate 生命周期。
void UCatCollectionWidget::NativeDestruct()
{
	if (CloseButton)
	{
		CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestCloseCollection);
	}
	Super::NativeDestruct();
}

// 在子控件消费之前处理关闭键，避免焦点落在列表控件上后无法关闭整页。
FReply UCatCollectionWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (ShouldCloseCollectionFromKey(InKeyEvent))
	{
		RequestCloseCollection();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

// 根控件直接收到按键时复用同一关闭判断；其余输入保持默认传播。
FReply UCatCollectionWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (ShouldCloseCollectionFromKey(InKeyEvent))
	{
		RequestCloseCollection();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

// 只为页面关闭解析 owning LocalPlayer 的控制器；图鉴数据源仍由 PageController 持有的 Model 决定。
UCatCollectionPageController* UCatCollectionWidget::ResolveCollectionPageController() const
{
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	UCatLocalPlayerUISubsystem* UI = LocalPlayer ? LocalPlayer->GetSubsystem<UCatLocalPlayerUISubsystem>() : nullptr;
	return UI ? UI->GetCollectionPageController() : nullptr;
}

// 关闭条件读取页面控制器的唯一打开状态；接受 Escape 与从正式 IMC 解析出的图鉴键，不硬编码 M。
bool UCatCollectionWidget::ShouldCloseCollectionFromKey(const FKeyEvent& InKeyEvent) const
{
	const UCatCollectionPageController* Controller = ResolveCollectionPageController();
	if (!Controller || !Controller->IsCollectionOpen())
	{
		return false;
	}
	const FName Key = InKeyEvent.GetKey().GetFName();
	const UCatUISettings* Settings = GetDefault<UCatUISettings>();
	return Key == EKeys::Escape.GetFName() || Key == Settings->ResolveCollectionToggleKeyName();
}
