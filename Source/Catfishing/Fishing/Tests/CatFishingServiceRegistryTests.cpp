#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingService.h"
#include "GameFramework/PlayerState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingServiceOneRodPerPlayerStateTest,
	"Catfishing.Unit.Fishing.Service.RodRegistryAllowsOneRodPerPlayerState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingServiceStaleRodUnregisterTest,
	"Catfishing.Unit.Fishing.Service.StaleRodUnregisterCannotRemoveReplacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingServiceSharedRodSlotsTest,
	"Catfishing.Unit.Fishing.Service.PrimaryOnlyAdmissionKeepsAuxiliaryLayoutReserved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingServiceSharedRodSlotsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("creates shared rod slot world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	UWorld* World = WorldWrapper.GetTestWorld();
	UCatFishingService* Fishing = World ? World->GetSubsystem<UCatFishingService>() : nullptr;
	APlayerState* Owner = World ? World->SpawnActor<APlayerState>() : nullptr;
	APlayerState* Helper = World ? World->SpawnActor<APlayerState>() : nullptr;
	ACatFishingRodActor* Rod = World ? World->SpawnActor<ACatFishingRodActor>() : nullptr;
	if (!TestNotNull(TEXT("fishing service exists"), Fishing)
		|| !TestNotNull(TEXT("owner exists"), Owner) || !TestNotNull(TEXT("helper exists"), Helper)
		|| !TestNotNull(TEXT("rod exists"), Rod))
	{
		return false;
	}
	TestTrue(TEXT("rod initializes with owner in primary slot"), Rod->InitializeAuthoritativeIdentity(
		FGuid::NewGuid(), TEXT("Rod"), TEXT("Skin"), Owner, Owner, true, false));
	TestTrue(TEXT("shared rod registers under immutable owner"), Fishing->RegisterDeployedRod(Owner, Rod));
	TestEqual(TEXT("owner lookup finds shared rod"), Fishing->FindRodOperatedBy(Owner), Rod);
	int32 JoinedSlot = INDEX_NONE;
	TestTrue(TEXT("helper joins auxiliary slot"), Rod->AddOperatorFromAuthority(Helper, 1, JoinedSlot));
	TestEqual(TEXT("helper occupies slot one"), JoinedSlot, 1);
	TestEqual(TEXT("helper lookup finds someone else's rod"), Fishing->FindRodOperatedBy(Helper), Rod);
	TestNull(TEXT("occupied primary rod is not offered as operable"),
		Fishing->FindNearestOperableRod(Rod->GetActorLocation(), 1000.0));
	APlayerState* Promoted = nullptr;
	TestTrue(TEXT("helper leaves auxiliary slot"), Rod->RemoveOperatorFromAuthority(Helper, 2, Promoted));
	TestNull(TEXT("auxiliary departure does not promote anyone"), Promoted);
	TestNull(TEXT("runtime admission does not expose the reserved auxiliary slot"),
		Fishing->FindNearestOperableRod(Rod->GetOperatorStandWorldTransform(1).GetLocation(), 1000.0));
	return !HasAnyErrors();
}

// Registry 契约：首次登记与相同 Actor 重放成功，同一 PlayerState 的第二根存活鱼竿被拒绝。
bool FCatFishingServiceOneRodPerPlayerStateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建鱼竿 Registry 测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	UCatFishingService* Fishing = World ? World->GetSubsystem<UCatFishingService>() : nullptr;
	APlayerState* PlayerState = World ? World->SpawnActor<APlayerState>() : nullptr;
	ACatFishingRodActor* FirstRod = World ? World->SpawnActor<ACatFishingRodActor>() : nullptr;
	ACatFishingRodActor* SecondRod = World ? World->SpawnActor<ACatFishingRodActor>() : nullptr;
	TestNotNull(TEXT("真实 FishingService 已创建"), Fishing);
	TestNotNull(TEXT("PlayerState 夹具已创建"), PlayerState);
	TestNotNull(TEXT("第一根鱼竿已创建"), FirstRod);
	TestNotNull(TEXT("第二根鱼竿已创建"), SecondRod);
	if (!Fishing || !PlayerState || !FirstRod || !SecondRod)
	{
		return false;
	}

	TestTrue(TEXT("首次登记成功"), Fishing->RegisterDeployedRod(PlayerState, FirstRod));
	TestTrue(TEXT("相同鱼竿重放成功"), Fishing->RegisterDeployedRod(PlayerState, FirstRod));
	TestFalse(TEXT("同一玩家的不同存活鱼竿被拒绝"), Fishing->RegisterDeployedRod(PlayerState, SecondRod));
	TestEqual(TEXT("查询保持返回第一根鱼竿"), Fishing->FindDeployedRod(PlayerState), FirstRod);
	TestEqual(TEXT("Registry 只有一个存活条目"), Fishing->GetDeployedRodCountForDiagnostics(), 1);
	return !HasAnyErrors();
}

// 精确注销契约：旧鱼竿迟到的 EndPlay 注销不能删除同一玩家后来登记的替代鱼竿。
bool FCatFishingServiceStaleRodUnregisterTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建迟到注销测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	UCatFishingService* Fishing = World ? World->GetSubsystem<UCatFishingService>() : nullptr;
	APlayerState* PlayerState = World ? World->SpawnActor<APlayerState>() : nullptr;
	ACatFishingRodActor* RodA = World ? World->SpawnActor<ACatFishingRodActor>() : nullptr;
	ACatFishingRodActor* RodB = World ? World->SpawnActor<ACatFishingRodActor>() : nullptr;
	TestNotNull(TEXT("真实 FishingService 已创建"), Fishing);
	TestNotNull(TEXT("PlayerState 夹具已创建"), PlayerState);
	TestNotNull(TEXT("鱼竿 A 已创建"), RodA);
	TestNotNull(TEXT("鱼竿 B 已创建"), RodB);
	if (!Fishing || !PlayerState || !RodA || !RodB)
	{
		return false;
	}

	TestTrue(TEXT("登记鱼竿 A"), Fishing->RegisterDeployedRod(PlayerState, RodA));
	Fishing->UnregisterDeployedRod(PlayerState, RodA);
	TestTrue(TEXT("精确注销 A 后可登记 B"), Fishing->RegisterDeployedRod(PlayerState, RodB));
	Fishing->UnregisterDeployedRod(PlayerState, RodA);
	TestEqual(TEXT("迟到的 A 注销不删除 B"), Fishing->FindDeployedRod(PlayerState), RodB);
	TestEqual(TEXT("迟到注销后仍有一个存活条目"), Fishing->GetDeployedRodCountForDiagnostics(), 1);

	Fishing->UnregisterDeployedRod(PlayerState, RodB);
	TestNull(TEXT("精确注销 B 后查询为空"), Fishing->FindDeployedRod(PlayerState));
	TestEqual(TEXT("精确注销 B 后 Registry 为空"), Fishing->GetDeployedRodCountForDiagnostics(), 0);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
