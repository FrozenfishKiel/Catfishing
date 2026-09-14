#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "UI/Frontend/CatFrontendRootWidget.h"
#include "UI/Frontend/CatFrontendRoomModel.h"
#include "UI/Frontend/CatFrontendPageController.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "AssetCompilingManager.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "ImageUtils.h"
#include "Serialization/BufferArchive.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFrontendJoinWidgetTest, "Catfishing.UI.Frontend.JoinPage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatFrontendJoinWidgetTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("创建独立 UI 世界"), TestWorld.CreateTestWorld(EWorldType::Game))) { return false; }
	UWorld* World = TestWorld.GetTestWorld();
	World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	if (!TestTrue(TEXT("UI 世界就绪"), TestWorld.BeginPlayInTestWorld())) { return false; }
	APlayerController* Player = World->SpawnActor<APlayerController>();
	Player->SetPlayer(NewObject<ULocalPlayer>(GEngine));
	const auto RootClass = LoadClass<UCatFrontendRootWidget>(nullptr, TEXT("/Game/UI/Frontend/WBP_CatFrontendRoot.WBP_CatFrontendRoot_C"));
	UCatFrontendRootWidget* View = CreateWidget<UCatFrontendRootWidget>(Player, RootClass);
	if (!TestNotNull(TEXT("创建正式 Root WBP"), View)) { return false; }
	UCatFrontendRoomModel* Room = NewObject<UCatFrontendRoomModel>();
	UCatFrontendPageController* Controller = NewObject<UCatFrontendPageController>();
	Controller->Initialize(nullptr, View, nullptr, Room, nullptr);
	View->InitializeFrontend(Controller, nullptr, Room, nullptr);
	UUserWidget* Menu = Cast<UUserWidget>(View->GetWidgetFromName(TEXT("MenuPage")));
	UUserWidget* Join = Cast<UUserWidget>(View->GetWidgetFromName(TEXT("JoinPage")));
	UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(View->GetWidgetFromName(TEXT("FrontendPageSwitcher")));
	if (!TestTrue(TEXT("正式页面装配"), Menu && Join && Switcher)) { return false; }
	UButton* Open = Cast<UButton>(Menu->GetWidgetFromName(TEXT("JoinPartyButton")));
	UButton* Submit = Cast<UButton>(Join->GetWidgetFromName(TEXT("JoinLinkButton")));
	UButton* Back = Cast<UButton>(Join->GetWidgetFromName(TEXT("JoinBackButton")));
	UEditableTextBox* Input = Cast<UEditableTextBox>(Join->GetWidgetFromName(TEXT("JoinLinkTextBox")));
	UTextBlock* Feedback = Cast<UTextBlock>(Join->GetWidgetFromName(TEXT("JoinResultText")));
	if (!TestTrue(TEXT("真实按钮与输入合同"), Open && Submit && Back && Input && Feedback)) { return false; }
	Open->OnClicked.Broadcast();
	TestTrue(TEXT("首页按钮进入加入页"), Switcher->GetActiveWidget() == Join);
	// 缺平台服务必须显示可恢复错误；不制造虚假的好友或房间。
	Input->SetText(FText::FromString(TEXT("invalid")));
	Submit->OnClicked.Broadcast();
	TestFalse(TEXT("缺服务返回真实错误"), Feedback->GetText().IsEmpty());
	TestTrue(TEXT("失败后可以重新提交"), Submit->GetIsEnabled());
	TestTrue(TEXT("失败后仍在加入页"), Switcher->GetActiveWidget() == Join);
	Back->OnClicked.Broadcast();
	TestTrue(TEXT("返回按钮回首页"), Switcher->GetActiveWidget() == Menu);
	// 离屏截图仅证明正式 WBP 的受控布局，不替代 Steam 真人双端。
	View->ShowJoin();
	Input->SetText(FText::GetEmpty());
	FAssetCompilingManager::Get().FinishAllCompilation();
	if (FApp::CanEverRender())
	{
		const TSharedRef<SWidget> SlateView = View->TakeWidget();
		FWidgetRenderer Renderer(true);
		// AutoWrapText 需要一次实际布局确定宽度，再捕获稳定帧。
		Renderer.DrawWidget(SlateView, FVector2D(1280, 720));
		View->ForceLayoutPrepass();
		UTextureRenderTarget2D* Target = Renderer.DrawWidget(SlateView, FVector2D(1280, 720));
		FBufferArchive PNG;
		if (TestNotNull(TEXT("正式界面渲染"), Target) && TestTrue(TEXT("导出截图"), FImageUtils::ExportRenderTarget2DAsPNG(Target, PNG)))
		{
			const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/FrontendJoin");
			IFileManager::Get().MakeDirectory(*Directory, true);
			TestTrue(TEXT("保存截图"), FFileHelper::SaveArrayToFile(PNG, *(Directory / TEXT("JoinPage.png"))));
		}
	}
	Controller->Shutdown();
	View->ResetFrontend();
	return true;
}
#endif
