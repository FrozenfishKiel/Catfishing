#include "UI/Shop/CatShopWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "UI/CatUISettings.h"

// 动态按钮初始化流程：保存父 View 和当前商品 EntryId，重建点击绑定；按钮本身不保存任何后端经济事实。
void UCatShopEntryButton::InitializeShopEntry(UCatShopWidget* InOwnerShopWidget, const FName InEntryId)
{
	OwnerShopWidget = InOwnerShopWidget;
	EntryId = InEntryId;
	OnClicked.RemoveDynamic(this, &ThisClass::HandleShopEntryClicked);
	OnClicked.AddDynamic(this, &ThisClass::HandleShopEntryClicked);
}

// 动态按钮点击流程：校验父 View 和 EntryId 后调用统一加购请求口；价格、库存和支付能力仍由 Model/Controller/服务器裁决。
void UCatShopEntryButton::HandleShopEntryClicked()
{
	UCatShopWidget* ShopWidget = OwnerShopWidget.Get();
	if (!ShopWidget || EntryId.IsNone())
	{
		return;
	}
	ShopWidget->RequestAddEntryToCart(EntryId);
}

// 构造流程：
// 1. 先把商店根 Widget 设为可聚焦，让通用 UIOnly 输入模式能把键盘焦点真正交给本页面；后续关闭键才会进入 NativeOnKeyDown。
// 2. 再把 Designer 里的关闭按钮和支付按钮接到统一入口；正式货架按钮会在 RenderShop 中按 DisplayedEntries 动态生成。
void UCatShopWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	if (CloseButton)
	{
		CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCloseClicked);
		CloseButton->OnClicked.AddDynamic(this, &ThisClass::HandleCloseClicked);
		CloseButton->SetIsEnabled(true);
	}
	if (PayButton)
	{
		PayButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestPayCart);
		PayButton->OnClicked.AddDynamic(this, &ThisClass::RequestPayCart);
		PayButton->SetIsEnabled(false);
	}
}

// 析构流程：
// 1. 先清掉本次动态生成的商品按钮绑定，防止货架行对象迟到提交过期 EntryId。
// 2. 再解除本 View 绑定到可选关闭/支付按钮上的 UMG 点击事件，防止再次入视口时叠加回调。
// 3. 保留外部意图订阅；RemoveFromParent 只是页面离开视口，PageController::Unbind 才是外部订阅的结束点。
void UCatShopWidget::NativeDestruct()
{
	for (UCatShopEntryButton* Button : DynamicEntryButtons)
	{
		if (Button)
		{
			Button->OnClicked.Clear();
		}
	}
	DynamicEntryButtons.Reset();
	if (CloseButton)
	{
		CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCloseClicked);
	}
	if (PayButton)
	{
		PayButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestPayCart);
	}
	Super::NativeDestruct();
}

