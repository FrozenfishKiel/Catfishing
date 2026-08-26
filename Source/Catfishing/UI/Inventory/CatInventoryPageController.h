#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CatInventoryPageController.generated.h"

class ACatCampHubActor;
class APlayerController;
class UCatInventoryModel;
class UCatInventoryWidget;
class UEnhancedInputComponent;
class UInputAction;
class ULocalPlayer;
enum class ECatInventoryAction : uint8;
enum class ECatInventorySlotPointerAction : uint8;

/** 个人鱼护背包的 PageController；它连接 Model 与 WBP，并把纯 UI 意图翻译成输入模式或正式服务器命令。 */
UCLASS()
class CATFISHING_API UCatInventoryPageController : public UObject
{
	GENERATED_BODY()

public:
	/** 绑定 LocalPlayer、Controller、Model 与背包 View；成功后安装输入、订阅委托并渲染当前状态。 */
	bool Bind(ULocalPlayer* InLocalPlayer, APlayerController* InController,
		UCatInventoryModel* InModel, UCatInventoryWidget* InView);

	/** 成对解除输入、Model/View 委托和 UI 焦点；换 Pawn、旅行或销毁时调用。 */
	void Unbind();

	/** 切换个人背包打开状态；只有当前 Controller、Model 和 View 都有效时才改变输入模式和 ViewState。 */
	void ToggleInventory();

	/** 返回背包是否由本 PageController 保持打开；不从 Widget 可见性反推。 */
	bool IsInventoryOpen() const;

	/** 外部事实变化时让 Model 重读；PageController 不保存任何后端快照。 */
	void RefreshModel();

private:
	/** Model ViewState 变化入口；把最新背包投影交给 WBP。 */
	void HandleModelViewStateChanged();

	/** 背包 View 发出关闭意图时只关闭已打开背包；关闭状态下的迟到点击不反向打开。 */
	void HandleViewCloseRequested();

	/** 背包 View 发出格子选择意图时转交 Model；PageController 不缓存第二份下标。 */
	void HandleViewSlotSelectionRequested(int32 SlotIndex);

	/** 背包格子右键或拖拽意图；默认只同步选择，具体上下文表现留给 WBP。 */
	void HandleViewSlotPointerRequested(int32 SlotIndex, ECatInventorySlotPointerAction PointerAction);

	/** 背包 View 发出动作意图时从 Model 当前选择重建 PlayerController 服务器命令。 */
	void HandleViewActionRequested(ECatInventoryAction Action);

	/** 为转缸动作在当前 World 中定位最近固定营地；找不到时返回空并让 Model 显示结构化拒绝。 */
	ACatCampHubActor* ResolveCampHubForInventoryAction() const;

	/** 加载 Settings 中配置的背包 Action，并绑定到当前 EnhancedInputComponent；按键映射必须来自既有 InputContext。 */
	void InstallInventoryInput();

	/** 从原 EnhancedInputComponent 精确移除背包 Action 绑定，再释放配置资产强引用。 */
	void RemoveInventoryInput();

	/** 根据背包打开状态设置 UIOnly/GameOnly、键盘焦点和鼠标，并在关闭时恢复打开前鼠标可见性。 */
	void ApplyInventoryInputMode(bool bOpen);

	/** 当前 PageController 所属 LocalPlayer；用于访问配置和输入生命周期。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ULocalPlayer> BoundLocalPlayer;

	/** 当前页面绑定的 PlayerController；输入模式、鼠标和 RPC 都只作用于这一只 Controller。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> BoundPlayerController;

	/** 当前页面读取的背包 Model；PageController 不直接订阅鱼护复制组件。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatInventoryModel> BoundModel;

	/** 当前页面渲染的背包 WBP；PageController 只调用 RenderInventory 和订阅纯意图。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatInventoryWidget> BoundView;

	/** 当前页面安装的背包开关 Action；保存强引用只为输入绑定生命周期配对。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> AppliedInventoryToggleAction;

	/** Action 实际绑定的 EnhancedInputComponent；换 Controller 时从原组件精确移除。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UEnhancedInputComponent> BoundInventoryInputComponent;

	/** Model 完整投影变化订阅句柄；Unbind 必须从同一 Model 移除。 */
	FDelegateHandle ModelViewChangedHandle;

	/** View 关闭意图订阅句柄；Unbind 必须从同一 View 移除。 */
	FDelegateHandle ViewCloseHandle;

	/** View 格子选择意图订阅句柄；Unbind 必须从同一 View 移除。 */
	FDelegateHandle ViewSlotSelectionHandle;

	/** View 格子鼠标上下文意图订阅句柄；Unbind 必须从同一 View 移除。 */
	FDelegateHandle ViewSlotPointerHandle;

	/** View 背包动作意图订阅句柄；Unbind 必须从同一 View 移除。 */
	FDelegateHandle ViewActionHandle;

	/** Enhanced Input 中背包 Action 的唯一绑定句柄；0 表示当前没有可移除绑定。 */
	uint32 InventoryInputBindingHandle = 0;

	/** 背包当前是否打开的唯一状态；Toggle 写入，Model 和输入恢复只读取。 */
	bool bInventoryOpen = false;

	/** 打开背包前 Controller 的鼠标可见性；关闭、换 Pawn 或旅行时恢复该值。 */
	bool bPreviousMouseCursorVisible = false;
};
