#include "UI/Shop/CatShopWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/Texture2D.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Logging/CatLog.h"
#include "UI/CatUISettings.h"

namespace
{
	// 价格文本流程：所有商店金额都用同一个短格式显示，右侧总计和服务器扣款仍读取 Model 投影中的数值字段。
	FText MakeShellPriceText(const int32 Price)
	{
		return FText::FromString(FString::Printf(TEXT("贝 %d"), Price));
	}

	// 后备符号流程：
	// 1. 优先取展示名首字，让图标缺失时仍能从数据本身得到一个可见识别。
	// 2. 展示名为空时才取稳定 ID 首字，不按鱼竿、鱼饵、鱼窝这些具体业务名写死规则。
	// 3. 这个符号只服务临时视觉识别，分类、结算和交付都继续读取 ViewState 投影字段。
	FString MakeFallbackGlyphText(const FText& DisplayNameText, const FName StableFallbackId)
	{
		FString DisplayName = DisplayNameText.ToString();
		DisplayName.TrimStartAndEndInline();
		if (!DisplayName.IsEmpty())
		{
			return DisplayName.Left(1);
		}
		FString FallbackId = StableFallbackId.ToString();
		FallbackId.TrimStartAndEndInline();
		if (!FallbackId.IsEmpty())
		{
			return FallbackId.Left(1).ToUpper();
		}
		return FString();
	}

	// 文本同步流程：展示 TextBlock 是可选绑定点；存在就写数据，不存在就交给蓝图自己的表现层处理。
	void SetOptionalText(UTextBlock* TextBlock, const FText& Text)
	{
		if (TextBlock)
		{
			TextBlock->SetText(Text);
		}
	}

	// 可见性同步流程：展示装饰是可选绑定点；存在就跟随状态，不存在不影响主交互链路。
	void SetOptionalVisibility(UWidget* Widget, const ESlateVisibility Visibility)
	{
		if (Widget)
		{
			Widget->SetVisibility(Visibility);
		}
	}

	// 图标同步流程：
	// 1. 有正式图标时同步到可选 Image 并显示。
	// 2. 没有图标或加载失败时折叠 Image，让 WBP 可以露出文字后备层。
	// 3. 图标只来自商品或购物车投影，不在 UI 里按商品名推断资源。
	void ApplyOptionalIcon(UImage* IconImage, const TSoftObjectPtr<UTexture2D>& IconOverride)
	{
		if (!IconImage)
		{
			return;
		}
		UTexture2D* IconTexture = IconOverride.IsNull() ? nullptr : IconOverride.LoadSynchronous();
		if (!IconTexture)
		{
			IconImage->SetVisibility(ESlateVisibility::Collapsed);
			return;
		}
		IconImage->SetBrushFromTexture(IconTexture, false);
		IconImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	// 分类标题流程：Model 通常会给 DisplayNameText；这里补足空文本时的稳定展示，避免页签出现无字按钮。
	FText MakeCategoryLabelText(const FCatShopCategoryView& Category)
	{
		if (!Category.DisplayNameText.IsEmpty())
		{
			return Category.DisplayNameText;
		}
		return Category.CategoryId.IsNone() ? FText::FromString(TEXT("全部")) : FText::FromName(Category.CategoryId);
	}

	// WBP 类加载流程：动态列表只接受正式蓝图子控件类；缺类时记录错误并停止生成该区域，避免运行时画出第二套界面。
	template <typename WidgetType>
	TSubclassOf<WidgetType> LoadRequiredDesignerWidgetClass(const TSoftClassPtr<WidgetType>& WidgetClass,
		const TCHAR* WidgetRole)
	{
		TSubclassOf<WidgetType> LoadedClass = WidgetClass.LoadSynchronous();
		if (!LoadedClass)
		{
			UE_LOG(LogCatUI, Error, TEXT("Event=ui_shop_designer_widget_class_missing Role=%s ClassPath=%s"),
				WidgetRole, *WidgetClass.ToString());
		}
		return LoadedClass;
	}
}

// 分类页签初始化流程：
// 1. 保存父商店页弱引用和当前分类投影，旧值会被整行覆盖。
// 2. 重新绑定 Designer 按钮点击，避免重建时出现重复选择。
// 3. 最后把投影写入命名控件并触发蓝图扩展表现。
void UCatShopCategoryTabWidget::InitializeCategoryTab(UCatShopWidget* InOwnerShopWidget,
	const FCatShopCategoryView& InCategoryView)
{
	OwnerShopWidget = InOwnerShopWidget;
	CategoryView = InCategoryView;
	if (CategoryButton)
	{
		CategoryButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCategoryButtonClicked);
		CategoryButton->OnClicked.AddDynamic(this, &ThisClass::HandleCategoryButtonClicked);
	}
	ApplyCategoryTabToDesignerWidgets();
}

