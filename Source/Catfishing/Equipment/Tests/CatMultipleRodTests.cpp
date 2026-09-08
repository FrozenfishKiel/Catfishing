#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Character/CatCharacter.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "UObject/StrongObjectPtr.h"

namespace CatMultipleRodTests
{
	// 两种型号使用不同耐久；库存、部署、选择、预留与磨损均通过正式 Equipment 入口。
	struct FFixture
	{
		UCatEquipmentSettings* Settings = GetMutableDefault<UCatEquipmentSettings>();
		TArray<TSoftObjectPtr<UCatEquipmentDefinition>> SavedDefinitions = Settings->Definitions;
		ECatDomainPolicy SavedTrustPolicy = Settings->ProfileLoadoutTrustPolicy;
		int32 SavedCapacity = Settings->InventorySlotCapacity;
		int32 SavedStackCapacity = Settings->InventoryQuantityStackCapacity;
		TArray<TStrongObjectPtr<UCatEquipmentDefinition>> Definitions;
		FTestWorldWrapper WorldWrapper;
		UCatEquipmentComponent* Equipment = nullptr;
		UCatEquipmentDefinition* SecondDefinition = nullptr;
		FGuid FirstRodId;
		FGuid SecondRodId;

		~FFixture()
		{
			Settings->Definitions = SavedDefinitions;
			Settings->ProfileLoadoutTrustPolicy = SavedTrustPolicy;
			Settings->InventorySlotCapacity = SavedCapacity;
			Settings->InventoryQuantityStackCapacity = SavedStackCapacity;
		}

