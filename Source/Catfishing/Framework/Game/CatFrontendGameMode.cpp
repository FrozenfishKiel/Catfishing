#include "Framework/Game/CatFrontendGameMode.h"

#include "Engine/World.h"
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
