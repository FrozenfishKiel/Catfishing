#include "UI/Voice/CatVoicePresentation.h"
#include "Online/Voice/CatVoiceSettings.h"
#include "Settings/CatGameUserSettings.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

FText CatVoicePresentation::ModeText(const ECatVoiceInputMode Mode)
{
	return FText::FromString(Mode == ECatVoiceInputMode::AlwaysOn ? TEXT("常开")
		: Mode == ECatVoiceInputMode::PushToTalk ? TEXT("按住 V 说话") : TEXT("禁用（仍可听到队友）"));
}

FText CatVoicePresentation::StatusText(const ECatVoiceInputMode Mode, const bool bServiceAvailable,
	const bool bHasPawn, const bool bReady, const bool bContextAllowed, const bool bSending)
{
	const TCHAR* Value = !bServiceAvailable ? TEXT("语音服务未就绪")
		: !bHasPawn ? TEXT("距离语音 · 进入游戏后生效")
		: Mode == ECatVoiceInputMode::Disabled ? TEXT("自己的麦克风已禁用")
		: !bReady ? TEXT("麦克风未就绪，请检查设置")
		: !bContextAllowed ? TEXT("当前暂停发送")
		: !bSending && Mode == ECatVoiceInputMode::PushToTalk ? TEXT("等待按住 V")
		: bSending ? TEXT("麦克风可发送 · 有声音时显示提示") : TEXT("当前未发送");
	return FText::FromString(Value);
}

void CatVoicePresentation::ReadLocalStatus(ULocalPlayer* Player, FText& OutMode, FText& OutStatus)
{
	const UCatGameUserSettings* Settings = UCatGameUserSettings::Get();
	UWorld* World = Player ? Player->GetWorld() : nullptr;
	const auto* Controller = Player && World ? Player->GetPlayerController(World) : nullptr;
	const auto* Transmit = Player ? Player->GetSubsystem<UCatVoiceTransmitSubsystem>() : nullptr;
	const auto Mode = Settings ? Settings->GetVoiceInputMode() : ECatVoiceInputMode::Disabled;
	OutMode = ModeText(Mode);
	OutStatus = StatusText(Mode, Settings && Settings->HasVoiceChatSupport(World), Controller && Controller->GetPawn(),
		Transmit && Transmit->IsReady(), Transmit && Transmit->IsInputContextAllowed(), Transmit && Transmit->IsSending());
}

void FCatVoiceActivityEnvelope::Update(const float Amplitude, const bool bEligible, const float DeltaSeconds, const UCatVoiceSettings& Settings)
{
	if (!bEligible || !FMath::IsFinite(Amplitude) || !FMath::IsFinite(DeltaSeconds)) { *this = {}; return; }
	const float Delta = FMath::Clamp(DeltaSeconds, 0.0f, 0.1f);
	Level = FMath::Clamp(Amplitude, 0.0f, 1.0f);
	if (Level >= FMath::Clamp(Settings.ActivityThreshold, 0.001f, 1.0f))
	{
		QuietSeconds = 0;
		AboveSeconds += Delta;
		bActive |= AboveSeconds >= FMath::Clamp(Settings.ActivityAttackSeconds, 0.0f, 1.0f);
	}
	else
	{
		AboveSeconds = 0;
		QuietSeconds += Delta;
		if (QuietSeconds >= FMath::Clamp(Settings.ActivityReleaseSeconds, 0.0f, 2.0f)) { bActive = false; }
	}
}
