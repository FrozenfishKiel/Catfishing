#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CatLakeMainMenuWidget.generated.h"

class UButton;
class UCheckBox;
class UComboBoxString;
class UPanelWidget;
class USlider;
class UTextBlock;
class UWidgetSwitcher;
class UCatFrontendSettingsModel;

/** 局内主菜单的一次玩家意图；Widget 只声明按钮语义，真正保存、设置或离开由 Controller 裁决。 */
UENUM(BlueprintType)
enum class ECatLakeMainMenuAction : uint8
{
	/** 关闭当前局内菜单并把输入还给游戏；不会保存、旅行或改变任何设置。 */
	Close,

	/** 请求打开局内设置页；Controller 会复用主界面 SettingsModel，不在暂停菜单里另建设置来源。 */
	OpenSettings,

	/** 请求把当前活动世界写入现有活动槽；是否可保存由 Save 子系统按 Host 和活动槽状态裁决。 */
	Save,

	/** 请求离开当前游戏局；实际路径走 Online 子系统，Host 会沿用离开前保存链路。 */
	ExitGame,

	/** 请求应用局内设置页草稿；真正写入仍由 SettingsModel 和 Controller 共同裁决。 */
	ApplySettings,

	/** 请求取消局内设置页草稿并回到暂停菜单；不会关闭整个局内菜单或退出游戏。 */
	CancelSettings,

	/** 请求把局内设置页草稿恢复为项目默认值；玩家仍需显式应用才会生效。 */
	RestoreSettingsDefaults,

	/** 请求刷新音频输出设备列表；异步枚举只交给正式 SettingsModel，不由 View 伪造设备。 */
	RefreshAudioOutputDevices,

	/** 请求切到游戏设置分类；分类状态仍保存在 SettingsModel。 */
	SelectGameSettings,

	/** 请求切到画面设置分类；分类状态仍保存在 SettingsModel。 */
	SelectGraphicsSettings,

	/** 请求切到声音设置分类；分类状态仍保存在 SettingsModel。 */
	SelectAudioSettings,

	/** 请求切到控制设置分类；当前只显示正式不可用说明，不生成临时键位配置。 */
	SelectControlsSettings
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

	/** 返回游戏按钮是否可点击；退出流程已接管时关闭，避免玩家隐藏正在退出的菜单状态。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCloseEnabled = true;

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
	/** 注入局内设置页共用的主界面 SettingsModel；Controller 拥有模型生命周期，Widget 只订阅并渲染草稿。 */
	void InitializeLakeMenuSettings(UCatFrontendSettingsModel* InSettingsModel);

	/** 解除局内设置页的 Model 订阅和显示映射；Controller 拆除菜单时调用，不应用或保存任何草稿。 */
	void ResetLakeMenuSettings();

	/** 接收 Controller 的最新只读状态并刷新按钮、状态文本和蓝图扩展点；Widget 不缓存 Save 或 Online 来源。 */
	void RenderMenu(const FCatLakeMainMenuViewState& ViewState);

	/** 显示暂停菜单命令页；设置页取消、应用成功和首次打开菜单都经这里回到纵向按钮列表。 */
	void ShowCommandMenu();

	/** 显示局内设置页；它复用主界面设置 Model 的字段、分类和应用规则，不创建第二套设置来源。 */
	void ShowSettingsPanel();

	/** 局内设置页可见性是 ESC 输入的分流条件；设置页内按 ESC 只放弃本次编辑，命令页按 ESC 才恢复游戏输入。 */
	bool IsShowingSettingsPanel() const;

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

