#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingService.h"
#include "GameFramework/PlayerState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingServiceTwoRodsPerPlayerStateTest,
	"Catfishing.Unit.Fishing.Service.RodRegistryAllowsTwoRodsPerPlayerState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingServiceStaleRodUnregisterTest,
	"Catfishing.Unit.Fishing.Service.StaleRodUnregisterCannotRemoveReplacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingServiceSharedRodSlotsTest,
	"Catfishing.Unit.Fishing.Service.SharedRodSlotsAreDiscoverableAndBounded",
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
		FGuid::NewGuid(), FGuid::NewGuid(), TEXT("Rod"), TEXT("Skin"), Owner, Owner, true, false));
	TestTrue(TEXT("shared rod registers under immutable owner"), Fishing->RegisterDeployedRod(Owner, Rod));
	TestEqual(TEXT("owner lookup finds shared rod"), Fishing->FindRodOperatedBy(Owner), Rod);
	const FVector SharedInteractionLocation = Rod->GetOperatorInteractionWorldTransform().GetLocation();
	TestEqual(TEXT("occupied rod exposes its next container position from the shared interaction point"),
		Fishing->FindNearestOperableRod(SharedInteractionLocation, 1.0), Rod);
	int32 JoinedSlot = INDEX_NONE;
	TestTrue(TEXT("helper joins auxiliary slot"), Rod->AddOperatorFromAuthority(Helper, 1, JoinedSlot));
	TestEqual(TEXT("helper occupies slot one"), JoinedSlot, 1);
	TestEqual(TEXT("helper lookup finds someone else's rod"), Fishing->FindRodOperatedBy(Helper), Rod);
	TestNull(TEXT("full two-person rod is not offered as operable"),
		Fishing->FindNearestOperableRod(Rod->GetActorLocation(), 1000.0));
	APlayerState* Promoted = nullptr;
	TestTrue(TEXT("helper leaves auxiliary slot"), Rod->RemoveOperatorFromAuthority(Helper, 2, Promoted));
	TestNull(TEXT("auxiliary departure does not promote anyone"), Promoted);
	TestEqual(TEXT("rod with free container position is offered again from the same interaction point"),
		Fishing->FindNearestOperableRod(SharedInteractionLocation, 1.0), Rod);
	return !HasAnyErrors();
}

