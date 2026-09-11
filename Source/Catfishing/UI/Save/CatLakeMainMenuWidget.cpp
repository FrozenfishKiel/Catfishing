#include "UI/Save/CatLakeMainMenuWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/PanelWidget.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Input/Events.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "Logging/CatLog.h"
#include "UI/Frontend/CatFrontendSettingsModel.h"

#define LOCTEXT_NAMESPACE "CatLakeMainMenuWidget"

// 设置模型注入流程：先解除上一份模型通知，再保存弱引用、订阅刷新并回填当前控件；本 View 不拥有设置来源，也不触发 Apply。
void UCatLakeMainMenuWidget::InitializeLakeMenuSettings(UCatFrontendSettingsModel* InSettingsModel)
{
	UnbindSettingsModelChanges();
	SettingsModel = InSettingsModel;
	BindSettingsModelChanges();
	HandleSettingsModelChanged();
}

// 设置模型清理流程：解除通知、清空输出设备显示映射和回填保护；不会调用 SettingsModel::Shutdown，因为生命周期属于 Controller。
void UCatLakeMainMenuWidget::ResetLakeMenuSettings()
{
	UnbindSettingsModelChanges();
	SettingsModel.Reset();
	AudioOutputDeviceIdsByOption.Reset();
	bRefreshingSettingsControls = false;
}

