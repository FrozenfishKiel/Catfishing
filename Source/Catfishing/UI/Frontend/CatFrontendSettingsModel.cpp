#include "UI/Frontend/CatFrontendSettingsModel.h"

#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Kismet/KismetInternationalizationLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Logging/CatLog.h"
#include "Settings/CatAudioOutputRequest.h"
#include "Settings/CatGameUserSettings.h"

#define LOCTEXT_NAMESPACE "CatFrontendSettingsModel"

// SettingsModel 绑定流程把设置页接到正式项目设置类，避免打开前端页面时重复应用运行时设置：
// 1. 先要求 LocalPlayer 和正式 UCatGameUserSettings 同时可用，避免页面在错误的玩家或基类设置来源上运行。
// 2. 保存弱 LocalPlayer 引用并从正式来源读取完整草稿，草稿此时尚未改动窗口、国际化或音频。
// 3. 只请求设备枚举并发布首次刷新；运行时震动、语音和音量的恢复归正式设置宿主的 World/Controller 生命周期，避免打开页面重复应用。
bool UCatFrontendSettingsModel::Initialize(ULocalPlayer* InLocalPlayer)
{
	Shutdown();
	if (!InLocalPlayer)
	{
		PublishChanged(LOCTEXT("SettingsUnavailable", "设置来源不可用。"));
		return false;
	}

	UCatGameUserSettings* ResolvedUserSettings = UCatGameUserSettings::Get();
	if (!ResolvedUserSettings)
	{
		PublishChanged(LOCTEXT("SettingsClassUnavailable", "项目设置类未装配。"));
		return false;
	}

	BoundLocalPlayer = InLocalPlayer;
	UserSettings = ResolvedUserSettings;
	ReloadDraftFromSettings();
	AvailableLanguages = UKismetInternationalizationLibrary::GetLocalizedCultures(true, false, false, false);
	if (!AvailableLanguages.Contains(DraftLanguage))
	{
		AvailableLanguages.Add(DraftLanguage);
	}
	RefreshAudioOutputDevices();
	PublishChanged(LOCTEXT("SettingsReady", "设置已读取。"));
	return true;
}

// 关闭流程：
// 1. 丢弃未提交的草稿，防止同一 Model 被后续 LocalPlayer 复用时串入前一个玩家设置。
// 2. 递增异步请求代次、Cancel 活动请求的轮询和结果接收并释放它，使失效结果不能覆盖后续初始化；已受理的硬件切换无法撤回。
// 3. 清除用户设置强引用和 LocalPlayer 弱引用，不调用 Apply 或 SaveSettings。
// 4. 发布最终刷新，使仍存活的 View 读取安全默认值而不是悬挂的失效状态。
void UCatFrontendSettingsModel::Shutdown()
{
	BoundLocalPlayer.Reset();
	UserSettings = nullptr;
	DraftLanguage.Reset();
	AvailableLanguages.Reset();
	DraftFullscreenMode = EWindowMode::WindowedFullscreen;
	DraftScreenResolution = FIntPoint::ZeroValue;
	DraftOverallScalabilityLevel = -1;
	bDraftVSyncEnabled = false;
	DraftUIScale = 1.0f;
	DraftDisplayGamma = 2.2f;
	bDraftVibrationEnabled = true;
	bDraftVoiceChatEnabled = false;
	DraftMasterVolume = 1.0f;
	DraftMusicVolume = 1.0f;
	DraftSFXVolume = 1.0f;
	DraftAmbienceVolume = 1.0f;
	DraftVoiceVolume = 1.0f;
	bDraftMuteAudioWhenUnfocused = true;
	AudioOutputDeviceNames.Reset();
	AudioOutputDeviceIds.Reset();
	SystemDefaultAudioOutputDeviceId.Reset();
	DraftAudioOutputDeviceId.Reset();
	++AudioOutputDeviceRequestGeneration;
	if (ActiveAudioOutputRequest)
	{
		ActiveAudioOutputRequest->Cancel();
	}
	ActiveAudioOutputRequest = nullptr;
	bDraftDefaultsRequested = false;
	bPendingAudioOutputDefaultRestore = false;
	LastResultText = FText::GetEmpty();
	OnChanged.Broadcast();
}

// 游戏分类选择流程：将四个互斥标志切到游戏项，再发布刷新；不通过页面动作枚举或文本路由推断目标分类。
void UCatFrontendSettingsModel::SelectGame()
{
	bGameSelected = true;
	bGraphicsSelected = false;
	bAudioSelected = false;
	bControlsSelected = false;
	PublishChanged(LOCTEXT("GameCategorySelected", "已选择游戏设置。"));
}

// 画面分类选择流程：将四个互斥标志切到画面项，再发布刷新；草稿本身保持不变。
void UCatFrontendSettingsModel::SelectGraphics()
{
	bGameSelected = false;
	bGraphicsSelected = true;
	bAudioSelected = false;
	bControlsSelected = false;
	PublishChanged(LOCTEXT("GraphicsCategorySelected", "已选择画面设置。"));
}

// 声音分类选择流程：将四个互斥标志切到声音项，再发布刷新；缺资产时 View 仍能从可用性接口显示真实原因。
void UCatFrontendSettingsModel::SelectAudio()
{
	bGameSelected = false;
	bGraphicsSelected = false;
	bAudioSelected = true;
	bControlsSelected = false;
	PublishChanged(LOCTEXT("AudioCategorySelected", "已选择声音设置。"));
}

// 控制分类选择流程：将四个互斥标志切到控制项，再发布刷新；当前不为控制创建假映射或本地按键表。
void UCatFrontendSettingsModel::SelectControls()
{
	bGameSelected = false;
	bGraphicsSelected = false;
	bAudioSelected = false;
	bControlsSelected = true;
	PublishChanged(LOCTEXT("ControlsCategorySelected", "已选择控制设置。"));
}

// 游戏分类读取流程：返回当前互斥选择位，不读取或修改任何引擎设置。
bool UCatFrontendSettingsModel::IsGameSelected() const
{
	return bGameSelected;
}

// 画面分类读取流程：返回当前互斥选择位，不读取或修改任何引擎设置。
bool UCatFrontendSettingsModel::IsGraphicsSelected() const
{
	return bGraphicsSelected;
}

