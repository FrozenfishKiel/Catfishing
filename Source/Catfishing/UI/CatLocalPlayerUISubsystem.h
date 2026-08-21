#pragma once

#include "CoreMinimal.h"
#include "Online/CatOnlineTypes.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "CatLocalPlayerUISubsystem.generated.h"

class APlayerController;
class APawn;
class ACatCharacter;
class ACatfishingGameState;
class UCatTravelWidget;
class UCatSurvivalWidget;
class UAbilitySystemComponent;
class UCatConditionComponent;
class UCatEquipmentComponent;
struct FOnAttributeChangeData;

/** 每个 LocalPlayer 的唯一 UI MVC 协调器；分别消费 Online 公共快照与当前 Pawn 的五属性/Condition/Equipment/Run/Help 投影，Controller/World 替换时成对解绑。 */
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

private:
	/** 响应 Online 事实变更；实现重新读取完整 Snapshot，View 不从旧事件参数拼接状态。 */
	void HandleOnlineSnapshotChanged();

	/** 把 View 的 Host/Find/Join/Invite/Leave 意图翻译为 Online 公共接口调用；不直接访问 OSS 或旅行 API。 */
	void HandleActionRequested(ECatOnlineUIAction Action, FGuid OpaqueHandle);

	/** 根据当前 Controller 与完整 Online 快照调和唯一 TravelWidget；直达玩法地图且没有 Session 时移除，其余状态创建或刷新。 */
	void RefreshOnlineWidgetForCurrentController();

	/** 成对解除 View 动作广播并移出视口；空实例和重复调用保持幂等。 */
	void RemoveOnlineWidget();

	/** 弱绑定 Controller 的 GetOnNewPawnNotifier，并立即尝试装配其当前 Pawn；不跨 World 强持 Controller。 */
	void BindController(APlayerController* Controller);

	/** 从旧 Controller 精确移除 Pawn notifier 并清弱引用；Controller 已销毁时只清本地句柄。 */
	void UnbindController();

	/** 当前 Controller Pawn 变化入口；先完整解绑旧身体，再尝试把 NewPawn 作为新的 ACatCharacter Model 来源。 */
	void HandleControllerPawnChanged(APawn* NewPawn);

	/** 当 View gate 与当前 Character 有效时，绑定五属性与 Condition/Equipment/Run/Help 通知，创建 View 并发布首份完整投影。 */
	void AttachSurvivalPawn(ACatCharacter* Character);

	/** 先从旧 ASC 移除五属性 delegate，再解除 Condition/Equipment/GameState 的多源通知，最后释放 View 和全部弱引用。 */
	void DetachSurvivalPawn();

	/** 任一 ASC HUD 属性变化时重新读取全部数值和完整快照，避免 UI 从增量事件拼接 Model。 */
	void HandleSurvivalAttributeChanged(const FOnAttributeChangeData& ChangeData);

	/** Condition、Equipment、Run 或 Help 完整快照变化的无载荷入口；每次都从当前宿主重建整份 HUD ViewState。 */
	void HandleGameplaySnapshotChanged();

	/** 从当前弱绑定 ASC、Condition、Equipment 与 GameState 读取完整投影并只调用 Widget::Render；Widget 不接触玩法宿主。 */
	void RefreshSurvivalView();

	/** 当前 LocalPlayer 唯一 Online 白盒界面；子系统拥有，Controller 变化或销毁时释放。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatTravelWidget> OnlineWidget;

	/** 当前 LocalPlayer 唯一 Lake 状态 View；仅在有 ACatCharacter 且正式 UI gate 开启时存在。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatSurvivalWidget> SurvivalWidget;

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

	/** Online 快照广播的配对解绑句柄；Initialize 写入，Deinitialize 消费。 */
	FDelegateHandle OnlineSnapshotHandle;

	/** Widget 动作广播的配对解绑句柄；创建时写入，移除时消费。 */
	FDelegateHandle ActionHandle;

	/** 当前 Controller Pawn notifier 的配对解绑句柄；Controller 变化或 Deinitialize 时消费。 */
	FDelegateHandle PawnChangedHandle;

	/** Hunger 属性变化 delegate 的配对解绑句柄；Attach 写入，Detach 从同一 ASC/属性键移除。 */
	FDelegateHandle HungerChangedHandle;

	/** Fatigue 属性变化 delegate 的配对解绑句柄；Attach 写入，Detach 从同一 ASC/属性键移除。 */
	FDelegateHandle FatigueChangedHandle;

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
