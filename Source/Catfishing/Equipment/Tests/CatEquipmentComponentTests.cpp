#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Equipment/CatEquipmentComponent.h"
#include "GameFramework/Actor.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentComponentDefaultConsumableGateTest,
	"Catfishing.Unit.Equipment.Component.DefaultCatalogRejectsConsumableGrantWithoutMutatingSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentComponentFishingFailureNoneTest,
	"Catfishing.Unit.Equipment.Component.FishingFailureNoneIsIdempotentAndDoesNotPunish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatEquipmentComponentTest
{
	// 组件装配流程：用普通 authority Actor 承载 Equipment 组件；测试只通过组件公开命令和 Snapshot 观察行为。
	static UCatEquipmentComponent* AddEquipmentComponent(AActor* Host)
	{
		UCatEquipmentComponent* Component = Host ? NewObject<UCatEquipmentComponent>(Host) : nullptr;
		if (Host && Component)
		{
			Host->AddInstanceComponent(Component);
			Component->RegisterComponent();
		}
		return Component;
	}
}

// 测试流程：默认装备目录为空时提交耗材授予；Result 必须拒绝，Snapshot Revision、耗材数组和装配字段保持初始值。
bool FCatEquipmentComponentDefaultConsumableGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建 Equipment 默认目录测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UWorld* World = WorldWrapper.GetTestWorld();
		AActor* Host = World ? World->SpawnActor<AActor>() : nullptr;
		UCatEquipmentComponent* Component = CatEquipmentComponentTest::AddEquipmentComponent(Host);
		TestNotNull(TEXT("Equipment 默认目录测试宿主 Actor 可创建"), Host);
		TestNotNull(TEXT("Equipment 默认目录测试组件可创建"), Component);
		if (Component)
		{
			const FCatEquipmentLoadoutSnapshot Before = Component->GetSnapshot();
			const FCatDomainCommandResult Result = Component->GrantRunConsumableFromAuthority(
				FGuid::NewGuid(), Before.Revision, TEXT("MissingConsumable"), 1);
			TestFalse(TEXT("默认目录下耗材授予不提交"), Result.bCommitted);
			TestEqual(TEXT("默认目录下耗材授予返回 InvalidPayload"), Result.Error, ECatDomainCommandError::InvalidPayload);
			const FCatEquipmentLoadoutSnapshot& After = Component->GetSnapshot();
			TestEqual(TEXT("拒绝后 Equipment Revision 不变"), After.Revision, Before.Revision);
			TestEqual(TEXT("拒绝后耗材数组仍为空"), After.Consumables.Num(), Before.Consumables.Num());
			TestEqual(TEXT("拒绝后 Rod 仍为空"), After.RodDefinitionId, Before.RodDefinitionId);
			TestEqual(TEXT("拒绝后 Bait 仍为空"), After.BaitDefinitionId, Before.BaitDefinitionId);
			TestEqual(TEXT("拒绝后 Float 仍为空"), After.FloatDefinitionId, Before.FloatDefinitionId);
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

// 测试流程：用 Penalty=None 提交一次失败预算并原样重放；它应形成幂等终态，但不改变耐久、耗材或 Revision。
bool FCatEquipmentComponentFishingFailureNoneTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建 Equipment 失败预算测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UWorld* World = WorldWrapper.GetTestWorld();
		AActor* Host = World ? World->SpawnActor<AActor>() : nullptr;
		UCatEquipmentComponent* Component = CatEquipmentComponentTest::AddEquipmentComponent(Host);
		TestNotNull(TEXT("失败预算测试宿主 Actor 可创建"), Host);
		TestNotNull(TEXT("失败预算测试组件可创建"), Component);
		if (Component)
		{
			const FGuid RequestId = FGuid::NewGuid();
			const FCatEquipmentLoadoutSnapshot Before = Component->GetSnapshot();
			const FCatFishingFailureResult First = Component->CommitFishingFailure(
				RequestId, Before.Revision, ECatFishingFailurePenalty::None);
			TestTrue(TEXT("None 失败预算首次提交成功"), First.Command.bCommitted);
			TestEqual(TEXT("None 失败预算无错误"), First.Command.Error, ECatDomainCommandError::None);
			TestEqual(TEXT("None 失败预算不推进 Revision"), First.Command.Revision, Before.Revision);
			TestEqual(TEXT("None 失败预算保留惩罚类别"), First.Penalty, ECatFishingFailurePenalty::None);
			TestEqual(TEXT("None 失败预算不改变耐久"), Component->GetSnapshot().RodDurability, Before.RodDurability);
			TestEqual(TEXT("None 失败预算不创建耗材"), Component->GetSnapshot().Consumables.Num(), 0);

			const FCatFishingFailureResult Replay = Component->CommitFishingFailure(
				RequestId, Before.Revision, ECatFishingFailurePenalty::DamageRod);
			TestFalse(TEXT("失败预算重放不再次提交"), Replay.Command.bCommitted);
			TestEqual(TEXT("失败预算重放返回 AlreadyResolved"), Replay.Command.Error, ECatDomainCommandError::AlreadyResolved);
			TestEqual(TEXT("失败预算重放保留首次 None 惩罚"), Replay.Penalty, ECatFishingFailurePenalty::None);
			TestEqual(TEXT("失败预算重放不推进 Revision"), Replay.Command.Revision, First.Command.Revision);
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
