#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Online/Voice/CatVoiceInputDevice.h"
#include "Interfaces/VoiceCapture.h"
#include "VoiceEngineImpl.h"
#include "VoiceInterfaceImpl.h"
#include "VoiceModule.h"
#include "Settings/CatGameUserSettings.h"
#include "UObject/UnrealType.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Tests/AutomationCommon.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Components/ComboBoxString.h"
#include "Components/TextBlock.h"
#include "UI/Frontend/CatFrontendSettingsModel.h"
#include "UI/Frontend/CatFrontendRootWidget.h"
#include "UI/Save/CatLakeMainMenuWidget.h"

namespace
{
	// 只替换物理驱动；注册、发送 gate、Stop 尾采集、清包和重新启动均使用正式 UE 实现。
	class FCaptureDeviceDouble : public IVoiceCapture
	{
	public:
		FString ActiveName;
		TSet<FString> FailedDevices;
		TSet<FString> FailedStarts;
		EVoiceCaptureState::Type State = EVoiceCaptureState::NotCapturing;
		int32 ChangeCount = 0;
		bool Init(const FString&, int32, int32) override { return true; }
		void Shutdown() override { State = EVoiceCaptureState::UnInitialized; }
		bool Start() override
		{
			if (FailedStarts.Contains(ActiveName)) { return false; }
			State = EVoiceCaptureState::Ok;
			return true;
		}
		void Stop() override { if (State == EVoiceCaptureState::Ok) { State = EVoiceCaptureState::Stopping; } }
		bool ChangeDevice(const FString& Name, int32, int32) override
		{
			++ChangeCount;
			ActiveName.Reset();
			State = EVoiceCaptureState::NotCapturing;
			if (FailedDevices.Contains(Name)) { return false; }
			ActiveName = Name;
			return true;
		}
		bool IsCapturing() override { return State == EVoiceCaptureState::Ok; }
		EVoiceCaptureState::Type GetCaptureState(uint32& Bytes) const override { Bytes = 0; return State; }
		int32 GetBufferSize() const override { return 32000; }
		void DumpState() const override {}
	};
	class FTestVoiceEngine : public FVoiceEngineImpl
	{
	public:
		FTestVoiceEngine(const TSharedPtr<IVoiceCapture>& Capture) : FVoiceEngineImpl(nullptr) { GetVoiceCapture() = Capture; }
		void SeedRemainder() { GetLocalPlayerVoiceData()[0].VoiceRemainderSize = 4; }
		uint32 Remainder() { return GetLocalPlayerVoiceData()[0].VoiceRemainderSize; }
	};
	class FTestOnlineVoice : public FOnlineVoiceImpl
	{
	public:
		FTestOnlineVoice(const IVoiceEnginePtr& Engine) : FOnlineVoiceImpl(nullptr)
		{
			VoiceEngine = Engine;
			MaxLocalTalkers = 1;
			MaxRemoteTalkers = 0;
			SessionInt = nullptr;
			IdentityInt = nullptr;
			LocalTalkers.Init(FLocalTalker(), 1);
		}
		bool Sending() const { return LocalTalkers[0].bHasNetworkedVoice; }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatVoiceInputTransactionTest, "Catfishing.Online.Voice.InputTransaction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatVoiceInputTransactionTest::RunTest(const FString& Parameters)
{
	using CatVoiceInput::Detail::ApplyToVoice;
	TSharedPtr<FCaptureDeviceDouble> Capture = MakeShared<FCaptureDeviceDouble>();
	TSharedPtr<FTestVoiceEngine, ESPMode::ThreadSafe> Engine = MakeShared<FTestVoiceEngine, ESPMode::ThreadSafe>(Capture);
	FTestOnlineVoice Voice(Engine);
	const TArray<FCatVoiceInputDevice> Devices = { {TEXT(""), TEXT("Default")}, {TEXT("a"), TEXT("Mic A")}, {TEXT("b"), TEXT("Mic B")} };
	Engine->SeedRemainder();
	auto Result = ApplyToVoice(Voice, 0, TEXT("a"), Devices, false, false);
	TestTrue(TEXT("Select actual capture while voice is off"), Result.bApplied);
	TestEqual(TEXT("Active physical device"), Capture->ActiveName, FString(TEXT("Mic A")));
	TestFalse(TEXT("Selection does not enable network sending"), Voice.Sending());
	TestFalse(TEXT("Selection leaves capture stopped"), Capture->IsCapturing());
	TestEqual(TEXT("Old PCM remainder discarded"), Engine->Remainder(), uint32(0));
	Result = ApplyToVoice(Voice, 0, TEXT("a"), Devices, true, false);
	TestTrue(TEXT("Explicit enable starts real UE engine state machine"), Result.bApplied && Result.bSending && Voice.Sending() && Capture->IsCapturing());
	Result = ApplyToVoice(Voice, 0, TEXT("b"), Devices, true, true);
	TestTrue(TEXT("Switch during continuous speech resumes new input"), Result.bApplied && Result.bSending);
	TestEqual(TEXT("New active device"), Capture->ActiveName, FString(TEXT("Mic B")));
	Capture->FailedDevices.Add(TEXT("Mic A"));
	Result = ApplyToVoice(Voice, 0, TEXT("a"), Devices, true, true);
	TestFalse(TEXT("Failed switch cannot report success"), Result.bApplied);
	TestTrue(TEXT("Previous enabled input resumes after rollback"), Result.bSending && Voice.Sending());
	TestEqual(TEXT("Rollback restored actual previous input"), Capture->ActiveName, FString(TEXT("Mic B")));
	Capture->FailedDevices.Add(TEXT("Mic B"));
	Result = ApplyToVoice(Voice, 0, TEXT("a"), Devices, true, true);
	TestFalse(TEXT("Double failure stops sending"), Result.bSending || Voice.Sending() || Capture->IsCapturing());
	Capture->FailedDevices.Reset();
	Result = ApplyToVoice(Voice, 0, TEXT("a"), Devices, true, false);
	TestTrue(TEXT("Recover from failed device without restarting engine"), Result.bApplied && Result.bSending);
	Capture->FailedStarts.Add(TEXT("Mic B"));
	AddExpectedError(TEXT("Failed to start voice recording"), EAutomationExpectedErrorFlags::Contains, 1);
	Result = ApplyToVoice(Voice, 0, TEXT("b"), Devices, true, true);
	TestFalse(TEXT("Successful open with failed recording is not saved"), Result.bApplied);
	TestTrue(TEXT("Start failure restores previous recording"), Result.bSending);
	TestEqual(TEXT("Previous device restored after start failure"), Capture->ActiveName, FString(TEXT("Mic A")));
	const int32 BeforeMissing = Capture->ChangeCount;
	Result = ApplyToVoice(Voice, 0, TEXT("missing"), Devices, true, false);
	TestFalse(TEXT("Startup restore of missing saved device never sends default"), Result.bApplied || Result.bSending || Voice.Sending());
	TestEqual(TEXT("Missing selection never opens another device"), Capture->ChangeCount, BeforeMissing);
	Result = ApplyToVoice(Voice, 0, TEXT(""), Devices, false, false);
	TestTrue(TEXT("Reset to platform default applies"), Result.bApplied);
	TestTrue(TEXT("Default passes empty device name"), Capture->ActiveName.IsEmpty());
	TestFalse(TEXT("Reset stays muted"), Voice.Sending() || Capture->IsCapturing());
	Result = ApplyToVoice(Voice, 1, TEXT("a"), Devices, true, false);
	TestEqual(TEXT("Wrong local user rejected"), Result.Error, FName(TEXT("InvalidLocalUser")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatVoiceInputPreferencesTest, "Catfishing.Online.Voice.InputPreferences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatVoiceInputPreferencesTest::RunTest(const FString& Parameters)
{
	UCatGameUserSettings* Settings = NewObject<UCatGameUserSettings>();
	FStrProperty* Device = FindFProperty<FStrProperty>(Settings->GetClass(), TEXT("AudioInputDeviceId"));
	TestTrue(TEXT("Input device participates in per-user config persistence"), Device && Device->HasAnyPropertyFlags(CPF_Config));
	if (!Device) { return false; }
	Device->SetPropertyValue_InContainer(Settings, TEXT("previous-input"));
	const FString ConfigPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Tests/MicrophonePreferences.ini"));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(ConfigPath), true);
	Settings->SaveConfig(CPF_Config, *ConfigPath);
	UCatGameUserSettings* Reloaded = NewObject<UCatGameUserSettings>();
	Reloaded->LoadConfig(UCatGameUserSettings::StaticClass(), *ConfigPath);
	TestEqual(TEXT("Device ID survives config save and reload"), Reloaded->GetAudioInputDeviceId(), FString(TEXT("previous-input")));
	TArray<FCatVoiceInputDevice> HardwareDevices;
	CatVoiceInput::Enumerate(HardwareDevices);
	AddInfo(FString::Printf(TEXT("Physical enumeration returned %d selectable entries; no microphone was recorded"), HardwareDevices.Num()));
	Settings->SetNonVoiceSettingsToDefaults();
	TestEqual(TEXT("Failed default application can preserve saved device"), Settings->GetAudioInputDeviceId(), FString(TEXT("previous-input")));
	AddExpectedError(TEXT("settings_voice_apply_rejected"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("No world cannot commit a new microphone"), Settings->ApplyVoicePreferences(nullptr, 0, TEXT("new-input"), false));
	TestEqual(TEXT("Unapplied selection is never persisted"), Settings->GetAudioInputDeviceId(), FString(TEXT("previous-input")));
	Settings->SetToDefaults();
	TestTrue(TEXT("Clean default has no device override"), Settings->GetAudioInputDeviceId().IsEmpty());
	TestFalse(TEXT("Clean default voice remains disabled"), Settings->IsVoiceChatEnabled());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatVoiceInputBuffersTest, "Catfishing.Online.Voice.InputDeviceBuffers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatVoiceInputBuffersTest::RunTest(const FString& Parameters)
{
	TArray<FCatVoiceInputDevice> Devices;
	if (!CatVoiceInput::Enumerate(Devices))
	{
		AddWarning(TEXT("No capture devices: physical buffer selection is unverified on this machine"));
		return true;
	}
	TSharedPtr<IVoiceCapture> Capture = FVoiceModule::Get().CreateVoiceCapture(FString());
	if (!TestTrue(TEXT("Actual Windows Voice capture initializes"), Capture.IsValid())) { return false; }
	for (const FCatVoiceInputDevice& Device : Devices)
	{
		const auto Result = CatVoiceInput::ChangeCaptureDevice(*Capture, Device.Id.IsEmpty() ? FString() : Device.Name, FString());
		TestTrue(TEXT("Native device name resolves in the actual UE capture cache"), Result == CatVoiceInput::ESwitchResult::Applied);
		TestTrue(TEXT("Real capture buffer was created"), Capture->GetBufferSize() > 0);
		TestFalse(TEXT("Buffer selection alone does not record audio"), Capture->IsCapturing());
	}
	AddInfo(FString::Printf(TEXT("Verified %d real device/default buffer selections without recording or sending audio"), Devices.Num()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatMicrophoneSettingsInteractionTest, "Catfishing.Online.Voice.InputSettingsWidgets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatMicrophoneSettingsInteractionTest::RunTest(const FString& Parameters)
{
	const auto Fixture = MakeShared<FTestWorldWrapper>();
	if (!Fixture->CreateTestWorld(EWorldType::Game)) { return false; }
	UWorld* World = Fixture->GetTestWorld();
	World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	if (!Fixture->BeginPlayInTestWorld()) { return false; }
	UGameInstance* Game = World->GetGameInstance();
	ULocalPlayer* Local = NewObject<ULocalPlayer>(GEngine);
	Game->AddLocalPlayer(Local, FPlatformUserId::CreateFromInternalId(0));
	UGameViewportClient* Viewport = NewObject<UGameViewportClient>(GEngine);
	Viewport->Init(*GEngine->GetWorldContextFromWorld(World), Game, false);
	Local->ViewportClient = Viewport;
	APlayerController* Player = World->SpawnActor<APlayerController>();
	Player->SetPlayer(Local);
	const auto SettingsClass = LoadClass<UUserWidget>(nullptr, TEXT("/Game/UI/Frontend/WBP_CatFrontendSettings.WBP_CatFrontendSettings_C"));
	UUserWidget* Probe = SettingsClass ? CreateWidget<UUserWidget>(Player, SettingsClass) : nullptr;
	// 沿用已有前端交互测试的明确资产缺口白名单；不把其它设置的既有缺口归入麦克风功能。
	for (const TCHAR* Control : { TEXT("AccessibilitySettingsCategoryButton"), TEXT("TextSizeSlider"), TEXT("HighContrastCheckBox"),
		TEXT("ColorBlindModeComboBox"), TEXT("ReduceCameraShakeCheckBox"), TEXT("ReduceFlashingEffectsCheckBox"),
		TEXT("MouseSensitivitySlider"), TEXT("CameraSensitivitySlider"), TEXT("InvertYAxisCheckBox") })
	{
		if (Probe && !Probe->GetWidgetFromName(Control))
		{
			AddExpectedError(FString::Printf(TEXT("Event=frontend_widget_contract_missing Page=FrontendSettingsPage Control=%s Reason=missing_or_wrong_type"), Control),
				EAutomationExpectedErrorFlags::Contains, 0);
			AddInfo(FString::Printf(TEXT("Existing unrelated settings asset gap: %s"), Control));
		}
	}
	const auto RootClass = LoadClass<UCatFrontendRootWidget>(nullptr, TEXT("/Game/UI/Frontend/WBP_CatFrontendRoot.WBP_CatFrontendRoot_C"));
	const auto LakeClass = LoadClass<UCatLakeMainMenuWidget>(nullptr, TEXT("/Game/UI/Save/WBP_CatLakeMainMenu.WBP_CatLakeMainMenu_C"));
	UCatFrontendRootWidget* Root = RootClass ? CreateWidget<UCatFrontendRootWidget>(Player, RootClass) : nullptr;
	UCatLakeMainMenuWidget* Lake = LakeClass ? CreateWidget<UCatLakeMainMenuWidget>(Player, LakeClass) : nullptr;
	if (!TestNotNull(TEXT("Formal frontend root"), Root) || !TestNotNull(TEXT("Formal lake menu"), Lake)) { return false; }
	UCatFrontendSettingsModel* Model = NewObject<UCatFrontendSettingsModel>(Game);
	Model->BoundLocalPlayer = Local;
	Model->UserSettings = NewObject<UCatGameUserSettings>(Model);
	Model->UserSettings->SetToDefaults();
	FindFProperty<FStrProperty>(Model->UserSettings->GetClass(), TEXT("AudioInputDeviceId"))->SetPropertyValue_InContainer(Model->UserSettings, TEXT("a"));
	Model->ReloadDraftFromSettings();
	// 受控枚举结果只驱动正式 Model/WBP 草稿交互；不伪装真实 Steam 或物理采集验证。
	Model->Microphones = { {TEXT(""), TEXT("Default")}, {TEXT("a"), TEXT("Mic A")}, {TEXT("b"), TEXT("Mic B")} };
	Root->InitializeFrontend(nullptr, nullptr, nullptr, Model);
	const TSharedRef<SWidget> LakeSlate = Lake->TakeWidget(); // 保持 Slate 存活，否则临时引用析构会立即触发 NativeDestruct。
	Lake->InitializeLakeMenuSettings(Model);
	UUserWidget* SettingsPage = Cast<UUserWidget>(Root->GetWidgetFromName(TEXT("FrontendSettingsPage")));
	TestTrue(TEXT("Formal root consumes the existing settings WBP"), SettingsPage && SettingsPage->GetClass() == SettingsClass);
	for (UUserWidget* View : { SettingsPage, static_cast<UUserWidget*>(Lake) })
	{
		AddInfo(FString::Printf(TEXT("Checking formal consumer: %s"), *GetNameSafe(View)));
		UComboBoxString* Combo = View ? Cast<UComboBoxString>(View->GetWidgetFromName(TEXT("MicrophoneComboBox"))) : nullptr;
		if (!TestNotNull(TEXT("Formal microphone control"), Combo)) { continue; }
		TestTrue(TEXT("Real device list enables existing control"), Combo->GetIsEnabled());
		TestEqual(TEXT("Saved input is selected"), Combo->GetSelectedOption(), FString(TEXT("Mic A")));
		TestTrue(TEXT("Live Slate consumer retains its selection delegate"), Combo->OnSelectionChanged.IsBound());
		Combo->OnSelectionChanged.Broadcast(TEXT("Mic B"), ESelectInfo::OnMouseClick);
		TestEqual(TEXT("Widget event reaches shared draft model"), Model->GetDraftAudioInputDeviceId(), FString(TEXT("b")));
		TestEqual(TEXT("Selecting does not persist before Apply"), Model->UserSettings->GetAudioInputDeviceId(), FString(TEXT("a")));
		TestFalse(TEXT("Selecting never enables voice preference"), Model->GetDraftVoiceChatEnabled());
		TestTrue(TEXT("Input selection participates in pending changes"), Model->HasPendingChanges());
		Model->Cancel();
		TestEqual(TEXT("Cancel redraws saved microphone"), Combo->GetSelectedOption(), FString(TEXT("Mic A")));
		Model->RestoreDefaults();
		TestTrue(TEXT("Reset modifies only default draft"), Model->GetDraftAudioInputDeviceId().IsEmpty());
		TestEqual(TEXT("Reset has no persistence side effect"), Model->UserSettings->GetAudioInputDeviceId(), FString(TEXT("a")));
		Model->Cancel();
	}
	Model->Microphones.Reset();
	Model->OnChanged.Broadcast();
	for (UUserWidget* View : { SettingsPage, static_cast<UUserWidget*>(Lake) })
	{
		AddInfo(FString::Printf(TEXT("Checking formal consumer: %s"), *GetNameSafe(View)));
		UComboBoxString* Combo = View ? Cast<UComboBoxString>(View->GetWidgetFromName(TEXT("MicrophoneComboBox"))) : nullptr;
		if (Combo)
		{
			TestFalse(TEXT("Missing device disables selection"), Combo->GetIsEnabled());
			TestEqual(TEXT("Missing saved microphone is not disguised as default"), Combo->GetSelectedOption(), FString(TEXT("已保存的麦克风当前不可用")));
		}
	}
	Root->ResetFrontend();
	Lake->ResetLakeMenuSettings();
	Model->Shutdown();
	Game->RemoveLocalPlayer(Local);
	Player->Destroy();
	Local->ViewportClient = nullptr;
	return true;
}

#endif
