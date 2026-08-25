#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Character/CatCharacter.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "Fishing/CatFishingSession.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "UI/CatFishingViewBridge.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "UI/CatUISettings.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SOverlay.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatLocalPlayerUISubsystemTypeContractTest,
	"Catfishing.Unit.UI.LocalPlayerUISubsystem.IsLocalPlayerScopedAndNotGlobalSingleton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatLocalPlayerUISubsystemFishingSessionLifecycleTest,
	"Catfishing.Unit.UI.LocalPlayerUISubsystem.ClearsFishingSessionWhenObservedActorEnds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatLocalPlayerUISubsystemLakeReachAttachTest,
	"Catfishing.Unit.UI.LocalPlayerUISubsystem.AttachesSingleLakeReachRootForPossessedCat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatLocalPlayerUISubsystemOnlineWidgetPolicyTest,
	"Catfishing.Unit.UI.LocalPlayerUISubsystem.OnlineTravelWidgetStaysOutOfLake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：先验证 UI 协调器归属 LocalPlayer 而不是 GameInstance，再按 ULocalPlayer 的 ClassWithin 约束用 Engine 作为 Outer 构造测试对象。
// 断言边界只覆盖子系统作用域、合法 Outer 构造和无正式根 View/Controller 时 Toggle 安全退出，不在这里伪造真实 Controller/Pawn 生命周期。
bool FCatLocalPlayerUISubsystemTypeContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UClass* SubsystemClass = UCatLocalPlayerUISubsystem::StaticClass();
	TestNotNull(TEXT("LocalPlayer UI 子系统类型可反射"), SubsystemClass);
	TestTrue(TEXT("UI 协调器挂在 LocalPlayerSubsystem 上"),
		SubsystemClass && SubsystemClass->IsChildOf(ULocalPlayerSubsystem::StaticClass()));
	TestFalse(TEXT("UI 协调器不是 GameInstance 全局单例"),
		SubsystemClass && SubsystemClass->IsChildOf(UGameInstanceSubsystem::StaticClass()));

	TestNotNull(TEXT("LocalPlayer 测试需要 Engine 作为 ClassWithin Outer"), GEngine);
	ULocalPlayer* LocalPlayer = GEngine ? NewObject<ULocalPlayer>(GEngine) : nullptr;
	TestNotNull(TEXT("可用合法 Engine Outer 创建测试 LocalPlayer"), LocalPlayer);
	UCatLocalPlayerUISubsystem* LocalUI = LocalPlayer
		? NewObject<UCatLocalPlayerUISubsystem>(LocalPlayer) : nullptr;
	TestNotNull(TEXT("可为单个 LocalPlayer 创建独立 UI 状态所有者"), LocalUI);
	if (LocalUI)
	{
		TestFalse(TEXT("没有正式根 View 时菜单保持关闭"), LocalUI->IsLakeMenuOpen());
		LocalUI->ToggleLakeMenu();
		TestFalse(TEXT("没有根 View 和 Controller 时 Toggle 不制造半套输入状态"), LocalUI->IsLakeMenuOpen());
	}
	return !HasAnyErrors();
}

