#include "Online/CatSteamCodeSearch.h"
#include "Misc/ScopeLock.h"
#if WITH_STEAMWORKS
THIRD_PARTY_INCLUDES_START
#include "steam/steam_api.h"
THIRD_PARTY_INCLUDES_END
#endif

class FCatSteamCodeSearch::FImpl
{
public:
 FCriticalSection Mutex;
 bool bDone = false, bFailed = false;
 TArray<FCatSteamCodeCandidate> Results;
#if WITH_STEAMWORKS
 CCallResult<FImpl, LobbyMatchList_t> Callback;
 void Complete(LobbyMatchList_t* Data, bool bIOFailure)
 {
   FScopeLock Lock(&Mutex);
   bDone = true; bFailed = bIOFailure || !Data || !SteamMatchmaking();
   if (bFailed) { return; }
   for (uint32 Index = 0; Index < Data->m_nLobbiesMatching; ++Index)
   {
     const CSteamID Lobby = SteamMatchmaking()->GetLobbyByIndex(Index);
     if (!Lobby.IsValid()) { continue; }
     FCatSteamCodeCandidate& Candidate = Results.AddDefaulted_GetRef();
     Candidate.LobbyId = Lobby.ConvertToUint64();
     Candidate.Summary.RoomName = UTF8_TO_TCHAR(SteamMatchmaking()->GetLobbyData(Lobby, "CAT_ROOM_NAME_s"));
     Candidate.Summary.OwnerDisplayName = SteamFriends() ? UTF8_TO_TCHAR(SteamFriends()->GetFriendPersonaName(SteamMatchmaking()->GetLobbyOwner(Lobby))) : TEXT("房主");
     Candidate.Summary.CurrentPlayers = SteamMatchmaking()->GetNumLobbyMembers(Lobby);
     Candidate.Summary.MaxPlayers = SteamMatchmaking()->GetLobbyMemberLimit(Lobby);
     Candidate.Summary.bHasPassword = FCStringAnsi::Strcmp(SteamMatchmaking()->GetLobbyData(Lobby, "CAT_PASSWORD_s"), "1") == 0;
     Candidate.Summary.bInProgress = FCStringAnsi::Strcmp(SteamMatchmaking()->GetLobbyData(Lobby, "CAT_GAME_READY_s"), "1") == 0;
     Candidate.Summary.bCanJoin = Candidate.Summary.MaxPlayers > Candidate.Summary.CurrentPlayers;
   }
 }
#endif
};
FCatSteamCodeSearch::FCatSteamCodeSearch() : Impl(MakeUnique<FImpl>()) {}
FCatSteamCodeSearch::~FCatSteamCodeSearch() = default;
bool FCatSteamCodeSearch::Begin(const FString& Route, const FString& Map)
{
#if WITH_STEAMWORKS
 if (!SteamAPI_IsSteamRunning() || !SteamMatchmaking() || Route.Len() != 3) { return false; }
 SteamMatchmaking()->AddRequestLobbyListStringFilter("CAT_PROJECT_s", "Catfishing", k_ELobbyComparisonEqual);
 SteamMatchmaking()->AddRequestLobbyListStringFilter("CAT_PROTOCOL_VERSION_s", "3", k_ELobbyComparisonEqual);
 SteamMatchmaking()->AddRequestLobbyListStringFilter("CAT_MAP_s", TCHAR_TO_UTF8(*Map), k_ELobbyComparisonEqual);
 SteamMatchmaking()->AddRequestLobbyListStringFilter("CAT_CODE_ROUTE_s", TCHAR_TO_UTF8(*Route), k_ELobbyComparisonEqual);
 SteamMatchmaking()->AddRequestLobbyListDistanceFilter(k_ELobbyDistanceFilterWorldwide);
 SteamMatchmaking()->AddRequestLobbyListResultCountFilter(50);
 const SteamAPICall_t Request = SteamMatchmaking()->RequestLobbyList();
 if (Request == k_uAPICallInvalid) { return false; }
 Impl->Callback.Set(Request, Impl.Get(), &FImpl::Complete);
 return true;
#else
 return false;
#endif
}
bool FCatSteamCodeSearch::Poll(bool& bFailed, TArray<FCatSteamCodeCandidate>& Results)
{
 FScopeLock Lock(&Impl->Mutex);
 if (!Impl->bDone) { return false; }
 bFailed = Impl->bFailed; Results = MoveTemp(Impl->Results); return true;
}
