#include "Settings/CatAudioOutputRequest.h"

#include "AudioMixerDevice.h"
#include "AudioThread.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Logging/CatLog.h"

// 启动流程：只允许未启动过的对象接管一次操作，避免复用时失效原生回调误认新请求；固定 World、设备和目标并登记八秒限额，实际操作从下一次 Tick 开始，调用方先保留请求身份。
void UCatAudioOutputRequest::Start(UWorld* World, const FString& DeviceId, FOnCompleted InCompleted)
{
	if (DeadlineSeconds != 0.0)
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=audio_output_request_reuse_rejected Request=%s"), *GetName());
		return;
	}
	RequestWorld = World;
	AudioDevice = World ? World->GetAudioDevice() : FAudioDeviceHandle();
	RequestedDeviceId = DeviceId;
	Devices.Reset();
	Completed = MoveTemp(InCompleted);
	bSubmitted = false;
	bSwapAccepted = false;
	bQueryPending = false;
	bFinished = false;
	DeadlineSeconds = FPlatformTime::Seconds() + 8.0;
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &ThisClass::TickRequest), 0.1f);
	UE_LOG(LogCatUI, Log, TEXT("Event=audio_output_request_started Request=%s World=%s Operation=%s TimeoutSeconds=8"),
		*GetName(), *GetNameSafe(World), DeviceId.IsEmpty() ? TEXT("Enumerate") : TEXT("Swap"));
}

