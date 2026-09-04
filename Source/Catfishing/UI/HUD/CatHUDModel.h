#pragma once

#include "CoreMinimal.h"
#include "TimerManager.h"
#include "UI/HUD/CatHUDWidget.h"
#include "UObject/Object.h"
#include "CatHUDModel.generated.h"

class APlayerController;
class ACatCharacter;
class ACatFishingSession;
class ACatfishingGameState;
class UWorld;
class UAbilitySystemComponent;
class UCatConditionComponent;
class UCatFishingCommandComponent;
class UCatFishingViewBridge;
class UCatGrowthComponent;
class ULocalPlayer;
struct FOnAttributeChangeData;

/** HUD Model 完整投影变化通知；PageController/Subsystem 收到后只渲染 HUD WBP。 */
DECLARE_MULTICAST_DELEGATE(FCatHUDModelChanged);

/** 主 HUD 的 MVC Model；它只读局天数、入口显隐和可选调试事实，不知道背包内容、商店或图鉴页面。 */
UCLASS()
class CATFISHING_API UCatHUDModel : public UObject
{
	GENERATED_BODY()

public:
	/** 绑定当前 LocalPlayer、Controller 和 Character；成功后订阅 Run、状态和钓鱼变化并发布首份 HUD 投影。 */
	bool Bind(ULocalPlayer* InLocalPlayer, APlayerController* InController, ACatCharacter* InCharacter);

	/** 成对解除 Run、ASC、Condition、Fishing 命令和会话桥订阅，并清空当前 HUD 投影。 */
	void Unbind();

	/** 主动重读当前 HUD 所需只读事实；外部生命周期变化都收敛到这里。 */
	void Refresh();

	/** 返回最近一次 HUD 投影；调用方只能读取，不能写回 Character 或 FishingSession。 */
	const FCatHUDViewState& GetViewState() const;

	/** HUD 投影变化通知；Bind、Refresh、属性变化和 Fishing 结果都会触发。 */
	FCatHUDModelChanged OnViewStateChanged;

protected:
	/** UObject 销毁兜底；即使外部没有显式 Unbind，也会收掉 Run 订阅和本地等待 Timer。 */
	virtual void BeginDestroy() override;

private:
	/** ASC 属性变化入口；忽略单项载荷后重读完整 HUD 事实。 */
	void HandleAttributeChanged(const FOnAttributeChangeData& ChangeData);

	/** Condition 快照变化入口；重读完整 HUD 投影。 */
	void HandleConditionChanged();

	/** Growth 快照变化入口；重读完整 HUD 投影。 */
	void HandleGrowthChanged();

	/** Run GameState 观察者接线入口；客户端 GameState 迟到时会本地重试，找到后先读一次公开快照刷新 HUD，再订阅后续变化。 */
	bool RefreshRunGameStateBinding();

	/** Run GameState 解绑入口；移除当前快照通知并停掉等待 GameState 的本地重试。 */
	void ClearRunGameStateBinding();

	/** Run GameState 等待入口；只在 HUD 已绑定玩家但本机还没有 GameState 时短间隔重试，补上订阅后立即停止。 */
	void ScheduleRunGameStateBindingRetry();

	/** Run GameState 等待收口入口；World 切换、解绑或订阅成功后清理本地 Timer。 */
	void ClearRunGameStateBindingRetry();

	/** Run GameState 重试 Tick；发现 GameState 后交给观察者接线入口完成首次刷新和订阅，否则保持等待。 */
	void HandleRunGameStateBindingRetry();

	/** Run 公开快照变化入口；重读天数和阶段相关 HUD 投影，避免客户端复制到达后界面继续显示旧天数。 */
	void HandleRunPublicStateChanged();

	/** Fishing 会话投影变化入口；Bridge 已经更新自身，Model 只重建 HUD 文本。 */
	void HandleFishingViewStateChanged(const FCatFishingViewState& ViewState);

	/** Fishing 命令终态入口；缓存最近结果并重新定位可能新建的 FishingSession。 */
	UFUNCTION()
	void HandleFishingCommandResult(const FCatFishingCommandResult& Result);

	/** 按当前 PlayerState 定位 FishingSession 并调和 FishingViewBridge；找不到时显示无活动会话。 */
	void RefreshFishingSessionBinding();

	/** 当前本地玩家读源；只用于定位 Profile/World 生命周期，不保存领域状态。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ULocalPlayer> BoundLocalPlayer;

	/** 当前 owning Controller 读源；用于定位 Pawn、PlayerState 和 Fishing 命令结果源。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> BoundPlayerController;

	/** 当前 HUD 对应的猫身体读源；只用于解析猫种类体力基线，不从这里写 Character 状态。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ACatCharacter> BoundCharacter;

	/** 当前 Character 的 ASC 弱引用；HUD 只读三项数值。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UAbilitySystemComponent> BoundAbilitySystem;

	/** 当前 Character 的 Condition 组件弱引用；HUD 只读离散状态快照。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatConditionComponent> BoundCondition;

	/** 当前 Character 的 Growth 组件弱引用；HUD 只读经验和待选次数，不提交成长选择。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatGrowthComponent> BoundGrowth;

	/** 当前 Controller 的 Fishing 命令结果源；HUD 用它显示最近反馈。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatFishingCommandComponent> BoundFishingCommand;

	/** 当前 World 的 Run 公开事实源；HUD 只订阅它的变化通知，不在本地推进天数或阶段。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ACatfishingGameState> BoundRunGameState;

	/** 当前 HUD 使用的 Fishing 只读桥；它把 FishingSession 复制快照投成 HUD 可读 DTO。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatFishingViewBridge> FishingViewBridge;

	/** Poison 属性变化解绑句柄。 */
	FDelegateHandle PoisonChangedHandle;

	/** FishingStrength 属性变化解绑句柄。 */
	FDelegateHandle FishingStrengthChangedHandle;

	/** FightStamina 属性变化解绑句柄。 */
	FDelegateHandle FightStaminaChangedHandle;

	/** Condition 快照变化解绑句柄。 */
	FDelegateHandle ConditionChangedHandle;

	/** Growth 快照变化解绑句柄。 */
	FDelegateHandle GrowthChangedHandle;

	/** Run 公开快照变化解绑句柄。 */
	FDelegateHandle RunPublicStateChangedHandle;

	/** 等待客户端 GameState 出现的本地 Timer；它只保证 HUD 订阅接上线，不保存 Run 天数或阶段。 */
	FTimerHandle RunGameStateBindingRetryTimerHandle;

	/** 当前 GameState 等待 Timer 所属的 World；清理时用它回到创建 Timer 的 TimerManager，避免切图后清错 World。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UWorld> RunGameStateBindingRetryWorld;

	/** FishingViewBridge 投影变化解绑句柄。 */
	FDelegateHandle FishingViewChangedHandle;

	/** 最近钓鱼命令结果；只用于 HUD 反馈文本，不承担请求幂等缓存。 */
	FCatFishingCommandResult LastFishingCommandResult;

	/** 最近是否收到过钓鱼命令结果。 */
	bool bHasFishingCommandResult = false;

	/** 最近发布给 HUD View 的完整投影。 */
	FCatHUDViewState ViewState;
};
