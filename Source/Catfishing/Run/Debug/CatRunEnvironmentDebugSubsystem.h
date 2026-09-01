#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "CatRunEnvironmentDebugSubsystem.generated.h"

class APlayerController;

/**
 * RunEnvironmentSocial 的开发期诊断入口；常驻面板和 Dump 只读 GameState 复制事实，DayLength 指令只请求服务器 GameMode 重排当前白天。
 * 打包 Development 可通过 `cat.RunEnvironmentSocial.Debug 1` 打开屏幕信息，通过 `cat.RunEnvironmentSocial.Dump` 写一次结构化日志，通过 `cat.RunEnvironmentSocial.DayLength 60` 调整当前白天剩余时长；Shipping 不创建本子系统。
 */
UCLASS()
class CATFISHING_API UCatRunEnvironmentDebugSubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** World 创建时不做状态订阅；Debug 开关由 Console Variable 低频读取，避免给正式运行链增加生命周期依赖。 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	/** World 结束时仅释放父类资源；本子系统不拥有任何领域对象或委托绑定。 */
	virtual void Deinitialize() override;
	/** 只在非 Shipping 的游戏 World 创建；编辑器资源浏览、CDO 和菜单外 World 不需要该运行时诊断入口。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	/** 每帧只在 Debug 开关打开时读取当前复制快照并刷新屏幕行；Revision 变化时额外写一条日志便于双端对账。 */
	virtual void Tick(float DeltaTime) override;
	/** 给 Tick 系统提供稳定统计 ID；不代表本子系统拥有独立玩法更新。 */
	virtual TStatId GetStatId() const override;
	/** 该调试面板只服务真实游戏 World；编辑器预览不创建第二套 Run 观察口。 */
	virtual bool IsTickableInEditor() const override { return false; }

private:
	/** 上一次已写入日志的 Run Revision；只用于 Debug 降噪，不参与任何同步、验收或玩法裁决。 */
	int64 LastLoggedRevision = INDEX_NONE;
};
