#include "UI/Shop/CatShopWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
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

// 分类按钮初始化流程：保存父 View 和分类 ID，重建点击绑定；按钮本身只负责切换本地展示列表。
void UCatShopCategoryButton::InitializeShopCategory(UCatShopWidget* InOwnerShopWidget, const FName InCategoryId)
{
	OwnerShopWidget = InOwnerShopWidget;
	CategoryId = InCategoryId;
	OnClicked.RemoveDynamic(this, &ThisClass::HandleShopCategoryClicked);
	OnClicked.AddDynamic(this, &ThisClass::HandleShopCategoryClicked);
}

// 分类按钮点击流程：交回父 View 统一处理；分类选择不会写回 Model 的 Entries 或服务器库存。
void UCatShopCategoryButton::HandleShopCategoryClicked()
{
	UCatShopWidget* ShopWidget = OwnerShopWidget.Get();
	if (!ShopWidget)
	{
		return;
	}
	ShopWidget->RequestSelectCategory(CategoryId);
}

// 购物车删除按钮初始化流程：保存父 View 和 EntryId，重建点击绑定；按钮本身不保存数量、价格或库存。
void UCatShopCartLineRemoveButton::InitializeCartLine(UCatShopWidget* InOwnerShopWidget, const FName InEntryId)
{
	OwnerShopWidget = InOwnerShopWidget;
	EntryId = InEntryId;
	OnClicked.RemoveDynamic(this, &ThisClass::HandleCartLineRemoveClicked);
	OnClicked.AddDynamic(this, &ThisClass::HandleCartLineRemoveClicked);
}

// 购物车删除按钮点击流程：只请求删除一份本地选购；支付 pending 保护仍由父 View 和 Model 双重判断。
void UCatShopCartLineRemoveButton::HandleCartLineRemoveClicked()
{
	UCatShopWidget* ShopWidget = OwnerShopWidget.Get();
	if (!ShopWidget || EntryId.IsNone())
	{
		return;
	}
	ShopWidget->RequestRemoveOneCartItem(EntryId);
}

// 构造流程：
// 1. 先把商店根 Widget 设为可聚焦，让通用 UIOnly 输入模式能把键盘焦点真正交给本页面；后续关闭键才会进入 NativeOnKeyDown。
// 2. 再补齐旧 WBP 缺少的新购物车控件，并折叠历史直购按钮，避免旧入口和新购物车链路同时可点。
// 3. 最后把 Designer 或运行时兜底里的关闭按钮和支付按钮接到统一入口；正式动态列表会在 RenderShop 中按 Model 投影生成。
void UCatShopWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	EnsureFallbackRuntimeControls();
	DisableLegacyDirectPurchaseButtons();
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
// 1. 先清掉本次动态生成的商品、分类和购物车删除按钮绑定；分类和购物车按钮还会从父容器移除。
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
	for (UCatShopCategoryButton* Button : DynamicCategoryButtons)
	{
		if (Button)
		{
			Button->OnClicked.Clear();
			Button->RemoveFromParent();
		}
	}
	DynamicCategoryButtons.Reset();
	for (UCatShopCartLineRemoveButton* Button : DynamicCartLineButtons)
	{
		if (Button)
		{
			Button->OnClicked.Clear();
			Button->RemoveFromParent();
		}
	}
	DynamicCartLineButtons.Reset();
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
// 1. 缓存只读投影并复制 Designer 绑定字段，关闭投影会把本地分类选择清回“全部”。
// 2. 先同步分类按钮、当前分类商品、购物车行和 C++ 简单控件；支付按钮同时刷新可用状态和禁用提示。
// 3. 最后调用 BP 渲染扩展；蓝图可继续从 GetCategories/GetDisplayedEntries/GetCartLines 读取同一份本地投影。
// 4. pending 期间把键盘焦点拉回商店根页，避免商品按钮在点击后被重建或禁用时截断 Escape/交互键关闭。
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
	RefreshCategoryPresentation();
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
		PayButton->SetToolTipText(ViewState.bCanPayCart ? FText() : ViewState.PayDisabledReasonText);
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