// 分类页签投影读取流程：直接返回当前页签缓存的只读行；外部只能观察，不能通过这里改 Model 或服务器状态。
const FCatShopCategoryView& UCatShopCategoryTabWidget::GetCategoryView() const
{
	return CategoryView;
}

// 分类页签选择流程：玩家点击本页签时只转发 CategoryId；父商店页会用完整 Entries 在本地重建 DisplayedEntries。
void UCatShopCategoryTabWidget::RequestSelectThisCategory()
{
	if (UCatShopWidget* ShopWidget = OwnerShopWidget.Get())
	{
		ShopWidget->RequestSelectCategory(CategoryView.CategoryId);
	}
}

// 分类页签构造流程：先让 UUserWidget 完成 BindWidget，再补绑按钮和刷新数据；页签数量来自父页数据数组。
void UCatShopCategoryTabWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (CategoryButton)
	{
		CategoryButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCategoryButtonClicked);
		CategoryButton->OnClicked.AddDynamic(this, &ThisClass::HandleCategoryButtonClicked);
	}
	ApplyCategoryTabToDesignerWidgets();
}

// 分类页签析构流程：解除本页签按钮绑定后交给父类释放 WidgetTree；父商店页会清理数组引用。
void UCatShopCategoryTabWidget::NativeDestruct()
{
	if (CategoryButton)
	{
		CategoryButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCategoryButtonClicked);
	}
	Super::NativeDestruct();
}

// 分类页签点击流程：按钮事件只转到本页签的公开选择入口；父页存在性和分类有效性继续留在统一链路里。
void UCatShopCategoryTabWidget::HandleCategoryButtonClicked()
{
	RequestSelectThisCategory();
}

// 分类页签刷新流程：
// 1. 从分类投影写入显示名和数量，分类数量来自 Model/ViewState，不来自主 WBP 固定槽位。
// 2. 选中装饰只跟随当前客户端本地选中态，其他玩家选择自己的分类不会影响这里。
// 3. 最后触发蓝图扩展事件，后续正式视觉只改页签 WBP。
void UCatShopCategoryTabWidget::ApplyCategoryTabToDesignerWidgets()
{
	const FText CategoryLabel = MakeCategoryLabelText(CategoryView);
	SetOptionalText(CategoryLabelTextBlock, CategoryLabel);
	SetOptionalText(CategoryCountTextBlock,
		FText::FromString(FString::Printf(TEXT("%d"), CategoryView.EntryCount)));
	SetOptionalVisibility(CategorySelectedVisual, CategoryView.bSelected
		? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (CategoryButton)
	{
		CategoryButton->SetIsEnabled(true);
		CategoryButton->SetToolTipText(FText::FromString(FString::Printf(TEXT("%s：%d 件商品"),
			*CategoryLabel.ToString(), CategoryView.EntryCount)));
	}
	BP_RenderCategoryTab(CategoryView);
}

// 商品卡初始化流程：
// 1. 保存父商店页弱引用和当前商品投影，旧值会被整行覆盖。
// 2. 重新绑定 Designer 按钮点击，避免复用或重建时出现重复加购。
// 3. 最后把投影写入命名控件并触发蓝图扩展表现。
void UCatShopGoodsItemWidget::InitializeGoodsItem(UCatShopWidget* InOwnerShopWidget,
	const FCatShopEntryView& InEntryView)
{
	OwnerShopWidget = InOwnerShopWidget;
	EntryView = InEntryView;
	if (GoodsButton)
	{
		GoodsButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleGoodsButtonClicked);
		GoodsButton->OnClicked.AddDynamic(this, &ThisClass::HandleGoodsButtonClicked);
	}
	ApplyGoodsItemToDesignerWidgets();
}

