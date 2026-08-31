#pragma once

#include "CoreMinimal.h"
#include "UI/CatUIModalInputMode.h"
#include "UObject/Object.h"
#include "CatInventoryPageController.generated.h"

class APlayerController;
class ACatCampInventoryActor;
class UCatCampInventoryWidget;
class UCatContainerReplicationComponent;
class UCatInventoryModel;
class UCatInventoryWidget;
class UEnhancedInputComponent;
class UInputAction;
class ULocalPlayer;
enum class ECatInventoryAction : uint8;
struct FCatInventorySlotView;

/** 库存 PageController；它只管理库存页面打开、输入模式和玩家意图到服务器命令的翻译，不负责渲染 WBP。 */
UCLASS()
class CATFISHING_API UCatInventoryPageController : public UObject
{
	GENERATED_BODY()

public:
	/** 绑定 LocalPlayer、Controller、Model 与默认库存 View；成功后安装输入，渲染由各库存 WBP 自己监听 Model 完成。 */
	bool Bind(ULocalPlayer* InLocalPlayer, APlayerController* InController,
		UCatInventoryModel* InModel, UCatInventoryWidget* InView);

	/** 成对解除输入、当前页面和 UI 焦点；换 Pawn、旅行或销毁时调用。 */
	void Unbind();

	/** 切换库存打开状态；只有当前 Controller、Model 和 View 都有效时才改变输入模式和 ViewState。 */
	void ToggleInventory();

	/** 打开带外部容器上下文的库存；交互对象只提供容器读源，后续移动仍由 Drop 和服务器权限裁决。 */
	void OpenInventoryWithExternalContainerContexts(const TArray<UCatContainerReplicationComponent*>& ExternalContainers);

	/** 使用交互对象指定的库存 WBP 类打开外部容器；PageController 创建临时页面并在打开失败时返回 false。 */
	bool OpenInventoryWithExternalContainerContextsUsingViewClass(
		const TArray<UCatContainerReplicationComponent*>& ExternalContainers,
		TSubclassOf<UCatInventoryWidget> InventoryViewClass);

	/** 打开营地公共仓库上下文；公共仓库必须使用调用方传入的独立 WBP 类，后续取用由服务器按仓库 Actor 裁决。 */
	bool OpenCampInventory(ACatCampInventoryActor* CampInventory,
		TSubclassOf<UCatCampInventoryWidget> InventoryViewClass);

	/** 返回库存是否由本 PageController 保持打开；不从 Widget 可见性反推。 */
	bool IsInventoryOpen() const;

	/** Controller 的输入组件可能晚于 UI 装配完成；本入口让拥有者在输入链就绪后重新安装库存 Action。 */
	void RefreshInputBinding();

	/** 库存 WBP 请求关闭当前库存页；关闭状态下的迟到点击不会反向打开。 */
	void RequestCloseInventoryFromWidget();

	/** 库存 WBP 请求处理一个格子的右键上下文；PageController 会按该格所属数据源决定取用或装备选择。 */
	void RequestInventorySlotContextFromWidget(const FCatInventorySlotView& Slot);

	/** 库存 WBP 请求处理一次格子 Drop；PageController 从最新 Model 复核来源后提交服务器事务。 */
	void RequestInventorySlotDropFromWidget(const FCatInventorySlotView& SourceSlot,
		const FCatInventorySlotView& TargetSlot);

	/** 库存 WBP 请求执行吃鱼或献祭动作；PageController 用页面传入的本地选择对照最新 Model 快照重建服务器命令。 */
	void RequestInventoryActionFromWidget(ECatInventoryAction Action, const FCatInventorySlotView& SelectedSlot);

private:
	/** 设置库存打开态并成对处理视口、输入模式和 Model 打开投影；普通切换和交互打开共用这条生命周期。 */
	void SetInventoryOpen(bool bOpen);

	/** 加载 Settings 中配置的库存 Action，并绑定到当前 EnhancedInputComponent；按键映射必须来自既有 InputContext。 */
	void InstallInventoryInput();

	/** 从原 EnhancedInputComponent 精确移除库存 Action 绑定，再释放配置资产强引用。 */
	void RemoveInventoryInput();

	/** 根据库存打开状态应用或释放模态 UI 输入锁；打开会停止当前移动，关闭会恢复本页面改过的焦点和鼠标。 */
	void ApplyInventoryInputMode(bool bOpen);

	/** 切换当前库存根页面；只处理当前页面和视口焦点，不重建输入绑定或后端容器上下文。 */
	bool SwitchInventoryView(UCatInventoryWidget* NewView);

	/** 使用已经创建好的库存页面打开外部容器；内部给 ViewClass 入口复用，失败时不写第二套库存状态。 */
	bool OpenInventoryWithExternalContainerContextsUsingView(
		const TArray<UCatContainerReplicationComponent*>& ExternalContainers, UCatInventoryWidget* PreferredView);

	/** 清理失败的交互库存打开状态；只处理按需临时页、外部容器和营地仓库上下文，不关闭普通 Tab 背包。 */
	void ClearInteractionInventoryOpenFailure();

	/** 移出当前库存根页面；输入和后端上下文由外层生命周期继续管理。 */
	void UnbindInventoryView();

	/** 当前页面绑定的 PlayerController；输入模式、鼠标和 RPC 都只作用于这一只 Controller。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> BoundPlayerController;

	/** 当前页面读取的库存 Model；PageController 不直接订阅地面鱼护、其他世界容器或随身库存复制源。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatInventoryModel> BoundModel;

	/** 当前正在作为库存根页面打开的 WBP；PageController 只负责把它加入或移出视口。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatInventoryWidget> BoundView;

	/** 普通 Tab 库存使用的默认 WBP；从鱼护箱子关闭后再次普通打开时要切回这张页面。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatInventoryWidget> DefaultInventoryView;

	/**
	 * 交互对象按需指定的临时库存 WBP，表示当前鱼护、营地仓库等世界库存请求使用的非默认页面实例。
	 * PageController 创建、持有并在关闭或解绑时释放它；它只替换当前页面，不会把交互失败退回默认库存页，也不改 Model 的后端快照。
	 */
	UPROPERTY(Transient)
	TObjectPtr<UCatInventoryWidget> InteractionInventoryView;

	/** 当前页面安装的库存开关 Action；保存强引用只为输入绑定生命周期配对。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> AppliedInventoryToggleAction;

	/** Action 实际绑定的 EnhancedInputComponent；换 Controller 时从原组件精确移除。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UEnhancedInputComponent> BoundInventoryInputComponent;

	/** Enhanced Input 中库存 Action 的唯一绑定句柄；0 表示当前没有可移除绑定。 */
	uint32 InventoryInputBindingHandle = 0;

	/** 库存当前是否打开的唯一状态；Toggle 写入，Model 和输入恢复只读取。 */
	bool bInventoryOpen = false;

	/** 库存打开期间的模态输入恢复记录；它只记录本页面申请的一层移动/视角锁和鼠标状态。 */
	FCatUIModalInputModeState ModalInputModeState;

	/** 当前交互打开的营地公共仓库 Actor；只用于营地整理、右键取用和背包/营地拖放 RPC，不直接写公共仓库格。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ACatCampInventoryActor> BoundCampInventory;
};
