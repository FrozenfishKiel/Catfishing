#include "Settings/CatGameUserSettings.h"
#include "Settings/CatAudioOutputRequest.h"

#include "AudioDevice.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Kismet/GameplayStatics.h"
#include "Interfaces/VoiceInterface.h"
#include "Logging/CatLog.h"
#include "Misc/App.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "Scalability.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"

// 构造流程：
// 1. 为正式混音资产写入稳定软路径，资产尚未由 Editor 创建时保留空加载结果而不制造替身。
// 2. 其余用户偏好沿用头文件默认值，随后由引擎 GameUserSettings 配置覆盖。
// 3. 不在构造期应用语言、Slate 或音频，因为此时 World 与 AudioDevice 尚未保证可用。
UCatGameUserSettings::UCatGameUserSettings()
{
	FrontendSoundMix = TSoftObjectPtr<USoundMix>(FSoftObjectPath(
		TEXT("/Game/Audio/Settings/SMX_CatFrontendSettings.SMX_CatFrontendSettings")));
	MasterSoundClass = TSoftObjectPtr<USoundClass>(FSoftObjectPath(
		TEXT("/Game/Audio/Settings/SC_CatMaster.SC_CatMaster")));
	MusicSoundClass = TSoftObjectPtr<USoundClass>(FSoftObjectPath(
		TEXT("/Game/Audio/Settings/SC_CatMusic.SC_CatMusic")));
	SFXSoundClass = TSoftObjectPtr<USoundClass>(FSoftObjectPath(
		TEXT("/Game/Audio/Settings/SC_CatSFX.SC_CatSFX")));
	AmbienceSoundClass = TSoftObjectPtr<USoundClass>(FSoftObjectPath(
		TEXT("/Game/Audio/Settings/SC_CatAmbience.SC_CatAmbience")));
	VoiceSoundClass = TSoftObjectPtr<USoundClass>(FSoftObjectPath(
		TEXT("/Game/Audio/Settings/SC_CatVoice.SC_CatVoice")));
}

// 项目设置单例读取流程确保前端只接触配置指定的 UCatGameUserSettings，避免误用基类实例丢失项目字段：
// 1. 向引擎读取唯一 GameUserSettings 实例，避免页面私自 NewObject 形成第二份持久化来源。
// 2. 验证 DefaultEngine 指向本项目子类；配置错误或启动早期无法取得实例时返回空。
// 3. 调用方据此禁用设置页实际提交，而不是退回基类后丢失项目音频与 UI 缩放配置。
UCatGameUserSettings* UCatGameUserSettings::Get()
{
	return Cast<UCatGameUserSettings>(UGameUserSettings::GetGameUserSettings());
}

// 默认快照创建流程：
// 1. 画面默认沿用 UE SetToDefaults 同源入口，保持窗口、分辨率和画质组合与引擎版本一致。
// 2. 项目字段使用结构体中不读 Config 的初始值，避免 GameUserSettings.ini 覆盖过的 CDO 反向污染恢复默认。
// 3. 语言写入引擎启动默认 culture，输出设备保持空偏好以表达“跟随平台系统默认”。
FCatGameUserSettingsDefaultSnapshot UCatGameUserSettings::MakeDefaultSnapshot()
{
	FCatGameUserSettingsDefaultSnapshot Defaults;
	Defaults.FullscreenMode = UGameUserSettings::GetDefaultWindowMode();
	Defaults.ScreenResolution = UGameUserSettings::GetDefaultResolution();

	Scalability::FQualityLevels DefaultQualityLevels;
	DefaultQualityLevels.SetDefaults();
	Defaults.OverallScalabilityLevel = DefaultQualityLevels.GetSingleQualityLevel();
	Defaults.FrontendLanguage = FInternationalization::Get().GetDefaultLanguage()->GetName();
	return Defaults;
}

// 语言应用流程：
// 1. 拒绝空 culture，避免把无效输入保存为“当前语言”。
// 2. 让国际化系统原子切换语言与区域，失败时保留此前生效状态和持久化字段。
// 3. 只有切换成功才记录 culture 名称，后续由 ApplySettings/SaveSettings 写入本机配置。
bool UCatGameUserSettings::ApplyLanguage(const FString& NewLanguage)
{
	if (NewLanguage.IsEmpty() || !FInternationalization::Get().SetCurrentLanguageAndLocale(NewLanguage))
	{
		return false;
	}

	FrontendLanguage = NewLanguage;
	return true;
}