// 分类数组读取流程：返回本地标记过选中态的分类投影；每个客户端可以不同步地选择自己的分类页签。
const TArray<FCatShopCategoryView>& UCatShopWidget::GetCategories() const
{
	return BlueprintCategories;
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

// 分类请求流程：只改本 Widget 的本地分类 ID，再用完整 Entries 重新生成 DisplayedEntries 并唤醒 BP 渲染扩展；不会写回 Model 或服务器。
void UCatShopWidget::RequestSelectCategory(const FName CategoryId)
{
	BlueprintSelectedCategoryId = DoesCategoryExist(CategoryId) ? CategoryId : NAME_None;
	RefreshCategoryPresentation();
	RefreshDisplayedEntries();
	BP_RenderShop(LastShopViewState);
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

// 运行时兜底控件流程：
// 1. 旧 WBP 缺少新购物车命名控件时，优先挂到根面板，保证分类、右侧购物车、总价和支付按钮至少可用。
// 2. 已存在同名控件时完全不覆盖；正式水彩布局仍由 WBP 资产决定，C++ 只补缺口。
// 3. 新建控件都继续走同一套 RenderShop/RequestPayCart/RequestRemoveOneCartItem，不产生第二条购买链路。
void UCatShopWidget::EnsureFallbackRuntimeControls()
{
	UPanelWidget* RootPanel = WidgetTree ? Cast<UPanelWidget>(WidgetTree->RootWidget) : nullptr;
	if (!RootPanel)
	{
		return;
	}
	if (!CategoryButtons)
	{
		CategoryButtons = WidgetTree->ConstructWidget<UHorizontalBox>(
			UHorizontalBox::StaticClass(), FName(TEXT("CategoryButtons")));
		if (CategoryButtons)
		{
			RootPanel->AddChild(CategoryButtons);
		}
	}
	if (!CartTextBlock)
	{
		CartTextBlock = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), FName(TEXT("CartTextBlock")));
		if (CartTextBlock)
		{
			CartTextBlock->SetAutoWrapText(true);
			RootPanel->AddChild(CartTextBlock);
		}
	}
	if (!CartTotalTextBlock)
	{
		CartTotalTextBlock = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), FName(TEXT("CartTotalTextBlock")));
		if (CartTotalTextBlock)
		{
			RootPanel->AddChild(CartTotalTextBlock);
		}
	}
	if (!CartLinesPanel)
	{
		CartLinesPanel = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), FName(TEXT("CartLinesPanel")));
		if (CartLinesPanel)
		{
			RootPanel->AddChild(CartLinesPanel);
		}
	}
	if (!PayButton)
	{
		PayButton = WidgetTree->ConstructWidget<UButton>(
			UButton::StaticClass(), FName(TEXT("PayButton")));
		UTextBlock* PayText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), FName(TEXT("PayButtonText")));
		if (PayButton && PayText)
		{
			PayText->SetText(FText::FromString(TEXT("支付")));
			PayButton->AddChild(PayText);
			RootPanel->AddChild(PayButton);
		}
	}
}

// 旧直购按钮清理流程：
// 1. 扫描当前 WBP 的 WidgetTree，只匹配历史白盒页里以 Purchase/ClaimFree 命名的 Button。
// 2. 命中的按钮直接折叠并禁用，让旧资产残留不会和新购物车入口同时出现在页面上。
// 3. 不读取按钮文本和商品名，也不把旧 EntryId 写回代码；正式商品按钮仍全部由 DataTable + DisplayedEntries 生成。
void UCatShopWidget::DisableLegacyDirectPurchaseButtons()
{
	if (!WidgetTree)
	{
		return;
	}
	WidgetTree->ForEachWidget([](UWidget* Widget)
	{
		UButton* Button = Cast<UButton>(Widget);
		if (!Button)
		{
			return;
		}
		const FString WidgetName = Button->GetFName().ToString();
		const bool bLegacyPurchaseButton = WidgetName.StartsWith(TEXT("Purchase"))
			|| WidgetName.StartsWith(TEXT("ClaimFree"));
		if (!bLegacyPurchaseButton)
		{
			return;
		}
		Button->SetIsEnabled(false);
		Button->SetVisibility(ESlateVisibility::Collapsed);
	});
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
		if (!Child || Child == CloseButton || Child == PayButton || Child == WalletTextBlock
			|| Child == ResultTextBlock || Child == EntriesTextBlock || Child == CartTextBlock
			|| Child == CartTotalTextBlock || Child == CategoryButtons || Child == CartLinesPanel)
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

// 动态分类流程：
// 1. 没有分类容器或 WidgetTree 时只保留 BlueprintCategories 给 WBP 手动渲染。
// 2. 有容器时清理上一轮 C++ 生成的分类按钮，不删除 Designer 自己放的装饰控件。
// 3. 每个按钮只保存 CategoryId；选中态用文字兜底表达，正式视觉可在 BP_RenderShop 里覆盖。
void UCatShopWidget::RebuildDynamicCategoryButtons()
{
	if (!CategoryButtons || !WidgetTree)
	{
		return;
	}
	for (UCatShopCategoryButton* Button : DynamicCategoryButtons)
	{
		if (Button)
		{
			Button->OnClicked.Clear();
			Button->RemoveFromParent();
		}
	}
	DynamicCategoryButtons.Reset();

	for (const FCatShopCategoryView& Category : BlueprintCategories)
	{
		UCatShopCategoryButton* CategoryButton = WidgetTree->ConstructWidget<UCatShopCategoryButton>(
			UCatShopCategoryButton::StaticClass(), MakeUniqueObjectName(WidgetTree,
				UCatShopCategoryButton::StaticClass(), FName(TEXT("ShopCategoryButton"))));
		UTextBlock* ButtonText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), MakeUniqueObjectName(WidgetTree, UTextBlock::StaticClass(),
				FName(TEXT("ShopCategoryButtonText"))));
		if (!CategoryButton || !ButtonText)
		{
			continue;
		}
		const FString CategoryLabel = Category.DisplayNameText.IsEmpty()
			? Category.CategoryId.ToString() : Category.DisplayNameText.ToString();
		const FString ButtonLabel = Category.bSelected
			? FString::Printf(TEXT("【%s】"), *CategoryLabel) : CategoryLabel;
		ButtonText->SetText(FText::FromString(ButtonLabel));
		ButtonText->SetAutoWrapText(true);
		CategoryButton->AddChild(ButtonText);
		CategoryButton->SetIsEnabled(true);
		CategoryButton->InitializeShopCategory(this, Category.CategoryId);
		CategoryButtons->AddChild(CategoryButton);
		DynamicCategoryButtons.Add(CategoryButton);
	}
}

