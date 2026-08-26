#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CatLakeReachPageController.generated.h"

class APlayerController;
class ACatCampHubActor;
class UCatLakeReachModel;
class UCatLakeReachWidget;
class UEnhancedInputComponent;
class UInputAction;
class ULocalPlayer;
enum class ECatUIReachFishGuardAction : uint8;
enum class ECatUIReachShopAction : uint8;

/** LakeReach 的 MVC PageController；它连接 Model 与 WBP View，并把 UI 意图翻译成输入模式或正式 Online 请求。 */
UCLASS()
class CATFISHING_API UCatLakeReachPageController : public UObject
{
	GENERATED_BODY()

public:
	/** 绑定 LocalPlayer、Controller、Model 与 View；成功后安装菜单输入、订阅 View/Model，并立即渲染当前状态。 */
	bool Bind(ULocalPlayer* InLocalPlayer, APlayerController* InController, UCatLakeReachModel* InModel, UCatLakeReachWidget* InView);

	/** 成对解除输入、Model/View 委托和焦点状态；销毁或换 Pawn 时调用，避免旧页面继续接收迟到事件。 */
	void Unbind();

	/** 切换当前 Lake 菜单；只有 View、Model 与 Controller 都有效时才改变 InputMode、焦点和鼠标。 */
	void ToggleLakeMenu();

	/** 返回当前菜单是否由本 PageController 保持打开；它不从 Widget 可见性或鼠标状态反推。 */
	bool IsLakeMenuOpen() const;

	/** 外部 Online 快照变化后主动刷新 Model；PageController 不保存 Online 状态，只触发只读重读。 */
	void RefreshModel();

private:
	/** Model 完整 ViewState 变化入口；读取 Model 最新 DTO 并交给 WBP View 渲染。 */
	void HandleModelViewStateChanged();

	/** 根 View 发出关闭意图时只关闭已打开菜单；关闭状态下的迟到点击不反向打开。 */
	void HandleViewCloseRequested();

	/** 根 View 发出离局意图时转交给统一 Online Leave；不直接 DestroySession、ServerTravel 或 ClientTravel。 */
	void HandleViewLeaveRequested();

	/** 根 View 发出鱼护选择偏移时转交给 Model；PageController 不持有第二份选择状态。 */
	void HandleViewFishGuardSelectionRequested(int32 Offset);

	/** 根 View 发出鱼护动作意图时重读 Model 选择，并把它翻译成正式 PlayerController 服务器命令。 */
	void HandleViewFishGuardActionRequested(ECatUIReachFishGuardAction Action);

	/** 根 View 发出商店购买或免费领取意图时重读团队钱包 Revision，并转交 PlayerController 的 Shop RPC。 */
	void HandleViewShopActionRequested(ECatUIReachShopAction Action);

	/** 为鱼护转缸动作在当前 World 中定位最近固定营地；找不到时返回空并让 Model 显示结构化拒绝。 */
	ACatCampHubActor* ResolveCampHubForFishGuardAction() const;

	/** 加载 Settings 中配置的菜单 Action，并把它绑定到当前 EnhancedInputComponent；按键映射必须来自项目既有 InputContext。 */
	void InstallMenuInput();

	/** 从原 Controller 精确移除菜单 Action 绑定，再释放本页对配置资产的强引用。 */
	void RemoveMenuInput();

	/** 根据菜单状态设置 UIOnly 或 GameOnly、键盘焦点和鼠标，并在关闭时恢复打开前的鼠标可见性。 */
	void ApplyLakeMenuInputMode(bool bOpen);

	/** 当前 PageController 所属 LocalPlayer；用于访问 EnhancedInput 子系统和 Online 子系统。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ULocalPlayer> BoundLocalPlayer;

	/** 当前页面绑定的 PlayerController；输入模式、鼠标和 EnhancedInput 绑定都只作用于这一只 Controller。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> BoundPlayerController;

	/** 当前页面读取的 UIReach Model；PageController 不直接订阅玩法 Query。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatLakeReachModel> BoundModel;

	/** 当前页面渲染的 WBP View；PageController 只调用 Render 和绑定纯意图委托。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatLakeReachWidget> BoundView;

	/** 当前页面安装的菜单 Action 资产；它来自配置软引用，保存强引用只为输入绑定生命周期配对。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> AppliedLakeMenuToggleAction;

	/** 菜单 Action 实际绑定的 Enhanced Input 组件；换 Controller 时从原组件精确移除。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UEnhancedInputComponent> BoundMenuInputComponent;

	/** Model ViewState 变化订阅句柄；Unbind 必须用同一 Model 移除。 */
	FDelegateHandle ModelViewChangedHandle;

	/** View 关闭意图订阅句柄；Unbind 必须用同一 View 移除。 */
	FDelegateHandle ViewCloseHandle;

	/** View 离局意图订阅句柄；Unbind 必须用同一 View 移除。 */
	FDelegateHandle ViewLeaveHandle;

	/** View 鱼护选择意图订阅句柄；Unbind 必须用同一 View 移除。 */
	FDelegateHandle ViewFishGuardSelectionHandle;

	/** View 鱼护动作意图订阅句柄；Unbind 必须用同一 View 移除。 */
	FDelegateHandle ViewFishGuardActionHandle;

	/** View 商店意图订阅句柄；Unbind 必须用同一 View 移除，避免旧按钮迟到提交订单。 */
	FDelegateHandle ViewShopActionHandle;

	/** Enhanced Input 组件中菜单 Action 的唯一绑定句柄；0 表示没有可移除绑定。 */
	uint32 LakeMenuInputBindingHandle = 0;

	/** 菜单当前是否打开的唯一状态；Toggle 写入，View 和输入恢复只读取。 */
	bool bLakeMenuOpen = false;

	/** 打开菜单前 Controller 的鼠标可见性；关闭、换 Pawn 或旅行时恢复该值。 */
	bool bPreviousMouseCursorVisible = false;
};
