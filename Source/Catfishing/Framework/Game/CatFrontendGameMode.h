#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CatFrontendGameMode.generated.h"

/**
 * 前台（主菜单）地图的服务器宿主。
 *
 * 它拥有的真相只有一条：前台没有可操控身体。所以它在类默认对象上把 DefaultPawnClass 清成 nullptr，
 * 保证进入前台地图只生成 Controller，由 Controller 承载 LocalPlayer 的 Online UI。
 *
 * 它不拥有：Session 的创建/查找/加入（那是 UCatOnlineSubsystem 的事）、地图旅行（由 Online 发起）、
 * 任何一局的玩法状态。前台不产生也不保存 Run、Items、Social 之类的领域事实，
 * 因此这个文件不依赖任何领域类型。
 */
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