// UI 比例应用流程：
// 1. 先确认 Slate 已启动；专用服务器或引擎启动早期没有 UI 时不能伪装为成功。
// 2. 把输入限制到前端约定的可读范围，避免配置文件异常把整个界面缩到不可操作。
// 3. 将有效值交给 Slate 并记录为用户偏好，持久化仍由页面最终的 SaveSettings 统一完成。
bool UCatGameUserSettings::ApplyUIScale(float NewUIScale)
{
	if (!FSlateApplication::IsInitialized())
	{
		return false;
	}

	UIScale = FMath::Clamp(NewUIScale, 0.75f, 2.0f);
	FSlateApplication::Get().SetApplicationScale(UIScale);
	return true;
}

// 亮度应用流程：
// 1. 先确认全局引擎实例存在，专用服务器或启动早期没有渲染输出时不能保存假成功。
// 2. 使用 UE Gamma 命令相同的安全范围裁剪输入，避免损坏配置让渲染目标取得无效 Gamma。
// 3. 同时更新 GEngine 的真实输出值和本机持久化记录，调用方后续统一 SaveSettings。
bool UCatGameUserSettings::ApplyDisplayGamma(const float NewDisplayGamma)
{
	if (!GEngine)
	{
		return false;
	}

	DisplayGamma = FMath::Clamp(NewDisplayGamma, 0.5f, 5.0f);
	GEngine->DisplayGamma = DisplayGamma;
	return true;
}

// 震动应用流程：
// 1. 只接受当前本地 PlayerController，避免设置页跨网络修改其他玩家的反馈 gate。
// 2. 将正式偏好写入 Controller 的 bForceFeedbackEnabled，UE 更新输入设备时会据此输出实际值或归零。
// 3. 仅写入成功后更新本机持久化字段；控制器不存在时保持旧偏好，由 World/Controller 生命周期恢复。
bool UCatGameUserSettings::ApplyVibration(APlayerController* PlayerController, const bool bEnableVibration)
{
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return false;
	}

	PlayerController->bForceFeedbackEnabled = bEnableVibration;
	bVibrationEnabled = bEnableVibration;
	return true;
}

// 语音能力查询流程：按当前 World 查询对应 OSS，只有其 Voice 接口有效才开放开关；禁止退回进程级默认 OSS，以免 PIE 或多本地玩家串线。
bool UCatGameUserSettings::HasVoiceChatSupport(const UWorld* World) const
{
	IOnlineSubsystem* OnlineSubsystem = World ? Online::GetSubsystem(World) : nullptr;
	return OnlineSubsystem && OnlineSubsystem->GetVoiceInterface().IsValid();
}

// 语音应用流程：
// 1. 检查 World 对应 OSS 的 Voice 接口和本地用户范围；接口缺失或非法用户均记录失败，不改偏好。
// 2. 开启时要求正式 talker 注册成功后再启动发送；注册失败立即停止并清包，避免 RegisterLocalTalker 自带的启动副作用遗留。
// 3. 关闭时停止发送并清除排队包；接口命令成功提交后更新偏好。Start/Stop 为 void，日志不把提交等同于麦克风采集或远端收听证明。
bool UCatGameUserSettings::ApplyVoiceChat(UWorld* World, const uint8 LocalUserNum, const bool bEnableVoiceChat)
{
	IOnlineSubsystem* OnlineSubsystem = World ? Online::GetSubsystem(World) : nullptr;
	const IOnlineVoicePtr VoiceInterface = OnlineSubsystem ? OnlineSubsystem->GetVoiceInterface() : nullptr;
	if (!VoiceInterface.IsValid() || LocalUserNum >= VoiceInterface->GetNumLocalTalkers())
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=settings_voice_apply_rejected World=%s LocalUser=%u Enabled=%s Reason=VoiceOrLocalUserUnavailable"),
			*GetNameSafe(World), LocalUserNum, bEnableVoiceChat ? TEXT("true") : TEXT("false"));
		return false;
	}

	if (bEnableVoiceChat)
	{
		if (!VoiceInterface->RegisterLocalTalker(LocalUserNum))
		{
			VoiceInterface->StopNetworkedVoice(LocalUserNum);
			VoiceInterface->ClearVoicePackets();
			UE_LOG(LogCatUI, Warning, TEXT("Event=settings_voice_apply_rejected World=%s NetMode=%d LocalUser=%u Reason=TalkerRegistrationFailed"),
				*GetNameSafe(World), static_cast<int32>(World->GetNetMode()), LocalUserNum);
			return false;
		}
		VoiceInterface->StartNetworkedVoice(LocalUserNum);
	}
	else
	{
		VoiceInterface->StopNetworkedVoice(LocalUserNum);
		VoiceInterface->ClearVoicePackets();
	}

	bVoiceChatEnabled = bEnableVoiceChat;
	UE_LOG(LogCatUI, Log, TEXT("Event=settings_voice_preference_dispatched World=%s NetMode=%d LocalUser=%u Enabled=%s CaptureConfirmed=false"),
		*GetNameSafe(World), static_cast<int32>(World->GetNetMode()), LocalUserNum, bEnableVoiceChat ? TEXT("true") : TEXT("false"));
	return true;
}