	/** 提交应用设置意图；Widget 不直接写 UGameUserSettings、音频设备或本地化配置。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|LakeMenu")
	void RequestApplySettings();

	/** 提交取消设置意图；Controller 会丢弃草稿并回到暂停菜单，不关闭整个菜单。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|LakeMenu")
	void RequestCancelSettings();

	/** 提交恢复默认设置意图；它只改草稿，直到应用成功才会写入正式配置。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|LakeMenu")
	void RequestRestoreSettingsDefaults();

	/** 提交刷新音频输出设备意图；设备列表来自 AudioMixer 异步结果。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|LakeMenu")
	void RequestRefreshAudioOutputDevices();

	/** 提交选择游戏设置分类意图；分类切换交给 SettingsModel 统一广播。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|LakeMenu")
	void RequestSelectGameSettings();

	/** 提交选择画面设置分类意图；分类切换交给 SettingsModel 统一广播。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|LakeMenu")
	void RequestSelectGraphicsSettings();

	/** 提交选择声音设置分类意图；分类切换交给 SettingsModel 统一广播。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|LakeMenu")
	void RequestSelectAudioSettings();

	/** 提交选择控制设置分类意图；当前只显示已纳入产品的不可用说明。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|LakeMenu")
	void RequestSelectControlsSettings();

	/** 所有菜单按钮的统一原生广播；LocalPlayer UI Controller 订阅它，不让 HUD 或 Widget 持有业务系统。 */
	FCatLakeMainMenuActionRequested OnActionRequested;

protected:
	/** Slate/UMG 构造完成后绑定命令与设置控件，并回到命令页，保证 ESC 和按钮都从稳定初始页开始。 */
	virtual void NativeConstruct() override;

	/** 离开视口时解除命令与设置控件绑定，避免 WBP 重建或 Slate 重建后重复广播同一点击。 */
	virtual void NativeDestruct() override;

	/** 预览键盘输入时优先消费 ESC；设置页内回命令页，命令页内关闭菜单并恢复游戏输入。 */
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** 菜单根拿到键盘焦点时复用 ESC 分流；预览未命中的其它键继续交还父类。 */
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** WBP 可选渲染扩展点；正式资产可以读取 ViewState 决定动画、焦点或局部文案。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|LakeMenu")
	void BP_RenderMenu(const FCatLakeMainMenuViewState& ViewState);

	/** WBP 可选意图扩展点；只用于表现响应，不替代 Controller 的保存、设置或离开裁决。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|LakeMenu")
	void BP_HandleMenuAction(ECatLakeMainMenuAction Action);

	/** WBP 可选设置页扩展点；原生已经完成控件回填，蓝图只能补动画和样式表现。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|LakeMenu")
	void BP_RenderSettings();

private:
	/** 绑定 Designer 里同名按钮到统一意图入口；缺少某个按钮时只跳过该资产控件，不创建第二套表现入口。 */
	void BindDesignerButtons();

	/** 解除 Designer 按钮绑定；每个控件只移除本对象的委托，不影响蓝图自己追加的表现逻辑。 */
	void UnbindDesignerButtons();

	/** 订阅当前 SettingsModel 的刷新通知；重复注入时先移除旧句柄，避免旧 World 设置页回调进来。 */
	void BindSettingsModelChanges();

	/** 解除 SettingsModel 刷新通知；菜单关闭不调用它，只有 Controller 拆除或重新注入时成对清理。 */
	void UnbindSettingsModelChanges();

	/** 绑定设置页具名控件的输入委托；所有按钮仍只广播语义，滑块和下拉只写 SettingsModel 草稿。 */
	void BindSettingsControls();

	/** 解除设置页具名控件的输入委托；Widget 从视口移除再加入时不会叠加回调。 */
	void UnbindSettingsControls();

	/** SettingsModel 变化后回填局内设置页全部控件；它只消费模型草稿和可用性，不应用任何设置。 */
	void HandleSettingsModelChanged();

	/** 广播指定菜单意图并通知蓝图表现扩展；它不读取任何业务系统，也不改变菜单打开状态。 */
	void SubmitMenuAction(ECatLakeMainMenuAction Action);

	/** 判断当前键盘事件是否代表关闭菜单；这里只处理已聚焦 UI 内的 Escape，不负责运行时输入映射。 */
	bool ShouldCloseMenuFromKey(const FKeyEvent& InKeyEvent) const;

