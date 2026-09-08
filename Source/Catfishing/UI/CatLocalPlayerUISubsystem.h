#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "CatLocalPlayerUISubsystem.generated.h"

class APlayerController;
class APawn;
class ACatCampInventoryActor;
class ACatCharacter;
class UCatHUDModel;
class UCatHUDWidget;
class UCatFrontendPageController;
class UCatFrontendRootWidget;
class UCatFrontendRoomModel;
class UCatFrontendSaveModel;
class UCatFrontendSettingsModel;
class UCatContainerReplicationComponent;
class UCatCampInventoryWidget;
class UCatInteractionPageController;
class UCatInteractionPromptWidget;
class UCatInventoryModel;
class UCatInventoryPageController;
class UCatInventoryWidget;
class UCatLakeMainMenuController;
class UCatLakeMainMenuWidget;
class UUserWidget;
enum class ECatHUDAction : uint8;
struct FCatOnlineSnapshot;

/** 每个 LocalPlayer 的 UI 生命周期协调器；只装配本地玩家拥有的 HUD、背包和交互提示，不预建商店或聚合业务页面。 */
UCLASS()
class CATFISHING_API UCatLocalPlayerUISubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

#if WITH_DEV_AUTOMATION_TESTS
	friend class FCatLocalPlayerUISubsystemSplitPlayerModulesAttachTest;
#endif