// 商品卡投影读取流程：直接返回当前卡片缓存的只读行；外部只能观察，不能通过这里改购物车或服务器订单。
const FCatShopEntryView& UCatShopGoodsItemWidget::GetEntryView() const
{
	return EntryView;
}

// 商品卡加购流程：玩家点击本卡时只转发 EntryId；父商店页继续检查 pending，Model 继续检查库存和价格投影。
void UCatShopGoodsItemWidget::RequestAddThisEntryToCart()
{
	if (UCatShopWidget* ShopWidget = OwnerShopWidget.Get())
	{
		ShopWidget->RequestAddEntryToCart(EntryView.EntryId);
	}
}

// 商品卡构造流程：先让 UUserWidget 完成 BindWidget，再补绑按钮和刷新数据；这样蓝图资产负责结构，C++ 只认命名控件。
void UCatShopGoodsItemWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (GoodsButton)
	{
		GoodsButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleGoodsButtonClicked);
		GoodsButton->OnClicked.AddDynamic(this, &ThisClass::HandleGoodsButtonClicked);
	}
	ApplyGoodsItemToDesignerWidgets();
}

// 商品卡析构流程：解除本卡按钮绑定后交给父类释放 WidgetTree；父商店页会清理数组引用。
void UCatShopGoodsItemWidget::NativeDestruct()
{
	if (GoodsButton)
	{
		GoodsButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleGoodsButtonClicked);
	}
	Super::NativeDestruct();
}

// 商品卡点击流程：按钮事件只转到本卡的公开加购入口；父页存在性、EntryId、库存和 pending 校验继续留在后续统一链路里。
void UCatShopGoodsItemWidget::HandleGoodsButtonClicked()
{
	RequestAddThisEntryToCart();
}

// 商品卡刷新流程：
// 1. 写入商品名、图标、价格和库存/已选短提示，纯展示控件缺失时只跳过对应表现。
// 2. 图标来自商品投影，后备符号只取展示名或 EntryId 首字，不按具体商品类别写死 UI 规则。
// 3. 按 Model 给出的 bActionEnabled 禁用按钮并显示 Designer 遮罩，视觉样式仍留在 WBP 里。
// 4. 最后触发蓝图扩展事件，后续正式贴图或动效可以只改 WBP。
void UCatShopGoodsItemWidget::ApplyGoodsItemToDesignerWidgets()
{
	SetOptionalText(GoodsNameTextBlock, EntryView.DisplayNameText);
	ApplyOptionalIcon(GoodsIconImage, EntryView.IconOverride);
	SetOptionalText(GoodsGlyphTextBlock, FText::FromString(ResolveGoodsGlyph()));
	SetOptionalText(GoodsPriceTextBlock, MakeShellPriceText(EntryView.UnitPrice));
	SetOptionalText(GoodsMetaTextBlock, MakeGoodsMetaText());
	SetOptionalVisibility(GoodsDisabledVisual, EntryView.bActionEnabled
		? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	if (GoodsButton)
	{
		GoodsButton->SetIsEnabled(EntryView.bActionEnabled);
		GoodsButton->SetToolTipText(EntryView.DisplayText);
	}
	BP_RenderGoodsItem(EntryView);
}

// 商品卡图形流程：把当前行的展示名和 EntryId 交给统一后备符号函数，避免卡片维护第二套商品分类表。
FString UCatShopGoodsItemWidget::ResolveGoodsGlyph() const
{
	return MakeFallbackGlyphText(EntryView.DisplayNameText, EntryView.EntryId);
}

// 商品卡状态文案流程：优先展示购物车数量，其次展示库存状态；这些都是本地投影，不作为服务器结算输入。
FText UCatShopGoodsItemWidget::MakeGoodsMetaText() const
{
	if (EntryView.CartCount > 0)
	{
		return FText::FromString(FString::Printf(TEXT("已选 x%d"), EntryView.CartCount));
	}
	if (!EntryView.bStockAvailable)
	{
		return FText::FromString(TEXT("库存同步中"));
	}
	if (EntryView.bUnlimitedStock)
	{
		return FText::FromString(TEXT("库存充足"));
	}
	if (EntryView.bSoldOut)
	{
		return FText::FromString(TEXT("已售罄"));
	}
	return FText::FromString(FString::Printf(TEXT("余 %d"), EntryView.RemainingStock));
}

// 购物车行初始化流程：保存父页、购物车行和 pending 状态，重建删除绑定，并刷新 Designer 命名控件。
void UCatShopCartLineWidget::InitializeCartLine(UCatShopWidget* InOwnerShopWidget,
	const FCatShopCartLineView& InCartLineView, const bool bInActionPending)
{
	OwnerShopWidget = InOwnerShopWidget;
	CartLineView = InCartLineView;
	bActionPending = bInActionPending;
	if (CartLineRemoveButton)
	{
		CartLineRemoveButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleRemoveButtonClicked);
		CartLineRemoveButton->OnClicked.AddDynamic(this, &ThisClass::HandleRemoveButtonClicked);
	}
	ApplyCartLineToDesignerWidgets();
}

