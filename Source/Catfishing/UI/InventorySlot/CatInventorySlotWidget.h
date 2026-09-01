#pragma once

#include "CoreMinimal.h"
#include "Blueprint/DragDropOperation.h"
#include "Blueprint/UserWidget.h"
#include "UI/Inventory/CatInventoryTypes.h"
#include "CatInventorySlotWidget.generated.h"

class UTextBlock;
class UImage;
class UTexture2D;

/** 库存格子选中事件；参数是该格自己的来源身份，不能被解释成跨库存共享下标。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatInventorySlotSelected, const FCatInventorySlotView&);

/** 库存格子右键上下文事件；主界面收到后按该格所属数据源决定取用营地物品或设置随身钓具。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatInventorySlotContextRequested, const FCatInventorySlotView&);

/** 库存格子 Drop 事件；源和目标都是 Model 投影的只读副本，真正移动仍由 PageController 重读后提交服务器。 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FCatInventorySlotDropRequested, const FCatInventorySlotView&,
                                     const FCatInventorySlotView&);

/** 库存拖拽操作的轻量载荷；它只冻结拖拽开始时的源格事实，不保存源格 Widget 指针或后端容器写口。 */
UCLASS()
class CATFISHING_API UCatInventoryDragDropOperation : public UDragDropOperation
{
	GENERATED_BODY()

public:
	/** 拖拽开始时的源格只读投影；Drop 目标收到后还必须让 PageController 对照最新 ViewState 复核。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory")
	FCatInventorySlotView SourceSlot;
};

/** 库存中的单格 WBP 基类；它不是 Button，所有点击、右键、拖拽和 Drop 都通过 UUserWidget 鼠标重写函数发出。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatInventorySlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 接收主界面传入的单格只读投影；完成后蓝图可从 LastSlotView 或 BP_RenderSlot 刷新外观。 */
	void RenderSlot(const FCatInventorySlotView& SlotView);

	/** 暴露最近一次格子投影给蓝图表现；它只是主库存传入的副本，不能被格子拿来改容器或提交领域动作。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Inventory")
	const FCatInventorySlotView& GetLastSlotView() const;

	/** 左键点击该格时广播；主界面是唯一订阅者，格子不直接访问 Model。 */
	FCatInventorySlotSelected OnSlotSelected;

	/** 右键上下文广播；它只描述 UI 意图，不提交吃鱼、移动或献祭。 */
	FCatInventorySlotContextRequested OnContextRequested;

	/** Drop 完成广播；格子只提供源目标快照，移动方向、权限和并发前提仍交给 PageController/服务器。 */
	FCatInventorySlotDropRequested OnSlotDropRequested;

protected:
	/** 首次初始化时允许格子接收鼠标和键盘焦点；具体可视布局仍来自 WBP。 */
	virtual void NativeOnInitialized() override;

	/** 鼠标按下流程只启动左键拖拽检测并处理右键上下文；左键选择等到松开时再发，避免拖拽前刷新库存页。 */
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	/** 鼠标松开流程负责确认一次普通左键点击；已经进入拖拽的输入不会靠这里提前改格子显示。 */
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	/** 拖拽开始流程为运行期库存物品或 Items 容器物体创建轻量 DragDropOperation；正式转移仍由目标格 Drop 和服务器命令裁决，拖拽预览只取真实 SlotView 缩略图，不叠加文字或上一次 Brush。 */
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent,
	                                  UDragDropOperation*& OutOperation) override;

	/** 拖拽悬停流程只判断目标格能否接收当前载荷；命中后让本格保持 Drop 目标，避免空格把事件漏给同屏其他库存页。 */
	virtual bool NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
		UDragDropOperation* InOperation) override;

	/** Drop 流程只识别运行期库存格或 Items 容器格的拖拽载荷，并把源目标格事实广播给主界面复核。 */
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
		UDragDropOperation* InOperation) override;

	/** WBP 可选渲染扩展点；Designer 也可以直接绑定 LastSlotView、BlueprintDisplayText 等字段。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|Inventory")
	void BP_RenderSlot(const FCatInventorySlotView& SlotView);

private:
	/** 最近一次主界面传入的格子投影；所有鼠标事件都用它识别空格、来源身份和拖拽上下文。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	FCatInventorySlotView LastSlotView;

	/** 给 WBP TextBlock 直接绑定的格子文本；RenderSlot 从 LastSlotView 写入，鼠标事件不修改。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	FText BlueprintDisplayText;

	/** 给 WBP 名称控件直接绑定的玩家可见名称；它来自 SlotView 的定义资产投影，Widget 不再自己查定义表。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	FText BlueprintDisplayName;

	/** 给 WBP 详情控件直接绑定的说明文本；配置缺失时为空，不影响格子交互。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	FText BlueprintDescription;

	/** 给 WBP 图片控件直接绑定的缩略图软引用；鱼和运行期库存物品都从同一份 SlotView 读取。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	TSoftObjectPtr<UTexture2D> BlueprintThumbnail;

	/** 给 WBP 数量控件直接绑定的当前数量；不可堆叠物品通常为 1，空格为 0。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	int32 BlueprintQuantity = 0;

	/** 给 WBP 数量角标直接绑定的文本；不需要显示数量时为空文本。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	FText BlueprintQuantityText;

	/** 给 WBP 数量角标显隐直接绑定的状态；它由 Model 按数量和堆叠规则计算。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	bool bBlueprintShowQuantity = false;

	/** 给 WBP 堆叠表现直接绑定的状态；它表示定义层是否允许单格承载多份。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	bool bBlueprintStackable = false;

	/** 给 WBP 边框或颜色绑定的占用状态；它来自库存 Model 对运行期库存格或鱼容器格的只读投影。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	bool bBlueprintOccupied = false;

	/** 给 WBP 边框或颜色绑定的选中状态；它只来自库存 Model 当前选择。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	bool bBlueprintSelected = false;

	/** WBP Designer 中的格子文本控件；存在时 RenderSlot 会直接写入当前鱼或空格摘要。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DisplayTextBlock;

	/** WBP Designer 中的缩略图控件；存在时 RenderSlot 会写入当前鱼或物品定义里的预览图。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> ThumbnailImage;

	/** WBP Designer 中的数量角标文本控件；存在时 RenderSlot 会按 bBlueprintShowQuantity 自动显隐。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> QuantityTextBlock;
};
