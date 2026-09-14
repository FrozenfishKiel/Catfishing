#include "Online/CatSteamJoinLink.h"
#include "Misc/ScopeLock.h"

#if WITH_STEAMWORKS
THIRD_PARTY_INCLUDES_START
#include "steam/steam_api.h"
THIRD_PARTY_INCLUDES_END
#endif

namespace CatSteamJoinLink
{
	static bool ParseNumber(const FString& Text, uint64& Out)
	{
		Out = 0;
		if (Text.IsEmpty() || Text.Len() > 20) { return false; }
		for (TCHAR C : Text)
		{
			if (C < TEXT('0') || C > TEXT('9')) { return false; }
			const uint64 Digit = C - TEXT('0');
			if (Out > (MAX_uint64 - Digit) / 10) { return false; }
			Out = Out * 10 + Digit;
		}
		return Out != 0;
	}

	bool Parse(const FString& Input, uint32 AppId, uint64& OutLobbyId)
	{
		OutLobbyId = 0;
		FString Text = Input.TrimStartAndEnd();
		if (Text.Len() > 160 || AppId == 0) { return false; }
		if (Text.StartsWith(TEXT("steam://joinlobby/"), ESearchCase::IgnoreCase))
		{
			Text.RightChopInline(18);
			TArray<FString> Parts;
			Text.ParseIntoArray(Parts, TEXT("/"), false);
			uint64 ParsedApp = 0, Owner = 0;
			if ((Parts.Num() != 2 && Parts.Num() != 3) || !ParseNumber(Parts[0], ParsedApp) || ParsedApp != AppId
				|| (Parts.Num() == 3 && !ParseNumber(Parts[2], Owner))) { return false; }
			Text = Parts[1];
		}
		uint64 Lobby = 0;
		if (!ParseNumber(Text, Lobby)) { return false; }
#if WITH_STEAMWORKS
		if (!CSteamID(Lobby).IsValid() || !CSteamID(Lobby).IsLobby()) { return false; }
#else
		return false;
#endif
		OutLobbyId = Lobby;
		return true;
	}
}

class FCatSteamJoinLink::FImpl
{
public:
	mutable FCriticalSection Mutex;
	uint64 Lobby = 0;
	bool bFailed = false;
	FString Uri;
#if WITH_STEAMWORKS
	FImpl() : Callback(this, &FImpl::OnData) {}
	CCallback<FImpl, LobbyDataUpdate_t> Callback;
	void OnData(LobbyDataUpdate_t* Data)
	{
		// Steam OSS 在异步线程驱动 SDK 回调；游戏线程只读取受锁保护的查询结果。
		FScopeLock Lock(&Mutex);
		if (Data->m_ulSteamIDLobby != Lobby || Data->m_ulSteamIDMember != Lobby) { return; }
		if (!Data->m_bSuccess || !SteamMatchmaking() || !SteamUtils()) { bFailed = true; return; }
		const CSteamID Owner = SteamMatchmaking()->GetLobbyOwner(CSteamID(Lobby));
		if (!Owner.IsValid()) { bFailed = true; return; }
		Uri = FString::Printf(TEXT("steam://joinlobby/%u/%llu/%llu"), SteamUtils()->GetAppID(), Lobby, Owner.ConvertToUint64());
	}
#endif
};

FCatSteamJoinLink::FCatSteamJoinLink() : Impl(MakeUnique<FImpl>()) {}
FCatSteamJoinLink::~FCatSteamJoinLink() = default;
bool FCatSteamJoinLink::Begin(uint64 LobbyId)
{
	{ FScopeLock Lock(&Impl->Mutex); Impl->Lobby = LobbyId; }
#if WITH_STEAMWORKS
	return SteamAPI_IsSteamRunning() && SteamMatchmaking() && SteamMatchmaking()->RequestLobbyData(CSteamID(LobbyId));
#else
	return false;
#endif
}
bool FCatSteamJoinLink::HasFailed() const { FScopeLock Lock(&Impl->Mutex); return Impl->bFailed; }
FString FCatSteamJoinLink::TakeReadyUri() { FScopeLock Lock(&Impl->Mutex); FString Uri = MoveTemp(Impl->Uri); Impl->Uri.Reset(); return Uri; }
uint64 FCatSteamJoinLink::GetLobbyId() const { FScopeLock Lock(&Impl->Mutex); return Impl->Lobby; }
