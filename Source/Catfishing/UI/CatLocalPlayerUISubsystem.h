#pragma once

#include "CoreMinimal.h"
#include "Online/CatOnlineTypes.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "CatLocalPlayerUISubsystem.generated.h"

class APlayerController;
class APawn;
class ACatCharacter;
class UCatLakeReachModel;
class UCatLakeReachPageController;
class UCatLakeReachWidget;
class UCatTravelWidget;

/** 每个 LocalPlayer 的 UI 根模块；负责 Frontend 旅行面板和 LakeReach MVC 对象生命周期，不直接聚合玩法 Query 或渲染布局。 */
UCLASS()
class CATFISHING_API UCatLocalPlayerUISubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

#if WITH_DEV_AUTOMATION_TESTS
	friend class FCatLocalPlayerUISubsystemLakeReachAttachTest;
	friend class FCatLocalPlayerUISubsystemOnlineWidgetPolicyTest;
#endif

public:
	/** 订阅 GameInstance Online 快照，绑定当前 Controller Pawn notifier，并按当前 Pawn 尝试装配 LakeReach MVC。 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** 先移除 LakeReach MVC 与 Controller 绑定，再移除 Online View 和快照订阅，保证 LocalPlayer 销毁后没有迟到 UI 更新。 */
	virtual void Deinitialize() override;

	/** Controller 替换时先从旧 Controller 恢复并清理 UI，再弱绑定新 Controller 并装配其当前 Pawn。 */
	virtual void PlayerControllerChanged(APlayerController* NewController) override;

	/** 切换当前 LocalPlayer 的 Lake 菜单；实际输入模式、焦点和鼠标由 LakeReach PageController 管理。 */
	void ToggleLakeMenu();

	/** 返回当前 Lake 菜单是否由 LakeReach PageController 保持打开；无页面时固定为 false。 */
	bool IsLakeMenuOpen() const;

private:
	/** 响应 Online 事实变更；实现重新读取完整 Snapshot，并同步 Frontend 面板与 LakeReach Model。 */
	void HandleOnlineSnapshotChanged();

	/** 把 Frontend Travel View 的 Host/Find/Join/Invite/Leave 意图翻译为 Online 公共接口调用。 */
	void HandleActionRequested(ECatOnlineUIAction Action, FGuid OpaqueHandle);

	/** 根据当前 Controller 与完整 Online 快照调和唯一 TravelWidget；只有 Frontend 或进入 Lake 前的旅行态会创建或刷新。 */
	void RefreshOnlineWidgetForCurrentController();

	/** 判断 Frontend 旅行面板是否仍属于当前屏幕；Lake、回 Frontend 途中和异常 World 都不显示 Host/Find/Join 白盒。 */
	static bool ShouldShowOnlineTravelWidget(const FCatOnlineSnapshot& Snapshot);

	/** 成对解除 Frontend View 动作广播并移出视口；空实例和重复调用保持幂等。 */
	void RemoveOnlineWidget();

	/** 弱绑定 Controller 的 GetOnNewPawnNotifier，并立即尝试装配其当前 Pawn；不跨 World 强持 Controller。 */
	void BindController(APlayerController* Controller);

	/** 从旧 Controller 精确移除 Pawn notifier 并清弱引用；Controller 已销毁时只清本地句柄。 */
	void UnbindController();

	/** 当前 Controller Pawn 变化入口；先完整解绑旧 LakeReach MVC，再尝试把 NewPawn 作为新的 ACatCharacter 只读来源。 */
	void HandleControllerPawnChanged(APawn* NewPawn);

	/** 当配置 WBP、当前 Controller 与 Character 有效时创建唯一 LakeReach MVC，并让 Model/PageController/View 成对绑定。 */
	void AttachLakePawn(ACatCharacter* Character);

	/** 先解绑 PageController，再解绑 Model 并移除 View，最后清理 LakeReach MVC 引用。 */
	void DetachLakePawn();

	/** 当前 LocalPlayer 唯一 Frontend/旅行白盒界面；子系统拥有，Controller 变化或销毁时释放。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatTravelWidget> OnlineWidget;

	/** 当前 LocalPlayer 的正式 LakeReach WBP View；由 Settings 配置类创建，缺类时不创建原生替身。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatLakeReachWidget> LakeReachWidget;

	/** 当前 LakeReach MVC Model；它拥有只读 Query 订阅、Fishing Bridge 和当前 ViewState。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatLakeReachModel> LakeReachModel;

	/** 当前 LakeReach MVC PageController；它管理菜单输入、焦点和 View 意图转发。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatLakeReachPageController> LakeReachPageController;

	/** 当前绑定 Pawn notifier 的 Controller 弱引用；Controller/World 替换时先解绑，绝不成为跨 World 所有者。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> BoundPlayerController;

	/** Online 快照广播的配对解绑句柄；Initialize 写入，Deinitialize 消费。 */
	FDelegateHandle OnlineSnapshotHandle;

	/** Frontend TravelWidget 动作广播的配对解绑句柄；创建时写入，移除时消费。 */
	FDelegateHandle ActionHandle;

	/** 当前 Controller Pawn notifier 的配对解绑句柄；Controller 变化或 Deinitialize 时消费。 */
	FDelegateHandle PawnChangedHandle;
};
