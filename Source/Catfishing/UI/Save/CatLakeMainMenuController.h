#pragma once

#include "CoreMinimal.h"
#include "UI/CatUIModalInputMode.h"
#include "UObject/Object.h"
#include "CatLakeMainMenuController.generated.h"

class APlayerController;
class UCatLakeMainMenuWidget;
class UCatFrontendSettingsModel;
class UCatOnlineSubsystem;
class UCatSaveSubsystem;
class UEnhancedInputComponent;
class UInputAction;
class ULocalPlayer;
enum class ECatLakeMainMenuAction : uint8;

/** 局内 ESC 菜单控制器；它拥有菜单打开态、输入绑定，并把保存、设置、回主菜单和本地 Quit 分别转交给权威系统。 */
UCLASS()
class CATFISHING_API UCatLakeMainMenuController : public UObject
{
	GENERATED_BODY()

public:
	/** 绑定本地玩家、Controller 和菜单 View；成功后安装主菜单 Action，并订阅保存忙闲与完成变化。 */
	bool Bind(ULocalPlayer* InLocalPlayer, APlayerController* InController, UCatLakeMainMenuWidget* InView);

	/** 成对关闭菜单、恢复输入、解除 Action 绑定和系统订阅；换 Pawn、旅行或 LocalPlayer 销毁时调用。 */
	void Unbind();

	/** 切换局内菜单打开状态；PIE 中 Shift+Escape 会被跳过并交还编辑器，普通菜单键才由 SetMenuOpen 处理视口、焦点和输入锁。 */
	void ToggleMenu();

	/** 返回菜单是否由本 Controller 保持打开；不从 Widget 可见性反推。 */
	bool IsMenuOpen() const;

	/** Controller 的 EnhancedInputComponent 可能晚于 UI 创建；拥有者用本入口重新安装主菜单 Action。 */
	void RefreshInputBinding();

	/** Widget 请求关闭菜单；关闭状态下的迟到点击不会反向打开。 */
	void RequestCloseFromWidget();

	/** Widget 请求打开设置；Controller 切到复用主界面 SettingsModel 的局内设置页，不创建第二套设置来源。 */
	void RequestSettingsFromWidget();

	/** Widget 请求保存当前活动世界；Controller 只转交 Save 子系统并显示同步或异步结果文本。 */
	void RequestSaveFromWidget();

	/** Widget 请求退出到主菜单；Controller 转交 Online Leave 并保持等待画面直到保存、拆局、销毁会话与回前台旅行真实结案。 */
	void RequestReturnToMainMenuFromWidget();

	/** Widget 请求直接退出本地游戏进程；PIE 中交给引擎退出入口停止当前编辑器运行。 */
	void RequestExitGameFromWidget();

	/** Widget 请求应用局内设置草稿；成功后回到暂停菜单，失败时留在设置页显示 SettingsModel 反馈。 */
	void RequestApplySettingsFromWidget();

	/** Widget 请求取消局内设置；Controller 丢弃草稿并回到暂停菜单，不关闭整个 ESC 菜单。 */
	void RequestCancelSettingsFromWidget();

	/** Widget 请求恢复设置默认值；只修改 SettingsModel 草稿，等待玩家再应用。 */
	void RequestRestoreSettingsDefaultsFromWidget();

	/** Widget 请求刷新输出设备；Controller 转交 SettingsModel 的正式 AudioMixer 枚举入口。 */
	void RequestRefreshAudioOutputDevicesFromWidget();

	/** Widget 请求切到游戏设置分类；Controller 转交 SettingsModel 的命名分类接口。 */
	void RequestSelectGameSettingsFromWidget();

	/** Widget 请求切到画面设置分类；Controller 转交 SettingsModel 的命名分类接口。 */
	void RequestSelectGraphicsSettingsFromWidget();

	/** Widget 请求切到声音设置分类；Controller 转交 SettingsModel 的命名分类接口。 */
	void RequestSelectAudioSettingsFromWidget();

	/** Widget 请求切到控制设置分类；Controller 转交 SettingsModel 的正式受限分类接口。 */
	void RequestSelectControlsSettingsFromWidget();

private:
	/** 菜单打开态是模态输入恢复的唯一闸口；所有入口都经这里成对处理视口、焦点、鼠标和移动锁。 */
	void SetMenuOpen(bool bOpen);

	/** 加载 UI Settings 中的主菜单 Action 并绑定到当前 EnhancedInputComponent；按键映射只来自既有 IMC。 */
	void InstallMenuInput();