// 测试流程：用 FTestWorldWrapper 创建带 GameInstance 的 Game World，再给 LocalPlayer 装入同一 WorldContext 且带 Overlay 的临时 GameViewportClient，避免命令行 Viewport 缺层的假失败。
// 随后让项目 Controller 通过正式 Pawn notifier 占有项目 Character；先断言默认玩家路径不创建 LakeReach 白盒根，再显式打开 gate 验证旧 UIReach 根仍可成对装卸。
bool FCatLocalPlayerUISubsystemLakeReachAttachTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建 UIReach Lake 根装配测试 World"),
		WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	FWorldContext* WorldContext = GEngine && World ? GEngine->GetWorldContextFromWorld(World) : nullptr;
	if (!TestNotNull(TEXT("读取 UIReach Lake 根装配测试 World"), World)
		|| !TestNotNull(TEXT("读取 UIReach 测试 GameInstance"), GameInstance)
		|| !TestNotNull(TEXT("读取 UIReach 测试 WorldContext"), WorldContext))
	{
		return false;
	}
	if (!TestTrue(TEXT("启动 UIReach Lake 根装配测试 World Play"), WorldWrapper.BeginPlayInTestWorld()))
	{
		WorldWrapper.ForwardErrorMessages(this);
		return false;
	}

	TStrongObjectPtr<UGameViewportClient> ViewportClient(GEngine ? NewObject<UGameViewportClient>(GEngine) : nullptr);
	TStrongObjectPtr<ULocalPlayer> LocalPlayer(GEngine ? NewObject<ULocalPlayer>(GEngine) : nullptr);
	if (!TestNotNull(TEXT("创建 UIReach 测试 GameViewportClient"), ViewportClient.Get())
		|| !TestNotNull(TEXT("创建 UIReach 测试 LocalPlayer"), LocalPlayer.Get()))
	{
		return false;
	}
	ViewportClient->Init(*WorldContext, GameInstance, false);
	// UE5.8 的真实 GameViewport 会通过窗口 Bind 创建 Overlay；命令行测试没有窗口，只用 deprecated setter 填入最小承载层，避免 UMG AddToViewport 因夹具缺层触发 ensure。
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	ViewportClient->SetViewportOverlayWidget(nullptr, SNew(SOverlay));
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	WorldContext->GameViewport = ViewportClient.Get();
	LocalPlayer->PlayerAdded(ViewportClient.Get(), 0);
	UCatLocalPlayerUISubsystem* LocalUI = LocalPlayer->GetSubsystem<UCatLocalPlayerUISubsystem>();
	ACatfishingPlayerController* Controller = World->SpawnActor<ACatfishingPlayerController>();
	ACatCharacter* Character = World->SpawnActor<ACatCharacter>();
	if (!TestNotNull(TEXT("取得 UIReach LocalPlayer 子系统"), LocalUI)
		|| !TestNotNull(TEXT("生成 UIReach 项目 Controller"), Controller)
		|| !TestNotNull(TEXT("生成 UIReach 项目 Character"), Character))
	{
		LocalPlayer->PlayerRemoved();
		WorldContext->GameViewport = nullptr;
		return false;
	}
	UCatUISettings* Settings = GetMutableDefault<UCatUISettings>();
	if (!TestNotNull(TEXT("读取可变 UI Settings 默认对象"), Settings))
	{
		LocalPlayer->PlayerRemoved();
		WorldContext->GameViewport = nullptr;
		return false;
	}
	const bool bSavedLakeStatusView = Settings->bEnableLakeStatusView;
	Settings->bEnableLakeStatusView = false;

	Controller->SetPlayer(LocalPlayer.Get());
	LocalUI->PlayerControllerChanged(Controller);
	Controller->Possess(Character);
	TestNull(TEXT("默认配置下占有 Character 不创建 LakeReach 白盒根 View"), LocalUI->LakeReachWidget.Get());
	TestNull(TEXT("默认配置下不创建 UIReach Fishing 只读 Bridge"), LocalUI->FishingViewBridge.Get());
	TestFalse(TEXT("默认配置下菜单不会被白盒根打开"), LocalUI->IsLakeMenuOpen());

	Settings->bEnableLakeStatusView = true;
	LocalUI->HandleControllerPawnChanged(Character);
	TestNotNull(TEXT("显式开启后创建唯一 LakeReach 根 View"), LocalUI->LakeReachWidget.Get());
	TestNotNull(TEXT("显式开启后创建唯一 Fishing 只读 Bridge"), LocalUI->FishingViewBridge.Get());
	TestTrue(TEXT("LakeReach 根绑定当前 Character 的 ASC"),
		LocalUI->BoundLakeASC.Get() == Character->GetAbilitySystemComponent());
	TestFalse(TEXT("初始菜单不会在装配时自动打开"), LocalUI->IsLakeMenuOpen());

	LocalUI->DetachLakePawn();
	TestNull(TEXT("Detach 后移除 LakeReach 根 View"), LocalUI->LakeReachWidget.Get());
	TestNull(TEXT("Detach 后释放 Fishing 只读 Bridge"), LocalUI->FishingViewBridge.Get());
	TestFalse(TEXT("Detach 后菜单状态保持关闭"), LocalUI->IsLakeMenuOpen());
	Settings->bEnableLakeStatusView = bSavedLakeStatusView;
	LocalPlayer->PlayerRemoved();
	WorldContext->GameViewport = nullptr;
	return !HasAnyErrors();
}