// 声音分类读取流程：返回当前互斥选择位，不读取或修改任何引擎设置。
bool UCatFrontendSettingsModel::IsAudioSelected() const
{
	return bAudioSelected;
}

// 控制分类读取流程：返回当前互斥选择位，不将分类入口误解为控制设置能力已经存在。
bool UCatFrontendSettingsModel::IsControlsSelected() const
{
	return bControlsSelected;
}

// 语言草稿读取流程：返回本地未应用值；调用方不得通过返回引用修改草稿或直接调用国际化系统。
const FString& UCatFrontendSettingsModel::GetDraftLanguage() const
{
	return DraftLanguage;
}

// 语言草稿写入流程：空值与未变化输入保持现状；其余输入只记录为页面草稿并通知 View，真实 culture 切换只发生在 Apply。
void UCatFrontendSettingsModel::SetDraftLanguage(const FString& NewLanguage)
{
	if (NewLanguage.IsEmpty() || DraftLanguage == NewLanguage || !AvailableLanguages.Contains(NewLanguage))
	{
		return;
	}

	DraftLanguage = NewLanguage;
	PublishChanged(LOCTEXT("LanguageDraftChanged", "语言设置等待应用。"));
}

// 语言数量读取流程：返回本地化资源系统在初始化时发现的 culture 数量；不扫描引擎全部 culture，避免未随游戏打包的语言成为可选项。
int32 UCatFrontendSettingsModel::GetAvailableLanguageCount() const
{
	return AvailableLanguages.Num();
}

// 语言读取流程：只从初始化时的正式 culture 列表读取；无效索引返回静态空值，View 不能把展示文本反向当作 culture 标识。
const FString& UCatFrontendSettingsModel::GetAvailableLanguage(const int32 LanguageIndex) const
{
	static const FString EmptyLanguage;
	return AvailableLanguages.IsValidIndex(LanguageIndex) ? AvailableLanguages[LanguageIndex] : EmptyLanguage;
}

// 语音聊天可用性读取流程：用绑定 World 精确查询 OSS Voice 接口；Steam 接口有效时允许提交，当前无平台或接口初始化失败时保持禁用。
bool UCatFrontendSettingsModel::IsVoiceChatSettingAvailable() const
{
	return UserSettings && UserSettings->HasVoiceChatSupport(GetLocalPlayerWorld());
}

// 语音聊天草稿读取流程：返回页面本地开关，不提前开始或停止 OSS 的网络语音处理。
bool UCatFrontendSettingsModel::GetDraftVoiceChatEnabled() const
{
	return bDraftVoiceChatEnabled;
}

// 语音聊天草稿写入流程：只在值变化时更新页面草稿并通知 View；实际 OSS 调用由 Apply 在有效 World 中完成。
void UCatFrontendSettingsModel::SetDraftVoiceChatEnabled(const bool bNewVoiceChatEnabled)
{
	if (bDraftVoiceChatEnabled == bNewVoiceChatEnabled)
	{
		return;
	}

	bDraftVoiceChatEnabled = bNewVoiceChatEnabled;
	PublishChanged(LOCTEXT("VoiceChatDraftChanged", "语音聊天等待应用。"));
}

// 语音输入模式可用性读取流程：当前 Steam IOnlineVoice 的正式接口只暴露 Start/StopNetworkedVoice，源码把它定义为 push-to-talk 风格，既没有连续发言/按键发言的模式字段，也没有对应的持久化状态。
// VoiceChat 的 EVoiceChatTransmitMode 只表示发送频道范围而非输入模式，不能替代本项；项目没有现成输入动作生命周期接线，因此不向 View 公开一个不能实际应用的选项。
bool UCatFrontendSettingsModel::IsInputModeSettingAvailable() const
{
	return false;
}

// 麦克风可用性读取流程：当前 Steam IOnlineVoice 只提供本地语音的开始/停止和状态查询，未提供输入设备枚举或选择，故不创建会被忽略的设备偏好。
bool UCatFrontendSettingsModel::IsMicrophoneSettingAvailable() const
{
	return false;
}

// 震动可用性读取流程：当前存在本地 PlayerController 时可写其正式 ForceFeedback gate；Controller 尚未创建或已切图时明确禁用。
bool UCatFrontendSettingsModel::IsVibrationSettingAvailable() const
{
	return GetLocalPlayerController() != nullptr;
}

// 震动草稿读取流程：返回页面本地开关，不在 View 刷新期间更改正在输出的 ForceFeedback。
bool UCatFrontendSettingsModel::GetDraftVibrationEnabled() const
{
	return bDraftVibrationEnabled;
}

// 震动草稿写入流程：仅在值变化时保存页面草稿；真正的 Controller gate 写入由 Apply 统一完成。
void UCatFrontendSettingsModel::SetDraftVibrationEnabled(const bool bNewVibrationEnabled)
{
	if (bDraftVibrationEnabled == bNewVibrationEnabled)
	{
		return;
	}

	bDraftVibrationEnabled = bNewVibrationEnabled;
	PublishChanged(LOCTEXT("VibrationDraftChanged", "震动设置等待应用。"));
}

// 显示模式草稿读取流程：返回最近读取或修改的本地值，不直接查询窗口当前状态，避免 View 刷新冲掉未应用选择。
EWindowMode::Type UCatFrontendSettingsModel::GetDraftFullscreenMode() const
{
	return DraftFullscreenMode;
}

// 显示模式草稿写入流程：只接受 UE 三种窗口模式；合法变化留在页面草稿，真实窗口重建由 Apply 统一触发。
void UCatFrontendSettingsModel::SetDraftFullscreenMode(const EWindowMode::Type NewFullscreenMode)
{
	if (NewFullscreenMode < EWindowMode::Fullscreen || NewFullscreenMode > EWindowMode::Windowed
		|| DraftFullscreenMode == NewFullscreenMode)
	{
		return;
	}

	DraftFullscreenMode = NewFullscreenMode;
	PublishChanged(LOCTEXT("FullscreenDraftChanged", "显示模式等待应用。"));
}

