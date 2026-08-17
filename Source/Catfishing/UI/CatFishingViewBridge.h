#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UI/CatFishingViewTypes.h"
#include "CatFishingViewBridge.generated.h"

class ACatFishingSession;

/** Read-only adapter from replicated Session facts to a UI-owned DTO/delegate. */
UCLASS()
class CATFISHING_API UCatFishingViewBridge : public UObject
{
	GENERATED_BODY()
public:
	bool BindSession(ACatFishingSession* Session);
	void UnbindSession();
	const FCatFishingViewState& GetViewState() const { return ViewState; }
	FCatFishingViewStateChanged OnViewStateChanged;

protected:
	virtual void BeginDestroy() override;

private:
	void RefreshFromSession();
	TWeakObjectPtr<ACatFishingSession> BoundSession;
	FDelegateHandle SnapshotChangedHandle;
	FCatFishingViewState ViewState;
};
