#pragma once

#include "CoreMinimal.h"
#include "Blueprint/DragDropOperation.h"
#include "Blueprint/UserWidget.h"
#include "Inventory/CatInventoryComponent.h"
#include "CatInventorySlotWidget.generated.h"

class UImage;
class UTextBlock;

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

	/** 请求使用当前格；右键与父页使用按钮共用此入口，效果和扣量由服务器决定。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	void RequestUseItem();

	/** 普通左键点击时通知父页本库存内的下标；拖拽不会提前触发选择。 */
	FCatInventorySlotSelected OnSlotSelected;

protected:
	/** 首次初始化时允许本格接收输入；布局和悬停颜色继续使用正式 WBP。 */
	virtual void NativeOnInitialized() override;

	/** 左键开始检测拖拽，右键提交使用；其他输入交回父类。 */
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
};
