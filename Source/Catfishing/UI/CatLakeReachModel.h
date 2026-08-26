#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Fishing/Integration/CatFishingCommandTypes.h"
#include "Online/CatOnlineTypes.h"
#include "UI/CatLakeReachWidget.h"
#include "UObject/Object.h"
#include "UObject/ObjectKey.h"
#include "CatLakeReachModel.generated.h"

class AActor;
class APlayerController;
class ACatCharacter;
class ACatFishingSession;
class ACatfishingGameState;
class UAbilitySystemComponent;
class UCatConditionComponent;
class UCatContainerReplicationComponent;
class UCatEquipmentComponent;
class UCatFishingCommandComponent;
class UCatFishingViewBridge;
class UCatGrowthComponent;
class UCatProfileSubsystem;
class ULocalPlayer;
class UWorld;
struct FCatFishingViewState;
struct FOnAttributeChangeData;

/** UIReach Model 发布完整只读快照后的本机通知；PageController 收到后只把快照交给 View 渲染。 */
DECLARE_MULTICAST_DELEGATE(FCatLakeReachModelChanged);

/** LakeReach 的 MVC Model；它集中持有 Query 订阅、Fishing 只读桥、最近 Result 和当前 ViewState，不创建 Widget 也不提交玩法命令。 */
UCLASS()
class CATFISHING_API UCatLakeReachModel : public UObject
{
	GENERATED_BODY()

#if WITH_DEV_AUTOMATION_TESTS
	friend class FCatLocalPlayerUISubsystemFishingSessionLifecycleTest;
	friend class FCatLocalPlayerUISubsystemLakeReachAttachTest;
#endif

public:
	/** 绑定当前 LocalPlayer/Controller/Character 作为只读来源；成功后立即发布一份完整 ViewState，失败时不留下半套订阅。 */
	bool Bind(ULocalPlayer* InLocalPlayer, APlayerController* InController, ACatCharacter* InCharacter);

	/** 成对解除全部 Query、Result、FishingSession 生命周期与 World 监听，并清空当前 ViewState。 */
	void Unbind();

	/** 返回 Model 是否仍绑定一套有效 Lake 生命周期；PageController 用它判断是否可以刷新 View。 */
	bool IsBound() const;

	/** 写入 PageController 持有的菜单可见状态并重建 ViewState；Model 不读取 Widget 可见性反推菜单状态。 */
	void SetMenuOpen(bool bOpen);

	/** 按 UI 提供的相对偏移移动当前鱼护选择；Model 会基于最近鱼护快照裁剪范围并重新发布 ViewState。 */
	bool SelectFishGuardEntryByOffset(int32 Offset);

	/** 标记 PageController 已把鱼护动作提交给服务器；Model 用 RequestId 等待对应结果并让 View 暂时禁用重复点击。 */
	void MarkFishGuardActionSubmitted(ECatUIReachFishGuardAction Action, FGuid RequestId);

	/** 标记鱼护动作在 PageController 本地适配阶段已被拒绝；Model 发布结构化反馈但不伪造任何后端写入。 */
	void MarkFishGuardActionRejected(ECatUIReachFishGuardAction Action, FGuid RequestId,
		ECatDomainCommandError Error, int64 Revision);

	/** 主动从当前 Query 来源重读一次完整快照；Online 变化这类外部事件由 PageController 调用它收敛到 Model。 */
	void Refresh();

	/** 返回最近一次完整 UIReach ViewState；调用方只能读取副本语义，不能通过它写回任何领域真相。 */
	const FCatUIReachViewState& GetViewState() const;

	/** 判断 Lake 菜单是否允许提交正式离局意图；规则只读 Online Snapshot，不访问 Widget 或平台旅行接口。 */
	static bool CanRequestOnlineLeaveFromLake(const FCatOnlineSnapshot& Snapshot);

	/** 当前绑定的 Fishing 只读桥；测试只用它确认会话生命周期清理，正式调用方不应跨过 Model 直接渲染。 */
	UCatFishingViewBridge* GetFishingViewBridgeForTests() const;

	/** 当前 HUD 属性来源 ASC；测试用它证明 Model 绑定的是被占有 Character 的能力系统。 */
	UAbilitySystemComponent* GetBoundAbilitySystemForTests() const;

	/** 当前 Model 观察的 FishingSession；测试用它确认 Destroyed/EndPlay 后弱引用会被清空。 */
	ACatFishingSession* GetObservedFishingSessionForTests() const;

