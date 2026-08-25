#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Fishing/Integration/CatFishingCommandTypes.h"
#include "Online/CatOnlineTypes.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "UObject/ObjectKey.h"
#include "CatLocalPlayerUISubsystem.generated.h"

class AActor;
class APlayerController;
class APawn;
class ACatCharacter;
class ACatFishingSession;
class ACatfishingGameState;
class UCatTravelWidget;
class UCatLakeReachWidget;
class UCatFishingViewBridge;
class UCatFishingCommandComponent;
class UCatContainerReplicationComponent;
class UCatProfileSubsystem;
class UAbilitySystemComponent;
class UCatConditionComponent;
class UCatEquipmentComponent;
class UCatGrowthComponent;
class UEnhancedInputComponent;
class UInputAction;
class UInputMappingContext;
class UWorld;
struct FCatFishingViewState;
struct FOnAttributeChangeData;

/** 每个 LocalPlayer 的唯一 UI 深模块；统一拥有 Frontend 旅行 View，并只在显式 gate 开启时装配 LakeReach 根和其只读订阅。 */
UCLASS()
class CATFISHING_API UCatLocalPlayerUISubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

#if WITH_DEV_AUTOMATION_TESTS
	friend class FCatLocalPlayerUISubsystemFishingSessionLifecycleTest;
	friend class FCatLocalPlayerUISubsystemLakeReachAttachTest;
	friend class FCatLocalPlayerUISubsystemOnlineWidgetPolicyTest;
#endif

public:
	/** 订阅 GameInstance Online 快照，绑定当前 Controller Pawn notifier，并在显式 gate 开启时按当前 Pawn 装配 LakeReach 根 View。 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** 先恢复菜单输入并解绑 Pawn/Controller，再移除 Online View 和快照订阅，保证 LocalPlayer 销毁后没有迟到 UI 更新。 */
	virtual void Deinitialize() override;

	/** Controller 替换时先从旧 Controller 恢复 InputMode 并清全部订阅，再弱绑定新 Controller 并装配其当前 Pawn。 */
	virtual void PlayerControllerChanged(APlayerController* NewController) override;

	/** 切换当前 LocalPlayer 的 Lake 菜单；只有显式启用的根 View 与 Controller 同时存在时才改变 InputMode、焦点和鼠标。 */
	void ToggleLakeMenu();

	/** 返回当前 Lake 菜单是否由本协调器保持打开；它不从 Widget 可见性或鼠标状态反推。 */
	bool IsLakeMenuOpen() const;

