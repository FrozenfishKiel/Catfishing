#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Inventory/CatInventoryTypes.h"
#include "CatInventoryWidget.generated.h"

class UButton;
class UCatInventoryModel;
class UCatInventoryPageController;
class UCatInventorySlotWidget;
class UTextBlock;
class UWrapBox;

/** 库存界面基类；每个 WBP 实例都自己监听当前库存 Model，构建完成时直接按 Model 最新状态刷新自己。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatInventoryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 接收库存 Model 的只读投影并刷新本页控件；调用方只提供数据，本页只更新自己。 */
	virtual void RenderInventory(const FCatInventoryViewState& ViewState);

	/** 设置动态格子使用的 WBP 类；外部可显式指定，本页构建时也会从 UI Settings 兜底读取。 */
	virtual void SetInventorySlotWidgetClass(TSubclassOf<UCatInventorySlotWidget> InSlotWidgetClass);

	/** 绑定本库存页对应的 Model；本页会监听它的变化，并在绑定完成后立刻按当前投影刷新。 */
	void BindInventoryModel(UCatInventoryModel* InModel);

	/** 断开本页对库存 Model 的监听；页面移出视口或换 Model 时调用，不销毁 Model 本身。 */
	void UnbindInventoryModel();

	/** 蓝图或按钮请求关闭库存；本页把意图交给当前 PageController，不自己修改输入模式。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	void RequestCloseInventory();

	/** 蓝图请求选中本页 DisplayedSlots 里的某个下标；这个下标只在当前 WBP 内有效，不能跨库存使用。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	void RequestSelectSlot(int32 SlotIndex);

	/** 请求吃掉本页当前本地选中鱼；PageController 会对照最新 Model 快照复核鱼仍在原容器格。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	void RequestConsumeSelectedFish();

	/** 请求献祭本页当前本地选中鱼；献祭命令不依赖其他库存 WBP 的选择状态。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	void RequestSacrificeSelectedFish();

	/** 请求把本页当前本地选中鱼放入营地共享鱼缸；Widget 只交出鱼护格身份，目标鱼缸由服务器按固定营地解析。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	void RequestStoreSelectedFishInSharedTank();

	/** 本页最近一次渲染后的只读状态；蓝图只能读取当前 WBP 自己的选择和表现，不能借它回写 Model 或玩法状态。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Inventory")
	const FCatInventoryViewState& GetLastInventoryViewState() const;

protected:
	/** 进入视口时绑定可选按钮、解析当前 Model 并刷新本页；没有 Designer 绑定按钮时仍可由蓝图直接调用 Request*。 */
	virtual void NativeConstruct() override;

	/** 离开视口时解除按钮、格子和 Model 监听；下次构建会重新从当前 LocalPlayer 找 Model。 */
	virtual void NativeDestruct() override;

	/** 预览按键先于子按钮和格子处理；库存打开时命中关闭键会统一请求关闭，避免焦点落在子控件后按键失效。 */
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** 库存处于模态焦点时按关闭键会请求关闭；其他按键交回父类处理。 */
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** WBP 可选渲染扩展点；正式布局可用 Designer 字段绑定和这个事件共同表现。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|Inventory")
	void BP_RenderInventory(const FCatInventoryViewState& ViewState);

	/** 返回本库存 WBP 对应的数据源 Slots；普通背包页在鱼缸这类外部容器上下文下读容器格，否则读随身库存，专用页仍可覆盖自己的数据源。 */
	virtual const TArray<FCatInventorySlotView>& GetInventorySlotsForWidget(
		const FCatInventoryViewState& ViewState) const;

	/** 把本页渲染后的 Slots 写回本页 ViewState 副本；默认页按当前上下文写回外部容器或随身库存，派生页可覆盖到自己的数组。 */
	virtual void StoreDisplayedSlotsInViewState(FCatInventoryViewState& ViewState,
		const TArray<FCatInventorySlotView>& Slots) const;