// 分辨率草稿读取流程：返回最近读取或修改的本地像素尺寸，零尺寸表示设置来源未就绪。
FIntPoint UCatFrontendSettingsModel::GetDraftScreenResolution() const
{
	return DraftScreenResolution;
}

// 分辨率草稿写入流程：拒绝非正像素尺寸和未变化输入；合法值只写页面草稿，真实窗口改变延后到 Apply。
void UCatFrontendSettingsModel::SetDraftScreenResolution(const FIntPoint NewScreenResolution)
{
	if (NewScreenResolution.X <= 0 || NewScreenResolution.Y <= 0 || DraftScreenResolution == NewScreenResolution)
	{
		return;
	}

	DraftScreenResolution = NewScreenResolution;
	PublishChanged(LOCTEXT("ResolutionDraftChanged", "分辨率等待应用。"));
}

// 分辨率查询流程：
// 1. 先清空调用方数组，使失败不会与上次显示模式的候选混合。
// 2. 全屏与无边框全屏交给 RHI 查询当前显示器实际支持的模式，窗口模式交给引擎的推荐窗口尺寸。
// 3. 将引擎返回值原样交给 View；本方法不补造常见分辨率，平台无结果时 View 必须保留当前值。
bool UCatFrontendSettingsModel::GetSupportedScreenResolutions(TArray<FIntPoint>& OutResolutions) const
{
	OutResolutions.Reset();
	return DraftFullscreenMode == EWindowMode::Windowed
		? UKismetSystemLibrary::GetConvenientWindowedResolutions(OutResolutions)
		: UKismetSystemLibrary::GetSupportedFullscreenResolutions(OutResolutions);
}

// 画质草稿读取流程：返回 UE 当前的整体质量档；-1 保留为引擎“自定义”只读状态。
int32 UCatFrontendSettingsModel::GetDraftOverallScalabilityLevel() const
{
	return DraftOverallScalabilityLevel;
}

// 画质草稿写入流程：
// 1. 只接受 UE 明确定义的 0 到 4 档，-1 仅用于保留现有自定义态而不能凭空创建未知组合。
// 2. 拒绝把非自定义档直接改成 -1，因为本页没有每个质量维度的草稿，应用后会是假效果。
// 3. 合法变化只记录草稿，具体 Scalability CVar 由 Apply 交给 UGameUserSettings 统一处理。
void UCatFrontendSettingsModel::SetDraftOverallScalabilityLevel(const int32 NewOverallScalabilityLevel)
{
	if (NewOverallScalabilityLevel < -1 || NewOverallScalabilityLevel > 4
		|| DraftOverallScalabilityLevel == NewOverallScalabilityLevel
		|| (NewOverallScalabilityLevel == -1 && DraftOverallScalabilityLevel != -1))
	{
		return;
	}

	DraftOverallScalabilityLevel = NewOverallScalabilityLevel;
	PublishChanged(LOCTEXT("QualityDraftChanged", "画质等待应用。"));
}

// 垂直同步草稿读取流程：返回页面本地布尔值，不提前影响当前渲染帧节奏。
bool UCatFrontendSettingsModel::GetDraftVSyncEnabled() const
{
	return bDraftVSyncEnabled;
}

// 垂直同步草稿写入流程：仅在值变化时更新本地草稿并通知 View，真实 r.VSync 由 ApplySettings 统一提交。
void UCatFrontendSettingsModel::SetDraftVSyncEnabled(const bool bNewVSyncEnabled)
{
	if (bDraftVSyncEnabled == bNewVSyncEnabled)
	{
		return;
	}

	bDraftVSyncEnabled = bNewVSyncEnabled;
	PublishChanged(LOCTEXT("VSyncDraftChanged", "垂直同步等待应用。"));
}

// UI 比例草稿读取流程：返回页面本地比例，不在普通 View 刷新中反复调用 Slate 全局缩放。
float UCatFrontendSettingsModel::GetDraftUIScale() const
{
	return DraftUIScale;
}

// UI 比例草稿写入流程：将输入限制在项目约定范围；值变化时只更新草稿，真正全局缩放延后到 Apply。
void UCatFrontendSettingsModel::SetDraftUIScale(const float NewUIScale)
{
	const float ClampedUIScale = FMath::Clamp(NewUIScale, 0.75f, 2.0f);
	if (FMath::IsNearlyEqual(DraftUIScale, ClampedUIScale))
	{
		return;
	}

	DraftUIScale = ClampedUIScale;
	PublishChanged(LOCTEXT("UIScaleDraftChanged", "界面缩放等待应用。"));
}

// 亮度可用性读取流程：GEngine 存在时 DisplayGamma 会被 FRenderTarget 真实读取；专用服务器或 UI 初始化阶段没有渲染引擎时禁用。
bool UCatFrontendSettingsModel::IsBrightnessSettingAvailable() const
{
	return GEngine != nullptr;
}

// 亮度草稿读取流程：返回页面本地 Gamma，不在滑块拖动期间反复改写全局渲染输出。
float UCatFrontendSettingsModel::GetDraftDisplayGamma() const
{
	return DraftDisplayGamma;
}

// 亮度草稿写入流程：裁剪到 UE Gamma 命令允许范围并通知 View；实际 GEngine 输出修改延后到 Apply。
void UCatFrontendSettingsModel::SetDraftDisplayGamma(const float NewDisplayGamma)
{
	const float ClampedDisplayGamma = FMath::Clamp(NewDisplayGamma, 0.5f, 5.0f);
	if (FMath::IsNearlyEqual(DraftDisplayGamma, ClampedDisplayGamma))
	{
		return;
	}

	DraftDisplayGamma = ClampedDisplayGamma;
	PublishChanged(LOCTEXT("DisplayGammaDraftChanged", "亮度等待应用。"));
}

// 主音量草稿读取流程：返回页面本地比例，缺混音资产时该值不会被持久化或写入 AudioDevice。
float UCatFrontendSettingsModel::GetDraftMasterVolume() const
{
	return DraftMasterVolume;
}