// 渲染流程：
// 1. 保存 Controller 给出的唯一菜单投影，避免 Widget 从按钮状态反推 Save、Settings 或退出事实。
// 2. 写入可选 WBP 控件；返回、设置、保存、回主菜单和退出都只反映 Controller 投影，不读取业务系统。
// 3. 回主菜单等待中只禁用命令按钮并保留状态文本；全局遮罩负责真正的等待表现，最后通知蓝图扩展点读取 LastMenuViewState。
void UCatLakeMainMenuWidget::RenderMenu(const FCatLakeMainMenuViewState& ViewState)
{
	LastMenuViewState = ViewState;
	if (StatusTextBlock)
	{
		StatusTextBlock->SetText(LastMenuViewState.StatusText);
		StatusTextBlock->SetVisibility(
			LastMenuViewState.StatusText.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (SettingsButton)
	{
		SettingsButton->SetIsEnabled(LastMenuViewState.bSettingsEnabled);
	}
	if (CloseButton)
	{
		CloseButton->SetIsEnabled(LastMenuViewState.bCloseEnabled);
	}
	if (SaveButton)
	{
		SaveButton->SetIsEnabled(LastMenuViewState.bSaveEnabled);
	}
	if (ReturnToMainMenuButton)
	{
		ReturnToMainMenuButton->SetIsEnabled(LastMenuViewState.bReturnToMainMenuEnabled);
	}
	if (ExitGameButton)
	{
		ExitGameButton->SetIsEnabled(LastMenuViewState.bExitEnabled);
	}
	BP_RenderMenu(LastMenuViewState);
}

// 命令页显示流程：显式切回暂停菜单的纵向按钮列表并隐藏设置页；没有 Switcher 的资产改用根容器显隐，但不创建新控件。
void UCatLakeMainMenuWidget::ShowCommandMenu()
{
	if (LakeMainMenuPageSwitcher && LakeCommandPanel)
	{
		LakeMainMenuPageSwitcher->SetActiveWidget(LakeCommandPanel);
	}
	if (LakeCommandPanel)
	{
		LakeCommandPanel->SetVisibility(ESlateVisibility::Visible);
	}
	if (LakeSettingsPanel)
	{
		LakeSettingsPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (UButton* FocusButton = CloseButton ? CloseButton.Get() : SettingsButton.Get(); FocusButton && GetOwningPlayer())
	{
		FocusButton->SetUserFocus(GetOwningPlayer());
	}
}

// 设置页显示流程：切到局内设置面板并刷新全部同名设置控件；焦点给分类按钮，保证键盘手柄导航有稳定起点。
void UCatLakeMainMenuWidget::ShowSettingsPanel()
{
	if (LakeMainMenuPageSwitcher && LakeSettingsPanel)
	{
		LakeMainMenuPageSwitcher->SetActiveWidget(LakeSettingsPanel);
	}
	if (LakeCommandPanel)
	{
		LakeCommandPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (LakeSettingsPanel)
	{
		LakeSettingsPanel->SetVisibility(ESlateVisibility::Visible);
	}
	HandleSettingsModelChanged();
	if (GameSettingsCategoryButton && GetOwningPlayer())
	{
		GameSettingsCategoryButton->SetUserFocus(GetOwningPlayer());
	}
}

// 设置页查询流程：优先读取 Switcher 的激活页；既有布局没有 Switcher 时退回设置根可见性，只服务 ESC 的局部返回判断。
bool UCatLakeMainMenuWidget::IsShowingSettingsPanel() const
{
	if (LakeMainMenuPageSwitcher && LakeSettingsPanel)
	{
		return LakeMainMenuPageSwitcher->GetActiveWidget() == LakeSettingsPanel;
	}
	return LakeSettingsPanel && LakeSettingsPanel->GetVisibility() != ESlateVisibility::Collapsed
		&& LakeSettingsPanel->GetVisibility() != ESlateVisibility::Hidden;
}

// 状态读取流程：返回最后一次 Controller 渲染输入；调用方不能据此提交保存或退出游戏，只能用于表现绑定。
const FCatLakeMainMenuViewState& UCatLakeMainMenuWidget::GetLastMenuViewState() const
{
	return LastMenuViewState;
}

// 关闭请求流程：把 ESC、关闭按钮或蓝图调用统一转成 Close 意图；本对象不直接切换输入模式。
void UCatLakeMainMenuWidget::RequestCloseMenu()
{
	SubmitMenuAction(ECatLakeMainMenuAction::Close);
}

// 设置请求流程：只广播设置意图；正式设置页是否存在由 Controller 处理，蓝图扩展只做按钮反馈或动画。
void UCatLakeMainMenuWidget::RequestOpenSettings()
{
	SubmitMenuAction(ECatLakeMainMenuAction::OpenSettings);
}

// 保存请求流程：只广播保存意图；Save 子系统负责判断 Host、活动槽、busy 和磁盘结果。
void UCatLakeMainMenuWidget::RequestSave()
{
	SubmitMenuAction(ECatLakeMainMenuAction::Save);
}

// 退出到主菜单请求流程：只广播 ReturnToMainMenu 意图；保存、拆局、Session 销毁和旅行仍由 Controller/Online 链路决定。
void UCatLakeMainMenuWidget::RequestReturnToMainMenu()
{
	SubmitMenuAction(ECatLakeMainMenuAction::ReturnToMainMenu);
}

// 退出请求流程：只广播直接退出游戏意图；是否保存由玩家显式点击保存按钮决定。
void UCatLakeMainMenuWidget::RequestExitGame()
{
	SubmitMenuAction(ECatLakeMainMenuAction::ExitGame);
}

// 应用设置请求流程：只广播按钮语义；Controller 调用 SettingsModel::Apply 后决定留在设置页还是回菜单。
void UCatLakeMainMenuWidget::RequestApplySettings()
{
	SubmitMenuAction(ECatLakeMainMenuAction::ApplySettings);
}

// 取消设置请求流程：只广播取消语义；Controller 丢弃设置草稿并切回命令页。
void UCatLakeMainMenuWidget::RequestCancelSettings()
{
	SubmitMenuAction(ECatLakeMainMenuAction::CancelSettings);
}

// 恢复默认请求流程：只广播恢复默认语义；默认值仍由 SettingsModel 解释并保存在草稿里。
void UCatLakeMainMenuWidget::RequestRestoreSettingsDefaults()
{
	SubmitMenuAction(ECatLakeMainMenuAction::RestoreSettingsDefaults);
}

// 刷新输出设备请求流程：只广播刷新语义；设备枚举和失败文本都归 SettingsModel。
void UCatLakeMainMenuWidget::RequestRefreshAudioOutputDevices()
{
	SubmitMenuAction(ECatLakeMainMenuAction::RefreshAudioOutputDevices);
}

// 游戏分类请求流程：只广播分类语义；View 不通过字符串修改分类状态。
void UCatLakeMainMenuWidget::RequestSelectGameSettings()
{
	SubmitMenuAction(ECatLakeMainMenuAction::SelectGameSettings);
}

// 画面分类请求流程：只广播分类语义；View 不通过字符串修改分类状态。
void UCatLakeMainMenuWidget::RequestSelectGraphicsSettings()
{
	SubmitMenuAction(ECatLakeMainMenuAction::SelectGraphicsSettings);
}

// 声音分类请求流程：只广播分类语义；View 不通过字符串修改分类状态。
void UCatLakeMainMenuWidget::RequestSelectAudioSettings()
{
	SubmitMenuAction(ECatLakeMainMenuAction::SelectAudioSettings);
}

// 控制分类请求流程：只广播分类语义；当前分类只展示正式不可用说明。
void UCatLakeMainMenuWidget::RequestSelectControlsSettings()
{
	SubmitMenuAction(ECatLakeMainMenuAction::SelectControlsSettings);
}

// 构造流程：父类完成 WBP 变量绑定后打开焦点能力，随后绑定命令按钮和设置页输入，最后强制回到暂停命令页作为初始状态。
void UCatLakeMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	BindDesignerButtons();
	BindSettingsControls();
	ShowCommandMenu();
}

// 销毁流程：先解除命令按钮和设置页输入的动态绑定，避免下次 Slate 重建时重复 AddDynamic，再交还父类生命周期。
void UCatLakeMainMenuWidget::NativeDestruct()
{
	UnbindDesignerButtons();
	UnbindSettingsControls();
	Super::NativeDestruct();
}

// 预览键流程：子按钮处理前只识别普通 Escape；回主菜单等待中只消费不关闭，设置页中先取消回暂停菜单，命令页中才关闭菜单，Shift+Escape 继续透传。
FReply UCatLakeMainMenuWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (ShouldCloseMenuFromKey(InKeyEvent))
	{
		if (LastMenuViewState.bReturnToMainMenuPending)
		{
			return FReply::Handled();
		}
		IsShowingSettingsPanel() ? RequestCancelSettings() : RequestCloseMenu();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

// 键盘流程：当菜单根直接持有焦点时复用普通 Escape 分支；回主菜单等待中锁住关闭入口，Shift+Escape 和其它按键继续走父类默认处理。
FReply UCatLakeMainMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (ShouldCloseMenuFromKey(InKeyEvent))
	{
		if (LastMenuViewState.bReturnToMainMenuPending)
		{
			return FReply::Handled();
		}
		IsShowingSettingsPanel() ? RequestCancelSettings() : RequestCloseMenu();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

// Designer 按钮绑定流程：每个按钮先移除本对象失效绑定再新增，覆盖 Construct 重入和 WBP 热重建。
void UCatLakeMainMenuWidget::BindDesignerButtons()
{
	if (SettingsButton)
	{
		SettingsButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestOpenSettings);
		SettingsButton->OnClicked.AddDynamic(this, &ThisClass::RequestOpenSettings);
	}
	if (SaveButton)
	{
		SaveButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestSave);
		SaveButton->OnClicked.AddDynamic(this, &ThisClass::RequestSave);
	}
	if (ReturnToMainMenuButton)
	{
		ReturnToMainMenuButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestReturnToMainMenu);
		ReturnToMainMenuButton->OnClicked.AddDynamic(this, &ThisClass::RequestReturnToMainMenu);
	}
	if (ExitGameButton)
	{
		ExitGameButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestExitGame);
		ExitGameButton->OnClicked.AddDynamic(this, &ThisClass::RequestExitGame);
	}
	if (CloseButton)
	{
		CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestCloseMenu);
		CloseButton->OnClicked.AddDynamic(this, &ThisClass::RequestCloseMenu);
	}
}

