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
};
