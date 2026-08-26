#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Character/CatCharacter.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "UI/CatUISettings.h"
#include "UI/HUD/CatHUDModel.h"
#include "UI/HUD/CatHUDWidget.h"
#include "UI/Interaction/CatInteractionPageController.h"
#include "UI/Interaction/CatInteractionPromptWidget.h"
#include "UI/Inventory/CatInventoryModel.h"
#include "UI/Inventory/CatInventoryPageController.h"
#include "UI/Inventory/CatInventoryWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SOverlay.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatLocalPlayerUISubsystemTypeContractTest,
	"Catfishing.Unit.UI.LocalPlayerUISubsystem.IsLocalPlayerScopedAndNotGlobalSingleton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatLocalPlayerUISubsystemSplitPlayerModulesAttachTest,
	"Catfishing.Unit.UI.LocalPlayerUISubsystem.AttachesSplitPlayerModulesForPossessedCat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatLocalPlayerUISubsystemOnlineWidgetPolicyTest,
	"Catfishing.Unit.UI.LocalPlayerUISubsystem.OnlineTravelWidgetStaysOutOfLake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：先验证 UI 协调器归属 LocalPlayer 而不是 GameInstance，再按 ULocalPlayer 的 ClassWithin 约束用 Engine 作为 Outer 构造测试对象。
// 断言边界只覆盖子系统作用域、合法 Outer 构造和无正式背包页面时 Toggle 安全退出，不伪造真实 Controller/Pawn 生命周期。
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
		TestFalse(TEXT("没有背包 PageController 时背包保持关闭"), LocalUI->IsInventoryOpen());
		LocalUI->ToggleInventory();
		TestFalse(TEXT("没有背包 View 和 Controller 时 Toggle 不制造半套输入状态"), LocalUI->IsInventoryOpen());
	}
	return !HasAnyErrors();
}

// 测试流程：用 FTestWorldWrapper 创建带 GameInstance 的 Game World，再给 LocalPlayer 装入同一 WorldContext 且带 Overlay 的临时 GameViewportClient，避免命令行 Viewport 缺层的假失败。
// 随后让项目 Controller 通过正式 Pawn notifier 占有项目 Character；先断言关闭 gate 时不创建半套模块，再显式打开 gate 验证 HUD、背包和交互提示成对装卸。
bool FCatLocalPlayerUISubsystemSplitPlayerModulesAttachTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建拆分玩家 UI 装配测试 World"),
		WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	FWorldContext* WorldContext = GEngine && World ? GEngine->GetWorldContextFromWorld(World) : nullptr;
	if (!TestNotNull(TEXT("读取拆分玩家 UI 装配测试 World"), World)
		|| !TestNotNull(TEXT("读取拆分玩家 UI 测试 GameInstance"), GameInstance)
		|| !TestNotNull(TEXT("读取拆分玩家 UI 测试 WorldContext"), WorldContext))
	{
		return false;
	}
	if (!TestTrue(TEXT("启动拆分玩家 UI 装配测试 World Play"), WorldWrapper.BeginPlayInTestWorld()))
	{
		WorldWrapper.ForwardErrorMessages(this);
		return false;
	}

	TStrongObjectPtr<UGameViewportClient> ViewportClient(GEngine ? NewObject<UGameViewportClient>(GEngine) : nullptr);
	TStrongObjectPtr<ULocalPlayer> LocalPlayer(GEngine ? NewObject<ULocalPlayer>(GEngine) : nullptr);
	if (!TestNotNull(TEXT("创建拆分玩家 UI 测试 GameViewportClient"), ViewportClient.Get())
		|| !TestNotNull(TEXT("创建拆分玩家 UI 测试 LocalPlayer"), LocalPlayer.Get()))
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
	if (!TestNotNull(TEXT("取得拆分玩家 UI LocalPlayer 子系统"), LocalUI)
		|| !TestNotNull(TEXT("生成项目 Controller"), Controller)
		|| !TestNotNull(TEXT("生成项目 Character"), Character))
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
	const bool bSavedPlayerLakeUI = Settings->bEnablePlayerLakeUI;
	Settings->bEnablePlayerLakeUI = false;

	Controller->SetPlayer(LocalPlayer.Get());
	LocalUI->PlayerControllerChanged(Controller);
	Controller->Possess(Character);
	TestNull(TEXT("关闭 gate 后不创建 HUD View"), LocalUI->HUDWidget.Get());
	TestNull(TEXT("关闭 gate 后不创建 HUD Model"), LocalUI->HUDModel.Get());
	TestNull(TEXT("关闭 gate 后不创建 Inventory View"), LocalUI->InventoryWidget.Get());
	TestNull(TEXT("关闭 gate 后不创建 Inventory Model"), LocalUI->InventoryModel.Get());
	TestNull(TEXT("关闭 gate 后不创建 Inventory PageController"), LocalUI->InventoryPageController.Get());
	TestNull(TEXT("关闭 gate 后不创建 Interaction View"), LocalUI->InteractionPromptWidget.Get());
	TestNull(TEXT("关闭 gate 后不创建 Interaction PageController"), LocalUI->InteractionPageController.Get());
	TestFalse(TEXT("关闭 gate 后背包不会生成半套输入状态"), LocalUI->IsInventoryOpen());

	Settings->bEnablePlayerLakeUI = true;
	LocalUI->HandleControllerPawnChanged(Character);
	TestNotNull(TEXT("显式开启后创建 HUD WBP View"), LocalUI->HUDWidget.Get());
	TestNotNull(TEXT("显式开启后创建 HUD Model"), LocalUI->HUDModel.Get());
	TestNotNull(TEXT("显式开启后创建 Inventory WBP View"), LocalUI->InventoryWidget.Get());
	TestNotNull(TEXT("显式开启后创建 Inventory Model"), LocalUI->InventoryModel.Get());
	TestNotNull(TEXT("显式开启后创建 Inventory PageController"), LocalUI->InventoryPageController.Get());
	TestNotNull(TEXT("显式开启后创建 Interaction WBP View"), LocalUI->InteractionPromptWidget.Get());
	TestNotNull(TEXT("显式开启后创建 Interaction PageController"), LocalUI->InteractionPageController.Get());
	if (LocalUI->HUDWidget)
	{
		TestTrue(TEXT("HUD 玩家前端继承正式 View 基类"),
			LocalUI->HUDWidget->GetClass()->IsChildOf(UCatHUDWidget::StaticClass()));
		TestEqual(TEXT("HUD 玩家前端加载正式 WBP 类"),
			LocalUI->HUDWidget->GetClass()->GetName(),
			FString(TEXT("WBP_CatHUD_C")));
	}
	if (LocalUI->InventoryWidget)
	{
		TestTrue(TEXT("Inventory 玩家前端继承正式 View 基类"),
			LocalUI->InventoryWidget->GetClass()->IsChildOf(UCatInventoryWidget::StaticClass()));
		TestEqual(TEXT("Inventory 玩家前端加载正式 WBP 类"),
			LocalUI->InventoryWidget->GetClass()->GetName(),
			FString(TEXT("WBP_CatInventory_C")));
	}
	if (LocalUI->InteractionPromptWidget)
	{
		TestTrue(TEXT("Interaction 玩家前端继承正式 View 基类"),
			LocalUI->InteractionPromptWidget->GetClass()->IsChildOf(UCatInteractionPromptWidget::StaticClass()));
		TestEqual(TEXT("Interaction 玩家前端加载正式 WBP 类"),
			LocalUI->InteractionPromptWidget->GetClass()->GetName(),
			FString(TEXT("WBP_CatInteractionPrompt_C")));
	}
	TestFalse(TEXT("初始背包不会在装配时自动打开"), LocalUI->IsInventoryOpen());

	LocalUI->DetachPlayerLakeUI();
	TestNull(TEXT("Detach 后移除 HUD WBP View"), LocalUI->HUDWidget.Get());
	TestNull(TEXT("Detach 后释放 HUD Model"), LocalUI->HUDModel.Get());
	TestNull(TEXT("Detach 后移除 Inventory WBP View"), LocalUI->InventoryWidget.Get());
	TestNull(TEXT("Detach 后释放 Inventory Model"), LocalUI->InventoryModel.Get());
	TestNull(TEXT("Detach 后释放 Inventory PageController"), LocalUI->InventoryPageController.Get());
	TestNull(TEXT("Detach 后移除 Interaction WBP View"), LocalUI->InteractionPromptWidget.Get());
	TestNull(TEXT("Detach 后释放 Interaction PageController"), LocalUI->InteractionPageController.Get());
	TestFalse(TEXT("Detach 后背包状态保持关闭"), LocalUI->IsInventoryOpen());
	Settings->bEnablePlayerLakeUI = bSavedPlayerLakeUI;
	LocalPlayer->PlayerRemoved();
	WorldContext->GameViewport = nullptr;
	return !HasAnyErrors();
}