// 会话语音恢复流程：在 Online 确认本地 talker 注册完成后，读取原有发送偏好并逐一提交给本 World 的本地玩家；非法 ControllerId 跳过，关闭选择会撤销 Steam 注册时的自动发送，不写配置。
void UCatGameUserSettings::RestoreVoiceChatForLocalPlayers(UWorld* World)
{
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	if (!GameInstance)
	{
		return;
	}
	for (ULocalPlayer* LocalPlayer : GameInstance->GetLocalPlayers())
	{
		const int32 LocalUser = LocalPlayer ? LocalPlayer->GetControllerId() : INDEX_NONE;
		if (LocalUser >= 0 && LocalUser <= MAX_uint8)
		{
			ApplyVoiceChat(World, static_cast<uint8>(LocalUser), bVoiceChatEnabled);
		}
	}
}

// 自动输出恢复取消流程：撤销尚在等待的请求并释放身份引用；已进入平台的切换无法撤回，但旧结果不再触发恢复完成通知，也不会保存任何设备偏好。
void UCatGameUserSettings::CancelAudioOutputRestore()
{
	if (AudioOutputRestoreRequest)
	{
		AudioOutputRestoreRequest->Cancel();
		AudioOutputRestoreRequest = nullptr;
	}
}

// 项目设置重载流程在 UE 基类完成后恢复本项目字段，并把需要 World/AudioDevice 的工作延后到生命周期回调：
// 1. 先让 UE 基类读取并校验窗口、分辨率、垂直同步与画质配置。
// 2. 再恢复本项目持久化的语言；culture 已失效时保留引擎启动语言，不覆盖为猜测值。
// 3. Slate 可用时恢复经范围校验的 UI 比例，FApp 恢复用户的失焦音量倍率；随后登记 World 生命周期，让分类音频等待对应 AudioDevice 而非依赖前端页面存活。
void UCatGameUserSettings::LoadSettings(const bool bForceReload)
{
	Super::LoadSettings(bForceReload);

	if (!FrontendLanguage.IsEmpty())
	{
		FInternationalization::Get().SetCurrentLanguageAndLocale(FrontendLanguage);
	}

	UIScale = FMath::Clamp(UIScale, 0.75f, 2.0f);
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetApplicationScale(UIScale);
	}
	if (GEngine)
	{
		GEngine->DisplayGamma = FMath::Clamp(DisplayGamma, 0.5f, 5.0f);
	}
	FApp::SetUnfocusedVolumeMultiplier(bMuteAudioWhenUnfocused ? 0.0f : 1.0f);
	RegisterWorldLifecycle();
}

// 默认恢复写入流程：
// 1. 先执行 UE 基类默认逻辑，恢复窗口、分辨率、画质、HDR 等引擎内建字段。
// 2. 再用项目干净默认快照覆盖本类额外偏好和基类未主动重置的 VSync，确保恢复默认不读取用户 ini。
// 3. 这里只改设置对象状态，不直接触发语言、Slate、Gamma、SoundMix 或输出设备切换；调用方负责按可用能力应用并保存。
void UCatGameUserSettings::SetToDefaults()
{
	Super::SetToDefaults();

	const FCatGameUserSettingsDefaultSnapshot Defaults = MakeDefaultSnapshot();
	SetVSyncEnabled(Defaults.bVSyncEnabled);
	FrontendLanguage = Defaults.FrontendLanguage;
	UIScale = Defaults.UIScale;
	DisplayGamma = Defaults.DisplayGamma;
	bVibrationEnabled = Defaults.bVibrationEnabled;
	bVoiceChatEnabled = Defaults.bVoiceChatEnabled;
	bMuteAudioWhenUnfocused = Defaults.bMuteAudioWhenUnfocused;
	MasterVolume = Defaults.MasterVolume;
	MusicVolume = Defaults.MusicVolume;
	SFXVolume = Defaults.SFXVolume;
	AmbienceVolume = Defaults.AmbienceVolume;
	VoiceVolume = Defaults.VoiceVolume;
	AudioOutputDeviceId = Defaults.AudioOutputDeviceId;
}