// Designer 按钮解绑流程：只解除本类添加的动态委托，蓝图自己绑定的动画或声音反馈不被清掉。
void UCatLakeMainMenuWidget::UnbindDesignerButtons()
{
	if (SettingsButton)
	{
		SettingsButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestOpenSettings);
	}
	if (SaveButton)
	{
		SaveButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestSave);
	}
	if (ReturnToMainMenuButton)
	{
		ReturnToMainMenuButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestReturnToMainMenu);
	}
	if (ExitGameButton)
	{
		ExitGameButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestExitGame);
	}
	if (CloseButton)
	{
		CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestCloseMenu);
	}
}

// 设置模型订阅流程：只订阅 Controller 注入的当前模型；无模型时保持设置按钮由 Controller 禁用或降级。
void UCatLakeMainMenuWidget::BindSettingsModelChanges()
{
	if (UCatFrontendSettingsModel* Model = SettingsModel.Get(); Model && !SettingsModelChangedHandle.IsValid())
	{
		SettingsModelChangedHandle = Model->OnChanged.AddUObject(this, &ThisClass::HandleSettingsModelChanged);
	}
}

// 设置模型解绑流程：用保存的句柄从原模型移除刷新回调；模型已销毁或句柄无效时安全跳过。
void UCatLakeMainMenuWidget::UnbindSettingsModelChanges()
{
	if (UCatFrontendSettingsModel* Model = SettingsModel.Get(); Model && SettingsModelChangedHandle.IsValid())
	{
		Model->OnChanged.Remove(SettingsModelChangedHandle);
	}
	SettingsModelChangedHandle.Reset();
}

