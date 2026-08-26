#include "UI/CatFishingViewBridge.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingSession.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerState.h"

UCatFishingViewBridge* UCatFishingViewBridge::CreateFishingViewBridge(UObject* Outer)
{
	// 工厂流程：优先把桥挂到调用方提供的 UI/Controller Outer 下；没有 Outer 时才退回 transient，结果仍然只是只读投影对象。
	return NewObject<UCatFishingViewBridge>(Outer ? Outer : GetTransientPackage());
}

// 客户端只能看到复制过来的会话；按公开的 FisherPlayerState 匹配，不读任何服务器私有身份。
ACatFishingSession* UCatFishingViewBridge::FindFishingSessionForPlayerState(UObject* WorldContextObject,
	APlayerState* PlayerState)
{
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World || !PlayerState) return nullptr;
	for (TActorIterator<ACatFishingSession> It(World); It; ++It)
	{
		ACatFishingSession* Session = *It;
		if (IsValid(Session) && Session->GetSnapshot().FisherPlayerState == PlayerState && !Session->IsTerminal())
		{
			return Session;
		}
	}
	return nullptr;
}

// 同样只读复制过来的公开事实：Rod 的 OperatorPlayerStates 是完整占位数组，OperatorPlayerState 只代表主位；
// 客户端与服务器看到的是同一份值，不需要（也拿不到）服务器侧的 DeployedRodByPlayerState 索引。
ACatFishingRodActor* UCatFishingViewBridge::FindRodOperatedByPlayerState(UObject* WorldContextObject,
	APlayerState* PlayerState)
{
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World || !PlayerState) return nullptr;
	for (TActorIterator<ACatFishingRodActor> It(World); It; ++It)
	{
		ACatFishingRodActor* Rod = *It;
		if (!IsValid(Rod)) continue;
		const FCatFishingRodPresentationState& State = Rod->GetPresentationState();
		if (State.OperatorPlayerStates.Contains(PlayerState) && State.bDeployed && !State.bBroken)
		{
			return Rod;
		}
	}
	return nullptr;
}

bool UCatFishingViewBridge::BindSession(ACatFishingSession* Session)
{
	// 绑定流程：先消费旧会话委托，再保存新会话弱引用并订阅 Snapshot 变化；首次绑定立即刷新，保证 UI 不等下一次复制事件。
	UnbindSession();
	if (!Session) return false;
	BoundSession = Session;
	SnapshotChangedHandle = Session->OnSnapshotChanged.AddUObject(this, &ThisClass::RefreshFromSession);
	Session->OnDestroyed.AddDynamic(this, &ThisClass::HandleBoundSessionDestroyed);
	Session->OnEndPlay.AddDynamic(this, &ThisClass::HandleBoundSessionEndPlay);
	RefreshFromSession();
	return true;
}

void UCatFishingViewBridge::UnbindSession()
{
	// 解绑流程：只有旧会话仍有效时才用保存的句柄移除委托；Actor 已销毁时清本地状态即可，弱引用不会延长生命周期。
	if (ACatFishingSession* Session = BoundSession.Get())
	{
		Session->OnSnapshotChanged.Remove(SnapshotChangedHandle);
		Session->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleBoundSessionDestroyed);
		Session->OnEndPlay.RemoveDynamic(this, &ThisClass::HandleBoundSessionEndPlay);
	}
	SnapshotChangedHandle.Reset();
	BoundSession.Reset();
	ViewState = FCatFishingViewState();
}

void UCatFishingViewBridge::BeginDestroy()
{
	// UObject 销毁流程：先断开 Session 通知，再让父类继续释放；这样迟到复制回调不会访问半销毁的 UI 桥。
	UnbindSession();
	Super::BeginDestroy();
}

void UCatFishingViewBridge::RefreshFromSession()
{
	// 刷新流程：读取当前会话完整 Snapshot，投影为只读 DTO，然后同时通知 C++ 与蓝图订阅者；会话丢失时不广播伪造状态。
	const ACatFishingSession* Session = BoundSession.Get();
	if (!Session) return;
	ViewState = FCatFishingViewState::FromSnapshot(Session->GetSnapshot());
	OnViewStateChanged.Broadcast(ViewState);
	OnViewStateChangedBP.Broadcast(ViewState);
}

void UCatFishingViewBridge::HandleBoundSessionDestroyed(AActor* DestroyedActor)
{
	// 销毁回调只表示当前绑定会话不再可展示；清空后广播默认 DTO，具体重新查找由 HUD Model 下一次调和负责。
	(void)DestroyedActor;
	UnbindSession();
	OnViewStateChanged.Broadcast(ViewState);
	OnViewStateChangedBP.Broadcast(ViewState);
}

void UCatFishingViewBridge::HandleBoundSessionEndPlay(AActor* Actor, const EEndPlayReason::Type EndPlayReason)
{
	// EndPlay 覆盖关卡切换和 Destroy 生命周期；重复回调会因 BoundSession 已清空而保持幂等。
	(void)Actor;
	(void)EndPlayReason;
	UnbindSession();
	OnViewStateChanged.Broadcast(ViewState);
	OnViewStateChangedBP.Broadcast(ViewState);
}
