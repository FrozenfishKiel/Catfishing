#include "UI/Shop/CatShopWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Styling/SlateBrush.h"
#include "UI/CatUISettings.h"

namespace
{
	const FLinearColor ShopInkColor(0.34f, 0.25f, 0.17f, 1.0f);
	const FLinearColor ShopMutedInkColor(0.58f, 0.49f, 0.38f, 1.0f);
	const FLinearColor ShopPaperColor(0.92f, 0.85f, 0.70f, 0.96f);
	const FLinearColor ShopLightPaperColor(0.98f, 0.92f, 0.78f, 0.98f);
	const FLinearColor ShopPanelColor(0.95f, 0.88f, 0.73f, 0.88f);
	const FLinearColor ShopAwningBlueColor(0.60f, 0.67f, 0.66f, 0.72f);
	const FLinearColor ShopSelectedTabColor(0.90f, 0.76f, 0.38f, 0.95f);
	const FLinearColor ShopButtonHoverColor(0.95f, 0.82f, 0.51f, 0.95f);
	const FLinearColor ShopPayEnabledColor(0.78f, 0.59f, 0.33f, 1.0f);
	const FLinearColor ShopPayHoverColor(0.86f, 0.67f, 0.39f, 1.0f);
	const FLinearColor ShopPayDisabledColor(0.69f, 0.58f, 0.43f, 0.50f);

	// 运行时控件创建流程：给 C++ 自建商店树分配唯一名字，避免 Widget 重新 Construct 时与旧白盒节点撞名。
	template <typename WidgetType>
	WidgetType* CreateShopWidget(UWidgetTree* WidgetTree, const TCHAR* BaseName)
	{
		if (!WidgetTree)
		{
			return nullptr;
		}
		return WidgetTree->ConstructWidget<WidgetType>(
			WidgetType::StaticClass(),
			MakeUniqueObjectName(WidgetTree, WidgetType::StaticClass(), FName(BaseName)));
	}

	// 文本风格流程：统一设置商店页里的字号、颜色和换行策略，让动态行与静态标题保持同一套水彩纸面观感。
	void ConfigureShopText(UTextBlock* TextBlock, const int32 FontSize, const FLinearColor Color,
		const ETextJustify::Type Justification, const bool bAutoWrap)
	{
		if (!TextBlock)
		{
			return;
		}
		FSlateFontInfo Font = TextBlock->GetFont();
		Font.Size = FontSize;
		TextBlock->SetFont(Font);
		TextBlock->SetColorAndOpacity(FSlateColor(Color));
		TextBlock->SetJustification(Justification);
		TextBlock->SetAutoWrapText(bAutoWrap);
	}

	// 文本控件创建流程：创建后立即套用通用商店文本样式，调用方只需要关心语义和放置位置。
	UTextBlock* CreateShopTextBlock(UWidgetTree* WidgetTree, const TCHAR* BaseName, const FText Text,
		const int32 FontSize, const FLinearColor Color, const ETextJustify::Type Justification,
		const bool bAutoWrap = false)
	{
		UTextBlock* TextBlock = CreateShopWidget<UTextBlock>(WidgetTree, BaseName);
		if (!TextBlock)
		{
			return nullptr;
		}
		TextBlock->SetText(Text);
		ConfigureShopText(TextBlock, FontSize, Color, Justification, bAutoWrap);
		return TextBlock;
	}

