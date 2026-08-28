#pragma once

#include "CoreMinimal.h"

class AController;
class APlayerState;
struct FCatWaterSpatialResult;

/**
 * 结构化日志的共享上下文生成器。
 *
 * 这些函数只读取服务器当前事实，不改变玩法状态；字段使用稳定的 Key=Value 形式，便于从打包日志中
 * 对照监听主机与远端客户端。StableNetId 始终遵守 Online 隐私配置，默认只输出有效/已脱敏状态。
 */
namespace CatLogContext
{
	CATFISHING_API FString BuildStableNetIdValue(const APlayerState* PlayerState);
	CATFISHING_API FString BuildControllerFields(const AController* Controller);
	CATFISHING_API FString BuildWaterSpatialFields(
		const TCHAR* Prefix,
		const FVector& QueryLocation,
		const FCatWaterSpatialResult& Result);
}