// 主音量草稿写入流程：裁剪输入到合法比例并只更新页面草稿，避免滑块拖动过程持续重写 AudioDevice。
void UCatFrontendSettingsModel::SetDraftMasterVolume(const float NewMasterVolume)
{
	const float ClampedVolume = FMath::Clamp(NewMasterVolume, 0.0f, 1.0f);
	if (FMath::IsNearlyEqual(DraftMasterVolume, ClampedVolume))
	{
		return;
	}

	DraftMasterVolume = ClampedVolume;
	PublishChanged(LOCTEXT("MasterVolumeDraftChanged", "主音量等待应用。"));
}

// 音乐音量草稿读取流程：返回页面本地比例，缺混音资产时该值不会被持久化或写入 AudioDevice。
float UCatFrontendSettingsModel::GetDraftMusicVolume() const
{
	return DraftMusicVolume;
}

// 音乐音量草稿写入流程：裁剪输入到合法比例并只更新页面草稿，实际音乐总线覆盖延后到 Apply。
void UCatFrontendSettingsModel::SetDraftMusicVolume(const float NewMusicVolume)
{
	const float ClampedVolume = FMath::Clamp(NewMusicVolume, 0.0f, 1.0f);
	if (FMath::IsNearlyEqual(DraftMusicVolume, ClampedVolume))
	{
		return;
	}

	DraftMusicVolume = ClampedVolume;
	PublishChanged(LOCTEXT("MusicVolumeDraftChanged", "音乐音量等待应用。"));
}

// 音效音量草稿读取流程：返回页面本地比例，缺混音资产时该值不会被持久化或写入 AudioDevice。
float UCatFrontendSettingsModel::GetDraftSFXVolume() const
{
	return DraftSFXVolume;
}

// 音效音量草稿写入流程：裁剪输入到合法比例并只更新页面草稿，实际音效总线覆盖延后到 Apply。
void UCatFrontendSettingsModel::SetDraftSFXVolume(const float NewSFXVolume)
{
	const float ClampedVolume = FMath::Clamp(NewSFXVolume, 0.0f, 1.0f);
	if (FMath::IsNearlyEqual(DraftSFXVolume, ClampedVolume))
	{
		return;
	}

	DraftSFXVolume = ClampedVolume;
	PublishChanged(LOCTEXT("SFXVolumeDraftChanged", "音效音量等待应用。"));
}

// 环境音音量草稿读取流程：返回页面本地比例，缺混音资产时该值不会被持久化或写入 AudioDevice。
float UCatFrontendSettingsModel::GetDraftAmbienceVolume() const
{
	return DraftAmbienceVolume;
}

// 环境音音量草稿写入流程：裁剪输入到合法比例并只更新页面草稿，实际环境音总线覆盖延后到 Apply。
void UCatFrontendSettingsModel::SetDraftAmbienceVolume(const float NewAmbienceVolume)
{
	const float ClampedVolume = FMath::Clamp(NewAmbienceVolume, 0.0f, 1.0f);
	if (FMath::IsNearlyEqual(DraftAmbienceVolume, ClampedVolume))
	{
		return;
	}

	DraftAmbienceVolume = ClampedVolume;
	PublishChanged(LOCTEXT("AmbienceVolumeDraftChanged", "环境音量等待应用。"));
}

// 语音音量草稿读取流程：返回页面本地比例，不将该分类音量误解为语音聊天开关。
float UCatFrontendSettingsModel::GetDraftVoiceVolume() const
{
	return DraftVoiceVolume;
}

// 语音音量草稿写入流程：裁剪输入到合法比例并只更新页面草稿，实际语音分类覆盖延后到 Apply。
void UCatFrontendSettingsModel::SetDraftVoiceVolume(const float NewVoiceVolume)
{
	const float ClampedVolume = FMath::Clamp(NewVoiceVolume, 0.0f, 1.0f);
	if (FMath::IsNearlyEqual(DraftVoiceVolume, ClampedVolume))
	{
		return;
	}

	DraftVoiceVolume = ClampedVolume;
	PublishChanged(LOCTEXT("VoiceVolumeDraftChanged", "语音音量等待应用。"));
}

// 后台静音草稿读取流程：返回页面本地开关，不在普通 View 刷新中直接修改全局应用失焦音量倍率。
bool UCatFrontendSettingsModel::GetDraftMuteAudioWhenUnfocused() const
{
	return bDraftMuteAudioWhenUnfocused;
}

// 后台静音草稿写入流程：只在值变化时更新页面草稿；实际 FApp 倍率写入延后到 Apply，使取消能保持当前已生效行为。
void UCatFrontendSettingsModel::SetDraftMuteAudioWhenUnfocused(const bool bNewMuteAudioWhenUnfocused)
{
	if (bDraftMuteAudioWhenUnfocused == bNewMuteAudioWhenUnfocused)
	{
		return;
	}

	bDraftMuteAudioWhenUnfocused = bNewMuteAudioWhenUnfocused;
	PublishChanged(LOCTEXT("BackgroundMuteDraftChanged", "后台静音等待应用。"));
}

// 音频路由资产读取流程：只有正式项目设置单例和六个资产齐备时才返回 true；它不以此推断现有内容是否已经分到各分类。
bool UCatFrontendSettingsModel::IsAudioRoutingAvailable() const
{
	return UserSettings && UserSettings->HasAudioRoutingAssets();
}

// 输出设备可用性读取流程：只有异步枚举返回至少一个真实平台设备时才开放选择，避免 View 把空列表或系统默认文本当作可切换设备。
bool UCatFrontendSettingsModel::IsOutputDeviceSettingAvailable() const
{
	return AudioOutputDeviceNames.Num() > 0 && AudioOutputDeviceNames.Num() == AudioOutputDeviceIds.Num();
}

