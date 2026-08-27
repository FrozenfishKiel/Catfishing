#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Shop/CatShopTypes.h"
#include "CatShopWidget.generated.h"

class UButton;
class UHorizontalBox;
class UTextBlock;

/** 商店关闭意图；交互对象拥有页面生命周期，Widget 只发关闭请求。 */
DECLARE_MULTICAST_DELEGATE(FCatShopCloseRequested);

/** 商品点击意图；参数只包含目录 ID 和购买/领取类型，不包含价格或公款并发版本。 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FCatShopEntryActionRequested, FName, ECatShopUIAction);

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
	/** UMG 构造本商店页时补齐旧 WBP 的默认鱼漂按钮并打开键盘焦点能力；完成后 UIOnly 焦点可以落到本页，固定按钮也会转到统一请求入口。 */
	virtual void NativeConstruct() override;

	/** 析构时只解绑本 View 的可选按钮；关闭和商品动作订阅由 PageController::Unbind 清理。 */
	virtual void NativeDestruct() override;

	/** UMG 在本页拥有键盘焦点时交付按键；商店打开且命中 Escape 或当前交互键时广播关闭意图并返回已处理，其他按键继续交给父类。 */
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** WBP 可选渲染扩展点；正式列表可在蓝图里按 Entries 创建商品行。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|Shop")
	void BP_RenderShop(const FCatShopViewState& ViewState);

private:
	/** 项目默认二级竿按钮入口；动态 WBP 不需要它，保留是为了冷启动资产有可点商品。 */
	UFUNCTION()
	void HandlePurchaseRodClicked();

	/** 项目默认窝料按钮入口；动态 WBP 不需要它，保留是为了冷启动资产有可点商品。 */
	UFUNCTION()
	void HandlePurchaseChumClicked();

	/** 项目默认鱼漂按钮入口；动态 WBP 不需要它，保留是为了冷启动资产能凑齐开钓前置物。 */
	UFUNCTION()
	void HandlePurchaseFloatClicked();

	/** 项目默认免费鱼饵按钮入口；动态 WBP 不需要它，保留是为了冷启动资产有可点商品。 */
	UFUNCTION()
	void HandleClaimBaitClicked();

	/** 项目默认保底竿按钮入口；动态 WBP 不需要它，保留是为了冷启动资产有可点商品。 */
	UFUNCTION()
	void HandleClaimStarterRodClicked();

	/** 关闭按钮入口；它只广播关闭意图，不直接 RemoveFromParent。 */
	UFUNCTION()
	void HandleCloseClicked();

	/** 默认鱼漂按钮补齐流程；旧 WBP 只有按钮行没有鱼漂按钮时，运行时补一个最小购买入口。 */
	void EnsureDefaultFloatButton();

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

	/** WBP Designer 中的默认商品按钮行；旧资产缺少鱼漂按钮时，Widget 会在这里追加一个运行时按钮。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> ShopButtons;

	/** 可选默认商品按钮：购买二级竿；正式动态列表不依赖这个名字。 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> PurchaseShopRodT2Button;

	/** 可选默认商品按钮：购买窝料；正式动态列表不依赖这个名字。 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> PurchaseBugChumButton;

	/** 可选默认商品按钮：购买鱼漂；它保证未做动态商品行的冷启动商店也能获得开钓必需的鱼漂。 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> PurchaseYarnBallFloatButton;

	/** 可选默认商品按钮：领取普通鱼饵；正式动态列表不依赖这个名字。 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> ClaimFreeBugBaitButton;

	/** 可选默认商品按钮：领取保底竿；正式动态列表不依赖这个名字。 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> ClaimFreeStarterRodButton;
};