	/** 完整 ViewState 已变化的通知；只有 Bind、Refresh、Result、Session 生命周期和菜单状态变化会触发。 */
	FCatLakeReachModelChanged OnViewStateChanged;

private:
	/** ASC HUD 属性变化入口；事件载荷只说明有事实变更，Model 随后重读所有 Query 来源。 */
	void HandleLakeAttributeChanged(const FOnAttributeChangeData& ChangeData);

	/** Condition、Equipment、Run、Help、Shop、鱼护或 Profile 变化入口；每次统一重建整份 ViewState。 */
	void HandleLakeSnapshotChanged();

	/** FishingBridge 发布新会话投影的入口；Model 忽略增量载荷并重新聚合所有 UIReach 事实。 */
	void HandleFishingViewStateChanged(const FCatFishingViewState& ViewState);

	/** owning Controller 收到结构化 Fishing 终态时缓存最近 Result，并重新定位可能新建的复制 Session。 */
	UFUNCTION()
	void HandleFishingCommandResult(const FCatFishingCommandResult& Result);

	/** owning Controller 收到转缸等营地命令终态时匹配当前鱼护请求，并把结果写入 UI 反馈区。 */
	void HandleCampCommandResult(const FCatDomainCommandResult& Result);

	/** owning Controller 收到献祭协议终态时匹配当前鱼护请求，并把协议阶段折算到 UI 反馈区。 */
	void HandleSacrificeResult(const FCatSacrificeResult& Result);

	/** owning Controller 收到直接吃鱼终态时匹配当前鱼护请求，并把 Items 结果写入 UI 反馈区。 */
	void HandleFishConsumeResult(const FCatFishConsumeResult& Result);

	/** 判断某个服务器回包是否属于当前等待的鱼护动作；动作类型和 RequestId 都必须一致。 */
	bool IsPendingFishGuardResult(ECatUIReachFishGuardAction Action, FGuid RequestId) const;

	/** 当前 World 生成 FishingSession Actor 时安排下一帧重新定位；其他 Actor 不触发 UI 工作。 */
	void HandleWorldActorSpawned(AActor* SpawnedActor);

	/** 按当前 PlayerState 定位唯一非终态复制 Session，并调和 FishingBridge 与 Actor 生命周期观察。 */
	void RefreshFishingSessionBinding();

	/** 把 FishingBridge 会话绑定和 Actor 生命周期观察作为一个原子切换；传空或绑定失败会刷新为无活动会话。 */
	void SetFishingViewSession(ACatFishingSession* Session);

	/** 观察当前 Bridge 绑定会话的 Actor 生命周期；只保存弱引用和对象身份键，不持有或延长 FishingSession。 */
	void ObserveFishingSessionLifecycle(ACatFishingSession* Session);

	/** 移除当前 FishingSession 的 Destroyed/EndPlay 观察并清空弱引用与身份键。 */
	void StopObservingFishingSessionLifecycle();

	/** 当前观察的 FishingSession 发出 Destroyed 时进入统一收口，来源过滤由身份键完成。 */
	UFUNCTION()
	void HandleFishingSessionDestroyed(AActor* DestroyedActor);

	/** 当前观察的 FishingSession 发出 EndPlay 时进入统一收口，EndPlayReason 不参与 UI 结果推导。 */
	UFUNCTION()
	void HandleFishingSessionEndPlay(AActor* Actor, EEndPlayReason::Type EndPlayReason);

	/** Destroyed/EndPlay 的统一收口；确认来源后解绑 Bridge 并刷新为 no active session。 */
	void HandleFishingSessionLifecycleEnded(AActor* SessionActor);

	/** 当前 LocalPlayer 读源；Model 用它定位 Profile 与 GameInstance Online 快照，不跨 World 持有全局状态。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ULocalPlayer> BoundLocalPlayer;

	/** 当前 owning Controller 读源；Model 用它确认 Pawn、PlayerState 与 Fishing 命令结果来源。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> BoundPlayerController;

	/** 当前根 View 使用的唯一 Fishing 只读桥；Model 持有并随 Pawn/World 一起解绑。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatFishingViewBridge> FishingViewBridge;

	/** 当前 HUD 属性 delegate 所属 ASC 弱引用；Unbind 必须在它仍存活时用相同属性键成对 Remove。 */
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

