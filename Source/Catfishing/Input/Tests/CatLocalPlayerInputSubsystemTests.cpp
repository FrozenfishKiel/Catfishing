#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "EnhancedInputSubsystems.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Input/CatInputSettings.h"
#include "Input/CatLocalPlayerInputSubsystem.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Misc/ScopeExit.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatInputSettingsRuntimeGateTest,
	"Catfishing.Unit.Input.Settings.GlobalContextsRequireExplicitGateAndAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatInputSettingsCharacterInputGateTest,
	"Catfishing.Unit.Input.Settings.CharacterInputRequiresGateAndMoveLookActions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatLocalPlayerInputSubsystemTypeContractTest,
	"Catfishing.Unit.Input.LocalPlayerInputSubsystem.IsLocalPlayerScoped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatLocalPlayerInputSubsystemRefreshGateTest,
	"Catfishing.Unit.Input.LocalPlayerInputSubsystem.RefreshUsesConfigGateWithoutViewport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：瞬态 Settings 会复制 CDO 上已加载的项目 ini 值，所以先显式清成 gate 关闭、列表为空，再验证 fail-closed 判
// 定；开启 gate 后只收集非空 MappingContext，并按 Priority、Layer 稳定排序。
bool FCatInputSettingsRuntimeGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatInputSettings* Settings = NewObject<UCatInputSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 Input Settings"), Settings);
	if (!Settings)
	{
		return false;
	}
	Settings->bEnableGlobalInputContexts = false;
	Settings->MappingContexts.Reset();
	TestFalse(TEXT("gate 关闭时全局输入上下文不可运行"), Settings->IsRuntimeReady());

	Settings->bEnableGlobalInputContexts = true;
	TestFalse(TEXT("没有 MappingContext 资产时仍不可运行"), Settings->IsRuntimeReady());

	FCatInputMappingContextConfig EmptyContext;
	EmptyContext.Priority = 10;
	Settings->MappingContexts.Add(EmptyContext);
	TArray<FCatInputMappingContextConfig> RuntimeContexts;
	Settings->GetRuntimeContexts(RuntimeContexts);
	TestEqual(TEXT("空软引用不会进入运行上下文"), RuntimeContexts.Num(), 0);

	UInputMappingContext* LowPriorityContext = NewObject<UInputMappingContext>(Settings);
	UInputMappingContext* FrontendContext = NewObject<UInputMappingContext>(Settings);
	UInputMappingContext* OverlayContext = NewObject<UInputMappingContext>(Settings);
	TestNotNull(TEXT("可创建低优先级 MappingContext"), LowPriorityContext);
	TestNotNull(TEXT("可创建前端 MappingContext"), FrontendContext);
	TestNotNull(TEXT("可创建覆盖层 MappingContext"), OverlayContext);
	if (!LowPriorityContext || !FrontendContext || !OverlayContext)
	{
		return false;
	}

	FCatInputMappingContextConfig OverlaySamePriority;
	OverlaySamePriority.Layer = ECatInputContextLayer::OverlayUI;
	OverlaySamePriority.MappingContext = OverlayContext;
	OverlaySamePriority.Priority = 10;
	FCatInputMappingContextConfig LowPriority;
	LowPriority.Layer = ECatInputContextLayer::LakeGameplay;
	LowPriority.MappingContext = LowPriorityContext;
	LowPriority.Priority = -5;
	FCatInputMappingContextConfig FrontendSamePriority;
	FrontendSamePriority.Layer = ECatInputContextLayer::Frontend;
	FrontendSamePriority.MappingContext = FrontendContext;
	FrontendSamePriority.Priority = 10;
	Settings->MappingContexts = {OverlaySamePriority, LowPriority, EmptyContext, FrontendSamePriority};
	Settings->GetRuntimeContexts(RuntimeContexts);
	TestEqual(TEXT("只收集三条非空 MappingContext"), RuntimeContexts.Num(), 3);
	if (RuntimeContexts.Num() == 3)
	{
		TestEqual(TEXT("Priority 仍是主排序键"), RuntimeContexts[0].Priority, -5);
		TestEqual(TEXT("同 Priority 时 Frontend 层先于 Overlay 层"), RuntimeContexts[1].Layer, ECatInputContextLayer::Frontend);
		TestEqual(TEXT("同 Priority 时 Overlay 层排在 Frontend 后"), RuntimeContexts[2].Layer, ECatInputContextLayer::OverlayUI);
	}
	return !HasAnyErrors();
}

