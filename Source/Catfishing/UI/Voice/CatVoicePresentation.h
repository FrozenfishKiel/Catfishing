#pragma once
#include "CoreMinimal.h"
#include "Online/Voice/CatVoiceTransmitSubsystem.h"
class ULocalPlayer;
class UCatVoiceSettings;

/** UI 只读状态：保存模式与当前发送条件分别表达，不写设置、不注册 talker。 */
namespace CatVoicePresentation
{
	FText ModeText(ECatVoiceInputMode Mode);
	FText StatusText(ECatVoiceInputMode Mode, bool bServiceAvailable, bool bHasPawn, bool bReady, bool bContextAllowed, bool bSending);
	void ReadLocalStatus(ULocalPlayer* Player, FText& OutMode, FText& OutStatus);
}

/** 振幅阈值加启停延时；仅 UI 状态，不复制。 */
struct FCatVoiceActivityEnvelope
{
	float AboveSeconds = 0;
	float QuietSeconds = 0;
	float Level = 0;
	bool bActive = false;
	void Update(float Amplitude, bool bEligible, float DeltaSeconds, const UCatVoiceSettings& Settings);
};