// 销毁流程：
// 1. 先从每个仍有效的 World 撤销 BeginPlay 回调，再清空本地跟踪集合，防止旅行或异步清理阶段回调本对象。
// 2. 对每个已绑定 GameInstance 对称移除 Pawn-Controller 动态委托，避免它在设置对象析构后继续恢复 Controller 状态。
// 3. 取消在途输出恢复，移除全局 World 委托并交给父类释放资源；不改写任何用户配置。
void UCatGameUserSettings::BeginDestroy()
{
	CancelAudioOutputRestore();
	for (const TPair<TWeakObjectPtr<UWorld>, FDelegateHandle>& PendingWorld : PendingWorldBeginPlayHandles)
	{
		if (UWorld* World = PendingWorld.Key.Get())
		{
			World->OnWorldBeginPlay.Remove(PendingWorld.Value);
		}
	}
	PendingWorldBeginPlayHandles.Empty();
	RestoredWorlds.Empty();

	for (const TWeakObjectPtr<UGameInstance>& GameInstanceReference : ControllerRecoveryGameInstances)
	{
		if (UGameInstance* GameInstance = GameInstanceReference.Get())
		{
			GameInstance->GetOnPawnControllerChanged().RemoveDynamic(this,
				&UCatGameUserSettings::HandlePawnControllerChanged);
		}
	}
	ControllerRecoveryGameInstances.Empty();

	if (WorldPostInitializationHandle.IsValid())
	{
		FWorldDelegates::OnPostWorldInitialization.Remove(WorldPostInitializationHandle);
		WorldPostInitializationHandle.Reset();
	}
	if (WorldCleanupHandle.IsValid())
	{
		FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
		WorldCleanupHandle.Reset();
	}

	Super::BeginDestroy();
}

// 后台静音应用流程：
// 1. 将用户布尔偏好映射为引擎定义的失焦倍率，静音为零、允许后台声音为一。
// 2. 同步更新 FApp 的运行时倍率，使当前进程下次失焦立即使用新策略。
// 3. 记录同一布尔值供 SaveSettings 与 LoadSettings 跨进程恢复，而不依赖 FApp 仅写入的 Engine 配置缓存。
bool UCatGameUserSettings::ApplyMuteAudioWhenUnfocused(const bool bEnableMuteAudioWhenUnfocused)
{
	FApp::SetUnfocusedVolumeMultiplier(bEnableMuteAudioWhenUnfocused ? 0.0f : 1.0f);
	bMuteAudioWhenUnfocused = bEnableMuteAudioWhenUnfocused;
	return true;
}

// 音频资产检查流程：解析全部正式软引用并要求六个对象均存在；任何一个分类缺失都会让页面保持禁用，避免只写半套总线产生不可解释的音量结果。
bool UCatGameUserSettings::HasAudioRoutingAssets() const
{
	USoundMix* SoundMix = nullptr;
	USoundClass* MasterClass = nullptr;
	USoundClass* MusicClass = nullptr;
	USoundClass* SFXClass = nullptr;
	USoundClass* AmbienceClass = nullptr;
	USoundClass* VoiceClass = nullptr;
	return ResolveAudioRouting(SoundMix, MasterClass, MusicClass, SFXClass, AmbienceClass, VoiceClass);
}