// 测试流程：瞬态 Settings 会复制 CDO 上已加载的项目 ini 值，所以先显式清成 gate 关闭、三个 IA 为空；再验证角色输入判
// 定是 fail-closed 的：总 gate、Move、Look 三者缺一都不可绑定，Jump 缺失不影响判定。
bool FCatInputSettingsCharacterInputGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatInputSettings* Settings = NewObject<UCatInputSettings>(GetTransientPackage());
	UInputAction* MoveAction = NewObject<UInputAction>(Settings);
	UInputAction* LookAction = NewObject<UInputAction>(Settings);
	TestNotNull(TEXT("可创建瞬态 Input Settings"), Settings);
	TestNotNull(TEXT("可创建 Move InputAction"), MoveAction);
	TestNotNull(TEXT("可创建 Look InputAction"), LookAction);
	if (!Settings || !MoveAction || !LookAction)
	{
		return false;
	}
	Settings->bEnableGlobalInputContexts = false;
	Settings->MoveAction.Reset();
	Settings->LookAction.Reset();
	Settings->JumpAction.Reset();

	TestFalse(TEXT("没有 gate 和资产时角色输入不可绑定"), Settings->IsCharacterInputReady());
	Settings->MoveAction = MoveAction;
	Settings->LookAction = LookAction;
	TestFalse(TEXT("只配了 Move/Look 但 gate 关闭时仍不可绑定"), Settings->IsCharacterInputReady());
	Settings->bEnableGlobalInputContexts = true;
	TestTrue(TEXT("gate 开启且 Move/Look 齐全时可绑定，Jump 为空不阻断"), Settings->IsCharacterInputReady());
	Settings->LookAction.Reset();
	TestFalse(TEXT("缺少 Look 时不可绑定"), Settings->IsCharacterInputReady());
	Settings->LookAction = LookAction;
	Settings->MoveAction.Reset();
	TestFalse(TEXT("缺少 Move 时不可绑定"), Settings->IsCharacterInputReady());
	return !HasAnyErrors();
}

// 测试流程：确认全局输入装配器挂在 LocalPlayer seam 上，而不是 GameInstance 或 Character 专属生命周期。
bool FCatLocalPlayerInputSubsystemTypeContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UClass* SubsystemClass = UCatLocalPlayerInputSubsystem::StaticClass();
	TestNotNull(TEXT("LocalPlayer Input 子系统类型可反射"), SubsystemClass);
	TestTrue(TEXT("输入装配器属于 LocalPlayerSubsystem"),
		SubsystemClass && SubsystemClass->IsChildOf(ULocalPlayerSubsystem::StaticClass()));
	return !HasAnyErrors();
}

