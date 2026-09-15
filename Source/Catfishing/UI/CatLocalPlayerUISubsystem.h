#pragma once

#include "CoreMinimal.h"
#include "Online/CatOnlineTypes.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "CatLocalPlayerUISubsystem.generated.h"

class APlayerController;
class APawn;
class ACatCharacter;
class ACatfishingGameState;
class UCatCollectionPageController;
class UCatCollectionWidget;
class UCatFishRevealWidget;
class UCatHUDModel;
class UCatHUDWidget;
class UCatItemTooltipController;
class UCatItemTooltipWidget;
class UCatFrontendPageController;
class UCatFrontendRootWidget;
class UCatFrontendRoomModel;
class UCatFrontendSaveModel;
class UCatFrontendSettingsModel;
class UCatInteractionPageController;
class UCatInteractionPromptWidget;
class UCatInventoryComponent;
class UCatInventoryPageController;
class UCatInventoryWidget;
class UCatInventoryQuickbarWidget;
class UCatLakeMainMenuController;
class UCatLakeMainMenuWidget;
class UCatAltarConfirmationWidget;
class UUserWidget;
class UCatDayTransitionWidget;
class UCatWorldInfoController;
struct FCatAltarConfirmationSnapshot;
struct FCatRunDayTransition;
enum class ECatHUDAction : uint8;

/** 每个 LocalPlayer 的 UI 生命周期协调器；只装配本地玩家拥有的 HUD、背包、物品提示、交互提示和个人图鉴，不预建商店或聚合业务页面。 */
UCLASS()
class CATFISHING_API UCatLocalPlayerUISubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

#if WITH_DEV_AUTOMATION_TESTS
	friend class FCatLocalPlayerUISubsystemSplitPlayerModulesAttachTest;
	friend class FCatLoadingReadinessLatentCommand;
	friend class FCatPackagedTravelCommand;
#endif

public:
	/** 订阅 GameInstance Online 快照，绑定当前本地 Controller，并按当前 Pawn 尝试装配本地玩家 UI 模块。 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** 先移除本地玩家 UI 模块与 Controller 绑定，再移除 Frontend Root 和快照订阅，保证 LocalPlayer 销毁后没有迟到 UI 更新。 */
	virtual void Deinitialize() override;

	/** Controller 替换时先判断 Frontend Root 是否需要跨空窗保留；完成后失效局内 UI 已清理，新 Controller 已重新绑定并恢复必要焦点。 */
	virtual void PlayerControllerChanged(APlayerController* NewController) override;

	/** 切换当前 LocalPlayer 的背包页面；实际输入模式、焦点和鼠标由 Inventory PageController 管理。 */
	void ToggleInventory();

	/** 打开一份明确库存对应的 WBP；交互对象提供库存与页面类，本地 UI 返回真实打开结果。 */
	bool OpenInventory(UCatInventoryComponent* Inventory, TSubclassOf<UCatInventoryWidget> InventoryViewClass);

	/** 查询背包 PageController 打开态；没有已装配页面时返回 false，避免失效 Widget 引用影响输入切换判断。 */
	bool IsInventoryOpen() const;

	/** 返回当前 LocalPlayer 的库存窗口控制器；WBP 只用它关闭窗口，库存 Model 由各库存组件提供。 */
	UCatInventoryPageController* GetInventoryPageController() const;

	/** 返回当前本地玩家已装配的常驻快捷栏 View；自动化和调试只读它，背包数据仍由组件 Model 持有。 */
	UCatInventoryQuickbarWidget* GetInventoryQuickbarWidget() const;

	/** 切换当前 LocalPlayer 的个人图鉴页；HUD 猫爪印、图鉴按键和局内派对菜单三个入口都经这里，不各自创建页面。 */
	void ToggleCollection();

	/** 返回当前 LocalPlayer 的图鉴页面控制器；图鉴 WBP 只用它关闭窗口，记录仍由 Collection Model 单向推送。 */
	UCatCollectionPageController* GetCollectionPageController() const;

	/**
	 * 本机此刻是否开着背包、图鉴或局内菜单三页之一。
	 * 它是「打开界面」这件事的唯一事实源：HUD 读它决定世界进度那一位露不露面（交互册 §42），
	 * 三页共用一层模态输入锁，所以任一开着都算开着。
	 */
	bool IsAnyPlayerPageOpen() const;

	/** 返回此玩家已经装配的唯一悬停控制器；库存格只提交显示意图，不创建各自的 Tooltip。 */
	UCatItemTooltipController* GetItemTooltipController() const;

	/** owning client 的 PlayerController 在 Pawn 或输入链就绪后调用；子系统据此重新对齐本地 HUD、背包和交互提示。 */
	void RefreshPlayerLakeUIForController(APlayerController* Controller);

	/** Controller 传入公开快照与同步服务器秒数；关闭操作页面并管理独立翻天 UI，不改变 Online loading 或 Run。 */
	void RefreshDayTransition(APlayerController* Controller, const FCatRunDayTransition& Transition, double ServerTimeSeconds);

	/** Controller 退出、旅行或 LocalPlayer 换绑时移除翻天 UI 和失败停留记忆；不释放其他功能的锁。 */
	void ClearDayTransition();

	/** 消费服务器公开的祭坛确认快照并创建或收起顶部确认窗口；等待确认不关闭页面、不改输入模式，取消原因只停留两秒。 */
	void RefreshAltarConfirmation(APlayerController* Controller, const FCatAltarConfirmationSnapshot& Confirmation);

	/** Controller 退出、旅行或 LocalPlayer 换绑时移除祭坛确认窗口与本机取消反馈记忆；不改变 GameMode 的确认请求。 */
	void ClearAltarConfirmation();

