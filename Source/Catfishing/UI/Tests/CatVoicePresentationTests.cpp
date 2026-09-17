#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/Voice/CatVoicePresentation.h"
#include "Online/Voice/CatVoiceSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatVoiceActivityEnvelopeTest, "Catfishing.Online.Voice.ActivityEnvelope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatVoiceActivityEnvelopeTest::RunTest(const FString& Parameters)
{
	const auto* Settings = GetDefault<UCatVoiceSettings>();
	FCatVoiceActivityEnvelope State;
	for (int32 I=0; I<20; ++I) { State.Update(0, true, .02f, *Settings); }
	TestFalse(TEXT("Open microphone with silence is not speaking"), State.bActive);
	State.Update(.5f, true, .02f, *Settings);
	State.Update(0, true, .02f, *Settings);
	TestFalse(TEXT("Short click does not flash icon"), State.bActive);
	for (int32 I=0; I<10; ++I) { State.Update(.2f, true, .02f, *Settings); }
	TestTrue(TEXT("Sustained voice activates icon"), State.bActive);
	State.Update(0, true, .05f, *Settings);
	TestTrue(TEXT("Brief gap between words retains icon"), State.bActive);
	for (int32 I=0; I<10; ++I) { State.Update(0, true, .05f, *Settings); }
	TestFalse(TEXT("Silence removes icon after release"), State.bActive);
	for (int32 I=0; I<10; ++I) { State.Update(.3f, true, .02f, *Settings); }
	State.Update(.3f, false, .001f, *Settings);
	TestFalse(TEXT("Disable, key release or out-of-range clears immediately"), State.bActive);
	State.Update(.5f, true, .02f, *Settings);
	TestFalse(TEXT("Reentry requires fresh sustained audio"), State.bActive);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatVoiceStatusTextTest, "Catfishing.Online.Voice.StatusPresentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatVoiceStatusTextTest::RunTest(const FString& Parameters)
{
	using namespace CatVoicePresentation;
	TestTrue(TEXT("Saved PTT mode is explicit"), ModeText(ECatVoiceInputMode::PushToTalk).ToString().Contains(TEXT("V")));
	TestTrue(TEXT("Frontend cannot claim active speech"), StatusText(ECatVoiceInputMode::AlwaysOn,true,false,true,false,false).ToString().Contains(TEXT("进入游戏")));
	TestTrue(TEXT("Unavailable backend is visible"), StatusText(ECatVoiceInputMode::AlwaysOn,false,true,false,true,false).ToString().Contains(TEXT("服务未就绪")));
	TestTrue(TEXT("Failed capture does not claim enabled"), StatusText(ECatVoiceInputMode::AlwaysOn,true,true,false,true,false).ToString().Contains(TEXT("麦克风未就绪")));
	TestTrue(TEXT("Menu or focus suppresses input"), StatusText(ECatVoiceInputMode::AlwaysOn,true,true,true,false,false).ToString().Contains(TEXT("暂停")));
	TestTrue(TEXT("PTT waits for key"), StatusText(ECatVoiceInputMode::PushToTalk,true,true,true,true,false).ToString().Contains(TEXT("等待按住")));
	TestTrue(TEXT("Gate open does not claim speaking"), StatusText(ECatVoiceInputMode::AlwaysOn,true,true,true,true,true).ToString().Contains(TEXT("有声音时")));
	return true;
}
#endif