// 购物车行投影读取流程：返回当前行缓存的展示数据；外部不能通过这个返回值绕开父商店页删除入口。
const FCatShopCartLineView& UCatShopCartLineWidget::GetCartLineView() const
{
	return CartLineView;
}

// 购物车行删除流程：玩家点击垃圾桶时只转发 EntryId；父商店页继续检查 pending 并广播本地删除意图。
void UCatShopCartLineWidget::RequestRemoveThisCartLine()
{
	if (UCatShopWidget* ShopWidget = OwnerShopWidget.Get())
	{
		ShopWidget->RequestRemoveOneCartItem(CartLineView.EntryId);
	}
}

// 购物车行构造流程：BindWidget 完成后补绑垃圾桶按钮，并把已缓存的行投影写到各文本控件。
void UCatShopCartLineWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (CartLineRemoveButton)
	{
		CartLineRemoveButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleRemoveButtonClicked);
		CartLineRemoveButton->OnClicked.AddDynamic(this, &ThisClass::HandleRemoveButtonClicked);
	}
	ApplyCartLineToDesignerWidgets();
}

// 购物车行析构流程：解除删除按钮绑定；购物车真实状态由父 Model 保存，不由行控件销毁决定。
void UCatShopCartLineWidget::NativeDestruct()
{
	if (CartLineRemoveButton)
	{
		CartLineRemoveButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleRemoveButtonClicked);
	}
	Super::NativeDestruct();
}

// 删除按钮流程：按钮事件只转到本行的公开删除入口；父页存在性、EntryId 和 pending 校验继续留在后续统一链路里。
void UCatShopCartLineWidget::HandleRemoveButtonClicked()
{
	RequestRemoveThisCartLine();
}

// 购物车行刷新流程：
// 1. 写入商品名、图标、数量和行小计，纯展示控件缺失时只跳过对应表现。
// 2. 图标来自购物车行投影，后备符号只取展示名或 EntryId 首字，不在 UI 内写死商品类型。
// 3. pending 时禁用删除按钮，避免支付请求发出后本地购物车继续变化。
// 4. 库存或价格失效时显示无效提示装饰，支付入口会因此保持不可提交。
// 5. 最后触发蓝图扩展事件，后续正式贴图或动效可以只改 WBP。
void UCatShopCartLineWidget::ApplyCartLineToDesignerWidgets()
{
	SetOptionalText(CartLineNameTextBlock, CartLineView.DisplayNameText);
	ApplyOptionalIcon(CartLineIconImage, CartLineView.IconOverride);
	SetOptionalText(CartLineCountTextBlock,
		FText::FromString(FString::Printf(TEXT("x %d"), CartLineView.CartCount)));
	SetOptionalText(CartLinePriceTextBlock, MakeShellPriceText(CartLineView.LineTotalPrice));
	SetOptionalText(CartLineGlyphTextBlock, FText::FromString(ResolveCartLineGlyph()));
	SetOptionalVisibility(CartLineInvalidVisual, CartLineView.bLineAvailable
		? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	if (CartLineRemoveButton)
	{
		CartLineRemoveButton->SetIsEnabled(!bActionPending);
		CartLineRemoveButton->SetToolTipText(FText::FromString(FString::Printf(TEXT("删除一份：%s"),
			*CartLineView.DisplayNameText.ToString())));
	}
	BP_RenderCartLine(CartLineView, bActionPending);
}

// 购物车行图形流程：复用展示名和 EntryId，不从购物车行之外读取商品数据或写死商品分类。
FString UCatShopCartLineWidget::ResolveCartLineGlyph() const
{
	return MakeFallbackGlyphText(CartLineView.DisplayNameText, CartLineView.EntryId);
}

// 商店页构造流程：默认指向正式蓝图子控件资产；如果资产缺失只记录日志，不在运行时生成另一套视觉。
UCatShopWidget::UCatShopWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CategoryTabWidgetClass = TSoftClassPtr<UCatShopCategoryTabWidget>(
		FSoftObjectPath(TEXT("/Game/UI/Shop/WBP_CatShopCategoryTab.WBP_CatShopCategoryTab_C")));
	GoodsItemWidgetClass = TSoftClassPtr<UCatShopGoodsItemWidget>(
		FSoftObjectPath(TEXT("/Game/UI/Shop/WBP_CatShopGoodsItem.WBP_CatShopGoodsItem_C")));
	CartLineWidgetClass = TSoftClassPtr<UCatShopCartLineWidget>(
		FSoftObjectPath(TEXT("/Game/UI/Shop/WBP_CatShopCartLine.WBP_CatShopCartLine_C")));
}

