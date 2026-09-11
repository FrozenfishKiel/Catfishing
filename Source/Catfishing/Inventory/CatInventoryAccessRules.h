#pragma once

#include "CoreMinimal.h"

class AActor;
class ACatCharacter;
class UCatCampSettings;

namespace CatInventoryAccessRules
{
	/** 解析库存宿主的服务器触达半径；优先使用可交互 Actor 自己声明的范围，缺失时才回退营地配置。 */
	CATFISHING_API double ResolveReachRadiusCentimeters(const AActor* Host, const UCatCampSettings* Settings);

	/** 判断当前角色是否仍能触达库存宿主；服务器库存事务用它统一复核距离，不让各 RPC 各写一套范围规则。 */
	CATFISHING_API bool IsHostReachable(const AActor* Host, const ACatCharacter* Character,
		const UCatCampSettings* Settings);
}