// 设备枚举流程：
// 1. 先拒绝重复请求和切图期间缺失的 LocalPlayer World，防止两个当前请求交叉覆盖同一组设备事实。
// 2. 强持有单次请求并把页面代次绑定到原生完成委托；请求在所属音频设备上枚举，并核对活动设备标志。
// 3. 请求存在即 pending；八秒超时或完成后解除等待，Shutdown 取消请求，失效结果不能覆盖重初始化后的列表。
bool UCatFrontendSettingsModel::RefreshAudioOutputDevices()
{
	UWorld* World = GetLocalPlayerWorld();
	if (ActiveAudioOutputRequest || !World || !UserSettings)
	{
		return false;
	}

	const uint64 RequestGeneration = ++AudioOutputDeviceRequestGeneration;
	ActiveAudioOutputRequest = NewObject<UCatAudioOutputRequest>(this);
	ActiveAudioOutputRequest->Start(World, FString(), UCatAudioOutputRequest::FOnCompleted::CreateUObject(
		this, &ThisClass::HandleAudioOutputRequestCompleted, RequestGeneration));
	PublishChanged(LOCTEXT("AudioOutputDevicesLoading", "正在读取音频输出设备。"));
	return true;
}

// 设备数量读取流程：返回最新一次成功异步枚举的数量，等待或失败时为零；调用方不能据此访问未同步的 ID 数组。
int32 UCatFrontendSettingsModel::GetAudioOutputDeviceCount() const
{
	return IsOutputDeviceSettingAvailable() ? AudioOutputDeviceNames.Num() : 0;
}

// 设备名称读取流程：仅在名称/ID 数组已同步且索引有效时返回显示文本；无效索引返回静态空字符串，不猜测系统默认设备。
const FString& UCatFrontendSettingsModel::GetAudioOutputDeviceName(const int32 DeviceIndex) const
{
	static const FString EmptyDeviceName;
	return AudioOutputDeviceNames.IsValidIndex(DeviceIndex) && AudioOutputDeviceIds.IsValidIndex(DeviceIndex)
		? AudioOutputDeviceNames[DeviceIndex]
		: EmptyDeviceName;
}

// 设备 ID 读取流程：仅在名称/ID 数组已同步且索引有效时返回平台稳定 ID；无效索引返回静态空字符串，不能提交给热切换 API。
const FString& UCatFrontendSettingsModel::GetAudioOutputDeviceId(const int32 DeviceIndex) const
{
	static const FString EmptyDeviceId;
	return AudioOutputDeviceNames.IsValidIndex(DeviceIndex) && AudioOutputDeviceIds.IsValidIndex(DeviceIndex)
		? AudioOutputDeviceIds[DeviceIndex]
		: EmptyDeviceId;
}

// 输出设备草稿读取流程：返回已在真实枚举列表中选择的设备 ID；空值表示保留平台默认设备策略。
const FString& UCatFrontendSettingsModel::GetDraftAudioOutputDeviceId() const
{
	return DraftAudioOutputDeviceId;
}

// 输出设备草稿写入流程：有在途请求时保持固定目标；空闲时只接受当前枚举列表中的非空新 ID，未知或未变化输入不修改草稿。
void UCatFrontendSettingsModel::SetDraftAudioOutputDeviceId(const FString& NewAudioOutputDeviceId)
{
	if (ActiveAudioOutputRequest || NewAudioOutputDeviceId.IsEmpty() || DraftAudioOutputDeviceId == NewAudioOutputDeviceId
		|| !AudioOutputDeviceIds.Contains(NewAudioOutputDeviceId))
	{
		return;
	}

	DraftAudioOutputDeviceId = NewAudioOutputDeviceId;
	PublishChanged(LOCTEXT("AudioOutputDeviceDraftChanged", "输出设备等待应用。"));
}

// 输出设备异步状态读取流程：返回枚举或热切换是否尚未回调；View 用它屏蔽重入，不据此宣称设备已经切换成功。
bool UCatFrontendSettingsModel::IsAudioOutputDeviceOperationPending() const
{
	return ActiveAudioOutputRequest != nullptr;
}