private:
	/** 按当前 Slots 数组刷新单一 WrapBox 子格；能原位更新时不重建，结构变化时才清空并重建。 */
	void RebuildSlotWidgets();

	/** 从当前 LocalPlayer UI 子系统取得库存 Model；蓝图树里的每个库存 WBP 都走同一条自解析入口。 */
	UCatInventoryModel* ResolveInventoryModel() const;

	/** 从已绑定 Model 读取最新 ViewState 并刷新本页；Model 不存在时保留当前显示，避免构造预览被清空。 */
	void RefreshInventoryViewFromModel();

	/** Model 广播入口；本页收到后直接重读 Model 当前投影并刷新自己。 */
	void HandleInventoryModelViewStateChanged();

	/** 从当前 LocalPlayer UI 子系统取得库存 PageController；本页只把玩家意图交给它。 */
	UCatInventoryPageController* ResolveInventoryPageController() const;

	/** 解除当前 WrapBox 子格的原生委托；刷新或销毁前调用，避免已移除格子继续广播。 */
	void UnbindSlotWidgets();

	/** 记录一个已经属于本页 DisplayedSlots 的本地选择；它只刷新当前 WBP，不进入共享 Model 广播。 */
	void RequestSelectSlotView(const FCatInventorySlotView& SlotView);

	/** 判断一次按键是否应该关闭当前库存页；世界交互打开的库存额外接受交互键，普通背包保留背包键和 Escape。 */
	bool ShouldCloseInventoryFromKey(const FKeyEvent& InKeyEvent) const;

	/** Slot Widget 左键点击入口；本页只更新自己的本地选择，不解释成全局下标。 */
	void HandleSlotSelected(const FCatInventorySlotView& SlotView);

	/** Slot Widget 右键上下文入口；本页把格子身份交给 PageController，由它直接按数据源决定动作。 */
	void HandleSlotContextRequested(const FCatInventorySlotView& SlotView);

	/** Slot Widget Drop 入口；只转交源和目标投影，避免本页从 Widget 指针反查后端事实。 */
	void HandleSlotDropRequested(const FCatInventorySlotView& SourceSlot, const FCatInventorySlotView& TargetSlot);

	/** 关闭按钮点击入口；收口到 RequestCloseInventory，避免按钮和蓝图图表两套逻辑。 */
	UFUNCTION()
	void HandleCloseClicked();

	/** 吃鱼按钮点击入口；收口到 RequestConsumeSelectedFish。 */
	UFUNCTION()
	void HandleConsumeClicked();

	/** 献祭按钮点击入口；收口到 RequestSacrificeSelectedFish。 */
	UFUNCTION()
	void HandleSacrificeClicked();

	/** 存鱼缸按钮点击入口；收口到 RequestStoreSelectedFishInSharedTank。 */
	UFUNCTION()
	void HandleStoreFishInTankClicked();

	/** 库存格子 WBP 类；本页用它为 WrapBox 每个后端格子创建独立 Widget。 */
	UPROPERTY(EditDefaultsOnly, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UCatInventorySlotWidget> InventorySlotWidgetClass;

	/** WBP Designer 中的格子容器；本页刷新时只对它 ClearChildren/AddChild，不硬编码八个按钮。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UWrapBox> InventorySlotWrapBox;

	/** WBP Designer 中的关闭按钮；点击只发关闭意图。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> CloseButton;

	/** WBP Designer 中的吃鱼按钮；点击只发动作意图。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> ConsumeFishButton;

	/** WBP Designer 中的献祭按钮；点击只发动作意图。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> SacrificeFishButton;

	/** WBP Designer 中的存入鱼缸按钮；只在鱼护页需要绑定，点击后服务器会重新寻找固定共享鱼缸。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> StoreFishInTankButton;

	/** 本页最近一次渲染后的 ViewState 副本；它只保留属于当前 WBP 的选中和动作表现，蓝图读取它不会拿到其他库存页的选择。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	FCatInventoryViewState LastInventoryViewState;

	/** 本页当前实际显示的格子副本；它只来自当前 WBP 对应的一份独立数据源，不会混入其他库存。 */
	UPROPERTY(Transient)
	TArray<FCatInventorySlotView> DisplayedSlots;

	/** 本页当前高亮格子的来源身份；它是纯 UI 本地状态，不写入 Model，也不会让同屏其他库存页刷新。 */
	UPROPERTY(Transient)
	FCatInventorySlotView LocalSelectedSlotIdentity;

	/** 本页是否保存了本地选择；Model 刷新后如果对应格子已经不存在，本页会自动清掉它。 */
	UPROPERTY(Transient)
	bool bHasLocalSelectedSlotIdentity = false;

	/** 当前 WrapBox 中由本对象创建并绑定的格子 Widget；刷新前需要逐个解绑。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCatInventorySlotWidget>> BoundSlotWidgets;

	/** 本页当前监听的库存 Model；它只提供 ViewState 和变化广播，不把后端写口暴露给 Widget。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatInventoryModel> BoundInventoryModel;

	/** 本页注册到 Model 变化广播的句柄；解绑必须用它从同一个 Model 移除，避免页面关闭后继续刷新。 */
	FDelegateHandle InventoryModelViewChangedHandle;

	/** 最近一次库存总览文本副本；RenderInventory 写入，简单 WBP 可直接绑定它显示当前页面的库存和容器概况。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	FText BlueprintSummaryText;

	/** 最近一次当前钓鱼选择文本副本；RenderInventory 写入，蓝图只读取它表现鱼竿、鱼饵、鱼漂和耐久。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	FText BlueprintEquipmentText;

	/** 最近一次随身库存概要文本副本；RenderInventory 写入，用来把玩家背包占用情况暴露给 WBP。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	FText BlueprintInventoryItemsText;

	/** 最近一次选中鱼文本副本；RenderInventory 写入，鱼容器格子的选择变化会改变这条说明。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	FText BlueprintSelectedFishText;

	/** 最近一次库存动作结果文本副本；RenderInventory 写入，蓝图用它展示服务器等待或终态反馈，本地无效操作只进日志。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	FText BlueprintResultText;

	/** WBP Designer 中的库存摘要文本控件；存在时 RenderInventory 会直接写入当前页面的库存和容器总览。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SummaryTextBlock;

	/** WBP Designer 中的当前选择文本控件；存在时 RenderInventory 会直接写入鱼竿、鱼饵、鱼漂和耐久。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> EquipmentTextBlock;

	/** WBP Designer 中的随身库存概要文本控件；存在时 RenderInventory 会直接写入固定库存格数组的概要。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> InventoryItemsTextBlock;

	/** WBP Designer 中的选中鱼文本控件；存在时 RenderInventory 会直接写入当前选中鱼摘要。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SelectedFishTextBlock;

	/** WBP Designer 中的结果文本控件；存在时 RenderInventory 会直接写入最近动作反馈。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ResultTextBlock;
};
