#include "Online/Voice/CatVoiceInputDevice.h"

#include "Engine/World.h"
#include "Interfaces/VoiceCapture.h"
#include "Net/VoiceConfig.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "VoiceEngineImpl.h"
#include "VoiceInterfaceImpl.h"
#include "Runtime/Launch/Resources/Version.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <mmsystem.h>
#include <dsound.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogCatVoiceInput, Log, All);

namespace
{
	// UE 5.8 Steam 的 FOnlineVoiceSteam/FVoiceEngineSteam 分别继承这两个公开实现类。
	// 通过基类成员指针访问受保护扩展点；不把真实对象强转为下列辅助派生类，不使用私有头或内存偏移。
	// 升级引擎前必须重新核对 Steam 继承关系，因此其它版本保持不可用。
	struct FVoiceInterfaceAccess : FOnlineVoiceImpl
	{
		static auto Engine() { return &FVoiceInterfaceAccess::VoiceEngine; }
	};
	struct FVoiceEngineAccess : FVoiceEngineImpl
	{
		using FCaptureGetter = TSharedPtr<IVoiceCapture>& (FVoiceEngineImpl::*)();
		static FCaptureGetter Capture() { return &FVoiceEngineAccess::GetVoiceCapture; }
		static auto LocalData() { return &FVoiceEngineAccess::GetLocalPlayerVoiceData; }
	};

	struct FAppliedDevice
	{
		TWeakPtr<IVoiceCapture> Capture;
		FString Name;
	};
	TArray<FAppliedDevice> AppliedDevices;

#if PLATFORM_WINDOWS
	BOOL CALLBACK EnumerateCapture(GUID* Guid, const TCHAR* Description, const TCHAR*, void* Context)
	{
		// 空 GUID 是平台默认别名，不能当作稳定设备 ID。
		if (Guid && Description)
		{
			const FGuid Id(Guid->Data1, (uint32(Guid->Data2) << 16) | Guid->Data3,
				(uint32(Guid->Data4[0]) << 24) | (uint32(Guid->Data4[1]) << 16) | (uint32(Guid->Data4[2]) << 8) | Guid->Data4[3],
				(uint32(Guid->Data4[4]) << 24) | (uint32(Guid->Data4[5]) << 16) | (uint32(Guid->Data4[6]) << 8) | Guid->Data4[7]);
			static_cast<TArray<FCatVoiceInputDevice>*>(Context)->Add({ Id.ToString(EGuidFormats::DigitsWithHyphens), Description });
		}
		return 1;
	}
#endif
}

bool CatVoiceInput::IsSupported(const UWorld* World)
{
#if PLATFORM_WINDOWS && ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION == 8
	IOnlineSubsystem* Subsystem = World && World->GetNetMode() != NM_DedicatedServer ? Online::GetSubsystem(World) : nullptr;
	return Subsystem && Subsystem->GetSubsystemName() == FName(TEXT("STEAM")) && Subsystem->GetVoiceInterface().IsValid();
#else
	return false;
#endif
}

bool CatVoiceInput::Enumerate(TArray<FCatVoiceInputDevice>& OutDevices)
{
	check(IsInGameThread());
	OutDevices.Reset();
#if PLATFORM_WINDOWS
	if (FAILED(DirectSoundCaptureEnumerateW(&EnumerateCapture, &OutDevices)))
	{
		return false;
	}
	// UE 的 Windows Voice 按描述字符串寻址；拒绝同名设备，避免保存 A 的 ID 却实际打开 B。
	TMap<FString, int32> NameCounts;
	for (const FCatVoiceInputDevice& Device : OutDevices) { ++NameCounts.FindOrAdd(Device.Name); }
	OutDevices.RemoveAll([&NameCounts](const FCatVoiceInputDevice& Device) { return NameCounts[Device.Name] != 1; });
	OutDevices.Sort([](const FCatVoiceInputDevice& A, const FCatVoiceInputDevice& B) { return A.Name < B.Name; });
	if (!OutDevices.IsEmpty())
	{
		OutDevices.Insert({ FString(), TEXT("平台默认（启动时）") }, 0);
	}
	return !OutDevices.IsEmpty();
#else
	return false;
#endif
}

CatVoiceInput::ESwitchResult CatVoiceInput::ChangeCaptureDevice(IVoiceCapture& Capture,
	const FString& DeviceName, const FString& PreviousDeviceName)
{
	const int32 Rate = UVOIPStatics::GetVoiceSampleRate();
	const int32 Channels = UVOIPStatics::GetVoiceNumChannels();
	if (Capture.ChangeDevice(DeviceName, Rate, Channels)) { return ESwitchResult::Applied; }
	if (Capture.ChangeDevice(PreviousDeviceName, Rate, Channels)) { return ESwitchResult::Restored; }
	Capture.Stop();
	return ESwitchResult::Stopped;
}

