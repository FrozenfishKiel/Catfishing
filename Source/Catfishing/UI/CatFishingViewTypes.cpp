#include "UI/CatFishingViewTypes.h"

FCatFishingViewState FCatFishingViewState::FromSnapshot(const FCatFishingSessionSnapshot& Snapshot)
{
	// 投影流程：逐项复制会话公开快照中允许 UI 展示的字段，并刻意不带出 Actor、组件引用或命令上下文，保持 View 边界只读。
	FCatFishingViewState View;
	View.FishingSessionId = Snapshot.FishingSessionId;
	View.Revision = Snapshot.Revision;
	View.Phase = Snapshot.Phase;
	View.Outcome = Snapshot.Outcome;
	View.PhaseStartedServerTime = Snapshot.PhaseStartedServerTime;
	View.WindowEndsServerTime = Snapshot.WindowEndsServerTime;
	View.FishDefinitionId = Snapshot.FishDefinitionId;
	View.NormalizedFishStamina = Snapshot.NormalizedFishStamina;
	View.PrimaryPowerAlpha = Snapshot.PrimaryPowerAlpha;
	View.ActiveCombinedFishingStrength = Snapshot.ActiveCombinedFishingStrength;
	View.ActiveHelperCount = Snapshot.ActiveHelperCount;
	View.bReeling = Snapshot.bReeling;
	View.bSlacking = Snapshot.bSlacking;
	View.bPerfectHook = Snapshot.bPerfectHook;
	View.FishMotionIntent = Snapshot.FishMotionIntent;
	View.FishLineAlignment = Snapshot.FishLineAlignment;
	View.NormalizedLineLoad = Snapshot.NormalizedLineLoad;
	View.bStrongConfrontation = Snapshot.bStrongConfrontation;
	return View;
}