// 构造流程：
// 1. 先把商店根 Widget 设为可聚焦，让通用 UIOnly 输入模式能把键盘焦点真正交给本页面。
// 2. 再把 Designer 里的关闭和支付按钮接到统一入口；分类、货架和购物车都等待数据驱动重建。
// 3. 最后等待 RenderShop 输入真实 ViewState；没有投影前不主动生成分类页签、货架或购物车内容。
void UCatShopWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	if (CloseButton)
	{
		CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestCloseShop);
		CloseButton->OnClicked.AddDynamic(this, &ThisClass::RequestCloseShop);
	}
	if (PayButton)
	{
		PayButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestPayCart);
		PayButton->OnClicked.AddDynamic(this, &ThisClass::RequestPayCart);
	}
}

// 析构流程：
// 1. 先移除本次动态创建的分类页签、商品卡和购物车行，让子 WBP 自己解绑内部按钮。
// 2. 再解除本 View 绑定到 Designer 按钮上的 UMG 点击事件，防止再次入视口时叠加回调。
// 3. 保留外部意图订阅；RemoveFromParent 只是页面离开视口，PageController::Unbind 才是外部订阅结束点。
void UCatShopWidget::NativeDestruct()
{
	for (UCatShopCategoryTabWidget* CategoryTab : DynamicCategoryTabs)
	{
		if (CategoryTab)
		{
			CategoryTab->RemoveFromParent();
		}
	}
	DynamicCategoryTabs.Reset();
	for (UCatShopGoodsItemWidget* GoodsItem : DynamicGoodsItems)
	{
		if (GoodsItem)
		{
			GoodsItem->RemoveFromParent();
		}
	}
	DynamicGoodsItems.Reset();
	for (UCatShopCartLineWidget* CartLineWidget : DynamicCartLineWidgets)
	{
		if (CartLineWidget)
		{
			CartLineWidget->RemoveFromParent();
		}
	}
	DynamicCartLineWidgets.Reset();
	if (CloseButton)
	{
		CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestCloseShop);
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
// 1. 先判断是否从关闭进入打开；新开商店时默认回到“全部”，同一打开会话内保留玩家当前分类。
// 2. 再缓存 Model 输入的完整投影，并复制蓝图可读字段。
// 3. 用最新 Categories 重建分类页签，并用最新真实 Entries 和当前本地分类重新生成 DisplayedEntries。
// 4. 最后触发 BP 渲染扩展并在 pending 时拉回键盘焦点，避免动态按钮重建后关闭键失效。
void UCatShopWidget::RenderShop(const FCatShopViewState& ViewState)
{
	const bool bOpeningFromClosed = ViewState.bOpen && !LastShopViewState.bOpen;
	LastShopViewState = ViewState;
	if (!ViewState.bOpen || bOpeningFromClosed)
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
	RebuildCartLines();
	RefreshMainDesignerWidgets();
	BP_RenderShop(LastShopViewState);

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

// 支付请求流程：只在 Model 投影允许支付且没有等待回包时广播；禁用原因留给 WBP 悬停显示，不在这里替服务器裁决。
void UCatShopWidget::RequestPayCart()
{
	if (!LastShopViewState.bCanPayCart || LastShopViewState.bActionPending)
	{
		return;
	}
	OnCartPayRequested.Broadcast();
}

// 分类请求流程：只改本 Widget 的本地分类 ID，再用完整 Entries 重新生成 DisplayedEntries 并唤醒 BP 渲染扩展；不会写回 Model 或服务器。
void UCatShopWidget::RequestSelectCategory(const FName CategoryId)
{
	if (!DoesCategoryExist(CategoryId))
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_shop_category_missing CategoryId=%s"), *CategoryId.ToString());
		return;
	}
	BlueprintSelectedCategoryId = CategoryId;
	RefreshCategoryPresentation();
	RefreshDisplayedEntries();
	RefreshMainDesignerWidgets();
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

// 动态分类页签流程：
// 1. 清空分类容器中的 Designer 样例页签或上一轮动态页签。
// 2. 加载正式分类页签 WBP 类，失败时只记录错误并跳过该区域。
// 3. 逐条读取 Categories 创建页签 WBP；页签只保存 CategoryId 并转发本地分类选择请求。
// 4. C++ 不再改子项 Slot 间距、对齐或尺寸，所有排版表现都由父容器和子 WBP 资产决定。
void UCatShopWidget::RebuildCategoryTabs()
{
	if (!CategoryTabsPanel)
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_shop_category_tabs_panel_missing"));
		DynamicCategoryTabs.Reset();
		return;
	}
	CategoryTabsPanel->ClearChildren();
	DynamicCategoryTabs.Reset();

	const TSubclassOf<UCatShopCategoryTabWidget> CategoryTabClass = LoadRequiredDesignerWidgetClass(
		CategoryTabWidgetClass, TEXT("CategoryTab"));
	if (!CategoryTabClass)
	{
		return;
	}
	for (const FCatShopCategoryView& Category : BlueprintCategories)
	{
		UCatShopCategoryTabWidget* CategoryTab = CreateWidget<UCatShopCategoryTabWidget>(
			this, CategoryTabClass, MakeUniqueObjectName(this, CategoryTabClass, FName(TEXT("ShopCategoryTab"))));
		if (!CategoryTab)
		{
			UE_LOG(LogCatUI, Error, TEXT("Event=ui_shop_category_tab_create_failed CategoryId=%s"),
				*Category.CategoryId.ToString());
			continue;
		}
		CategoryTab->InitializeCategoryTab(this, Category);
		CategoryTabsPanel->AddChild(CategoryTab);
		DynamicCategoryTabs.Add(CategoryTab);
	}
	CategoryTabsPanel->SetIsEnabled(!LastShopViewState.bActionPending);
}

// 动态货架流程：
// 1. 清空商品容器中的 Designer 样例卡或上一轮动态卡片；样例只服务编辑器预览，不进入真实商品状态。
// 2. 加载正式商品卡 WBP 类，失败时只记录错误并跳过该区域。
// 3. 逐条读取 DisplayedEntries 创建商品卡 WBP，卡片只保存 EntryId 并转发加购请求。
// 4. 商品卡尺寸、留白和网格对齐属于 WBP 表现层，后端只决定创建哪些数据实例。
void UCatShopWidget::RebuildGoodsItems()
{
	if (!ShopButtons)
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_shop_goods_panel_missing"));
		DynamicGoodsItems.Reset();
		return;
	}
	ShopButtons->ClearChildren();
	DynamicGoodsItems.Reset();

	const TSubclassOf<UCatShopGoodsItemWidget> GoodsItemClass = LoadRequiredDesignerWidgetClass(
		GoodsItemWidgetClass, TEXT("GoodsItem"));
	if (!GoodsItemClass)
	{
		return;
	}
	for (const FCatShopEntryView& Entry : BlueprintDisplayedEntries)
	{
		UCatShopGoodsItemWidget* GoodsItem = CreateWidget<UCatShopGoodsItemWidget>(
			this, GoodsItemClass, MakeUniqueObjectName(this, GoodsItemClass, FName(TEXT("ShopGoodsItem"))));
		if (!GoodsItem)
		{
			UE_LOG(LogCatUI, Error, TEXT("Event=ui_shop_goods_item_create_failed EntryId=%s"), *Entry.EntryId.ToString());
			continue;
		}
		GoodsItem->InitializeGoodsItem(this, Entry);
		ShopButtons->AddChild(GoodsItem);
		DynamicGoodsItems.Add(GoodsItem);
	}
	ShopButtons->SetIsEnabled(!LastShopViewState.bActionPending);
}

