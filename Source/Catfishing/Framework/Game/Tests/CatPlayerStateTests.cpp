#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Framework/Game/CatfishingPlayerState.h"

namespace CatPlayerStateTest
{
	// 记录构造流程：创建公开鱼图鉴摘要的一条合法记录；测试只观察 PlayerState 的公开读写合同，不触碰本地 Profile 存档。
	static FCatFishCollectionRecord MakeCollectionRecord(const FName FishDefinitionId)
	{
		FCatFishCollectionRecord Record;
		Record.FishDefinitionId = FishDefinitionId;
		Record.State = ECatFishCollectionState::Recorded;
		Record.BestWeightKilograms = 2.5;
		Record.EncounterCount = 1;
		return Record;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatPlayerStateCollectionAndUnlockTest,
	"Catfishing.Unit.Framework.PlayerState.PublicCollectionValidationAndStarterUnlockContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：在真实 World 生成项目 PlayerState，写入合法公开图鉴摘要，再尝试重复 ID；
// 随后验证装备解锁证明：None 永远是 starter；非空 UnlockId 在服务器还没持有清单时拒绝，owning client 上报并通过校验后放行，
// 含 None 或重复条目的清单整份被拒且不覆盖上一份。这条测试此前锁的是"非空一律拒绝"的 stub 语义，按 D-04 接线后改成锁
// "只认服务器持有的清单"，不是放宽——没上报仍然拒绝。
bool FCatPlayerStateCollectionAndUnlockTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 PlayerState 测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建 PlayerState 测试 World"), World);
	if (!World)
	{
		return false;
	}

	ACatfishingPlayerState* PlayerState = World->SpawnActor<ACatfishingPlayerState>();
	TestNotNull(TEXT("可生成项目 PlayerState"), PlayerState);
	if (!PlayerState)
	{
		return false;
	}

	const TArray<FCatFishCollectionRecord> ValidRecords = {
		CatPlayerStateTest::MakeCollectionRecord(TEXT("FishA"))
	};
	TestTrue(TEXT("合法公开图鉴摘要可写入"), PlayerState->SetPublicFishCollectionFromAuthority(ValidRecords));
	TestEqual(TEXT("公开图鉴摘要数量为 1"), PlayerState->GetPublicFishCollection().Num(), 1);
	TestEqual(TEXT("公开图鉴摘要保持 FishDefinitionId"), PlayerState->GetPublicFishCollection()[0].FishDefinitionId, FName(TEXT("FishA")));

	TArray<FCatFishCollectionRecord> DuplicateRecords;
	DuplicateRecords.Add(CatPlayerStateTest::MakeCollectionRecord(TEXT("FishA")));
	DuplicateRecords.Add(CatPlayerStateTest::MakeCollectionRecord(TEXT("FishA")));
	TestFalse(TEXT("重复 FishDefinitionId 摘要被拒绝"), PlayerState->SetPublicFishCollectionFromAuthority(DuplicateRecords));
	TestEqual(TEXT("拒绝后保留上一份合法摘要"), PlayerState->GetPublicFishCollection().Num(), 1);

	TestTrue(TEXT("None UnlockId 代表 starter 已授权"), PlayerState->HasServerAuthorizedEquipmentUnlock(NAME_None));
	TestFalse(TEXT("服务器尚未持有清单时非空 UnlockId 拒绝"), PlayerState->HasServerAuthorizedEquipmentUnlock(TEXT("PremiumRod")));
	TestTrue(TEXT("合法解锁清单可写入"), PlayerState->SetAuthorizedEquipmentUnlocksFromAuthority({TEXT("PremiumRod")}));
	TestTrue(TEXT("上报后的 UnlockId 被服务器认可"), PlayerState->HasServerAuthorizedEquipmentUnlock(TEXT("PremiumRod")));
	TestFalse(TEXT("没上报的 UnlockId 仍然拒绝"), PlayerState->HasServerAuthorizedEquipmentUnlock(TEXT("OtherRod")));
	TestFalse(TEXT("含 None 的清单被拒"), PlayerState->SetAuthorizedEquipmentUnlocksFromAuthority({TEXT("PremiumRod"), NAME_None}));
	TestFalse(TEXT("含重复条目的清单被拒"), PlayerState->SetAuthorizedEquipmentUnlocksFromAuthority({TEXT("A"), TEXT("A")}));
	TestEqual(TEXT("拒绝后保留上一份清单"), PlayerState->GetAuthorizedEquipmentUnlocks().Num(), 1);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
