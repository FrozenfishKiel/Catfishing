#pragma once

#include "CoreMinimal.h"
#include "Blueprint/DragDropOperation.h"
#include "Blueprint/UserWidget.h"
#include "UI/Inventory/CatInventoryTypes.h"
#include "CatInventorySlotWidget.generated.h"

class UTextBlock;

/** 背包格子选中事件；参数是主界面分配的格子下标，不携带鱼命令载荷。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatInventorySlotSelected, int32);

/** 背包格子鼠标事件；主界面收到后决定选择、上下文或拖拽表现，不让格子直接执行领域命令。 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FCatInventorySlotPointerActionRequested, int32, ECatInventorySlotPointerAction);

/** 背包格子 Drop 事件；源和目标都是 Model 投影的只读副本，真正移动仍由 PageController 重读后提交服务器。 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FCatInventorySlotDropRequested, const FCatInventorySlotView&, const FCatInventorySlotView&);

/** 背包拖拽操作的轻量载荷；它只冻结拖拽开始时的源格事实，不保存源格 Widget 指针或后端容器写口。 */
UCLASS()
class CATFISHING_API UCatInventoryDragDropOperation : public UDragDropOperation
{
	GENERATED_BODY()

public:
	/** 拖拽开始时的源格只读投影；Drop 目标收到后还必须让 PageController 对照最新 ViewState 复核。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory")
	FCatInventorySlotView SourceSlot;
};

/** 背包中的单格 WBP 基类；它不是 Button，所有点击、右键、拖拽和 Drop 都通过 UUserWidget 鼠标重写函数发出。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatInventorySlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 接收主界面传入的单格只读投影；完成后蓝图可从 LastSlotView 或 BP_RenderSlot 刷新外观。 */
	void RenderSlot(const FCatInventorySlotView& SlotView);

	/** 暴露最近一次格子投影给蓝图表现；它只是主背包传入的副本，不能被格子拿来改容器或提交领域动作。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Inventory")
	const FCatInventorySlotView& GetLastSlotView() const;

	/** 左键选择该格时广播；主界面是唯一订阅者，格子不直接访问 Model。 */
	FCatInventorySlotSelected OnSlotSelected;

	/** 右键、拖拽等额外交互广播；它只描述 UI 意图，不提交吃鱼、移动或献祭。 */
	FCatInventorySlotPointerActionRequested OnPointerActionRequested;

	/** Drop 完成广播；格子只提供源目标快照，移动方向、权限和 Revision 仍交给 PageController/服务器。 */
	FCatInventorySlotDropRequested OnSlotDropRequested;

protected:
	/** 首次初始化时允许格子接收鼠标和键盘焦点；具体可视布局仍来自 WBP。 */
	virtual void NativeOnInitialized() override;

	/** 鼠标按下流程会区分左键选择和右键上下文；空格只广播上下文，不构造领域命令。 */
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	/** 拖拽开始流程只为容器物体创建轻量 DragDropOperation；正式转移仍由目标格 Drop 和服务器命令裁决。 */
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent,
		UDragDropOperation*& OutOperation) override;

	/** Drop 流程只识别本背包的容器物体拖拽载荷，并把源目标格事实广播给主界面复核。 */
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
		UDragDropOperation* InOperation) override;

	/** WBP 可选渲染扩展点；Designer 也可以直接绑定 LastSlotView、BlueprintDisplayText 等字段。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|Inventory")
	void BP_RenderSlot(const FCatInventorySlotView& SlotView);

private:
	/** 最近一次主界面传入的格子投影；所有鼠标事件都用它过滤空格、旧下标和拖拽上下文。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	FCatInventorySlotView LastSlotView;

	/** 给 WBP TextBlock 直接绑定的格子文本；RenderSlot 从 LastSlotView 写入，鼠标事件不修改。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	FText BlueprintDisplayText;

	/** 给 WBP 边框或颜色绑定的占用状态；它来自背包 Model 对鱼竿槽或容器槽的只读投影。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	bool bBlueprintOccupied = false;

	/** 给 WBP 边框或颜色绑定的选中状态；它只来自背包 Model 当前选择。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	bool bBlueprintSelected = false;

	/** WBP Designer 中的格子文本控件；存在时 RenderSlot 会直接写入当前鱼或空格摘要。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DisplayTextBlock;
};
