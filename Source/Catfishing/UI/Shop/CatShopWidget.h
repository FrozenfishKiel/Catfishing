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

/** 商品点击意图；参数只包含目录 ID 和购买/领取类型，不包含价格或公款并发版本。 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FCatShopEntryActionRequested, FName, ECatShopUIAction);

/** 商店动态商品按钮；它只保存当前行的 EntryId 和动作类型，点击后回调父 Widget 的统一 UI 意图出口。 */
UCLASS()
class CATFISHING_API UCatShopEntryButton : public UButton
{
	GENERATED_BODY()

public:
	/** 绑定当前商品行的父 Widget、EntryId 和动作类型；重复初始化会先移除已有点击绑定，避免一键提交多次。 */
	void InitializeShopEntry(UCatShopWidget* InOwnerShopWidget, FName InEntryId, ECatShopUIAction InAction);

private:
	/** 动态按钮点击入口；只把 EntryId 和动作类型交回父 Widget，不读取价格、库存或公款。 */
	UFUNCTION()
	void HandleShopEntryClicked();

	/** 创建本按钮的商店 View；弱引用避免按钮比页面生命周期更长时阻止 Widget 回收。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatShopWidget> OwnerShopWidget;

	/** 本按钮代表的商店目录项；点击时只提交这个稳定 ID。 */
	UPROPERTY(Transient)
	FName EntryId = NAME_None;

	/** 本按钮代表购买还是免费领取；这个值来自 Model 投影，不由按钮根据价格自行推断。 */
	UPROPERTY(Transient)
	ECatShopUIAction Action = ECatShopUIAction::None;
};

/** 商店 WBP 基类；它只展示商品和公款，并把购买/领取点击作为纯 UI 意图发出。 */
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

	/** 蓝图商品行调用的购买入口；Widget 只校验 EntryId 非空并广播意图。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Shop")
	void RequestPurchaseEntry(FName EntryId);

	/** 蓝图商品行调用的免费领取入口；Widget 只校验 EntryId 非空并广播意图。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Shop")
	void RequestFreeClaimEntry(FName EntryId);

	/** 蓝图或关闭按钮调用的关闭入口；实际销毁交给交互对象组件。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Shop")
	void RequestCloseShop();

	/** 商店关闭意图广播；PageController 接收后恢复输入模式并通知拥有组件。 */
	FCatShopCloseRequested OnCloseRequested;

	/** 商品购买或领取意图广播；PageController 接收后调用 PlayerController 的正式 RPC。 */
	FCatShopEntryActionRequested OnEntryActionRequested;

protected:
	/** UMG 构造本商店页时打开键盘焦点能力并绑定关闭按钮；正式货架按钮会在渲染阶段按 Model 条目重建。 */
	virtual void NativeConstruct() override;

	/** 析构时解绑本 View 拥有的关闭按钮与动态商品按钮；关闭和商品动作订阅由 PageController::Unbind 清理。 */
	virtual void NativeDestruct() override;

	/** 预览按键先于商品按钮处理；商店打开时命中关闭键会统一请求关闭，避免焦点落在动态按钮后按键失效。 */
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** UMG 在本页拥有键盘焦点时交付按键；商店打开且命中关闭键时广播关闭意图并返回已处理，其他按键继续交给父类。 */
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** WBP 可选渲染扩展点；正式列表可在蓝图里按 Entries 创建商品行。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|Shop")
	void BP_RenderShop(const FCatShopViewState& ViewState);

private:
	/** 关闭按钮入口；它只广播关闭意图，不直接 RemoveFromParent。 */
	UFUNCTION()
	void HandleCloseClicked();

	/** 动态商品按钮重建流程；用 Model 给出的 Entries 生成当前货架按钮，控件名字和 Designer 预留位置都不能决定商品事实。 */
	void RebuildDynamicEntryButtons(const FCatShopViewState& ViewState);

	/** 判断一次按键是否应该关闭商店；Escape、交互键和库存开关键都作为模态页面关闭出口。 */
	bool ShouldCloseShopFromKey(const FKeyEvent& InKeyEvent) const;

	/** 最近一次 Shop Model 输入的完整投影；本 Widget 不保存任何后端对象。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Shop", meta = (AllowPrivateAccess = "true"))
	FCatShopViewState LastShopViewState;

	/** 给 WBP 顶部 TextBlock 直接绑定的团队公款文本。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Shop", meta = (AllowPrivateAccess = "true"))
	FText BlueprintWalletText;

	/** 给 WBP 结果 TextBlock 直接绑定的最近购买/领取反馈。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Shop", meta = (AllowPrivateAccess = "true"))
	FText BlueprintResultText;

	/** 给 WBP 商品区直接绑定的展示副本；蓝图可按它创建行，不访问 Settings。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Shop", meta = (AllowPrivateAccess = "true"))
	TArray<FCatShopEntryView> BlueprintEntries;

	/** 给 WBP TextBlock 直接绑定的商品列表文本；简单 WBP 可先展示它，复杂列表再按 Entries 创建行控件。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Shop", meta = (AllowPrivateAccess = "true"))
	FText BlueprintEntriesText;

	/** 可选关闭按钮；存在时 NativeConstruct 绑定到 RequestCloseShop。 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> CloseButton;

	/** WBP Designer 中的公款文本控件；存在时 RenderShop 会直接写入团队公款摘要。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> WalletTextBlock;

	/** WBP Designer 中的结果文本控件；存在时 RenderShop 会直接写入最近购买或领取反馈。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ResultTextBlock;

	/** WBP Designer 中的商品列表文本控件；存在时 RenderShop 会直接写入配置商品列表。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> EntriesTextBlock;

	/** WBP Designer 中的商品按钮容器；它代表表驱动货架区域，RenderShop 会按当前 Entries 重建里面的商品按钮。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> ShopButtons;

	/** 当前由 C++ 动态创建的商品按钮；保存引用是为了页面重绘或销毁时能解除点击绑定并让过期按钮释放。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCatShopEntryButton>> DynamicEntryButtons;
};