// 音量应用流程：
// 1. 先验证 World、AudioDevice 和完整分类资产，缺任一项均不写持久化字段。
// 2. 对唯一 Base SoundMix 写入递归 Master 乘子，再为其四个同级分类写入各自递归乘子；分类值不预乘 Master，故每个叶类最终只乘一次 Master 和一次所属分类值。
// 3. 不再重复 PushSoundMixModifier 增加 ActiveRefCount；覆盖提交后更新五个持久化值，供页面最终 SaveSettings 落盘。
bool UCatGameUserSettings::ApplyAudioVolumes(UWorld* World, const float NewMasterVolume, const float NewMusicVolume,
	const float NewSFXVolume, const float NewAmbienceVolume, const float NewVoiceVolume)
{
	USoundMix* SoundMix = nullptr;
	USoundClass* MasterClass = nullptr;
	USoundClass* MusicClass = nullptr;
	USoundClass* SFXClass = nullptr;
	USoundClass* AmbienceClass = nullptr;
	USoundClass* VoiceClass = nullptr;
	if (!World || !World->GetAudioDevice()
		|| !ResolveAudioRouting(SoundMix, MasterClass, MusicClass, SFXClass, AmbienceClass, VoiceClass))
	{
		return false;
	}

	const float ClampedMasterVolume = FMath::Clamp(NewMasterVolume, 0.0f, 1.0f);
	const float ClampedMusicVolume = FMath::Clamp(NewMusicVolume, 0.0f, 1.0f);
	const float ClampedSFXVolume = FMath::Clamp(NewSFXVolume, 0.0f, 1.0f);
	const float ClampedAmbienceVolume = FMath::Clamp(NewAmbienceVolume, 0.0f, 1.0f);
	const float ClampedVoiceVolume = FMath::Clamp(NewVoiceVolume, 0.0f, 1.0f);
	// UE 先传播 SoundClass 原始属性、后应用 Mix；必须在 Mix 中递归，Master=0 才会覆盖所有子分类。
	UGameplayStatics::SetSoundMixClassOverride(World, SoundMix, MasterClass, ClampedMasterVolume, 1.0f, 0.0f, true);
	UGameplayStatics::SetSoundMixClassOverride(World, SoundMix, MusicClass, ClampedMusicVolume, 1.0f, 0.0f, true);
	UGameplayStatics::SetSoundMixClassOverride(World, SoundMix, SFXClass, ClampedSFXVolume, 1.0f, 0.0f, true);
	UGameplayStatics::SetSoundMixClassOverride(World, SoundMix, AmbienceClass, ClampedAmbienceVolume, 1.0f, 0.0f, true);
	UGameplayStatics::SetSoundMixClassOverride(World, SoundMix, VoiceClass, ClampedVoiceVolume, 1.0f, 0.0f, true);
	MasterVolume = ClampedMasterVolume;
	MusicVolume = ClampedMusicVolume;
	SFXVolume = ClampedSFXVolume;
	AmbienceVolume = ClampedAmbienceVolume;
	VoiceVolume = ClampedVoiceVolume;
	return true;
}

// 主音量读取流程：返回最近一次成功写入 AudioDevice 的持久化值，不触发资源加载或运行时音频副作用。
float UCatGameUserSettings::GetMasterVolume() const
{
	return MasterVolume;
}

// 音乐音量读取流程：返回最近一次成功写入 AudioDevice 的持久化值，不触发资源加载或运行时音频副作用。
float UCatGameUserSettings::GetMusicVolume() const
{
	return MusicVolume;
}

// 音效音量读取流程：返回最近一次成功写入 AudioDevice 的持久化值，不触发资源加载或运行时音频副作用。
float UCatGameUserSettings::GetSFXVolume() const
{
	return SFXVolume;
}

// 环境音音量读取流程：返回最近一次成功写入 AudioDevice 的持久化值，不触发资源加载或运行时音频副作用。
float UCatGameUserSettings::GetAmbienceVolume() const
{
	return AmbienceVolume;
}

// 语音音量读取流程：返回最近一次成功写入 AudioDevice 的持久化值，不把该值误解为语音聊天启用状态。
float UCatGameUserSettings::GetVoiceVolume() const
{
	return VoiceVolume;
}

// UI 比例读取流程：返回最近一次成功交给 Slate 的持久化值，页面读取不会重新缩放整个应用。
float UCatGameUserSettings::GetUIScale() const
{
	return UIScale;
}

// 亮度读取流程：返回最近一次成功写入 GEngine 的持久化 Gamma，不触发任何渲染状态修改。
float UCatGameUserSettings::GetDisplayGamma() const
{
	return DisplayGamma;
}

// 震动读取流程：返回最近一次成功写入本地 Controller 的持久化开关，不推断当前硬件是否存在触觉马达。
bool UCatGameUserSettings::IsVibrationEnabled() const
{
	return bVibrationEnabled;
}

// 语音读取流程：返回最近一次成功提交给 OSS Voice 的持久化开关，不把它当作麦克风设备选择或远端连通证明。
bool UCatGameUserSettings::IsVoiceChatEnabled() const
{
	return bVoiceChatEnabled;
}

