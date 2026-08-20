#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatRunContracts.h"
#include "Items/CatItemTypes.h"
#include "Online/CatOnlineTypes.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "CatLocalPlayerUISubsystem.generated.h"

class APlayerController;
class APawn;
class ACatCampHubActor;
class ACatCharacter;
class ACatFishingSession;
class ACatFishTankActor;
class ACatfishingGameState;
class ACatfishingPlayerController;
class UCatCommandPanelWidget;
class UCatTravelWidget;
class UCatSurvivalWidget;
enum class ECatCommandPanelAction : uint8;
class UAbilitySystemComponent;
class UCatConditionComponent;
class UCatEquipmentComponent;
struct FOnAttributeChangeData;

/**
 * 开发期白盒命令面板一次点击分派所需的全部输入；UI 子系统在点击瞬间从已绑定的快照和当前 World 现取一份，分派函数只读它、不再碰 World。
 * 把它单独拿出来是为了让分派表成为纯函数：自动化测试用一份空输入逐枚举调用，就能证明每个意图都有分派分支，而不必伪造 Controller。
 */
struct FCatCommandPanelDispatchInput
{
	/** 真正发 RPC 的 owning-client Controller；为空时分派函数只生成描述文本、不发送（测试就是这样用的）。 */
	ACatfishingPlayerController* Controller = nullptr;

	/** 本人当前身体；抢抄命令要带一个表现位置，从它取 ActorLocation。 */
	ACatCharacter* Character = nullptr;

	/** 当前 World 里第一个营地宿主；休息/回看/入缸/救援四条命令的目标，为空时这些命令只记“跳过”。 */
	ACatCampHubActor* Camp = nullptr;

	/** 当前 World 里第一条非终态钓鱼会话；抢抄命令的目标和 ExpectedRevision 来源，为空时抢抄跳过。 */
	ACatFishingSession* FishingSession = nullptr;

	/** 当前 World 里第一个倒地的角色（可能是自己）；救援命令的目标，为空时救援跳过。 */
	ACatCharacter* RescueTarget = nullptr;

	/** 最近一次读到的 Run 公开快照；ready/结算/献祭都从它取 ExpectedRevision。 */
	FCatRunPublicState Run;

	/** 本人个人鱼护快照；“第一条鱼”类命令取 Fish[0] 与容器 Revision。 */
	FCatContainerSnapshot Guard;

	/** 当前 World 里第一个共享鱼缸容器的 Revision；入缸命令需要它做 Compare-and-Commit。 */
	int64 TankRevision = 0;

	/** 团队公款当前版本；商店买/领/卖三条命令都要带它。 */
	int64 WalletRevision = 0;

	/** 本人装备快照版本；starter 装配、取用装备和投窝三条命令都要带它。 */
	int64 EquipmentRevision = 0;

	/** 团队装备库当前版本；取用命令要带它做并发前提。 */
	int64 TeamLibraryRevision = 0;

	/** 团队装备库里第一件实物的实例 ID；取用命令的目标，无效时取用跳过。 */
	FGuid FirstTeamEquipmentInstanceId;

	/** 本人耗材栈里第一种窝料的定义 ID；投窝命令的耗材，None 时投窝跳过。 */
	FName FirstChumDefinitionId = NAME_None;

	/**
	 * 窝点集合当前版本；投窝命令要带它做并发前提。它只在本机同时是服务器（PIE 单机/监听服务器）时能读到——窝点子系统不复制、客户端上没有；
	 * 纯客户端这里是 0，投窝会被服务器以版本冲突拒绝。白盒面板接受这个限制，不为它新建复制通道。
	 */
	int64 ChumRevision = 0;

	/** 本人是否倒地；决定手动求助发“钓鱼求助”还是“倒地求助”。 */
	bool bDowned = false;
};

/**
 * 每个 LocalPlayer 的唯一 UI MVC 协调器；分别消费 Online 公共快照与当前 Pawn 的三属性/Condition/Equipment/Run/Help 投
 * 影，Controller/World 替换时成对解绑。
 */
