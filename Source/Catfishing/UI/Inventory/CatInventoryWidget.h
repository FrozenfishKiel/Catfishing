#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CatInventoryWidget.generated.h"

class UCatInventoryComponent;
class UCatInventoryPageController;
class UCatInventorySlotWidget;
class UTextBlock;
class UWrapBox;
struct FCatDomainCommandResult;
struct FCatInventoryEntry;

/** 库存面板只绑定一份库存并提供左键选择和拖拽宿主；物品操作统一交给页面控制器的右键菜单。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatInventoryWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	/** 指定本面板显示的库存并绑定其 Model；页面注入外部库存，未注入的背包面板使用 owning Pawn 的库存。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	void SetInventoryContext(UCatInventoryComponent* InInventory);
	/** 读取本面板唯一的数据源；多个库存页依靠各自上下文隔离。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Inventory")
	UCatInventoryComponent* GetInventoryContext() const;
	/** 指定正式格子 WBP 类；下一次列表刷新使用它创建控件。 */
	void SetInventorySlotWidgetClass(TSubclassOf<UCatInventorySlotWidget> InSlotWidgetClass);
	/** 请求关闭库存窗口；只交给页面控制器恢复输入并取消菜单，不修改 Model。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	void RequestCloseInventory();
	/** 只记录非背包容器的页面局部选择；背包点击不产生选择，也不会连接物品栏。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	virtual void RequestSelectSlot(int32 SlotIndex);
	/** 显示页面控制器关联的库存动作服务器回执；它只更新表现文本并刷新读模型。 */
	void ShowInventoryActionResult(const FCatDomainCommandResult& Result);
	/** 菜单关闭后重新读取当前 Slate 悬停格；只显示此刻真实命中的有效格，不恢复缓存的旧物品。 */
	void RefreshTooltipAtCurrentSlateHit();

protected:
	/** 构建时绑定领域回执和对应库存 Model，再读取当前列表；页面退出由现有按键入口处理。 */
	virtual void NativeConstruct() override;
	/** 移出视口时解除 Model、回执、按钮和格子监听；显示上下文保留供再次打开。 */
	virtual void NativeDestruct() override;
	/** 在子格消费前先转交 F8/F9，再处理菜单 Escape 和本页关闭键。 */
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	/** 根页获得焦点时复用同一键盘路由。 */
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	/** 读取当前选中格的只读副本和下标；容器级操作冻结对象前使用。 */
	bool GetSelectedInventoryEntry(FCatInventoryEntry& OutEntry, int32& OutSlotIndex) const;
	/** 读取 Model 列表并重建本面板 WrapBox；库存通知和命令回执触发。 */
	virtual void RefreshInventorySlots();
	/** 容器级操作提交 RPC 前登记本页请求；菜单操作由 PageController 单独关联。 */
	void BeginInventoryCommand(const FGuid& RequestId);
	/** 本页尚未收到终态回执的容器级操作标识；不代表库存事实。 */
	FGuid PendingCommandRequestId;

private:
	/** 只消费本页容器级请求的领域回执；菜单回执由 PageController 单独处理。 */
	void HandleInventoryCommandResult(const FCatDomainCommandResult& Result);
	/** 解除原 Model 的通知句柄；上下文切换和面板销毁都必须从原库存移除。 */
	void UnbindInventoryModel();
	/** 解除动态格子的选择监听并清空本地控件引用。 */
	void UnbindSlotWidgets();
	/** 解析本地页面控制器，关闭和菜单状态只读取这一份控制器。 */
	UCatInventoryPageController* ResolveInventoryPageController() const;
	/** 按当前页面类型收口关闭键；外部库存额外接受交互键退出。 */
	bool ShouldCloseInventoryFromKey(const FKeyEvent& InKeyEvent) const;
	/** 本面板的显示库存；页面注入或构建时解析，切换时重绑 Model。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatInventoryComponent> DisplayInventory;
	/** 注册在该库存 Model 上的列表通知句柄；上下文切换与销毁时移除。 */
	FDelegateHandle InventoryModelChangedHandle;
	/** 本页实际订阅回执的控制器；销毁时从同一对象解绑。 */
	TWeakObjectPtr<class ACatfishingPlayerController> CommandResultController;
	/** 最近一次本页操作的服务器结果文本；匹配回执后写入，不预测库存变更。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> InventoryActionResultText;
	/** 本页选中的库存下标；库存变化后清除，只供容器级入口读取。 */
	int32 SelectedSlotIndex = INDEX_NONE;
	/** 动态库存格使用的正式 WBP 类；页面或 UI Settings 提供。 */
	UPROPERTY(EditDefaultsOnly, Category = "Catfishing|Inventory", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UCatInventorySlotWidget> InventorySlotWidgetClass;
	/** 正式 WBP 的格子容器；刷新只清理并填充本面板的容器。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UWrapBox> InventorySlotWrapBox;
	/** 当前格子控件及选择订阅的所有权记录；刷新和销毁时解绑。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCatInventorySlotWidget>> SlotWidgets;
};
