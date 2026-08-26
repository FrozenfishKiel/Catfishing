#include "Growth/CatGrowthSettings.h"

// 就绪检查流程：只认显式 gate 和正槽长；Buff 池、数值、文案未裁时不影响经验槽记录，但会停在 PendingChoiceCount。
bool UCatGrowthSettings::IsRuntimeReady() const
{
	return bEnableGrowthRuntime && ExperiencePerChoiceSlot > 0;
}