// 设置控件绑定流程：先移除失效动态委托再添加当前 View 的委托，和 UnbindSettingsControls 形成生命周期配对。
// 命令按钮只广播菜单 Action，草稿输入只写入 Controller 注入的 SettingsModel，避免局内设置页复制主界面业务状态。
void UCatLakeMainMenuWidget::BindSettingsControls()
{
	if (GameSettingsCategoryButton) { GameSettingsCategoryButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestSelectGameSettings); GameSettingsCategoryButton->OnClicked.AddDynamic(this, &ThisClass::RequestSelectGameSettings); }
	if (GraphicsSettingsCategoryButton) { GraphicsSettingsCategoryButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestSelectGraphicsSettings); GraphicsSettingsCategoryButton->OnClicked.AddDynamic(this, &ThisClass::RequestSelectGraphicsSettings); }
	if (AudioSettingsCategoryButton) { AudioSettingsCategoryButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestSelectAudioSettings); AudioSettingsCategoryButton->OnClicked.AddDynamic(this, &ThisClass::RequestSelectAudioSettings); }
	if (ControlsSettingsCategoryButton) { ControlsSettingsCategoryButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestSelectControlsSettings); ControlsSettingsCategoryButton->OnClicked.AddDynamic(this, &ThisClass::RequestSelectControlsSettings); }
	if (ApplySettingsButton) { ApplySettingsButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestApplySettings); ApplySettingsButton->OnClicked.AddDynamic(this, &ThisClass::RequestApplySettings); }
	if (RestoreSettingsDefaultsButton) { RestoreSettingsDefaultsButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestRestoreSettingsDefaults); RestoreSettingsDefaultsButton->OnClicked.AddDynamic(this, &ThisClass::RequestRestoreSettingsDefaults); }
	if (CancelSettingsButton) { CancelSettingsButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestCancelSettings); CancelSettingsButton->OnClicked.AddDynamic(this, &ThisClass::RequestCancelSettings); }
	if (RefreshAudioOutputDevicesButton) { RefreshAudioOutputDevicesButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestRefreshAudioOutputDevices); RefreshAudioOutputDevicesButton->OnClicked.AddDynamic(this, &ThisClass::RequestRefreshAudioOutputDevices); }
	if (LanguageComboBox) { LanguageComboBox->OnSelectionChanged.RemoveDynamic(this, &ThisClass::HandleLanguageSelectionChanged); LanguageComboBox->OnSelectionChanged.AddDynamic(this, &ThisClass::HandleLanguageSelectionChanged); }
	if (FullscreenModeComboBox) { FullscreenModeComboBox->OnSelectionChanged.RemoveDynamic(this, &ThisClass::HandleFullscreenModeSelectionChanged); FullscreenModeComboBox->OnSelectionChanged.AddDynamic(this, &ThisClass::HandleFullscreenModeSelectionChanged); }
	if (ScreenResolutionComboBox) { ScreenResolutionComboBox->OnSelectionChanged.RemoveDynamic(this, &ThisClass::HandleScreenResolutionSelectionChanged); ScreenResolutionComboBox->OnSelectionChanged.AddDynamic(this, &ThisClass::HandleScreenResolutionSelectionChanged); }
	if (OverallQualityComboBox) { OverallQualityComboBox->OnSelectionChanged.RemoveDynamic(this, &ThisClass::HandleQualitySelectionChanged); OverallQualityComboBox->OnSelectionChanged.AddDynamic(this, &ThisClass::HandleQualitySelectionChanged); }
	if (VSyncCheckBox) { VSyncCheckBox->OnCheckStateChanged.RemoveDynamic(this, &ThisClass::HandleVSyncChanged); VSyncCheckBox->OnCheckStateChanged.AddDynamic(this, &ThisClass::HandleVSyncChanged); }
	if (UIScaleSlider) { UIScaleSlider->OnValueChanged.RemoveDynamic(this, &ThisClass::HandleUIScaleChanged); UIScaleSlider->OnValueChanged.AddDynamic(this, &ThisClass::HandleUIScaleChanged); }
	if (BrightnessSlider) { BrightnessSlider->OnValueChanged.RemoveDynamic(this, &ThisClass::HandleBrightnessChanged); BrightnessSlider->OnValueChanged.AddDynamic(this, &ThisClass::HandleBrightnessChanged); }
	if (VibrationCheckBox) { VibrationCheckBox->OnCheckStateChanged.RemoveDynamic(this, &ThisClass::HandleVibrationChanged); VibrationCheckBox->OnCheckStateChanged.AddDynamic(this, &ThisClass::HandleVibrationChanged); }
	if (VoiceChatCheckBox) { VoiceChatCheckBox->OnCheckStateChanged.RemoveDynamic(this, &ThisClass::HandleVoiceChatChanged); VoiceChatCheckBox->OnCheckStateChanged.AddDynamic(this, &ThisClass::HandleVoiceChatChanged); }
	if (MuteAudioWhenUnfocusedCheckBox) { MuteAudioWhenUnfocusedCheckBox->OnCheckStateChanged.RemoveDynamic(this, &ThisClass::HandleMuteAudioWhenUnfocusedChanged); MuteAudioWhenUnfocusedCheckBox->OnCheckStateChanged.AddDynamic(this, &ThisClass::HandleMuteAudioWhenUnfocusedChanged); }
	if (AudioOutputDeviceComboBox) { AudioOutputDeviceComboBox->OnSelectionChanged.RemoveDynamic(this, &ThisClass::HandleAudioOutputDeviceSelectionChanged); AudioOutputDeviceComboBox->OnSelectionChanged.AddDynamic(this, &ThisClass::HandleAudioOutputDeviceSelectionChanged); }
	if (MasterVolumeSlider) { MasterVolumeSlider->OnValueChanged.RemoveDynamic(this, &ThisClass::HandleMasterVolumeChanged); MasterVolumeSlider->OnValueChanged.AddDynamic(this, &ThisClass::HandleMasterVolumeChanged); }
	if (MusicVolumeSlider) { MusicVolumeSlider->OnValueChanged.RemoveDynamic(this, &ThisClass::HandleMusicVolumeChanged); MusicVolumeSlider->OnValueChanged.AddDynamic(this, &ThisClass::HandleMusicVolumeChanged); }
	if (SFXVolumeSlider) { SFXVolumeSlider->OnValueChanged.RemoveDynamic(this, &ThisClass::HandleSFXVolumeChanged); SFXVolumeSlider->OnValueChanged.AddDynamic(this, &ThisClass::HandleSFXVolumeChanged); }
	if (AmbienceVolumeSlider) { AmbienceVolumeSlider->OnValueChanged.RemoveDynamic(this, &ThisClass::HandleAmbienceVolumeChanged); AmbienceVolumeSlider->OnValueChanged.AddDynamic(this, &ThisClass::HandleAmbienceVolumeChanged); }
	if (VoiceVolumeSlider) { VoiceVolumeSlider->OnValueChanged.RemoveDynamic(this, &ThisClass::HandleVoiceVolumeChanged); VoiceVolumeSlider->OnValueChanged.AddDynamic(this, &ThisClass::HandleVoiceVolumeChanged); }
}

