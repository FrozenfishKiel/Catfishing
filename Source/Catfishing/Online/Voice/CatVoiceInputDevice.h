#pragma once

#include "CoreMinimal.h"

class UWorld;
class IVoiceCapture;
class FOnlineVoiceImpl;

struct FCatVoiceInputDevice
{
	FString Id;
	FString Name;
};

/** 本地设备准备结果；无论成功或失败均保持停发，发送模式由本地子系统统一执行。 */
struct FCatVoiceInputResult
{
	bool bApplied = false;
	FName Error;
};

/** UE 5.8 Windows Steam 采集适配；不拥有 OSS 或采集器，不创建另一条语音链。 */
namespace CatVoiceInput
{
	bool IsSupported(const UWorld* World);
	bool Enumerate(TArray<FCatVoiceInputDevice>& OutDevices);
	/** 只读既有采集器的归一化包络；不支持或采集无效返回 -1，不开启录音。 */
	float GetAmplitude(const UWorld* World, uint8 LocalUserNum);
	FCatVoiceInputResult Prepare(UWorld* World, uint8 LocalUserNum, const FString& DeviceId,
		bool bRequireCapture);

	namespace Detail
	{
		/** 仅已验证的 UE 标准实现可以进入；由平台入口和使用真实 UE 引擎状态机的测试调用。 */
		FCatVoiceInputResult PrepareVoice(FOnlineVoiceImpl& Voice, uint8 LocalUserNum, const FString& DeviceId,
			const TArray<FCatVoiceInputDevice>& Devices, bool bRequireCapture);
	}
	/** ChangeDevice 会先销毁旧缓冲；失败后必须显式重建旧设备。 */
	enum class ESwitchResult : uint8 { Applied, Restored, Stopped };
	ESwitchResult ChangeCaptureDevice(IVoiceCapture& Capture, const FString& DeviceName,
		const FString& PreviousDeviceName);
}
