#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CatLakeMainMenuWidget.generated.h"

class UButton;
class UTextBlock;

/** 局内主菜单的一次玩家意图；Widget 只声明按钮语义，真正保存、设置或离开由 Controller 裁决。 */
UENUM(BlueprintType)
enum class ECatLakeMainMenuAction : uint8
{
	/** 关闭当前局内菜单并把输入还给游戏；不会保存、旅行或改变任何设置。 */
	Close,

	/** 请求打开局内设置入口；没有正式设置页时 Controller 只显示明确反馈，不生成假设置状态。 */
	OpenSettings,

	/** 请求把当前活动世界写入现有活动槽；是否可保存由 Save 子系统按 Host 和活动槽状态裁决。 */
	Save,

	/** 请求离开当前游戏局；实际路径走 Online 子系统，Host 会沿用离开前保存链路。 */
	ExitGame
};

/** 局内菜单按钮点击通知；订阅者收到后读取 Action 并调用各自权威系统。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatLakeMainMenuActionRequested, ECatLakeMainMenuAction);

/** 局内菜单的只读显示状态；按钮可用性和反馈文本来自 Controller，不从 Widget 反推业务状态。 */
USTRUCT(BlueprintType)
struct FCatLakeMainMenuViewState
{
	GENERATED_BODY()

	/** 当前菜单底部展示的结果或降级说明；由保存、设置、退出入口写入，蓝图只显示它。 */
	UPROPERTY(BlueprintReadOnly)
	FText StatusText;

	/** 设置按钮是否可点击；退出流程已经受理后会关闭，避免玩家在旅行期间继续打开新页面。 */
	UPROPERTY(BlueprintReadOnly)
	bool bSettingsEnabled = true;

	/** 保存按钮是否可点击；Save 忙碌、服务缺失或退出流程在途时为 false。 */
	UPROPERTY(BlueprintReadOnly)
	bool bSaveEnabled = true;

	/** 退出游戏按钮是否可点击；Online 离开请求已经受理后禁用，避免重复提交同一离局意图。 */
	UPROPERTY(BlueprintReadOnly)
	bool bExitEnabled = true;
};

/** 局内 ESC 主菜单的 WBP 基类；它只绑定正式资产中的同名控件，不在 C++ 里另画一套菜单表现。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatLakeMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 接收 Controller 的最新只读状态并刷新按钮、状态文本和蓝图扩展点；Widget 不缓存 Save 或 Online 来源。 */
	void RenderMenu(const FCatLakeMainMenuViewState& ViewState);

	/** 暴露最近一次菜单投影给 WBP；它只用于表现，不代表可写的保存、设置或联机状态。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|LakeMenu")
	const FCatLakeMainMenuViewState& GetLastMenuViewState() const;

	/** 提交关闭菜单意图；按钮、ESC 和蓝图都应走这个入口，确保输入恢复只由 Controller 成对处理。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|LakeMenu")
	void RequestCloseMenu();

	/** 提交设置入口意图；本 Widget 不创建设置页，也不写任何设置草稿。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|LakeMenu")
	void RequestOpenSettings();

	/** 提交手动保存意图；是否保存、保存哪个活动槽以及失败原因全部交给 Save 子系统。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|LakeMenu")
	void RequestSave();

	/** 提交退出当前游戏局意图；实际保存和回前台流程由 Online 子系统处理。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|LakeMenu")
	void RequestExitGame();

	/** 所有菜单按钮的统一原生广播；LocalPlayer UI Controller 订阅它，不让 HUD 或 Widget 持有业务系统。 */
	FCatLakeMainMenuActionRequested OnActionRequested;

protected:
	/** Slate/UMG 构造完成后绑定可选 Designer 按钮，并保证菜单自身能接收 ESC 关闭键。 */
	virtual void NativeConstruct() override;

	/** 离开视口时解除可选 Designer 按钮绑定，避免 WBP 重建后重复广播同一点击。 */
	virtual void NativeDestruct() override;

	/** 预览键盘输入时优先消费 ESC 关闭键，防止子按钮焦点吞掉关闭菜单的玩家意图。 */
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** 菜单根拿到键盘焦点时消费 ESC 关闭键；预览未命中的其它键继续交还父类。 */
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** WBP 可选渲染扩展点；正式资产可以读取 ViewState 决定动画、焦点或局部文案。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|LakeMenu")
	void BP_RenderMenu(const FCatLakeMainMenuViewState& ViewState);

	/** WBP 可选意图扩展点；只用于表现响应，不替代 Controller 的保存、设置或离开裁决。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|LakeMenu")
	void BP_HandleMenuAction(ECatLakeMainMenuAction Action);

private:
	/** 绑定 Designer 里同名按钮到统一意图入口；缺少某个按钮时只跳过该资产控件，不创建第二套表现入口。 */
	void BindDesignerButtons();

	/** 解除 Designer 按钮绑定；每个控件只移除本对象的委托，不影响蓝图自己追加的表现逻辑。 */
	void UnbindDesignerButtons();

	/** 广播指定菜单意图并通知蓝图表现扩展；它不读取任何业务系统，也不改变菜单打开状态。 */
	void SubmitMenuAction(ECatLakeMainMenuAction Action);

	/** 判断当前键盘事件是否代表关闭菜单；这里只处理已聚焦 UI 内的 Escape，不负责运行时输入映射。 */
	bool ShouldCloseMenuFromKey(const FKeyEvent& InKeyEvent) const;

	/** 最近一次 Controller 写入的菜单显示状态；WBP 只读这一份 ViewState，不另存保存或退出结果。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|LakeMenu", meta = (AllowPrivateAccess = "true"))
	FCatLakeMainMenuViewState LastMenuViewState;

	/** WBP Designer 中的设置按钮；存在时点击广播 OpenSettings，不直接创建设置页。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> SettingsButton;

	/** WBP Designer 中的保存按钮；存在时点击广播 Save，后续由 Save 子系统判断活动槽和 Host 状态。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> SaveButton;

	/** WBP Designer 中的退出当前游戏按钮；存在时点击广播 ExitGame，后续由 Online 子系统执行正式离局。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> ExitGameButton;

	/** WBP Designer 中的可选关闭按钮；存在时只关闭菜单并恢复输入，不提交保存或离局。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> CloseButton;

	/** WBP Designer 中的结果文本；存在时显示保存、设置或退出入口返回的明确反馈。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusTextBlock;
};