// 动态购物车流程：
// 1. 清空购物车容器中的 Designer 样例行或上一轮动态行。
// 2. 加载正式购物车行 WBP 类，失败时只记录错误并跳过该区域。
// 3. 每一行只保存 EntryId、展示投影和 pending 状态，删除按钮只删除一份对应商品。
// 4. 购物车行的行高、间距和横向排版留给 WBP，C++ 不再写 Slot 样式。
void UCatShopWidget::RebuildCartLines()
{
	if (!CartLinesPanel)
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_shop_cart_lines_panel_missing"));
		DynamicCartLineWidgets.Reset();
		return;
	}
	CartLinesPanel->ClearChildren();
	DynamicCartLineWidgets.Reset();

	const TSubclassOf<UCatShopCartLineWidget> CartLineClass = LoadRequiredDesignerWidgetClass(
		CartLineWidgetClass, TEXT("CartLine"));
	if (!CartLineClass)
	{
		return;
	}
	for (const FCatShopCartLineView& Line : BlueprintCartLines)
	{
		UCatShopCartLineWidget* CartLineWidget = CreateWidget<UCatShopCartLineWidget>(
			this, CartLineClass, MakeUniqueObjectName(this, CartLineClass, FName(TEXT("ShopCartLine"))));
		if (!CartLineWidget)
		{
			UE_LOG(LogCatUI, Error, TEXT("Event=ui_shop_cart_line_create_failed EntryId=%s"), *Line.EntryId.ToString());
			continue;
		}
		CartLineWidget->InitializeCartLine(this, Line, LastShopViewState.bActionPending);
		CartLinesPanel->AddChild(CartLineWidget);
		DynamicCartLineWidgets.Add(CartLineWidget);
	}
	CartLinesPanel->SetIsEnabled(!LastShopViewState.bActionPending);
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
	RebuildCategoryTabs();
}

