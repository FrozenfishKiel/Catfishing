#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "CatRunEnvironmentDebugSubsystem.generated.h"

class APlayerController;
class STextBlock;
class SWidget;
class UGameViewportClient;

/**
 * RunEnvironmentSocial 的开发期诊断入口；中文面板和 Dump 是只读观察口，读取 GameState 复制事实以及服务器本机一次性生成的 GameMode Debug 快照。
 * 打包 Development 可通过 `cat.RunEnvironmentSocial.Debug 1` 打开 Slate 面板，通过 `cat.RunEnvironmentSocial.Dump` 写一次结构化日志，通过 `cat.RunEnvironmentSocial.DayLength 60`/`SkipToNight`/`SkipToNextDay`/`ForceNextDay` 辅助人工验证；Shipping 不创建本子系统。
 */
UCLASS()
class CATFISHING_API UCatRunEnvironmentDebugSubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** World 创建时只建立调试子系统生命周期；真正的 Slate 面板按 Debug 开关懒创建，避免给正式运行链增加状态订阅。 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	/** World 结束时移除本地 Slate 面板；本子系统不拥有领域对象、玩法委托或额外计时器。 */
	virtual void Deinitialize() override;
	/** 只在非 Shipping 的游戏 World 创建；编辑器资源浏览、CDO 和菜单外 World 不需要该运行时诊断入口。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	/** 每帧只在 Debug 开关打开时刷新本地中文面板并监听 Revision 变化写日志；所有读取都来自 GameState，服务器私有信息只在房主/服务器本机通过只读快照补充。 */
	virtual void Tick(float DeltaTime) override;
	/** 给 Tick 系统提供稳定统计 ID；不代表本子系统拥有独立玩法更新。 */
	virtual TStatId GetStatId() const override;
	/** 该调试面板只服务真实游戏 World；编辑器预览不创建第二套 Run 观察口。 */
	virtual bool IsTickableInEditor() const override { return false; }

private:
	/** 上一次已写入日志的 Run Revision；只用于 Debug 降噪，不参与任何同步、验收或玩法裁决。 */
	int64 LastLoggedRevision = INDEX_NONE;

	/** 当前添加到 GameViewport 的 Slate 面板实例；它只承载文本显示，不保存 Day、Phase、Revision 或任何可被玩法读取的状态。 */
	TSharedPtr<SWidget> DebugPanelWidget;

	/** 面板里的中文文本控件；Tick 只替换它的文本内容，不让 UI 保存任何独立玩法状态。 */
	TSharedPtr<STextBlock> DebugPanelTextBlock;

	/** 当前面板实际挂接的 World 视口；销毁时用同一个视口移除，避免多客户端 PIE 下从错误窗口解绑。 */
	TWeakObjectPtr<UGameViewportClient> DebugPanelViewport;

	/** 确保 Debug 开关打开时存在 Slate 面板、关闭时移除面板；返回后本地视口状态与开关一致。 */
	void RefreshDebugPanelLifecycle();

	/** 创建并挂接左上角可折行 Slate 面板；布局只服务调试文字可读性，面板字体优先选择中文字体。 */
	void CreateDebugPanelWidget();

	/** 从记录的原 GameViewport 移除当前面板并清空控件引用；重复调用保持安全空操作。 */
	void DestroyDebugPanelWidget();

	/** 用当前 World 的公开同步状态刷新面板文字；若本机是服务器再补充服务器私有只读快照，缺 GameState 时显示中文失败原因。 */
	void RefreshDebugPanelText(UWorld* World, APlayerController* Controller);
};
