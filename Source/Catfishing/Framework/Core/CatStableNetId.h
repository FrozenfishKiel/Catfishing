#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"

/**
 * StableNetId 是整个项目里"服务器认定的这个玩家是谁"的唯一表达：它取自 APlayerState 继承的 UniqueId，
 * 是幂等键 `StableNetId + CommandType + AggregateId + RequestId` 的第一段，也是所有服务器私有协议记录、
 * 终态缓存和冷却表的主人字段。
 *
 * 这个文件存在的原因是它必须只有一份推导规则。此前 Social、Collection、Run、Integration/Fishing、
 * Fishing/Session、Fishing/Service 各写了一份逐字相同的实现，Camp 与 PlayerController 还有若干处内联同一
 * 表达式；一旦要把原始平台 ID 换成脱敏值或哈希，改漏任何一处都会让两个玩家的记录互相串号，而构建和测试都
 * 不会报错。所有新入口都必须调这里，不要再在领域里重抄。
 *
 * 客户端提交的身份字符串永远不可信；调用者拿到的必须是从服务器侧 Controller/PlayerState 现场重建的结果。
 */

/**
 * 从已复制的 FUniqueNetIdRepl 解析服务器私有身份字符串。
 * 无效 ID 返回空串，调用者必须把空串当作"身份不可用"并 fail-closed，不能当成一个合法的匿名主人。
 */
inline FString CatResolveStableNetId(const FUniqueNetIdRepl& UniqueId)
{
	return UniqueId.IsValid() ? UniqueId->ToString() : FString();
}

/**
 * 从 PlayerState 解析服务器私有身份字符串。
 * PlayerState 为空或其 UniqueId 无效时返回空串，语义与上面的重载一致。
 */
inline FString CatResolveStableNetId(const APlayerState* PlayerState)
{
	return PlayerState ? CatResolveStableNetId(PlayerState->GetUniqueId()) : FString();
}

/**
 * 从 Controller 解析服务器私有身份字符串；这是各领域命令入口最常用的形态。
 * Controller 为空、尚未装配 PlayerState 或 UniqueId 无效时返回空串。
 */
inline FString CatResolveStableNetId(const AController* Controller)
{
	return Controller ? CatResolveStableNetId(Controller->PlayerState) : FString();
}

/**
 * 在指定 World 里按服务器私有身份反查当前活动的 Controller；找不到返回 nullptr。
 * 只比较现场重建的身份，不使用玩家名、网络地址或此前缓存的 Controller 指针，因此断线重连换了新 Controller
 * 之后仍能定位到同一个人，而旧的失效 Controller 不会被误认。
 */
inline AController* CatFindControllerByStableNetId(const UWorld* World, const FString& StableNetId)
{
	if (!World || StableNetId.IsEmpty())
	{
		return nullptr;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		AController* Controller = It->Get();
		if (CatResolveStableNetId(Controller) == StableNetId)
		{
			return Controller;
		}
	}
	return nullptr;
}
