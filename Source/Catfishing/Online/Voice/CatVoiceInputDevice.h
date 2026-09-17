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

/** 本地设备事务结果；失败且无法恢复旧设备时停止发送，不回退到另一支麦克风。 */
struct FCatVoiceInputResult
{
	bool bApplied = false;
	bool bSending = false;
	FName Error;
};

/** UE 5.8 Windows Steam 采集适配；不拥有 OSS 或采集器，不创建另一条语音链。 */
namespace CatVoiceInput
{
	bool IsSupported(const UWorld* World);
	bool Enumerate(TArray<FCatVoiceInputDevice>& OutDevices);
	FCatVoiceInputResult Apply(UWorld* World, uint8 LocalUserNum, const FString& DeviceId,
		bool bEnable, bool bPreviouslyEnabled);

	namespace Detail
	{
		/** 仅已验证的 UE 标准实现可以进入；由平台入口和使用真实 UE 引擎状态机的测试调用。 */
		FCatVoiceInputResult ApplyToVoice(FOnlineVoiceImpl& Voice, uint8 LocalUserNum, const FString& DeviceId,
			const TArray<FCatVoiceInputDevice>& Devices, bool bEnable, bool bPreviouslyEnabled);
	}
	/** ChangeDevice 会先销毁旧缓冲；失败后必须显式重建旧设备。 */
	enum class ESwitchResult : uint8 { Applied, Restored, Stopped };
	ESwitchResult ChangeCaptureDevice(IVoiceCapture& Capture, const FString& DeviceName,
		const FString& PreviousDeviceName);
}
