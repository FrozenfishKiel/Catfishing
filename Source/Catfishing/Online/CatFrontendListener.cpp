#include "Online/CatFrontendListener.h"

#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "Logging/CatLog.h"

bool FCatFrontendListener::IsListening(const UWorld* World) const
{
	return World && ListeningWorld.Get() == World && ListeningDriver.IsValid()
		&& World->GetNetDriver() == ListeningDriver.Get() && World->GetNetMode() == NM_ListenServer;
}

bool FCatFrontendListener::Start(UWorld* World, const FGuid& RequestId, const uint64 Epoch)
{
	if (IsListening(World)) { return true; }
	if (!GEngine || !World || !GEngine->GetWorldContextFromWorld(World) || World->GetNetDriver()
		|| ListeningDriver.IsValid() || World->GetNetMode() != NM_Standalone)
	{
		UE_LOG(LogCatOnline, Warning, TEXT("Event=online_frontend_listen_rejected RequestId=%s Epoch=%llu World=%s NetMode=%d Reason=InvalidWorldOrExistingDriver"),
			*RequestId.ToString(), Epoch, *GetNameSafe(World), World ? int32(World->GetNetMode()) : -1);
		return false;
	}

	// UWorld::Listen 失败会同步广播 NetworkFailure；调用方在此期间仅消费 Listen 的返回值，避免重入清理。
	TGuardValue<bool> ChangingDriver(bChangingDriver, true);
	FURL ListenUrl = World->URL;
	ListenUrl.AddOption(TEXT("listen"));
	if (!World->Listen(ListenUrl))
	{
		UE_LOG(LogCatOnline, Warning, TEXT("Event=online_frontend_listen_failed RequestId=%s Epoch=%llu World=%s NetMode=%d Reason=InitListenFailed"),
			*RequestId.ToString(), Epoch, *GetNameSafe(World), int32(World->GetNetMode()));
		return false;
	}
	ListeningWorld = World;
	ListeningDriver = World->GetNetDriver();
	if (!IsListening(World))
	{
		Stop(TEXT("UnexpectedNetMode"), RequestId, Epoch);
		return false;
	}
	UE_LOG(LogCatOnline, Log, TEXT("Event=online_frontend_listen_started RequestId=%s Epoch=%llu World=%s NetMode=%d Authority=1 Driver=%s Result=Listening"),
		*RequestId.ToString(), Epoch, *GetNameSafe(World), int32(World->GetNetMode()), *GetNameSafe(ListeningDriver.Get()));
	return true;
}

void FCatFrontendListener::Stop(const TCHAR* Reason, const FGuid& RequestId, const uint64 Epoch)
{
	UWorld* World = ListeningWorld.Get();
	UNetDriver* Driver = ListeningDriver.Get();
	ListeningWorld.Reset();
	ListeningDriver.Reset();
	if (!GEngine || !World || !Driver || World->GetNetDriver() != Driver
		|| !GEngine->GetWorldContextFromWorld(World) || GEngine->FindNamedNetDriver(World, Driver->NetDriverName) != Driver)
	{
		return;
	}
	TGuardValue<bool> ChangingDriver(bChangingDriver, true);
	UE_LOG(LogCatOnline, Log, TEXT("Event=online_frontend_listen_stopped RequestId=%s Epoch=%llu World=%s NetMode=%d Authority=1 Driver=%s Reason=%s"),
		*RequestId.ToString(), Epoch, *GetNameSafe(World), int32(World->GetNetMode()), *GetNameSafe(Driver), Reason);
	// 精确销毁本次持有的驱动；不调用会同时关闭 Replay/其他驱动的 ShutdownWorldNetDriver。
	GEngine->DestroyNamedNetDriver(World, Driver->NetDriverName);
}