// 测试流程：创建最小 Game World 和真实 FishingSession Actor，只走 UIReach 内部 Bridge/生命周期 helper；命令行 Editor 下不伪造 LocalPlayer 占有链。
// 断言边界是 Actor 销毁后 Bridge 与观察状态被清空，后续根 View 刷新才会得到 no active session。
bool FCatLocalPlayerUISubsystemFishingSessionLifecycleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建 UIReach Fishing 生命周期只读观察测试 World"),
		WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = WorldWrapper.GetTestWorld();
	if (!TestNotNull(TEXT("读取 UIReach Fishing 生命周期测试 World"), World))
	{
		return false;
	}
	WorldWrapper.BeginPlayInTestWorld();

	TStrongObjectPtr<ULocalPlayer> LocalPlayer(GEngine ? NewObject<ULocalPlayer>(GEngine) : nullptr);
	TStrongObjectPtr<UCatLocalPlayerUISubsystem> LocalUI(LocalPlayer.IsValid()
		? NewObject<UCatLocalPlayerUISubsystem>(LocalPlayer.Get()) : nullptr);
	if (!TestNotNull(TEXT("用合法 LocalPlayer Outer 创建 UIReach 子系统"), LocalUI.Get()))
	{
		return false;
	}
	LocalUI->FishingViewBridge = NewObject<UCatFishingViewBridge>(LocalUI.Get());
	if (!TestNotNull(TEXT("为 UIReach 子系统创建唯一 FishingViewBridge"), LocalUI->FishingViewBridge.Get()))
	{
		return false;
	}

	ACatFishingSession* Session = World->SpawnActor<ACatFishingSession>();
	if (!TestNotNull(TEXT("生成可被 UIReach 观察的 FishingSession Actor"), Session))
	{
		return false;
	}
	LocalUI->SetFishingViewSession(Session);
	TestTrue(TEXT("Bridge 先绑定当前 FishingSession"), LocalUI->FishingViewBridge->GetBoundSession() == Session);
	TestTrue(TEXT("UIReach 生命周期观察指向同一 FishingSession"), LocalUI->ObservedFishingSession.Get() == Session);

	TestTrue(TEXT("销毁当前观察的 FishingSession Actor"), Session->Destroy());
	TestNull(TEXT("Session 销毁后 Bridge 不再保留旧会话"), LocalUI->FishingViewBridge->GetBoundSession());
	TestFalse(TEXT("Session 销毁后生命周期观察被成对清理"), LocalUI->ObservedFishingSession.IsValid());
	return !HasAnyErrors();
}