public:
	/** 订阅 GameInstance Online 快照，绑定当前本地 Controller，并按当前 Pawn 尝试装配本地玩家 UI 模块。 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** 先移除本地玩家 UI 模块与 Controller 绑定，再移除 Frontend Root 和快照订阅，保证 LocalPlayer 销毁后没有迟到 UI 更新。 */
	virtual void Deinitialize() override;

	/** Controller 替换时先判断 Frontend Root 是否需要跨空窗保留；完成后旧局内 UI 已清理，新 Controller 已重新绑定并恢复必要焦点。 */
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
	/** 响应 Online 事实变更；实现按当前 World 调和 Frontend Root，并刷新局内 HUD 的只读投影。 */
	void HandleOnlineSnapshotChanged();

	/** 根据当前本地 Controller、World 和 Online 快照调和 Frontend Root；完成后要么存在唯一有效 Root，要么已拆除失效前端。 */
	void RefreshFrontendForCurrentController();

	/** 根据 Online 快照刷新全局加载遮罩；Start 和 Leave 等待期显示最高层遮罩，并把模型层地图进度传给 WBP。 */
	void RefreshGlobalLoadingScreen(const FCatOnlineSnapshot& Snapshot);

	/** 从 Online 快照判断全局遮罩是否需要显示，并输出玩家可读阶段文本；它只消费事实，不发起保存、Session 或旅行。 */
	bool ShouldShowGlobalLoadingScreen(const FCatOnlineSnapshot& Snapshot, FText& OutStatusText) const;

	/** 创建或复用全局加载遮罩并写入阶段和真实进度；遮罩复用正式 Loading WBP 资产，但不再属于 Frontend Root 子页。 */
	void ShowGlobalLoadingScreen(const FCatOnlineSnapshot& Snapshot, const FText& StatusText);

	/** 移除全局加载遮罩并清空最后阶段文本；它不改变 Online 操作，只释放本地 UMG 表现。 */
	void HideGlobalLoadingScreen();

	/** 将当前阶段文本和 Online 快照里的地图加载百分比写入全局 Loading WBP；缺百分比时只显示等待，不由 View 伪造进度。 */
	void RefreshGlobalLoadingScreenPresentation(const FCatOnlineSnapshot& Snapshot, const FText& StatusText);

	/** 判断已有 Frontend Root 是否处于 Start 失败恢复保护窗；返回值只授权保留旧 Root，不授权在非 Frontend World 新建 Root。 */
	bool ShouldKeepExistingFrontendRoot(const FCatOnlineSnapshot& Snapshot) const;

	/** 先成对 Shutdown Frontend Controller 和三个 Model，再从视口移除 Root；调用时若仍有绑定 Controller，才恢复前端鼠标状态。 */
	void RemoveFrontendRoot();

	/** 弱绑定当前 LocalPlayer Controller，并立即尝试装配其当前 Pawn；后续 Pawn 就绪通知由项目 PlayerController 主动转交。 */
	void BindController(APlayerController* Controller);

	/** 清理旧 Controller 弱引用；Controller 已销毁时不延长其生命周期。 */
	void UnbindController();

	/** 当前 Controller Pawn 变化入口；同 Pawn 刷新库存读模型和输入绑定，换 Pawn 或空 Pawn 才拆装本地玩家 UI 模块。 */
	void HandleControllerPawnChanged(APawn* NewPawn);

	/** 当配置 WBP、当前 Controller 与 Character 有效时创建 HUD、Inventory、Interaction 和局内菜单模块。 */
	void AttachPlayerLakeUI(ACatCharacter* Character);

	/** 先解绑各模块 PageController/Model，再移除 View，最后清理所有本地玩家 UI 引用。 */
	void DetachPlayerLakeUI();

	/** HUD Model 投影变化入口；只把最新状态交给 HUD WBP，不访问背包或商店。 */
	void HandleHUDModelViewStateChanged();

	/** 切换局内主菜单；打开菜单前会关闭已打开的背包，避免两个模态输入层同时争抢焦点。 */
	void ToggleLakeMainMenu();

	/** HUD 入口动作入口；背包和局内菜单都转交各自控制器，HUD 不创建或持有业务页面。 */
	void HandleHUDActionRequested(ECatHUDAction Action);

	/** 当前 LocalPlayer 的正式 Frontend 根 WBP，代表进入玩法前的顶层主界面；只在 Frontend World 创建，加载表现交给全局遮罩。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatFrontendRootWidget> FrontendRootWidget;

	/** 当前 LocalPlayer 的全局加载遮罩实例；Start/Leave 等待期加到最高层，空闲或错误时立即从视口移除。 */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> GlobalLoadingScreenWidget;

	/** 最近一次写入全局加载遮罩的阶段文本；只用于重复刷新去抖和日志，不作为 Online 状态来源。 */
	FText LastGlobalLoadingStatusText;

	/** 当前 LocalPlayer 的 Frontend 流程协调器；它只持有流程、确认槽位和命令等待事实，Root 按明确意图调用它。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatFrontendPageController> FrontendPageController;

	/** 当前 LocalPlayer 的存档列表 Model；它只读正式 Save 子系统，槽位、busy 和错误不在 UI 子系统复制。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatFrontendSaveModel> FrontendSaveModel;

	/** 当前 LocalPlayer 的房间 Model；它只读 Online 快照并转交产品意图，UI 子系统不持有好友或邀请码。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatFrontendRoomModel> FrontendRoomModel;

	/** 当前 LocalPlayer 的设置 Model；它维护前端草稿并在 Apply 时接正式设置来源，UI 子系统不复制字段。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatFrontendSettingsModel> FrontendSettingsModel;

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

	/** 当前 LocalPlayer 的局内主菜单 WBP；它只展示设置、保存和退出入口，不持有 Save 或 Online 系统。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatLakeMainMenuWidget> LakeMainMenuWidget;

	/** 当前 LocalPlayer 的局内主菜单 Controller；它管理 ESC 输入、模态焦点和保存/退出意图转交。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatLakeMainMenuController> LakeMainMenuController;

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

	/** Online 快照广播的配对解绑句柄；Initialize 写入，Deinitialize 消费，用于 Frontend 生命周期调和与 HUD 刷新。 */
	FDelegateHandle OnlineSnapshotHandle;

	/** HUD Model 变化广播的配对解绑句柄；AttachPlayerLakeUI 写入，DetachPlayerLakeUI 消费。 */
	FDelegateHandle HUDModelViewChangedHandle;

	/** HUD 入口动作广播的配对解绑句柄；AttachPlayerLakeUI 写入，DetachPlayerLakeUI 消费。 */
	FDelegateHandle HUDActionHandle;

};