// 预览键盘流程：先于商品按钮消费关闭键；命中后仍只广播关闭意图，让 PageController 和交互组件成对恢复输入与销毁页面。
FReply UCatShopWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (ShouldCloseShopFromKey(InKeyEvent))
	{
		RequestCloseShop();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

// 键盘流程：商店根页拿到焦点时复用同一关闭键判断；预览未命中的按键继续交给父类。
FReply UCatShopWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (ShouldCloseShopFromKey(InKeyEvent))
	{
		RequestCloseShop();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

// 渲染流程：
// 1. 缓存只读投影，复制 Designer 绑定字段，再按当前本地分类刷新 DisplayedEntries。
// 2. 先按 ViewState 同步 C++ 简单控件和支付按钮，再调用 BP 渲染扩展；分类列表始终来自 DisplayedEntries。
// 3. pending 期间把键盘焦点拉回商店根页，避免商品按钮在点击后被重建或禁用时截断 Escape/交互键关闭。
void UCatShopWidget::RenderShop(const FCatShopViewState& ViewState)
{
	LastShopViewState = ViewState;
	if (!ViewState.bOpen)
	{
		BlueprintSelectedCategoryId = NAME_None;
	}
	BlueprintWalletText = ViewState.WalletText;
	BlueprintResultText = ViewState.ResultText;
	BlueprintEntries = ViewState.Entries;
	BlueprintCartLines = ViewState.CartLines;
	BlueprintCartTotalText = ViewState.CartTotalText;
	BlueprintPayButtonText = ViewState.PayButtonText;
	BlueprintPayDisabledReasonText = ViewState.PayDisabledReasonText;
	RefreshDisplayedEntries();
	RefreshCartPresentation();
	if (WalletTextBlock)
	{
		WalletTextBlock->SetText(BlueprintWalletText);
	}
	if (ResultTextBlock)
	{
		ResultTextBlock->SetText(BlueprintResultText);
	}
	if (EntriesTextBlock)
	{
		EntriesTextBlock->SetText(BlueprintEntriesText);
	}
	if (CartTextBlock)
	{
		CartTextBlock->SetText(BlueprintCartText);
	}
	if (CartTotalTextBlock)
	{
		CartTotalTextBlock->SetText(BlueprintCartTotalText);
	}
	if (PayButton)
	{
		PayButton->SetIsEnabled(ViewState.bCanPayCart);
	}
	BP_RenderShop(LastShopViewState);
	SetIsEnabled(true);
	if (CloseButton)
	{
		CloseButton->SetIsEnabled(true);
	}
	if (ShopButtons)
	{
		ShopButtons->SetIsEnabled(true);
	}
	if (ViewState.bOpen && ViewState.bActionPending)
	{
		SetKeyboardFocus();
	}
}

// 状态读取流程：返回最近商店投影；调用者不能通过它取得公款或商品写口。
const FCatShopViewState& UCatShopWidget::GetLastShopViewState() const
{
	return LastShopViewState;
}

// 显示数组读取流程：返回本地分类过滤结果；这是商品区应绑定的数组，完整 Entries 只作为过滤来源保留。
const TArray<FCatShopEntryView>& UCatShopWidget::GetDisplayedEntries() const
{
	return BlueprintDisplayedEntries;
}

// 购物车数组读取流程：返回本地购物车投影；右侧已选购列表和垃圾桶按钮都应从这里生成。
const TArray<FCatShopCartLineView>& UCatShopWidget::GetCartLines() const
{
	return BlueprintCartLines;
}

// 加购请求流程：只允许有效 EntryId 且没有支付 pending 时出口；是否能加入本地购物车由 PageController/Model 根据最新投影裁决。
void UCatShopWidget::RequestAddEntryToCart(const FName EntryId)
{
	if (EntryId.IsNone() || LastShopViewState.bActionPending)
	{
		return;
	}
	OnEntryAddToCartRequested.Broadcast(EntryId);
}

// 购物车删除请求流程：只允许有效 EntryId 且没有支付 pending 时出口；每次删除一份本地选购，服务器货架不会被修改。
void UCatShopWidget::RequestRemoveOneCartItem(const FName EntryId)
{
	if (EntryId.IsNone() || LastShopViewState.bActionPending)
	{
		return;
	}
	OnCartLineRemoveRequested.Broadcast(EntryId);
}

// 支付请求流程：只在 Model 投影允许支付时广播；禁用原因留给 WBP 悬停显示，不在这里替服务器裁决。
void UCatShopWidget::RequestPayCart()
{
	if (!LastShopViewState.bCanPayCart)
	{
		return;
	}
	OnCartPayRequested.Broadcast();
}

// 分类请求流程：只改本 Widget 的本地分类 ID，再用完整 Entries 重新生成 DisplayedEntries；不会写回 Model 或服务器。
void UCatShopWidget::RequestSelectCategory(const FName CategoryId)
{
	BlueprintSelectedCategoryId = CategoryId;
	RefreshDisplayedEntries();
}

// 全部分类流程：把分类 ID 清回 NAME_None，再复用同一套 DisplayedEntries 刷新逻辑。
void UCatShopWidget::RequestShowAllCategory()
{
	RequestSelectCategory(NAME_None);
}

// 关闭请求流程：只广播关闭意图，输入模式和 Widget 生命周期由 PageController/交互组件处理。
void UCatShopWidget::RequestCloseShop()
{
	OnCloseRequested.Broadcast();
}

// 默认关闭按钮流程：复用统一关闭请求入口，避免按钮和蓝图形成两条关闭路径。
void UCatShopWidget::HandleCloseClicked()
{
	RequestCloseShop();
}

// 动态货架流程：
// 1. 缺少商品容器或 WidgetTree 时不重建动态按钮；文本投影和蓝图渲染扩展仍会继续承接展示。
// 2. 正常情况下先清空商品容器里的旧商品；直接放在这里的关闭按钮或支付按钮会保留为页面出口和支付入口。
// 3. 逐条读取 DisplayedEntries 生成按钮文本；每个按钮只保存 EntryId，点击后仍走统一加购广播。
void UCatShopWidget::RebuildDynamicEntryButtons()
{
	if (!ShopButtons || !WidgetTree)
	{
		return;
	}
	for (int32 ChildIndex = ShopButtons->GetChildrenCount() - 1; ChildIndex >= 0; --ChildIndex)
	{
		UWidget* Child = ShopButtons->GetChildAt(ChildIndex);
		if (!Child || Child == CloseButton || Child == PayButton)
		{
			continue;
		}
		if (UCatShopEntryButton* EntryButton = Cast<UCatShopEntryButton>(Child))
		{
			EntryButton->OnClicked.Clear();
		}
		ShopButtons->RemoveChildAt(ChildIndex);
	}
	DynamicEntryButtons.Reset();

	for (const FCatShopEntryView& Entry : BlueprintDisplayedEntries)
	{
		UCatShopEntryButton* EntryButton = WidgetTree->ConstructWidget<UCatShopEntryButton>(
			UCatShopEntryButton::StaticClass(), MakeUniqueObjectName(WidgetTree, UCatShopEntryButton::StaticClass(),
				FName(TEXT("ShopEntryButton"))));
		UTextBlock* ButtonText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), MakeUniqueObjectName(WidgetTree, UTextBlock::StaticClass(),
				FName(TEXT("ShopEntryButtonText"))));
		if (!EntryButton || !ButtonText)
		{
			continue;
		}
		const FString ButtonLabel = FString::Printf(TEXT("%s  %s"),
			*Entry.ActionText.ToString(), *Entry.DisplayText.ToString());
		ButtonText->SetText(FText::FromString(ButtonLabel));
		ButtonText->SetAutoWrapText(true);
		EntryButton->AddChild(ButtonText);
		EntryButton->SetIsEnabled(Entry.bActionEnabled);
		EntryButton->InitializeShopEntry(this, Entry.EntryId);
		ShopButtons->AddChild(EntryButton);
		DynamicEntryButtons.Add(EntryButton);
	}
}