// 后台静音读取流程：返回正式用户设置的布尔偏好；页面读取不查询或覆盖 FApp 的临时运行倍率。
bool UCatGameUserSettings::IsMuteAudioWhenUnfocused() const
{
	return bMuteAudioWhenUnfocused;
}

// 输出设备读取流程：返回最近一次 AudioMixer 成功热切换后保存的设备 ID；空值保持平台默认设备策略。
const FString& UCatGameUserSettings::GetAudioOutputDeviceId() const
{
	return AudioOutputDeviceId;
}

// 输出设备持久化流程：只由活动设备确认后的 Model 回调调用，复制已观察到的目标 ID；实际 SaveSettings 仍由 Model 在同一确认路径完成。
void UCatGameUserSettings::SetAudioOutputDeviceId(const FString& NewAudioOutputDeviceId)
{
	AudioOutputDeviceId = NewAudioOutputDeviceId;
}

// 世界生命周期登记流程：
// 1. CDO 不拥有玩家或 World，直接跳过；正式单例只登记一次全局初始化与清理委托。
// 2. 再检查此时已存在的 World，覆盖设置对象晚于 Frontend World 创建的启动顺序。
// 3. 后续地图由全局委托进入同一 TrackGameWorld 路径，不需要 Frontend Model 在 Lake 中继续存活。
void UCatGameUserSettings::RegisterWorldLifecycle()
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	if (!WorldPostInitializationHandle.IsValid())
	{
		WorldPostInitializationHandle = FWorldDelegates::OnPostWorldInitialization.AddUObject(this,
			&UCatGameUserSettings::HandleWorldPostInitialization);
	}
	if (!WorldCleanupHandle.IsValid())
	{
		WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this,
			&UCatGameUserSettings::HandleWorldCleanup);
	}

	if (GEngine)
	{
		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			TrackGameWorld(WorldContext.World());
		}
	}
}

// World 初始化处理流程：收到引擎新建完成的 World 后交给统一跟踪函数；初始化参数只用于匹配引擎委托签名，恢复时机以 IsGameWorld 与 BeginPlay 状态为准。
void UCatGameUserSettings::HandleWorldPostInitialization(UWorld* World,
	const UWorld::InitializationValues)
{
	TrackGameWorld(World);
}

// World 跟踪流程：
// 1. 过滤编辑器预览与空 World，避免设置改变编辑器音频或登记无法产生本地玩家的场景。
// 2. 已开始的 World 立刻恢复一次；尚未开始的 World 保存一次性 BeginPlay 句柄，等待 AudioDevice 和 Controller 可用。
// 3. 恢复状态由 RestoredWorlds 约束，重复 LoadSettings 或重复初始化通知都不会重复热切换音频输出设备。
void UCatGameUserSettings::TrackGameWorld(UWorld* World)
{
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	if (World->HasBegunPlay())
	{
		RegisterGameInstanceControllerRecovery(World->GetGameInstance());
		if (!RestoredWorlds.Contains(World))
		{
			RestoreRuntimePreferencesForWorld(World);
			RestoredWorlds.Add(World);
		}
		return;
	}

	if (!PendingWorldBeginPlayHandles.Contains(World))
	{
		PendingWorldBeginPlayHandles.Add(World,
			World->OnWorldBeginPlay.AddUObject(this, &UCatGameUserSettings::HandleTrackedWorldBeginPlay));
	}
}

// BeginPlay 处理流程：
// 1. 收集已开始与已失效的待处理 World，避免在遍历 TMap 时删除元素。
// 2. 为每个开始的 World 先绑定其 GameInstance 的 Controller 通知，再恢复一次持久化偏好。
// 3. 完成或失效后撤销相应 BeginPlay 句柄；未开始的其它 World 留在集合中等待自身事件。
void UCatGameUserSettings::HandleTrackedWorldBeginPlay()
{
	TArray<UWorld*> ReadyWorlds;
	TArray<TWeakObjectPtr<UWorld>> CompletedWorlds;
	for (const TPair<TWeakObjectPtr<UWorld>, FDelegateHandle>& PendingWorld : PendingWorldBeginPlayHandles)
	{
		UWorld* World = PendingWorld.Key.Get();
		if (!World)
		{
			CompletedWorlds.Add(PendingWorld.Key);
		}
		else if (World->HasBegunPlay())
		{
			ReadyWorlds.Add(World);
			CompletedWorlds.Add(PendingWorld.Key);
		}
	}

	for (UWorld* World : ReadyWorlds)
	{
		RegisterGameInstanceControllerRecovery(World->GetGameInstance());
		if (!RestoredWorlds.Contains(World))
		{
			RestoreRuntimePreferencesForWorld(World);
			RestoredWorlds.Add(World);
		}
	}

	for (const TWeakObjectPtr<UWorld>& WorldReference : CompletedWorlds)
	{
		if (FDelegateHandle* BeginPlayHandle = PendingWorldBeginPlayHandles.Find(WorldReference))
		{
			UWorld* World = WorldReference.Get();
			if (World)
			{
				World->OnWorldBeginPlay.Remove(*BeginPlayHandle);
			}
			PendingWorldBeginPlayHandles.Remove(WorldReference);
		}
	}
}

