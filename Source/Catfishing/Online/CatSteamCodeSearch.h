#pragma once
#include "CoreMinimal.h"
#include "Online/CatOnlineTypes.h"

struct FCatSteamCodeCandidate
{
 uint64 LobbyId = 0;
 FCatSessionSearchSummary Summary;
};
/** Steam OSS 的普通浏览固定默认地域；邀请码用平台全球过滤查询，回调只写受锁保护的结果。 */
class FCatSteamCodeSearch
{
public:
 FCatSteamCodeSearch();
 ~FCatSteamCodeSearch();
 bool Begin(const FString& Route, const FString& Map);
 bool Poll(bool& bFailed, TArray<FCatSteamCodeCandidate>& Results);
private:
 class FImpl;
 TUniquePtr<FImpl> Impl;
};
