#include "UI/CatFishingViewBridge.h"

#include "Fishing/CatFishingSession.h"

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
}
