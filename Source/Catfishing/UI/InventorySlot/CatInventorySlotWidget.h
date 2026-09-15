#pragma once

#include "CoreMinimal.h"
#include "Blueprint/DragDropOperation.h"
#include "Blueprint/UserWidget.h"
#include "Inventory/CatInventoryComponent.h"
#include "CatInventorySlotWidget.generated.h"

class UImage;
class UBorder;
class UTextBlock;
class UCatItemTooltipController;

/** 格子点击通知；父库存页只用本库存内的下标记录选择，物品使用与拖放直接提交 owning Controller。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatInventorySlotSelected, int32);

/** 拖放期间保留的源库存和格位；目标格据此提交交换，不冻结数量或建立另一份库存快照。 */
UCLASS()
class CATFISHING_API UCatInventoryDragDropOperation : public UDragDropOperation
{
	GENERATED_BODY()
public:
	/** 拖拽开始时的源库存；Drop 读取它解析服务器请求的宿主，操作释放后不再持有。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatInventoryComponent> SourceInventory;

	/** 源库存中的格位；Drop 只传递位置，服务器读取当时的实际内容。 */
	UPROPERTY(Transient)
	int32 SourceSlotIndex = INDEX_NONE;
};

/** 对应 AOInventorySlotBase/AOInventoryUI 的库存格：持有库存、下标和条目，直接提交使用与交换，显示更新来自 Model。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatInventorySlotWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	/** 把本格绑定到明确库存位置并显示条目；父库存 UI 遍历 Model 列表时调用。 */
	void SetSlotContext(int32 InSlotIndex, UCatInventoryComponent* InInventory, const FCatInventoryEntry& InEntry);

	/** 读取本格的显示副本；它服务图片、数量和按钮状态，真实使用/交换仍把库存宿主与槽位交给服务器重读。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Inventory")
	const FCatInventoryEntry& GetInventoryEntry() const;

	/** 读取本格当前绑定的库存来源；页面控制器只用它在提交前回到正式库存重读，不把显示副本当事实。 */
	UCatInventoryComponent* GetSourceInventory() const;

	/** 读取本格在来源库存中的当前下标；重绑后会变化，菜单提交前必须和实例身份一起复核。 */
	int32 GetSlotIndex() const;

	/** 按本地 Controller 的物品栏选择投影刷新外圈；仅改变表现，不保存另一份选择状态。 */
	void SetSelectedFromModel(bool bSelected);

	/** 返回正式选中外圈当前是否显示；Quickbar 自动化读取它核对 View 与 Controller 焦点，没有写入副作用。 */
	bool IsSelectedFromModel() const;

	/** 设置本格是否承接鼠标操作；快捷栏关闭交互以避免展示 View 旁路背包窗口的拖放和右键菜单。 */
	void SetAcceptsSlotInput(bool bInAcceptsSlotInput);

	/** 普通左键点击时通知父页本库存内的下标；拖拽不会提前触发选择。 */
	FCatInventorySlotSelected OnSlotSelected;

	/** 父页重建格子前撤销本格的提示来源；不会关闭已经由另一格接管的信息框。 */
	void CancelTooltip();

protected:
	/** 首次初始化时允许本格接收输入；布局和悬停颜色继续使用正式 WBP。 */
	virtual void NativeOnInitialized() override;

	/** 鼠标进入时把本格和鼠标屏幕坐标交给本地 Tooltip Controller。 */
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	/** 鼠标离开时仅撤销本格自己的悬停提示。 */
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;

	/** 控件销毁时兜底撤销悬停，覆盖格子重建或页面被移除但未收到 Leave 的情形。 */
	virtual void NativeDestruct() override;

	/** 左键开始检测拖拽，右键交给页面控制器打开统一菜单；其他输入交回父类。 */
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	/** 普通左键松开时通知父页选择本格；已经进入拖拽的输入由 Drop 路径处理。 */
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	/** 非空格开始拖拽时保存源库存与格位，并用物品图片创建临时预览。 */
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent,
		UDragDropOperation*& OutOperation) override;

	/** 库存拖拽悬停在有效格位时接住事件；不改数据或触发刷新。 */
	virtual bool NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
		UDragDropOperation* InOperation) override;

	/** 从拖拽载荷取得源格，连同本格交给 owning Controller；服务器修改后由 Model 通知 UI。 */
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
		UDragDropOperation* InOperation) override;

private:
	/** 从 owning LocalPlayer 读取已装配的唯一 Tooltip Controller；不创建额外显示入口。 */
	UCatItemTooltipController* ResolveTooltipController() const;

	/** 本格所属库存；父页绑定时写入，鼠标操作用它确定服务器请求的宿主。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatInventoryComponent> SourceInventory;

	/** 本格在所属库存中的位置；不同库存可以有相同下标，操作时必须同时携带 SourceInventory。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	int32 SlotIndex = INDEX_NONE;

	/** Model 提供的本格条目；本控件只显示它，服务器仍从实际库存读取操作对象。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	FCatInventoryEntry InventoryEntry;

	/** 正式格子 WBP 的物品图；绑定条目时从物品定义更新，空格清除旧图片。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> ThumbnailImage;

	/** 正式格子 WBP 的数量角标；仅在数量大于 1 时显示。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> QuantityTextBlock;

	/** 物品栏专用 WBP 的可选数字提示；绑定格位时写入 1 起始编号，背包与外部容器的格子不包含它。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SlotKeyTextBlock;

	/** 物品栏专用 WBP 的可选外圈；只根据 Controller 本地选择显隐，空格也允许显示被选中状态。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> SelectedBorder;

	/** 本格是否是可操作库存页的一部分；库存窗口保持 true，常驻快捷栏写 false 后只承担只读显示。 */
	bool bAcceptsSlotInput = true;
};
