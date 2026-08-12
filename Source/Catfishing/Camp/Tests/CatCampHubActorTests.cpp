#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Camp/CatCampHubActor.h"
#include "Camp/CatCampSettings.h"
#include "Character/CatCharacter.h"
#include "GameFramework/PlayerController.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatCampHubActorRangeAndFailClosedTest,
	"Catfishing.Unit.Camp.HubActor.RangeGateAndRequestsFailClosedWithoutBodyReadiness",
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

		/** 保存篝火事件原值；本测试不需要成像副作用。 */
		FName OldCampfireEvent = NAME_None;

		/** 记录可写默认设置对象；它是引擎 CDO，原始指针只在当前测试作用域恢复内存值。 */
		UCatCampSettings* Settings = nullptr;

		// 设置流程：仅在内存里开启固定营地范围，让测试可以验证服务器位置 gate；不调用 SaveConfig。
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
		}

		// 恢复流程：把所有被测设置恢复到进入测试前状态，防止后续模块测试读到临时营地配置。
		~FScopedCampSettings()
		{
			if (Settings)
			{
				Settings->bEnableCampRuntime = bOldRuntime;
				Settings->InteractionRadiusCentimeters = OldRadius;
				Settings->CampfireCoverEventId = OldCampfireEvent;
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
	TestEqual(TEXT("缺身份篝火请求 fail-closed"), CampfireResult.Error, ECatDomainCommandError::PolicyUndecided);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