// 测试流程：直接喂入 Online 完整快照，覆盖 Frontend、去 Lake、Lake Host/Client、直开 Lake 和离局 pending。
// 该用例不构造真实 Widget 或 Online 子系统，只验证 Frontend 旅行白盒不盖回 Lake 玩法画面。
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

	FCatOnlineSnapshot LakeClientSnapshot = LakeHostSnapshot;
	LakeClientSnapshot.SessionState = ECatOnlineSessionState::Client;
	LakeClientSnapshot.SessionRole = ECatOnlineSessionRole::Client;
	TestFalse(TEXT("Client 到达 Lake 后不再显示 Host/Find/Join 白盒"),
		UCatLocalPlayerUISubsystem::ShouldShowOnlineTravelWidget(LakeClientSnapshot));

	FCatOnlineSnapshot DirectLakeSnapshot;
	DirectLakeSnapshot.WorldState = ECatOnlineWorldState::Lake;
	DirectLakeSnapshot.SessionState = ECatOnlineSessionState::NoSession;
	DirectLakeSnapshot.SessionRole = ECatOnlineSessionRole::None;
	DirectLakeSnapshot.ActiveOperation = ECatOnlineOperation::None;
	TestFalse(TEXT("直开 Lake 不显示联机白盒"),
		UCatLocalPlayerUISubsystem::ShouldShowOnlineTravelWidget(DirectLakeSnapshot));

	FCatOnlineSnapshot LeavingLakeSnapshot = LakeClientSnapshot;
	LeavingLakeSnapshot.ActiveOperation = ECatOnlineOperation::Leave;
	TestFalse(TEXT("Lake Leave pending 时仍不显示联机白盒"),
		UCatLocalPlayerUISubsystem::ShouldShowOnlineTravelWidget(LeavingLakeSnapshot));

	FCatOnlineSnapshot TravelingToFrontendSnapshot = LeavingLakeSnapshot;
	TravelingToFrontendSnapshot.WorldState = ECatOnlineWorldState::TravelingToFrontend;
	TestFalse(TEXT("回 Frontend 途中不把 Host/Find/Join 面板盖回玩法画面"),
		UCatLocalPlayerUISubsystem::ShouldShowOnlineTravelWidget(TravelingToFrontendSnapshot));
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
