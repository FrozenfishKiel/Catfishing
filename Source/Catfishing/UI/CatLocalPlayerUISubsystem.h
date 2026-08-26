#pragma once

#include "CoreMinimal.h"
#include "Online/CatOnlineTypes.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "CatLocalPlayerUISubsystem.generated.h"

class APlayerController;
class APawn;
class ACatCharacter;
class UCatHUDModel;
class UCatHUDWidget;
class UCatContainerReplicationComponent;
class UCatInteractionPageController;
class UCatInteractionPromptWidget;
class UCatInventoryModel;
class UCatInventoryPageController;
class UCatInventoryWidget;
class UCatTravelWidget;
class UCatInteractionWidget;
class UCatInteractionTargetingComponent;

/** 每个 LocalPlayer 的 UI 生命周期协调器；只装配本地玩家拥有的 HUD、背包和交互提示，不预建商店或聚合业务页面。 */
UCLASS()
class CATFISHING_API UCatLocalPlayerUISubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

#if WITH_DEV_AUTOMATION_TESTS
	friend class FCatLocalPlayerUISubsystemSplitPlayerModulesAttachTest;
	friend class FCatLocalPlayerUISubsystemOnlineWidgetPolicyTest;
#endif

public:
	/** 订阅 GameInstance Online 快照，绑定当前 Controller Pawn notifier，并按当前 Pawn 尝试装配本地玩家 UI 模块。 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** 先移除本地玩家 UI 模块与 Controller 绑定，再移除 Online View 和快照订阅，保证 LocalPlayer 销毁后没有迟到 UI 更新。 */
	virtual void Deinitialize() override;

	/** Controller 替换时先从旧 Controller 恢复并清理 UI，再弱绑定新 Controller 并装配其当前 Pawn。 */
	virtual void PlayerControllerChanged(APlayerController* NewController) override;

	/** 切换当前 LocalPlayer 的背包页面；实际输入模式、焦点和鼠标由 Inventory PageController 管理。 */
	void ToggleInventory();

	/** 打开带外部容器上下文的背包；交互对象只提供只读容器复制源，跨容器移动仍由背包 Drop 和服务器裁决。 */
	void OpenInventoryWithExternalContainerContexts(const TArray<UCatContainerReplicationComponent*>& ExternalContainers);

	/** 查询背包 PageController 打开态；没有已装配页面时返回 false，避免旧 Widget 引用影响输入切换判断。 */
	bool IsInventoryOpen() const;

