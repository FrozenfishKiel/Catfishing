#pragma once

#include "CoreMinimal.h"

class AActor;
class ACatCharacter;
class UCatCampSettings;
struct FCatContainerSnapshot;

namespace CatContainerAccessRules
{
	/** 解析容器宿主的服务器触达半径；优先使用可交互 Actor 自己声明的范围，缺失时才回退营地配置。 */
	CATFISHING_API double ResolveReachRadiusCentimeters(const AActor* Host, const UCatCampSettings* Settings);

	/** 判断当前角色是否仍能触达容器宿主；服务器事务用它统一复核距离，不让各 RPC 各写一套范围规则。 */
	CATFISHING_API bool IsHostReachable(const AActor* Host, const ACatCharacter* Character,
		const UCatCampSettings* Settings);

	/** 查找容器快照中的第一个空槽；一键存缸这类按钮入口用它在服务器侧选择目标格。 */
	CATFISHING_API int32 FindFirstFreeSlot(const FCatContainerSnapshot& Snapshot);
}
