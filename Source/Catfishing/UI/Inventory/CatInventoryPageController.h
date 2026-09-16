#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UI/CatUIModalInputMode.h"
#include "UObject/Object.h"
#include "CatInventoryPageController.generated.h"

class APlayerController;
class UCatInventoryComponent;
class UCatInventoryContextMenuWidget;
class UCatInventorySlotWidget;
class UCatInventoryWidget;
class UEnhancedInputComponent;
class UInputAction;
struct FCatInventoryEntry;

/** 本地库存页面与唯一操作菜单的控制器；格子提供来源，控制器管理上下文、Tooltip生命周期及服务器请求，领域效果归物品实例。 */
UCLASS()
class CATFISHING_API UCatInventoryPageController : public UObject
{
	GENERATED_BODY()
public:
	/** 绑定本地 Controller 和默认背包页，并给背包注入角色库存；成功后安装库存按键。 */
	bool Bind(APlayerController* InController, UCatInventoryWidget* InView);

	/** 关闭页面并解除输入绑定；换 Pawn 或离开世界时调用，释放本控制器持有的页面。 */
	void Unbind();

	/** 切换普通背包窗口；从交互页面关闭后再次打开使用默认背包。 */
	void ToggleInventory();

	/** 用调用方指定的 WBP 显示一份库存；营地、鱼护和鱼缸共用此入口，创建失败返回 false。 */
	bool OpenInventory(UCatInventoryComponent* Inventory, TSubclassOf<UCatInventoryWidget> InventoryViewClass);

	/** 读取页面控制器维护的唯一窗口状态；调用方不得从 Widget 可见性、鼠标模式或库存 Model 再拼一套开关判断。 */
	bool IsInventoryOpen() const;

	/** Controller 输入组件就绪或替换后重新安装库存按键；安装前移除旧绑定。 */
	void RefreshInputBinding();

	/** 处理页面关闭意图；已经关闭时不重新打开。 */
	void RequestCloseInventoryFromWidget();

	/** 由任意通用库存格右键调用；控制器读取格子的当前条目建立唯一菜单上下文。 */
	void OpenInventoryContextMenu(UCatInventorySlotWidget* SourceSlot, const FVector2D& ScreenPosition);

	/** 由页面、Escape、来源换物或世界切换调用；关闭菜单但不直接恢复旧 Tooltip。 */
	void CancelInventoryContextMenu(bool bRestoreTooltipFromCurrentSlateHit = true);

	/** 返回唯一右键菜单是否持有有效上下文；库存页用它让 Escape 先取消菜单而非关闭页面。 */
	bool IsInventoryContextMenuOpen() const;

	/** 返回本控制器持有的菜单 View；Slate 专用输入监听只用它判定鼠标是否落在菜单几何内。 */
	UCatInventoryContextMenuWidget* GetInventoryContextMenu() const;

	/** 库存 Model 更新时重读菜单来源；实例未变则刷新数量和可用性，换物或来源失效才关闭。 */
	void RefreshInventoryContextMenuForInventory(UCatInventoryComponent* ChangedInventory);

private:
	/** 读取共享图鉴 Model，只在团队库存打开且追踪有效时显示左下角鱼卡。 */
	void RefreshTrackedFish();
	/** 当前团队库存打开期间显示的本人追踪鱼卡；关闭时移出视口并释放。 */
	UPROPERTY(Transient) TObjectPtr<class UCatFishCardWidget> TrackedFishCard;
	/** 本次团队库存打开时订阅的本地玩家图鉴 Model；关闭时从原对象解绑。 */
	UPROPERTY(Transient) TWeakObjectPtr<class UCatCollectionModel> TrackingModel;
	/** 图鉴变化监听的配对句柄；不建立第二份追踪状态。 */
	FDelegateHandle TrackingChangedHandle;

	/** 成对加入/移出视口并申请/释放模态输入；关闭交互页后释放它并切回默认背包引用。 */
	void SetInventoryOpen(bool bOpen);

	/** 绑定现有 IMC 中的库存 Action；与主菜单共享 Action 时避让，不创建额外映射。 */
	void InstallInventoryInput();

	/** 从原输入组件删除确切句柄并释放 Action 引用；重复解绑不产生副作用。 */
	void RemoveInventoryInput();

