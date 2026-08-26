#include "UI/CatFishingViewTypes.h"

FCatFishingViewState FCatFishingViewState::FromSnapshot(const FCatFishingSessionSnapshot& Snapshot)
{
	FCatFishingViewState View;
	View.FishingSessionId = Snapshot.FishingSessionId;
	View.Revision = Snapshot.Revision;
	View.Phase = Snapshot.Phase;
	View.Outcome = Snapshot.Outcome;
	View.FishDefinitionId = Snapshot.FishDefinitionId;
	View.NormalizedFishStamina = Snapshot.NormalizedFishStamina;
	View.bReeling = Snapshot.bReeling;
	View.bSlacking = Snapshot.bSlacking;
	View.bPerfectHook = Snapshot.bPerfectHook;
	View.FishMotionIntent = Snapshot.FishMotionIntent;
	View.FishLineAlignment = Snapshot.FishLineAlignment;
	View.NormalizedLineLoad = Snapshot.NormalizedLineLoad;
	View.bStrongConfrontation = Snapshot.bStrongConfrontation;
	return View;
}