private:
	/** 响应 Online 事实变更；实现重新读取完整 Snapshot，并同步 Frontend 面板与 Lake 菜单离局入口。 */
	void HandleOnlineSnapshotChanged();

	/** 把 View 的 Host/Find/Join/Invite/Leave 意图翻译为 Online 公共接口调用；不直接访问 OSS 或旅行 API。 */
	void HandleActionRequested(ECatOnlineUIAction Action, FGuid OpaqueHandle);

	/** 根据当前 Controller 与完整 Online 快照调和唯一 TravelWidget；只有 Frontend 或进入 Lake 前的旅行态会创建或刷新。 */
	void RefreshOnlineWidgetForCurrentController();

	/** 判断 Frontend 旅行面板是否仍属于当前屏幕；Lake、回 Frontend 途中和异常 World 都不显示 Host/Find/Join 白盒。 */
	static bool ShouldShowOnlineTravelWidget(const FCatOnlineSnapshot& Snapshot);

	/** 判断显式 UIReach 菜单是否可以展示正式离局入口；只接受已在 Lake、已有 Session 身份且没有待完成 Online 操作的快照。 */
	static bool CanRequestOnlineLeaveFromLake(const FCatOnlineSnapshot& Snapshot);

	/** 成对解除 View 动作广播并移出视口；空实例和重复调用保持幂等。 */
	void RemoveOnlineWidget();

	/** 弱绑定 Controller 的 GetOnNewPawnNotifier，并立即尝试装配其当前 Pawn；不跨 World 强持 Controller。 */
	void BindController(APlayerController* Controller);

	/** 从旧 Controller 精确移除 Pawn notifier 并清弱引用；Controller 已销毁时只清本地句柄。 */
	void UnbindController();

	/** 当前 Controller Pawn 变化入口；先完整解绑旧 Lake 根，再尝试把 NewPawn 作为新的 ACatCharacter 只读来源。 */
	void HandleControllerPawnChanged(APawn* NewPawn);

	/** 当 View gate 与当前 Character 有效时创建唯一 LakeReach 根，并绑定身体、Fishing、鱼护、Profile 和菜单输入。 */
	void AttachLakePawn(ACatCharacter* Character);

	/** 先恢复旧 Controller 输入，再解除所有多源通知、Mapping Context 与 Widget，最后清理弱引用和缓存。 */
	void DetachLakePawn();

	/** 任一 ASC HUD 属性变化时重新读取全部数值和完整快照，避免 UI 从增量事件拼接状态。 */
	void HandleLakeAttributeChanged(const FOnAttributeChangeData& ChangeData);

	/** Condition、Equipment、Run、Help、鱼护或 Profile 变化的无载荷入口；每次重建整份 UIReach ViewState。 */
	void HandleLakeSnapshotChanged();

	/** Fishing Bridge 发布新会话投影时重建完整 UIReach 状态；参数只表示变化来源，不被增量拼接。 */
	void HandleFishingViewStateChanged(const FCatFishingViewState& ViewState);

	/** owning Controller 收到结构化 Fishing 终态时缓存最近一条反馈，并重新定位可能新建的复制 Session。 */
	UFUNCTION()
	void HandleFishingCommandResult(const FCatFishingCommandResult& Result);

	/** World 生成 FishingSession Actor 时安排下一帧重新定位；其他 Actor 不触发 UI 工作。 */
	void HandleWorldActorSpawned(AActor* SpawnedActor);

	/** 按当前 PlayerState 定位唯一非终态复制 Session；旧会话已终态但尚未 EndPlay 时保留终态复制窗口，否则成对切换 Bridge 与观察关系。 */
	void RefreshFishingSessionBinding();

	/** 把 FishingBridge 会话绑定和 Actor 生命周期观察作为一个原子切换；传空或绑定失败时，已有 Bridge 会进入无活动会话刷新。 */
	void SetFishingViewSession(ACatFishingSession* Session);

	/** 观察当前 Bridge 绑定会话的 Actor 生命周期；只保存弱引用和对象身份键，不持有或延长 FishingSession。 */
	void ObserveFishingSessionLifecycle(ACatFishingSession* Session);

	/** 移除当前 FishingSession 的 Destroyed/EndPlay 观察并清空弱引用与身份键；Detach、换会话和会话结束回调都会走这里保持配对。 */
	void StopObservingFishingSessionLifecycle();

	/** 当前观察的 FishingSession 发出 Destroyed 时进入统一收口；非当前会话或 EndPlay 双触发会被身份键过滤。 */
	UFUNCTION()
	void HandleFishingSessionDestroyed(AActor* DestroyedActor);

	/** 当前观察的 FishingSession 发出 EndPlay 时进入统一收口；EndPlayReason 只表示生命周期边界，来源过滤和双触发去重交给收口处理。 */
	UFUNCTION()
	void HandleFishingSessionEndPlay(AActor* Actor, EEndPlayReason::Type EndPlayReason);

	/** Destroyed/EndPlay 的统一收口；确认来源仍是当前身份键后解绑 Bridge 并刷新根 View，让下一帧呈现 no active session。 */
	void HandleFishingSessionLifecycleEnded(AActor* SessionActor);

	/** 从当前 ASC、组件、Fishing Bridge、鱼护与 Profile 重建完整 DTO，并只调用根 Widget::Render。 */
	void RefreshLakeView();

	/** 为当前 Controller 创建一个配置化原生菜单 Action/Context，安装到 LocalPlayer 并保存唯一绑定句柄。 */
	void InstallMenuInput(APlayerController* Controller);

	/** 从原 Controller 精确移除菜单 Action 绑定和 Mapping Context，再释放瞬时输入对象。 */
	void RemoveMenuInput();

	/** 根据菜单状态设置 UIOnly 或 GameOnly、键盘焦点和鼠标，并在关闭时恢复打开前的鼠标可见性。 */
	void ApplyLakeMenuInputMode(bool bOpen);

	/** 根 View 发出关闭意图时只关闭已打开菜单；关闭状态下的迟到点击不反向打开。 */
	void HandleLakeMenuCloseRequested();

	/** 根 View 发出离局意图时转交给统一 Online Leave；不直接 DestroySession、ServerTravel 或 ClientTravel。 */
	void HandleLakeLeaveRequested();

	/** 当前 LocalPlayer 唯一 Online 白盒界面；子系统拥有，Controller 变化或销毁时释放。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatTravelWidget> OnlineWidget;

	/** 当前 LocalPlayer 显式启用后的唯一 LakeReach 根 View；默认玩家路径为空，启用时承载原生 HUD、Fishing、菜单、鱼护与图鉴表现。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatLakeReachWidget> LakeReachWidget;

	/** 当前根 View 使用的唯一 Fishing 只读桥；协调器持有并随 Pawn/World 一起解绑。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatFishingViewBridge> FishingViewBridge;

	/**
	 * 当前 UIReach 正在跟踪 Actor 生命周期的 FishingSession；绑定新会话时写入，停止观察、Detach 或结束回调时清空。
	 * 只在解绑 Destroyed/EndPlay 委托时读取，弱引用不持有会话也不阻止销毁。
	 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ACatFishingSession> ObservedFishingSession;

	/**
	 * 当前观察 Session 的对象身份快照；绑定新会话时写入，停止观察、Detach 或结束回调时清空。
	 * 生命周期回调用它在弱引用可能失效时过滤旧 Actor；FObjectKey 不持有对象，也不延长销毁窗口。
	 */
	FObjectKey ObservedFishingSessionKey;

	/** 当前 LocalPlayer 菜单的瞬时 Enhanced Input Action；只表达开关意图，不进入资产或领域状态。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LakeMenuToggleAction;

	/** 当前 LocalPlayer 菜单的瞬时 Mapping Context；安装与移除始终由本协调器成对执行。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> LakeMenuMappingContext;

	/** 菜单 Action 实际绑定的 Enhanced Input 组件；换 Controller 时从原组件精确移除，不假设当前 InputComponent 仍是同一对象。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UEnhancedInputComponent> BoundMenuInputComponent;

	/** 当前绑定 Pawn notifier 的 Controller 弱引用；Controller/World 替换时先解绑，绝不成为跨 World 所有者。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> BoundPlayerController;

	/** 当前 HUD 属性 delegate 所属 ASC 弱引用；Detach 必须在它仍存活时用相同属性键成对 Remove。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UAbilitySystemComponent> BoundLakeASC;

	/** 当前 HUD 读取的 Condition 弱引用；解绑时从同一对象移除 Snapshot 通知。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatConditionComponent> BoundCondition;

	/** 当前 HUD 读取的 Equipment 弱引用；解绑时从同一对象移除 Snapshot 通知。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatEquipmentComponent> BoundEquipment;

	/** 当前 HUD 读取的 Growth 弱引用；解绑时从同一对象移除 Snapshot 通知。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatGrowthComponent> BoundGrowth;

	/** 当前 World 的 GameState 弱引用；旅行时解绑 Run/Help 通知，不跨 World 保存公开快照。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ACatfishingGameState> BoundGameState;

	/** 当前 Lake 根实际绑定的 World；Actor 生成监听从这一个实例精确移除，旅行后不误操作新 World。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UWorld> BoundLakeWorld;

	/** 当前 Character 上的个人鱼护复制出口；UI 订阅其完整快照变化但不获得任何写权限。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatContainerReplicationComponent> BoundPersonalFishGuard;

	/** 当前 LocalPlayer 的 durable Profile 读源；只用于图鉴快照和变化通知。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatProfileSubsystem> BoundProfile;

	/** 当前 owning PlayerController 的 Fishing 命令结果源；动态委托用于显示最近结构化终态。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatFishingCommandComponent> BoundFishingCommand;

	/** Online 快照广播的配对解绑句柄；Initialize 写入，Deinitialize 消费。 */
	FDelegateHandle OnlineSnapshotHandle;

	/** Widget 动作广播的配对解绑句柄；创建时写入，移除时消费。 */
	FDelegateHandle ActionHandle;

	/** 当前 Controller Pawn notifier 的配对解绑句柄；Controller 变化或 Deinitialize 时消费。 */
	FDelegateHandle PawnChangedHandle;

	/** Poison 属性变化 delegate 的配对解绑句柄；Attach 写入，Detach 从同一 ASC 移除。 */
	FDelegateHandle PoisonChangedHandle;

	/** FishingStrength 属性变化 delegate 的配对解绑句柄；只驱动完整 HUD 重读。 */
	FDelegateHandle FishingStrengthChangedHandle;

	/** FightStamina 属性变化 delegate 的配对解绑句柄；只驱动完整 HUD 重读。 */
	FDelegateHandle FightStaminaChangedHandle;

	/** Condition 完整快照通知的配对解绑句柄。 */
	FDelegateHandle ConditionChangedHandle;

	/** Equipment 完整快照通知的配对解绑句柄。 */
	FDelegateHandle EquipmentChangedHandle;

	/** Growth 完整快照通知的配对解绑句柄。 */
	FDelegateHandle GrowthChangedHandle;

	/** GameState Run/Environment 完整快照通知的配对解绑句柄。 */
	FDelegateHandle RunChangedHandle;

	/** GameState 最近求助完整快照通知的配对解绑句柄。 */
	FDelegateHandle HelpChangedHandle;

	/** 个人鱼护快照订阅的生命周期凭据；AttachLakePawn 写入，DetachLakePawn 读取并移除，影响旧鱼护复制事件能否继续刷新当前 View。 */
	FDelegateHandle FishGuardChangedHandle;

	/** Profile durable 图鉴订阅的生命周期凭据；AttachLakePawn 写入，DetachLakePawn 读取并移除，影响旧 LocalPlayer 图鉴事件是否会迟到污染新 World。 */
	FDelegateHandle FishCollectionChangedHandle;

	/** FishingViewBridge 会话 DTO 订阅的生命周期凭据；AttachLakePawn 写入，DetachLakePawn 读取并移除，影响旧会话反馈是否会继续驱动 LakeReach 渲染。 */
	FDelegateHandle FishingViewChangedHandle;

	/** 当前 Lake World 的 Actor 生成订阅凭据；AttachLakePawn 写入，DetachLakePawn 读取并移除，只用于发现复制 FishingSession，漏解绑会让旅行后的旧 World 触发绑定。 */
	FDelegateHandle ActorSpawnedHandle;

	/** 根 View 关闭意图订阅的生命周期凭据；AttachLakePawn 写入，DetachLakePawn 读取并移除，影响旧菜单按钮是否还能改当前输入模式。 */
	FDelegateHandle LakeMenuCloseHandle;

	/** 根 View 离局意图订阅的生命周期凭据；AttachLakePawn 写入，DetachLakePawn 从同一 Widget 精确移除，影响旧按钮是否还能提交 Online Leave。 */
	FDelegateHandle LakeMenuLeaveHandle;

	/** Enhanced Input 组件中菜单 Action 的唯一绑定句柄；0 表示没有可移除绑定。 */
	uint32 LakeMenuInputBindingHandle = 0;

	/** 菜单 Mapping Context 当前是否已安装到 LocalPlayer；Remove 只在 true 时调用以保持精确配对。 */
	bool bLakeMenuMappingInstalled = false;

	/** 菜单当前是否打开的唯一状态；Toggle 写入，View 和输入恢复只读取。 */
	bool bLakeMenuOpen = false;

	/** 打开菜单前 Controller 的鼠标可见性；关闭、换 Pawn 或旅行时恢复该值。 */
	bool bPreviousMouseCursorVisible = false;

	/** 最近收到的 Fishing Command 结构化终态；只供 View 展示，不承担请求幂等或重试。 */
	FCatFishingCommandResult LastFishingCommandResult;

	/** 最近是否存在可展示的 Fishing Command 终态；Detach 清空以免跨 Pawn 泄漏旧错误。 */
	bool bHasFishingCommandResult = false;
};