// 应用流程：
// 1. 先缓存恢复默认标志、失效输出设备偏好和需要真实重放的运行时差异，随后才允许 SetToDefaults 改写设置对象。
// 2. 若草稿来自恢复默认，让正式设置宿主回到干净默认状态，再把窗口、分辨率、画质与垂直同步草稿交给 UGameUserSettings。
// 3. 独立尝试语言、Slate 缩放、Gamma、震动、OSS 语音、失焦音量和当前 World 的分类混音；失败项只影响返回值，不撤销其它已成功设置。
// 4. 输出设备变化发起有界的活动确认请求；恢复系统默认时热切换使用枚举 ID，确认前临时保留已有偏好，确认后才保存为空。
// 5. 保存可立即确认的设置并重读草稿；如果存在输出设备 pending，View 会继续显示请求目标，等待回调给出最终文本。
bool UCatFrontendSettingsModel::Apply()
{
	if (!UserSettings)
	{
		PublishChanged(LOCTEXT("ApplyWithoutSettings", "设置来源不可用，未应用更改。"));
		return false;
	}

	const bool bDefaultsWereRequested = bDraftDefaultsRequested;
	const FString SavedAudioOutputDeviceIdBeforeApply = UserSettings->GetAudioOutputDeviceId();
	const bool bVibrationWasRequested = bDraftVibrationEnabled != UserSettings->IsVibrationEnabled();
	const bool bVoiceChatWasRequested = bDraftVoiceChatEnabled != UserSettings->IsVoiceChatEnabled();
	const bool bAudioWasRequested = !FMath::IsNearlyEqual(DraftMasterVolume, UserSettings->GetMasterVolume())
		|| !FMath::IsNearlyEqual(DraftMusicVolume, UserSettings->GetMusicVolume())
		|| !FMath::IsNearlyEqual(DraftSFXVolume, UserSettings->GetSFXVolume())
		|| !FMath::IsNearlyEqual(DraftAmbienceVolume, UserSettings->GetAmbienceVolume())
		|| !FMath::IsNearlyEqual(DraftVoiceVolume, UserSettings->GetVoiceVolume());
	if (bDraftDefaultsRequested)
	{
		UserSettings->SetToDefaults();
	}
	UserSettings->SetFullscreenMode(DraftFullscreenMode);
	UserSettings->SetScreenResolution(DraftScreenResolution);
	if (DraftOverallScalabilityLevel >= 0)
	{
		UserSettings->SetOverallScalabilityLevel(DraftOverallScalabilityLevel);
	}
	UserSettings->SetVSyncEnabled(bDraftVSyncEnabled);
	UserSettings->ApplySettings(false);

	const bool bLanguageApplied = UserSettings->ApplyLanguage(DraftLanguage);
	const bool bUIScaleApplied = UserSettings->ApplyUIScale(DraftUIScale);
	const bool bBrightnessApplied = UserSettings->ApplyDisplayGamma(DraftDisplayGamma);
	const bool bVibrationApplied = !bVibrationWasRequested
		|| UserSettings->ApplyVibration(GetLocalPlayerController(), bDraftVibrationEnabled);
	const bool bVoiceChatApplied = !bVoiceChatWasRequested || (BoundLocalPlayer.IsValid() && IsVoiceChatSettingAvailable()
		&& UserSettings->ApplyVoiceChat(GetLocalPlayerWorld(), static_cast<uint8>(BoundLocalPlayer->GetControllerId()),
			bDraftVoiceChatEnabled));
	const bool bBackgroundMuteApplied = UserSettings->ApplyMuteAudioWhenUnfocused(bDraftMuteAudioWhenUnfocused);
	const bool bAudioApplied = !bAudioWasRequested || UserSettings->ApplyAudioVolumes(GetLocalPlayerWorld(), DraftMasterVolume,
		DraftMusicVolume, DraftSFXVolume, DraftAmbienceVolume, DraftVoiceVolume);
	const FString RequestedAudioOutputDeviceId = DraftAudioOutputDeviceId;
	const bool bOutputDeviceDefaultRestoreWasRequested = bDefaultsWereRequested && !RequestedAudioOutputDeviceId.IsEmpty()
		&& RequestedAudioOutputDeviceId == SystemDefaultAudioOutputDeviceId;
	const bool bOutputDeviceWasRequested = !RequestedAudioOutputDeviceId.IsEmpty()
		&& (!DoesDraftAudioOutputMatchSavedPreference()
			|| (bOutputDeviceDefaultRestoreWasRequested && !SavedAudioOutputDeviceIdBeforeApply.IsEmpty()));
	bool bOutputDeviceSwapStarted = !bOutputDeviceWasRequested
		|| (ActiveAudioOutputRequest && ActiveAudioOutputRequest->GetRequestedDeviceId() == RequestedAudioOutputDeviceId);
	if (bOutputDeviceWasRequested && !ActiveAudioOutputRequest
		&& AudioOutputDeviceIds.Contains(DraftAudioOutputDeviceId) && GetLocalPlayerWorld())
	{
		UserSettings->CancelAudioOutputRestore();
		const uint64 RequestGeneration = ++AudioOutputDeviceRequestGeneration;
		ActiveAudioOutputRequest = NewObject<UCatAudioOutputRequest>(this);
		ActiveAudioOutputRequest->Start(GetLocalPlayerWorld(), RequestedAudioOutputDeviceId,
			UCatAudioOutputRequest::FOnCompleted::CreateUObject(this, &ThisClass::HandleAudioOutputRequestCompleted, RequestGeneration));
		bPendingAudioOutputDefaultRestore = bOutputDeviceDefaultRestoreWasRequested;
		bOutputDeviceSwapStarted = true;
	}
	else if (!bOutputDeviceWasRequested && !ActiveAudioOutputRequest)
	{
		bPendingAudioOutputDefaultRestore = false;
	}

	if (bPendingAudioOutputDefaultRestore && ActiveAudioOutputRequest
		&& ActiveAudioOutputRequest->GetRequestedDeviceId() == RequestedAudioOutputDeviceId)
	{
		// 恢复系统默认要等活动设备确认；这里保留已有覆盖，避免切换失败后无法兑现“未保存新选择”的回退承诺。
		UserSettings->SetAudioOutputDeviceId(SavedAudioOutputDeviceIdBeforeApply);
	}
	UserSettings->SaveSettings();
	ReloadDraftFromSettings();
	if (bLanguageApplied && bUIScaleApplied && bBrightnessApplied && bVibrationApplied && bVoiceChatApplied
		&& bBackgroundMuteApplied && bAudioApplied
		&& bOutputDeviceSwapStarted)
	{
		PublishChanged(bOutputDeviceWasRequested
			? LOCTEXT("SettingsAppliedWithPendingDevice", "设置已应用；正在切换输出设备。")
			: LOCTEXT("SettingsApplied", "设置已应用。"));
		return true;
	}

	if (!bAudioApplied)
	{
		PublishChanged(LOCTEXT("SettingsAppliedWithoutAudio", "画面设置已应用；正式声音分类资产或音频设备不可用。"));
		return false;
	}

	PublishChanged(LOCTEXT("SettingsPartiallyApplied", "可用设置已应用；部分设备、语音或显示能力当前不可用。"));
	return false;
}

// 取消流程：从正式设置来源重建所有草稿并通知 View；不调用任何引擎 Apply 函数，因此不能回滚已经提交的窗口、语言或音频状态。
void UCatFrontendSettingsModel::Cancel()
{
	if (!UserSettings)
	{
		PublishChanged(LOCTEXT("CancelWithoutSettings", "设置来源不可用。"));
		return;
	}

	ReloadDraftFromSettings();
	PublishChanged(LOCTEXT("SettingsCancelled", "未应用的更改已取消。"));
}