private:
	/** 响应 Online 事实变更；实现重新读取完整 Snapshot，并同步 Frontend 面板与本地玩家 UI Model。 */
	void HandleOnlineSnapshotChanged();

	/** 把 Frontend Travel View 的 Host/Find/Join/Invite/Leave 意图翻译为 Online 公共接口调用。 */
	void HandleActionRequested(ECatOnlineUIAction Action, FGuid OpaqueHandle);

	/** 根据当前 Controller 与完整 Online 快照调和唯一 TravelWidget；只有 Frontend 或进入 Lake 前的旅行态会创建或刷新。 */
	void RefreshOnlineWidgetForCurrentController();

	/** 判断 Frontend 旅行面板是否仍属于当前屏幕；Lake、回 Frontend 途中和异常 World 都不显示 Host/Find/Join 白盒。 */
	static bool ShouldShowOnlineTravelWidget(const FCatOnlineSnapshot& Snapshot);

	/** 成对解除 Frontend View 动作广播并移出视口；空实例和重复调用保持幂等。 */
	void RemoveOnlineWidget();

	/** 弱绑定 Controller 的 GetOnNewPawnNotifier，并立即尝试装配其当前 Pawn；不跨 World 强持 Controller。 */
	void BindController(APlayerController* Controller);

	/** 从旧 Controller 精确移除 Pawn notifier 并清弱引用；Controller 已销毁时只清本地句柄。 */
	void UnbindController();

	/** 当前 Controller Pawn 变化入口；先完整解绑旧玩家 UI 模块，再尝试把 NewPawn 作为新的 ACatCharacter 只读来源。 */
	void HandleControllerPawnChanged(APawn* NewPawn);

	/** 当配置 WBP、当前 Controller 与 Character 有效时创建 HUD、Inventory 和 Interaction 三个独立模块。 */
	void AttachPlayerLakeUI(ACatCharacter* Character);

	/** 先解绑各模块 PageController/Model，再移除 View，最后清理所有本地玩家 UI 引用。 */
	void DetachPlayerLakeUI();

	/** 为当前本地 Character 创建准星/高亮/拾取提示层，并订阅 Controller 的视线目标变化。 */
	void AttachInteractionView(APlayerController* Controller, ACatCharacter* Character);

	/** 从本地视线 TargetingComponent 成对解绑并移除拾取提示层。 */
	void DetachInteractionView();

	/** 视线目标变化时只刷新本地提示，不在 UI 层提交权威命令。 */
	void HandleInteractionTargetChanged(AActor* PreviousTarget, AActor* CurrentTarget);

	/** HUD Model 投影变化入口；只把最新状态交给 HUD WBP，不访问背包或商店。 */
	void HandleHUDModelViewStateChanged();

	/** 当前 LocalPlayer 唯一 Frontend/旅行白盒界面；子系统拥有，Controller 变化或销毁时释放。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatTravelWidget> OnlineWidget;

	/** 当前 LocalPlayer 的状态 HUD WBP；只显示猫状态和钓鱼反馈。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatHUDWidget> HUDWidget;

	/** 当前 LocalPlayer 的状态 HUD Model；它只读 Character 状态和 Fishing 反馈。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatHUDModel> HUDModel;

	/** 当前 LocalPlayer 的背包主 WBP；普通打开展示个人资源，交互打开可追加外部容器上下文。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatInventoryWidget> InventoryWidget;

	/** 当前 LocalPlayer 的背包 Model；它只读鱼护、外部容器、装备、待取装备和动作结果。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatInventoryModel> InventoryModel;

	/** 当前 LocalPlayer 的背包 PageController；它管理背包输入、外部容器打开和 View 意图转发。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatInventoryPageController> InventoryPageController;

	/** 当前 LocalPlayer 的交互提示 WBP；只显示靠近对象提示，不打开具体对象 UI。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatInteractionPromptWidget> InteractionPromptWidget;

	/** 当前 LocalPlayer 的交互提示和确认键控制器；它扫描通用交互目标，不预建任何对象 UI。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatInteractionPageController> InteractionPageController;

	/** 本地准星和视线交互提示层；负责已上岸鱼的描边与拾取反馈。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatInteractionWidget> InteractionWidget;

	/** 本地提示当前订阅的视线 TargetingComponent，用于精确解绑。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatInteractionTargetingComponent> BoundInteractionTargeting;
	/** 当前绑定 Pawn notifier 的 Controller 弱引用；Controller/World 替换时先解绑，绝不成为跨 World 所有者。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> BoundPlayerController;

	/** Online 快照广播的配对解绑句柄；Initialize 写入，Deinitialize 消费。 */
	FDelegateHandle OnlineSnapshotHandle;

	/** Frontend TravelWidget 动作广播的配对解绑句柄；创建时写入，移除时消费。 */
	FDelegateHandle ActionHandle;

	/** HUD Model 变化广播的配对解绑句柄；AttachPlayerLakeUI 写入，DetachPlayerLakeUI 消费。 */
	FDelegateHandle HUDModelViewChangedHandle;

	/** 当前 Controller Pawn notifier 的配对解绑句柄；Controller 变化或 Deinitialize 时消费。 */
	FDelegateHandle PawnChangedHandle;

	/** 视线交互目标变化通知的配对解绑句柄。 */
	FDelegateHandle InteractionTargetChangedHandle;
};
