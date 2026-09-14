#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CatFrontendGameMode.generated.h"

/** 前台专用模式；明确不生成默认 Pawn，只承载 LocalPlayer Online UI。 */
UCLASS()
class CATFISHING_API ACatFrontendGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	/** 在类默认对象上关闭默认 Pawn 生成；菜单保留 Controller 承载 LocalPlayer UI。 */
	ACatFrontendGameMode();
	/** Frontend 开始玩法时记录地图和无 Pawn 装配；不创建 Session 或直接旅行。 */
	virtual void StartPlay() override;
	/** 准备房间尚未迁移到 UE 准入，远端仍走 Steam Lobby；提前监听不得放行绕过现有流程的直接连接。 */
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
};