// World 清理流程：取消属于该 World 的输出恢复，再撤销其 BeginPlay 委托并移除恢复标记；其它 World 的请求不受影响，用户保存值不变。
void UCatGameUserSettings::HandleWorldCleanup(UWorld* World, const bool, const bool)
{
	if (!World)
	{
		return;
	}

	if (AudioOutputRestoreRequest && AudioOutputRestoreRequest->GetWorld() == World)
	{
		CancelAudioOutputRestore();
	}
	if (FDelegateHandle* BeginPlayHandle = PendingWorldBeginPlayHandles.Find(World))
	{
		World->OnWorldBeginPlay.Remove(*BeginPlayHandle);
		PendingWorldBeginPlayHandles.Remove(World);
	}
	RestoredWorlds.Remove(World);
}

// GameInstance 绑定流程：确认实例尚未绑定后登记动态 Pawn-Controller 通知；旅行复用同一实例时保留单一监听，设置对象销毁时由 BeginDestroy 对称解绑。
void UCatGameUserSettings::RegisterGameInstanceControllerRecovery(UGameInstance* GameInstance)
{
	if (!GameInstance || ControllerRecoveryGameInstances.Contains(GameInstance))
	{
		return;
	}

	GameInstance->GetOnPawnControllerChanged().AddDynamic(this, &UCatGameUserSettings::HandlePawnControllerChanged);
	ControllerRecoveryGameInstances.Add(GameInstance);
}

// Controller 变更流程：
// 1. 只接受本地 PlayerController 与游戏 World，忽略远端复制和编辑器预览通知。
// 2. 将 Controller 相关的震动与可用 OSS 语音重新提交，覆盖旅行后新建 Controller 的默认状态。
// 3. 分类音频和输出设备属于 World 恢复流程，避免每次 Pawn 重绑重复提交全局音频切换。
void UCatGameUserSettings::HandlePawnControllerChanged(APawn*, AController* Controller)
{
	APlayerController* PlayerController = Cast<APlayerController>(Controller);
	if (!PlayerController || !PlayerController->IsLocalController() || !PlayerController->GetWorld()
		|| !PlayerController->GetWorld()->IsGameWorld())
	{
		return;
	}

	RestoreRuntimePreferencesForController(PlayerController);
}

// World 恢复流程：
// 1. 将五类持久化音量提交给目标 World 的 AudioDevice；音频资产或设备缺失时保留保存值并记录失败状态。
// 2. 遍历该 GameInstance 的本地玩家，恢复每个已存在 Controller 的震动与可用网络语音。
// 3. 对已保存的非空设备 ID 建立一次有界活动确认请求，替换旧 World 的恢复请求；空值保留平台默认输出，恢复不重新保存配置。
void UCatGameUserSettings::RestoreRuntimePreferencesForWorld(UWorld* World)
{
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	const bool bAudioVolumesApplied = ApplyAudioVolumes(World, MasterVolume, MusicVolume, SFXVolume, AmbienceVolume, VoiceVolume);
	int32 RestoredControllerCount = 0;
	if (UGameInstance* GameInstance = World->GetGameInstance())
	{
		for (ULocalPlayer* LocalPlayer : GameInstance->GetLocalPlayers())
		{
			if (APlayerController* PlayerController = LocalPlayer ? LocalPlayer->GetPlayerController(World) : nullptr)
			{
				RestoreRuntimePreferencesForController(PlayerController);
				++RestoredControllerCount;
			}
		}
	}

	const bool bOutputDeviceSwapRequested = !AudioOutputDeviceId.IsEmpty();
	if (bOutputDeviceSwapRequested)
	{
		CancelAudioOutputRestore();
		AudioOutputRestoreRequest = NewObject<UCatAudioOutputRequest>(this);
		AudioOutputRestoreRequest->Start(World, AudioOutputDeviceId, UCatAudioOutputRequest::FOnCompleted::CreateUObject(
			this, &ThisClass::HandleRuntimeAudioOutputDeviceSwapCompleted));
		UE_LOG(LogCatUI, Log, TEXT("Event=settings_runtime_audio_output_swap_requested World=%s NetMode=%d"),
			*World->GetName(), static_cast<int32>(World->GetNetMode()));
	}

	UE_LOG(LogCatUI, Log,
		TEXT("Event=settings_runtime_preferences_restored World=%s NetMode=%d AudioVolumesApplied=%s Controllers=%d OutputDeviceSwapRequested=%s"),
		*World->GetName(), static_cast<int32>(World->GetNetMode()), bAudioVolumesApplied ? TEXT("true") : TEXT("false"),
		RestoredControllerCount, bOutputDeviceSwapRequested ? TEXT("true") : TEXT("false"));
}