// 动态购物车流程：
// 1. 没有购物车容器或 WidgetTree 时只保留 BlueprintCartLines 给 WBP 手动渲染。
// 2. 有容器时清理上一轮 C++ 生成的删除按钮，不删除 Designer 自己放的背景、标题或总价文本。
// 3. 删除按钮即使商品已失效也保持可点，玩家需要能把失效行从本地购物车里删掉。
void UCatShopWidget::RebuildDynamicCartLineButtons()
{
	if (!CartLinesPanel || !WidgetTree)
	{
		return;
	}
	for (UCatShopCartLineRemoveButton* Button : DynamicCartLineButtons)
	{
		if (Button)
		{
			Button->OnClicked.Clear();
			Button->RemoveFromParent();
		}
	}
	DynamicCartLineButtons.Reset();

	for (const FCatShopCartLineView& Line : BlueprintCartLines)
	{
		UCatShopCartLineRemoveButton* RemoveButton = WidgetTree->ConstructWidget<UCatShopCartLineRemoveButton>(
			UCatShopCartLineRemoveButton::StaticClass(), MakeUniqueObjectName(WidgetTree,
				UCatShopCartLineRemoveButton::StaticClass(), FName(TEXT("ShopCartLineRemoveButton"))));
		UTextBlock* ButtonText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), MakeUniqueObjectName(WidgetTree, UTextBlock::StaticClass(),
				FName(TEXT("ShopCartLineRemoveButtonText"))));
		if (!RemoveButton || !ButtonText)
		{
			continue;
		}
		ButtonText->SetText(FText::FromString(FString::Printf(TEXT("删除  %s"), *Line.DisplayText.ToString())));
		ButtonText->SetAutoWrapText(true);
		RemoveButton->AddChild(ButtonText);
		RemoveButton->SetIsEnabled(!LastShopViewState.bActionPending);
		RemoveButton->InitializeCartLine(this, Line.EntryId);
		CartLinesPanel->AddChild(RemoveButton);
		DynamicCartLineButtons.Add(RemoveButton);
	}
}

// 分类文本流程：从最新 ViewState 复制分类投影，并在本地补上选中态；当商店刷新后当前分类消失，自动回到“全部”。
void UCatShopWidget::RefreshCategoryPresentation()
{
	BlueprintCategories = LastShopViewState.Categories;
	if (!DoesCategoryExist(BlueprintSelectedCategoryId))
	{
		BlueprintSelectedCategoryId = NAME_None;
	}
	for (FCatShopCategoryView& Category : BlueprintCategories)
	{
		Category.bSelected = Category.CategoryId == BlueprintSelectedCategoryId;
	}
	LastShopViewState.Categories = BlueprintCategories;
	RebuildDynamicCategoryButtons();
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

// 购物车文本刷新流程：先把购物车行拼成右侧已选购摘要，再重建 C++ 删除按钮；复杂 WBP 可忽略这段文本，直接按 BlueprintCartLines 创建行。
void UCatShopWidget::RefreshCartPresentation()
{
	TArray<FString> Lines;
	Lines.Reserve(BlueprintCartLines.Num());
	for (const FCatShopCartLineView& Line : BlueprintCartLines)
	{
		Lines.Add(Line.DisplayText.ToString());
	}
	BlueprintCartText = FText::FromString(FString::Join(Lines, TEXT("\n")));
	RebuildDynamicCartLineButtons();
	if (CartLinesPanel)
	{
		CartLinesPanel->SetIsEnabled(true);
	}
}

// 分类存在性流程：NAME_None 代表“全部”且始终合法；非空分类必须来自当前分类投影，避免固定旧按钮切到空白货架。
bool UCatShopWidget::DoesCategoryExist(const FName CategoryId) const
{
	if (CategoryId.IsNone())
	{
		return true;
	}
	return BlueprintCategories.ContainsByPredicate([CategoryId](const FCatShopCategoryView& Category)
	{
		return Category.CategoryId == CategoryId;
	});
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
