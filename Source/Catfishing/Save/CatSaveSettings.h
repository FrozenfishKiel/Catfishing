#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatSaveSettings.generated.h"

/** 世界槽持久化的运行时节奏配置；它只定义检查点间隔，不承载槽内容、玩家状态或磁盘路径。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing World Save"))
class CATFISHING_API UCatSaveSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	/** 读取可用的自动检查点间隔，单位秒；非有限或非正配置会禁用定期写盘，避免以错误频率刷磁盘。 */
	bool TryGetCheckpointIntervalSeconds(float& OutIntervalSeconds) const;

	/** 两次成功采样之间等待的世界秒数；GameMode 在已恢复玩法 World 中读取它安排检查点。 */
	UPROPERTY(Config, EditAnywhere, Category = "Persistence", meta = (ClampMin = "1.0", Units = "s"))
	float CheckpointIntervalSeconds = 180.0f;
};
