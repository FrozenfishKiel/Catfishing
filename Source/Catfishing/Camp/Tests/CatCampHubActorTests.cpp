#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "UObject/UnrealType.h"

#include "AbilitySystem/Config/CatAbilitySettings.h"
#include "Camp/CatCampHubActor.h"
#include "Camp/CatCampSettings.h"
#include "Character/CatCharacter.h"
#include "GameFramework/PlayerController.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatCampHubActorRangeAndFailClosedTest,
	"Catfishing.Unit.Camp.HubActor.RangeGateAndRequestsFailClosedWithoutBodyReadiness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 合同测试类型：把篝火表现的网络声明和本地委托桥接固定在同一个用例里，防止后续只改 RPC 或只改播放入口时破坏跨端可见的请求身份。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatCampHubActorCampfireMulticastContractTest,
	"Catfishing.Unit.Camp.HubActor.CampfirePlaybackUsesReliableMulticastAndLocalDelegate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatCampHubActorTest
{
	/** 临时改动默认营地设置；析构只恢复内存 CDO，不写 Config 文件。 */
	struct FScopedCampSettings
	{
		/** 保存运行 gate 原值，避免本测试影响同进程后续 Automation。 */
		bool bOldRuntime = false;

		/** 保存交互半径原值，避免跨测试泄漏营地范围。 */
		double OldRadius = 0.0;

		/** 保存篝火事件原值；本测试不需要触发表现侧副作用。 */
		FName OldCampfireEvent = NAME_None;

		/** 保存 Ability runtime 原值；Camp 范围测试只需要 Pawn 归属，不让外部 GAS 配置触发属性初始化副作用。 */
		bool bOldAbilityRuntime = false;

		/** 记录可写默认设置对象；它是引擎 CDO，原始指针只在当前测试作用域恢复内存值。 */
		UCatCampSettings* Settings = nullptr;

		/** 记录 Ability 默认设置对象；只在当前测试进程临时关闭 runtime，析构时恢复进入测试前的内存状态。 */
		UCatAbilitySettings* AbilitySettings = nullptr;

		// 测试隔离设置说明：
		// 1. 读取营地和 Ability 默认对象，保存进入测试前的运行时开关和营地半径。
		// 2. 只在内存里开启固定营地范围，让服务器位置 gate 有稳定输入。
		// 3. 临时关闭 Character Ability runtime，避免本用例为了生成 Pawn 而依赖外部 GAS 初始化。
		// 4. 不调用 SaveConfig，保证这些测试设置不会写回项目配置文件。
		FScopedCampSettings()
		{
			Settings = GetMutableDefault<UCatCampSettings>();
			if (Settings)
			{
				bOldRuntime = Settings->bEnableCampRuntime;
				OldRadius = Settings->InteractionRadiusCentimeters;
				OldCampfireEvent = Settings->CampfireCoverEventId;
				Settings->bEnableCampRuntime = true;
				Settings->InteractionRadiusCentimeters = 200.0;
				Settings->CampfireCoverEventId = NAME_None;
			}
			AbilitySettings = GetMutableDefault<UCatAbilitySettings>();
			if (AbilitySettings)
			{
				bOldAbilityRuntime = AbilitySettings->bEnableCharacterAbilityRuntime;
				AbilitySettings->bEnableCharacterAbilityRuntime = false;
			}
		}

		// 测试隔离恢复说明：
		// 1. 如果本轮拿到了营地 CDO，就还原运行开关、交互半径和篝火事件。
		// 2. 如果本轮拿到了 Ability CDO，就还原 Character Ability runtime 开关。
		// 3. 只恢复当前测试改过的内存值，防止后续模块测试读到临时营地或 Ability 配置。
		~FScopedCampSettings()
		{
			if (Settings)
			{
				Settings->bEnableCampRuntime = bOldRuntime;
				Settings->InteractionRadiusCentimeters = OldRadius;
				Settings->CampfireCoverEventId = OldCampfireEvent;
			}
			if (AbilitySettings)
			{
				AbilitySettings->bEnableCharacterAbilityRuntime = bOldAbilityRuntime;
			}
		}
	};
}

