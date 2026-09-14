#include "Framework/Game/CatFrontendGameMode.h"

#include "Logging/CatLog.h"

// 构造流程：在类默认对象阶段清空 PawnClass；Frontend Controller 只承载 LocalPlayer UI，不自动生成可操控身体。
ACatFrontendGameMode::ACatFrontendGameMode()
{
	DefaultPawnClass = nullptr;
}

// 启动流程：先让引擎完成 GameMode StartPlay，再记录当前地图和无 Pawn 合同；不会调用 Online 或旅行 API。
void ACatFrontendGameMode::StartPlay()
{
	Super::StartPlay();
	UE_LOG(LogCatfishing, Log, TEXT("Event=frontend_gamemode_ready World=%s DefaultPawn=None"), *GetWorld()->GetMapName());
}

void ACatFrontendGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (ErrorMessage.IsEmpty()) { ErrorMessage = TEXT("CAT_FRONTEND_ADMISSION_NOT_AVAILABLE"); }
	UE_LOG(LogCatOnline, Warning, TEXT("Event=online_frontend_admission_rejected World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s Player=Redacted Result=FrontendAdmissionNotAvailable"),
		*GetNameSafe(GetWorld()), int32(GetNetMode()), HasAuthority(), int32(GetLocalRole()), *GetNameSafe(this));
}
