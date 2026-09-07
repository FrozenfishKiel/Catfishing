#pragma once

#include "AudioMixerBlueprintLibrary.h"
#include "CoreMinimal.h"
#include "GenericPlatform/GenericWindow.h"
#include "UObject/Object.h"
#include "CatFrontendSettingsModel.generated.h"

class UCatGameUserSettings;
class UCatAudioOutputRequest;
class ULocalPlayer;
class UWorld;
class APlayerController;

/** 设置页内容或草稿发生变化时的原生通知；Root WBP 收到后重新读取本 Model，不携带可被 View 写回的副本。 */
DECLARE_MULTICAST_DELEGATE(FCatFrontendSettingsChanged);

/**
 * 主界面设置页的本地草稿 Model；它从正式 UCatGameUserSettings 读取已生效值，在 Apply 前不触碰引擎、音频或国际化运行态。
 * 控制细项与麦克风选择尚无正式来源时只暴露不可用状态；输出设备与网络语音通过各自引擎/OSS API 真实提交，不保存假偏好。
 */
UCLASS()
class CATFISHING_API UCatFrontendSettingsModel : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * 绑定本地玩家并从正式设置来源建立页面草稿；由 Frontend PageController 在设置页创建后调用，成功时发布第一份可读取状态。
	 */
	bool Initialize(ULocalPlayer* InLocalPlayer);

	/**
	 * 清空本地玩家和设置引用并丢弃未应用草稿，同时使在途 AudioMixer 回调失效；由 Frontend 生命周期结束时成对调用，之后所有读取接口返回安全默认值。
	 */
	void Shutdown();

	/** 选择游戏分类；由 Controller 的明确页面意图调用，通知 View 刷新但不通过字符串或动作枚举做二次分发。 */
	void SelectGame();

	/** 选择画面分类；由 Controller 的明确页面意图调用，通知 View 刷新但不通过字符串或动作枚举做二次分发。 */
	void SelectGraphics();

	/** 选择声音分类；由 Controller 的明确页面意图调用，通知 View 刷新但不通过字符串或动作枚举做二次分发。 */
	void SelectAudio();

	/** 选择控制分类；当前没有正式控制字段时只切换分类与说明，不建立临时按键映射表。 */
	void SelectControls();

	/** 返回游戏分类是否为当前选择，供 View 控制可见性；分类状态只属于本页面草稿，不参与游戏玩法状态。 */
	bool IsGameSelected() const;

	/** 返回画面分类是否为当前选择，供 View 控制可见性；分类状态只属于本页面草稿，不参与游戏玩法状态。 */
	bool IsGraphicsSelected() const;

	/** 返回声音分类是否为当前选择，供 View 控制可见性；分类状态只属于本页面草稿，不参与游戏玩法状态。 */
	bool IsAudioSelected() const;

	/** 返回控制分类是否为当前选择，供 View 控制可见性；控制项未接线时仍可显示正式入口。 */
	bool IsControlsSelected() const;

	/** 返回当前草稿语言 culture 名称；由 Initialize、Cancel、RestoreDefaults 或 SetDraftLanguage 写入，Apply 成功后成为运行时语言。 */
	const FString& GetDraftLanguage() const;

	/** 更新待应用的语言 culture 名称；只记录草稿并通知 View，真实国际化切换延后到 Apply。 */
	void SetDraftLanguage(const FString& NewLanguage);

	/** 返回本地化资源系统实际发现的游戏语言数量；Initialize 刷新该列表并补入当前语言，View 不得硬编码未打包的 culture 选项。 */
	int32 GetAvailableLanguageCount() const;

	/** 返回指定已打包语言的 culture 名称；索引无效时为空，名称可直接提交给 SetDraftLanguage 而不需要 View 自建映射表。 */
	const FString& GetAvailableLanguage(int32 LanguageIndex) const;

	/** 返回当前 World 的 OSS 是否提供网络语音接口；Steam Voice 可用时为 true，未初始化平台或不支持时 View 必须禁用开关。 */
	bool IsVoiceChatSettingAvailable() const;

	/** 返回待应用的网络语音开关；只有当前 OSS Voice 接口接受 Apply 时才会持久化并开始或停止本地语音处理。 */
	bool GetDraftVoiceChatEnabled() const;

	/** 更新待应用的网络语音开关；只记录页面草稿，实际 Start/StopNetworkedVoice 延后到 Apply。 */
	void SetDraftVoiceChatEnabled(bool bNewVoiceChatEnabled);

	/** 返回语音输入模式是否有正式运行时来源；当前 Steam IOnlineVoice 只提供 push-to-talk 风格的开始/停止调用，没有可持久化的模式选择，故为 false。 */
	bool IsInputModeSettingAvailable() const;

	/** 返回麦克风选择是否已有正式设备管理来源；当前 Steam IOnlineVoice 只支持开始/停止网络语音而不提供输入设备枚举或选择，故为 false。 */
	bool IsMicrophoneSettingAvailable() const;

	/** 返回震动开关是否可应用给当前本地 PlayerController；存在 Controller 时会写入其 ForceFeedback gate，设备本身仍由平台决定。 */
	bool IsVibrationSettingAvailable() const;

	/** 返回待应用的震动总开关；Apply 成功时写入当前本地 PlayerController 的 bForceFeedbackEnabled。 */
	bool GetDraftVibrationEnabled() const;

	/** 更新待应用的震动总开关；只记录页面草稿，避免在玩家拖动设置期间打断当前 ForceFeedback。 */
	void SetDraftVibrationEnabled(bool bNewVibrationEnabled);

	/** 返回待应用的窗口显示模式；来自正式 UGameUserSettings，Apply 时才写入引擎设置。 */
	EWindowMode::Type GetDraftFullscreenMode() const;

	/** 更新待应用的窗口显示模式；只记录草稿并通知 View，真正的窗口切换由 Apply 调用 UGameUserSettings 完成。 */
	void SetDraftFullscreenMode(EWindowMode::Type NewFullscreenMode);

	/** 返回待应用的屏幕分辨率像素值；来自正式 UGameUserSettings，零值表示设置来源尚不可用。 */
	FIntPoint GetDraftScreenResolution() const;

	/** 更新待应用的屏幕分辨率像素值；非正尺寸会被忽略，避免将无效窗口配置写入引擎。 */
	void SetDraftScreenResolution(FIntPoint NewScreenResolution);

	/** 按当前窗口模式向引擎查询可选分辨率；全屏查询实际显示模式，窗口模式查询引擎推荐尺寸，失败时只返回 false 不构造假列表。 */
	bool GetSupportedScreenResolutions(TArray<FIntPoint>& OutResolutions) const;

	/** 返回待应用的整体画质档位；数值沿用 UE 的 0 到 4 质量定义，-1 表示自定义画质。 */
	int32 GetDraftOverallScalabilityLevel() const;

	/** 更新待应用的整体画质档位；允许 -1 保留 UE 自定义档，0 到 4 由引擎在 Apply 时裁剪。 */
	void SetDraftOverallScalabilityLevel(int32 NewOverallScalabilityLevel);

	/** 返回待应用的垂直同步选择；来自正式 UGameUserSettings，Apply 时才影响渲染同步。 */
	bool GetDraftVSyncEnabled() const;

	/** 更新待应用的垂直同步选择；只记录草稿，避免切换按钮时立即打断当前帧节奏。 */
	void SetDraftVSyncEnabled(bool bNewVSyncEnabled);

	/** 返回待应用的 UI 比例；由正式 UCatGameUserSettings 持久化，Apply 成功时实际交给 Slate。 */
	float GetDraftUIScale() const;

	/** 更新待应用的 UI 比例；将值限制在 0.75 到 2.0 之间，避免不可读或不可操作的前端布局。 */
	void SetDraftUIScale(float NewUIScale);

	/** 返回亮度调整是否可应用；GEngine 存在时对应真实 DisplayGamma，专用服务器或启动早期无引擎时返回 false。 */
	bool IsBrightnessSettingAvailable() const;

	/** 返回待应用的显示 Gamma；数值沿用 UE Gamma 命令的 0.5 到 5.0 范围，Apply 时写入真实渲染输出。 */
	float GetDraftDisplayGamma() const;

	/** 更新待应用的显示 Gamma；值限制在引擎接受范围，只记录草稿，实际渲染改变延后到 Apply。 */
	void SetDraftDisplayGamma(float NewDisplayGamma);

	/** 返回待应用的主音量比例；只有 SoundMix 与全部分类资产可用时 Apply 才会把它写入 AudioDevice。 */
	float GetDraftMasterVolume() const;

	/** 更新待应用的主音量比例；值限制在 0 到 1，缺少音频分类资产时仍只保留页面草稿。 */
	void SetDraftMasterVolume(float NewMasterVolume);

	/** 返回待应用的音乐音量比例；只有完整正式音频路由可用时 Apply 才会写入 AudioDevice。 */
	float GetDraftMusicVolume() const;

	/** 更新待应用的音乐音量比例；值限制在 0 到 1，真实分类覆盖延后到 Apply。 */
	void SetDraftMusicVolume(float NewMusicVolume);

	/** 返回待应用的音效音量比例；只有完整正式音频路由可用时 Apply 才会写入 AudioDevice。 */
	float GetDraftSFXVolume() const;

	/** 更新待应用的音效音量比例；值限制在 0 到 1，真实分类覆盖延后到 Apply。 */
	void SetDraftSFXVolume(float NewSFXVolume);

	/** 返回待应用的环境音音量比例；只有完整正式音频路由可用时 Apply 才会写入 AudioDevice。 */
	float GetDraftAmbienceVolume() const;

	/** 更新待应用的环境音音量比例；值限制在 0 到 1，真实分类覆盖延后到 Apply。 */
	void SetDraftAmbienceVolume(float NewAmbienceVolume);

	/** 返回待应用的语音分类音量比例；它不表示语音聊天开关，只有 Voice SoundClass 接入后才实际生效。 */
	float GetDraftVoiceVolume() const;

	/** 更新待应用的语音分类音量比例；值限制在 0 到 1，真实分类覆盖延后到 Apply。 */
	void SetDraftVoiceVolume(float NewVoiceVolume);

	/** 返回待应用的后台静音选择；它映射到引擎失焦音量倍率，Apply 时由 FApp 持久化并在窗口失焦时真实生效。 */
	bool GetDraftMuteAudioWhenUnfocused() const;

	/** 更新待应用的后台静音选择；只记录草稿，Apply 前不会改变当前窗口失焦时的引擎音量倍率。 */
	void SetDraftMuteAudioWhenUnfocused(bool bNewMuteAudioWhenUnfocused);

	/** 返回正式 SoundMix 与分类 SoundClass 资产是否已齐备；它只证明混音覆盖可提交，不证明现有内容已经路由到音乐、音效或环境分类。 */
	bool IsAudioRoutingAvailable() const;

	/** 返回音频输出设备切换是否已取得至少一个真实平台设备；设备枚举异步完成前为 false，不能显示可提交的空下拉框。 */
	bool IsOutputDeviceSettingAvailable() const;

	/** 向 AudioMixer 异步请求当前 World 的可用输出设备；重复请求或 World 缺失时返回 false，结果通过 OnChanged 刷新。 */
	bool RefreshAudioOutputDevices();

	/** 返回最近一次 AudioMixer 枚举到的输出设备数量；设备列表只由异步成功回调写入。 */
	int32 GetAudioOutputDeviceCount() const;

	/** 返回指定设备的可显示名称；索引无效时返回空字符串，View 不得据此猜测平台默认设备。 */
	const FString& GetAudioOutputDeviceName(int32 DeviceIndex) const;

	/** 返回指定设备的稳定平台 ID；该 ID 只用于提交给 AudioMixer 的热切换 API，不作为玩家身份或存档数据。 */
	const FString& GetAudioOutputDeviceId(int32 DeviceIndex) const;

	/** 返回待切换的输出设备 ID；空值表示保持平台当前默认设备，真实切换结果由异步回调确认。 */
	const FString& GetDraftAudioOutputDeviceId() const;

	/** 选择已枚举的输出设备 ID 作为草稿；未知 ID 会被拒绝，防止向 AudioMixer 提交过期或伪造设备标识。 */
	void SetDraftAudioOutputDeviceId(const FString& NewAudioOutputDeviceId);

	/** 返回输出设备枚举或热切换是否仍在等待 AudioMixer 回调；等待期间 View 必须禁用同类操作以避免覆盖关联结果。 */
	bool IsAudioOutputDeviceOperationPending() const;

	/** 将所有可实际生效的草稿提交给正式来源；画面、语言、UI 比例和音频彼此独立处理，缺少音频资产不能阻断可用画面设置。 */
	bool Apply();

	/** 放弃所有未应用草稿并从正式设置来源重新读取；不会调用引擎 ApplySettings，也不会恢复已经提交的运行时设置。 */
	void Cancel();

	/** 恢复可实现项目设置的默认草稿；只修改页面草稿，仍需玩家显式 Apply 才会实际改动窗口、语言、UI 或声音。 */
	void RestoreDefaults();

	/** 待应用状态只比较项目当前可真实提交的草稿字段；语音输入模式和麦克风这类不可用项不会制造脏数据。 */
	bool HasPendingChanges() const;

	/** 返回最近一次初始化、应用或降级的可显示结果；View 只展示文本，不依此文本推导业务状态。 */
	const FText& GetLastResultText() const;

	/** 设置页刷新通知；Initialize、草稿修改、Apply、Cancel 与 RestoreDefaults 完成后广播，外部只能读取本 Model。 */
	FCatFrontendSettingsChanged OnChanged;