	/** 语言下拉输入处理；只把 SettingsModel 提供的已打包 culture 写入草稿，应用前不改变运行语言。 */
	UFUNCTION() void HandleLanguageSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	/** 窗口模式下拉输入处理；只接受三种明确显示项，未知文本不会改写草稿。 */
	UFUNCTION() void HandleFullscreenModeSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	/** 分辨率下拉输入处理；把显示文本拆成宽高后交给 SettingsModel 校验正尺寸。 */
	UFUNCTION() void HandleScreenResolutionSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	/** 画质下拉输入处理；只把已知 UE 质量档或自定义状态写入草稿。 */
	UFUNCTION() void HandleQualitySelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	/** 垂直同步勾选输入处理；只写设置草稿，实际渲染同步等待应用。 */
	UFUNCTION() void HandleVSyncChanged(bool bIsChecked);
	/** UI 比例滑块输入处理；把 0..1 视图值换算为项目正式 0.75..2.0 范围。 */
	UFUNCTION() void HandleUIScaleChanged(float NormalizedValue);
	/** 显示 Gamma 滑块输入处理；把 0..1 视图值换算为项目正式 0.5..5.0 范围。 */
	UFUNCTION() void HandleBrightnessChanged(float NormalizedValue);
	/** 震动勾选输入处理；只有 SettingsModel 确认本地 Controller 可用时才写草稿。 */
	UFUNCTION() void HandleVibrationChanged(bool bIsChecked);
	/** 网络语音勾选输入处理；只有正式 OSS Voice 来源可用时才写草稿。 */
	UFUNCTION() void HandleVoiceChatChanged(bool bIsChecked);
	/** 后台静音勾选输入处理；只写失焦音量草稿，应用前不改变当前音频。 */
	UFUNCTION() void HandleMuteAudioWhenUnfocusedChanged(bool bIsChecked);
	/** 输出设备下拉输入处理；通过本 View 的显示项到设备 ID 映射写入正式设备草稿。 */
	UFUNCTION() void HandleAudioOutputDeviceSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	/** 主音量滑块输入处理；只写分类混音草稿，应用前不触碰 AudioDevice。 */
	UFUNCTION() void HandleMasterVolumeChanged(float Value);
	/** 音乐音量滑块输入处理；只写分类混音草稿，应用前不触碰 AudioDevice。 */
	UFUNCTION() void HandleMusicVolumeChanged(float Value);
	/** 音效音量滑块输入处理；只写分类混音草稿，应用前不触碰 AudioDevice。 */
	UFUNCTION() void HandleSFXVolumeChanged(float Value);
	/** 环境音音量滑块输入处理；只写分类混音草稿，应用前不触碰 AudioDevice。 */
	UFUNCTION() void HandleAmbienceVolumeChanged(float Value);
	/** 语音分类音量滑块输入处理；它只写分类音量草稿，不代替网络语音开关。 */
	UFUNCTION() void HandleVoiceVolumeChanged(float Value);

	/** 最近一次 Controller 写入的菜单显示状态；WBP 只读这一份 ViewState，不另存保存或退出结果。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|LakeMenu", meta = (AllowPrivateAccess = "true"))
	FCatLakeMainMenuViewState LastMenuViewState;

	/** 当前局内设置页读取的主界面 SettingsModel；Controller 拥有它，Widget 只保留弱引用防止生命周期反向持有。 */
	TWeakObjectPtr<UCatFrontendSettingsModel> SettingsModel;

	/** 输出设备下拉显示项到正式 AudioMixer ID 的瞬态映射；每次设置模型刷新时重建，选择回调用它避免猜 ID。 */
	TMap<FString, FString> AudioOutputDeviceIdsByOption;

	/** SettingsModel 刷新通知的解绑句柄；模型重新注入或菜单 Controller 拆除时按句柄移除。 */
	FDelegateHandle SettingsModelChangedHandle;

	/** 原生回填设置草稿到控件时的重入保护；回填阶段的 UMG 输入回调不会被误认作玩家修改。 */
	bool bRefreshingSettingsControls = false;

