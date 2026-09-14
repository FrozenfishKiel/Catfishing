#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Online/CatFrontendListener.h"
#include "Online/CatOnlineSubsystem.h"
#include "Framework/Game/CatFrontendGameMode.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFrontendListenLifecycleTest,
	"Catfishing.Online.FrontendListen.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FCatFrontendListenLifecycleTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!TestTrue(TEXT("创建隔离的真实 World"), Fixture.CreateTestWorld(EWorldType::Game))) { return false; }
	UWorld* World = Fixture.GetTestWorld();
	World->GetWorldSettings()->DefaultGameMode = ACatFrontendGameMode::StaticClass();
	if (!TestTrue(TEXT("使用正式 Frontend GameMode"), Fixture.BeginPlayInTestWorld())) { return false; }
	// 临时驱动定义仅供本隔离 World 使用；真实 socket 随机端口，不占用项目 Steam 驱动或编辑器会话。
	FNetDriverDefinition Definition;
	Definition.DefName = FName(*(TEXT("CatListenTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	Definition.DriverClassName = FName(TEXT("/Script/OnlineSubsystemUtils.IpNetDriver"));
	Definition.DriverClassNameFallback = Definition.DriverClassName;
	GEngine->NetDriverDefinitions.Add(Definition);
	World->URL.AddOption(*FString::Printf(TEXT("NetDriverDef=%s"), *Definition.DefName.ToString()));
	World->URL.Port = 0;
	UCatOnlineSubsystem* Online = World->GetGameInstance()->GetSubsystem<UCatOnlineSubsystem>();
	if (!TestNotNull(TEXT("正式 Online 子系统"), Online))
	{
		GEngine->NetDriverDefinitions.RemoveAll([&](const auto& D) { return D.DefName == Definition.DefName; });
		return false;
	}
	const FGuid RequestId = FGuid::NewGuid();
	if (TestTrue(TEXT("原地启动真实监听"), Online->FrontendListener.Start(World, RequestId, 1)))
	{
		UNetDriver* FirstDriver = World->GetNetDriver();
		TestEqual(TEXT("前台已成为 Listen Server"), World->GetNetMode(), NM_ListenServer);
		TestTrue(TEXT("重复启动幂等"), Online->FrontendListener.Start(World, RequestId, 1));
		TestEqual(TEXT("重复调用不替换驱动"), World->GetNetDriver(), FirstDriver);

		FCatFrontendListener OtherOwner;
		AddExpectedMessage(TEXT("Event=online_frontend_listen_rejected"), EAutomationExpectedMessageFlags::Contains, 1);
		TestFalse(TEXT("其他生命周期不能接管已有驱动"), OtherOwner.Start(World, RequestId, 2));
		OtherOwner.Stop(TEXT("NotOwner"), RequestId, 2);
		TestEqual(TEXT("非所有者清理不影响监听"), World->GetNetDriver(), FirstDriver);

		FString Error;
		AddExpectedMessage(TEXT("Event=online_frontend_admission_rejected"), EAutomationExpectedMessageFlags::Contains, 1);
		World->GetAuthGameMode<ACatFrontendGameMode>()->PreLogin(TEXT(""), TEXT("127.0.0.1"), FUniqueNetIdRepl(), Error);
		TestFalse(TEXT("新增监听未开放尚未完成的前台准入"), Error.IsEmpty());

		// 真实平台异步失败最终汇入相同收口函数，检查失败能释放 socket 并允许重新开房。
		Online->ActiveOperation = ECatOnlineOperation::Create;
		Online->SessionRole = ECatOnlineSessionRole::None;
		Online->FinishOperationFailure(ECatOnlineError::CreateFailed);
		TestNull(TEXT("创建失败释放监听"), World->GetNetDriver());
		TestEqual(TEXT("失败恢复独立 World"), World->GetNetMode(), NM_Standalone);
		TestTrue(TEXT("失败后可再次监听"), Online->FrontendListener.Start(World, RequestId, 3));
		Online->ActiveOperation = ECatOnlineOperation::Start;
		Online->SessionRole = ECatOnlineSessionRole::Host;
		Online->FinishOperationFailure(ECatOnlineError::GameplayPreloadFailed);
		TestTrue(TEXT("开始预载失败保留房间监听"), Online->FrontendListener.IsListening(World));
		Online->ActiveOperation = ECatOnlineOperation::Leave;
		Online->FinishOperationFailure(ECatOnlineError::DestroyFailed);
		TestTrue(TEXT("Session 销毁失败保留原监听"), Online->FrontendListener.IsListening(World));

		// 引擎旅行会销毁旧驱动；模拟同一个 World 上换成新驱动，旧所有权不能误关新实例。
		GEngine->DestroyNamedNetDriver(World, NAME_GameNetDriver);
		FURL NewUrl = World->URL;
		NewUrl.AddOption(TEXT("listen"));
		if (TestTrue(TEXT("引擎创建新的监听实例"), World->Listen(NewUrl)))
		{
			UNetDriver* Replacement = World->GetNetDriver();
			Online->FrontendListener.Stop(TEXT("OldWorldCleanup"), RequestId, 4);
			TestEqual(TEXT("旧监听清理不关闭新驱动"), World->GetNetDriver(), Replacement);
			GEngine->DestroyNamedNetDriver(World, NAME_GameNetDriver);
		}
		TestTrue(TEXT("前台离房前可启动监听"), Online->FrontendListener.Start(World, RequestId, 5));
		Online->WorldState = ECatOnlineWorldState::Frontend;
		Online->ActiveOperation = ECatOnlineOperation::Leave;
		Online->SessionState = ECatOnlineSessionState::NoSession;
		Online->SessionRole = ECatOnlineSessionRole::None;
		TestTrue(TEXT("前台离房走正式终态入口"), Online->BeginTravelToFrontend());
		TestNull(TEXT("前台离房关闭驱动"), World->GetNetDriver());
		TestTrue(TEXT("反初始化前可再次监听"), Online->FrontendListener.Start(World, RequestId, 6));
	}
	// GameInstance Shutdown 调用正式 Deinitialize；在 World context 被移除之前检查驱动已释放。
	World->GetGameInstance()->Shutdown();
	TestNull(TEXT("GameInstance 关闭释放监听"), World->GetNetDriver());
	GEngine->NetDriverDefinitions.RemoveAll([&](const auto& D) { return D.DefName == Definition.DefName; });
	return !HasAnyErrors();
}

#endif
