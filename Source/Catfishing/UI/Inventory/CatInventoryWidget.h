#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Inventory/CatInventoryTypes.h"
#include "CatInventoryWidget.generated.h"

class UButton;
class UCatInventorySlotWidget;
class UTextBlock;
class UWrapBox;

/** 背包关闭意图；Widget 不恢复输入模式，PageController 收到后统一处理焦点和鼠标。 */
DECLARE_MULTICAST_DELEGATE(FCatInventoryCloseRequested);

/** 背包格子选择意图；参数是格子下标，Model 会按最新后端快照裁剪。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatInventorySlotSelectionRequested, int32);

/** 背包格子上下文意图；它只说明鼠标行为，不直接转成服务器命令。 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FCatInventorySlotPointerRequested, int32, ECatInventorySlotPointerAction);

/** 背包动作意图；Widget 不携带鱼 ID，PageController 从 Model 当前选择重建正式服务器命令。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatInventoryActionRequested, ECatInventoryAction);

/** 个人鱼护/背包主界面；它只拥有 WrapBox 和动作按钮，不承载 HUD、商店或图鉴。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatInventoryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 接收背包 Model 的完整只读投影；刷新 Designer 字段并用 WrapBox 重建格子 Widget。 */
	void RenderInventory(const FCatInventoryViewState& ViewState);

	/** 设置 WrapBox 动态创建格子时使用的 WBP 类；LocalPlayer 从 UI Settings 传入，避免主界面硬编码资产路径。 */
	void SetInventorySlotWidgetClass(TSubclassOf<UCatInventorySlotWidget> InSlotWidgetClass);

	/** 蓝图或按钮请求关闭背包；View 只广播意图，不修改输入模式。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	void RequestCloseInventory();

	/** 蓝图或格子请求选中某个下标；View 只广播下标，不读取或修改鱼数组。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	void RequestSelectSlot(int32 SlotIndex);

	/** 请求吃掉当前选中鱼；真正的鱼实例由 PageController 从 Model 当前快照读取。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	void RequestConsumeSelectedFish();

	/** 请求把当前选中鱼转入共享鱼缸；营地、鱼缸和 Revision 由 PageController 重读。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	void RequestTransferSelectedFishToTank();

	/** 请求献祭当前选中鱼；献祭命令由 PageController 从 Model 构造。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	void RequestSacrificeSelectedFish();

	/** 最近一次背包投影是 View 与蓝图动画之间的只读交接面；蓝图只能读取它来表现当前帧，不能借它回写 Model 或玩法状态。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Inventory")
	const FCatInventoryViewState& GetLastInventoryViewState() const;

	/** 背包关闭意图广播；PageController 是正式订阅者。 */
	FCatInventoryCloseRequested OnCloseRequested;

	/** 格子选择意图广播；主界面不自己裁剪下标。 */
	FCatInventorySlotSelectionRequested OnSlotSelectionRequested;

	/** 格子鼠标上下文广播；PageController 默认只选择，蓝图可用它弹上下文表现。 */
	FCatInventorySlotPointerRequested OnSlotPointerRequested;

	/** 吃鱼、转缸或献祭动作广播；PageController 负责翻译成服务器命令。 */
	FCatInventoryActionRequested OnInventoryActionRequested;

protected:
	/** 进入视口时绑定可选按钮；没有 Designer 绑定按钮时仍可由蓝图直接调用 Request*。 */
	virtual void NativeConstruct() override;

	/** 离开视口时只解除本 View 自己绑定的按钮和格子委托；外部关闭、格子和动作意图订阅保留到 PageController::Unbind 统一移除。 */
	virtual void NativeDestruct() override;

	/** 背包 UIOnly 焦点下再次按打开键会请求关闭；其他按键交回父类处理。 */
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** WBP 可选渲染扩展点；正式布局可用 Designer 字段绑定和这个事件共同表现。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|Inventory")
	void BP_RenderInventory(const FCatInventoryViewState& ViewState);

private:
	/** 按当前 Slots 数组清空并重建 WrapBox 子格；每个子格都是 UCatInventorySlotWidget，不是 Button。 */
	void RebuildSlotWidgets();

	/** 解除当前 WrapBox 子格的原生委托；刷新或销毁前调用，避免旧格子继续广播。 */
	void UnbindSlotWidgets();

	/** Slot Widget 左键选中入口；主界面只转发下标给 PageController/Model。 */
	void HandleSlotSelected(int32 SlotIndex);

	/** Slot Widget 右键或拖拽入口；默认也选择该格，再把上下文意图交给上层。 */
	void HandleSlotPointerAction(int32 SlotIndex, ECatInventorySlotPointerAction PointerAction);

	/** 关闭按钮点击入口；收口到 RequestCloseInventory，避免按钮和蓝图图表两套逻辑。 */
	UFUNCTION()
	void HandleCloseClicked();

	/** 吃鱼按钮点击入口；收口到 RequestConsumeSelectedFish。 */
	UFUNCTION()
	void HandleConsumeClicked();

	/** 转缸按钮点击入口；收口到 RequestTransferSelectedFishToTank。 */
	UFUNCTION()
	void HandleTransferClicked();

	/** 献祭按钮点击入口；收口到 RequestSacrificeSelectedFish。 */
	UFUNCTION()
	void HandleSacrificeClicked();

	/** 背包格子 WBP 类；主界面用它为 WrapBox 每个后端格子创建独立 Widget。 */
	UPROPERTY(EditDefaultsOnly, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UCatInventorySlotWidget> InventorySlotWidgetClass;

	/** WBP Designer 中的格子容器；主界面刷新时只对它 ClearChildren/AddChild，不硬编码八个按钮。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UWrapBox> InventorySlotWrapBox;

	/** WBP Designer 中的关闭按钮；点击只发关闭意图。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> CloseButton;

	/** WBP Designer 中的吃鱼按钮；点击只发动作意图。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> ConsumeFishButton;

	/** WBP Designer 中的转缸按钮；点击只发动作意图。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> TransferFishToTankButton;

	/** WBP Designer 中的献祭按钮；点击只发动作意图。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> SacrificeFishButton;

	/** 最近一次背包 Model 输入；WrapBox 重建、键盘关闭和蓝图绑定都读取这一份。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	FCatInventoryViewState LastInventoryViewState;

	/** 当前 WrapBox 中由本对象创建并绑定的格子 Widget；刷新前需要逐个解绑。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCatInventorySlotWidget>> BoundSlotWidgets;

	/** 给 WBP TextBlock 直接绑定的背包摘要文本。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	FText BlueprintSummaryText;

	/** 给 WBP TextBlock 直接绑定的选中鱼文本。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	FText BlueprintSelectedFishText;

	/** 给 WBP TextBlock 直接绑定的最近结果文本。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	FText BlueprintResultText;

	/** WBP Designer 中的背包摘要文本控件；存在时 RenderInventory 会直接写入当前容量和鱼数。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SummaryTextBlock;

	/** WBP Designer 中的选中鱼文本控件；存在时 RenderInventory 会直接写入当前选中鱼摘要。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SelectedFishTextBlock;

	/** WBP Designer 中的结果文本控件；存在时 RenderInventory 会直接写入最近动作反馈。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ResultTextBlock;
};