FCatVoiceInputResult CatVoiceInput::Prepare(UWorld* World, const uint8 LocalUserNum, const FString& DeviceId,
	const bool bRequireCapture)
{
	check(IsInGameThread());
	UE_LOG(LogCatVoiceInput, Log, TEXT("Event=voice_input_request World=%s NetMode=%d LocalUser=%u Device=%s RequireCapture=%d"),
		*GetNameSafe(World), World ? int32(World->GetNetMode()) : -1, LocalUserNum, *DeviceId, bRequireCapture);
	FCatVoiceInputResult Result;
	if (!IsSupported(World)) { Result.Error = TEXT("UnsupportedProvider"); }
	else
	{
		TArray<FCatVoiceInputDevice> Devices;
		Enumerate(Devices);
		Result = Detail::PrepareVoice(static_cast<FOnlineVoiceImpl&>(*Online::GetSubsystem(World)->GetVoiceInterface()),
			LocalUserNum, DeviceId, Devices, bRequireCapture);
	}
	if (Result.bApplied)
	{
		UE_LOG(LogCatVoiceInput, Log, TEXT("Event=voice_input_applied World=%s NetMode=%d LocalUser=%u Device=%s Sending=false"),
			*GetNameSafe(World), World ? int32(World->GetNetMode()) : -1, LocalUserNum, *DeviceId);
	}
	else
	{
		UE_LOG(LogCatVoiceInput, Warning, TEXT("Event=voice_input_failed World=%s NetMode=%d LocalUser=%u Device=%s Reason=%s Sending=false"),
			*GetNameSafe(World), World ? int32(World->GetNetMode()) : -1, LocalUserNum, *DeviceId, *Result.Error.ToString());
	}
	return Result;
}

FCatVoiceInputResult CatVoiceInput::Detail::PrepareVoice(FOnlineVoiceImpl& Voice, const uint8 LocalUserNum,
	const FString& DeviceId, const TArray<FCatVoiceInputDevice>& Devices, const bool bRequireCapture)
{
	check(IsInGameThread());
	FCatVoiceInputResult Result;
	if (LocalUserNum >= Voice.GetNumLocalTalkers()) { Result.Error = TEXT("InvalidLocalUser"); return Result; }
	Voice.StopNetworkedVoice(LocalUserNum);
	Voice.ClearVoicePackets();
	const IVoiceEnginePtr Engine = Voice.*FVoiceInterfaceAccess::Engine();
	if (!Engine.IsValid()) { Result.Error = TEXT("EngineUnavailable"); return Result; }
	FVoiceEngineImpl& EngineImpl = static_cast<FVoiceEngineImpl&>(*Engine);
	TSharedPtr<IVoiceCapture>& Capture = (EngineImpl.*FVoiceEngineAccess::Capture())();
	// 仅禁用模式允许无采集器；常开/PTT 不能把默认设备缺失误报为准备成功。
	if (!bRequireCapture && DeviceId.IsEmpty() && !Capture) { Result.bApplied = true; return Result; }
	if (!Capture) { Result.Error = TEXT("CaptureUnavailable"); return Result; }
	const FCatVoiceInputDevice* Requested = Devices.FindByPredicate([&DeviceId](const FCatVoiceInputDevice& D) { return D.Id == DeviceId; });
	if (!Requested) { Result.Error = TEXT("DeviceMissingOrAmbiguous"); return Result; }
	const bool bRegistered = Voice.RegisterLocalTalker(LocalUserNum);
	// RegisterLocalTalker 自带启用副作用；成功、失败都立即撤销，同一调用栈内没有网络 Tick。
	Voice.StopNetworkedVoice(LocalUserNum);
	Voice.ClearVoicePackets();
	if (!bRegistered) { Result.Error = TEXT("RegistrationFailed"); return Result; }
	AppliedDevices.RemoveAll([](const FAppliedDevice& State) { return !State.Capture.IsValid(); });
	FAppliedDevice* State = AppliedDevices.FindByPredicate([&Capture](const FAppliedDevice& D) { return D.Capture.Pin() == Capture; });
	if (!State) { State = &AppliedDevices.AddDefaulted_GetRef(); State->Capture = Capture; }
	const FString RequestedName = DeviceId.IsEmpty() ? FString() : Requested->Name;
	const ESwitchResult Switched = ChangeCaptureDevice(*Capture, RequestedName, State->Name);
	// 保留采样计数，清空旧设备 PCM 编码余数，再让引擎结束 Stop 的尾部采集。
	FLocalVoiceData* LocalData = (EngineImpl.*FVoiceEngineAccess::LocalData())();
	LocalData[LocalUserNum].VoiceRemainderSize = 0;
	LocalData[LocalUserNum].VoiceRemainder.Reset();
	Engine->GetVoiceDataReadyFlags();
	if (Switched == ESwitchResult::Applied)
	{
		State->Name = RequestedName;
		Result.bApplied = true;
	}
	else
	{
		Result.Error = Switched == ESwitchResult::Restored ? TEXT("SwitchFailedRestored") : TEXT("SwitchAndRestoreFailed");
	}
	return Result;
}