	// 色块 Brush 创建流程：动态按钮和面板共用同一套无贴图纸面色块，避免 UI 依赖额外资源才能显示。
	FSlateBrush MakeShopBrush(const FLinearColor Color)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::Box;
		Brush.TintColor = FSlateColor(Color);
		Brush.Margin = FMargin(0.18f);
		return Brush;
	}

	// 按钮风格流程：把普通、悬停、按下和禁用色一次写入 Slate 样式；动态卡片和支付按钮都走这里。
	void ApplyShopButtonStyle(UButton* Button, const FLinearColor NormalColor, const FLinearColor HoveredColor,
		const FLinearColor PressedColor, const FLinearColor DisabledColor)
	{
		if (!Button)
		{
			return;
		}
		FButtonStyle Style;
		Style.Normal = MakeShopBrush(NormalColor);
		Style.Hovered = MakeShopBrush(HoveredColor);
		Style.Pressed = MakeShopBrush(PressedColor);
		Style.Disabled = MakeShopBrush(DisabledColor);
		Style.NormalPadding = FMargin(3.0f);
		Style.PressedPadding = FMargin(4.0f, 4.0f, 2.0f, 2.0f);
		Button->SetStyle(Style);
	}

	// 垂直槽配置流程：根据该区域是固定高度还是填满余量设置 SizeRule，并统一写入边距和对齐。
	void ConfigureVerticalSlot(UVerticalBoxSlot* Slot, const FMargin Padding,
		const ESlateSizeRule::Type SizeRule = ESlateSizeRule::Automatic, const float FillValue = 1.0f)
	{
		if (!Slot)
		{
			return;
		}
		FSlateChildSize Size;
		Size.SizeRule = SizeRule;
		Size.Value = FillValue;
		Slot->SetSize(Size);
		Slot->SetPadding(Padding);
		Slot->SetHorizontalAlignment(HAlign_Fill);
		Slot->SetVerticalAlignment(VAlign_Fill);
	}

	// 水平槽配置流程：商品区与购物车区按固定宽度或填充宽度共存，这里统一设置横向 SizeRule 和边距。
	void ConfigureHorizontalSlot(UHorizontalBoxSlot* Slot, const FMargin Padding,
		const ESlateSizeRule::Type SizeRule = ESlateSizeRule::Automatic, const float FillValue = 1.0f)
	{
		if (!Slot)
		{
			return;
		}
		FSlateChildSize Size;
		Size.SizeRule = SizeRule;
		Size.Value = FillValue;
		Slot->SetSize(Size);
		Slot->SetPadding(Padding);
		Slot->SetHorizontalAlignment(HAlign_Fill);
		Slot->SetVerticalAlignment(VAlign_Fill);
	}

	// 控件尺寸流程：只在明确需要稳定尺寸的卡片、页签和支付按钮上写 Override，防止动态文本撑乱布局。
	void ConfigureSizeBox(USizeBox* SizeBox, const float WidthOverride, const float HeightOverride)
	{
		if (!SizeBox)
		{
			return;
		}
		if (WidthOverride > 0.0f)
		{
			SizeBox->SetWidthOverride(WidthOverride);
		}
		if (HeightOverride > 0.0f)
		{
			SizeBox->SetHeightOverride(HeightOverride);
		}
	}

	// 商品图标回退流程：策划表没有贴图时，用商品名和分类给一个稳定符号，保证货架卡片不是纯文字列表。
	FString ResolveShopEntryGlyph(const FCatShopEntryView& Entry)
	{
		const FString DisplayName = Entry.DisplayNameText.ToString();
		const FString CategoryName = Entry.DisplayCategoryId.ToString();
		if (DisplayName.Contains(TEXT("鱼竿")) || CategoryName.Contains(TEXT("Rod"))
			|| CategoryName.Contains(TEXT("rod")))
		{
			return TEXT("/");
		}
		if (DisplayName.Contains(TEXT("红虫")) || DisplayName.Contains(TEXT("蚯蚓"))
			|| CategoryName.Contains(TEXT("Bait")) || CategoryName.Contains(TEXT("bait")))
		{
			return TEXT("~");
		}
		if (DisplayName.Contains(TEXT("小鱼")))
		{
			return TEXT("><>");
		}
		if (DisplayName.Contains(TEXT("面团")) || DisplayName.Contains(TEXT("玉米"))
			|| DisplayName.Contains(TEXT("特制")))
		{
			return TEXT("●");
		}
		if (DisplayName.Contains(TEXT("窝")) || CategoryName.Contains(TEXT("Chum"))
			|| CategoryName.Contains(TEXT("chum")))
		{
			return TEXT("□");
		}
		return TEXT("◇");
	}

	// 购物车图标回退流程：购物车行没有商品完整投影时仍按名称给出近似符号，保证右侧列表和左侧货架视觉一致。
	FString ResolveShopCartLineGlyph(const FCatShopCartLineView& Line)
	{
		const FString DisplayName = Line.DisplayNameText.ToString();
		const FString CategoryName = Line.DisplayCategoryId.ToString();
		if (DisplayName.Contains(TEXT("鱼竿")) || CategoryName.Contains(TEXT("Rod"))
			|| CategoryName.Contains(TEXT("rod")))
		{
			return TEXT("/");
		}
		if (DisplayName.Contains(TEXT("小鱼")))
		{
			return TEXT("><>");
		}
		if (DisplayName.Contains(TEXT("窝")) || CategoryName.Contains(TEXT("Chum"))
			|| CategoryName.Contains(TEXT("chum")))
		{
			return TEXT("□");
		}
		if (DisplayName.Contains(TEXT("面团")) || DisplayName.Contains(TEXT("玉米"))
			|| DisplayName.Contains(TEXT("红虫")) || DisplayName.Contains(TEXT("蚯蚓"))
			|| CategoryName.Contains(TEXT("Bait")) || CategoryName.Contains(TEXT("bait")))
		{
			return TEXT("~");
		}
		return TEXT("◇");
	}

	// 商品库存短文案流程：卡片底部只保留玩家立即需要的信息，完整诊断仍在 Entry.DisplayText 里。
	FText MakeShopEntryMetaText(const FCatShopEntryView& Entry)
	{
		if (Entry.CartCount > 0)
		{
			return FText::FromString(FString::Printf(TEXT("已选 x%d"), Entry.CartCount));
		}
		if (!Entry.bStockAvailable)
		{
			return FText::FromString(TEXT("库存同步中"));
		}
		if (Entry.bUnlimitedStock)
		{
			return FText::FromString(TEXT("库存充足"));
		}
		if (Entry.bSoldOut)
		{
			return FText::FromString(TEXT("已售罄"));
		}
		return FText::FromString(FString::Printf(TEXT("余 %d"), Entry.RemainingStock));
	}

	// 分类标题流程：空分类名回退到稳定 ID，“全部”则使用固定中文，避免策划表漏配时出现空按钮。
	FText MakeShopCategoryLabel(const FCatShopCategoryView& Category)
	{
		if (!Category.DisplayNameText.IsEmpty())
		{
			return Category.DisplayNameText;
		}
		return Category.CategoryId.IsNone() ? FText::FromString(TEXT("全部")) : FText::FromName(Category.CategoryId);
	}
}

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

// 初始化流程：在 UUserWidget::RebuildWidget 取 RootWidget 之前搭建正式商店树；这样 AddToViewport 时玩家拿到的是新 UI，不会先显示旧白盒页。
void UCatShopWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildNativeShopLayout();
}

