#pragma once

#include "CoreMinimal.h"
#include "UI/HUD/CatHUDWidget.h"
#include "UObject/Object.h"
#include "CatHUDModel.generated.h"

class APlayerController;
class ACatCharacter;
class ACatFishingSession;
class UAbilitySystemComponent;
class UCatConditionComponent;
class UCatFishingCommandComponent;
class UCatFishingViewBridge;
class ULocalPlayer;
struct FOnAttributeChangeData;

/** HUD Model 完整投影变化通知；PageController/Subsystem 收到后只渲染 HUD WBP。 */
DECLARE_MULTICAST_DELEGATE(FCatHUDModelChanged);

/** 状态 HUD 的 MVC Model；它只读 Character 状态和钓鱼反馈，不知道背包、商店或图鉴。 */
UCLASS()
class CATFISHING_API UCatHUDModel : public UObject
{
	GENERATED_BODY()

public:
	/** 绑定当前 LocalPlayer、Controller 和 Character；成功后订阅状态变化并发布首份 HUD 投影。 */
	bool Bind(ULocalPlayer* InLocalPlayer, APlayerController* InController, ACatCharacter* InCharacter);

	/** 成对解除 ASC、Condition、Fishing 命令和会话桥订阅，并清空当前 HUD 投影。 */
	void Unbind();

	/** 主动重读当前 HUD 所需只读事实；外部生命周期变化都收敛到这里。 */
	void Refresh();

	/** 返回最近一次 HUD 投影；调用方只能读取，不能写回 Character 或 FishingSession。 */
	const FCatHUDViewState& GetViewState() const;

	/** HUD 投影变化通知；Bind、Refresh、属性变化和 Fishing 结果都会触发。 */
	FCatHUDModelChanged OnViewStateChanged;

private:
	/** ASC 属性变化入口；忽略单项载荷后重读完整 HUD 事实。 */
	void HandleAttributeChanged(const FOnAttributeChangeData& ChangeData);

	/** Condition 快照变化入口；重读完整 HUD 投影。 */
	void HandleConditionChanged();

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

	/** 当前 Character 的 ASC 弱引用；HUD 只读三项数值。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UAbilitySystemComponent> BoundAbilitySystem;

	/** 当前 Character 的 Condition 组件弱引用；HUD 只读离散状态快照。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatConditionComponent> BoundCondition;

	/** 当前 Controller 的 Fishing 命令结果源；HUD 用它显示最近反馈。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatFishingCommandComponent> BoundFishingCommand;

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

	/** FishingViewBridge 投影变化解绑句柄。 */
	FDelegateHandle FishingViewChangedHandle;

	/** 最近钓鱼命令结果；只用于 HUD 反馈文本，不承担请求幂等缓存。 */
	FCatFishingCommandResult LastFishingCommandResult;

	/** 最近是否收到过钓鱼命令结果。 */
	bool bHasFishingCommandResult = false;

	/** 最近发布给 HUD View 的完整投影。 */
	FCatHUDViewState ViewState;
};
