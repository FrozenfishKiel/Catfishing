#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UI/CatFishingViewTypes.h"
#include "CatFishingViewBridge.generated.h"

class ACatFishingRodActor;
class ACatFishingSession;
class APlayerState;

/** 蓝图可绑定的会话视图变化通知；每次复制快照变化广播一次完整只读 DTO。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCatFishingViewStateChangedDynamic, const FCatFishingViewState&, ViewState);

/** FishingSession 复制事实到 UI DTO 的只读桥；它只绑定会话快照通知并广播展示副本，不提供命令、结算或状态写入口。 */
UCLASS(BlueprintType)
class CATFISHING_API UCatFishingViewBridge : public UObject
{
	GENERATED_BODY()
public:
	/** 蓝图工厂：在 Outer 上创建一个桥对象（通常传 Controller 或 HUD）。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Fishing|View", meta = (DefaultToSelf = "Outer"))
	static UCatFishingViewBridge* CreateFishingViewBridge(UObject* Outer);

	/** 在客户端按 PlayerState 查找当前复制过来的钓鱼会话 Actor；找不到返回空。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Fishing|View", meta = (WorldContext = "WorldContextObject"))
	static ACatFishingSession* FindFishingSessionForPlayerState(UObject* WorldContextObject, APlayerState* PlayerState);

	/**
	 * 在客户端查找这名玩家当前正在操作的鱼竿；没占任何竿位时返回空。
	 *
	 * 存在的理由：权威侧的等价查询是 UCatFishingService::FindRodOperatedBy，但那个 WorldSubsystem
	 * 在客户端压根不创建（ShouldCreateSubsystem 里 NetMode != NM_Client），表现层够不着。
	 * Rod Actor 及其 PresentationState.OperatorPlayerState 是复制的，所以客户端自己扫一遍即可。
	 *
	 * 典型用途：左键按下的表现钩子要区分"我在竿位上准备甩竿"和"我只是站着按了下左键"——
	 * 后者服务器不会有任何反应，播举竿动画就是表现骗玩家。
	 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Fishing|View", meta = (WorldContext = "WorldContextObject"))
	static ACatFishingRodActor* FindRodOperatedByPlayerState(UObject* WorldContextObject, APlayerState* PlayerState);

	/** 绑定一个客户端可见的 FishingSession，并立即发布首份只读 ViewState；调用前会先解绑旧会话，传空时返回 false。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Fishing|View")
	bool BindSession(ACatFishingSession* Session);

	/** 移除当前会话的快照委托并清空弱引用；Detach、换会话和对象销毁都可以重复调用它保持生命周期配对。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Fishing|View")
	void UnbindSession();

	/** 返回最近一次从会话快照投影出的只读状态；没有绑定会话时保持默认空状态。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Fishing|View")
	const FCatFishingViewState& GetViewState() const { return ViewState; }

	/** 返回当前弱绑定的 FishingSession；调用者只能用它判断显示来源，不能把 Bridge 当会话所有者。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Fishing|View")
	ACatFishingSession* GetBoundSession() const { return BoundSession.Get(); }

	/** C++ 订阅入口（非动态）。 */
	FCatFishingViewStateChanged OnViewStateChanged;

	/** 蓝图订阅入口。 */
	UPROPERTY(BlueprintAssignable, Category = "Catfishing|Fishing|View")
	FCatFishingViewStateChangedDynamic OnViewStateChangedBP;

protected:
	/** UObject 销毁边界；销毁前必须解绑会话委托，避免迟到 Snapshot 通知打到已释放的桥对象。 */
	virtual void BeginDestroy() override;

private:
	/** 从当前绑定会话重建完整 ViewState 并广播；会话失效时直接返回，不制造 UI 私有终态。 */
	void RefreshFromSession();

	/** 当前 Bridge 正在观察的 FishingSession；弱引用由 BindSession 写入、UnbindSession 清空，不延长会话生命周期。 */
	TWeakObjectPtr<ACatFishingSession> BoundSession;

	/** 绑定到当前 Session OnSnapshotChanged 的句柄；只与 BoundSession 成对使用，换会话或销毁时消费。 */
	FDelegateHandle SnapshotChangedHandle;

	/** 最近一次成功投影的只读 DTO；RefreshFromSession 是唯一写者，Widget、蓝图和协调器只读取或订阅它。 */
	FCatFishingViewState ViewState;
};