// 构造流程：
// 1. 先把商店根 Widget 设为可聚焦，让通用 UIOnly 输入模式能把键盘焦点真正交给本页面；后续关闭键才会进入 NativeOnKeyDown。
// 2. 若初始化阶段未能生成完整控件，构造阶段复用当前根面板再建一次，避免旧资产或异常生命周期留下半成品。
// 3. 最后把新布局里的关闭按钮和支付按钮接到统一入口；动态货架、分类和购物车会在 RenderShop 中按 Model 投影生成。
void UCatShopWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	if (!CloseButton || !PayButton || !CategoryButtons || !ShopButtons || !CartLinesPanel || !CartTotalTextBlock)
	{
		BuildNativeShopLayout();
	}
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
		PayButton->SetIsEnabled(true);
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
	if (PayButtonLabelTextBlock)
	{
		PayButtonLabelTextBlock->SetText(BlueprintPayButtonText);
	}
	if (PayButton)
	{
		// 资金不足时支付按钮需要保留悬停命中区，否则 Slate 禁用控件不会触发鼠标旁提示；真正提交仍由 RequestPayCart 的 bCanPayCart 拦住。
		const bool bKeepHoverForDisabledReason = !ViewState.bActionPending;
		PayButton->SetIsEnabled(ViewState.bCanPayCart || bKeepHoverForDisabledReason);
		PayButton->SetToolTipText(ViewState.bCanPayCart ? FText() : ViewState.PayDisabledReasonText);
		if (ViewState.bCanPayCart)
		{
			ApplyShopButtonStyle(PayButton, ShopPayEnabledColor, ShopPayHoverColor,
				FLinearColor(0.67f, 0.45f, 0.23f, 1.0f), ShopPayDisabledColor);
		}
		else
		{
			ApplyShopButtonStyle(PayButton, ShopPayDisabledColor, ShopPayDisabledColor,
				ShopPayDisabledColor, ShopPayDisabledColor);
		}
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

// 正式商店布局流程：
// 1. 优先复用 WBP 已经拥有的根面板并清空旧子节点；没有可用面板时只在初始化阶段补建 Canvas 根。
// 2. 把所有关键控件写回本类的 BindWidgetOptional 成员，让现有 RenderShop、分类刷新和购物车刷新继续复用同一条 MVC 数据链。
// 3. 隐藏纯文本兜底控件但继续保留字段同步，蓝图扩展或诊断读取不会因为界面重做而失去数据。
void UCatShopWidget::BuildNativeShopLayout()
{
	if (!WidgetTree)
	{
		return;
	}

	CloseButton = nullptr;
	WalletTextBlock = nullptr;
	ResultTextBlock = nullptr;
	EntriesTextBlock = nullptr;
	CartTextBlock = nullptr;
	CartTotalTextBlock = nullptr;
	PayButton = nullptr;
	PayButtonLabelTextBlock = nullptr;
	ShopButtons = nullptr;
	CategoryButtons = nullptr;
	CartLinesPanel = nullptr;

	UPanelWidget* RootPanel = Cast<UPanelWidget>(WidgetTree->RootWidget);
	if (!RootPanel)
	{
		RootPanel = CreateShopWidget<UCanvasPanel>(WidgetTree, TEXT("ShopRoot"));
		if (!RootPanel)
		{
			return;
		}
		WidgetTree->RootWidget = RootPanel;
	}
	else
	{
		RootPanel->ClearChildren();
	}
	RootPanel->SetVisibility(ESlateVisibility::Visible);
	RootPanel->SetIsEnabled(true);

	UBorder* PageBorder = CreateShopWidget<UBorder>(WidgetTree, TEXT("ShopPageBorder"));
	UVerticalBox* PageStack = CreateShopWidget<UVerticalBox>(WidgetTree, TEXT("ShopPageStack"));
	if (!PageBorder || !PageStack)
	{
		return;
	}
	PageBorder->SetBrushColor(ShopPaperColor);
	PageBorder->SetPadding(FMargin(24.0f, 18.0f, 24.0f, 24.0f));
	PageBorder->SetContent(PageStack);
	if (UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(RootPanel))
	{
		if (UCanvasPanelSlot* PageSlot = RootCanvas->AddChildToCanvas(PageBorder))
		{
			PageSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			PageSlot->SetOffsets(FMargin(28.0f, 22.0f, 28.0f, 26.0f));
		}
	}
	else
	{
		UPanelSlot* PagePanelSlot = RootPanel->AddChild(PageBorder);
		if (UVerticalBoxSlot* VerticalSlot = Cast<UVerticalBoxSlot>(PagePanelSlot))
		{
			ConfigureVerticalSlot(VerticalSlot, FMargin(28.0f, 22.0f, 28.0f, 26.0f),
				ESlateSizeRule::Fill, 1.0f);
		}
		else if (UHorizontalBoxSlot* HorizontalSlot = Cast<UHorizontalBoxSlot>(PagePanelSlot))
		{
			ConfigureHorizontalSlot(HorizontalSlot, FMargin(28.0f, 22.0f, 28.0f, 26.0f),
				ESlateSizeRule::Fill, 1.0f);
		}
	}

	USizeBox* HeaderSizeBox = CreateShopWidget<USizeBox>(WidgetTree, TEXT("ShopHeaderSizeBox"));
	UVerticalBox* HeaderStack = CreateShopWidget<UVerticalBox>(WidgetTree, TEXT("ShopHeaderStack"));
	if (HeaderSizeBox && HeaderStack)
	{
		ConfigureSizeBox(HeaderSizeBox, 0.0f, 126.0f);
		HeaderSizeBox->AddChild(HeaderStack);
		ConfigureVerticalSlot(PageStack->AddChildToVerticalBox(HeaderSizeBox), FMargin(0.0f, 0.0f, 0.0f, 12.0f));

		USizeBox* AwningSizeBox = CreateShopWidget<USizeBox>(WidgetTree, TEXT("ShopAwningSizeBox"));
		UHorizontalBox* AwningBox = CreateShopWidget<UHorizontalBox>(WidgetTree, TEXT("ShopAwningBox"));
		if (AwningSizeBox && AwningBox)
		{
			ConfigureSizeBox(AwningSizeBox, 0.0f, 42.0f);
			AwningSizeBox->AddChild(AwningBox);
			ConfigureVerticalSlot(HeaderStack->AddChildToVerticalBox(AwningSizeBox), FMargin(0.0f, 0.0f, 0.0f, 8.0f));
			for (int32 SegmentIndex = 0; SegmentIndex < 12; ++SegmentIndex)
			{
				UBorder* Segment = CreateShopWidget<UBorder>(WidgetTree, TEXT("ShopAwningSegment"));
				if (!Segment)
				{
					continue;
				}
				Segment->SetBrushColor(SegmentIndex % 2 == 0 ? ShopLightPaperColor : ShopAwningBlueColor);
				ConfigureHorizontalSlot(AwningBox->AddChildToHorizontalBox(Segment), FMargin(0.0f),
					ESlateSizeRule::Fill, 1.0f);
			}
		}

		UHorizontalBox* TitleRow = CreateShopWidget<UHorizontalBox>(WidgetTree, TEXT("ShopTitleRow"));
		if (TitleRow)
		{
			ConfigureVerticalSlot(HeaderStack->AddChildToVerticalBox(TitleRow), FMargin(0.0f),
				ESlateSizeRule::Fill, 1.0f);

			USizeBox* SignSizeBox = CreateShopWidget<USizeBox>(WidgetTree, TEXT("ShopSignSizeBox"));
			UBorder* SignBorder = CreateShopWidget<UBorder>(WidgetTree, TEXT("ShopSignBorder"));
			UTextBlock* SignText = CreateShopTextBlock(WidgetTree, TEXT("ShopSignText"),
				FText::FromString(TEXT("钓具商店")), 30, ShopInkColor, ETextJustify::Center);
			if (SignSizeBox && SignBorder && SignText)
			{
				ConfigureSizeBox(SignSizeBox, 210.0f, 62.0f);
				SignBorder->SetBrushColor(FLinearColor(0.75f, 0.59f, 0.36f, 0.82f));
				SignBorder->SetPadding(FMargin(12.0f));
				SignBorder->SetContent(SignText);
				SignSizeBox->AddChild(SignBorder);
				ConfigureHorizontalSlot(TitleRow->AddChildToHorizontalBox(SignSizeBox),
					FMargin(0.0f, 0.0f, 18.0f, 0.0f));
			}

			UTextBlock* FishMarkText = CreateShopTextBlock(WidgetTree, TEXT("ShopFishMarkText"),
				FText::FromString(TEXT("><)))>")), 40, FLinearColor(0.45f, 0.49f, 0.46f, 0.72f),
				ETextJustify::Center);
			if (FishMarkText)
			{
				ConfigureHorizontalSlot(TitleRow->AddChildToHorizontalBox(FishMarkText), FMargin(0.0f),
					ESlateSizeRule::Fill, 1.0f);
			}

			WalletTextBlock = CreateShopTextBlock(WidgetTree, TEXT("WalletTextBlock"),
				FText::FromString(TEXT("商店：团队公款 0")), 22, ShopInkColor, ETextJustify::Center);
			if (WalletTextBlock)
			{
				ConfigureHorizontalSlot(TitleRow->AddChildToHorizontalBox(WalletTextBlock),
					FMargin(14.0f, 6.0f, 14.0f, 0.0f));
			}

			USizeBox* CloseSizeBox = CreateShopWidget<USizeBox>(WidgetTree, TEXT("ShopCloseSizeBox"));
			CloseButton = CreateShopWidget<UButton>(WidgetTree, TEXT("CloseButton"));
			UTextBlock* CloseText = CreateShopTextBlock(WidgetTree, TEXT("ShopCloseText"),
				FText::FromString(TEXT("X")), 32, ShopMutedInkColor, ETextJustify::Center);
			if (CloseSizeBox && CloseButton && CloseText)
			{
				ConfigureSizeBox(CloseSizeBox, 58.0f, 58.0f);
				ApplyShopButtonStyle(CloseButton, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f),
					FLinearColor(0.80f, 0.65f, 0.45f, 0.32f),
					FLinearColor(0.70f, 0.50f, 0.32f, 0.38f),
					FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
				if (UButtonSlot* CloseSlot = Cast<UButtonSlot>(CloseButton->AddChild(CloseText)))
				{
					CloseSlot->SetHorizontalAlignment(HAlign_Center);
					CloseSlot->SetVerticalAlignment(VAlign_Center);
				}
				CloseSizeBox->AddChild(CloseButton);
				ConfigureHorizontalSlot(TitleRow->AddChildToHorizontalBox(CloseSizeBox), FMargin(0.0f));
			}
		}
	}

	UHorizontalBox* BodyRow = CreateShopWidget<UHorizontalBox>(WidgetTree, TEXT("ShopBodyRow"));
	if (!BodyRow)
	{
		return;
	}
	ConfigureVerticalSlot(PageStack->AddChildToVerticalBox(BodyRow), FMargin(0.0f),
		ESlateSizeRule::Fill, 1.0f);

	UBorder* ProductBorder = CreateShopWidget<UBorder>(WidgetTree, TEXT("ShopProductBorder"));
	UVerticalBox* ProductStack = CreateShopWidget<UVerticalBox>(WidgetTree, TEXT("ShopProductStack"));
	if (ProductBorder && ProductStack)
	{
		ProductBorder->SetBrushColor(FLinearColor(0.99f, 0.94f, 0.82f, 0.42f));
		ProductBorder->SetPadding(FMargin(18.0f, 16.0f, 18.0f, 12.0f));
		ProductBorder->SetContent(ProductStack);
		ConfigureHorizontalSlot(BodyRow->AddChildToHorizontalBox(ProductBorder),
			FMargin(0.0f, 0.0f, 22.0f, 0.0f), ESlateSizeRule::Fill, 1.0f);

		CategoryButtons = CreateShopWidget<UHorizontalBox>(WidgetTree, TEXT("CategoryButtons"));
		USizeBox* CategorySizeBox = CreateShopWidget<USizeBox>(WidgetTree, TEXT("ShopCategorySizeBox"));
		if (CategoryButtons && CategorySizeBox)
		{
			ConfigureSizeBox(CategorySizeBox, 0.0f, 52.0f);
			CategorySizeBox->AddChild(CategoryButtons);
			ConfigureVerticalSlot(ProductStack->AddChildToVerticalBox(CategorySizeBox),
				FMargin(0.0f, 0.0f, 0.0f, 18.0f));
		}

		UScrollBox* ProductScrollBox = CreateShopWidget<UScrollBox>(WidgetTree, TEXT("ShopProductScrollBox"));
		UWrapBox* ProductWrapBox = CreateShopWidget<UWrapBox>(WidgetTree, TEXT("ShopButtons"));
		if (ProductScrollBox && ProductWrapBox)
		{
			ProductScrollBox->SetScrollBarVisibility(ESlateVisibility::Visible);
			ProductWrapBox->SetInnerSlotPadding(FVector2D(12.0f, 12.0f));
			ShopButtons = ProductWrapBox;
			ProductScrollBox->AddChild(ProductWrapBox);
			ConfigureVerticalSlot(ProductStack->AddChildToVerticalBox(ProductScrollBox), FMargin(0.0f),
				ESlateSizeRule::Fill, 1.0f);
		}
	}

	USizeBox* CartSizeBox = CreateShopWidget<USizeBox>(WidgetTree, TEXT("ShopCartSizeBox"));
	UBorder* CartBorder = CreateShopWidget<UBorder>(WidgetTree, TEXT("ShopCartBorder"));
	UVerticalBox* CartStack = CreateShopWidget<UVerticalBox>(WidgetTree, TEXT("ShopCartStack"));
	if (CartSizeBox && CartBorder && CartStack)
	{
		ConfigureSizeBox(CartSizeBox, 430.0f, 0.0f);
		CartBorder->SetBrushColor(FLinearColor(0.97f, 0.91f, 0.78f, 0.52f));
		CartBorder->SetPadding(FMargin(22.0f, 18.0f, 22.0f, 18.0f));
		CartBorder->SetContent(CartStack);
		CartSizeBox->AddChild(CartBorder);
		ConfigureHorizontalSlot(BodyRow->AddChildToHorizontalBox(CartSizeBox), FMargin(0.0f));

		UTextBlock* CartTitleText = CreateShopTextBlock(WidgetTree, TEXT("ShopCartTitleText"),
			FText::FromString(TEXT("已选购")), 28, ShopInkColor, ETextJustify::Left);
		if (CartTitleText)
		{
			ConfigureVerticalSlot(CartStack->AddChildToVerticalBox(CartTitleText),
				FMargin(0.0f, 0.0f, 0.0f, 12.0f));
		}

		UScrollBox* CartScrollBox = CreateShopWidget<UScrollBox>(WidgetTree, TEXT("ShopCartScrollBox"));
		UVerticalBox* CartListBox = CreateShopWidget<UVerticalBox>(WidgetTree, TEXT("CartLinesPanel"));
		if (CartScrollBox && CartListBox)
		{
			CartScrollBox->SetScrollBarVisibility(ESlateVisibility::Visible);
			CartLinesPanel = CartListBox;
			CartScrollBox->AddChild(CartListBox);
			ConfigureVerticalSlot(CartStack->AddChildToVerticalBox(CartScrollBox),
				FMargin(0.0f, 0.0f, 0.0f, 16.0f), ESlateSizeRule::Fill, 1.0f);
		}

		CartTotalTextBlock = CreateShopTextBlock(WidgetTree, TEXT("CartTotalTextBlock"),
			FText::FromString(TEXT("总计：0")), 30, ShopInkColor, ETextJustify::Right);
		if (CartTotalTextBlock)
		{
			ConfigureVerticalSlot(CartStack->AddChildToVerticalBox(CartTotalTextBlock),
				FMargin(0.0f, 8.0f, 0.0f, 14.0f));
		}

		USizeBox* PaySizeBox = CreateShopWidget<USizeBox>(WidgetTree, TEXT("ShopPaySizeBox"));
		PayButton = CreateShopWidget<UButton>(WidgetTree, TEXT("PayButton"));
		PayButtonLabelTextBlock = CreateShopTextBlock(WidgetTree, TEXT("PayButtonText"),
			FText::FromString(TEXT("支付")), 30, ShopInkColor, ETextJustify::Center);
		if (PaySizeBox && PayButton && PayButtonLabelTextBlock)
		{
			ConfigureSizeBox(PaySizeBox, 0.0f, 72.0f);
			ApplyShopButtonStyle(PayButton, ShopPayDisabledColor, ShopPayDisabledColor,
				ShopPayDisabledColor, ShopPayDisabledColor);
			if (UButtonSlot* PaySlot = Cast<UButtonSlot>(PayButton->AddChild(PayButtonLabelTextBlock)))
			{
				PaySlot->SetHorizontalAlignment(HAlign_Center);
				PaySlot->SetVerticalAlignment(VAlign_Center);
			}
			PaySizeBox->AddChild(PayButton);
			ConfigureVerticalSlot(CartStack->AddChildToVerticalBox(PaySizeBox),
				FMargin(0.0f, 0.0f, 0.0f, 12.0f));
		}

		ResultTextBlock = CreateShopTextBlock(WidgetTree, TEXT("ResultTextBlock"),
			FText::FromString(TEXT("请选择商品加入已选购")), 18, ShopMutedInkColor, ETextJustify::Center, true);
		if (ResultTextBlock)
		{
			ConfigureVerticalSlot(CartStack->AddChildToVerticalBox(ResultTextBlock), FMargin(0.0f));
		}
	}

	EntriesTextBlock = CreateShopTextBlock(WidgetTree, TEXT("EntriesTextBlock"), FText(),
		16, ShopMutedInkColor, ETextJustify::Left, true);
	CartTextBlock = CreateShopTextBlock(WidgetTree, TEXT("CartTextBlock"), FText(),
		16, ShopMutedInkColor, ETextJustify::Left, true);
	if (EntriesTextBlock)
	{
		EntriesTextBlock->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (CartTextBlock)
	{
		CartTextBlock->SetVisibility(ESlateVisibility::Collapsed);
	}
}

// 旧直购按钮补防流程：
// 1. 扫描当前 WidgetTree 里可能仍残留的 Purchase/ClaimFree 命名 Button，正常路径下这些旧子节点已经被正式布局清空。
// 2. 命中的按钮直接折叠并禁用，让异常生命周期或资产残留也不会和新购物车入口同时出现在页面上。
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
// 2. 正常情况下清空本轮 C++ 生成的卡片，再按 DisplayedEntries 逐条生成参考图式商品卡。
// 3. 卡片只保存 EntryId，点击后仍走统一加购广播；价格、库存和可用性只来自 Model 投影。
void UCatShopWidget::RebuildDynamicEntryButtons()
{
	if (!ShopButtons || !WidgetTree)
	{
		return;
	}
	for (UCatShopEntryButton* Button : DynamicEntryButtons)
	{
		if (Button)
		{
			Button->OnClicked.Clear();
		}
	}
	ShopButtons->ClearChildren();
	DynamicEntryButtons.Reset();

	for (const FCatShopEntryView& Entry : BlueprintDisplayedEntries)
	{
		UCatShopEntryButton* EntryButton = WidgetTree->ConstructWidget<UCatShopEntryButton>(
			UCatShopEntryButton::StaticClass(), MakeUniqueObjectName(WidgetTree, UCatShopEntryButton::StaticClass(),
				FName(TEXT("ShopEntryButton"))));
		USizeBox* CardSizeBox = CreateShopWidget<USizeBox>(WidgetTree, TEXT("ShopEntryCardSizeBox"));
		UVerticalBox* CardStack = CreateShopWidget<UVerticalBox>(WidgetTree, TEXT("ShopEntryCardStack"));
		if (!EntryButton || !CardSizeBox || !CardStack)
		{
			continue;
		}

		ConfigureSizeBox(CardSizeBox, 158.0f, 196.0f);
		CardSizeBox->AddChild(CardStack);
		const FLinearColor CardNormalColor = Entry.bActionEnabled
			? FLinearColor(0.98f, 0.92f, 0.80f, 0.88f)
			: FLinearColor(0.74f, 0.68f, 0.56f, 0.45f);
		ApplyShopButtonStyle(EntryButton, CardNormalColor, ShopButtonHoverColor,
			FLinearColor(0.86f, 0.70f, 0.43f, 0.95f), FLinearColor(0.74f, 0.68f, 0.56f, 0.45f));

		UTextBlock* NameText = CreateShopTextBlock(WidgetTree, TEXT("ShopEntryNameText"),
			Entry.DisplayNameText, 19, ShopInkColor, ETextJustify::Center, true);
		if (NameText)
		{
			ConfigureVerticalSlot(CardStack->AddChildToVerticalBox(NameText), FMargin(8.0f, 10.0f, 8.0f, 4.0f));
		}

		UWidget* IconWidget = nullptr;
		if (UTexture2D* IconTexture = Entry.IconOverride.LoadSynchronous())
		{
			USizeBox* IconSizeBox = CreateShopWidget<USizeBox>(WidgetTree, TEXT("ShopEntryIconSizeBox"));
			UImage* IconImage = CreateShopWidget<UImage>(WidgetTree, TEXT("ShopEntryIconImage"));
			if (IconSizeBox && IconImage)
			{
				ConfigureSizeBox(IconSizeBox, 86.0f, 74.0f);
				IconImage->SetBrushFromTexture(IconTexture, true);
				IconSizeBox->AddChild(IconImage);
				IconWidget = IconSizeBox;
			}
		}
		if (!IconWidget)
		{
			IconWidget = CreateShopTextBlock(WidgetTree, TEXT("ShopEntryGlyphText"),
				FText::FromString(ResolveShopEntryGlyph(Entry)), 48,
				FLinearColor(0.45f, 0.38f, 0.29f, 0.88f), ETextJustify::Center);
		}
		if (IconWidget)
		{
			UVerticalBoxSlot* IconSlot = CardStack->AddChildToVerticalBox(IconWidget);
			if (IconSlot)
			{
				FSlateChildSize IconSize;
				IconSize.SizeRule = ESlateSizeRule::Fill;
				IconSize.Value = 1.0f;
				IconSlot->SetSize(IconSize);
				IconSlot->SetPadding(FMargin(8.0f, 2.0f, 8.0f, 4.0f));
				IconSlot->SetHorizontalAlignment(HAlign_Center);
				IconSlot->SetVerticalAlignment(VAlign_Center);
			}
		}

		UHorizontalBox* PriceRow = CreateShopWidget<UHorizontalBox>(WidgetTree, TEXT("ShopEntryPriceRow"));
		if (PriceRow)
		{
			UTextBlock* ShellText = CreateShopTextBlock(WidgetTree, TEXT("ShopEntryShellText"),
				FText::FromString(TEXT("贝")), 18, ShopMutedInkColor, ETextJustify::Right);
			UTextBlock* PriceText = CreateShopTextBlock(WidgetTree, TEXT("ShopEntryPriceText"),
				FText::AsNumber(Entry.UnitPrice), 24, ShopInkColor, ETextJustify::Left);
			if (ShellText)
			{
				ConfigureHorizontalSlot(PriceRow->AddChildToHorizontalBox(ShellText),
					FMargin(0.0f, 0.0f, 6.0f, 0.0f));
			}
			if (PriceText)
			{
				ConfigureHorizontalSlot(PriceRow->AddChildToHorizontalBox(PriceText), FMargin(0.0f));
			}
			UVerticalBoxSlot* PriceSlot = CardStack->AddChildToVerticalBox(PriceRow);
			if (PriceSlot)
			{
				PriceSlot->SetPadding(FMargin(18.0f, 0.0f, 18.0f, 2.0f));
				PriceSlot->SetHorizontalAlignment(HAlign_Center);
				PriceSlot->SetVerticalAlignment(VAlign_Center);
			}
		}

		UTextBlock* MetaText = CreateShopTextBlock(WidgetTree, TEXT("ShopEntryMetaText"),
			MakeShopEntryMetaText(Entry), 15, ShopMutedInkColor, ETextJustify::Center);
		if (MetaText)
		{
			ConfigureVerticalSlot(CardStack->AddChildToVerticalBox(MetaText), FMargin(6.0f, 0.0f, 6.0f, 8.0f));
		}

		if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(EntryButton->AddChild(CardSizeBox)))
		{
			ButtonSlot->SetHorizontalAlignment(HAlign_Center);
			ButtonSlot->SetVerticalAlignment(VAlign_Center);
		}
		EntryButton->SetToolTipText(Entry.DisplayText);
		EntryButton->SetIsEnabled(Entry.bActionEnabled);
		EntryButton->InitializeShopEntry(this, Entry.EntryId);
		UPanelSlot* AddedSlot = ShopButtons->AddChild(EntryButton);
		if (UWrapBoxSlot* WrapSlot = Cast<UWrapBoxSlot>(AddedSlot))
		{
			WrapSlot->SetPadding(FMargin(6.0f));
			WrapSlot->SetHorizontalAlignment(HAlign_Center);
			WrapSlot->SetVerticalAlignment(VAlign_Center);
		}
		DynamicEntryButtons.Add(EntryButton);
	}
}

// 动态分类流程：
// 1. 没有分类容器或 WidgetTree 时只保留 BlueprintCategories 给 WBP 手动渲染。
// 2. 有容器时清空上一轮页签，并按当前真实商品归纳出的分类重新创建“全部/鱼竿/鱼饵/鱼窝”等按钮。
// 3. 每个按钮只保存 CategoryId；选中态只改变按钮外观和 DisplayedEntries，不写回 Model 的 Entries。
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
		}
	}
	CategoryButtons->ClearChildren();
	DynamicCategoryButtons.Reset();

	for (const FCatShopCategoryView& Category : BlueprintCategories)
	{
		UCatShopCategoryButton* CategoryButton = WidgetTree->ConstructWidget<UCatShopCategoryButton>(
			UCatShopCategoryButton::StaticClass(), MakeUniqueObjectName(WidgetTree,
				UCatShopCategoryButton::StaticClass(), FName(TEXT("ShopCategoryButton"))));
		USizeBox* CategorySizeBox = CreateShopWidget<USizeBox>(WidgetTree, TEXT("ShopCategoryButtonSizeBox"));
		UTextBlock* ButtonText = CreateShopTextBlock(WidgetTree, TEXT("ShopCategoryButtonText"),
			MakeShopCategoryLabel(Category), 22, ShopInkColor, ETextJustify::Center);
		if (!CategoryButton || !CategorySizeBox || !ButtonText)
		{
			continue;
		}
		ConfigureSizeBox(CategorySizeBox, 134.0f, 46.0f);
		ApplyShopButtonStyle(CategoryButton,
			Category.bSelected ? ShopSelectedTabColor : ShopPanelColor,
			Category.bSelected ? ShopSelectedTabColor : ShopButtonHoverColor,
			FLinearColor(0.78f, 0.61f, 0.34f, 0.95f),
			ShopPayDisabledColor);
		CategorySizeBox->AddChild(ButtonText);
		if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(CategoryButton->AddChild(CategorySizeBox)))
		{
			ButtonSlot->SetHorizontalAlignment(HAlign_Center);
			ButtonSlot->SetVerticalAlignment(VAlign_Center);
		}
		CategoryButton->SetToolTipText(FText::FromString(FString::Printf(TEXT("%s：%d 件商品"),
			*MakeShopCategoryLabel(Category).ToString(), Category.EntryCount)));
		CategoryButton->SetIsEnabled(true);
		CategoryButton->InitializeShopCategory(this, Category.CategoryId);
		if (UHorizontalBoxSlot* CategorySlot = Cast<UHorizontalBoxSlot>(CategoryButtons->AddChild(CategoryButton)))
		{
			ConfigureHorizontalSlot(CategorySlot, FMargin(0.0f, 0.0f, 10.0f, 0.0f));
		}
		DynamicCategoryButtons.Add(CategoryButton);
	}
}

