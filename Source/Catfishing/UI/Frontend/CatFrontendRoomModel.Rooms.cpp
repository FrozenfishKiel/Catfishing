#include "UI/Frontend/CatFrontendRoomModel.h"
#include "Online/CatOnlineSubsystem.h"

bool UCatFrontendRoomModel::IsFindingPublicRooms() const
{
 const UCatOnlineSubsystem* Source = Online.Get();
 return Source && Source->IsFindingPublicRooms();
}

FCatOnlineResult UCatFrontendRoomModel::RefreshPublicRooms()
{
 FCatOnlineResult Result;
 if (UCatOnlineSubsystem* Source = Online.Get()) { Result = Source->RequestFindSessions(); }
 else { Result.Error = ECatOnlineError::OnlineSubsystemUnavailable; }
 CaptureResult(Result); return Result;
}
FCatOnlineResult UCatFrontendRoomModel::JoinPublicRoom(FCatSessionSearchHandle Handle)
{
 FCatOnlineResult Result;
 if (UCatOnlineSubsystem* Source = Online.Get()) { Result = Source->RequestJoinSession(Handle); }
 else { Result.Error = ECatOnlineError::OnlineSubsystemUnavailable; }
 CaptureResult(Result); return Result;
}
FCatOnlineResult UCatFrontendRoomModel::SubmitPassword(const FString& Password)
{
 FCatOnlineResult Result;
 if (UCatOnlineSubsystem* Source = Online.Get()) { Result = Source->RequestSubmitRoomPassword(Password); }
 else { Result.Error = ECatOnlineError::OnlineSubsystemUnavailable; }
 CaptureResult(Result); return Result;
}
void UCatFrontendRoomModel::CancelPassword()
{
 if (UCatOnlineSubsystem* Source = Online.Get()) { Source->CancelRoomPassword(); }
 HandleOnlineChanged();
}
FCatOnlineResult UCatFrontendRoomModel::UpdateRoomSettings(const FString& Name, int32 Capacity, ECatSessionAccessPolicy Access, const FString& Password, bool bClearPassword)
{
 FCatOnlineResult Result;
 if (UCatOnlineSubsystem* Source = Online.Get()) { Result = Source->RequestUpdateRoomSettings(Name, Capacity, Access, Password, bClearPassword); }
 else { Result.Error = ECatOnlineError::OnlineSubsystemUnavailable; }
 CaptureResult(Result); return Result;
}
