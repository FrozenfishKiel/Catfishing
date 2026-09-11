#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CatInventoryWidget.generated.h"

class UButton;
class ACatfishingPlayerController;
class UCatInventoryComponent;
class UCatInventoryPageController;
class UCatInventorySlotWidget;
class USpinBox;
class UTextBlock;
class UWidget;
class UWrapBox;
enum class ECatInventoryWorldAction : uint8;
struct FCatInventoryEntry;
struct FCatDomainCommandResult;

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
	virtual void RequestSelectSlot(int32 SlotIndex);

	/** 请求使用本页当前选中格；与格子右键共用同一个提交入口。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	void RequestUseSelectedItem();

	/** 请求把当前选中物品以物理轻抛方式离开库存；堆叠物先进入当前页面的数量确认，服务器仍复核实例和数量。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	void RequestDropSelectedItem();

	/** 请求把当前选中物品放到服务器确认的地面位置；堆叠物先进入当前页面的数量确认，不创建客户端预览。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	void RequestPlaceSelectedItem();

	/** 请求把鱼缸或地面鱼护中当前选中的单条鱼叼到嘴部；仅转交既有库存世界动作入口，服务器继续复核容器、鱼实例和口中占用。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	void RequestCarrySelectedFish();

protected:
	/** 构建时绑定按钮、领域回执和对应库存 Model，再读取当前列表；嵌套背包独立解析自己的 Pawn 库存。 */
	virtual void NativeConstruct() override;

	/** 移出视口时解除 Model、回执、按钮和格子监听并清理数量与请求状态；显示上下文保留供再次打开。 */
	virtual void NativeDestruct() override;

	/** 优先处理窗口关闭键，避免焦点停在子格时失效；其他按键保持 UMG 传播。 */
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** 根页获得键盘焦点时也接受同一关闭键；其他按键交回父类。 */
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** 页面可见期间只轮询已复制的嘴部携带引用并同步叼起按钮；不重建库存或改变选择，保证外部占用变化能立即禁用操作。 */
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** 读取当前选中格的只读副本和下标；子页面用它冻结操作对象，真实库存仍在提交前由服务器重读。 */
	bool GetSelectedInventoryEntry(FCatInventoryEntry& OutEntry, int32& OutSlotIndex) const;

	/** 读取所绑定 Model 的列表并重建本面板 WrapBox；绑定、库存通知和操作提交或回执触发，普通选中不会重建。 */
	virtual void RefreshInventorySlots();

	/** 提交 RPC 前登记本页请求并清理选择；回执到达前禁用操作，兼容房主同步回执。 */
	void BeginInventoryCommand(const FGuid& RequestId);

	/** 本页尚未收到终态回执的操作标识；提交前写入，匹配回执或销毁时清空，不代表库存事实。 */
	FGuid PendingCommandRequestId;