// 这里处理 NativeDestruct 和资产重建时的委托成对清理；只移除本 View 添加的动态委托，保留蓝图事件图中纯表现绑定。
void UCatLakeMainMenuWidget::UnbindSettingsControls()
{
	if (GameSettingsCategoryButton) { GameSettingsCategoryButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestSelectGameSettings); }
	if (GraphicsSettingsCategoryButton) { GraphicsSettingsCategoryButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestSelectGraphicsSettings); }
	if (AudioSettingsCategoryButton) { AudioSettingsCategoryButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestSelectAudioSettings); }
	if (ControlsSettingsCategoryButton) { ControlsSettingsCategoryButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestSelectControlsSettings); }
	if (ApplySettingsButton) { ApplySettingsButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestApplySettings); }
	if (RestoreSettingsDefaultsButton) { RestoreSettingsDefaultsButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestRestoreSettingsDefaults); }
	if (CancelSettingsButton) { CancelSettingsButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestCancelSettings); }
	if (RefreshAudioOutputDevicesButton) { RefreshAudioOutputDevicesButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestRefreshAudioOutputDevices); }
	if (LanguageComboBox) { LanguageComboBox->OnSelectionChanged.RemoveDynamic(this, &ThisClass::HandleLanguageSelectionChanged); }
	if (FullscreenModeComboBox) { FullscreenModeComboBox->OnSelectionChanged.RemoveDynamic(this, &ThisClass::HandleFullscreenModeSelectionChanged); }
	if (ScreenResolutionComboBox) { ScreenResolutionComboBox->OnSelectionChanged.RemoveDynamic(this, &ThisClass::HandleScreenResolutionSelectionChanged); }
	if (OverallQualityComboBox) { OverallQualityComboBox->OnSelectionChanged.RemoveDynamic(this, &ThisClass::HandleQualitySelectionChanged); }
	if (VSyncCheckBox) { VSyncCheckBox->OnCheckStateChanged.RemoveDynamic(this, &ThisClass::HandleVSyncChanged); }
	if (UIScaleSlider) { UIScaleSlider->OnValueChanged.RemoveDynamic(this, &ThisClass::HandleUIScaleChanged); }
	if (BrightnessSlider) { BrightnessSlider->OnValueChanged.RemoveDynamic(this, &ThisClass::HandleBrightnessChanged); }
	if (VibrationCheckBox) { VibrationCheckBox->OnCheckStateChanged.RemoveDynamic(this, &ThisClass::HandleVibrationChanged); }
	if (VoiceChatCheckBox) { VoiceChatCheckBox->OnCheckStateChanged.RemoveDynamic(this, &ThisClass::HandleVoiceChatChanged); }
	if (MuteAudioWhenUnfocusedCheckBox) { MuteAudioWhenUnfocusedCheckBox->OnCheckStateChanged.RemoveDynamic(this, &ThisClass::HandleMuteAudioWhenUnfocusedChanged); }
	if (AudioOutputDeviceComboBox) { AudioOutputDeviceComboBox->OnSelectionChanged.RemoveDynamic(this, &ThisClass::HandleAudioOutputDeviceSelectionChanged); }
	if (MasterVolumeSlider) { MasterVolumeSlider->OnValueChanged.RemoveDynamic(this, &ThisClass::HandleMasterVolumeChanged); }
	if (MusicVolumeSlider) { MusicVolumeSlider->OnValueChanged.RemoveDynamic(this, &ThisClass::HandleMusicVolumeChanged); }
	if (SFXVolumeSlider) { SFXVolumeSlider->OnValueChanged.RemoveDynamic(this, &ThisClass::HandleSFXVolumeChanged); }
	if (AmbienceVolumeSlider) { AmbienceVolumeSlider->OnValueChanged.RemoveDynamic(this, &ThisClass::HandleAmbienceVolumeChanged); }
	if (VoiceVolumeSlider) { VoiceVolumeSlider->OnValueChanged.RemoveDynamic(this, &ThisClass::HandleVoiceVolumeChanged); }
}