	/** 从安装时记录的 EnhancedInputComponent 精确移除主菜单 Action 绑定，再释放配置资产强引用。 */
	void RemoveMenuInput();

	/** 根据菜单打开态应用或释放模态 UI 输入锁；打开时玩家移动和视角被本菜单暂停。 */
	void ApplyMenuInputMode(bool bOpen);

	/** 按 Controller 当前事实重绘菜单状态；按钮可用性只读取 Save busy 和服务是否存在。 */
	void UpdateView();

	/** 响应 Widget 的统一菜单 Action；这里把按钮语义分发到关闭、设置、保存、回主菜单和退出进程入口。 */
	void HandleMenuActionRequested(ECatLakeMainMenuAction Action);

	/** Save 子系统目录或忙闲状态变化入口；这里只刷新按钮可用性，手动保存文案由匹配请求 ID 的完成回调写入。 */
	void HandleSaveChanged();

	/** Save 子系统完成回调；只消费本菜单发起的手动保存请求，避免目录读取等文本覆盖保存反馈。 */
	void HandleSaveCompleted(FGuid RequestId, bool bSuccess);

	/** Online 快照变化入口；只在退出到主菜单等待中刷新阶段文字或恢复失败后的命令页。 */
	void HandleOnlineChanged();

	/** 通过绑定的 LocalPlayer 定位当前 GameInstance 级 Save 子系统；任一生命周期层失效时返回空。 */
	UCatSaveSubsystem* GetSaveSubsystem() const;

	/** 通过绑定的 LocalPlayer 定位当前 GameInstance 级 Online 子系统；任一生命周期层失效时返回空。 */
	UCatOnlineSubsystem* GetOnlineSubsystem() const;

	/** 返回局内菜单持有的 SettingsModel；它与主界面模型同类同规则，但生命周期随当前玩法 UI。 */
	UCatFrontendSettingsModel* GetSettingsModel() const;

	/** 当前菜单绑定的本地玩家；只用于定位本机 GameInstance 子系统，不保存跨 World 状态。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ULocalPlayer> BoundLocalPlayer;

	/** 当前菜单绑定的本地 Controller；输入绑定、焦点和鼠标恢复都只作用于这一只 Controller。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> BoundPlayerController;

	/** 当前菜单 View；Controller 只把它加入或移出视口，并向它写只读 ViewState。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatLakeMainMenuWidget> BoundView;

	/** 局内设置页的正式数据源；Controller 创建并关闭它，View 只订阅草稿和结果文本。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatFrontendSettingsModel> SettingsModel;

	/** 当前页面安装的主菜单开关 Action；保存强引用只为输入绑定生命周期配对。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> AppliedMainMenuToggleAction;

	/** Action 实际绑定的 EnhancedInputComponent；Controller 输入链重建时从失效组件精确移除。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UEnhancedInputComponent> BoundMenuInputComponent;

	/** Enhanced Input 中主菜单 Action 的唯一绑定句柄；0 表示当前没有可移除绑定。 */
	uint32 MenuInputBindingHandle = 0;

	/** Save 子系统 OnChanged 的配对解绑句柄；只在 Bind 成功并且 Save 来源有效时存在。 */
	FDelegateHandle SaveChangedHandle;

	/** Save 子系统 OnSaveCompleted 的配对解绑句柄；只用于手动保存结果的明确完成文案。 */
	FDelegateHandle SaveCompletedHandle;

	/** Online 快照广播的配对解绑句柄；只用于退出到主菜单等待状态读取真实异步阶段和失败结果。 */
	FDelegateHandle OnlineChangedHandle;

	/** 菜单当前是否打开的唯一状态；Toggle 写入，输入模式和 ViewState 只读取。 */
	bool bMenuOpen = false;

	/** 当前等待回执的手动保存请求 ID；无效值表示没有由本菜单发起且尚未结案的保存。 */
	FGuid PendingManualSaveRequestId;

	/** 当前等待回主菜单的 Online Leave 请求 ID；无效值表示没有本菜单发起的离局链路在途。 */
	FGuid PendingReturnToMainMenuRequestId;

	/** 菜单打开期间的模态输入恢复记录；它只记录本菜单申请的一层移动/视角锁和鼠标状态。 */
	FCatUIModalInputModeState ModalInputModeState;

	/** 当前是否正在等待退出到主菜单链路结案；为 true 时关闭、保存和设置入口都会被锁住。 */
	bool bReturnToMainMenuPending = false;

	/** 最近一次局内菜单命令得到的可展示反馈；按钮入口和匹配的保存完成回调写入它，普通 Save 目录刷新不能覆盖。 */
	FText LastStatusText;
};
