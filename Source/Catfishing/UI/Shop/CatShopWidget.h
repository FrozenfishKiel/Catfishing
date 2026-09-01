#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Shop/CatShopTypes.h"
#include "CatShopWidget.generated.h"

class UButton;
class UImage;
class UPanelWidget;
class UTextBlock;
class UWidget;
class UCatShopWidget;

/** 商店关闭意图；交互对象拥有页面生命周期，Widget 只发关闭请求。 */
DECLARE_MULTICAST_DELEGATE(FCatShopCloseRequested);

/** 商品加入购物车意图；Widget 只提交货架 EntryId，Model 决定本地购物车是否接收。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatShopEntryAddToCartRequested, FName);

/** 购物车单行删除意图；Widget 只提交货架 EntryId，Model 删除对应商品的一份选购。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatShopCartLineRemoveRequested, FName);

/** 购物车支付意图；PageController 会把当前本地购物车转换成服务器 RPC。 */
DECLARE_MULTICAST_DELEGATE(FCatShopCartPayRequested);

/** 分类页签 WBP 基类；每个实例只代表一条分类投影，并把点击转成当前客户端的本地过滤意图。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatShopCategoryTabWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 接收父商店页和一条分类展示投影；完成后页签会刷新命名控件并把点击重新绑定到分类入口。 */
	void InitializeCategoryTab(UCatShopWidget* InOwnerShopWidget, const FCatShopCategoryView& InCategoryView);

	/** 暴露本页签的分类表现输入；蓝图只能据此决定样式，不能把它写回真实商品分类或同步给其他玩家。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Shop")
	const FCatShopCategoryView& GetCategoryView() const;

	/** 分类页签按钮调用的选择入口；它只把本页签 CategoryId 交回父商店页，不改 Model。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Shop")
	void RequestSelectThisCategory();

protected:
	/** UMG 构造页签实例时补绑 Designer 中的按钮；父页先注入数据或后注入数据都能得到一致表现。 */
	virtual void NativeConstruct() override;

	/** UMG 销毁页签实例时解除按钮点击绑定；分类选择不会在页签移除后继续触发。 */
	virtual void NativeDestruct() override;

	/** WBP 的可选渲染扩展点；蓝图可以在原生命名控件同步后追加选中动画、图标或材质表现。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|Shop")
	void BP_RenderCategoryTab(const FCatShopCategoryView& InCategoryView);

private:
	/** 分类页签点击入口；它只把 CategoryId 交回父商店页，让父页按本地 Entries 副本重建商品显示数组。 */
	UFUNCTION()
	void HandleCategoryButtonClicked();

	/** 将当前分类投影写入 Designer 命名控件；缺少纯展示控件时只跳过对应表现，不影响商店打开。 */
	void ApplyCategoryTabToDesignerWidgets();

	/** 拥有本页签的商店页，表示点击事件最终要回到哪个页面；弱引用避免页签延长页面生命周期。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatShopWidget> OwnerShopWidget;

	/** 本页签正在展示的只读分类投影；每次商店刷新会整体覆盖它，不在旧值上增量修改。 */
	UPROPERTY(Transient)
	FCatShopCategoryView CategoryView;

	/** Designer 里的页签按钮；存在时由 C++ 自动绑定点击，不存在时蓝图仍可手动调用 RequestSelectThisCategory。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> CategoryButton;

	/** Designer 里的分类名文本；存在时显示表配置或“全部”的显示名。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CategoryLabelTextBlock;

	/** Designer 里的分类数量文本；存在时显示该分类下当前可展示商品数。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CategoryCountTextBlock;

	/** Designer 里的选中装饰；存在时只在当前客户端选中该分类时显示。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UWidget> CategorySelectedVisual;
};

/** 商品卡 WBP 基类；每个实例只代表货架上的一条商品展示数据，并把点击转成加购意图。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatShopGoodsItemWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 接收父商店页和一条商品展示投影；完成后卡片会刷新命名控件并把点击重新绑定到加购入口。 */
	void InitializeGoodsItem(UCatShopWidget* InOwnerShopWidget, const FCatShopEntryView& InEntryView);

	/** 返回本商品卡当前展示的商品投影；蓝图可读取它做额外动画或图标表现，但不能把它当结算真相。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Shop")
	const FCatShopEntryView& GetEntryView() const;

	/** 商品卡按钮调用的加购入口；它只把本卡 EntryId 交回父商店页。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Shop")
	void RequestAddThisEntryToCart();

protected:
	/** UMG 构造卡片实例时补绑 Designer 中的按钮；如果父页先注入了数据，会立即把数据写回命名控件。 */
	virtual void NativeConstruct() override;

	/** UMG 销毁卡片实例时解除按钮点击绑定；卡片不持有后端对象，销毁后不会留下加购回调。 */
	virtual void NativeDestruct() override;

	/** WBP 的可选渲染扩展点；Designer 仍是视觉来源，蓝图可以在原有命名控件之外补充动画或贴图。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|Shop")
	void BP_RenderGoodsItem(const FCatShopEntryView& InEntryView);

private:
	/** 商品卡点击入口；它只把 EntryId 交回父商店页，不读取价格、库存或公款。 */
	UFUNCTION()
	void HandleGoodsButtonClicked();

	/** 将当前商品投影写入 Designer 命名控件；如果蓝图实现扩展事件，会在原生文本更新后继续接管表现。 */
	void ApplyGoodsItemToDesignerWidgets();

	/** 生成卡片中部的后备文字符号；它只服务缺少商品图标时的视觉识别，不参与分类和结算。 */
	FString ResolveGoodsGlyph() const;

	/** 生成卡片底部库存/已选提示；它让正式 WBP 保持简洁，同时仍能看到当前本地购物车数量。 */
	FText MakeGoodsMetaText() const;

	/** 拥有本商品卡的商店页，表示点击事件最终要回到哪个页面；弱引用避免卡片延长页面生命周期。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatShopWidget> OwnerShopWidget;

	/** 本商品卡正在展示的只读商品投影；每次商店刷新会整体覆盖它，不在旧值上增量修改。 */
	UPROPERTY(Transient)
	FCatShopEntryView EntryView;

	/** Designer 里的整张商品卡按钮；存在时由 C++ 自动绑定点击，不存在时蓝图仍可手动调用 RequestAddThisEntryToCart。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> GoodsButton;

	/** Designer 里的商品名文本；存在时写入商品表或定义回退出的显示名。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> GoodsNameTextBlock;

	/** Designer 里的商品图标控件；存在且商品投影有图标时显示正式贴图。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> GoodsIconImage;

	/** Designer 里的商品符号文本；存在时在没有正式贴图的情况下显示商品名首字作为后备识别。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> GoodsGlyphTextBlock;

	/** Designer 里的商品价格文本；存在时只展示单价，实际扣款仍由服务器重算。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> GoodsPriceTextBlock;

	/** Designer 里的库存或已选提示文本；存在时展示“已选”“库存充足”“余量”等局部状态。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> GoodsMetaTextBlock;

	/** Designer 里的不可加购遮罩；存在时在商品不可点时显示，避免玩家以为已售罄商品还能加入购物车。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UWidget> GoodsDisabledVisual;
};

/** 购物车行 WBP 基类；每个实例只代表右侧已选购列表中的一条商品，并把垃圾桶点击转成删除一份。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatShopCartLineWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 接收父商店页和一条购物车行投影；完成后刷新行文本、图形和删除按钮状态。 */
	void InitializeCartLine(UCatShopWidget* InOwnerShopWidget, const FCatShopCartLineView& InCartLineView,
		bool bInActionPending);

	/** 本行缓存的购物车展示快照；蓝图只用它补视觉表现，删除仍回父页统一入口，避免行控件持有第二份购物车状态。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Shop")
	const FCatShopCartLineView& GetCartLineView() const;

	/** 购物车行垃圾桶调用的删除入口；每次只从本地购物车删除一份该商品。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Shop")
	void RequestRemoveThisCartLine();

protected:
	/** UMG 构造购物车行时补绑删除按钮；父页刷新购物车前后创建本行都能得到一致点击行为。 */
	virtual void NativeConstruct() override;

	/** UMG 销毁购物车行时解除删除按钮点击绑定；删除意图不会在行移除后继续触发。 */
	virtual void NativeDestruct() override;

	/** WBP 的可选渲染扩展点；蓝图可以在原生命名控件同步后追加动画、颜色或正式贴图表现。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|Shop")
	void BP_RenderCartLine(const FCatShopCartLineView& InCartLineView, bool bInActionPending);

private:
	/** 购物车垃圾桶点击入口；它只请求删除一份对应 EntryId，不触碰服务器订单。 */
	UFUNCTION()
	void HandleRemoveButtonClicked();

	/** 将当前购物车行投影写入 Designer 命名控件；支付 pending 时会禁用删除按钮。 */
	void ApplyCartLineToDesignerWidgets();

	/** 生成购物车行的后备文字符号；它只用于缺少商品图标时的右侧列表识别。 */
	FString ResolveCartLineGlyph() const;

	/** 拥有本购物车行的商店页，表示删除意图最终要回到哪个页面；弱引用避免行控件延长页面生命周期。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatShopWidget> OwnerShopWidget;

	/** 本行正在展示的购物车投影；父页每次刷新会整体重建行控件并覆盖这份数据。 */
	UPROPERTY(Transient)
	FCatShopCartLineView CartLineView;

	/** 当前商店是否正在等待支付回包；为 true 时本行删除按钮暂时不可用。 */
	UPROPERTY(Transient)
	bool bActionPending = false;

	/** Designer 里的删除按钮；存在时由 C++ 自动绑定点击，不存在时蓝图仍可手动调用 RequestRemoveThisCartLine。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> CartLineRemoveButton;

	/** Designer 里的商品名文本；存在时显示购物车行对应的商品名。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CartLineNameTextBlock;

	/** Designer 里的数量文本；存在时显示本地选购次数。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CartLineCountTextBlock;

	/** Designer 里的小计文本；存在时显示本行单价乘以次数后的展示金额。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CartLinePriceTextBlock;

	/** Designer 里的购物车行图标控件；存在且行投影有图标时显示正式贴图。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> CartLineIconImage;

	/** Designer 里的商品符号文本；存在时在没有正式贴图的情况下显示商品名首字作为后备识别。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CartLineGlyphTextBlock;

	/** Designer 里的无效行提示装饰；存在时在库存或价格失效时显示，支付入口会因此保持不可支付。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UWidget> CartLineInvalidVisual;
};

/** 商店 WBP 基类；它展示商品、公款和购物车，并把加购、删除、支付点击作为纯 UI 意图发出。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatShopWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 构造商店页默认配置；默认子控件类指向正式 WBP，运行时不在 C++ 生成整页视觉树。 */
	UCatShopWidget(const FObjectInitializer& ObjectInitializer);

	/** 接收 Shop Model 的只读投影并同步蓝图绑定字段；不读取 GameState 或 ShopEconomy。 */
	void RenderShop(const FCatShopViewState& ViewState);

	/** 返回最近一次由 Model 投入本 View 的商店只读快照；蓝图只能用它做延迟表现，不能把它当作订单提交或公款真相来源。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Shop")
	const FCatShopViewState& GetLastShopViewState() const;

	/** 返回当前分类过滤后的商品行；商品区必须读这个本地数组，而不是直接读完整 Entries。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Shop")
	const TArray<FCatShopEntryView>& GetDisplayedEntries() const;

	/** 返回从当前真实商品数组归纳出的分类按钮数据；Widget 会在本地标记哪个分类被当前玩家选中。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Shop")
	const TArray<FCatShopCategoryView>& GetCategories() const;

	/** 当前购物车展示行是右侧已选购列表的只读来源；蓝图按它生成删除按钮和小计。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Shop")
	const TArray<FCatShopCartLineView>& GetCartLines() const;

	/** 蓝图商品行调用的加购入口；Widget 只校验 EntryId 非空并广播本地加购意图。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Shop")
	void RequestAddEntryToCart(FName EntryId);

	/** 蓝图购物车垃圾桶调用的删除入口；每次只从本地购物车删掉一份该商品。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Shop")
	void RequestRemoveOneCartItem(FName EntryId);

	/** 蓝图支付按钮调用的支付入口；只有 ViewState 允许支付时才广播给 PageController。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Shop")
	void RequestPayCart();

	/** 蓝图分类按钮调用的分类入口；NAME_None 表示“全部”，非空值按商品 DisplayCategoryId 本地过滤。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Shop")
	void RequestSelectCategory(FName CategoryId);

	/** 蓝图“全部”分类按钮调用的快捷入口；它会清空本地分类过滤。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Shop")
	void RequestShowAllCategory();

	/** 蓝图或关闭按钮调用的关闭入口；实际销毁交给交互对象组件。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Shop")
	void RequestCloseShop();

	/** 商店关闭意图广播；PageController 接收后恢复输入模式并通知拥有组件。 */
	FCatShopCloseRequested OnCloseRequested;

	/** 商品加购意图广播；PageController 接收后只修改本地 Model 购物车。 */
	FCatShopEntryAddToCartRequested OnEntryAddToCartRequested;

	/** 购物车删除意图广播；PageController 接收后只修改本地 Model 购物车。 */
	FCatShopCartLineRemoveRequested OnCartLineRemoveRequested;

	/** 购物车支付意图广播；PageController 接收后提交正式购物车 RPC。 */
	FCatShopCartPayRequested OnCartPayRequested;