// Registry 契约：每人最多两根；相同 Actor 重放不重复计数，玩家之间的部署名额互相独立。
bool FCatFishingServiceTwoRodsPerPlayerStateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建鱼竿 Registry 测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	UCatFishingService* Fishing = World ? World->GetSubsystem<UCatFishingService>() : nullptr;
	APlayerState* PlayerState = World ? World->SpawnActor<APlayerState>() : nullptr;
	APlayerState* OtherPlayerState = World ? World->SpawnActor<APlayerState>() : nullptr;
	ACatFishingRodActor* FirstRod = World ? World->SpawnActor<ACatFishingRodActor>() : nullptr;
	ACatFishingRodActor* SecondRod = World ? World->SpawnActor<ACatFishingRodActor>() : nullptr;
	ACatFishingRodActor* ThirdRod = World ? World->SpawnActor<ACatFishingRodActor>() : nullptr;
	ACatFishingRodActor* OtherFirstRod = World ? World->SpawnActor<ACatFishingRodActor>() : nullptr;
	ACatFishingRodActor* OtherSecondRod = World ? World->SpawnActor<ACatFishingRodActor>() : nullptr;
	TestNotNull(TEXT("真实 FishingService 已创建"), Fishing);
	TestNotNull(TEXT("PlayerState 夹具已创建"), PlayerState);
	TestNotNull(TEXT("第一根鱼竿已创建"), FirstRod);
	TestNotNull(TEXT("第二根鱼竿已创建"), SecondRod);
	TestNotNull(TEXT("第三根鱼竿已创建"), ThirdRod);
	TestNotNull(TEXT("另一玩家已创建"), OtherPlayerState);
	TestNotNull(TEXT("另一玩家第一根鱼竿已创建"), OtherFirstRod);
	TestNotNull(TEXT("另一玩家第二根鱼竿已创建"), OtherSecondRod);
	if (!Fishing || !PlayerState || !OtherPlayerState || !FirstRod || !SecondRod || !ThirdRod
		|| !OtherFirstRod || !OtherSecondRod)
	{
		return false;
	}

	TestTrue(TEXT("首次登记成功"), Fishing->RegisterDeployedRod(PlayerState, FirstRod));
	TestTrue(TEXT("相同鱼竿重放成功"), Fishing->RegisterDeployedRod(PlayerState, FirstRod));
	TestEqual(TEXT("重放不占用第二个名额"), Fishing->GetDeployedRodCount(PlayerState), 1);
	TestTrue(TEXT("同一玩家第二根存活鱼竿登记成功"), Fishing->RegisterDeployedRod(PlayerState, SecondRod));
	TestTrue(TEXT("满额后已登记鱼竿重放仍成功"), Fishing->RegisterDeployedRod(PlayerState, SecondRod));
	TestFalse(TEXT("同一玩家第三根存活鱼竿被拒绝"), Fishing->RegisterDeployedRod(PlayerState, ThirdRod));
	TestEqual(TEXT("本人共有两根存活鱼竿"), Fishing->GetDeployedRodCount(PlayerState), 2);
	TestTrue(TEXT("只读首根查询返回本人两根之一"), Fishing->FindDeployedRod(PlayerState) == FirstRod
		|| Fishing->FindDeployedRod(PlayerState) == SecondRod);
	TestFalse(TEXT("同一 Actor 不能跨玩家重复登记"), Fishing->RegisterDeployedRod(OtherPlayerState, FirstRod));
	TestTrue(TEXT("另一玩家第一根不占本人名额"), Fishing->RegisterDeployedRod(OtherPlayerState, OtherFirstRod));
	TestTrue(TEXT("另一玩家也可登记第二根"), Fishing->RegisterDeployedRod(OtherPlayerState, OtherSecondRod));
	TestEqual(TEXT("另一玩家名额独立计数"), Fishing->GetDeployedRodCount(OtherPlayerState), 2);
	TestEqual(TEXT("两位玩家共有四个存活条目"), Fishing->GetDeployedRodCountForDiagnostics(), 4);
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
	ACatFishingRodActor* RodC = World ? World->SpawnActor<ACatFishingRodActor>() : nullptr;
	TestNotNull(TEXT("真实 FishingService 已创建"), Fishing);
	TestNotNull(TEXT("PlayerState 夹具已创建"), PlayerState);
	TestNotNull(TEXT("鱼竿 A 已创建"), RodA);
	TestNotNull(TEXT("鱼竿 B 已创建"), RodB);
	TestNotNull(TEXT("鱼竿 C 已创建"), RodC);
	if (!Fishing || !PlayerState || !RodA || !RodB || !RodC)
	{
		return false;
	}

	TestTrue(TEXT("登记鱼竿 A"), Fishing->RegisterDeployedRod(PlayerState, RodA));
	TestTrue(TEXT("同主同时登记鱼竿 B"), Fishing->RegisterDeployedRod(PlayerState, RodB));
	Fishing->UnregisterDeployedRod(PlayerState, RodA);
	TestEqual(TEXT("注销 A 不误删同主 B"), Fishing->FindDeployedRod(PlayerState), RodB);
	TestEqual(TEXT("注销 A 只释放一个名额"), Fishing->GetDeployedRodCount(PlayerState), 1);
	TestTrue(TEXT("释放的名额可登记替代竿 C"), Fishing->RegisterDeployedRod(PlayerState, RodC));
	Fishing->UnregisterDeployedRod(PlayerState, RodA);
	TestEqual(TEXT("迟到的 A 注销不删除 B 或 C"), Fishing->GetDeployedRodCount(PlayerState), 2);
	TestEqual(TEXT("迟到注销后仍有两个存活条目"), Fishing->GetDeployedRodCountForDiagnostics(), 2);

	Fishing->UnregisterDeployedRod(PlayerState, RodB);
	TestEqual(TEXT("精确注销 B 后只保留 C"), Fishing->FindDeployedRod(PlayerState), RodC);
	Fishing->UnregisterDeployedRod(PlayerState, RodC);
	TestNull(TEXT("全部精确注销后查询为空"), Fishing->FindDeployedRod(PlayerState));
	TestEqual(TEXT("全部精确注销后 Registry 为空"), Fishing->GetDeployedRodCountForDiagnostics(), 0);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