private:
	/** 只消费本页请求的领域回执，显示结果并重读库存；其他系统或已关页请求不影响当前选择。 */
	void HandleInventoryCommandResult(const FCatDomainCommandResult& Result);

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

	/** Drop 按钮入口；根据当前选中格决定直接提交或显示同页数量面板。 */
	UFUNCTION()
	void HandleDropClicked();

	/** Place 按钮入口；根据当前选中格决定直接提交或显示同页数量面板。 */
	UFUNCTION()
	void HandlePlaceClicked();

	/** 数量确认按钮入口；只接受仍指向冻结实例且数量足够的格位，避免刷新后误丢新物品。 */
	UFUNCTION()
	void HandleReleaseQuantityConfirmed();

	/** 数量取消按钮入口；清空冻结选择并隐藏当前页面的数量面板，不修改库存。 */
	UFUNCTION()
	void HandleReleaseQuantityCancelled();

	/** 为选中格准备 Drop 或 Place；单件直接提交，堆叠物冻结来源、槽位、实例和数量上限后等待确认。 */
	void BeginReleaseSelectedItem(ECatInventoryWorldAction Action);

	/** 根据选中条目、目标鱼容器和角色已复制的嘴部携带 Actor 判断本地是否能叼起；它只控制 UI 与提交前拒绝，服务器仍是最终裁决者。 */
	bool CanCarrySelectedFish(FCatInventoryEntry& OutEntry, int32& OutSlotIndex) const;

	/** 只投影当前叼起资格到可选按钮；由选择、库存刷新、命令状态和窄 Tick 复用，避免为嘴部占用另建复制或事件真相。 */
	void RefreshCarryAction();

	/** 向 PlayerController 提交已冻结的物品离库意图；UI 不改库存，服务器以宿主、槽位和实例 ID 复核后执行。 */
	void SubmitReleaseItem(UCatInventoryComponent* SourceInventory, int32 SourceSlotIndex,
		const FGuid& ItemInstanceId, int32 Quantity, ECatInventoryWorldAction Action);

	/** 确认前判断冻结格位是否仍是原实例并裁剪到可用整数数量；失败即取消，库存通知会直接清除冻结选择。 */
	bool ResolvePendingRelease(int32& OutQuantity) const;

	/** 清空冻结的离库选择并同步数量面板可见性；库存刷新、取消和提交后共用，防止旧槽位继续可提交。 */
	void ResetPendingRelease();

	/** 本面板的显示库存；页面注入或构建时解析，切换时重绑 Model，不受其他 WBP 的上下文影响。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatInventoryComponent> DisplayInventory;

	/** 注册在该库存 Model 上的列表通知句柄；上下文切换与销毁时移除。 */
	FDelegateHandle InventoryModelChangedHandle;

	/** 本页实际订阅回执的控制器；构建时记录，销毁时从同一对象解绑，避免 owning player 切换留下监听。 */
	TWeakObjectPtr<ACatfishingPlayerController> CommandResultController;

	/** 最近一次本页操作的服务器结果文本；提交时清空，匹配回执后显示成功或拒绝，不预测库存变更。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> InventoryActionResultText;

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

	/** 正式 WBP 的丢弃按钮；点击读取当前选中格并走统一的服务器世界落地请求。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> DropButton;

	/** 正式 WBP 的放置按钮；点击复用当前选中格与数量面板，但把动作语义交给服务器。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> PlaceButton;

	/** 正式鱼缸与鱼护 WBP 可选的叼起按钮；只在选中单条鱼、当前容器受支持且嘴部空闲时启用，点击不直接写库存。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> CarryButton;

	/** 当前库存页内的数量确认区域；只在堆叠 Drop/Place 时显示，不承担独立页面或库存状态。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UWidget> ReleaseQuantityPanel;

	/** 数量确认区域的数量输入，代表冻结实例本次离库的数量，范围由 BeginReleaseSelectedItem 写为 1 到当前堆叠数。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USpinBox> ReleaseQuantitySpinBox;

	/** 数量确认区域的提交按钮；点击时重新比对冻结实例与当前库存，再向服务器发出唯一请求。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> ReleaseQuantityConfirmButton;

	/** 数量确认区域的取消按钮；点击只撤销本地冻结选择，库存内容保持不变。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> ReleaseQuantityCancelButton;

	/** 数量面板冻结时的来源库存；开始选择时写入，刷新、取消或提交后清空，防止页面切换误用旧宿主。 */
	TWeakObjectPtr<UCatInventoryComponent> PendingReleaseInventory;

	/** 数量面板冻结时的库存槽位；它必须和实例 ID 同时匹配，不能单独按下标提交。 */
	int32 PendingReleaseSlotIndex = INDEX_NONE;

	/** 数量面板冻结时的物品实例身份；确认时与当前条目比对，避免槽位换物后把新物品离库。 */
	FGuid PendingReleaseItemInstanceId;

	/** 数量面板允许的最大离库数量；开始选择时由当前堆叠数写入，确认时还会复核实际数量。 */
	int32 PendingReleaseMaximumQuantity = 0;

	/** 数量面板冻结的世界动作类型；确认时复用它区分轻抛和固定放置，不由 UI 重新猜测。 */
	ECatInventoryWorldAction PendingReleaseAction{};
};