// 测试流程：在无窗口自动化可安全构造的 LocalPlayer 上验证刷新边界；配置开启时子系统能接受显式 MappingContext，配置关
// 闭后返回未装配，不把无 viewport 的 EnhancedInput 状态误当作完整玩家输入验收。
bool FCatLocalPlayerInputSubsystemRefreshGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestNotNull(TEXT("LocalPlayer 输入测试存在引擎 Outer"), GEngine);
	if (!GEngine)
	{
		return false;
	}

	UCatInputSettings* Settings = GetMutableDefault<UCatInputSettings>();
	TestNotNull(TEXT("可取得可写输入配置默认对象"), Settings);
	if (!Settings)
	{
		return false;
	}
	const bool bSavedEnableGlobalInputContexts = Settings->bEnableGlobalInputContexts;
	const TArray<FCatInputMappingContextConfig> SavedMappingContexts = Settings->MappingContexts;
	ON_SCOPE_EXIT
	{
		Settings->bEnableGlobalInputContexts = bSavedEnableGlobalInputContexts;
		Settings->MappingContexts = SavedMappingContexts;
	};

	UInputMappingContext* GameplayContext = NewObject<UInputMappingContext>(GetTransientPackage());
	UInputMappingContext* OverlayContext = NewObject<UInputMappingContext>(GetTransientPackage());
	TestNotNull(TEXT("可创建玩法输入 MappingContext"), GameplayContext);
	TestNotNull(TEXT("可创建覆盖层输入 MappingContext"), OverlayContext);
	if (!GameplayContext || !OverlayContext)
	{
		return false;
	}
	GameplayContext->AddToRoot();
	OverlayContext->AddToRoot();
	ON_SCOPE_EXIT
	{
		GameplayContext->RemoveFromRoot();
		OverlayContext->RemoveFromRoot();
	};

	FCatInputMappingContextConfig GameplayConfig;
	GameplayConfig.Layer = ECatInputContextLayer::LakeGameplay;
	GameplayConfig.MappingContext = GameplayContext;
	GameplayConfig.Priority = 5;
	FCatInputMappingContextConfig OverlayConfig;
	OverlayConfig.Layer = ECatInputContextLayer::OverlayUI;
	OverlayConfig.MappingContext = OverlayContext;
	OverlayConfig.Priority = 15;
	Settings->bEnableGlobalInputContexts = false;
	Settings->MappingContexts.Reset();

	ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
	TestNotNull(TEXT("可创建测试 LocalPlayer"), LocalPlayer);
	if (!LocalPlayer)
	{
		return false;
	}
	LocalPlayer->PlayerAdded(nullptr, 0);
	ON_SCOPE_EXIT
	{
		LocalPlayer->PlayerRemoved();
	};

	UCatLocalPlayerInputSubsystem* CatInput = LocalPlayer->GetSubsystem<UCatLocalPlayerInputSubsystem>();
	UEnhancedInputLocalPlayerSubsystem* EnhancedInput = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	TestNotNull(TEXT("Cat LocalPlayer 输入子系统可用"), CatInput);
	TestNotNull(TEXT("EnhancedInput LocalPlayer 子系统可用"), EnhancedInput);
	if (!CatInput || !EnhancedInput)
	{
		return false;
	}

	TestFalse(TEXT("配置关闭时刷新保持未装配"), CatInput->RefreshConfiguredInputContexts());
	Settings->bEnableGlobalInputContexts = true;
	Settings->MappingContexts = {GameplayConfig, OverlayConfig};
	TestTrue(TEXT("配置开启且资源完整时刷新可接受 MappingContext"), CatInput->RefreshConfiguredInputContexts());
	Settings->bEnableGlobalInputContexts = false;
	Settings->MappingContexts.Reset();
	TestFalse(TEXT("再次关闭配置会移除本子系统记录并返回未装配"), CatInput->RefreshConfiguredInputContexts());
	return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatInputSettingsProjectDefaultsResolveTest,
	"Catfishing.Unit.Input.Settings.ProjectDefaultsResolveToExistingInputAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：从项目 DefaultGame.ini 读取 Input Settings，确认 gate 已开启、每条 MappingContext 与 Move/Look/Jump 软引
// 用都能同步加载到真实资产，且 IA 值类型与角色绑定假设一致（Move/Look=Axis2D，Jump=Boolean）；
// 这把“ini 指向的资产必须真的在 Content 里”固定成回归检查，资产被改名或删除时在这里失败，而不是 PIE 里悄悄没输入。
bool FCatInputSettingsProjectDefaultsResolveTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UCatInputSettings* Settings = GetDefault<UCatInputSettings>();
	TestNotNull(TEXT("项目 Input Settings 可读取"), Settings);
	if (!Settings)
	{
		return false;
	}

	TestTrue(TEXT("项目默认全局输入装配已开启"), Settings->bEnableGlobalInputContexts);
	TestTrue(TEXT("项目默认 MappingContext 配置可运行"), Settings->IsRuntimeReady());
	TestTrue(TEXT("项目默认角色输入可绑定"), Settings->IsCharacterInputReady());
	TArray<FCatInputMappingContextConfig> RuntimeContexts;
	Settings->GetRuntimeContexts(RuntimeContexts);
	TestTrue(TEXT("项目默认至少有一条可装配输入上下文"), RuntimeContexts.Num() > 0);
	for (const FCatInputMappingContextConfig& Context : RuntimeContexts)
	{
		TestNotNull(*FString::Printf(TEXT("MappingContext 资产可加载：%s"), *Context.MappingContext.ToString()),
			Context.MappingContext.LoadSynchronous());
	}

	const UInputAction* MoveAction = Settings->MoveAction.LoadSynchronous();
	const UInputAction* LookAction = Settings->LookAction.LoadSynchronous();
	const UInputAction* JumpAction = Settings->JumpAction.LoadSynchronous();
	TestNotNull(TEXT("Move InputAction 资产可加载"), MoveAction);
	TestNotNull(TEXT("Look InputAction 资产可加载"), LookAction);
	TestNotNull(TEXT("Jump InputAction 资产可加载"), JumpAction);
	if (MoveAction)
	{
		TestEqual(TEXT("Move 是 Axis2D"), MoveAction->ValueType, EInputActionValueType::Axis2D);
	}
	if (LookAction)
	{
		TestEqual(TEXT("Look 是 Axis2D"), LookAction->ValueType, EInputActionValueType::Axis2D);
	}
	if (JumpAction)
	{
		TestEqual(TEXT("Jump 是 Boolean"), JumpAction->ValueType, EInputActionValueType::Boolean);
	}
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