protected:
	/** UMG 构造本商店页时打开键盘焦点能力并绑定 Designer 中的正式按钮。 */
	virtual void NativeConstruct() override;

	/** 析构时解绑本 View 拥有的按钮和动态子 WBP；外部意图订阅由 PageController::Unbind 清理。 */
	virtual void NativeDestruct() override;

	/** 预览按键先于商品按钮处理；商店打开时命中关闭键会统一请求关闭，避免焦点落在动态按钮后按键失效。 */
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** UMG 在本页拥有键盘焦点时交付按键；商店打开且命中关闭键时广播关闭意图并返回已处理。 */
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** WBP 可选渲染扩展点；正式商品列表应通过 GetDisplayedEntries 读取当前分类结果。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|Shop")
	void BP_RenderShop(const FCatShopViewState& ViewState);

private:
	/** 动态分类页签重建流程；用当前 Categories 创建分类页签 WBP，分类数量只由数据决定。 */
	void RebuildCategoryTabs();

	/** 动态商品卡重建流程；用本地 DisplayedEntries 创建商品卡 WBP，分类不会写回 Model 或服务器。 */
	void RebuildGoodsItems();

	/** 动态购物车行重建流程；用当前 CartLines 创建购物车行 WBP，每行只删除一份对应商品。 */
	void RebuildCartLines();

	/** 按最新 ViewState.Categories 同步本地分类数组；当前选择失效时回到“全部”。 */
	void RefreshCategoryPresentation();

	/** 按当前分类把完整 Entries 过滤成本地 DisplayedEntries，并同步商品卡。 */
	void RefreshDisplayedEntries();

	/** 刷新主 WBP 里的公款、结果、总计和支付按钮；资金不足提示通过禁用遮罩提供鼠标悬停文案。 */
	void RefreshMainDesignerWidgets();

	/** 判断当前分类按钮数据里是否存在目标分类；NAME_None 永远合法，代表“全部”。 */
	bool DoesCategoryExist(FName CategoryId) const;

	/** 判断一次按键是否应该关闭商店；Escape、交互键和库存开关键都作为模态页面关闭出口。 */
	bool ShouldCloseShopFromKey(const FKeyEvent& InKeyEvent) const;

	/** 最近一次 Shop Model 输入的完整投影；本 Widget 不保存任何后端对象。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Shop", meta = (AllowPrivateAccess = "true"))
	FCatShopViewState LastShopViewState;

	/** 给 WBP 顶部 TextBlock 直接绑定的团队公款文本。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Shop", meta = (AllowPrivateAccess = "true"))
	FText BlueprintWalletText;

	/** 给 WBP 结果 TextBlock 直接绑定的最近加购、删除、支付或拒绝反馈。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Shop", meta = (AllowPrivateAccess = "true"))
	FText BlueprintResultText;

	/** 给 WBP 商品区直接绑定的完整商品副本；分类显示请优先读取 BlueprintDisplayedEntries。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Shop", meta = (AllowPrivateAccess = "true"))
	TArray<FCatShopEntryView> BlueprintEntries;

	/** 给 WBP 商品区直接绑定的当前分类显示副本；它只在本地客户端存在，不同步给其他玩家。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Shop", meta = (AllowPrivateAccess = "true"))
	TArray<FCatShopEntryView> BlueprintDisplayedEntries;

	/** 给 WBP 顶部分类区直接绑定的分类副本；选中状态由本地 Widget 写入，不来自服务器。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Shop", meta = (AllowPrivateAccess = "true"))
	TArray<FCatShopCategoryView> BlueprintCategories;

	/** 给 WBP 购物车区直接绑定的已选购副本；蓝图按它生成右侧列表和垃圾桶按钮。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Shop", meta = (AllowPrivateAccess = "true"))
	TArray<FCatShopCartLineView> BlueprintCartLines;

	/** 给 WBP 总计 TextBlock 直接绑定的购物车金额文本。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Shop", meta = (AllowPrivateAccess = "true"))
	FText BlueprintCartTotalText;

	/** 给 WBP 支付按钮直接绑定的支付文案。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Shop", meta = (AllowPrivateAccess = "true"))
	FText BlueprintPayButtonText;

	/** 给 WBP 支付按钮禁用提示绑定的原因；资金不足时显示“资金不足，无法购买！”。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Shop", meta = (AllowPrivateAccess = "true"))
	FText BlueprintPayDisabledReasonText;

	/** 当前本地分类过滤 ID；NAME_None 表示“全部”，关闭商店后会回到这个默认值。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Shop", meta = (AllowPrivateAccess = "true"))
	FName BlueprintSelectedCategoryId = NAME_None;

	/** 分类页签蓝图类引用；父页刷新分类区时用它为每条分类投影创建一个页签，默认指向 `/Game/UI/Shop/WBP_CatShopCategoryTab`。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Catfishing|Shop|Designer", meta = (AllowPrivateAccess = "true"))
	TSoftClassPtr<UCatShopCategoryTabWidget> CategoryTabWidgetClass;

	/** 商品卡蓝图类引用；父页刷新货架时用它创建可见卡片，默认指向 `/Game/UI/Shop/WBP_CatShopGoodsItem`。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Catfishing|Shop|Designer", meta = (AllowPrivateAccess = "true"))
	TSoftClassPtr<UCatShopGoodsItemWidget> GoodsItemWidgetClass;

	/** 购物车行蓝图类引用；父页刷新已选购列表时用它创建右侧行，默认指向 `/Game/UI/Shop/WBP_CatShopCartLine`。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Catfishing|Shop|Designer", meta = (AllowPrivateAccess = "true"))
	TSoftClassPtr<UCatShopCartLineWidget> CartLineWidgetClass;

	/** Designer 里的关闭按钮；存在时由 C++ 自动绑定关闭请求，不存在时蓝图仍可调用 RequestCloseShop。 */
	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> CloseButton;

	/** Designer 里的团队公款文本控件；存在时 RenderShop 会直接写入团队公款摘要。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> WalletTextBlock;

	/** Designer 里的结果文本控件；存在时 RenderShop 会直接写入最近加购、删除、支付或拒绝反馈。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ResultTextBlock;

	/** Designer 里的购物车总计文本控件；存在时 RenderShop 会直接写入右侧合计金额。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CartTotalTextBlock;

	/** Designer 里的支付按钮；存在时 RenderShop 会按支付许可和 pending 状态禁用它，RequestPayCart 仍会再次守住提交条件。 */
	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> PayButton;

	/** 支付按钮内部的文案控件；存在时 RenderShop 会用 Model 投影同步“支付”等本地化文字。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PayButtonLabelTextBlock;

	/** 支付按钮上方的禁用命中层；存在时它接管鼠标悬停提示，但不会广播支付请求。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UWidget> PayDisabledHintLayer;

	/** WBP Designer 中的分类页签容器；它代表表驱动分类区域，RenderShop 会按 Categories 重建里面的页签。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> CategoryTabsPanel;

	/** WBP Designer 中的商品卡容器；它代表表驱动货架区域，RenderShop 会按当前分类结果重建里面的商品卡。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> ShopButtons;

	/** WBP Designer 中的购物车行容器；RenderShop 会按 CartLines 重建右侧已选购删除行。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> CartLinesPanel;

	/** 当前由 C++ 创建的分类页签 WBP；保存引用是为了页面重绘或销毁时解除点击绑定并移出旧页签。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCatShopCategoryTabWidget>> DynamicCategoryTabs;

	/** 当前由 C++ 创建的商品卡 WBP；保存引用是为了页面重绘或销毁时解除点击绑定并移出旧卡片。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCatShopGoodsItemWidget>> DynamicGoodsItems;

	/** 当前由 C++ 创建的购物车行 WBP；保存引用是为了购物车刷新或销毁时解除点击绑定并移出旧行。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCatShopCartLineWidget>> DynamicCartLineWidgets;
};
