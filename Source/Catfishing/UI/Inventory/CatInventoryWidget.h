#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Inventory/CatInventoryTypes.h"
#include "CatInventoryWidget.generated.h"

class UButton;
class UCatInventorySlotWidget;
class UTextBlock;
class UWrapBox;

/** 库存关闭意图；Widget 不恢复输入模式，PageController 收到后统一处理焦点和鼠标。 */
DECLARE_MULTICAST_DELEGATE(FCatInventoryCloseRequested);

/** 库存格子选择意图；参数是格子下标，Model 会按最新后端快照裁剪。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatInventorySlotSelectionRequested, int32);

/** 库存格子上下文意图；它只说明鼠标行为，不直接转成服务器命令。 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FCatInventorySlotPointerRequested, int32, ECatInventorySlotPointerAction);

/** 库存主界面转发的格子 Drop 意图；源和目标都是格子只读投影，PageController 会按最新 Model 复核后再提交服务器。 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FCatInventorySlotDropForwarded, const FCatInventorySlotView&, const FCatInventorySlotView&);

/** 库存动作意图；Widget 不携带鱼 ID，PageController 从 Model 当前选择重建正式服务器命令。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatInventoryActionRequested, ECatInventoryAction);

/** 库存主界面；它展示随身库存物品和鱼容器，吃鱼/献祭通过鱼容器选择提交，库存整理和鱼容器移动都通过格子拖拽提交。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatInventoryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 接收库存 Model 的完整只读投影；刷新 Designer 字段并用 WrapBox 重建格子 Widget。 */
	void RenderInventory(const FCatInventoryViewState& ViewState);

	/** 设置 WrapBox 动态创建格子时使用的 WBP 类；LocalPlayer 从 UI Settings 传入，避免主界面硬编码资产路径。 */
	void SetInventorySlotWidgetClass(TSubclassOf<UCatInventorySlotWidget> InSlotWidgetClass);

	/** 蓝图或按钮请求关闭库存；View 只广播意图，不修改输入模式。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	void RequestCloseInventory();

	/** 蓝图或格子请求选中某个下标；View 只广播下标，不读取或修改鱼数组。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	void RequestSelectSlot(int32 SlotIndex);

	/** 请求吃掉当前选中鱼；真正的鱼实例由 PageController 从 Model 当前快照读取。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	void RequestConsumeSelectedFish();

	/** 请求献祭当前选中鱼；献祭命令由 PageController 从 Model 构造。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	void RequestSacrificeSelectedFish();

	/** 最近一次库存投影是 View 与蓝图动画之间的只读交接面；蓝图只能读取它来表现当前帧，不能借它回写 Model 或玩法状态。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Inventory")
	const FCatInventoryViewState& GetLastInventoryViewState() const;

	/** 库存关闭意图广播；PageController 是正式订阅者。 */
	FCatInventoryCloseRequested OnCloseRequested;

	/** 格子选择意图广播；主界面不自己裁剪下标。 */
	FCatInventorySlotSelectionRequested OnSlotSelectionRequested;

	/** 格子鼠标上下文广播；PageController 默认只选择，蓝图可用它弹上下文表现。 */
	FCatInventorySlotPointerRequested OnSlotPointerRequested;

	/** 格子 Drop 广播；Widget 不判断方向或权限，只把两个格子的只读事实交给 PageController。 */
	FCatInventorySlotDropForwarded OnSlotDropRequested;

	/** 吃鱼或献祭动作广播；PageController 负责翻译成服务器命令或结构化拒绝。 */
	FCatInventoryActionRequested OnInventoryActionRequested;

protected:
	/** 进入视口时绑定可选按钮；没有 Designer 绑定按钮时仍可由蓝图直接调用 Request*。 */
	virtual void NativeConstruct() override;

	/** 离开视口时只解除本 View 自己绑定的按钮和格子委托；外部关闭、格子和动作意图订阅保留到 PageController::Unbind 统一移除。 */
	virtual void NativeDestruct() override;

	/** 库存处于模态焦点时再次按打开键会请求关闭；其他按键交回父类处理。 */
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** WBP 可选渲染扩展点；正式布局可用 Designer 字段绑定和这个事件共同表现。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|Inventory")
	void BP_RenderInventory(const FCatInventoryViewState& ViewState);

private:
	/** 按当前 Slots 数组清空并重建 WrapBox 子格；WBP 可以选择单列表，也可以把随身库存和外部容器拆成两栏。 */
	void RebuildSlotWidgets();

	/** 解除当前 WrapBox 子格的原生委托；刷新或销毁前调用，避免旧格子继续广播。 */
	void UnbindSlotWidgets();

	/** Slot Widget 左键选中入口；主界面只转发下标给 PageController/Model。 */
	void HandleSlotSelected(int32 SlotIndex);

	/** Slot Widget 右键或拖拽入口；默认也选择该格，再把上下文意图交给上层。 */
	void HandleSlotPointerAction(int32 SlotIndex, ECatInventorySlotPointerAction PointerAction);

	/** Slot Widget Drop 入口；只转发源和目标投影，避免主界面从 Widget 指针反查后端事实。 */
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

	/** 库存格子 WBP 类；主界面用它为 WrapBox 每个后端格子创建独立 Widget。 */
	UPROPERTY(EditDefaultsOnly, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UCatInventorySlotWidget> InventorySlotWidgetClass;

	/** WBP Designer 中的格子容器；主界面刷新时只对它 ClearChildren/AddChild，不硬编码八个按钮。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UWrapBox> InventorySlotWrapBox;

	/** WBP Designer 中的随身库存格容器；鱼护箱子页用它把玩家背包放在独立面板里。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UWrapBox> InventoryObjectSlotWrapBox;

	/** WBP Designer 中的容器格容器；鱼护箱子页用它把鱼护箱子内容和玩家背包分开展示。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UWrapBox> ExternalContainerSlotWrapBox;

	/** WBP Designer 中的关闭按钮；点击只发关闭意图。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> CloseButton;

	/** WBP Designer 中的吃鱼按钮；点击只发动作意图。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> ConsumeFishButton;

	/** WBP Designer 中的献祭按钮；点击只发动作意图。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> SacrificeFishButton;

	/** 最近一次库存 Model 输入；WrapBox 重建、键盘关闭和蓝图绑定都读取这一份。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	FCatInventoryViewState LastInventoryViewState;

	/** 当前 WrapBox 中由本对象创建并绑定的格子 Widget；刷新前需要逐个解绑。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCatInventorySlotWidget>> BoundSlotWidgets;

	/** 最近一次库存总览文本副本；RenderInventory 写入，简单 WBP 可直接绑定它显示随身物品和鱼容器概况。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	FText BlueprintSummaryText;

	/** 最近一次当前钓鱼选择文本副本；RenderInventory 写入，蓝图只读取它表现鱼竿、鱼饵、鱼漂和耐久。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	FText BlueprintEquipmentText;

	/** 最近一次随身库存概要文本副本；RenderInventory 写入，用来把统一库存格占用情况暴露给 WBP。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	FText BlueprintInventoryItemsText;

	/** 最近一次选中鱼文本副本；RenderInventory 写入，鱼容器格子的选择变化会改变这条说明。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	FText BlueprintSelectedFishText;

	/** 最近一次库存动作结果文本副本；RenderInventory 写入，蓝图用它展示 pending、成功或拒绝反馈。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	FText BlueprintResultText;

	/** WBP Designer 中的库存摘要文本控件；存在时 RenderInventory 会直接写入随身物品和鱼容器总览。 */
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