	/** 读取当前菜单来源并在同一帧重新校验库存、实例和动作可用性；通过后向服务器提交一次请求并关闭菜单。 */
	void SubmitInventoryContextAction(FGameplayTag Action, int32 Quantity);

	/** 菜单取消回调；它只清上下文，Tooltip 仍等待下一次 Slate 命中更新后再显示。 */
	void HandleInventoryContextMenuCancelled();

	/** 复用现有领域命令回执显示动作结果；只消费本菜单发出的 RequestId，避免污染售鱼等其它页面请求。 */
	void HandleInventoryActionCommandResult(const struct FCatDomainCommandResult& Result);

	/** 按固定库存、槽位和实例身份重新投影菜单行；调用前已经确认来源仍有效。 */
	void PresentInventoryContextMenuForStoredSource(const FCatInventoryEntry& Entry);

	/** 菜单存续期注册专用 Slate 输入预处理器；只侦听外部鼠标按下并返回 false，不夺走格子拖拽或右键事件。 */
	void InstallInventoryContextMenuInputProcessor();

	/** 菜单关闭、页面卸载或世界切换时撤销输入预处理器与待执行的 Slate 命中回调。 */
	void RemoveInventoryContextMenuSlateHooks();

	/** 菜单关闭后登记一次 PostTick 命中查询；只沿当前 Slate 路径寻找 UCatInventorySlotWidget，不扫描全部库存。 */
	void ScheduleTooltipRestoreFromSlateHit();

	/** 本窗口所属本地玩家 Controller；按键安装与输入恢复都针对它。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> BoundPlayerController;

	/** 当前页面；默认背包或本次交互创建的页面，控制器持有到关闭/解绑。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatInventoryWidget> BoundView;

	/** 常规背包页面；由 LocalPlayer UI 持有，关闭交互页后重新使用。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatInventoryWidget> DefaultInventoryView;

	/** 已安装的库存开关 Action；保持引用直到解除输入绑定。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> AppliedInventoryToggleAction;

	/** Action 实际绑定到的输入组件；替换 Controller 时从这里移除原句柄。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UEnhancedInputComponent> BoundInventoryInputComponent;

	/** 库存 Action 的绑定句柄；0 表示尚未绑定，安装写入、解绑清零。 */
	uint32 InventoryInputBindingHandle = 0;

	/** 本控制器唯一的窗口打开状态；切换写入，关闭键与菜单读取。 */
	bool bInventoryOpen = false;

	/** 本页面申请模态输入时的恢复记录；关闭时只释放本页面持有的输入锁。 */
	FCatUIModalInputModeState ModalInputModeState;

	/** 当前本地玩家唯一的库存右键菜单；按不同库存和格子复用，不让页面或物品类型各自创建菜单。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatInventoryContextMenuWidget> InventoryContextMenu;

	/** 菜单来源的正式库存；打开时冻结，刷新与提交都从这份权威读模型重读。 */
	TWeakObjectPtr<UCatInventoryComponent> ContextMenuSourceInventory;

	/** 菜单来源在正式库存中的下标；它必须和固定实例身份一起匹配，不能单独定位操作对象。 */
	int32 ContextMenuSourceSlotIndex = INDEX_NONE;

	/** 菜单打开时冻结的物品实例身份；槽位换物后即使同定义也必须拒绝或关闭。 */
	FGuid ContextMenuItemInstanceId;

	/** 菜单打开时的屏幕位置；同一实例数量或状态变更后按此位置刷新，不把焦点跳回旧格。 */
	FVector2D ContextMenuScreenPosition = FVector2D::ZeroVector;

	/** 本菜单发出的未回执请求；只用于把已有服务器回执投影到当前库存页，不是库存事实。 */
	FGuid PendingInventoryActionRequestId;

	/** 本控制器在 PlayerController 领域回执广播上的订阅句柄；Bind/Unbind 成对管理。 */
	FDelegateHandle InventoryActionResultHandle;

	/** 菜单存续期的外部点击监听器；弱引用控制器，关闭时必须从 Slate 注销。 */
	TSharedPtr<class FCatInventoryContextMenuInputProcessor> InventoryContextMenuInputProcessor;

	/** 一次性 Slate PostTick 句柄；关闭后用当前命中路径恢复 Tooltip，执行或 teardown 时移除。 */
	FDelegateHandle TooltipRestorePostTickHandle;
};