		UCatEquipmentDefinition* AddDefinition(const FName Id, const ECatEquipmentKind Kind)
		{
			UCatEquipmentDefinition* Definition = NewObject<UCatEquipmentDefinition>();
			Definitions.Emplace(Definition);
			Definition->EquipmentDefinitionId = Id;
			Definition->Kind = Kind;
			Definition->FunctionalRouteId = Id;
			Definition->LoadoutSlotId = Id;
			Definition->bEnableRuntimeDefinition = true;
			Settings->Definitions.Add(Definition);
			return Definition;
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			Settings->Definitions.Reset();
			Settings->ProfileLoadoutTrustPolicy = ECatDomainPolicy::Enabled;
			Settings->InventorySlotCapacity = 12;
			Settings->InventoryQuantityStackCapacity = 20;
			UCatEquipmentDefinition* First = AddDefinition(TEXT("MultipleRodA"), ECatEquipmentKind::Rod);
			SecondDefinition = AddDefinition(TEXT("MultipleRodB"), ECatEquipmentKind::Rod);
			for (UCatEquipmentDefinition* Rod : {First, SecondDefinition})
			{
				Rod->MaximumRodDurability = Rod == First ? 100.0 : 220.0;
				Rod->MaximumLineLengthCentimeters = 1500.0;
				Rod->HighTensionWearMultiplier = 1.0;
				Rod->UseActorClass = ACatFishingRodActor::StaticClass();
				Rod->UseInventoryEffect = ECatEquipmentUseInventoryEffect::HoldInstanceUntilUnUse;
			}
			AddDefinition(TEXT("MultipleRodFloat"), ECatEquipmentKind::Float)->MaximumCastDistanceCentimeters = 1000.0;
			UCatEquipmentDefinition* Bait = AddDefinition(TEXT("MultipleRodBait"), ECatEquipmentKind::Bait);
			Bait->bRunConsumable = true;
			Bait->BiteRateMultiplier = 1.0;
			Bait->MinimumBiteDelayMultiplier = 1.0;
			for (const auto& Definition : Definitions)
			{
				if (!Test.TestTrue(TEXT("两竿测试定义完整"), Definition->IsRuntimeDefinitionReady())) return false;
			}
			if (!Test.TestTrue(TEXT("创建真实装备 World"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
			WorldWrapper.ForwardErrorMessages(&Test);
			UWorld* World = WorldWrapper.GetTestWorld();
			ACatCharacter* Character = World->SpawnActor<ACatCharacter>();
			ACatfishingPlayerState* PlayerState = World->SpawnActor<ACatfishingPlayerState>();
			if (!Test.TestTrue(TEXT("创建角色和服务器玩家状态"), Character && PlayerState)) return false;
			Character->SetPlayerState(PlayerState);
			Equipment = Character->GetEquipmentComponent();
			if (!Test.TestNotNull(TEXT("角色包含正式装备组件"), Equipment)) return false;
			for (const FName Id : {FName(TEXT("MultipleRodA")), FName(TEXT("MultipleRodB")), FName(TEXT("MultipleRodFloat"))})
			{
				if (!Test.TestTrue(TEXT("授予独立工具实例"), Equipment->GrantEquipmentFromAuthority(
					FGuid::NewGuid(), Equipment->GetSnapshot().Revision, Id).bCommitted)) return false;
			}
			if (!Test.TestTrue(TEXT("授予八份鱼饵"), Equipment->GrantInventoryQuantityFromAuthority(
				FGuid::NewGuid(), Equipment->GetSnapshot().Revision, TEXT("MultipleRodBait"), 8).bCommitted)) return false;
			for (const FCatRunInventorySlot& Slot : Equipment->GetSnapshot().InventorySlots)
			{
				if (Slot.DefinitionId == FName(TEXT("MultipleRodA"))) FirstRodId = Slot.ItemInstanceId;
				if (Slot.DefinitionId == FName(TEXT("MultipleRodB"))) SecondRodId = Slot.ItemInstanceId;
			}
			return Test.TestTrue(TEXT("两竿是不同实物"), FirstRodId.IsValid() && SecondRodId.IsValid() && FirstRodId != SecondRodId);
		}

		FCatDomainCommandResult Select(const FGuid RodId, const FName DefinitionId)
		{
			const FCatEquipmentLoadoutSnapshot Current = Equipment->GetSnapshot();
			return Equipment->ConfigureLoadoutFromAuthority(FGuid::NewGuid(), Current.Revision, DefinitionId,
				Current.BaitDefinitionId, Current.FloatDefinitionId, Current.ScoopNetDefinitionId,
				Current.RodSkinDefinitionId, RodId, Current.BaitItemInstanceId, Current.FloatItemInstanceId,
				Current.ScoopNetItemInstanceId);
		}

		bool Deploy(FAutomationTestBase& Test, const FGuid RodId)
		{
			return Test.TestTrue(TEXT("部署指定真实实例"), Equipment->Use(FGuid::NewGuid(), Equipment->GetSnapshot().Revision, RodId).bCommitted);
		}

		FCatFishingUseReservationResult Begin(const FGuid SessionId, const FGuid RodId, const FName DefinitionId)
		{
			const FCatEquipmentLoadoutSnapshot Current = Equipment->GetSnapshot();
			return Equipment->BeginFishingUse(SessionId, RodId, Current.BaitItemInstanceId, Current.FloatItemInstanceId,
				DefinitionId, Current.BaitDefinitionId, Current.FloatDefinitionId, Current.Revision);
		}

		int32 BaitQuantity() const
		{
			int32 Result = 0;
			for (const FCatRunInventorySlot& Slot : Equipment->GetSnapshot().InventorySlots)
			{
				if (Slot.DefinitionId == FName(TEXT("MultipleRodBait"))) Result += Slot.Quantity;
			}
			return Result;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatMultipleRodSelectionTest,
	"Catfishing.Unit.Equipment.MultipleRods.SelectionCanChangeWhileFirstRodIsDeployed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatMultipleRodSelectionTest::RunTest(const FString& Parameters)
{
	CatMultipleRodTests::FFixture Fixture;
	if (!Fixture.Initialize(*this) || !Fixture.Deploy(*this, Fixture.FirstRodId)) return false;
	UCatEquipmentComponent* Equipment = Fixture.Equipment;
	const FGuid SessionId = FGuid::NewGuid();
	if (!TestTrue(TEXT("原竿可以保有活动会话"), Fixture.Begin(SessionId, Fixture.FirstRodId, TEXT("MultipleRodA")).bReserved)) return false;
	const int64 BeforeInvalidSelection = Equipment->GetSnapshot().Revision;
	TestEqual(TEXT("不能选择并不属于本人的实例"), Fixture.Select(FGuid::NewGuid(), TEXT("MultipleRodB")).Error,
		ECatDomainCommandError::NotFound);
	Fixture.SecondDefinition->RequiredUnlockId = TEXT("MultipleRodLocked");
	TestEqual(TEXT("切换第二根仍检查服务器解锁"), Fixture.Select(Fixture.SecondRodId, TEXT("MultipleRodB")).Error,
		ECatDomainCommandError::PermissionDenied);
	Fixture.SecondDefinition->RequiredUnlockId = NAME_None;
	TestEqual(TEXT("选择拒绝不改库存版本"), Equipment->GetSnapshot().Revision, BeforeInvalidSelection);
	if (!TestTrue(TEXT("原竿部署且有会话时可选择另一根库存竿"), Fixture.Select(Fixture.SecondRodId, TEXT("MultipleRodB")).bCommitted)) return false;
	TestEqual(TEXT("第二根型号成为库存选择"), Equipment->GetSnapshot().RodDefinitionId, FName(TEXT("MultipleRodB")));
	TestEqual(TEXT("选择读取第二根自己的耐久上限"), Equipment->GetSnapshot().RodDurability, 220.0);
	double FirstDurability = 0.0;
	bool bFirstBroken = true;
	TestTrue(TEXT("原会话仍可读取原实例"), Equipment->GetFishingRodDurability(SessionId, FirstDurability, bFirstBroken));
	TestEqual(TEXT("原竿耐久不受选择切换影响"), FirstDurability, 100.0);
	TestTrue(TEXT("原会话仍保持活动"), Equipment->IsFishingUseActive(SessionId));
	TestTrue(TEXT("原会话可独立取消归还鱼饵"), Equipment->ReleaseFishingUse(SessionId).bApplied);
	TestEqual(TEXT("选择切换没有消费鱼饵"), Fixture.BaitQuantity(), 8);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatMultipleRodIndependentSessionsTest,
	"Catfishing.Unit.Equipment.MultipleRods.SessionsKeepInstanceDurabilityAndBaitIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatMultipleRodIndependentSessionsTest::RunTest(const FString& Parameters)
{
	CatMultipleRodTests::FFixture Fixture;
	if (!Fixture.Initialize(*this) || !Fixture.Deploy(*this, Fixture.FirstRodId)
		|| !Fixture.Deploy(*this, Fixture.SecondRodId)) return false;
	UCatEquipmentComponent* Equipment = Fixture.Equipment;
	const FGuid FirstSession = FGuid::NewGuid();
	const FGuid SecondSession = FGuid::NewGuid();
	if (!TestTrue(TEXT("当前选择 B 时仍可绑定已部署 A"), Fixture.Begin(FirstSession, Fixture.FirstRodId, TEXT("MultipleRodA")).bReserved)) return false;
	if (!TestTrue(TEXT("另一根实体竿可同时预留"), Fixture.Begin(SecondSession, Fixture.SecondRodId, TEXT("MultipleRodB")).bReserved)) return false;
	TestEqual(TEXT("两根各预留一份饵"), Fixture.BaitQuantity(), 6);
	const int64 BeforeRejectedBegin = Equipment->GetSnapshot().Revision;
	AddExpectedErrorPlain(TEXT("Event=equipment_rod_session_rejected"), EAutomationExpectedErrorFlags::Contains, 4);
	TestEqual(TEXT("同一实体竿不能同时绑定第二个会话"), Fixture.Begin(FGuid::NewGuid(), Fixture.FirstRodId, TEXT("MultipleRodA")).Error,
		ECatDomainCommandError::InvalidPhase);
	TestEqual(TEXT("已知实例不能伪报另一种型号"), Fixture.Begin(FGuid::NewGuid(), Fixture.FirstRodId, TEXT("MultipleRodB")).Error,
		ECatDomainCommandError::NotFound);
	TestEqual(TEXT("他人实例没有本组件的 Use 权限"), Fixture.Begin(FGuid::NewGuid(), FGuid::NewGuid(), TEXT("MultipleRodA")).Error,
		ECatDomainCommandError::NotFound);
	TestEqual(TEXT("失败预留不推进库存"), Equipment->GetSnapshot().Revision, BeforeRejectedBegin);
	TestEqual(TEXT("失败预留不额外扣饵"), Fixture.BaitQuantity(), 6);
	if (!TestTrue(TEXT("第一场提交自己的鱼饵"), Equipment->CommitFishingBaitDeferred(FirstSession).bApplied)
		|| !TestTrue(TEXT("第二场提交自己的鱼饵"), Equipment->CommitFishingBaitDeferred(SecondSession).bApplied)) return false;
	const auto FirstWear = Equipment->ApplyFishingRodWear(FirstSession, 1, 12.5);
	TestTrue(TEXT("A 的磨损写回 A"), FirstWear.bApplied);
	TestEqual(TEXT("A 按自身 100 耐久扣减"), FirstWear.RemainingRodDurability, 87.5);
	TestEqual(TEXT("A 磨损不覆盖当前 B 的选择读模型"), Equipment->GetSnapshot().RodDurability, 220.0);
	const auto SecondWear = Equipment->ApplyFishingRodWear(SecondSession, 1, 30.0);
	TestTrue(TEXT("B 的磨损写回 B"), SecondWear.bApplied);
	TestEqual(TEXT("B 按自身 220 耐久扣减"), SecondWear.RemainingRodDurability, 190.0);
	TestTrue(TEXT("第一场单独结束"), Equipment->ReleaseFishingUse(FirstSession).bApplied);
	TestTrue(TEXT("第一场结束不释放第二场"), Equipment->IsFishingUseActive(SecondSession));
	TestTrue(TEXT("耗尽当前选择 B"), Equipment->ApplyFishingRodWear(SecondSession, 2, 220.0).bRodBroken);
	TestTrue(TEXT("第二场单独结束"), Equipment->ReleaseFishingUse(SecondSession).bApplied);
	TestTrue(TEXT("当前选择仍是坏 B"), Equipment->GetSnapshot().RodItemInstanceId == Fixture.SecondRodId && Equipment->GetSnapshot().bRodBroken);
	const FGuid NextFirstSession = FGuid::NewGuid();
	if (!TestTrue(TEXT("当前选择坏 B 不应阻断健康 A 再抛"), Fixture.Begin(NextFirstSession, Fixture.FirstRodId, TEXT("MultipleRodA")).bReserved)) return false;
	TestEqual(TEXT("已释放 A 可建立新预留且仅扣一份"), Fixture.BaitQuantity(), 5);
	TestEqual(TEXT("坏 B 自己不能重新预留"), Fixture.Begin(FGuid::NewGuid(), Fixture.SecondRodId, TEXT("MultipleRodB")).Error,
		ECatDomainCommandError::InvalidPhase);
	TestTrue(TEXT("A 新会话取消仅归还自己一份饵"), Equipment->ReleaseFishingUse(NextFirstSession).bApplied);
	TestEqual(TEXT("两场已消耗鱼饵不会被其他取消退回"), Fixture.BaitQuantity(), 6);
	const auto FirstReturned = Equipment->UnUse(FGuid::NewGuid(), Fixture.FirstRodId);
	const auto SecondReturned = Equipment->UnUse(FGuid::NewGuid(), Fixture.SecondRodId);
	TestTrue(TEXT("两根实体均可独立收回"), FirstReturned.bCommitted && SecondReturned.bCommitted);
	TestEqual(TEXT("A 原实例保留累计耐久"), FirstReturned.Item.RodDurability, 87.5);
	TestEqual(TEXT("B 原实例保留耗尽状态"), SecondReturned.Item.RodDurability, 0.0);
	TestTrue(TEXT("B 破损不会污染 A"), SecondReturned.Item.bRodBroken && !FirstReturned.Item.bRodBroken);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatMultipleRodDeploymentCandidateTest,
	"Catfishing.Unit.Equipment.MultipleRods.DeploymentCandidateIsReadOnlyAndSkipsUsedOrBrokenRods",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatMultipleRodDeploymentCandidateTest::RunTest(const FString& Parameters)
{
	CatMultipleRodTests::FFixture Fixture;
	if (!Fixture.Initialize(*this)) return false;
	UCatEquipmentComponent* Equipment = Fixture.Equipment;
	if (!TestTrue(TEXT("手工优先选择库存后面的 B"), Fixture.Select(Fixture.SecondRodId, TEXT("MultipleRodB")).bCommitted)) return false;
	const int64 InitialRevision = Equipment->GetSnapshot().Revision;
	FCatRunInventorySlot Candidate;
	TestTrue(TEXT("存在可部署库存竿"), Equipment->TryGetInventoryRodForDeployment(Candidate));
	TestEqual(TEXT("候选优先当前 B 而非库存最前 A"), Candidate.ItemInstanceId, Fixture.SecondRodId);
	TestEqual(TEXT("查询候选不改版本"), Equipment->GetSnapshot().Revision, InitialRevision);
	if (!Fixture.Deploy(*this, Fixture.SecondRodId)) return false;
	const int64 DeployedRevision = Equipment->GetSnapshot().Revision;
	TestTrue(TEXT("选中 B 已部署时发现库存 A"), Equipment->TryGetInventoryRodForDeployment(Candidate));
	TestEqual(TEXT("不会重复选已 Use 的 B"), Candidate.ItemInstanceId, Fixture.FirstRodId);
	TestEqual(TEXT("回退查询仍不切换玩家选择"), Equipment->GetSnapshot().RodItemInstanceId, Fixture.SecondRodId);
	TestEqual(TEXT("回退查询不改版本"), Equipment->GetSnapshot().Revision, DeployedRevision);
	if (!Fixture.Deploy(*this, Fixture.FirstRodId)) return false;
	TestFalse(TEXT("两根均部署时不存在第三份库存竿"), Equipment->TryGetInventoryRodForDeployment(Candidate));
	TestFalse(TEXT("失败清除之前的候选实例"), Candidate.ItemInstanceId.IsValid());
	for (const FGuid RodId : {Fixture.FirstRodId, Fixture.SecondRodId})
	{
		const FName DefinitionId = RodId == Fixture.FirstRodId ? FName(TEXT("MultipleRodA")) : FName(TEXT("MultipleRodB"));
		const FGuid SessionId = FGuid::NewGuid();
		if (!TestTrue(TEXT("为坏竿候选检查创建真实会话"), Fixture.Begin(SessionId, RodId, DefinitionId).bReserved)
			|| !TestTrue(TEXT("提交会话鱼饵"), Equipment->CommitFishingBaitDeferred(SessionId).bApplied)
			|| !TestTrue(TEXT("实际磨损耗尽实例"), Equipment->ApplyFishingRodWear(SessionId, 1, 250.0).bRodBroken)
			|| !TestTrue(TEXT("释放耗尽实例会话"), Equipment->ReleaseFishingUse(SessionId).bApplied)
			|| !TestTrue(TEXT("坏竿原样收回库存"), Equipment->UnUse(FGuid::NewGuid(), RodId).bCommitted)) return false;
		TestFalse(TEXT("库存只出现坏竿时无可部署候选"), Equipment->TryGetInventoryRodForDeployment(Candidate));
	}
	TestEqual(TEXT("查询候选从未授予第三根竿"), Equipment->GetSnapshot().InventorySlots.FilterByPredicate(
		[](const FCatRunInventorySlot& Slot) { return Slot.ItemInstanceId.IsValid() && Slot.bRodBroken; }).Num(), 2);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatMultipleRodReentrantPublicationTest,
	"Catfishing.Unit.Equipment.MultipleRods.PublicationReentryPreservesCommittedResultsAndReleaseState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatMultipleRodReentrantPublicationTest::RunTest(const FString& Parameters)
{
	CatMultipleRodTests::FFixture Fixture;
	if (!Fixture.Initialize(*this) || !Fixture.Deploy(*this, Fixture.FirstRodId)) return false;
	UCatEquipmentComponent* Equipment = Fixture.Equipment;
	const FGuid FirstSession = FGuid::NewGuid();
	FGuid SecondSession = FGuid::NewGuid();
	bool bBeginReentered = false;
	const int64 BeforeBegin = Equipment->GetSnapshot().Revision;
	const FDelegateHandle BeginObserver = Equipment->OnSnapshotChanged.AddLambda([&]()
	{
		if (bBeginReentered) return;
		bBeginReentered = true;
		TestTrue(TEXT("Begin 广播时第一根的预留已提交"), Equipment->IsFishingUseActive(FirstSession));
		// 第二根 Use 会修改部署实例表，随后 Begin 再修改会话表；外层不能保留表内地址跨过广播。
		if (!Fixture.Deploy(*this, Fixture.SecondRodId)) return;
		TestTrue(TEXT("Begin 回调可给第二根创建独立预留"), Fixture.Begin(
			SecondSession, Fixture.SecondRodId, TEXT("MultipleRodB")).bReserved);
	});
	const auto FirstBegin = Fixture.Begin(FirstSession, Fixture.FirstRodId, TEXT("MultipleRodA"));
	Equipment->OnSnapshotChanged.Remove(BeginObserver);
	TestTrue(TEXT("第一根 Begin 与回调内第二根 Begin 都成功"), FirstBegin.bReserved && bBeginReentered
		&& Equipment->IsFishingUseActive(SecondSession));
	TestEqual(TEXT("外层 Begin 回执冻结自己的提交版本"), FirstBegin.EquipmentRevision, BeforeBegin + 1);
	TestEqual(TEXT("外层 Begin 回执保留 A 的耐久"), FirstBegin.RemainingRodDurability, 100.0);
	TestEqual(TEXT("两根嵌套预留只扣两份饵"), Fixture.BaitQuantity(), 6);
	if (!TestTrue(TEXT("第一根确认消耗鱼饵以进行磨损"), Equipment->CommitFishingBaitDeferred(FirstSession).bApplied)) return false;

	bool bWearReentered = false;
	const int64 BeforeWear = Equipment->GetSnapshot().Revision;
	const FDelegateHandle WearObserver = Equipment->OnSnapshotChanged.AddLambda([&]()
	{
		if (bWearReentered) return;
		bWearReentered = true;
		// 同一根 B 连续取消并重抛产生新的会话终态，保证跨过会话 TMap 的扩容边界。
		for (int32 Index = 0; Index < 20; ++Index)
		{
			if (!TestTrue(TEXT("磨损回调可结束 B 的旧预留"), Equipment->ReleaseFishingUse(SecondSession).bApplied)) return;
			SecondSession = FGuid::NewGuid();
			if (!TestTrue(TEXT("磨损回调可重新预留 B"), Fixture.Begin(
				SecondSession, Fixture.SecondRodId, TEXT("MultipleRodB")).bReserved)) return;
		}
	});
	const auto FirstWear = Equipment->ApplyFishingRodWear(FirstSession, 1, 12.5);
	Equipment->OnSnapshotChanged.Remove(WearObserver);
	TestTrue(TEXT("磨损广播已执行跨会话重入"), FirstWear.bApplied && bWearReentered);
	TestEqual(TEXT("磨损回执冻结自己的提交版本"), FirstWear.EquipmentRevision, BeforeWear + 1);
	TestEqual(TEXT("磨损回执仍属于 A"), FirstWear.RemainingRodDurability, 87.5);
	TestEqual(TEXT("磨损回执仍保留正确序号"), FirstWear.WearSequence, int64(1));
	TestEqual(TEXT("反复取消 B 不额外消费鱼饵"), Fixture.BaitQuantity(), 6);
	if (!TestTrue(TEXT("结束 A 已消耗会话"), Equipment->ReleaseFishingUse(FirstSession).bApplied)) return false;
	const FGuid PendingFirstSession = FGuid::NewGuid();
	if (!TestTrue(TEXT("A 新会话保持未消耗以验证归还广播"), Fixture.Begin(
		PendingFirstSession, Fixture.FirstRodId, TEXT("MultipleRodA")).bReserved)) return false;

	bool bReleaseReentered = false;
	const int64 BeforeRelease = Equipment->GetSnapshot().Revision;
	const FDelegateHandle ReleaseObserver = Equipment->OnSnapshotChanged.AddLambda([&]()
	{
		if (bReleaseReentered) return;
		bReleaseReentered = true;
		TestFalse(TEXT("归还广播之前 A 记录已经释放"), Equipment->IsFishingUseActive(PendingFirstSession));
		const auto Replay = Equipment->ReleaseFishingUse(PendingFirstSession);
		TestEqual(TEXT("归还回调重放同一释放只返回终态"), Replay.Error, ECatDomainCommandError::AlreadyResolved);
		TestFalse(TEXT("归还重放没有第二次提交"), Replay.bApplied);
		if (!TestTrue(TEXT("归还 A 回调内也可结束 B"), Equipment->ReleaseFishingUse(SecondSession).bApplied)) return;
		SecondSession = FGuid::NewGuid();
		TestTrue(TEXT("归还 A 回调内可继续给 B 预留"), Fixture.Begin(
			SecondSession, Fixture.SecondRodId, TEXT("MultipleRodB")).bReserved);
	});
	const auto FirstRelease = Equipment->ReleaseFishingUse(PendingFirstSession);
	Equipment->OnSnapshotChanged.Remove(ReleaseObserver);
	TestTrue(TEXT("归还广播执行完成"), FirstRelease.bApplied && bReleaseReentered);
	TestEqual(TEXT("归还回执冻结自己的提交版本"), FirstRelease.EquipmentRevision, BeforeRelease + 1);
	TestEqual(TEXT("归还回执仍读取 A 的耐久"), FirstRelease.RemainingRodDurability, 87.5);
	TestFalse(TEXT("A 旧预留终态没有被回调覆盖"), Equipment->IsFishingUseActive(PendingFirstSession));
	TestTrue(TEXT("B 新预留没有被外层归还误释放"), Equipment->IsFishingUseActive(SecondSession));
	TestTrue(TEXT("最后清理 B 的未消耗鱼饵"), Equipment->ReleaseFishingUse(SecondSession).bApplied);
	TestEqual(TEXT("整个重入链只实际消耗 A 的一份饵"), Fixture.BaitQuantity(), 7);
	return !HasAnyErrors();
}

#endif