private:
	/**
	 * 从正式设置来源重建整份草稿；Initialize、Cancel 与 Apply 后调用，确保页面不保留已提交前的旧值或无效音频草稿。
	 */
	void ReloadDraftFromSettings();

	/**
	 * 返回当前 LocalPlayer 所在 World；音频混音必须提交到其实际 AudioDevice，World 缺失时不回退到任意全局 World。
	 */
	UWorld* GetLocalPlayerWorld() const;

	/**
	 * 记录最近结果并广播页面刷新；每个公开状态变更统一经此处通知，避免 View 订阅多个草稿字段产生不同步显示。
	 */
	void PublishChanged(const FText& NewResultText);

	/** 返回绑定 LocalPlayer 的当前 Controller；震动只能写本地 Controller，切图或 Controller 未就绪时返回空。 */
	APlayerController* GetLocalPlayerController() const;

	/** 接收枚举或切换的最终结果；代次与对象身份均匹配才释放 pending，实际目标已活动才保存 ID，超时或拒绝保留旧偏好并开放重试。 */
	void HandleAudioOutputRequestCompleted(UCatAudioOutputRequest* Request, FName Error, uint64 RequestGeneration);

	/** 当前设置页绑定的本地玩家；Initialize 写入、Shutdown 清空，只用于定位本机 World 和生命周期，不保存设置真相。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ULocalPlayer> BoundLocalPlayer;

	/** 当前正式设置来源；Initialize 从 UCatGameUserSettings 单例取得，所有 Apply、Cancel 和默认值读取都通过它完成。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatGameUserSettings> UserSettings;

	/** 当前选择的游戏分类标志；四个 Select 方法互斥写入，View 读取它决定设置页显示内容。 */
	bool bGameSelected = true;

	/** 当前选择的画面分类标志；四个 Select 方法互斥写入，View 读取它决定设置页显示内容。 */
	bool bGraphicsSelected = false;

	/** 当前选择的声音分类标志；四个 Select 方法互斥写入，View 读取它决定设置页显示内容。 */
	bool bAudioSelected = false;

	/** 当前选择的控制分类标志；入口保留但没有虚构的控制字段，View 读取它展示控制细项延期说明。 */
	bool bControlsSelected = false;

	/** 当前草稿是否要求在 Apply 时调用 UE 的 SetToDefaults；RestoreDefaults 写入、ReloadDraftFromSettings 清除，确保取消不会提前修改权威设置。 */
	bool bDraftDefaultsRequested = false;

	/** 待应用的语言 culture 名称；页面输入写入、Apply 成功后由国际化系统持久化，Cancel 从当前语言重读。 */
	FString DraftLanguage;

	/** 当前本地化资源系统实际发现的游戏 culture 列表；Initialize 写入、Shutdown 清空，语言控件只读取这份运行时来源。 */
	TArray<FString> AvailableLanguages;

	/** 待应用的窗口显示模式；页面输入写入、Apply 交给 UGameUserSettings，Cancel 从当前设置重读。 */
	EWindowMode::Type DraftFullscreenMode = EWindowMode::WindowedFullscreen;

	/** 待应用的分辨率像素尺寸；页面输入写入、Apply 交给 UGameUserSettings，Cancel 从当前设置重读。 */
	FIntPoint DraftScreenResolution = FIntPoint::ZeroValue;

	/** 待应用的 UE 整体画质档位；页面输入写入、Apply 交给 UGameUserSettings，-1 保留自定义档语义。 */
	int32 DraftOverallScalabilityLevel = -1;

	/** 待应用的垂直同步选择；页面输入写入、Apply 交给 UGameUserSettings，Cancel 从当前设置重读。 */
	bool bDraftVSyncEnabled = false;

	/** 待应用的 Slate UI 比例；页面输入写入、Apply 成功后交给 UCatGameUserSettings 持久化与恢复。 */
	float DraftUIScale = 1.0f;

	/** 待应用的显示 Gamma；页面输入写入、Apply 成功后交给 GEngine，取消时从正式用户设置重读。 */
	float DraftDisplayGamma = 2.2f;

	/** 待应用的震动总开关；页面输入写入、Apply 成功后写入本地 PlayerController 的 ForceFeedback gate。 */
	bool bDraftVibrationEnabled = true;

	/** 待应用的网络语音开关；页面输入写入、Apply 成功后由当前 World 的 OSS Voice 接口开始或停止本地语音。 */
	bool bDraftVoiceChatEnabled = false;

	/** 待应用的主音量比例；页面输入写入、只在完整 SoundMix/SoundClass 路由实际成功时持久化。 */
	float DraftMasterVolume = 1.0f;

	/** 待应用的音乐音量比例；页面输入写入、只在完整 SoundMix/SoundClass 路由实际成功时持久化。 */
	float DraftMusicVolume = 1.0f;

	/** 待应用的音效音量比例；页面输入写入、只在完整 SoundMix/SoundClass 路由实际成功时持久化。 */
	float DraftSFXVolume = 1.0f;

	/** 待应用的环境音音量比例；页面输入写入、只在完整 SoundMix/SoundClass 路由实际成功时持久化。 */
	float DraftAmbienceVolume = 1.0f;

	/** 待应用的语音分类音量比例；页面输入写入、只在完整 SoundMix/SoundClass 路由实际成功时持久化。 */
	float DraftVoiceVolume = 1.0f;

	/** 待应用的后台静音开关；页面输入写入、Apply 时映射到 FApp 的失焦音量倍率，取消时从真实引擎状态重读。 */
	bool bDraftMuteAudioWhenUnfocused = true;

	/** AudioMixer 最近一次枚举到的设备显示名称；异步成功回调整体替换，View 只按索引读取。 */
	TArray<FString> AudioOutputDeviceNames;

	/** 与名称数组同序的稳定平台设备 ID；异步成功回调整体替换，热切换只能提交此数组中当前存在的值。 */
	TArray<FString> AudioOutputDeviceIds;

	/** 平台标记的系统默认输出设备 ID；设备枚举回调写入，恢复默认时把它作为真实热切换目标而不是保存一个不会改变运行设备的空值。 */
	FString SystemDefaultAudioOutputDeviceId;

	/** 待热切换的输出设备 ID；页面选择写入，成功回调才同步到正式用户设置并持久化。 */
	FString DraftAudioOutputDeviceId;

	/** 当前尚未收到最终结果的 AudioMixer 请求；非空即为 pending，完成或 Shutdown 取消并释放，身份校验阻止旧结果写入新页面。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatAudioOutputRequest> ActiveAudioOutputRequest;

	/** 输出设备异步请求的单调代次；每次新请求和每次 Shutdown 都递增，回调仅可提交与该值相等的结果。 */
	uint64 AudioOutputDeviceRequestGeneration = 0;

	/** 最近一次操作的用户可见说明；公开操作写入、View 只读，文本不作为是否应用成功的唯一事实来源。 */
	FText LastResultText;
};