// 取消流程：先封闭晚到结果，再移除轮询和完成委托并释放设备；音频线程已经取得的句柄副本自行活到命令结束，但不会再改写调用方状态。
void UCatAudioOutputRequest::Cancel()
{
	bFinished = true;
	if (TickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	Completed.Unbind();
	AudioDevice.Reset();
	RequestWorld.Reset();
}

// 销毁流程：撤销本对象所有结果接收和轮询，再交给 UObject 回收；不保存偏好或尝试回滚平台设备。
void UCatAudioOutputRequest::BeginDestroy()
{
	Cancel();
	Super::BeginDestroy();
}

// World 读取流程：只解析创建请求时的弱 World；旅行清理后返回空，调用方不会从任意全局地图获取替代上下文。
UWorld* UCatAudioOutputRequest::GetWorld() const
{
	return RequestWorld.Get();
}

// 目标读取流程：返回 Start 固定的 ID；页面后续草稿改变不会改写在途请求的比较基准。
const FString& UCatAudioOutputRequest::GetRequestedDeviceId() const
{
	return RequestedDeviceId;
}

// 设备读取流程：返回本请求已取得的设备列表；调用方只在成功完成通知后使用当前设备标志。
const TArray<FAudioOutputDeviceInfo>& UCatAudioOutputRequest::GetDevices() const
{
	return Devices;
}

// 轮询流程：
// 1. 检查真实时间截止值及 World 是否仍使用创建请求时的设备；失效时结束，不能给新设备接受失效查询结果。
// 2. 枚举只提交一次，平台查询在音频线程执行；通过设备强句柄而非跨线程裸 World 指针保证寿命。
// 3. 切换先查询当前设备，受理后持续查询至目标活动；每次最多一个查询，不反复提交切换命令。
bool UCatAudioOutputRequest::TickRequest(float)
{
	UWorld* World = RequestWorld.Get();
	FName Error;
	if (FPlatformTime::Seconds() >= DeadlineSeconds)
	{
		Error = TEXT("Timeout");
	}
	else if (!World || !AudioDevice || !(World->GetAudioDevice() == AudioDevice))
	{
		Error = TEXT("WorldOrDeviceChanged");
	}
	if (bFinished || !Error.IsNone())
	{
		TickerHandle.Reset(); // 当前 Tick 返回 false 即移除，无需再走显式撤销路径。
		if (!bFinished)
		{
			Finish(Error);
		}
		return false;
	}

	if (RequestedDeviceId.IsEmpty())
	{
		if (!bSubmitted)
		{
			bSubmitted = true;
			const FAudioDeviceHandle Device = AudioDevice;
			const TWeakObjectPtr<UCatAudioOutputRequest> WeakThis(this);
			FAudioThread::RunCommandOnAudioThread([Device, WeakThis]()
			{
				TArray<FAudioOutputDeviceInfo> Result;
				Audio::FMixerDevice* Mixer = static_cast<Audio::FMixerDevice*>(Device.GetAudioDevice());
				Audio::IAudioMixerPlatformInterface* Platform = Mixer ? Mixer->GetAudioMixerPlatform() : nullptr;
				uint32 Count = 0;
				if (Platform && Platform->GetNumOutputDevices(Count))
				{
					for (uint32 Index = 0; Index < Count; ++Index)
					{
						Audio::FAudioPlatformDeviceInfo Info;
						if (Platform->GetOutputDeviceInfo(Index, Info))
						{
							Result.Emplace(Info);
						}
					}
				}
				FAudioThread::RunCommandOnGameThread([WeakThis, Result = MoveTemp(Result)]()
				{
					if (UCatAudioOutputRequest* Request = WeakThis.Get())
					{
						Request->HandleDevicesObtained(Result);
					}
				});
			});
		}
	}
	else if (!bQueryPending && (!bSubmitted || bSwapAccepted))
	{
		QueryActiveDevice();
	}
	return true;
}

// 枚举返回流程：已结束则忽略；保存真实设备列表，空列表明确失败，非空列表再查询活动 ID，修正引擎缓存枚举路径未设置 bIsCurrentDevice 的情况。
void UCatAudioOutputRequest::HandleDevicesObtained(const TArray<FAudioOutputDeviceInfo>& AvailableDevices)
{
	if (bFinished)
	{
		return;
	}
	Devices = AvailableDevices;
	if (Devices.IsEmpty())
	{
		Finish(TEXT("NoOutputDevices"));
		return;
	}
	QueryActiveDevice();
}

// 受理返回流程：忽略已结束请求并验证目标；拒绝立即通知失败，受理仅放开后续活动查询，不清除调用方 pending 或保存设备 ID。
void UCatAudioOutputRequest::HandleSwapAccepted(const FSwapAudioOutputResult& Result)
{
	if (bFinished)
	{
		return;
	}
	if (Result.RequestedDeviceId != RequestedDeviceId || Result.Result != ESwapAudioOutputDeviceResultState::Success)
	{
		Finish(TEXT("SwapRejected"));
		return;
	}
	bSwapAccepted = true;
	UE_LOG(LogCatUI, Log, TEXT("Event=audio_output_swap_accepted Request=%s ConfirmationPending=true"), *GetName());
}

// 活动查询流程：在游戏线程占用单次查询槽，复制设备强句柄到音频线程，读取 Mixer 完成设备重建后才更新的 PlatformInfo；结果返回游戏线程后才访问请求 UObject。
void UCatAudioOutputRequest::QueryActiveDevice()
{
	if (bFinished || bQueryPending)
	{
		return;
	}
	bQueryPending = true;
	const FAudioDeviceHandle Device = AudioDevice;
	const TWeakObjectPtr<UCatAudioOutputRequest> WeakThis(this);
	FAudioThread::RunCommandOnAudioThread([Device, WeakThis]()
	{
		const Audio::FMixerDevice* Mixer = static_cast<Audio::FMixerDevice*>(Device.GetAudioDevice());
		const FString ActiveId = Mixer ? Mixer->GetPlatformDeviceInfo().DeviceId : FString();
		FAudioThread::RunCommandOnGameThread([WeakThis, ActiveId]()
		{
			if (UCatAudioOutputRequest* Request = WeakThis.Get())
			{
				Request->HandleActiveDevice(ActiveId);
			}
		});
	});
}

// 活动结果流程：
// 1. 重新检查 World、截止时间与结束状态，防止音频线程慢回执跨过 timeout 或旅行边界。
// 2. 枚举结果按实际活动 ID 修正；切换目标已活动时完成，目标不同且尚未提交时才发起一次 RequestDeviceSwap。
// 3. 平台请求同样用强设备句柄在音频线程调用，受理回执不代表完成；后续轮询等到 Mixer 活动 ID 真正改变或超时。
void UCatAudioOutputRequest::HandleActiveDevice(const FString& ActiveDeviceId)
{
	if (bFinished)
	{
		return;
	}
	bQueryPending = false;
	UWorld* World = RequestWorld.Get();
	if (FPlatformTime::Seconds() >= DeadlineSeconds)
	{
		Finish(TEXT("Timeout"));
		return;
	}
	if (!World || !AudioDevice || !(World->GetAudioDevice() == AudioDevice))
	{
		Finish(TEXT("WorldOrDeviceChanged"));
		return;
	}
	if (RequestedDeviceId.IsEmpty())
	{
		for (FAudioOutputDeviceInfo& Device : Devices)
		{
			Device.bIsCurrentDevice = Device.DeviceId == ActiveDeviceId;
		}
		Finish(NAME_None);
		return;
	}
	if (ActiveDeviceId == RequestedDeviceId)
	{
		Finish(NAME_None);
		return;
	}
	if (!bSubmitted)
	{
		bSubmitted = true;
		const FAudioDeviceHandle Device = AudioDevice;
		const FString TargetId = RequestedDeviceId;
		const TWeakObjectPtr<UCatAudioOutputRequest> WeakThis(this);
		FAudioThread::RunCommandOnAudioThread([Device, TargetId, WeakThis]()
		{
			Audio::FMixerDevice* Mixer = static_cast<Audio::FMixerDevice*>(Device.GetAudioDevice());
			Audio::IAudioMixerPlatformInterface* Platform = Mixer ? Mixer->GetAudioMixerPlatform() : nullptr;
			FSwapAudioOutputResult Result;
			Result.RequestedDeviceId = TargetId;
			const bool bAccepted = Platform && Platform->RequestDeviceSwap(TargetId, false, TEXT("CatGameUserSettings"));
			Result.Result = bAccepted ? ESwapAudioOutputDeviceResultState::Success : ESwapAudioOutputDeviceResultState::Failure;
			FAudioThread::RunCommandOnGameThread([WeakThis, Result]()
			{
				if (UCatAudioOutputRequest* Request = WeakThis.Get())
				{
					Request->HandleSwapAccepted(Result);
				}
			});
		});
	}
}

// 完成流程：先一次性取走通知并 Cancel 全部后续接收，再输出与请求名关联的最终结果；通知只携带真实活动确认或明确错误，不把平台受理包装成成功。
void UCatAudioOutputRequest::Finish(const FName Error)
{
	if (bFinished)
	{
		return;
	}
	FOnCompleted Completion = MoveTemp(Completed);
	Cancel();
	if (Error.IsNone())
	{
		UE_LOG(LogCatUI, Log, TEXT("Event=audio_output_request_confirmed Request=%s Operation=%s"),
			*GetName(), RequestedDeviceId.IsEmpty() ? TEXT("Enumerate") : TEXT("Swap"));
	}
	else
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=audio_output_request_failed Request=%s Error=%s"), *GetName(), *Error.ToString());
	}
	Completion.ExecuteIfBound(this, Error);
}