// 恢复默认流程：
// 1. 从 UCatGameUserSettings 读取不受本机 ini 影响的干净默认快照，只覆盖本 Model 草稿，绝不修改当前已应用的 UserSettings 实例。
// 2. 将语言、UI、亮度、震动、语音、静音、分类音量与输出设备草稿设为正式默认候选，不调用任何运行时应用 API。
// 3. 发布草稿变化；Cancel 因而仍能从未变的权威设置完整恢复进入页面前的已应用值。
void UCatFrontendSettingsModel::RestoreDefaults()
{
	if (!UserSettings)
	{
		PublishChanged(LOCTEXT("DefaultsWithoutSettings", "设置来源不可用。"));
		return;
	}

	const FCatGameUserSettingsDefaultSnapshot Defaults = UCatGameUserSettings::MakeDefaultSnapshot();
	DraftFullscreenMode = Defaults.FullscreenMode;
	DraftScreenResolution = Defaults.ScreenResolution;
	DraftOverallScalabilityLevel = Defaults.OverallScalabilityLevel;
	bDraftVSyncEnabled = Defaults.bVSyncEnabled;
	DraftLanguage = Defaults.FrontendLanguage;
	DraftUIScale = Defaults.UIScale;
	DraftDisplayGamma = Defaults.DisplayGamma;
	bDraftVibrationEnabled = Defaults.bVibrationEnabled;
	bDraftVoiceChatEnabled = Defaults.bVoiceChatEnabled;
	bDraftMuteAudioWhenUnfocused = Defaults.bMuteAudioWhenUnfocused;
	DraftMasterVolume = Defaults.MasterVolume;
	DraftMusicVolume = Defaults.MusicVolume;
	DraftSFXVolume = Defaults.SFXVolume;
	DraftAmbienceVolume = Defaults.AmbienceVolume;
	DraftVoiceVolume = Defaults.VoiceVolume;
	DraftAudioOutputDeviceId = ActiveAudioOutputRequest && !ActiveAudioOutputRequest->GetRequestedDeviceId().IsEmpty()
		? ActiveAudioOutputRequest->GetRequestedDeviceId()
		: (!SystemDefaultAudioOutputDeviceId.IsEmpty() ? SystemDefaultAudioOutputDeviceId : Defaults.AudioOutputDeviceId);
	bDraftDefaultsRequested = true;
	PublishChanged(LOCTEXT("DefaultsRestored", "默认设置等待应用。"));
}

// 脏状态计算流程：
// 1. 比较所有可由 UGameUserSettings 真实应用的画面草稿与当前设置。
// 2. 以国际化、正式 UI、Gamma、震动、OSS 语音和失焦音量的运行时/持久化值比较游戏草稿。
// 3. 只有完整音频路由可用时才比较分类音量；输出设备只在真实枚举列表中存在时参与比较。
bool UCatFrontendSettingsModel::HasPendingChanges() const
{
	if (!UserSettings)
	{
		return false;
	}

	if (bDraftDefaultsRequested
		|| DraftFullscreenMode != UserSettings->GetFullscreenMode()
		|| DraftScreenResolution != UserSettings->GetScreenResolution()
		|| (DraftOverallScalabilityLevel >= 0 && DraftOverallScalabilityLevel != UserSettings->GetOverallScalabilityLevel())
		|| bDraftVSyncEnabled != UserSettings->IsVSyncEnabled()
		|| DraftLanguage != FInternationalization::Get().GetCurrentLanguage()->GetName()
		|| !FMath::IsNearlyEqual(DraftUIScale, UserSettings->GetUIScale())
		|| !FMath::IsNearlyEqual(DraftDisplayGamma, UserSettings->GetDisplayGamma())
		|| (IsVibrationSettingAvailable() && bDraftVibrationEnabled != UserSettings->IsVibrationEnabled())
		|| (IsVoiceChatSettingAvailable() && bDraftVoiceChatEnabled != UserSettings->IsVoiceChatEnabled())
		|| bDraftMuteAudioWhenUnfocused != UserSettings->IsMuteAudioWhenUnfocused()
		|| !DoesDraftAudioOutputMatchSavedPreference())
	{
		return true;
	}

	return IsAudioRoutingAvailable()
		&& (!FMath::IsNearlyEqual(DraftMasterVolume, UserSettings->GetMasterVolume())
			|| !FMath::IsNearlyEqual(DraftMusicVolume, UserSettings->GetMusicVolume())
			|| !FMath::IsNearlyEqual(DraftSFXVolume, UserSettings->GetSFXVolume())
			|| !FMath::IsNearlyEqual(DraftAmbienceVolume, UserSettings->GetAmbienceVolume())
			|| !FMath::IsNearlyEqual(DraftVoiceVolume, UserSettings->GetVoiceVolume()));
}

// 结果文本读取流程：返回最近一次公开操作写入的不可变文本引用，不触发刷新、保存或引擎设置调用。
const FText& UCatFrontendSettingsModel::GetLastResultText() const
{
	return LastResultText;
}

// 草稿重读流程：
// 1. 设置来源缺失时保留安全默认草稿，调用方随后会显示不可用结果。
// 2. 从 UGameUserSettings 读取画面项，从国际化系统读取当前真实语言，从项目设置读取 UI、Gamma、震动、语音和音频持久化值。
// 3. 从正式用户设置读取已持久化的失焦静音偏好；设备切换已提交时保留请求固定目标，Cancel 或其它设置的 Apply 不能把它伪装成已撤销。
void UCatFrontendSettingsModel::ReloadDraftFromSettings()
{
	if (!UserSettings)
	{
		return;
	}

	bDraftDefaultsRequested = false;
	DraftLanguage = FInternationalization::Get().GetCurrentLanguage()->GetName();
	DraftFullscreenMode = UserSettings->GetFullscreenMode();
	DraftScreenResolution = UserSettings->GetScreenResolution();
	DraftOverallScalabilityLevel = UserSettings->GetOverallScalabilityLevel();
	bDraftVSyncEnabled = UserSettings->IsVSyncEnabled();
	DraftUIScale = UserSettings->GetUIScale();
	DraftDisplayGamma = UserSettings->GetDisplayGamma();
	bDraftVibrationEnabled = UserSettings->IsVibrationEnabled();
	bDraftVoiceChatEnabled = UserSettings->IsVoiceChatEnabled();
	DraftMasterVolume = UserSettings->GetMasterVolume();
	DraftMusicVolume = UserSettings->GetMusicVolume();
	DraftSFXVolume = UserSettings->GetSFXVolume();
	DraftAmbienceVolume = UserSettings->GetAmbienceVolume();
	DraftVoiceVolume = UserSettings->GetVoiceVolume();
	bDraftMuteAudioWhenUnfocused = UserSettings->IsMuteAudioWhenUnfocused();
	DraftAudioOutputDeviceId = ActiveAudioOutputRequest && !ActiveAudioOutputRequest->GetRequestedDeviceId().IsEmpty()
		? ActiveAudioOutputRequest->GetRequestedDeviceId() : UserSettings->GetAudioOutputDeviceId();
}