// 测试流程：直接喂入 Online 完整快照，覆盖 Frontend、去 Lake、Lake Host/Client、直开 Lake 和离局 pending；断言白盒 TravelWidget 只属于前端，Lake 离局只挂在菜单策略上。
// 该用例不构造真实 Widget 或 Online 子系统，避免把会话创建、地图旅行和 UI 可见性揉成一个难定位的集成失败。
bool FCatLocalPlayerUISubsystemOnlineWidgetPolicyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCatOnlineSnapshot FrontendSnapshot;
	FrontendSnapshot.WorldState = ECatOnlineWorldState::Frontend;
	FrontendSnapshot.SessionState = ECatOnlineSessionState::NoSession;
	FrontendSnapshot.SessionRole = ECatOnlineSessionRole::None;
	FrontendSnapshot.ActiveOperation = ECatOnlineOperation::None;
	TestTrue(TEXT("Frontend 仍显示正式组局入口"),
		UCatLocalPlayerUISubsystem::ShouldShowOnlineTravelWidget(FrontendSnapshot));
	TestFalse(TEXT("Frontend 不显示 Lake 菜单离局入口"),
		UCatLocalPlayerUISubsystem::CanRequestOnlineLeaveFromLake(FrontendSnapshot));

	FCatOnlineSnapshot TravelingToLakeSnapshot = FrontendSnapshot;
	TravelingToLakeSnapshot.WorldState = ECatOnlineWorldState::TravelingToLake;
	TravelingToLakeSnapshot.ActiveOperation = ECatOnlineOperation::Create;
	TestTrue(TEXT("进入 Lake 前的旅行 pending 仍可刷新 Frontend 面板"),
		UCatLocalPlayerUISubsystem::ShouldShowOnlineTravelWidget(TravelingToLakeSnapshot));

	FCatOnlineSnapshot LakeHostSnapshot;
	LakeHostSnapshot.WorldState = ECatOnlineWorldState::Lake;
	LakeHostSnapshot.SessionState = ECatOnlineSessionState::Host;
	LakeHostSnapshot.SessionRole = ECatOnlineSessionRole::Host;
	LakeHostSnapshot.ActiveOperation = ECatOnlineOperation::None;
	TestFalse(TEXT("Host 到达 Lake 后不再显示 Host/Find/Join 白盒"),
		UCatLocalPlayerUISubsystem::ShouldShowOnlineTravelWidget(LakeHostSnapshot));
	TestTrue(TEXT("Host 到达 Lake 后菜单可提交正式 Leave"),
		UCatLocalPlayerUISubsystem::CanRequestOnlineLeaveFromLake(LakeHostSnapshot));

	FCatOnlineSnapshot LakeClientSnapshot = LakeHostSnapshot;
	LakeClientSnapshot.SessionState = ECatOnlineSessionState::Client;
	LakeClientSnapshot.SessionRole = ECatOnlineSessionRole::Client;
	TestFalse(TEXT("Client 到达 Lake 后不再显示 Host/Find/Join 白盒"),
		UCatLocalPlayerUISubsystem::ShouldShowOnlineTravelWidget(LakeClientSnapshot));
	TestTrue(TEXT("Client 到达 Lake 后菜单可提交正式 Leave"),
		UCatLocalPlayerUISubsystem::CanRequestOnlineLeaveFromLake(LakeClientSnapshot));

	FCatOnlineSnapshot DirectLakeSnapshot;
	DirectLakeSnapshot.WorldState = ECatOnlineWorldState::Lake;
	DirectLakeSnapshot.SessionState = ECatOnlineSessionState::NoSession;
	DirectLakeSnapshot.SessionRole = ECatOnlineSessionRole::None;
	DirectLakeSnapshot.ActiveOperation = ECatOnlineOperation::None;
	TestFalse(TEXT("直开 Lake 不显示联机白盒"),
		UCatLocalPlayerUISubsystem::ShouldShowOnlineTravelWidget(DirectLakeSnapshot));
	TestFalse(TEXT("直开 Lake 没有 Session 时不显示离局按钮"),
		UCatLocalPlayerUISubsystem::CanRequestOnlineLeaveFromLake(DirectLakeSnapshot));

	FCatOnlineSnapshot LeavingLakeSnapshot = LakeClientSnapshot;
	LeavingLakeSnapshot.ActiveOperation = ECatOnlineOperation::Leave;
	TestFalse(TEXT("Lake Leave pending 时仍不显示联机白盒"),
		UCatLocalPlayerUISubsystem::ShouldShowOnlineTravelWidget(LeavingLakeSnapshot));
	TestFalse(TEXT("Lake Leave pending 时禁用重复离局入口"),
		UCatLocalPlayerUISubsystem::CanRequestOnlineLeaveFromLake(LeavingLakeSnapshot));

	FCatOnlineSnapshot TravelingToFrontendSnapshot = LeavingLakeSnapshot;
	TravelingToFrontendSnapshot.WorldState = ECatOnlineWorldState::TravelingToFrontend;
	TestFalse(TEXT("回 Frontend 途中不把 Host/Find/Join 面板盖回玩法画面"),
		UCatLocalPlayerUISubsystem::ShouldShowOnlineTravelWidget(TravelingToFrontendSnapshot));
	TestFalse(TEXT("回 Frontend 途中不再显示 Lake 菜单离局入口"),
		UCatLocalPlayerUISubsystem::CanRequestOnlineLeaveFromLake(TravelingToFrontendSnapshot));
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
