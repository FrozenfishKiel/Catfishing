#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Profile/CatProfileSettings.h"
#include "Profile/CatProfileSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatProfileSubsystemCapturePlanGateTest,
	"Catfishing.Unit.Profile.Subsystem.CapturePlanBroadcastRequiresExplicitBridgeAndCompletePlan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatProfileSubsystemTest
{
	/** 临时覆盖 Profile 默认配置的守卫；ReceiveCapturePlan 只读取 GetDefault，因此测试必须恢复默认对象。 */
	struct FProfileSettingsOverride
	{
		/** 被临时改写的默认配置对象。 */
		UCatProfileSettings* Settings = GetMutableDefault<UCatProfileSettings>();

		/** 原始持久化 gate。 */
		bool bOldPersistence = false;

		/** 原始槽位名。 */
		FString OldSlotName;

		/** 原始成像桥 gate。 */
		bool bOldBridge = false;

		// 保存流程：记录旧值后写入一套可让外部成像桥成立的显式配置。
		FProfileSettingsOverride()
		{
			if (Settings)
			{
				bOldPersistence = Settings->bEnableProfilePersistence;
				OldSlotName = Settings->SaveSlotBaseName;
				bOldBridge = Settings->bEnableExternalImprintCaptureBridge;
				Settings->bEnableProfilePersistence = true;
				Settings->SaveSlotBaseName = TEXT("AutomationProfile");
				Settings->bEnableExternalImprintCaptureBridge = true;
			}
		}

		// 恢复流程：只还原内存默认对象，不写配置文件。
		~FProfileSettingsOverride()
		{
			if (Settings)
			{
				Settings->bEnableProfilePersistence = bOldPersistence;
				Settings->SaveSlotBaseName = OldSlotName;
				Settings->bEnableExternalImprintCaptureBridge = bOldBridge;
			}
		}
	};

	// 计划构造流程：构造一份字段完整的 CapturePlan；图片生成和文件落盘不在 Profile 子系统内伪造。
	static FCatCapturePlan MakeValidPlan()
	{
		FCatCapturePlan Plan;
		Plan.CapturePlanId = FGuid::NewGuid();
		Plan.CandidateId = FGuid::NewGuid();
		Plan.RunId = FGuid::NewGuid();
		Plan.RunAlbumId = FGuid::NewGuid();
		Plan.EventType = TEXT("CaptureMoment");
		Plan.SubjectId = FGuid::NewGuid();
		return Plan;
	}
}

// 测试流程：直接通过 Profile 子系统公开入口接收 CapturePlan；缺桥或缺稳定字段时不得广播，桥和计划都完整时只广播一次原计划。
bool FCatProfileSubsystemCapturePlanGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestNotNull(TEXT("Profile 子系统测试存在引擎 Outer"), GEngine);
	ULocalPlayer* LocalPlayer = GEngine ? NewObject<ULocalPlayer>(GEngine) : nullptr;
	TestNotNull(TEXT("Profile 子系统测试 LocalPlayer 可用合法 Engine Outer 创建"), LocalPlayer);
	UCatProfileSubsystem* Profile = LocalPlayer ? NewObject<UCatProfileSubsystem>(LocalPlayer) : nullptr;
	TestNotNull(TEXT("可创建 Profile 子系统对象"), Profile);
	if (!Profile)
	{
		return false;
	}

	int32 BroadcastCount = 0;
	FCatCapturePlan LastPlan;
	Profile->OnCapturePlanReceived.AddLambda([&BroadcastCount, &LastPlan](const FCatCapturePlan& Plan)
	{
		++BroadcastCount;
		LastPlan = Plan;
	});

	const FCatCapturePlan ValidPlan = CatProfileSubsystemTest::MakeValidPlan();
	TestFalse(TEXT("默认配置下 CapturePlan 不广播"), Profile->ReceiveCapturePlan(ValidPlan));
	TestEqual(TEXT("默认配置广播次数为 0"), BroadcastCount, 0);

	{
		CatProfileSubsystemTest::FProfileSettingsOverride Override;
		FCatCapturePlan InvalidPlan = ValidPlan;
		InvalidPlan.RunAlbumId.Invalidate();
		TestFalse(TEXT("字段不完整的 CapturePlan 不广播"), Profile->ReceiveCapturePlan(InvalidPlan));
		TestEqual(TEXT("无效计划后广播次数仍为 0"), BroadcastCount, 0);

		TestTrue(TEXT("桥和计划都完整时 CapturePlan 被接管"), Profile->ReceiveCapturePlan(ValidPlan));
		TestEqual(TEXT("完整计划只广播一次"), BroadcastCount, 1);
		TestEqual(TEXT("广播保留 CapturePlanId"), LastPlan.CapturePlanId, ValidPlan.CapturePlanId);
		TestEqual(TEXT("广播保留 CandidateId"), LastPlan.CandidateId, ValidPlan.CandidateId);
		TestEqual(TEXT("广播保留 RunAlbumId"), LastPlan.RunAlbumId, ValidPlan.RunAlbumId);
	}
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
