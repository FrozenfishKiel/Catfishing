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

/** Read-only adapter from replicated Session facts to a UI-owned DTO/delegate. 蓝图只读，不提供任何写口。 */
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

	UFUNCTION(BlueprintCallable, Category = "Catfishing|Fishing|View")
	bool BindSession(ACatFishingSession* Session);

	UFUNCTION(BlueprintCallable, Category = "Catfishing|Fishing|View")
	void UnbindSession();

	UFUNCTION(BlueprintPure, Category = "Catfishing|Fishing|View")
	const FCatFishingViewState& GetViewState() const { return ViewState; }

	UFUNCTION(BlueprintPure, Category = "Catfishing|Fishing|View")
	ACatFishingSession* GetBoundSession() const { return BoundSession.Get(); }

	/** C++ 订阅入口（非动态）。 */
	FCatFishingViewStateChanged OnViewStateChanged;

	/** 蓝图订阅入口。 */
	UPROPERTY(BlueprintAssignable, Category = "Catfishing|Fishing|View")
	FCatFishingViewStateChangedDynamic OnViewStateChangedBP;

protected:
	virtual void BeginDestroy() override;

private:
	void RefreshFromSession();
	TWeakObjectPtr<ACatFishingSession> BoundSession;
	FDelegateHandle SnapshotChangedHandle;
	FCatFishingViewState ViewState;
};