// 分类显示刷新流程：
// 1. 从 LastShopViewState.Entries 重新生成本地 DisplayedEntries；NAME_None 表示“全部”。
// 2. 商品区只读取 DisplayedEntries，避免分类切换时误改完整真实商品数组。
// 3. 本函数不调用 BP_RenderShop，调用方会在整轮刷新完成后统一唤醒蓝图表现。
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
	RebuildGoodsItems();
}

// 主控件刷新流程：
// 1. 把 Model 投影中的公款、结果、总计和支付文案写入主 WBP 命名控件，纯展示控件缺失时只跳过对应表现。
// 2. 支付按钮在不可支付或等待回包时禁用，保证资金不足和 pending 期间点击都不会触发。
// 3. 禁用提示层只在存在禁用原因时显示并接管 Tooltip；这样按钮不可点时仍能在鼠标旁显示原因。
void UCatShopWidget::RefreshMainDesignerWidgets()
{
	SetOptionalText(WalletTextBlock, BlueprintWalletText);
	SetOptionalText(ResultTextBlock, BlueprintResultText);
	SetOptionalText(CartTotalTextBlock, BlueprintCartTotalText);
	SetOptionalText(PayButtonLabelTextBlock, BlueprintPayButtonText);

	const bool bCanClickPay = LastShopViewState.bCanPayCart && !LastShopViewState.bActionPending;
	if (PayButton)
	{
		PayButton->SetIsEnabled(bCanClickPay);
	}

	const bool bShowDisabledHint = !bCanClickPay && !BlueprintPayDisabledReasonText.IsEmpty();
	if (PayDisabledHintLayer)
	{
		PayDisabledHintLayer->SetVisibility(bShowDisabledHint ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		PayDisabledHintLayer->SetIsEnabled(bShowDisabledHint);
		PayDisabledHintLayer->SetToolTipText(BlueprintPayDisabledReasonText);
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
// 2. Escape 始终作为模态 UI 关闭键。
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