UCLASS()
class CATFISHING_API UCatLocalPlayerUISubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	/** 订阅 GameInstance Online 快照，绑定当前 Controller Pawn notifier，并按当前 Pawn 分别装配 Online 与 Survival View。 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** 先解绑 Controller/Pawn/ASC/View，再移除 Online Widget 和快照订阅，保证 LocalPlayer 销毁后没有迟到 UI 更新。 */
	virtual void Deinitialize() override;

	/** Controller 替换时先解绑旧 Pawn notifier、ASC delegate 与 View，再弱绑定新 Controller 并装配当前 Pawn。 */
	virtual void PlayerControllerChanged(APlayerController* NewController) override;

	/**
	 * 开发期白盒面板意图到 ACatfishingPlayerController Server RPC 的唯一分派表；按意图从 Input 取参数并调用对应
	 * RPC，返回一行人能读的反馈文本（发了什么、带了什么参数，或为什么没发）。
	 * Input.Controller 为空时只描述不发送；没有 default 分支，漏项返回空串，自动化测试按 StaticEnum 逐值调用并断言非空。
	 */
	static FString DispatchCommandPanelAction(ECatCommandPanelAction Action, const FCatCommandPanelDispatchInput& Input);

private:
	/** 响应 Online 事实变更；实现重新读取完整 Snapshot，View 不从旧事件参数拼接状态。 */
	void HandleOnlineSnapshotChanged();

	/** 把 View 的 Host/Find/Join/Invite/Leave 意图翻译为 Online 公共接口调用；不直接访问 OSS 或旅行 API。 */
	void HandleActionRequested(ECatOnlineUIAction Action, FGuid OpaqueHandle);

	/** 根据当前 Controller 与完整 Online 快照调和唯一 TravelWidget；直达 Lake 且没有 Session 时移除，其余状态创建或刷新。 */
	void RefreshOnlineWidgetForCurrentController();

	/** 成对解除 View 动作广播并移出视口；空实例和重复调用保持幂等。 */
	void RemoveOnlineWidget();

	/** 弱绑定 Controller 的 GetOnNewPawnNotifier，并立即尝试装配其当前 Pawn；不跨 World 强持 Controller。 */
	void BindController(APlayerController* Controller);

	/** 从旧 Controller 精确移除 Pawn notifier 并清弱引用；Controller 已销毁时只清本地句柄。 */
	void UnbindController();

	/** 当前 Controller Pawn 变化入口；先完整解绑旧身体，再尝试把 NewPawn 作为新的 ACatCharacter Model 来源。 */
	void HandleControllerPawnChanged(APawn* NewPawn);

	/**
	 * GameInstance 级 Pawn↔Controller 变化入口——这是**远端客户端**唯一可靠的占有通知。
	 * Controller 的 GetOnNewPawnNotifier 只在服务器的 Possess/UnPossess 里广播；客户端收到 Pawn 走的是
	 * 复制回调（APawn::OnRep_Controller → NotifyControllerChanged），那条链最终广播的是
	 * UGameInstance::GetOnPawnControllerChanged。只订阅前者时，联机局里除房主外所有玩家的 HUD 永远不会装配
	 * （2026-08-20 双进程联机冒烟第一次跑就抓到了这个缺陷）。两个订阅并存：本机占有走 notifier，远端占有走这里；
	 * 处理器对"已装配同一身体"做幂等短路，两条通知先后到达不会重复建 View。
	 * 该广播是动态多播委托，所以必须是 UFUNCTION；解绑用 RemoveDynamic 按 (对象,函数) 配对，不走 FDelegateHandle。
	 */
	UFUNCTION()
	void HandlePawnControllerChanged(APawn* Pawn, AController* NewController);

	/** 当 View gate 与当前 Character 有效时，绑定三属性与 Condition/Equipment/Run/Help 通知，创建 View 并发布首份完整投影。 */
	void AttachSurvivalPawn(ACatCharacter* Character);

	/** 先从旧 ASC 移除三属性 delegate，再解除 Condition/Equipment/GameState 的多源通知，最后释放 View 和全部弱引用。 */
	void DetachSurvivalPawn();

	/** 任一 ASC HUD 属性变化时重新读取全部数值和完整快照，避免 UI 从增量事件拼接 Model。 */
	void HandleSurvivalAttributeChanged(const FOnAttributeChangeData& ChangeData);

	/** Condition、Equipment、Run 或 Help 完整快照变化的无载荷入口；每次都从当前宿主重建整份 HUD ViewState。 */
	void HandleGameplaySnapshotChanged();

	/**
	 * 从当前弱绑定 ASC、Condition、Equipment 与 GameState 读取完整投影并只调用 Widget::Render；面板存在时再把同一批事
	 * 实整理成面板 ViewState 交给 Configure。Widget 不接触玩法宿主。
	 */
	void RefreshSurvivalView();

	/** 面板点击入口：现取一份分派输入交给 DispatchCommandPanelAction，把反馈文本记下来并写日志，最后整份重刷 View 让人立刻看到结果。 */
	void HandleCommandPanelAction(ECatCommandPanelAction Action);

	/**
	 * 从已绑定的 Controller/GameState/Condition/Equipment 和当前 World 现取一份面板分派输入；面板刷新和点击都用同一份
	 * 采集逻辑，避免按钮可用性与实际发送看到两套事实。
	 * 它会写 CachedCamp / CachedFishTank，所以不是 const 成员函数。
	 */
	FCatCommandPanelDispatchInput GatherCommandPanelInput();

	/** 当前 LocalPlayer 唯一 Online 白盒界面；子系统拥有，Controller 变化或销毁时释放。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatTravelWidget> OnlineWidget;

	/** 当前 LocalPlayer 唯一 Lake 状态 View；仅在有 ACatCharacter 且正式 UI gate 开启时存在。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatSurvivalWidget> SurvivalWidget;

	/** 当前 LocalPlayer 唯一开发期白盒命令面板；只在状态 View 已装配且 UISettings 开关打开时存在，随状态 View 一起创建/销毁。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatCommandPanelWidget> CommandPanel;

	/** 当前绑定 Pawn notifier 的 Controller 弱引用；Controller/World 替换时先解绑，绝不成为跨 World 所有者。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> BoundPlayerController;

	/** 当前属性 delegate 所属 ASC 弱引用；Detach 必须在它仍存活时用相同属性键成对 Remove。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UAbilitySystemComponent> BoundSurvivalASC;

	/** 当前 HUD 读取的 Condition 弱引用；解绑时从同一对象移除 Snapshot 通知。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatConditionComponent> BoundCondition;

	/** 当前 HUD 读取的 Equipment 弱引用；解绑时从同一对象移除 Snapshot 通知。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatEquipmentComponent> BoundEquipment;

	/** 当前 World 的 GameState 弱引用；旅行时解绑 Run/Help 通知，不跨 World 保存公开快照。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ACatfishingGameState> BoundGameState;

	/**
	 * 上一次找到的营地宿主。营地是关卡摆放的，一局之内既不会换也不会多出第二个，所以一旦找到就没有必要每次采集面板输入
	 * 都把整个 World 的 Actor 扫一遍——而面板输入在搏斗期间是逐帧重取的。
	 * 只有它失效（被销毁）或者属于别的 World（旅行后）时才重新扫描，扫不到就保持空并在下次继续扫，这样客户端上营地还没
	 * 复制过来的那几帧行为和以前完全一样。
	 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ACatCampHubActor> CachedCamp;

	/** 上一次找到的共享鱼缸；和营地同为关卡摆放的稳定目标，复用理由与失效条件相同。面板每次仍从它现读容器 Revision，缓存的只是"找哪个 Actor"。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ACatFishTankActor> CachedFishTank;

	/** Online 快照广播的配对解绑句柄；Initialize 写入，Deinitialize 消费。 */
	FDelegateHandle OnlineSnapshotHandle;

	/** Widget 动作广播的配对解绑句柄；创建时写入，移除时消费。 */
	FDelegateHandle ActionHandle;

	/** 命令面板意图广播的配对解绑句柄；创建面板时写入，移除时消费。 */
	FDelegateHandle CommandPanelActionHandle;

	/** 最近一次面板点击的分派反馈文本；HandleCommandPanelAction 写入，RefreshSurvivalView 原样送进面板显示，面板销毁时清空。 */
	FString LastCommandPanelFeedback;

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

	/** GameState Run/Environment 完整快照通知的配对解绑句柄。 */
	FDelegateHandle RunChangedHandle;

	/** GameState 最近求助完整快照通知的配对解绑句柄。 */
	FDelegateHandle HelpChangedHandle;
};
