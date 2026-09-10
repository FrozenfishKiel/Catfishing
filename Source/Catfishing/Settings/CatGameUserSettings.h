#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "CatGameUserSettings.generated.h"

class USoundClass;
class USoundMix;
class APlayerController;
class AController;
class APawn;
class UGameInstance;
class UWorld;
class UCatAudioOutputRequest;

/**
 * 恢复默认时使用的干净设置快照；它描述项目第一次启动时应回到的值，不读取本机 GameUserSettings.ini，也不代表当前运行时已经应用这些值。
 */
struct CATFISHING_API FCatGameUserSettingsDefaultSnapshot
{
	/** 默认窗口模式；恢复默认页读取它填充画面草稿，应用阶段再交给 UGameUserSettings 切换实际窗口状态。 */
	EWindowMode::Type FullscreenMode = EWindowMode::WindowedFullscreen;

	/** 默认分辨率像素尺寸；零值沿用 UE 的启动默认策略，页面只把它作为恢复默认候选而不立即改窗口。 */
	FIntPoint ScreenResolution = FIntPoint::ZeroValue;

	/** 默认整体画质档位；-1 保留 UE 默认的自定义组合语义，应用时不强行替换为某个手动档。 */
	int32 OverallScalabilityLevel = -1;

	/** 默认垂直同步开关；恢复默认应用时写回 UGameUserSettings，避免已保存的 VSync 选择继续污染默认草稿。 */
	bool bVSyncEnabled = false;

	/** 默认语言 culture 名称；恢复默认页用它回到引擎启动语言，应用成功后才由国际化系统持久化。 */
	FString FrontendLanguage;

	/** 默认 Slate UI 比例；恢复默认和 SetToDefaults 共用它，避免上次保存的界面缩放成为新默认。 */
	float UIScale = 1.0f;

	/** 默认显示 Gamma；恢复默认和 SetToDefaults 共用它，避免上次保存的亮度成为新默认。 */
	float DisplayGamma = 2.2f;

	/** 默认震动开关；恢复默认应用时写回本地 PlayerController 的反馈 gate。 */
	bool bVibrationEnabled = true;

	/** 默认网络语音开关；恢复默认保持新配置不主动发送语音，已有会话仍由应用阶段显式提交。 */
	bool bVoiceChatEnabled = false;

	/** 默认后台静音偏好；恢复默认时重新交给 FApp 的失焦音量倍率。 */
	bool bMuteAudioWhenUnfocused = true;

	/** 默认主音量比例；恢复默认应用时写入正式 SoundMix，资产缺失时保持失败闭合。 */
	float MasterVolume = 1.0f;

	/** 默认音乐音量比例；恢复默认应用时写入音乐 SoundClass 覆盖。 */
	float MusicVolume = 1.0f;

	/** 默认音效音量比例；恢复默认应用时写入音效 SoundClass 覆盖。 */
	float SFXVolume = 1.0f;

	/** 默认环境音音量比例；恢复默认应用时写入环境 SoundClass 覆盖。 */
	float AmbienceVolume = 1.0f;

	/** 默认语音播放音量比例；恢复默认应用时写入语音 SoundClass 覆盖。 */
	float VoiceVolume = 1.0f;

	/** 默认输出设备偏好；空值表示跟随平台系统默认设备，不把某次枚举到的设备 ID 固定保存。 */
	FString AudioOutputDeviceId;
};

/**
 * 本机玩家可持久化的正式设置宿主；继承 UE 的分辨率、窗口模式和画质设置，补充项目已接线的语言、UI 缩放、Gamma、震动、网络语音、音频分类与输出设备偏好。
 * 页面 Model 只在正式 API 确认或完成调用后写入这些偏好；没有输入设备选择或控制映射来源的字段不在这里伪装成已生效设置。
 */
