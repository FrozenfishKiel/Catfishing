#include "Save/CatSaveSettings.h"

// 检查点间隔读取流程：先验证配置为有限正秒数，再把值交给 GameMode；无效值显式禁用，而不回退到隐藏魔法常量。
bool UCatSaveSettings::TryGetCheckpointIntervalSeconds(float& OutIntervalSeconds) const
{
	OutIntervalSeconds = 0.0f;
	if (!FMath::IsFinite(CheckpointIntervalSeconds) || CheckpointIntervalSeconds <= 0.0f)
	{
		return false;
	}
	OutIntervalSeconds = CheckpointIntervalSeconds;
	return true;
}
