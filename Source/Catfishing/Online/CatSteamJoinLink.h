#pragma once

#include "CoreMinimal.h"

/** 严格解析本游戏的 Steam Lobby 输入；不会把用户输入直接交给操作系统。 */
namespace CatSteamJoinLink
{
	bool Parse(const FString& Input, uint32 AppId, uint64& OutLobbyId);
}

/** 一次 Lobby 元数据查询；只生成经平台确认的 URI，不加入 Lobby 或维护玩法状态。 */
class FCatSteamJoinLink
{
public:
	FCatSteamJoinLink();
	~FCatSteamJoinLink();
	bool Begin(uint64 LobbyId);
	bool HasFailed() const;
	FString TakeReadyUri();
	uint64 GetLobbyId() const;
private:
	class FImpl;
	TUniquePtr<FImpl> Impl;
};