// 这里把主界面设置模型投影成局内设置页：无模型时只显示明确降级文本，不尝试写配置或关闭菜单。
// 有模型时先打开回填保护，再按当前分类切换四个面板，随后重建语言、窗口、分辨率、质量和音频输出下拉项。
// 音频输出显示项会重新映射到设备 ID；语音输入和麦克风保持正式不可用状态，结果文本和蓝图扩展点最后刷新。
void UCatLakeMainMenuWidget::HandleSettingsModelChanged()
{
	UCatFrontendSettingsModel* Model = SettingsModel.Get();
	if (!Model)
	{
		if (FrontendSettingsResultTextBlock)
		{
			FrontendSettingsResultTextBlock->SetText(FText::FromString(TEXT("设置服务当前不可用。")));
			FrontendSettingsResultTextBlock->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		return;
	}

	bRefreshingSettingsControls = true;
	if (GameSettingsPanel) { GameSettingsPanel->SetVisibility(Model->IsGameSelected() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	if (GraphicsSettingsPanel) { GraphicsSettingsPanel->SetVisibility(Model->IsGraphicsSelected() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	if (AudioSettingsPanel) { AudioSettingsPanel->SetVisibility(Model->IsAudioSelected() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	if (ControlsSettingsPanel) { ControlsSettingsPanel->SetVisibility(Model->IsControlsSelected() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	if (SettingsDescriptionTextBlock)
	{
		SettingsDescriptionTextBlock->SetText(FText::FromString(Model->IsGameSelected() ? TEXT("语言、语音与手柄震动")
			: Model->IsGraphicsSelected() ? TEXT("显示、画质、亮度与界面缩放")
			: Model->IsAudioSelected() ? TEXT("音量、输出设备与后台声音") : TEXT("控制细项暂未开放")));
	}
	if (LanguageComboBox)
	{
		LanguageComboBox->ClearOptions();
		for (int32 Index = 0; Index < Model->GetAvailableLanguageCount(); ++Index)
		{
			LanguageComboBox->AddOption(Model->GetAvailableLanguage(Index));
		}
		LanguageComboBox->SetSelectedOption(Model->GetDraftLanguage());
	}
	if (FullscreenModeComboBox)
	{
		FullscreenModeComboBox->ClearOptions();
		FullscreenModeComboBox->AddOption(TEXT("全屏"));
		FullscreenModeComboBox->AddOption(TEXT("无边框窗口"));
		FullscreenModeComboBox->AddOption(TEXT("窗口"));
		const EWindowMode::Type Mode = Model->GetDraftFullscreenMode();
		if (Mode == EWindowMode::Fullscreen) { FullscreenModeComboBox->SetSelectedOption(TEXT("全屏")); }
		else if (Mode == EWindowMode::WindowedFullscreen) { FullscreenModeComboBox->SetSelectedOption(TEXT("无边框窗口")); }
		else if (Mode == EWindowMode::Windowed) { FullscreenModeComboBox->SetSelectedOption(TEXT("窗口")); }
		else { FullscreenModeComboBox->ClearSelection(); }
	}
	if (ScreenResolutionComboBox)
	{
		TArray<FIntPoint> Resolutions;
		ScreenResolutionComboBox->ClearOptions();
		if (Model->GetSupportedScreenResolutions(Resolutions))
		{
			for (const FIntPoint& Resolution : Resolutions)
			{
				ScreenResolutionComboBox->AddOption(FString::Printf(TEXT("%d x %d"), Resolution.X, Resolution.Y));
			}
			const FIntPoint Resolution = Model->GetDraftScreenResolution();
			ScreenResolutionComboBox->SetSelectedOption(FString::Printf(TEXT("%d x %d"), Resolution.X, Resolution.Y));
		}
		ScreenResolutionComboBox->SetIsEnabled(Resolutions.Num() > 0);
	}
	if (OverallQualityComboBox)
	{
		OverallQualityComboBox->ClearOptions();
		OverallQualityComboBox->AddOption(TEXT("自定义"));
		OverallQualityComboBox->AddOption(TEXT("低"));
		OverallQualityComboBox->AddOption(TEXT("中"));
		OverallQualityComboBox->AddOption(TEXT("高"));
		OverallQualityComboBox->AddOption(TEXT("史诗"));
		OverallQualityComboBox->AddOption(TEXT("电影"));
		const int32 Level = Model->GetDraftOverallScalabilityLevel();
		if (Level >= -1 && Level <= 4)
		{
			OverallQualityComboBox->SetSelectedOption(Level == -1 ? TEXT("自定义") : Level == 0 ? TEXT("低")
				: Level == 1 ? TEXT("中") : Level == 2 ? TEXT("高") : Level == 3 ? TEXT("史诗") : TEXT("电影"));
		}
		else
		{
			OverallQualityComboBox->ClearSelection();
		}
	}
	if (VSyncCheckBox) { VSyncCheckBox->SetIsChecked(Model->GetDraftVSyncEnabled()); }
	if (UIScaleSlider) { UIScaleSlider->SetValue((Model->GetDraftUIScale() - 0.75f) / 1.25f); }
	if (BrightnessSlider)
	{
		BrightnessSlider->SetValue((Model->GetDraftDisplayGamma() - 0.5f) / 4.5f);
		BrightnessSlider->SetIsEnabled(Model->IsBrightnessSettingAvailable());
	}
	if (VibrationCheckBox)
	{
		VibrationCheckBox->SetIsChecked(Model->GetDraftVibrationEnabled());
		VibrationCheckBox->SetIsEnabled(Model->IsVibrationSettingAvailable());
	}
	if (VoiceChatCheckBox)
	{
		VoiceChatCheckBox->SetIsChecked(Model->GetDraftVoiceChatEnabled());
		VoiceChatCheckBox->SetIsEnabled(Model->IsVoiceChatSettingAvailable());
	}
	if (MuteAudioWhenUnfocusedCheckBox) { MuteAudioWhenUnfocusedCheckBox->SetIsChecked(Model->GetDraftMuteAudioWhenUnfocused()); }
	if (VoiceInputModeUnavailableText)
	{
		VoiceInputModeUnavailableText->SetText(FText::FromString(Model->IsInputModeSettingAvailable()
			? TEXT("当前语音输入方式由平台管理。") : TEXT("当前语音服务不支持切换输入模式。")));
		VoiceInputModeUnavailableText->SetVisibility(ESlateVisibility::Visible);
		VoiceInputModeUnavailableText->SetIsEnabled(false);
	}
	if (VoiceInputModeComboBox)
	{
		VoiceInputModeComboBox->ClearOptions();
		VoiceInputModeComboBox->AddOption(TEXT("当前平台不支持此设置"));
		VoiceInputModeComboBox->SetSelectedOption(TEXT("当前平台不支持此设置"));
		VoiceInputModeComboBox->SetIsEnabled(false);
	}
	if (MicrophoneUnavailableText)
	{
		MicrophoneUnavailableText->SetText(FText::FromString(Model->IsMicrophoneSettingAvailable()
			? TEXT("当前麦克风由平台管理。") : TEXT("当前语音服务不支持选择麦克风，请在系统声音设置中更改默认输入设备。")));
		MicrophoneUnavailableText->SetVisibility(ESlateVisibility::Visible);
		MicrophoneUnavailableText->SetIsEnabled(false);
	}
	if (MicrophoneComboBox)
	{
		MicrophoneComboBox->ClearOptions();
		MicrophoneComboBox->AddOption(TEXT("当前平台不支持此设置"));
		MicrophoneComboBox->SetSelectedOption(TEXT("当前平台不支持此设置"));
		MicrophoneComboBox->SetIsEnabled(false);
	}
	if (AudioOutputDeviceComboBox)
	{
		AudioOutputDeviceIdsByOption.Reset();
		AudioOutputDeviceComboBox->ClearOptions();
		FString SelectedOption;
		for (int32 Index = 0; Index < Model->GetAudioOutputDeviceCount(); ++Index)
		{
			const FString& DeviceName = Model->GetAudioOutputDeviceName(Index);
			const FString& DeviceId = Model->GetAudioOutputDeviceId(Index);
			if (DeviceName.IsEmpty() || DeviceId.IsEmpty()) { continue; }
			FString Option = DeviceName;
			for (int32 DuplicateIndex = 2; AudioOutputDeviceIdsByOption.Contains(Option); ++DuplicateIndex)
			{
				Option = FString::Printf(TEXT("%s (%d)"), *DeviceName, DuplicateIndex);
			}
			AudioOutputDeviceIdsByOption.Add(Option, DeviceId);
			AudioOutputDeviceComboBox->AddOption(Option);
			if (DeviceId == Model->GetDraftAudioOutputDeviceId()) { SelectedOption = Option; }
		}
		AudioOutputDeviceComboBox->SetSelectedOption(SelectedOption);
		AudioOutputDeviceComboBox->SetIsEnabled(Model->IsOutputDeviceSettingAvailable()
			&& !Model->IsAudioOutputDeviceOperationPending());
	}
	if (RefreshAudioOutputDevicesButton)
	{
		RefreshAudioOutputDevicesButton->SetIsEnabled(!Model->IsAudioOutputDeviceOperationPending());
	}
	const bool bAudioAvailable = Model->IsAudioRoutingAvailable();
	if (MasterVolumeSlider) { MasterVolumeSlider->SetValue(Model->GetDraftMasterVolume()); MasterVolumeSlider->SetIsEnabled(bAudioAvailable); }
	if (MusicVolumeSlider) { MusicVolumeSlider->SetValue(Model->GetDraftMusicVolume()); MusicVolumeSlider->SetIsEnabled(bAudioAvailable); }
	if (SFXVolumeSlider) { SFXVolumeSlider->SetValue(Model->GetDraftSFXVolume()); SFXVolumeSlider->SetIsEnabled(bAudioAvailable); }
	if (AmbienceVolumeSlider) { AmbienceVolumeSlider->SetValue(Model->GetDraftAmbienceVolume()); AmbienceVolumeSlider->SetIsEnabled(bAudioAvailable); }
	if (VoiceVolumeSlider) { VoiceVolumeSlider->SetValue(Model->GetDraftVoiceVolume()); VoiceVolumeSlider->SetIsEnabled(bAudioAvailable); }
	if (FrontendSettingsResultTextBlock)
	{
		const FText& ResultText = Model->GetLastResultText();
		FrontendSettingsResultTextBlock->SetText(ResultText);
		FrontendSettingsResultTextBlock->SetVisibility(ResultText.IsEmpty()
			? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	bRefreshingSettingsControls = false;
	BP_RenderSettings();
}

// 意图提交流程：先把 Action 交给菜单 Controller 处理权威系统，再通知蓝图做不改变业务状态的表现扩展。
void UCatLakeMainMenuWidget::SubmitMenuAction(const ECatLakeMainMenuAction Action)
{
	OnActionRequested.Broadcast(Action);
	BP_HandleMenuAction(Action);
}

// 关闭键判断流程：只消费菜单已经获得焦点后的普通 Escape；PIE 的 Shift+Escape 停止运行快捷键继续交给编辑器处理。
bool UCatLakeMainMenuWidget::ShouldCloseMenuFromKey(const FKeyEvent& InKeyEvent) const
{
	if (InKeyEvent.GetKey() != EKeys::Escape)
	{
		return false;
	}
#if WITH_EDITOR
	if (InKeyEvent.IsShiftDown())
	{
		return false;
	}
#endif
	return true;
}

// 语言选择流程：回填保护外把玩家选择交给 SettingsModel；未打包语言会由 Model 拒绝并刷新结果文本。
void UCatLakeMainMenuWidget::HandleLanguageSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (!bRefreshingSettingsControls)
	{
		if (UCatFrontendSettingsModel* Model = SettingsModel.Get()) { Model->SetDraftLanguage(SelectedItem); }
	}
}

// 窗口模式选择流程：只接受与主界面一致的三个显示项；未知项记录诊断并保持原草稿。
void UCatLakeMainMenuWidget::HandleFullscreenModeSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	UCatFrontendSettingsModel* Model = SettingsModel.Get();
	if (bRefreshingSettingsControls || !Model) { return; }
	EWindowMode::Type WindowMode;
	if (SelectedItem == TEXT("全屏")) { WindowMode = EWindowMode::Fullscreen; }
	else if (SelectedItem == TEXT("窗口")) { WindowMode = EWindowMode::Windowed; }
	else if (SelectedItem == TEXT("无边框窗口")) { WindowMode = EWindowMode::WindowedFullscreen; }
	else
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=lake_menu_setting_selection_rejected Control=FullscreenModeComboBox Reason=unknown_option"));
		return;
	}
	Model->SetDraftFullscreenMode(WindowMode);
}

// 分辨率选择流程：从显示文本拆出宽高后交给 SettingsModel；无法拆分时不猜测当前窗口尺寸。
void UCatLakeMainMenuWidget::HandleScreenResolutionSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (bRefreshingSettingsControls)
	{
		return;
	}
	if (UCatFrontendSettingsModel* Model = SettingsModel.Get())
	{
		FString WidthText;
		FString HeightText;
		if (SelectedItem.Split(TEXT(" x "), &WidthText, &HeightText))
		{
			Model->SetDraftScreenResolution(FIntPoint(FCString::Atoi(*WidthText), FCString::Atoi(*HeightText)));
		}
	}
}

// 画质选择流程：显示项到 UE 质量档的映射与主界面保持一致，未知项不写草稿。
void UCatLakeMainMenuWidget::HandleQualitySelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	UCatFrontendSettingsModel* Model = SettingsModel.Get();
	if (bRefreshingSettingsControls || !Model) { return; }
	int32 QualityLevel;
	if (SelectedItem == TEXT("自定义")) { QualityLevel = -1; }
	else if (SelectedItem == TEXT("低")) { QualityLevel = 0; }
	else if (SelectedItem == TEXT("中")) { QualityLevel = 1; }
	else if (SelectedItem == TEXT("高")) { QualityLevel = 2; }
	else if (SelectedItem == TEXT("史诗")) { QualityLevel = 3; }
	else if (SelectedItem == TEXT("电影")) { QualityLevel = 4; }
	else
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=lake_menu_setting_selection_rejected Control=OverallQualityComboBox Reason=unknown_option"));
		return;
	}
	Model->SetDraftOverallScalabilityLevel(QualityLevel);
}

// 垂直同步输入流程：玩家勾选只写设置草稿，真实帧同步等待应用动作。
void UCatLakeMainMenuWidget::HandleVSyncChanged(bool bIsChecked)
{
	if (!bRefreshingSettingsControls)
	{
		if (UCatFrontendSettingsModel* Model = SettingsModel.Get()) { Model->SetDraftVSyncEnabled(bIsChecked); }
	}
}

// UI 比例输入流程：把局内 View 的归一化滑块值换算为 SettingsModel 的正式比例范围。
void UCatLakeMainMenuWidget::HandleUIScaleChanged(float NormalizedValue)
{
	if (!bRefreshingSettingsControls)
	{
		if (UCatFrontendSettingsModel* Model = SettingsModel.Get()) { Model->SetDraftUIScale(0.75f + NormalizedValue * 1.25f); }
	}
}

// 亮度输入流程：把局内 View 的归一化滑块值换算为 SettingsModel 的 Gamma 范围。
void UCatLakeMainMenuWidget::HandleBrightnessChanged(float NormalizedValue)
{
	if (!bRefreshingSettingsControls)
	{
		if (UCatFrontendSettingsModel* Model = SettingsModel.Get()) { Model->SetDraftDisplayGamma(0.5f + NormalizedValue * 4.5f); }
	}
}

// 震动输入流程：只有当前 Controller 可写 ForceFeedback gate 时才把选择保存为草稿。
void UCatLakeMainMenuWidget::HandleVibrationChanged(bool bIsChecked)
{
	if (!bRefreshingSettingsControls)
	{
		if (UCatFrontendSettingsModel* Model = SettingsModel.Get(); Model && Model->IsVibrationSettingAvailable()) { Model->SetDraftVibrationEnabled(bIsChecked); }
	}
}

// 网络语音输入流程：只有 OSS Voice 可用时才写草稿，平台不可用时控件保持禁用。
void UCatLakeMainMenuWidget::HandleVoiceChatChanged(bool bIsChecked)
{
	if (!bRefreshingSettingsControls)
	{
		if (UCatFrontendSettingsModel* Model = SettingsModel.Get(); Model && Model->IsVoiceChatSettingAvailable()) { Model->SetDraftVoiceChatEnabled(bIsChecked); }
	}
}

// 失焦静音输入流程：只写草稿，当前窗口的实际失焦音量在应用后才更新。
void UCatLakeMainMenuWidget::HandleMuteAudioWhenUnfocusedChanged(bool bIsChecked)
{
	if (!bRefreshingSettingsControls)
	{
		if (UCatFrontendSettingsModel* Model = SettingsModel.Get()) { Model->SetDraftMuteAudioWhenUnfocused(bIsChecked); }
	}
}

// 输出设备选择流程：用本次刷新建立的显示项映射找正式设备 ID；映射缺失时忽略失效 UI 选择。
void UCatLakeMainMenuWidget::HandleAudioOutputDeviceSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (bRefreshingSettingsControls)
	{
		return;
	}
	if (UCatFrontendSettingsModel* Model = SettingsModel.Get())
	{
		if (const FString* DeviceId = AudioOutputDeviceIdsByOption.Find(SelectedItem))
		{
			Model->SetDraftAudioOutputDeviceId(*DeviceId);
		}
	}
}

// 主音量输入流程：写入 SettingsModel 主混音草稿，真实 AudioDevice 覆写仍等待应用。
void UCatLakeMainMenuWidget::HandleMasterVolumeChanged(float Value)
{
	if (!bRefreshingSettingsControls)
	{
		if (UCatFrontendSettingsModel* Model = SettingsModel.Get()) { Model->SetDraftMasterVolume(Value); }
	}
}

// 音乐音量输入流程：写入 SettingsModel 音乐混音草稿，缺少正式音频路由时控件会被刷新禁用。
void UCatLakeMainMenuWidget::HandleMusicVolumeChanged(float Value)
{
	if (!bRefreshingSettingsControls)
	{
		if (UCatFrontendSettingsModel* Model = SettingsModel.Get()) { Model->SetDraftMusicVolume(Value); }
	}
}

// 音效音量输入流程：写入 SettingsModel 音效混音草稿，缺少正式音频路由时控件会被刷新禁用。
void UCatLakeMainMenuWidget::HandleSFXVolumeChanged(float Value)
{
	if (!bRefreshingSettingsControls)
	{
		if (UCatFrontendSettingsModel* Model = SettingsModel.Get()) { Model->SetDraftSFXVolume(Value); }
	}
}

// 环境音输入流程：写入 SettingsModel 环境混音草稿，缺少正式音频路由时控件会被刷新禁用。
void UCatLakeMainMenuWidget::HandleAmbienceVolumeChanged(float Value)
{
	if (!bRefreshingSettingsControls)
	{
		if (UCatFrontendSettingsModel* Model = SettingsModel.Get()) { Model->SetDraftAmbienceVolume(Value); }
	}
}

// 语音音量输入流程：写入 SettingsModel 语音分类音量草稿，不改变网络语音开关。
void UCatLakeMainMenuWidget::HandleVoiceVolumeChanged(float Value)
{
	if (!bRefreshingSettingsControls)
	{
		if (UCatFrontendSettingsModel* Model = SettingsModel.Get()) { Model->SetDraftVoiceVolume(Value); }
	}
}

#undef LOCTEXT_NAMESPACE
