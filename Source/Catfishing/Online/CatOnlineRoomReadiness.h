#pragma once

#include "Online/CatOnlineTypes.h"

/** UI 与 Host 开始入口共用的只读准入规则；房主点击开始即表示自己准备。 */
namespace CatOnlineRoomReadiness
{
	inline bool CanHostStart(const TArray<FCatOnlineRoomMember>& Members, int32 CurrentPlayers, int32 MaxPlayers)
	{
		if (Members.IsEmpty() || Members.Num() != CurrentPlayers || CurrentPlayers > MaxPlayers) { return false; }
		TSet<FGuid> Seen;
		int32 Owners = 0;
		int32 Locals = 0;
		for (const FCatOnlineRoomMember& Member : Members)
		{
			if (!Member.MemberId.IsValid() || Seen.Contains(Member.MemberId)) { return false; }
			Seen.Add(Member.MemberId);
			Owners += Member.bIsLobbyOwner ? 1 : 0;
			Locals += Member.bIsLocalPlayer ? 1 : 0;
			if (Member.bIsLobbyOwner != Member.bIsLocalPlayer) { return false; }
			if (!Member.bIsLobbyOwner && !Member.bIsReady) { return false; }
		}
		return Owners == 1 && Locals == 1;
	}
}
