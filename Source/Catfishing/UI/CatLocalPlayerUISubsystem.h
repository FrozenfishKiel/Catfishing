#pragma once

#include "CoreMinimal.h"
#include "Online/CatOnlineTypes.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "CatLocalPlayerUISubsystem.generated.h"

class APlayerController;
class APawn;
class ACatCampInventoryActor;
class ACatCharacter;
class UCatHUDModel;
class UCatHUDWidget;
class UCatContainerReplicationComponent;
class UCatCampInventoryWidget;
class UCatInteractionPageController;
class UCatInteractionPromptWidget;
class UCatInventoryModel;
class UCatInventoryPageController;
class UCatInventoryWidget;
class UCatTravelWidget;
enum class ECatHUDAction : uint8;

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
	/** 订阅 GameInstance Online 快照，绑定当前本地 Controller，并按当前 Pawn 尝试装配本地玩家 UI 模块。 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** 先移除本地玩家 UI 模块与 Controller 绑定，再移除 Online View 和快照订阅，保证 LocalPlayer 销毁后没有迟到 UI 更新。 */
	virtual void Deinitialize() override;

	/** Controller 替换时先从旧 Controller 恢复并清理 UI，再弱绑定新 Controller 并装配其当前 Pawn。 */
	virtual void PlayerControllerChanged(APlayerController* NewController) override;

	/** 切换当前 LocalPlayer 的背包页面；实际输入模式、焦点和鼠标由 Inventory PageController 管理。 */
	void ToggleInventory();

	/** 打开带外部容器上下文的背包；交互对象只提供只读容器复制源，跨容器移动仍由背包 Drop 和服务器裁决。 */
	void OpenInventoryWithExternalContainerContexts(const TArray<UCatContainerReplicationComponent*>& ExternalContainers);

	/** 用交互对象指定的库存页面打开外部容器；LocalPlayer 不理解箱子类型，只负责把页面请求交给库存控制器并返回打开结果。 */
	bool OpenInventoryWithExternalContainerContextsUsingViewClass(
		const TArray<UCatContainerReplicationComponent*>& ExternalContainers,
		TSubclassOf<UCatInventoryWidget> InventoryViewClass);

	/** 打开营地公共仓库；公共仓库是团队共享箱子，页面类必须由仓库 Actor 提供，取用由库存 PageController 提交服务器。 */
	bool OpenCampInventory(ACatCampInventoryActor* CampInventory,
		TSubclassOf<UCatCampInventoryWidget> InventoryViewClass);

	/** 查询背包 PageController 打开态；没有已装配页面时返回 false，避免旧 Widget 引用影响输入切换判断。 */
	bool IsInventoryOpen() const;

	/** 返回当前 LocalPlayer 的库存 Model；库存 WBP 构建时用它订阅 ViewState，调用方不得通过它写玩法状态。 */
	UCatInventoryModel* GetInventoryModel() const;

	/** 返回当前 LocalPlayer 的库存 PageController；库存 WBP 用它提交玩家意图，刷新仍由 WBP 自己完成。 */
	UCatInventoryPageController* GetInventoryPageController() const;

	/** owning client 的 PlayerController 在 Pawn 或输入链就绪后调用；子系统据此重新对齐本地 HUD、背包和交互提示。 */
	void RefreshPlayerLakeUIForController(APlayerController* Controller);

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

	/** 弱绑定当前 LocalPlayer Controller，并立即尝试装配其当前 Pawn；后续 Pawn 就绪通知由项目 PlayerController 主动转交。 */
	void BindController(APlayerController* Controller);

	/** 清理旧 Controller 弱引用；Controller 已销毁时不延长其生命周期。 */
	void UnbindController();

	/** 当前 Controller Pawn 变化入口；同 Pawn 刷新库存读模型和输入绑定，换 Pawn 或空 Pawn 才拆装本地玩家 UI 模块。 */
	void HandleControllerPawnChanged(APawn* NewPawn);

	/** 当配置 WBP、当前 Controller 与 Character 有效时创建 HUD、Inventory 和 Interaction 三个本地玩家模块。 */
	void AttachPlayerLakeUI(ACatCharacter* Character);

	/** 先解绑各模块 PageController/Model，再移除 View，最后清理所有本地玩家 UI 引用。 */
	void DetachPlayerLakeUI();

	/** HUD Model 投影变化入口；只把最新状态交给 HUD WBP，不访问背包或商店。 */
	void HandleHUDModelViewStateChanged();

	/** HUD 入口动作入口；背包交给既有库存控制器，菜单只保留给蓝图或未来页面控制器。 */
	void HandleHUDActionRequested(ECatHUDAction Action);

	/** 当前 LocalPlayer 唯一 Frontend/旅行白盒界面；子系统拥有，Controller 变化或销毁时释放。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatTravelWidget> OnlineWidget;

	/** 当前 LocalPlayer 的主 HUD WBP；常驻天数、背包和设置入口，调试文字只有显式开启时才露出。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatHUDWidget> HUDWidget;

	/** 当前 LocalPlayer 的主 HUD Model；它聚合天数、入口显隐和可选调试事实，不保存玩法真相。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatHUDModel> HUDModel;

	/** 当前 LocalPlayer 的背包主 WBP；普通打开展示个人资源，交互打开可追加外部容器上下文。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatInventoryWidget> InventoryWidget;

	/** 当前 LocalPlayer 的库存 Model；它只读随身库存、本次交互外部容器、当前选择和动作结果。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatInventoryModel> InventoryModel;

	/** 当前 LocalPlayer 的背包 PageController；它管理背包输入、外部容器打开和玩家库存操作转交。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatInventoryPageController> InventoryPageController;

	/** 当前 LocalPlayer 的交互提示 WBP；只显示靠近对象提示，不打开具体对象 UI。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatInteractionPromptWidget> InteractionPromptWidget;

	/** 当前 LocalPlayer 的交互提示和确认键控制器；它扫描通用交互目标，不预建任何对象 UI。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatInteractionPageController> InteractionPageController;

	/** 当前 LocalPlayer 对应的 Controller 弱引用；Controller/World 替换时先解绑，绝不成为跨 World 所有者。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> BoundPlayerController;

	/** 当前本地 UI 已经成功装配的猫身体；用于识别重复 SetPawn/输入刷新，不把同一个 Pawn 的 UI 反复拆装。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ACatCharacter> AttachedPlayerLakeCharacter;

	/** Online 快照广播的配对解绑句柄；Initialize 写入，Deinitialize 消费。 */
	FDelegateHandle OnlineSnapshotHandle;

	/** Frontend TravelWidget 动作广播的配对解绑句柄；创建时写入，移除时消费。 */
	FDelegateHandle ActionHandle;

	/** HUD Model 变化广播的配对解绑句柄；AttachPlayerLakeUI 写入，DetachPlayerLakeUI 消费。 */
	FDelegateHandle HUDModelViewChangedHandle;

	/** HUD 入口动作广播的配对解绑句柄；AttachPlayerLakeUI 写入，DetachPlayerLakeUI 消费。 */
	FDelegateHandle HUDActionHandle;

};
