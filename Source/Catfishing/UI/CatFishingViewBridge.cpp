#include "UI/CatFishingViewBridge.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingSession.h"
#include "GameFramework/PlayerState.h"

UCatFishingViewBridge* UCatFishingViewBridge::CreateFishingViewBridge(UObject* Outer)
{
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
	UnbindSession();
	if (!Session) return false;
	BoundSession = Session;
	SnapshotChangedHandle = Session->OnSnapshotChanged.AddUObject(this, &ThisClass::RefreshFromSession);
	RefreshFromSession();
	return true;
}

void UCatFishingViewBridge::UnbindSession()
{
	if (ACatFishingSession* Session = BoundSession.Get())
	{
		Session->OnSnapshotChanged.Remove(SnapshotChangedHandle);
	}
	SnapshotChangedHandle.Reset();
	BoundSession.Reset();
}

void UCatFishingViewBridge::BeginDestroy()
{
	UnbindSession();
	Super::BeginDestroy();
}

void UCatFishingViewBridge::RefreshFromSession()
{
	const ACatFishingSession* Session = BoundSession.Get();
	if (!Session) return;
	ViewState = FCatFishingViewState::FromSnapshot(Session->GetSnapshot());
	OnViewStateChanged.Broadcast(ViewState);
	OnViewStateChangedBP.Broadcast(ViewState);
}