	/** 局内菜单页切换器；WBP 提供时在暂停命令页和设置页之间显式切换，不用可见性猜流程。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UWidgetSwitcher> LakeMainMenuPageSwitcher;

	/** 暂停菜单命令页根容器；没有 Switcher 的资产仍可用它显隐命令区。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> LakeCommandPanel;

	/** 局内设置页根容器；它承载和主界面同名的设置控件，供同一 SettingsModel 回填。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> LakeSettingsPanel;

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

	/** 设置页游戏分类按钮；点击只广播选择分类意图，分类状态仍由 SettingsModel 持有。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> GameSettingsCategoryButton;

	/** 设置页画面分类按钮；点击只广播选择分类意图，分类状态仍由 SettingsModel 持有。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> GraphicsSettingsCategoryButton;

	/** 设置页声音分类按钮；点击只广播选择分类意图，分类状态仍由 SettingsModel 持有。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> AudioSettingsCategoryButton;

	/** 控制分类按钮代表尚未落地的键位设置入口；它只展示正式占位说明，避免局内菜单生成第二套临时键位表。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> ControlsSettingsCategoryButton;

	/** 设置页应用按钮；点击交给 Controller 调用 SettingsModel::Apply，View 不直接写配置。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> ApplySettingsButton;

	/** 恢复默认按钮代表对设置草稿的重置请求；Controller 只重置待应用值，玩家确认应用前不写入持久配置。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> RestoreSettingsDefaultsButton;

	/** 取消按钮代表放弃本次局内设置编辑的出口；Controller 丢弃草稿后回到暂停命令页，游戏仍保持暂停状态。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> CancelSettingsButton;

	/** 设置页说明文本；Widget 按 SettingsModel 当前分类写入产品说明。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SettingsDescriptionTextBlock;

	/** 设置页反馈文本；只显示 SettingsModel 最近一次设置操作，不混入保存反馈。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FrontendSettingsResultTextBlock;

	/** 游戏分类面板；可见性由 SettingsModel 分类状态控制。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> GameSettingsPanel;

	/** 画面分类面板；可见性由 SettingsModel 分类状态控制。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> GraphicsSettingsPanel;

	/** 声音分类面板；可见性由 SettingsModel 分类状态控制。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> AudioSettingsPanel;

	/** 控制分类面板；当前只显示尚未开放的正式说明，不保存按键配置。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> ControlsSettingsPanel;

	/** 语言下拉框；选项和草稿来自 SettingsModel。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> LanguageComboBox;

	/** 窗口模式下拉框；显示项映射到 UE 的窗口模式枚举。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> FullscreenModeComboBox;

	/** 分辨率下拉框；由 SettingsModel 查询当前窗口模式支持的尺寸。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> ScreenResolutionComboBox;

	/** 整体画质下拉框；显示项映射到 UE -1 到 4 的质量档。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> OverallQualityComboBox;

	/** 垂直同步复选框；只显示和修改设置草稿。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> VSyncCheckBox;

	/** UI 比例滑块；0..1 视图值会换算为 SettingsModel 的项目范围。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USlider> UIScaleSlider;

	/** 亮度滑块；0..1 视图值会换算为 SettingsModel 的 DisplayGamma 范围。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USlider> BrightnessSlider;

	/** 手柄震动复选框；只有当前本地 Controller 支持时才可操作。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> VibrationCheckBox;

	/** 网络语音复选框；只有正式 OSS Voice 来源可用时才可操作。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> VoiceChatCheckBox;

	/** 失焦静音复选框；应用后映射到引擎失焦音量倍率。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> MuteAudioWhenUnfocusedCheckBox;

	/** 语音输入模式禁用说明；当前平台没有正式可持久化来源时显示原因。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> VoiceInputModeUnavailableText;

	/** 语音输入模式下拉框；当前仅作为禁用占位，不写任何草稿。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> VoiceInputModeComboBox;

	/** 麦克风选择禁用说明；当前平台没有正式设备枚举来源时显示原因。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MicrophoneUnavailableText;

	/** 麦克风选择下拉框；当前仅作为禁用占位，不写任何草稿。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> MicrophoneComboBox;

	/** 音频输出设备下拉框；显示项映射到 AudioMixer 稳定设备 ID。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> AudioOutputDeviceComboBox;

	/** 音频输出设备刷新按钮；枚举或切换在途时禁用。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> RefreshAudioOutputDevicesButton;

	/** 主音量滑块；只写 SettingsModel 的主混音草稿。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USlider> MasterVolumeSlider;

	/** 音乐音量滑块；只写 SettingsModel 的音乐混音草稿。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USlider> MusicVolumeSlider;

	/** 音效音量滑块；只写 SettingsModel 的音效混音草稿。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USlider> SFXVolumeSlider;

	/** 环境音音量滑块；只写 SettingsModel 的环境混音草稿。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USlider> AmbienceVolumeSlider;

	/** 语音音量滑块；只写 SettingsModel 的语音分类音量草稿。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USlider> VoiceVolumeSlider;
};