// Controller 恢复流程：
// 1. 对本地 Controller 写入持久化的 ForceFeedback gate；控制器尚未就绪或远端 Controller 时不产生保存副作用。
// 2. 从 Controller 对应 LocalPlayer 取本地用户序号，仅当前 World 的 OSS Voice 支持时重新开始或停止网络语音。
// 3. 输出结构化日志说明两项恢复是否被实际接口接受，不把缺少 Voice provider 伪装成成功。
void UCatGameUserSettings::RestoreRuntimePreferencesForController(APlayerController* PlayerController)
{
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	UWorld* World = PlayerController->GetWorld();
	const bool bVibrationApplied = ApplyVibration(PlayerController, bVibrationEnabled);
	bool bVoiceChatApplied = false;
	if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
	{
		const int32 LocalUser = LocalPlayer->GetControllerId();
		if (LocalUser >= 0 && LocalUser <= MAX_uint8)
		{
			bVoiceChatApplied = ApplyVoiceChat(World, static_cast<uint8>(LocalUser), bVoiceChatEnabled);
		}
	}

	UE_LOG(LogCatUI, Log,
		TEXT("Event=settings_runtime_controller_preferences_restored Controller=%s World=%s VibrationApplied=%s VoiceChatApplied=%s"),
		*PlayerController->GetName(), World ? *World->GetName() : TEXT("None"), bVibrationApplied ? TEXT("true") : TEXT("false"),
		bVoiceChatApplied ? TEXT("true") : TEXT("false"));
}

// 输出恢复完成流程：只消费本宿主当前持有的单次请求，清除引用后记录实际活动确认或明确失败；旅行、取消后的旧结果被忽略，自动恢复不改写用户保存值。
void UCatGameUserSettings::HandleRuntimeAudioOutputDeviceSwapCompleted(UCatAudioOutputRequest* Request, const FName Error)
{
	if (!Request || AudioOutputRestoreRequest != Request)
	{
		return;
	}
	AudioOutputRestoreRequest = nullptr;
	if (Error.IsNone())
	{
		UE_LOG(LogCatUI, Log, TEXT("Event=settings_runtime_audio_output_confirmed Request=%s"), *GetNameSafe(Request));
	}
	else
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=settings_runtime_audio_output_failed Request=%s Error=%s"),
			*GetNameSafe(Request), *Error.ToString());
	}
}

// 音频路由解析流程：
// 1. 同步加载固定的正式软引用，使用户设置首次打开时能立即判断控件是否可用。
// 2. 分别验证 SoundMix、总线和四个内容分类，防止单一空类导致一部分音量静默失效。
// 3. 仅通过全部验证时把局部指针交给调用方，失败路径不保留半有效输出。
bool UCatGameUserSettings::ResolveAudioRouting(USoundMix*& OutSoundMix, USoundClass*& OutMasterClass,
	USoundClass*& OutMusicClass, USoundClass*& OutSFXClass, USoundClass*& OutAmbienceClass,
	USoundClass*& OutVoiceClass) const
{
	OutSoundMix = FrontendSoundMix.LoadSynchronous();
	OutMasterClass = MasterSoundClass.LoadSynchronous();
	OutMusicClass = MusicSoundClass.LoadSynchronous();
	OutSFXClass = SFXSoundClass.LoadSynchronous();
	OutAmbienceClass = AmbienceSoundClass.LoadSynchronous();
	OutVoiceClass = VoiceSoundClass.LoadSynchronous();
	return OutSoundMix && OutMasterClass && OutMusicClass && OutSFXClass && OutAmbienceClass && OutVoiceClass;
}