// 动态购物车流程：
// 1. 没有购物车容器或 WidgetTree 时只保留 BlueprintCartLines 给 WBP 手动渲染。
// 2. 有容器时清空上一轮行控件，并按购物车投影生成右侧“已选购”列表。
// 3. 每一行整行都是删除命中区；玩家点击该行即可删除一份对应商品，支付 pending 时临时锁住。
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
		}
	}
	CartLinesPanel->ClearChildren();
	DynamicCartLineButtons.Reset();

	if (BlueprintCartLines.IsEmpty())
	{
		UTextBlock* EmptyText = CreateShopTextBlock(WidgetTree, TEXT("ShopCartEmptyText"),
			FText::FromString(TEXT("选购的商品会出现在这里")), 18,
			ShopMutedInkColor, ETextJustify::Center, true);
		if (EmptyText)
		{
			ConfigureVerticalSlot(Cast<UVerticalBoxSlot>(CartLinesPanel->AddChild(EmptyText)),
				FMargin(0.0f, 24.0f, 0.0f, 0.0f));
		}
		return;
	}

	for (const FCatShopCartLineView& Line : BlueprintCartLines)
	{
		UCatShopCartLineRemoveButton* RemoveButton = WidgetTree->ConstructWidget<UCatShopCartLineRemoveButton>(
			UCatShopCartLineRemoveButton::StaticClass(), MakeUniqueObjectName(WidgetTree,
				UCatShopCartLineRemoveButton::StaticClass(), FName(TEXT("ShopCartLineRemoveButton"))));
		USizeBox* RowSizeBox = CreateShopWidget<USizeBox>(WidgetTree, TEXT("ShopCartLineSizeBox"));
		UHorizontalBox* RowBox = CreateShopWidget<UHorizontalBox>(WidgetTree, TEXT("ShopCartLineBox"));
		if (!RemoveButton || !RowSizeBox || !RowBox)
		{
			continue;
		}

		ConfigureSizeBox(RowSizeBox, 0.0f, 66.0f);
		RowSizeBox->AddChild(RowBox);
		ApplyShopButtonStyle(RemoveButton, FLinearColor(0.98f, 0.92f, 0.80f, 0.44f), ShopButtonHoverColor,
			FLinearColor(0.78f, 0.61f, 0.34f, 0.82f), ShopPayDisabledColor);

		UWidget* IconWidget = nullptr;
		if (UTexture2D* IconTexture = Line.IconOverride.LoadSynchronous())
		{
			USizeBox* IconSizeBox = CreateShopWidget<USizeBox>(WidgetTree, TEXT("ShopCartIconSizeBox"));
			UImage* IconImage = CreateShopWidget<UImage>(WidgetTree, TEXT("ShopCartIconImage"));
			if (IconSizeBox && IconImage)
			{
				ConfigureSizeBox(IconSizeBox, 42.0f, 42.0f);
				IconImage->SetBrushFromTexture(IconTexture, true);
				IconSizeBox->AddChild(IconImage);
				IconWidget = IconSizeBox;
			}
		}
		if (!IconWidget)
		{
			IconWidget = CreateShopTextBlock(WidgetTree, TEXT("ShopCartGlyphText"),
				FText::FromString(ResolveShopCartLineGlyph(Line)), 30,
				FLinearColor(0.45f, 0.38f, 0.29f, 0.88f), ETextJustify::Center);
		}
		if (IconWidget)
		{
			ConfigureHorizontalSlot(RowBox->AddChildToHorizontalBox(IconWidget),
				FMargin(10.0f, 0.0f, 12.0f, 0.0f));
		}

		UTextBlock* NameText = CreateShopTextBlock(WidgetTree, TEXT("ShopCartNameText"),
			Line.DisplayNameText, 18, ShopInkColor, ETextJustify::Left, true);
		if (NameText)
		{
			ConfigureHorizontalSlot(RowBox->AddChildToHorizontalBox(NameText),
				FMargin(0.0f, 0.0f, 8.0f, 0.0f), ESlateSizeRule::Fill, 1.0f);
		}

		UTextBlock* CountText = CreateShopTextBlock(WidgetTree, TEXT("ShopCartCountText"),
			FText::FromString(FString::Printf(TEXT("x %d"), Line.CartCount)), 18,
			ShopInkColor, ETextJustify::Center);
		if (CountText)
		{
			ConfigureHorizontalSlot(RowBox->AddChildToHorizontalBox(CountText),
				FMargin(0.0f, 0.0f, 12.0f, 0.0f));
		}

		UTextBlock* PriceText = CreateShopTextBlock(WidgetTree, TEXT("ShopCartPriceText"),
			FText::FromString(FString::Printf(TEXT("贝 %d"), Line.LineTotalPrice)), 18,
			ShopInkColor, ETextJustify::Right);
		if (PriceText)
		{
			ConfigureHorizontalSlot(RowBox->AddChildToHorizontalBox(PriceText),
				FMargin(0.0f, 0.0f, 12.0f, 0.0f));
		}

		UTextBlock* DeleteText = CreateShopTextBlock(WidgetTree, TEXT("ShopCartDeleteText"),
			FText::FromString(TEXT("删")), 20, ShopMutedInkColor, ETextJustify::Center);
		if (DeleteText)
		{
			ConfigureHorizontalSlot(RowBox->AddChildToHorizontalBox(DeleteText),
				FMargin(0.0f, 0.0f, 8.0f, 0.0f));
		}

		if (UButtonSlot* RowButtonSlot = Cast<UButtonSlot>(RemoveButton->AddChild(RowSizeBox)))
		{
			RowButtonSlot->SetHorizontalAlignment(HAlign_Fill);
			RowButtonSlot->SetVerticalAlignment(VAlign_Center);
		}
		RemoveButton->SetToolTipText(FText::FromString(FString::Printf(TEXT("删除一份：%s"),
			*Line.DisplayNameText.ToString())));
		RemoveButton->SetIsEnabled(!LastShopViewState.bActionPending);
		RemoveButton->InitializeCartLine(this, Line.EntryId);
		ConfigureVerticalSlot(Cast<UVerticalBoxSlot>(CartLinesPanel->AddChild(RemoveButton)),
			FMargin(0.0f, 0.0f, 0.0f, 10.0f));
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