// 本地 World 读取流程：从绑定 LocalPlayer 取其实际 World；没有本地玩家或切图期间 World 为空时返回空，音频应用因此 fail-closed。
UWorld* UCatFrontendSettingsModel::GetLocalPlayerWorld() const
{
	return BoundLocalPlayer.IsValid() ? BoundLocalPlayer->GetWorld() : nullptr;
}

// 本地 Controller 读取流程：从绑定 LocalPlayer 和其实际 World 解析当前控制器并过滤远端/切图空值；震动设置只能交给此本地对象。
APlayerController* UCatFrontendSettingsModel::GetLocalPlayerController() const
{
	return BoundLocalPlayer.IsValid() ? BoundLocalPlayer->GetPlayerController(GetLocalPlayerWorld()) : nullptr;
}

// 设备请求完成流程：
// 1. 同时核对页面代次和单次请求身份；Shutdown、重初始化或新请求之后的失效通知不修改列表、草稿或配置。
// 2. 解除当前等待并消费恢复系统默认标志；切换失败保留目标草稿供重试，成功后按普通设备 ID 或空默认偏好保存。
// 3. 枚举成功时整代替换名称/ID 和系统默认值，只接收有名称与 ID 的设备，再建立可用草稿并通知 View。
void UCatFrontendSettingsModel::HandleAudioOutputRequestCompleted(UCatAudioOutputRequest* Request,
	const FName Error, const uint64 RequestGeneration)
{
	if (RequestGeneration != AudioOutputDeviceRequestGeneration || !Request || ActiveAudioOutputRequest != Request)
	{
		UE_LOG(LogCatUI, Log, TEXT("Event=frontend_audio_output_superseded_callback_ignored Request=%s Generation=%llu CurrentGeneration=%llu"),
			*GetNameSafe(Request), RequestGeneration, AudioOutputDeviceRequestGeneration);
		return;
	}

	const FString RequestedDeviceId = Request->GetRequestedDeviceId();
	const bool bShouldPersistPlatformDefault = bPendingAudioOutputDefaultRestore;
	bPendingAudioOutputDefaultRestore = false;
	ActiveAudioOutputRequest = nullptr;
	if (!UserSettings)
	{
		return;
	}
	if (!Error.IsNone())
	{
		if (!RequestedDeviceId.IsEmpty())
		{
			DraftAudioOutputDeviceId = RequestedDeviceId;
		}
		PublishChanged(FText::Format(LOCTEXT("AudioOutputRequestFailed", "音频输出设备请求未完成，未保存新选择，可重试。原因：{0}"), FText::FromName(Error)));
		return;
	}
	if (!RequestedDeviceId.IsEmpty())
	{
		UserSettings->SetAudioOutputDeviceId(bShouldPersistPlatformDefault ? FString() : RequestedDeviceId);
		UserSettings->SaveSettings();
		DraftAudioOutputDeviceId = RequestedDeviceId;
		PublishChanged(bShouldPersistPlatformDefault
			? LOCTEXT("AudioOutputDeviceDefaultConfirmed", "音频输出设备已恢复为系统默认。")
			: LOCTEXT("AudioOutputDeviceConfirmed", "音频输出设备已确认切换。"));
		return;
	}

	AudioOutputDeviceNames.Reset();
	AudioOutputDeviceIds.Reset();
	SystemDefaultAudioOutputDeviceId.Reset();
	FString CurrentDeviceId;
	for (const FAudioOutputDeviceInfo& Device : Request->GetDevices())
	{
		if (Device.Name.IsEmpty() || Device.DeviceId.IsEmpty())
		{
			continue;
		}

		AudioOutputDeviceNames.Add(Device.Name);
		AudioOutputDeviceIds.Add(Device.DeviceId);
		if (Device.bIsCurrentDevice)
		{
			CurrentDeviceId = Device.DeviceId;
		}
		if (Device.bIsSystemDefault)
		{
			SystemDefaultAudioOutputDeviceId = Device.DeviceId;
		}
	}
	UE_LOG(LogCatUI, Log, TEXT("Event=frontend_audio_output_enumeration_completed DeviceCount=%d"),
		AudioOutputDeviceIds.Num());

	if (AudioOutputDeviceIds.IsEmpty())
	{
		DraftAudioOutputDeviceId.Reset();
		PublishChanged(LOCTEXT("AudioOutputDevicesUnavailable", "当前平台未返回可切换的音频输出设备。"));
		return;
	}

	const FString& SavedDeviceId = UserSettings->GetAudioOutputDeviceId();
	DraftAudioOutputDeviceId = AudioOutputDeviceIds.Contains(SavedDeviceId) ? SavedDeviceId : CurrentDeviceId;
	if (DraftAudioOutputDeviceId.IsEmpty())
	{
		DraftAudioOutputDeviceId = AudioOutputDeviceIds[0];
	}
	PublishChanged(LOCTEXT("AudioOutputDevicesReady", "音频输出设备已读取。"));
}

// 输出设备草稿匹配流程：
// 1. 设置来源或设备枚举不可用时不参与脏状态计算，避免禁用控件制造伪 pending。
// 2. 草稿与已保存设备 ID 完全相同则视为无改动。
// 3. 已保存偏好为空时代表跟随系统默认；如果草稿等于本次枚举到的系统默认 ID，也视为同一选择。
bool UCatFrontendSettingsModel::DoesDraftAudioOutputMatchSavedPreference() const
{
	if (!UserSettings || !IsOutputDeviceSettingAvailable())
	{
		return true;
	}

	const FString& SavedAudioOutputDeviceId = UserSettings->GetAudioOutputDeviceId();
	return DraftAudioOutputDeviceId == SavedAudioOutputDeviceId
		|| (SavedAudioOutputDeviceId.IsEmpty() && !SystemDefaultAudioOutputDeviceId.IsEmpty()
			&& DraftAudioOutputDeviceId == SystemDefaultAudioOutputDeviceId);
}

// 变更发布流程：先替换最近结果文本，再广播无参原生通知；View 收到通知后重新读取 Model，不能缓存或写回本次草稿。
void UCatFrontendSettingsModel::PublishChanged(const FText& NewResultText)
{
	LastResultText = NewResultText;
	OnChanged.Broadcast();
}

#undef LOCTEXT_NAMESPACE