// 测试流程：在真实 World 中放置营地、Controller 和 Character；用正式范围查询观察进入/离开，再确认缺少身体恢复配置时请求不会提交。
bool FCatCampHubActorRangeAndFailClosedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatCampHubActorTest::FScopedCampSettings SettingsGuard;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 CampHub 测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	ACatCampHubActor* Camp = World ? World->SpawnActor<ACatCampHubActor>() : nullptr;
	ACatCharacter* Character = World ? World->SpawnActor<ACatCharacter>() : nullptr;
	APlayerController* Controller = World ? World->SpawnActor<APlayerController>() : nullptr;
	TestNotNull(TEXT("营地测试 World 可用"), World);
	TestNotNull(TEXT("可生成固定营地 Actor"), Camp);
	TestNotNull(TEXT("可生成项目 Character"), Character);
	TestNotNull(TEXT("可生成测试 Controller"), Controller);
	if (!Camp || !Character || !Controller)
	{
		return false;
	}

	Camp->SetActorLocation(FVector::ZeroVector);
	Character->SetActorLocation(FVector(50.0, 0.0, 0.0));
	Controller->Possess(Character);
	TestTrue(TEXT("角色在显式半径内被认定在营地"), Camp->IsControllerInCamp(Controller));

	Character->SetActorLocation(FVector(500.0, 0.0, 0.0));
	TestFalse(TEXT("角色离开显式半径后不再属于营地"), Camp->IsControllerInCamp(Controller));

	const FGuid RequestId = FGuid::NewGuid();
	const FCatDomainCommandResult RestResult = Camp->RequestRest(Controller, RequestId);
	TestFalse(TEXT("范围外休息不会提交"), RestResult.bCommitted);
	TestEqual(TEXT("范围外休息返回 PolicyUndecided"), RestResult.Error, ECatDomainCommandError::PolicyUndecided);
	TestEqual(TEXT("休息拒绝保留 RequestId"), RestResult.RequestId, RequestId);

	const FCatDomainCommandResult CampfireResult = Camp->RequestCampfirePlayback(nullptr, FGuid::NewGuid());
	TestFalse(TEXT("缺身份篝火请求不会提交"), CampfireResult.bCommitted);
	TestEqual(TEXT("缺身份篝火请求返回 InvalidIdentity"), CampfireResult.Error, ECatDomainCommandError::InvalidIdentity);
	return !HasAnyErrors();
}

// 测试装配与证据边界：
// 1. 先从反射合同确认篝火 RPC 同时具备网络、多播和可靠标记，避免声明层退化成普通函数或非可靠广播。
// 2. 再在真实测试 World 中生成营地 Actor，并直接调用本地实现路径观察既有表现委托。
// 3. 最后断言 delegate 只广播一次且 RequestId 原样透传，证明播放侧仍能关联发起请求。
// 4. 本测试只覆盖反射标记和本地实现，不证明跨进程传输、客户端实际播放或 RequestCampfirePlayback 全链路。
bool FCatCampHubActorCampfireMulticastContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UFunction* Function = ACatCampHubActor::StaticClass()->FindFunctionByName(
		TEXT("MulticastCampfirePlaybackRequested"));
	TestNotNull(TEXT("篝火表现 Multicast 已反射"), Function);
	if (Function)
	{
		TestTrue(TEXT("篝火表现 RPC 具备网络标记"), Function->HasAnyFunctionFlags(FUNC_Net));
		TestTrue(TEXT("篝火表现 RPC 是 NetMulticast"), Function->HasAnyFunctionFlags(FUNC_NetMulticast));
		TestTrue(TEXT("篝火表现 RPC 使用 Reliable"), Function->HasAnyFunctionFlags(FUNC_NetReliable));
	}

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Campfire Multicast 测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	ACatCampHubActor* Camp = World ? World->SpawnActor<ACatCampHubActor>() : nullptr;
	TestNotNull(TEXT("可生成篝火 Multicast 测试营地"), Camp);
	if (!Camp || !Function)
	{
		return false;
	}

	int32 BroadcastCount = 0;
	FGuid ObservedRequestId;
	const FDelegateHandle Handle = Camp->OnCampfirePlaybackRequested.AddLambda(
		[&BroadcastCount, &ObservedRequestId](const FGuid RequestId)
		{
			++BroadcastCount;
			ObservedRequestId = RequestId;
		});
	const FGuid RequestId = FGuid::NewGuid();
	Camp->MulticastCampfirePlaybackRequested_Implementation(RequestId);
	Camp->OnCampfirePlaybackRequested.Remove(Handle);
	TestEqual(TEXT("一次本地 Multicast 实现只广播一次既有 delegate"), BroadcastCount, 1);
	TestEqual(TEXT("本地 delegate 收到原始 RequestId"), ObservedRequestId, RequestId);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
