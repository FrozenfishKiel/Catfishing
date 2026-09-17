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
#include "Misc/ConfigCacheIni.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "InputCoreTypes.h"
#include "Components/InputComponent.h"
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
#include "UI/Voice/CatVoicePresentation.h"
#if WITH_EDITOR
#include "Slate/WidgetRenderer.h"
#include "ImageUtils.h"
#include "Serialization/BufferArchive.h"
#include "Misc/FileHelper.h"
#endif

namespace
{
	// 只替换物理驱动；注册、发送 gate、Stop 尾采集、清包和重新启动均使用正式 UE 实现。
	class FCaptureDeviceDouble : public IVoiceCapture
	{
	public:
		FString ActiveName;
		TSet<FString> FailedDevices;
		EVoiceCaptureState::Type State = EVoiceCaptureState::NotCapturing;
		int32 ChangeCount = 0;
		bool Init(const FString&, int32, int32) override { return true; }
		void Shutdown() override { State = EVoiceCaptureState::UnInitialized; }
		bool Start() override
		{
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
	using CatVoiceInput::Detail::PrepareVoice;
	TSharedPtr<FCaptureDeviceDouble> Capture = MakeShared<FCaptureDeviceDouble>();
	TSharedPtr<FTestVoiceEngine, ESPMode::ThreadSafe> Engine = MakeShared<FTestVoiceEngine, ESPMode::ThreadSafe>(Capture);
	FTestOnlineVoice Voice(Engine);
	const TArray<FCatVoiceInputDevice> Devices = { {TEXT(""), TEXT("Default")}, {TEXT("a"), TEXT("Mic A")}, {TEXT("b"), TEXT("Mic B")} };
	Engine->SeedRemainder();
	auto Result = PrepareVoice(Voice, 0, TEXT("a"), Devices, false);
	TestTrue(TEXT("Prepare input while disabled"), Result.bApplied);
	TestEqual(TEXT("Active physical device"), Capture->ActiveName, FString(TEXT("Mic A")));
	TestFalse(TEXT("Preparation never enables sending or recording"), Voice.Sending() || Capture->IsCapturing());
	TestEqual(TEXT("Old PCM remainder discarded"), Engine->Remainder(), uint32(0));
	Voice.StartNetworkedVoice(0);
	TestTrue(TEXT("Fixture starts old input before switching"), Voice.Sending() && Capture->IsCapturing());
	Result = PrepareVoice(Voice, 0, TEXT("b"), Devices, true);
	TestTrue(TEXT("Live switch prepares requested input"), Result.bApplied);
	TestFalse(TEXT("Device transaction leaves sending to mode controller"), Voice.Sending() || Capture->IsCapturing());
	TestEqual(TEXT("New active device"), Capture->ActiveName, FString(TEXT("Mic B")));
	Capture->FailedDevices.Add(TEXT("Mic A"));
	Result = PrepareVoice(Voice, 0, TEXT("a"), Devices, true);
	TestFalse(TEXT("Failed switch cannot report success"), Result.bApplied);
	TestEqual(TEXT("Rollback restored actual previous input"), Capture->ActiveName, FString(TEXT("Mic B")));
	TestFalse(TEXT("Rollback cannot secretly restart recording"), Voice.Sending() || Capture->IsCapturing());
	Capture->FailedDevices.Add(TEXT("Mic B"));
	Result = PrepareVoice(Voice, 0, TEXT("a"), Devices, true);
	TestFalse(TEXT("Double failure stays stopped"), Result.bApplied || Voice.Sending() || Capture->IsCapturing());
	Capture->FailedDevices.Reset();
	Result = PrepareVoice(Voice, 0, TEXT("a"), Devices, true);
	TestTrue(TEXT("Recover from failed device without restarting engine"), Result.bApplied);
	const int32 BeforeMissing = Capture->ChangeCount;
	Result = PrepareVoice(Voice, 0, TEXT("missing"), Devices, true);
	TestFalse(TEXT("Missing saved device never sends default"), Result.bApplied || Voice.Sending());
	TestEqual(TEXT("Missing selection never opens another device"), Capture->ChangeCount, BeforeMissing);
	Result = PrepareVoice(Voice, 0, TEXT(""), Devices, false);
	TestTrue(TEXT("Reset to platform default applies"), Result.bApplied);
	TestTrue(TEXT("Default passes empty device name"), Capture->ActiveName.IsEmpty());
	TestFalse(TEXT("Reset stays muted"), Voice.Sending() || Capture->IsCapturing());
	Result = PrepareVoice(Voice, 1, TEXT("a"), Devices, true);
	TestEqual(TEXT("Wrong local user rejected"), Result.Error, FName(TEXT("InvalidLocalUser")));
	TSharedPtr<FTestVoiceEngine, ESPMode::ThreadSafe> MissingEngine = MakeShared<FTestVoiceEngine, ESPMode::ThreadSafe>(TSharedPtr<IVoiceCapture>());
	FTestOnlineVoice MissingVoice(MissingEngine);
	TestTrue(TEXT("No microphone can still be disabled"), PrepareVoice(MissingVoice, 0, TEXT(""), Devices, false).bApplied);
	Result = PrepareVoice(MissingVoice, 0, TEXT(""), Devices, true);
	TestFalse(TEXT("AlwaysOn/PTT cannot report ready without microphone"), Result.bApplied);
	TestEqual(TEXT("Missing capture has explicit reason"), Result.Error, FName(TEXT("CaptureUnavailable")));
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
	Settings->VoiceInputMode = ECatVoiceInputMode::PushToTalk;
	Settings->SaveConfig(CPF_Config, *ConfigPath);
	UCatGameUserSettings* Reloaded = NewObject<UCatGameUserSettings>();
	Reloaded->LoadConfig(UCatGameUserSettings::StaticClass(), *ConfigPath);
	TestEqual(TEXT("Device ID survives config save and reload"), Reloaded->GetAudioInputDeviceId(), FString(TEXT("previous-input")));
	TestTrue(TEXT("PTT survives config save and reload"), Reloaded->GetVoiceInputMode() == ECatVoiceInputMode::PushToTalk);
	TArray<FCatVoiceInputDevice> HardwareDevices;
	CatVoiceInput::Enumerate(HardwareDevices);
	AddInfo(FString::Printf(TEXT("Physical enumeration returned %d selectable entries; no microphone was recorded"), HardwareDevices.Num()));
	Settings->SetNonVoiceSettingsToDefaults();
	TestEqual(TEXT("Failed default application can preserve saved device"), Settings->GetAudioInputDeviceId(), FString(TEXT("previous-input")));
	AddExpectedError(TEXT("settings_voice_apply_rejected"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("No world cannot commit a new microphone"), Settings->ApplyVoicePreferences(nullptr, 0, TEXT("new-input"), ECatVoiceInputMode::Disabled));
	TestEqual(TEXT("Unapplied selection is never persisted"), Settings->GetAudioInputDeviceId(), FString(TEXT("previous-input")));
	TestTrue(TEXT("Non-voice defaults preserve mode"), Settings->GetVoiceInputMode() == ECatVoiceInputMode::PushToTalk);
	const FString Section = Settings->GetClass()->GetPathName();
	GConfig->RemoveKey(*Section, TEXT("VoiceInputMode"), ConfigPath);
	GConfig->SetBool(*Section, TEXT("bVoiceChatEnabled"), true, ConfigPath);
	Settings->MigrateVoiceInputMode(ConfigPath);
	TestTrue(TEXT("Legacy enabled becomes AlwaysOn"), Settings->GetVoiceInputMode() == ECatVoiceInputMode::AlwaysOn);
	bool Legacy = false;
	TestFalse(TEXT("Legacy persistence key removed"), GConfig->GetBool(*Section, TEXT("bVoiceChatEnabled"), Legacy, ConfigPath));
	Settings->VoiceInputMode = ECatVoiceInputMode::PushToTalk;
	GConfig->SetBool(*Section, TEXT("bVoiceChatEnabled"), true, ConfigPath);
	Settings->MigrateVoiceInputMode(ConfigPath);
	TestTrue(TEXT("Existing new mode takes priority over old boolean"), Settings->GetVoiceInputMode() == ECatVoiceInputMode::PushToTalk);
	GConfig->RemoveKey(*Section, TEXT("VoiceInputMode"), ConfigPath);
	GConfig->SetBool(*Section, TEXT("bVoiceChatEnabled"), false, ConfigPath);
	Settings->MigrateVoiceInputMode(ConfigPath);
	TestTrue(TEXT("Legacy off stays disabled"), Settings->GetVoiceInputMode() == ECatVoiceInputMode::Disabled);
	Settings->SetToDefaults();
	TestTrue(TEXT("Clean default has no device override"), Settings->GetAudioInputDeviceId().IsEmpty());
	TestFalse(TEXT("Clean default voice remains disabled"), Settings->GetVoiceInputMode() != ECatVoiceInputMode::Disabled);
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
		UComboBoxString* Mode = View ? Cast<UComboBoxString>(View->GetWidgetFromName(TEXT("VoiceInputModeComboBox"))) : nullptr;
		if (TestNotNull(TEXT("Formal voice mode selector"), Mode))
		{
			TestEqual(TEXT("Exactly three modes"), Mode->GetOptionCount(), 3);
			TestTrue(TEXT("Mode delegate is bound on real WBP"), Mode->OnSelectionChanged.IsBound());
			TestFalse(TEXT("Old master checkbox removed from live tree"), View->GetWidgetFromName(TEXT("VoiceChatCheckBox")) != nullptr);
			Mode->OnSelectionChanged.Broadcast(TEXT("按住 V 说话"), ESelectInfo::OnMouseClick);
			if (Model->IsInputModeSettingAvailable())
			{
				TestTrue(TEXT("Supported selection reaches draft"), Model->GetDraftVoiceInputMode() == ECatVoiceInputMode::PushToTalk);
			}
			else
			{
				TestTrue(TEXT("Unavailable service rejects UI selection"), Model->GetDraftVoiceInputMode() == ECatVoiceInputMode::Disabled);
			}
			Model->SetDraftVoiceInputMode(ECatVoiceInputMode::PushToTalk);
			TestEqual(TEXT("Draft update renders actual mode selector"), Mode->GetSelectedOption(), FString(TEXT("按住 V 说话")));
			TestTrue(TEXT("Selection alone never changes saved mode"), Model->UserSettings->GetVoiceInputMode() == ECatVoiceInputMode::Disabled);
			Model->Cancel();
			TestEqual(TEXT("Cancel restores mode selector"), Mode->GetSelectedOption(), FString(TEXT("禁用")));
			Model->SetDraftVoiceInputMode(ECatVoiceInputMode::AlwaysOn);
			Model->RestoreDefaults();
			TestTrue(TEXT("Reset draft disables own transmission"), Model->GetDraftVoiceInputMode() == ECatVoiceInputMode::Disabled);
			Model->Cancel();
		}
		UComboBoxString* Combo = View ? Cast<UComboBoxString>(View->GetWidgetFromName(TEXT("MicrophoneComboBox"))) : nullptr;
		if (!TestNotNull(TEXT("Formal microphone control"), Combo)) { continue; }
		TestTrue(TEXT("Real device list enables existing control"), Combo->GetIsEnabled());
		TestEqual(TEXT("Saved input is selected"), Combo->GetSelectedOption(), FString(TEXT("Mic A")));
		TestTrue(TEXT("Live Slate consumer retains its selection delegate"), Combo->OnSelectionChanged.IsBound());
		Combo->OnSelectionChanged.Broadcast(TEXT("Mic B"), ESelectInfo::OnMouseClick);
		TestEqual(TEXT("Widget event reaches shared draft model"), Model->GetDraftAudioInputDeviceId(), FString(TEXT("b")));
		TestEqual(TEXT("Selecting does not persist before Apply"), Model->UserSettings->GetAudioInputDeviceId(), FString(TEXT("a")));
		TestFalse(TEXT("Selecting never enables voice preference"), Model->GetDraftVoiceInputMode() != ECatVoiceInputMode::Disabled);
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
	Root->RenderRoomSnapshot(FCatOnlineSnapshot());
	UUserWidget* RoomPage = Cast<UUserWidget>(Root->GetWidgetFromName(TEXT("RoomPage")));
	FText AppliedMode, RuntimeStatus;
	CatVoicePresentation::ReadLocalStatus(Local, AppliedMode, RuntimeStatus);
	for (const auto& Pair : TMap<FName,FText>{{TEXT("RoomVoiceValue"), RuntimeStatus}, {TEXT("RoomMicrophoneValue"), AppliedMode}})
	{
		auto* Label = RoomPage ? Cast<UTextBlock>(RoomPage->GetWidgetFromName(Pair.Key)) : nullptr;
		if (TestNotNull(TEXT("Formal room voice value exists"), Label)) { TestEqual(TEXT("Formal room consumes live status, not placeholders"), Label->GetText().ToString(), Pair.Value.ToString()); }
	}
#if WITH_EDITOR
	if (FApp::CanEverRender())
	{
		const auto RootSlate = Root->TakeWidget();
		Root->ShowRoom();
		Root->RequestOpenRoomSettings();
		TestTrue(TEXT("Production room settings dialog opens"), Root->IsRoomDialogOpen());
		FWidgetRenderer Renderer(true);
		Renderer.DrawWidget(RootSlate, FVector2D(1280,720));
		auto* Target = Renderer.DrawWidget(RootSlate, FVector2D(1280,720));
		FBufferArchive PNG;
		if (TestNotNull(TEXT("Formal room render target"), Target) && TestTrue(TEXT("Render formal room settings"), FImageUtils::ExportRenderTarget2DAsPNG(Target, PNG)))
		{
			const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/VoiceUI");
			IFileManager::Get().MakeDirectory(*Directory, true);
			TestTrue(TEXT("Save formal room screenshot"), FFileHelper::SaveArrayToFile(PNG, *(Directory / TEXT("RoomSettings.png"))));
		}
		Root->RequestCloseRoomDialog();
	}
#endif
	Root->ResetFrontend();
	Lake->ResetLakeMenuSettings();
	Model->Shutdown();
	Game->RemoveLocalPlayer(Local);
	Player->Destroy();
	Local->ViewportClient = nullptr;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatVoiceTransmitLifecycleTest, "Catfishing.Online.Voice.TransmitLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatVoiceTransmitLifecycleTest::RunTest(const FString& Parameters)
{
	// 替换物理驱动，正式 subsystem 仍向真实 UE VoiceInterface/VoiceEngine 发命令。
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
	ACatfishingPlayerController* Controller = World->SpawnActor<ACatfishingPlayerController>();
	Controller->SetPlayer(Local);
	UCatVoiceTransmitSubsystem* Transmit = Local->GetSubsystem<UCatVoiceTransmitSubsystem>();
	if (!TestNotNull(TEXT("LocalPlayer creates production transmit subsystem"), Transmit)) { return false; }
	TestNotNull(TEXT("Production controller creates input component"), Controller->InputComponent.Get());
	int32 PressBindings = 0, ReleaseBindings = 0;
	if (Controller->InputComponent)
	{
		for (const FInputKeyBinding& Binding : Controller->InputComponent->KeyBindings)
		{
			if (Binding.Chord.Key != EKeys::V) { continue; }
			PressBindings += Binding.KeyEvent == IE_Pressed;
			ReleaseBindings += Binding.KeyEvent == IE_Released;
		}
	}
	TestEqual(TEXT("Exactly one V press binding"), PressBindings, 1);
	TestEqual(TEXT("Exactly one V release binding"), ReleaseBindings, 1);
	Transmit->ConfiguredWorld = World;
	TSharedPtr<FCaptureDeviceDouble> Capture = MakeShared<FCaptureDeviceDouble>();
	TSharedPtr<FTestVoiceEngine, ESPMode::ThreadSafe> Engine = MakeShared<FTestVoiceEngine, ESPMode::ThreadSafe>(Capture);
	TSharedPtr<FTestOnlineVoice, ESPMode::ThreadSafe> Voice = MakeShared<FTestOnlineVoice, ESPMode::ThreadSafe>(Engine);
	const TArray<FCatVoiceInputDevice> Devices = { {TEXT("a"), TEXT("Mic A")} };
	TestTrue(TEXT("Prepare selected input while muted"), CatVoiceInput::Detail::PrepareVoice(*Voice, 0, TEXT("a"), Devices, true).bApplied);
	Transmit->ActiveVoice = Voice;
	Transmit->ConfiguredLocalUser = 0;
	Transmit->bReady = true;
	Transmit->InputMode = ECatVoiceInputMode::PushToTalk;
	Transmit->Reconcile(true, TEXT("TestIdle"));
	TestFalse(TEXT("PTT idle remains muted"), Voice->Sending());
	Transmit->bHeld = true;
	Transmit->Reconcile(true, TEXT("TestPress"));
	TestTrue(TEXT("Held PTT starts real capture and sending"), Voice->Sending() && Capture->IsCapturing());
	for (const FInputKeyBinding& Binding : Controller->InputComponent->KeyBindings)
	{
		if (Binding.Chord.Key == EKeys::V && Binding.KeyEvent == IE_Released) { Binding.KeyDelegate.Execute(EKeys::V); }
	}
	TestFalse(TEXT("Release immediately removes network gate"), Voice->Sending());
	Transmit->bHeld = true;
	Transmit->Reconcile(true, TEXT("TestRapidRepress"));
	TestTrue(TEXT("Rapid repress accepts pending capture restart"), Voice->Sending());
	Capture->State = EVoiceCaptureState::NotCapturing;
	Engine->GetVoiceDataReadyFlags();
	TestTrue(TEXT("UE finishes pending stop and restarts capture"), Capture->IsCapturing());
	Controller->ClearPhysicalControlInput(TEXT("ModalOpened"));
	TestFalse(TEXT("Production modal cleanup reaches the local voice subsystem"), Voice->Sending() || Transmit->bHeld);
	Transmit->Reconcile(false, TEXT("TestMenuOrFocusLost"));
	TestFalse(TEXT("Context loss stops sending and drops held input"), Voice->Sending() || Transmit->bHeld);
	Transmit->Reconcile(true, TEXT("TestContextReturned"));
	TestFalse(TEXT("Returning context cannot replay old held key"), Voice->Sending());
	Transmit->InputMode = ECatVoiceInputMode::AlwaysOn;
	Transmit->Reconcile(true, TEXT("TestAlwaysOn"));
	TestTrue(TEXT("AlwaysOn sends without held key"), Voice->Sending());
	Transmit->Reconcile(false, TEXT("TestAlwaysFocusLost"));
	TestFalse(TEXT("AlwaysOn also stops in blocked context"), Voice->Sending());
	Transmit->Reconcile(true, TEXT("TestAlwaysReturn"));
	TestTrue(TEXT("AlwaysOn resumes when context returns"), Voice->Sending());
	Transmit->InputMode = ECatVoiceInputMode::Disabled;
	Transmit->bHeld = true;
	Transmit->Reconcile(true, TEXT("TestDisabled"));
	TestFalse(TEXT("Disabled never sends despite held key"), Voice->Sending());
	Transmit->InputMode = ECatVoiceInputMode::AlwaysOn;
	Transmit->Reconcile(true, TEXT("TestBeforeTravel"));
	Transmit->Suspend(TEXT("Travel"));
	Transmit->Reconcile(true, TEXT("TestOldWorldTick"));
	TestFalse(TEXT("Travel blocks old world from restarting voice"), Voice->Sending() || Transmit->bReady || Transmit->bHeld);
	TestFalse(TEXT("Travel releases old provider reference"), Transmit->ActiveVoice.IsValid());
	TestEqual(TEXT("All key and lifecycle changes reuse the selected microphone"), Capture->ChangeCount, 1);
	Transmit->Configure(World, ECatVoiceInputMode::AlwaysOn);
	TestFalse(TEXT("Late session callback cannot reactivate the departing world"), Transmit->bReady || Transmit->bSending);
	Game->RemoveLocalPlayer(Local);
	Controller->Destroy();
	Local->ViewportClient = nullptr;
	return true;
}

#endif