private:
	/** 本地玩家独有的对象信息显示控制器；局内 UI 装配时绑定，换 Pawn、Controller 或旅行时成对解绑。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatWorldInfoController> WorldInfoController;

	/** 本玩家的正式翻天 WBP；RefreshDayTransition 创建和渲染，ClearDayTransition 或正常结束移除。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatDayTransitionWidget> DayTransitionWidget;

	/** 本玩家的正式祭坛确认 WBP；Waiting 或取消反馈期间由公开快照创建，Accepted/Idle/旅行时移除。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatAltarConfirmationWidget> AltarConfirmationWidget;

	/** 最近因正式祭坛确认 WBP 加载或创建失败而停止尝试的请求标识；同一请求不重复同步加载和刷日志，旅行清空。 */
	FGuid UnavailableAltarConfirmationViewId;

	/** 已开启取消原因反馈的请求标识；Cancelled 首次到达时写入，后续帧只保留同一窗口而不重置两秒时长。 */
	FGuid LastAltarConfirmationCancellationId;

	/** 取消原因允许保留在本机视图中的截止单调时间，单位秒；只控制表现停留，不参与服务器确认超时。 */
	double AltarConfirmationCancellationUntilSeconds = 0.0;

	/** 最近因正式 WBP 加载或创建失败而停止尝试的请求标识；RefreshDayTransition 写入并比较，ClearDayTransition 清空，同一请求不重复加载和刷日志。 */
	FGuid UnavailableDayTransitionViewId;

	/** 已展示过失败反馈的请求键；刷新时写入，避免同一失败快照每帧重开两秒提示，旅行清空。 */
	FGuid LastDayTransitionFailureId;

	/** 失败提示消失的本机单调时间，单位秒；首次收到失败时写入，刷新读取，不参与服务器锁或日计时。 */
	double DayTransitionFailureUntilSeconds = 0.0;

	/** 全局 Loading WBP 的一次渲染快照；它把“正在等什么”和“是否显示进度”分开，避免 View 自行编造加载状态。 */
	struct FCatGlobalLoadingPresentation
	{
		/** 遮罩正在承载的高层目标，例如进入游戏或返回主菜单；由 LocalPlayer UI 根据当前 Start/Leave 过渡写入。 */
		FText HeadingText;

		/** 玩家此刻真正等待的当前步骤；它来自 Online、引擎 WorldContext 或本地 UI 就绪事实，会直接显示在主状态行。 */
		FText StatusText;

		/** 当前步骤的补充说明；用于说明正在等待包回调、旅行确认、BeginPlay 或某个 UI 模块就绪。 */
		FText DetailText;

		/** 当前等待原因的工程侧锚点；它让开发包里能看到卡住的是 PreLoadMap、PostLoadMap、DestroySession 还是 UI 装配。 */
		FText ReasonText;

		/** 是否显示 Loading WBP 里的条形控件；进入游戏展示真实 gate 合成总进度，返回主菜单固定为 false。 */
		bool bShowProgressBar = false;

		/** 进度值是否允许对外显示成百分号；进入游戏使用真实 gate 合成值，退出主菜单不显示百分号。 */
		bool bHasProgressPercent = false;

		/** 遮罩条形控件使用的百分制总进度；只由 Start 受理、玩法软资源预热、地图包加载、Travel、World、BeginPlay、Transport 和本地 UI 的真实完成事实累加。 */
		float ProgressPercent = 0.0f;

		/**
		 * 本机是否拿到了一份可展示的本局摘要。
		 * 来源只有一个：本机 Save 子系统当前活动槽的摘要——也就是「存档记录的那一天」，不是本局实时天数。
		 * 客户端没有活动槽（世界槽在房主那边），所以这里为 false，遮罩上写「房主尚未提供」而不是编一个 0。
		 */
		bool bHasRunSummary = false;

		/** 存档记录的天数；bHasRunSummary 为 false 时不得显示。 */
		int32 RunSummaryDayIndex = 0;

		/** 存档记录的世界进度（原参考稿的「献祭进度」已作废，改称世界进度，主界面.md:71）。 */
		int32 RunSummaryWorldProgress = 0;

		/** 存档记录的当日任务点数；0 表示那份存档没记到目标。 */
		int32 RunSummaryDailyOfferingTarget = 0;

		/** 存档记录的缸内可献点数；INDEX_NONE 表示那份存档没记过这个量。 */
		int32 RunSummaryTankOfferingPoints = INDEX_NONE;
	};

	/** 响应 Online 事实变更；实现按当前 World 调和 Frontend Root，并刷新局内 HUD 的只读投影。 */
	void HandleOnlineSnapshotChanged();

	/** 根据当前本地 Controller、World 和 Online 快照调和 Frontend Root；完成后要么存在唯一有效 Root，要么已拆除失效前端。 */
	void RefreshFrontendForCurrentController();

	/** 根据 Online、World 与本地 UI 事实刷新遮罩；新请求或就绪回退取消旧完成展示，真实就绪后只安排一次撤罩。 */
	void RefreshGlobalLoadingScreen(const FCatOnlineSnapshot& Snapshot);

	/** 从当前 Online 子系统重读事实并刷新遮罩；事件和遮罩存活期的 Slate 观察共用此入口，完成与否仍由实际就绪条件决定。 */
	void RefreshGlobalLoadingScreenFromCurrentSnapshot();

	/**
	 * 把本机 Save 子系统当前活动槽的摘要填进遮罩表现（加载页三个量：世界进度、当日任务点数、缸内可献点数）。
	 * 它读的是「存档记录的那一天」，不是本局实时天数——加载期玩法 World 还没起来，没有实时事实可读。
	 * 客户端没有活动槽，填不出来就保持 bHasRunSummary=false，由写入侧显示「房主尚未提供」。
	 */
	void FillRunSummaryIntoLoadingPresentation(FCatGlobalLoadingPresentation& OutPresentation) const;

	/** 从 Online、Lyra 式引擎 gate 与本地 UI 就绪事实生成遮罩表现；进入游戏会同步给出真实 gate 合成总进度。 */
	bool ShouldShowGlobalLoadingScreen(const FCatOnlineSnapshot& Snapshot, FCatGlobalLoadingPresentation& OutPresentation) const;

	/** 创建或复用正式 Loading WBP 并观察其存活期间的就绪变化；重复调用复用同一个 Slate 订阅，不依赖后续 Online 广播。 */
	void ShowGlobalLoadingScreen(const FCatGlobalLoadingPresentation& Presentation);

	/** 移除遮罩并成对停止 Slate 观察，清空阶段文本和过渡记忆；只释放本地表现，不改变 Online 操作。 */
	void HideGlobalLoadingScreen();

	/** 请求在完成态短暂停留后移除全局遮罩；Start/Leave 已完成但玩家还需要看清 100% 或完成状态时调用。 */
	void RequestGlobalLoadingDismissalAfterPresentation(ECatOnlineOperation CompletedOperation);

	/** 遮罩存活期间重读 World、Online 和本地 UI 就绪条件；真实完成后才检查完成文案停留时长并撤罩。 */
	void HandleGlobalLoadingPostTick(float DeltaTime);

	/** 清空完成文案的待撤罩状态；重新等待时仍保留 Slate 观察，只有隐藏遮罩才移除订阅。 */
	void ResetGlobalLoadingDismissal();

	/** 将真实等待阶段写入正式 WBP，阶段变化时留下关联日志；进入游戏显示合成进度，回主菜单折叠进度条。 */
	void RefreshGlobalLoadingScreenPresentation(const FCatGlobalLoadingPresentation& Presentation);

	/** 合成进入玩法的总进度；Start、玩法软资源预热、地图包、Travel、World、BeginPlay、Connected 和本地 UI 都必须来自真实 gate，不在显示层改写。 */
	float GetGameplayLoadingProgressPercent(const FCatOnlineSnapshot& Snapshot) const;

	/** 记录最新 Start/Leave 请求直到实际撤罩，错误时失效；完成展示期间仍能识别就绪回退，记忆本身不代表已完成。 */
	void TrackGlobalLoadingTransition(const FCatOnlineSnapshot& Snapshot);

	/** 判断进入玩法的等待是否可以收口；必须同时看到 Lake/Connected、引擎 World 运行和本地 HUD/菜单/交互 UI 装配完成。 */
	bool IsGameplayLoadingReadyToDismiss(const FCatOnlineSnapshot& Snapshot) const;

	/** 判断回主菜单的等待是否可以收口；必须同时看到 Frontend/Idle、引擎 World 运行和正式 Frontend Root 入视口。 */
	bool IsFrontendLoadingReadyToDismiss(const FCatOnlineSnapshot& Snapshot) const;

	/** 读取 Lyra/CommonLoadingScreen 同类的引擎等待原因；返回 true 时说明 WorldContext、LoadMap、连接或 BeginPlay 仍未完成。 */
	bool TryGetEngineLoadingReason(const FCatOnlineSnapshot& Snapshot, bool bReturningToFrontend,
		FText& OutStatusText, FText& OutDetailText, FText& OutReasonText) const;

	/** 判断已有 Frontend Root 是否处于 Start 失败恢复保护窗；返回值只授权保留保留中的 Root，不授权在非 Frontend World 新建 Root。 */
	bool ShouldKeepExistingFrontendRoot(const FCatOnlineSnapshot& Snapshot) const;

	/** 先成对 Shutdown Frontend Controller 和三个 Model，再从视口移除 Root；调用时若仍有绑定 Controller，才恢复前端鼠标状态。 */
	void RemoveFrontendRoot();

	/** 弱绑定当前 LocalPlayer Controller，并立即尝试装配其当前 Pawn；后续 Pawn 就绪通知由项目 PlayerController 主动转交。 */
	void BindController(APlayerController* Controller);

	/** 清理失效 Controller 弱引用；Controller 已销毁时不延长其生命周期。 */
	void UnbindController();

	/** 当前 Controller Pawn 变化入口；同 Pawn 刷新库存读模型和输入绑定，换 Pawn 或空 Pawn 才拆装本地玩家 UI 模块。 */
	void HandleControllerPawnChanged(APawn* NewPawn);

	/** 当配置 WBP、当前 Controller 与 Character 有效时装配 HUD、Inventory、物品提示、Interaction、局内菜单及本玩家的 WorldInfo 控制器。 */
	void AttachPlayerLakeUI(ACatCharacter* Character);

	/** 退出或换绑时先清理 WorldInfo，再成对解绑各模块控制器、Model 与视图，清除委托和当前角色引用。 */
	void DetachPlayerLakeUI();

	/** HUD Model 投影变化入口；只把最新状态交给 HUD WBP，不访问背包或商店。 */
	void HandleHUDModelViewStateChanged();

	/** 切换局内主菜单；打开菜单前会关闭已打开的背包，避免两个模态输入层同时争抢焦点。 */
	void ToggleLakeMainMenu();

	/** HUD 入口动作入口；背包和局内菜单都转交各自控制器，HUD 不创建或持有业务页面。 */
	void HandleHUDActionRequested(ECatHUDAction Action);

	/** 页面开合之后让 HUD 立刻重读一次投影；世界进度那一位的显隐只跟着这件事变，不另存一份开合状态。 */
	void RefreshHUDAfterPageVisibilityChanged();

	/**
	 * 本机玩家自己的图鉴首次记录到达时，在钓点附近弹一次鱼种特写（ui 表第 14 行、主界面.md:121-125）。
	 * 浮层 Widget 资产不在本轮范围：类没配置时只记录一次诊断，不创建原生白盒替身。
	 */
	void HandleLocalFishSpeciesFirstRecorded(FName FishDefinitionId, double WeightKilograms);

	/**
	 * 同房其他玩家解锁了新鱼种时给一条不打断操作的提示（主界面.md「当玩家解锁新鱼，他人视角」）。
	 * 本人那一条走 HandleLocalFishSpeciesFirstRecorded 的特写，不在这里重复。
	 */
	void HandleFishSpeciesDiscoveryAnnounced();

	/** 接上当前 World 的 GameState 新鱼种广播；换 Controller、换 World 时成对解绑。 */
	void RefreshFishDiscoveryBinding();

	/** 成对解除新鱼种广播与本机图鉴首记订阅；不清除已经记下的鱼种，只停止后续通知。 */
	void ClearFishDiscoveryBinding();

	/**
	 * 把正式 IMC 里解析到的背包开关键名落一条日志。
	 * ui 表第 6 行与主界面.md:95 都写「按 B 弹出背包、再按 B 关闭」，而键位本体在 IMC 资产里，
	 * 代码这一侧看不见——对表因此连着两轮只能判 ❓。这里只记录不断言：
	 * 键位改哪边仍要策划确认，程序不替它决定，也不为一个未裁的键把背包判死。
	 */
	void LogInventoryToggleKeyBinding();

	/** 当前 LocalPlayer 的正式 Frontend 根 WBP，代表进入玩法前的顶层主界面；只在 Frontend World 创建，加载表现交给全局遮罩。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatFrontendRootWidget> FrontendRootWidget;

	/** 当前 LocalPlayer 的全局加载遮罩实例；Start/Leave 等待期加到最高层，空闲或错误时立即从视口移除。 */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> GlobalLoadingScreenWidget;

	/** 最近一次写入全局加载遮罩的阶段文本；只用于重复刷新去抖和日志，不作为 Online 状态来源。 */
	FText LastGlobalLoadingStatusText;

	/** 当前全局遮罩正在跟随的 Online 过渡意图；它只区分 Start/Leave，完成与否仍由 Online/World/UI 事实共同裁决。 */
	ECatOnlineOperation GlobalLoadingOperation = ECatOnlineOperation::None;

	/** 当前全局遮罩跟随的请求关联键；新 Start/Leave 请求会覆盖它，日志和迟到 UI 刷新可据此识别同一段等待。 */
	FGuid GlobalLoadingRequestId;

	/** 是否已经安排在完成态停留后移除遮罩；它代表 UI 收口等待，不代表 Online 还有加载任务。 */
	bool bGlobalLoadingDismissalPending = false;

	/** 全局加载完成态允许撤遮罩的最早单调时间，单位秒；只在真实完成后写入，值到达前不改变任何 Online 状态。 */
	double GlobalLoadingDismissalReadyTimeSeconds = 0.0;

	/** 遮罩存活期间的 Slate 观察订阅；显示时注册、隐藏时解绑，捕获没有 Online/Pawn 通知的迟到就绪，不保存第二份加载状态。 */
	FDelegateHandle GlobalLoadingPostTickHandle;

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

	/** 当前 LocalPlayer 的默认背包 WBP；始终显示角色库存，外部库存由页面控制器另建指定 WBP。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatInventoryWidget> InventoryWidget;

	/** 当前 LocalPlayer 的常驻快捷栏 WBP；它只读角色随身背包 Model，随 Pawn 切换在本子系统成对创建和移除。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatInventoryQuickbarWidget> InventoryQuickbarWidget;

	/** 当前 LocalPlayer 的库存窗口控制器；它管理背包与外部库存的页面、输入和焦点，不中转物品操作。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatInventoryPageController> InventoryPageController;

	/** 本玩家唯一的物品悬停控制器；局内 UI 装配时创建，卸载时先解除来源再销毁。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatItemTooltipController> ItemTooltipController;

	/** 本玩家唯一的正式物品提示 View；显示在库存上层且不参与命中，卸载时移出视口。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatItemTooltipWidget> ItemTooltipWidget;

	/** 当前 LocalPlayer 的个人图鉴 WBP；装配时创建但不入视口，三个图鉴入口打开时才由页面控制器挂上去。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatCollectionWidget> CollectionWidget;

	/** 当前 LocalPlayer 的首解锁鱼种特写浮层；资产配置后才创建，一次性展示完由它自己请求收起。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatFishRevealWidget> FishRevealWidget;

	/** 本机图鉴首次记录订阅的配对解绑句柄；AttachPlayerLakeUI 写入，DetachPlayerLakeUI 消费。 */
	FDelegateHandle FishSpeciesFirstRecordedHandle;

	/** GameState 新鱼种广播订阅的配对解绑句柄；接上 GameState 时写入，换绑或卸载时消费。 */
	FDelegateHandle FishSpeciesDiscoveryHandle;

	/** 当前订阅新鱼种广播的 GameState；换 World 时先从旧宿主解绑，避免迟到通知打到已失效的 UI 上。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ACatfishingGameState> BoundDiscoveryGameState;

	/** 已经播报过的新鱼种广播序号；复制重发同一条时不重复刷提示。 */
	FGuid LastAnnouncedFishDiscoveryId;

	/** 正式浮层 WBP 缺失时只记录一次，避免每条新鱼都刷同一条诊断。 */
	bool bHasLoggedMissingFishRevealWidget = false;

	/**
	 * 首解锁特写的兜底收起计时。它不是设计里的「自动关闭」——设计给的出口是「Space 继续」，
	 * 而本轮没有对应的 InputAction 资产，浮层又刻意不抢键盘焦点（抢了就违反「你仍可移动」）。
	 * 所以留一个有界计时，保证这层绝不会卡死在屏幕上；资产接上之后这条计时只是个上限，不改变按键出口。
	 */
	FTimerHandle FishRevealDismissTimerHandle;

	/** 背包开关键名只在装配完成后落一次日志，避免每次刷新都刷同一行。 */
	bool bHasLoggedInventoryToggleKey = false;

	/** 当前 LocalPlayer 的图鉴页面控制器；它管理图鉴页的开关、模态输入与焦点，并持有只读 Collection Model。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatCollectionPageController> CollectionPageController;

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
