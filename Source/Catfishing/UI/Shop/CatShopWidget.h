#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "UI/Shop/CatShopTypes.h"
#include "CatShopWidget.generated.h"

class UPanelWidget;
class UTextBlock;
class UCatShopWidget;

/** 商店关闭意图；交互对象拥有页面生命周期，Widget 只发关闭请求。 */
DECLARE_MULTICAST_DELEGATE(FCatShopCloseRequested);

/** 商品加入购物车意图；Widget 只提交目录 ID，Model 决定本地购物车是否接收。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatShopEntryAddToCartRequested, FName);

/** 购物车单行删除意图；Widget 只提交目录 ID，Model 删除对应商品的一份选购。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatShopCartLineRemoveRequested, FName);

/** 购物车支付意图；PageController 会把当前本地购物车转换成服务器 RPC。 */
DECLARE_MULTICAST_DELEGATE(FCatShopCartPayRequested);

/** 商店动态商品按钮；它只保存当前行的 EntryId，点击后回调父 Widget 的统一加购出口。 */
UCLASS()
class CATFISHING_API UCatShopEntryButton : public UButton
{
	GENERATED_BODY()

public:
	/** 绑定当前商品行的父 Widget 和 EntryId；重复初始化会先移除已有点击绑定，避免一键加购多次。 */
	void InitializeShopEntry(UCatShopWidget* InOwnerShopWidget, FName InEntryId);

private:
	/** 动态按钮点击入口；只把 EntryId 交回父 Widget，不读取价格、库存或公款。 */
	UFUNCTION()
	void HandleShopEntryClicked();

	/** 创建本按钮的商店 View；弱引用避免按钮比页面生命周期更长时阻止 Widget 回收。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatShopWidget> OwnerShopWidget;

	/** 本按钮代表的商店目录项；点击时只提交这个稳定 ID。 */
	UPROPERTY(Transient)
	FName EntryId = NAME_None;
};

/** 商店 WBP 基类；它只展示商品、公款和购物车，并把加购、删除、支付点击作为纯 UI 意图发出。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatShopWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 接收 Shop Model 的只读投影并同步蓝图绑定字段；不读取 GameState 或 ShopEconomy。 */
	void RenderShop(const FCatShopViewState& ViewState);

	/** 返回最近一次由 Model 投入本 View 的商店只读快照；蓝图只能用它做延迟表现，不能把它当作订单提交或公款真相来源。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Shop")
	const FCatShopViewState& GetLastShopViewState() const;

	/** 返回当前分类过滤后的商品行；商品区必须读这个本地数组，而不是直接读完整 Entries。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Shop")
	const TArray<FCatShopEntryView>& GetDisplayedEntries() const;

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
	/** UMG 构造本商店页时打开键盘焦点能力并绑定关闭按钮；正式货架按钮会在渲染阶段按 Model 条目重建。 */
	virtual void NativeConstruct() override;

	/** 析构时解绑本 View 拥有的关闭、支付和动态商品按钮；外部意图订阅由 PageController::Unbind 清理。 */
	virtual void NativeDestruct() override;

	/** 预览按键先于商品按钮处理；商店打开时命中关闭键会统一请求关闭，避免焦点落在动态按钮后按键失效。 */
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** UMG 在本页拥有键盘焦点时交付按键；商店打开且命中关闭键时广播关闭意图并返回已处理，其他按键继续交给父类。 */
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** WBP 可选渲染扩展点；正式商品列表应通过 GetDisplayedEntries 读取当前分类结果。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|Shop")
	void BP_RenderShop(const FCatShopViewState& ViewState);

private:
	/** 关闭按钮入口；它只广播关闭意图，不直接 RemoveFromParent。 */
	UFUNCTION()
	void HandleCloseClicked();

	/** 动态商品按钮重建流程；用本地 DisplayedEntries 生成当前货架按钮，分类不会写回 Model 或服务器。 */
	void RebuildDynamicEntryButtons();

	/** 按当前分类把完整 Entries 过滤成本地 DisplayedEntries，并同步简单文本和动态商品按钮。 */
	void RefreshDisplayedEntries();

	/** 购物车文本重建流程；把当前 CartLines 拼成简单文本，复杂 WBP 可直接读取数组生成右侧列表。 */
	void RefreshCartPresentation();

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

	/** 给 WBP 购物车区直接绑定的已选购副本；蓝图按它生成右侧列表和垃圾桶按钮。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Shop", meta = (AllowPrivateAccess = "true"))
	TArray<FCatShopCartLineView> BlueprintCartLines;

	/** 给 WBP TextBlock 直接绑定的商品列表文本；简单 WBP 可先展示它，复杂列表再按 Entries 创建行控件。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Shop", meta = (AllowPrivateAccess = "true"))
	FText BlueprintEntriesText;

	/** 给 WBP TextBlock 直接绑定的购物车列表文本；简单 WBP 可先展示它，复杂列表再按 CartLines 创建行控件。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Shop", meta = (AllowPrivateAccess = "true"))
	FText BlueprintCartText;

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

	/** 可选关闭按钮；存在时 NativeConstruct 绑定到 RequestCloseShop。 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> CloseButton;

	/** WBP Designer 中的公款文本控件；存在时 RenderShop 会直接写入团队公款摘要。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> WalletTextBlock;

	/** WBP Designer 中的结果文本控件；存在时 RenderShop 会直接写入最近加购、删除、支付或拒绝反馈。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ResultTextBlock;

	/** WBP Designer 中的商品列表文本控件；存在时 RenderShop 会直接写入配置商品列表。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> EntriesTextBlock;

	/** WBP Designer 中的购物车列表文本控件；存在时 RenderShop 会直接写入已选购摘要。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CartTextBlock;

	/** WBP Designer 中的购物车总计文本控件；存在时 RenderShop 会直接写入总价。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CartTotalTextBlock;

	/** WBP Designer 中的支付按钮；存在时 RenderShop 会按 bCanPayCart 直接设置可用状态。 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> PayButton;

	/** WBP Designer 中的商品按钮容器；它代表表驱动货架区域，RenderShop 会按当前分类结果重建里面的商品按钮。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> ShopButtons;

	/** 当前由 C++ 动态创建的商品按钮；保存引用是为了页面重绘或销毁时能解除点击绑定并让过期按钮释放。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCatShopEntryButton>> DynamicEntryButtons;
};