UCLASS(Config = GameUserSettings)
class CATFISHING_API UCatGameUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	/**
	 * 建立项目设置默认值与正式音频资产软引用；引擎创建 GameUserSettings 单例时调用，后续用户配置可覆盖音量而不能改变无资产时的失败策略。
	 */
	UCatGameUserSettings();

	/**
	 * 返回当前进程使用的正式项目设置单例；GameUserSettings 类配置错误或引擎尚未创建设置对象时返回空，调用方必须保持 fail-closed。
	 */
	static UCatGameUserSettings* Get();

	/**
	 * 创建不读取本机用户配置的恢复默认快照；设置页和 SetToDefaults 共用同一来源，防止 Config CDO 把上次保存值误当项目默认值。
	 */
	static FCatGameUserSettingsDefaultSnapshot MakeDefaultSnapshot();

	/**
	 * 将玩家选择的语言立即交给国际化系统并要求其持久化；只有引擎接受该 culture 名称时才返回成功，失败不会改写已生效语言。
	 */
	bool ApplyLanguage(const FString& NewLanguage);

	/**
	 * 把 UI 比例应用给已初始化的 Slate 应用并保存为本机偏好；比例限制在可读的运行时范围，专用服务器或 Slate 未启动时返回失败。
	 */
	bool ApplyUIScale(float NewUIScale);

	/** 将亮度草稿写入引擎真实 DisplayGamma；值遵循 UE Gamma 命令的 0.5 到 5.0 范围，成功后由本类随用户设置持久化。 */
	bool ApplyDisplayGamma(float NewDisplayGamma);

	/** 将震动启用状态应用给当前本地 PlayerController 的 ForceFeedback gate；没有本地 Controller 时不保存，避免下次启动误报已生效。 */
	bool ApplyVibration(APlayerController* PlayerController, bool bEnableVibration);

	/** 查询当前 World 的 OSS 是否提供可用 IOnlineVoice；Steam 支持该接口，空 OSS 或无 Voice 接口时返回 false。 */
	bool HasVoiceChatSupport(const UWorld* World) const;

	/** 启停当前 World 本地用户的 OSS 语音；开启须注册成功，关闭同时清包，接口命令提交后更新偏好，但不保证物理采集或远端接收。 */
	bool ApplyVoiceChat(UWorld* World, uint8 LocalUserNum, bool bEnableVoiceChat);

	/** 在正式 Session 完成本地 talker 注册后恢复已保存的发送选择；由 Online 成功回调调用，不保存新偏好，不修改远端 talker。 */
	void RestoreVoiceChatForLocalPlayers(UWorld* World);

	/** 放弃在途自动输出恢复的结果接收并释放请求；用户新选择、World 清理或销毁时调用，不回滚平台已受理的切换。 */
	void CancelAudioOutputRestore();

	/**
	 * 在引擎重载用户配置后恢复本项目额外设置；先让父类读取窗口和画质，再尽力恢复语言与 Slate 比例，失败项保留引擎当前安全状态。
	 */
	virtual void LoadSettings(bool bForceReload = false) override;

	/**
	 * 将 UE 画面默认和项目额外默认写回本设置实例；恢复默认的应用阶段调用，调用方随后仍需按能力 Apply 与 SaveSettings。
	 */
	virtual void SetToDefaults() override;

	/**
	 * 在设置宿主销毁时解除世界、控制器和异步回调关联；只清理本对象注册的委托，避免旅行或进程退出后失效 World 回调访问已销毁的设置实例。
	 */
	virtual void BeginDestroy() override;

	/** 将后台静音偏好应用给 FApp 并记录为用户设置；窗口失焦时平台读取该倍率，后续 LoadSettings 负责跨进程恢复。 */
	bool ApplyMuteAudioWhenUnfocused(bool bEnableMuteAudioWhenUnfocused);

	/**
	 * 判断 SoundMix 与五个正式 SoundClass 资产是否均可加载；缺少任一分类时音频草稿不得保存，避免把不存在的混音接线当成有效设置。
	 */
	bool HasAudioRoutingAssets() const;

	/**
	 * 将五个音量草稿交给当前 World 的唯一 Base SoundMix；Master 递归乘全部子类，四个同级分类各自递归乘分类值，提交后才同步持久化值，World 或资产缺失时保留既有配置。
	 */
	bool ApplyAudioVolumes(UWorld* World, float NewMasterVolume, float NewMusicVolume, float NewSFXVolume,
		float NewAmbienceVolume, float NewVoiceVolume);

	/** 返回当前已持久化的主音量比例，范围为 0 到 1；只有成功应用 SoundMix 后才更新。 */
	float GetMasterVolume() const;

	/** 音乐音量是当前已应用并持久化的 SoundClass 比例，范围为 0 到 1；设置页用它初始化草稿，子分类会继续受主音量影响。 */
	float GetMusicVolume() const;

	/** 音效音量是当前已应用并持久化的 SoundClass 比例，范围为 0 到 1；设置页用它初始化草稿，子分类会继续受主音量影响。 */
	float GetSFXVolume() const;

	/** 环境音音量是当前已应用并持久化的 SoundClass 比例，范围为 0 到 1；设置页用它初始化草稿，子分类会继续受主音量影响。 */
	float GetAmbienceVolume() const;

	/** 返回当前已持久化的语音分类音量比例，范围为 0 到 1；它只控制正式 Voice SoundClass，不充当语音聊天开关。 */
	float GetVoiceVolume() const;

	/** 返回当前已持久化的 Slate UI 比例；该值由 ApplyUIScale 成功后写入，供下次前端初始化恢复显示。 */
	float GetUIScale() const;

	/** 返回当前已生效的引擎 DisplayGamma；设置页用它建立亮度草稿，返回值来自本类最近成功应用的持久化记录。 */
	float GetDisplayGamma() const;

	/** 返回最近一次成功写入本地 PlayerController 的震动开关；它不代表某台设备一定具备物理触觉马达。 */
	bool IsVibrationEnabled() const;

	/** 返回最近一次成功交给 OSS Voice 的网络语音开关；它不代表麦克风设备选择能力或远端成员已入房。 */
	bool IsVoiceChatEnabled() const;

	/** 返回已持久化的后台静音偏好；页面以它建立草稿，而不是从可能被临时系统流程改写的 FApp 运行值反推用户选择。 */
	bool IsMuteAudioWhenUnfocused() const;

	/** 返回最近一次观察到目标实际活动后保存的输出设备 ID；空值表示继续使用平台当前默认设备。 */
	const FString& GetAudioOutputDeviceId() const;

	/** 记录已在 Mixer 活动设备信息中确认的目标 ID；仅 Model 的最终确认回调调用，随后由 SaveSettings 写入本机配置。 */
	void SetAudioOutputDeviceId(const FString& NewAudioOutputDeviceId);