// 分类显示刷新流程：
// 1. 从 LastShopViewState.Entries 重新生成本地 DisplayedEntries；NAME_None 表示“全部”。
// 2. 商品列表文本和动态按钮都只读取 DisplayedEntries，避免分类切换时误改完整真实商品数组。
// 3. 本函数不调用 BP_RenderShop，分类按钮只需要刷新本地数组和可选 C++ 简单控件。
void UCatShopWidget::RefreshDisplayedEntries()
{
	BlueprintDisplayedEntries.Reset();
	for (const FCatShopEntryView& Entry : LastShopViewState.Entries)
	{
		if (BlueprintSelectedCategoryId.IsNone() || Entry.DisplayCategoryId == BlueprintSelectedCategoryId)
		{
			BlueprintDisplayedEntries.Add(Entry);
		}
	}
	TArray<FString> Lines;
	Lines.Reserve(BlueprintDisplayedEntries.Num());
	for (const FCatShopEntryView& Entry : BlueprintDisplayedEntries)
	{
		Lines.Add(Entry.DisplayText.ToString());
	}
	BlueprintEntriesText = FText::FromString(FString::Join(Lines, TEXT("\n")));
	if (EntriesTextBlock)
	{
		EntriesTextBlock->SetText(BlueprintEntriesText);
	}
	if (ShopButtons)
	{
		RebuildDynamicEntryButtons();
		ShopButtons->SetIsEnabled(true);
	}
}

// 购物车文本刷新流程：把购物车行拼成右侧已选购摘要；复杂 WBP 可忽略这段文本，直接按 BlueprintCartLines 创建行。
void UCatShopWidget::RefreshCartPresentation()
{
	TArray<FString> Lines;
	Lines.Reserve(BlueprintCartLines.Num());
	for (const FCatShopCartLineView& Line : BlueprintCartLines)
	{
		Lines.Add(Line.DisplayText.ToString());
	}
	BlueprintCartText = FText::FromString(FString::Join(Lines, TEXT("\n")));
}

// 关闭键判断流程：
// 1. 先要求商店处于打开投影，避免构造或移出视口期间的迟到按键误触发关闭。
// 2. Escape 始终作为模态 UI 兜底关闭键。
// 3. 再接受项目配置里的交互键和背包键，让按 E 打开的商店能按 E 关闭，也能用常规库存键退出页面。
bool UCatShopWidget::ShouldCloseShopFromKey(const FKeyEvent& InKeyEvent) const
{
	if (!LastShopViewState.bOpen)
	{
		return false;
	}
	const FName PressedKeyName = InKeyEvent.GetKey().GetFName();
	if (PressedKeyName == EKeys::Escape.GetFName())
	{
		return true;
	}
	const UCatUISettings* Settings = GetDefault<UCatUISettings>();
	const FName InteractionKeyName = Settings ? Settings->ResolveInteractionConfirmKeyName() : NAME_None;
	const FName InventoryKeyName = Settings ? Settings->ResolveInventoryToggleKeyName() : NAME_None;
	return (!InteractionKeyName.IsNone() && PressedKeyName == InteractionKeyName)
		|| (!InventoryKeyName.IsNone() && PressedKeyName == InventoryKeyName);
}