	/** 当前 Character 上的个人鱼护复制出口；Model 订阅其完整快照变化但不获得任何写权限。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatContainerReplicationComponent> BoundPersonalFishGuard;

	/** 当前 LocalPlayer 的 durable Profile 读源；只用于图鉴快照和变化通知。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatProfileSubsystem> BoundProfile;

	/** 当前 owning PlayerController 的 Fishing 命令结果源；动态委托用于显示最近结构化终态。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatFishingCommandComponent> BoundFishingCommand;

	/** 当前 UIReach 正在跟踪 Actor 生命周期的 FishingSession；绑定新会话时写入，停止观察或结束回调时清空。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ACatFishingSession> ObservedFishingSession;

	/** 当前观察 Session 的对象身份快照；生命周期回调用它在弱引用可能失效时过滤旧 Actor。 */
	FObjectKey ObservedFishingSessionKey;

	/** Poison 属性变化 delegate 的配对解绑句柄；Bind 写入，Unbind 从同一 ASC 移除。 */
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

	/** GameState 商店公开快照通知的配对解绑句柄；影响钱包余额和商店按钮是否可提交。 */
	FDelegateHandle ShopEconomyChangedHandle;

	/** 个人鱼护快照订阅的生命周期凭据；影响旧鱼护复制事件能否继续刷新当前 ViewState。 */
	FDelegateHandle FishGuardChangedHandle;

	/** Profile durable 图鉴订阅的生命周期凭据；影响旧 LocalPlayer 图鉴事件是否会迟到污染新 World。 */
	FDelegateHandle FishCollectionChangedHandle;

	/** FishingViewBridge 会话 DTO 订阅的生命周期凭据；影响旧会话反馈是否会继续驱动 LakeReach 渲染。 */
	FDelegateHandle FishingViewChangedHandle;

	/** PlayerController 营地结果订阅的生命周期凭据；只用于鱼护转缸请求回包。 */
	FDelegateHandle CampCommandResultHandle;

	/** PlayerController 献祭结果订阅的生命周期凭据；只用于鱼护献祭请求回包。 */
	FDelegateHandle SacrificeResultHandle;

	/** PlayerController 吃鱼结果订阅的生命周期凭据；只用于鱼护直接进食请求回包。 */
	FDelegateHandle FishConsumeResultHandle;

	/** 当前 Lake World 的 Actor 生成订阅凭据；只用于发现复制 FishingSession，旅行时必须解绑。 */
	FDelegateHandle ActorSpawnedHandle;

	/** 最近收到的 Fishing Command 结构化终态；只供 ViewState 展示，不承担请求幂等或重试。 */
	FCatFishingCommandResult LastFishingCommandResult;

	/** 最近是否存在可展示的 Fishing Command 终态；Unbind 清空以免跨 Pawn 泄漏旧错误。 */
	bool bHasFishingCommandResult = false;

	/** 当前鱼护列表被选中的下标；Refresh 会按最新鱼护快照裁剪，空鱼护时重置为 INDEX_NONE。 */
	int32 SelectedFishGuardIndex = INDEX_NONE;

	/** 当前等待回包的鱼护动作类型；没有 pending 时保持 None。 */
	ECatUIReachFishGuardAction PendingFishGuardAction = ECatUIReachFishGuardAction::None;

	/** 当前等待回包的鱼护请求 ID；PageController 写入，服务器结果必须匹配它才会清 pending。 */
	FGuid PendingFishGuardRequestId;

	/** 当前是否已有鱼护动作发出但尚未收到对应服务器终态；View 用它禁用重复点击。 */
	bool bFishGuardActionPending = false;

	/** 最近一次完成或拒绝的鱼护动作类型；View 用它给反馈归类。 */
	ECatUIReachFishGuardAction LastFishGuardAction = ECatUIReachFishGuardAction::None;

	/** 最近一次鱼护动作的公共结果头；吃鱼/转缸原样缓存，献祭映射 Items 侧结果。 */
	FCatDomainCommandResult LastFishGuardCommandResult;

	/** 最近是否存在可展示的鱼护动作结果；Unbind 清空，避免跨 Pawn 泄漏旧反馈。 */
	bool bHasFishGuardCommandResult = false;

	/** 最近一次献祭协议详细结果；只有 LastFishGuardAction 为献祭时才对应同一 UI 操作。 */
	FCatSacrificeResult LastFishGuardSacrificeResult;

	/** 最近一次直接吃鱼详细结果；只有 LastFishGuardAction 为吃鱼时才对应同一 UI 操作。 */
	FCatFishConsumeResult LastFishGuardConsumeResult;

	/** 菜单当前是否打开的 PageController 投影；Model 只把它合入 ViewState，不修改 InputMode 或焦点。 */
	bool bMenuOpen = false;

	/** 最近一次发布给 View 的完整 DTO；所有 Query 来源变化都先收敛到这里，再由通知驱动渲染。 */
	FCatUIReachViewState ViewState;
};
