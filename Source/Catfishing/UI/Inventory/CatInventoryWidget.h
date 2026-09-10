#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CatInventoryWidget.generated.h"

class UButton;
class UCatInventoryComponent;
class UCatInventoryPageController;
class UCatInventorySlotWidget;
class UWrapBox;

/** 对应 AOBackPackUI 的库存面板；绑定一份明确库存的 Model，数据变化时只刷新自己的格子。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatInventoryWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	/** 指定本面板显示的库存并绑定其 Model；页面注入外部库存，未注入的背包面板使用 owning Pawn 的库存。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	void SetInventoryContext(UCatInventoryComponent* InInventory);

	/** 读取本面板唯一的数据源；同屏多个库存页依靠各自上下文隔离，槽位操作必须带回这份库存而不是按页面类型猜宿主。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Inventory")
	UCatInventoryComponent* GetInventoryContext() const;

	/** 指定正式格子 WBP 类；下一次列表刷新使用它创建控件。 */
	void SetInventorySlotWidgetClass(TSubclassOf<UCatInventorySlotWidget> InSlotWidgetClass);

	/** 请求关闭库存窗口；只交给页面控制器恢复输入，不修改 Model。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	void RequestCloseInventory();

	/** 选中本面板使用按钮所指的格位；只更新按钮可用性，不广播库存变化。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	void RequestSelectSlot(int32 SlotIndex);

	/** 请求使用本页当前选中格；与格子右键共用同一个提交入口。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	void RequestUseSelectedItem();

protected:
	/** 构建时绑定按钮和对应库存 Model，再读取当前列表；嵌套背包独立解析自己的 Pawn 库存。 */
	virtual void NativeConstruct() override;

	/** 移出视口时解除 Model、按钮和格子监听；显示上下文保留供同一面板再次打开。 */
	virtual void NativeDestruct() override;

	/** 优先处理窗口关闭键，避免焦点停在子格时失效；其他按键保持 UMG 传播。 */
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** 根页获得键盘焦点时也接受同一关闭键；其他按键交回父类。 */
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	/** 读取所绑定 Model 的列表并重建本面板 WrapBox；只由绑定或库存通知触发，不由点击触发。 */
	void RefreshInventorySlots();

	/** 解除原 Model 的通知句柄；上下文切换和面板销毁都必须从原库存移除。 */
	void UnbindInventoryModel();

	/** 解除动态格子的选择监听并清空本地控件引用；UMG 负责控件释放。 */
	void UnbindSlotWidgets();

	/** 解析本地页面控制器，关闭和按键判断只读取这一份窗口状态。 */
	UCatInventoryPageController* ResolveInventoryPageController() const;

	/** 按当前页面类型收口关闭键；外部库存允许交互键退出，普通背包不接管交互键，避免同一输入同时驱动世界交互和背包开关。 */
	bool ShouldCloseInventoryFromKey(const FKeyEvent& InKeyEvent) const;

	/** 关闭按钮统一调用公开关闭入口，避免另建输入恢复路径。 */
	UFUNCTION()
	void HandleCloseClicked();

	/** 使用按钮统一调用当前选中格的使用入口。 */
	UFUNCTION()
	void HandleConsumeClicked();

	/** 本面板的显示库存；页面注入或构建时解析，切换时重绑 Model，不受其他 WBP 的上下文影响。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatInventoryComponent> DisplayInventory;

	/** 注册在该库存 Model 上的列表通知句柄；上下文切换与销毁时移除。 */
	FDelegateHandle InventoryModelChangedHandle;

	/** 本页选中的库存下标；点击写入，库存变化后清除，使用按钮只读取它。 */
	int32 SelectedSlotIndex = INDEX_NONE;

	/** 动态库存格使用的正式 WBP 类；页面或 UI Settings 提供，刷新时读取。 */
	UPROPERTY(EditDefaultsOnly, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UCatInventorySlotWidget> InventorySlotWidgetClass;

	/** 正式 WBP 的格子容器；刷新只清理并填充本面板的容器，不触碰嵌套库存页。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UWrapBox> InventorySlotWrapBox;

	/** 当前格子控件及选择订阅的所有权记录；刷新和销毁时解绑，点击只访问本数组。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCatInventorySlotWidget>> SlotWidgets;

	/** 正式 WBP 可选的关闭按钮；构建时绑定，销毁时移除监听。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> CloseButton;

	/** 鱼护 WBP 的使用按钮；仅选中本页非空格时启用，提交仍走格子统一使用入口。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> ConsumeFishButton;
};