private:
	/**
	 * 登记正式设置的世界生命周期观察；LoadSettings 只为真实单例注册一次，并补扫已存在 World，使前端销毁后仍能在旅行目标恢复偏好。
	 */
	void RegisterWorldLifecycle();

	/**
	 * 接收引擎完成初始化的 World；游戏 World 尚未 BeginPlay 时登记一次性回调，已经运行的 World 立即走同一恢复流程。
	 */
	void HandleWorldPostInitialization(UWorld* World, const UWorld::InitializationValues InitializationValues);

	/**
	 * 跟踪单个游戏 World 的运行阶段；为未开始的 World 延迟恢复，为已开始的 World 确保只恢复一次，避免多次 LoadSettings 重复发起音频设备切换。
	 */
	void TrackGameWorld(UWorld* World);

	/**
	 * 在任一被跟踪 World 开始运行时检查全部待处理 World；对已开始者恢复偏好并撤销其一次性 BeginPlay 委托，未开始者继续等待。
	 */
	void HandleTrackedWorldBeginPlay();

	/**
	 * 在 World 清理前移除对应 BeginPlay 句柄与已恢复标记；旅行后的失效 World 不会保留到下一张地图，也不会让弱引用集合无限增长。
	 */
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);

	/**
	 * 为 GameInstance 的控制器变更登记一次恢复回调；同一个 GameInstance 跨地图复用时只绑定一次，BeginDestroy 负责对称解绑。
	 */
	void RegisterGameInstanceControllerRecovery(UGameInstance* GameInstance);

	/**
	 * 在 GameInstance 报告 Pawn-Controller 关联完成后恢复本地 Controller 的震动与可用 OSS 语音；远端 Controller 和非游戏 World 不参与本机偏好写入。
	 */
	UFUNCTION()
	void HandlePawnControllerChanged(APawn* Pawn, AController* Controller);

	/**
	 * 将持久化的音频、震动、语音和输出设备偏好恢复给已开始的 World；分类音频和输出设备均定位到该 World 的 AudioDevice，避免使用已销毁 Frontend Model 的上下文。
	 */
	void RestoreRuntimePreferencesForWorld(UWorld* World);

	/**
	 * 将依赖本地 Controller 的偏好恢复到新创建或重绑的控制器；震动始终尝试，语音仅在当前 OSS 暴露 Voice 接口时重新提交。
	 */
	void RestoreRuntimePreferencesForController(APlayerController* PlayerController);

	/**
	 * 接收世界恢复请求的最终活动确认或失败；只处理仍由本宿主持有的请求，记录结果但不改写原有设备偏好。
	 */
	void HandleRuntimeAudioOutputDeviceSwapCompleted(UCatAudioOutputRequest* Request, FName Error);

	/**
	 * 解析正式 SoundMix 与五个分类资产；每次应用都重新验证软引用，避免热重载、资源缺失或错误类继承时向 AudioDevice 提交半套覆盖。
	 */
	bool ResolveAudioRouting(USoundMix*& OutSoundMix, USoundClass*& OutMasterClass, USoundClass*& OutMusicClass,
		USoundClass*& OutSFXClass, USoundClass*& OutAmbienceClass, USoundClass*& OutVoiceClass) const;

	/**
	 * 前端总线的正式 SoundMix；Editor 资产作者在 /Game/Audio/Settings/SMX_CatFrontendSettings 创建它，音量应用以此混音作为唯一覆盖容器。
	 */
	UPROPERTY(Config)
	TSoftObjectPtr<USoundMix> FrontendSoundMix;

	/**
	 * 总音量 SoundClass；Editor 资产作者在 /Game/Audio/Settings/SC_CatMaster 创建它，所有项目声音应在资产层归入该根分类。
	 */
	UPROPERTY(Config)
	TSoftObjectPtr<USoundClass> MasterSoundClass;

	/** 音乐分类 SoundClass；Editor 资产作者在 /Game/Audio/Settings/SC_CatMusic 创建它，音乐资产必须挂在此分类或其子类。 */
	UPROPERTY(Config)
	TSoftObjectPtr<USoundClass> MusicSoundClass;

	/** 音效分类 SoundClass；Editor 资产作者在 /Game/Audio/Settings/SC_CatSFX 创建它，交互与玩法音效必须挂在此分类或其子类。 */
	UPROPERTY(Config)
	TSoftObjectPtr<USoundClass> SFXSoundClass;

	/** 环境音分类 SoundClass；Editor 资产作者在 /Game/Audio/Settings/SC_CatAmbience 创建它，环境循环与地点音效必须挂在此分类或其子类。 */
	UPROPERTY(Config)
	TSoftObjectPtr<USoundClass> AmbienceSoundClass;

	/** 语音分类 SoundClass；Editor 资产作者在 /Game/Audio/Settings/SC_CatVoice 创建它，语音播放资产接入后才受本设置控制。 */
	UPROPERTY(Config)
	TSoftObjectPtr<USoundClass> VoiceSoundClass;

	/** 已实际写入 AudioDevice 的主音量比例；ApplyAudioVolumes 成功写入，设置页读取它作为下次草稿基线。 */
	UPROPERTY(Config)
	float MasterVolume = 1.0f;

	/** 已实际写入 AudioDevice 的音乐音量比例；ApplyAudioVolumes 成功写入，缺少分类资产时保持变更前值。 */
	UPROPERTY(Config)
	float MusicVolume = 1.0f;

	/** 已实际写入 AudioDevice 的音效音量比例；ApplyAudioVolumes 成功写入，缺少分类资产时保持变更前值。 */
	UPROPERTY(Config)
	float SFXVolume = 1.0f;

	/** 已实际写入 AudioDevice 的环境音音量比例；ApplyAudioVolumes 成功写入，缺少分类资产时保持变更前值。 */
	UPROPERTY(Config)
	float AmbienceVolume = 1.0f;

	/** 已实际写入 AudioDevice 的语音播放音量比例；ApplyAudioVolumes 成功写入，与本地语音发送开关相互独立。 */
	UPROPERTY(Config)
	float VoiceVolume = 1.0f;

	/** 已成功交给 Slate 的全局 UI 比例；ApplyUIScale 写入，前端可在本地进程内恢复相同显示密度。 */
	UPROPERTY(Config)
	float UIScale = 1.0f;

	/** 已成功应用的语言与区域 culture 名称；ApplyLanguage 写入、LoadSettings 恢复，空值表示沿用引擎与操作系统的启动默认值。 */
	UPROPERTY(Config)
	FString FrontendLanguage;

	/** 已成功交给渲染引擎的显示 Gamma；ApplyDisplayGamma 写入，设置页用它在下一次打开时恢复真实亮度基线。 */
	UPROPERTY(Config)
	float DisplayGamma = 2.2f;

	/** 已成功写入本地 PlayerController 的 ForceFeedback 总开关；ApplyVibration 写入，未找到 Controller 时保持上次值。 */
	UPROPERTY(Config)
	bool bVibrationEnabled = true;

	/** 用户保存的网络语音发送选择；新配置默认关闭，已有 Config 值仍由引擎加载，ApplyVoiceChat 成功后更新，注册与旅行只恢复此选择。 */
	UPROPERTY(Config)
	bool bVoiceChatEnabled = false;

	/** 已成功交给 FApp 的后台静音偏好；ApplyMuteAudioWhenUnfocused 写入，LoadSettings 在每次进程启动时恢复其真实失焦倍率。 */
	UPROPERTY(Config)
	bool bMuteAudioWhenUnfocused = true;

	/** 已在 Mixer 活动设备信息中确认的输出设备 ID；页面最终确认后写入，空值表示不覆盖平台默认输出设备。 */
	UPROPERTY(Config)
	FString AudioOutputDeviceId;

	/** 尚未完成的世界输出设备恢复请求；世界恢复写入，用户选择、World 清理或完成回调释放，期间以强引用保持请求身份。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatAudioOutputRequest> AudioOutputRestoreRequest;

	/** 世界初始化全局委托的本对象句柄；RegisterWorldLifecycle 写入，BeginDestroy 移除，保证设置单例不会在销毁后接收新地图事件。 */
	FDelegateHandle WorldPostInitializationHandle;

	/** 世界清理全局委托的本对象句柄；用于旅行时及时移除每个 World 的 BeginPlay 句柄和已恢复记录。 */
	FDelegateHandle WorldCleanupHandle;

	/** 尚未 BeginPlay 的游戏 World 与一次性恢复委托；TrackGameWorld 写入，BeginPlay 或清理时删除，避免前端生命周期成为恢复前提。 */
	TMap<TWeakObjectPtr<UWorld>, FDelegateHandle> PendingWorldBeginPlayHandles;

	/** 已完成运行时偏好恢复的游戏 World；用于抑制重复音频输出切换，并在 World 清理时移除对应条目。 */
	TSet<TWeakObjectPtr<UWorld>> RestoredWorlds;

	/** 已绑定 Pawn-Controller 通知的 GameInstance；跨地图只保留一条动态委托，设置宿主销毁时逐个解除。 */
	TSet<TWeakObjectPtr<UGameInstance>> ControllerRecoveryGameInstances;
};
