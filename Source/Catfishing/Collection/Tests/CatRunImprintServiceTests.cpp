#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Collection/CatRunImprintService.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "OnlineSubsystemTypes.h"

namespace CatRunImprintServiceTest
{
	// 候选构造流程：创建服务端正式印记候选的最小不可变事实；参与者列表保留在服务端私有数组中，不进入公共 CapturePlan DTO。
	static FCatImprintCandidate MakeCandidate(const bool bAllActivePlayersPresent = true)
	{
		FCatImprintCandidate Candidate;
		Candidate.CandidateId = FGuid::NewGuid();
		Candidate.RunId = FGuid::NewGuid();
		Candidate.EventType = TEXT("CatchMoment");
		Candidate.SubjectId = FGuid::NewGuid();
		Candidate.FishDefinitionId = TEXT("FishA");
		Candidate.ParticipantCount = 2;
		Candidate.bAllActivePlayersPresent = bAllActivePlayersPresent;
		Candidate.ParticipantStableNetIds = {TEXT("PlayerA"), TEXT("PlayerB")};
		return Candidate;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatRunImprintServiceCandidateReplayTest,
	"Catfishing.Unit.Collection.RunImprintService.CandidatePreflightSubmitAndReplayContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatRunImprintServiceCapturePlansTest,
	"Catfishing.Unit.Collection.RunImprintService.CapturePlansDeduplicateRecipientsAndReplayStable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatRunImprintServiceTeardownGateTest,
	"Catfishing.Unit.Collection.RunImprintService.TeardownClosesNewCandidatesAndPlans",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatRunImprintServiceUnlockGrantAckTest,
	"Catfishing.Unit.Collection.RunImprintService.UnlockGrantAckAuthorizesEquipment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatRunImprintServiceCaptureResultGrantTest,
	"Catfishing.Unit.Collection.RunImprintService.CaptureResultCreatesImprintGrantAfterSuccess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：在 Game World 取得真实 RunImprintService，提交一个候选并用同 CandidateId 重放；一致事实可重放，不一致事实必须 fail-closed。
bool FCatRunImprintServiceCandidateReplayTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Collection 测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建 Collection 测试 World"), World);
	if (!World)
	{
		return false;
	}

	UCatRunImprintService* Service = World->GetSubsystem<UCatRunImprintService>();
	TestNotNull(TEXT("可取得 RunImprintService"), Service);
	if (!Service)
	{
		return false;
	}

	const FCatImprintCandidate Candidate = CatRunImprintServiceTest::MakeCandidate();
	TestTrue(TEXT("完整候选预检通过"), Service->CanAcceptImprintCandidate(Candidate));
	TestTrue(TEXT("完整候选首次提交通过"), Service->SubmitImprintCandidate(Candidate));
	TestTrue(TEXT("一致候选重放仍通过"), Service->SubmitImprintCandidate(Candidate));

	FCatImprintCandidate MutatedCandidate = Candidate;
	MutatedCandidate.EventType = TEXT("DifferentMoment");
	TestFalse(TEXT("同 CandidateId 的不一致事实被拒绝"), Service->CanAcceptImprintCandidate(MutatedCandidate));

	FCatImprintCandidate DuplicateParticipantCandidate = CatRunImprintServiceTest::MakeCandidate();
	DuplicateParticipantCandidate.ParticipantStableNetIds[1] = TEXT("PlayerA");
	TestFalse(TEXT("重复参与者候选被拒绝"), Service->CanAcceptImprintCandidate(DuplicateParticipantCandidate));
	return !HasAnyErrors();
}

// 测试流程：先提交候选，再用包含重复接收者的列表创建 CapturePlan；服务必须去重、保持 AlbumId 一致，并在同一请求重放时返回同一计划 ID。
bool FCatRunImprintServiceCapturePlansTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 CapturePlan 测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建 CapturePlan 测试 World"), World);
	if (!World)
	{
		return false;
	}

	UCatRunImprintService* Service = World->GetSubsystem<UCatRunImprintService>();
	TestNotNull(TEXT("可取得 CapturePlan 服务"), Service);
	if (!Service)
	{
		return false;
	}

	const FCatImprintCandidate Candidate = CatRunImprintServiceTest::MakeCandidate();
	TestTrue(TEXT("候选提交通过"), Service->SubmitImprintCandidate(Candidate));

	TArray<FCatCapturePlan> FirstPlans;
	TestTrue(TEXT("批量 CapturePlan 创建成功"),
		Service->CreateCapturePlansForParticipants(Candidate.CandidateId,
			{TEXT("PlayerA"), TEXT("PlayerA"), TEXT("PlayerB")}, false, FirstPlans));
	TestEqual(TEXT("重复接收者被去重为两份计划"), FirstPlans.Num(), 2);
	TestTrue(TEXT("第一份计划 ID 有效"), FirstPlans[0].CapturePlanId.IsValid());
	TestTrue(TEXT("第二份计划 ID 有效"), FirstPlans[1].CapturePlanId.IsValid());
	TestEqual(TEXT("两份计划共享同一个 RunAlbumId"), FirstPlans[0].RunAlbumId, FirstPlans[1].RunAlbumId);
	TestEqual(TEXT("计划复制候选 RunId"), FirstPlans[0].RunId, Candidate.RunId);
	TestFalse(TEXT("普通计划不被标成篝火封面"), FirstPlans[0].bCampfireCover);

	TArray<FCatCapturePlan> ReplayPlans;
	TestTrue(TEXT("批量 CapturePlan 重放成功"),
		Service->CreateCapturePlansForParticipants(Candidate.CandidateId,
			{TEXT("PlayerA"), TEXT("PlayerB")}, false, ReplayPlans));
	TestEqual(TEXT("重放仍返回两份计划"), ReplayPlans.Num(), 2);
	TestEqual(TEXT("PlayerA 重放返回同一计划 ID"), ReplayPlans[0].CapturePlanId, FirstPlans[0].CapturePlanId);
	TestEqual(TEXT("PlayerB 重放返回同一计划 ID"), ReplayPlans[1].CapturePlanId, FirstPlans[1].CapturePlanId);

	TArray<FCatCapturePlan> InvalidRecipientPlans;
	TestFalse(TEXT("非候选参与者不能创建计划"),
		Service->CreateCapturePlansForParticipants(Candidate.CandidateId, {TEXT("PlayerC")}, false, InvalidRecipientPlans));
	return !HasAnyErrors();
}

// 测试流程：验证篝火封面必须来自全员在场候选，并在 teardown 后关闭新候选和新计划；服务不能在收口阶段继续生成一局事实。
bool FCatRunImprintServiceTeardownGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Collection teardown 测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建 Collection teardown 测试 World"), World);
	if (!World)
	{
		return false;
	}

	UCatRunImprintService* Service = World->GetSubsystem<UCatRunImprintService>();
	TestNotNull(TEXT("可取得 Collection teardown 服务"), Service);
	if (!Service)
	{
		return false;
	}

	FCatImprintCandidate PartialCandidate = CatRunImprintServiceTest::MakeCandidate(false);
	TestTrue(TEXT("非全员候选本身可以提交普通计划事实"), Service->SubmitImprintCandidate(PartialCandidate));
	TArray<FCatCapturePlan> CampfirePlans;
	TestFalse(TEXT("非全员在场候选不能创建篝火封面计划"),
		Service->CreateCapturePlansForParticipants(PartialCandidate.CandidateId,
			{TEXT("PlayerA"), TEXT("PlayerB")}, true, CampfirePlans));

	TestTrue(TEXT("没有待 ACK Grant 时 teardown 可立即完成"), Service->PrepareForRunTeardown());
	TestFalse(TEXT("teardown 后不再接受捕获记录"), Service->CanRecordCommittedCapture());
	TestFalse(TEXT("teardown 后不再接受新候选"), Service->CanAcceptImprintCandidate(CatRunImprintServiceTest::MakeCandidate()));
	return !HasAnyErrors();
}

// 测试流程：服务器先为稳定接收者生成 Unlock Grant，再要求同一 StableNetId 的 Controller ACK 后才写入 PlayerState 授权；随后用 Profile 解锁摘要 RPC 验证本地 durable 解锁投影也能进入同一授权快照，并只读回查已 ACK Grant 保留原始 UnlockId。
bool FCatRunImprintServiceUnlockGrantAckTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Unlock Grant 测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建 Unlock Grant 测试 World"), World);
	if (!World)
	{
		return false;
	}

	UCatRunImprintService* Service = World->GetSubsystem<UCatRunImprintService>();
	ACatfishingPlayerController* Controller = World->SpawnActor<ACatfishingPlayerController>();
	ACatfishingPlayerState* PlayerState = World->SpawnActor<ACatfishingPlayerState>();
	TestNotNull(TEXT("可取得 RunImprintService"), Service);
	TestNotNull(TEXT("可生成项目 Controller"), Controller);
	TestNotNull(TEXT("可生成项目 PlayerState"), PlayerState);
	if (!Service || !Controller || !PlayerState)
	{
		return false;
	}

	const FString StableNetIdValue = TEXT("UnlockGrantPlayer");
	const FUniqueNetIdRef UniqueId = FUniqueNetIdString::Create(StableNetIdValue, FName(TEXT("CAT_TEST")));
	PlayerState->SetUniqueId(FUniqueNetIdRepl(UniqueId));
	Controller->PlayerState = PlayerState;

	const FName UnlockId(TEXT("PremiumRod"));
	const FGuid GrantId = Service->RecordCommittedUnlock(UnlockId, StableNetIdValue);
	TestTrue(TEXT("服务器创建 Unlock Grant"), GrantId.IsValid());
	TestFalse(TEXT("ACK 前非 starter 解锁未授权"), PlayerState->HasServerAuthorizedEquipmentUnlock(UnlockId));

	Controller->ServerAcknowledgeProfileGrant_Implementation(GrantId);
	TestTrue(TEXT("ACK 后 UnlockId 进入 PlayerState 授权"), PlayerState->HasServerAuthorizedEquipmentUnlock(UnlockId));

	const FName SnapshotUnlockId(TEXT("ProfileSnapshotFloat"));
	TArray<FName> SnapshotUnlocks;
	SnapshotUnlocks.Add(SnapshotUnlockId);
	Controller->ServerPublishEquipmentUnlocks_Implementation(SnapshotUnlocks);
	TestTrue(TEXT("Profile 解锁摘要 RPC 可写入 PlayerState 授权"),
		PlayerState->HasServerAuthorizedEquipmentUnlock(SnapshotUnlockId));

	FCatProfileGrant AcknowledgedGrant;
	TestTrue(TEXT("ACK 后可只读回查 Grant 内容"), Service->TryGetAcknowledgedGrant(GrantId, AcknowledgedGrant));
	TestEqual(TEXT("回查 Grant 保留 UnlockId"), AcknowledgedGrant.UnlockId, UnlockId);
	return !HasAnyErrors();
}

// 测试流程：先在无在线接收者时创建 CapturePlan，再用匹配 StableNetId 的 Controller 回报成像成功并生成独立待 ACK Imprint Grant；测试会核对 Grant 的 Kind、ImprintId、RunAlbumId、封面标记、接收者和未 ACK 阶段，同一计划重复回报必须只返回 AlreadyResolved 且不增 Grant，失败成像则进入 Cancelled 终态并保持 Grant 数量不变。
bool FCatRunImprintServiceCaptureResultGrantTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Imprint Grant 测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建 Imprint Grant 测试 World"), World);
	if (!World)
	{
		return false;
	}

	UCatRunImprintService* Service = World->GetSubsystem<UCatRunImprintService>();
	TestNotNull(TEXT("可取得 Imprint Grant 服务"), Service);
	if (!Service)
	{
		return false;
	}

	const FCatImprintCandidate Candidate = CatRunImprintServiceTest::MakeCandidate();
	TestTrue(TEXT("候选提交通过"), Service->SubmitImprintCandidate(Candidate));
	TArray<FCatCapturePlan> Plans;
	TestTrue(TEXT("为 PlayerA 创建待成像计划"),
		Service->CreateCapturePlansForParticipants(Candidate.CandidateId, {TEXT("PlayerA")}, false, Plans));
	TestEqual(TEXT("只创建一份 PlayerA 计划"), Plans.Num(), 1);
	TestEqual(TEXT("成像前没有待 ACK Grant"), Service->GetPendingGrantAckCount(), 0);

	ACatfishingPlayerController* Controller = World->SpawnActor<ACatfishingPlayerController>();
	ACatfishingPlayerState* PlayerState = World->SpawnActor<ACatfishingPlayerState>();
	TestNotNull(TEXT("可生成 Imprint Grant Controller"), Controller);
	TestNotNull(TEXT("可生成 Imprint Grant PlayerState"), PlayerState);
	if (!Controller || !PlayerState || Plans.Num() != 1)
	{
		return false;
	}
	const FUniqueNetIdRef UniqueId = FUniqueNetIdString::Create(TEXT("PlayerA"), FName(TEXT("CAT_TEST")));
	PlayerState->SetUniqueId(FUniqueNetIdRepl(UniqueId));
	Controller->PlayerState = PlayerState;

	const FGuid ImprintId = FGuid::NewGuid();
	const FCatDomainCommandResult SuccessResult = Service->ReportCaptureResult(
		Controller, Plans[0].CapturePlanId, true, ImprintId);
	TestEqual(TEXT("成功成像结果无错误"), SuccessResult.Error, ECatDomainCommandError::None);
	TestTrue(TEXT("成功成像提交 CapturePlan 终态"), SuccessResult.bCommitted);
	TestEqual(TEXT("成功成像后生成一份待 ACK Imprint Grant"), Service->GetPendingGrantAckCount(), 1);
	TArray<FCatGrantDeliveryRecord> DeliveryRecords;
	Service->CopyGrantDeliveryRecordsForAutomation(DeliveryRecords);
	TestEqual(TEXT("成功成像后只有一份 GrantDeliveryRecord"), DeliveryRecords.Num(), 1);
	if (DeliveryRecords.Num() == 1)
	{
		const FCatProfileGrant& ImprintGrant = DeliveryRecords[0].Grant;
		TestEqual(TEXT("成功成像生成的是 Imprint Grant"), ImprintGrant.Kind, ECatProfileGrantKind::Imprint);
		TestEqual(TEXT("Imprint Grant 保留客户端 durable ImprintId"), ImprintGrant.ImprintId, ImprintId);
		TestEqual(TEXT("Imprint Grant 保留服务端 RunAlbumId"), ImprintGrant.RunAlbumId, Plans[0].RunAlbumId);
		TestEqual(TEXT("普通成像计划不被误标为篝火封面"), ImprintGrant.bRunAlbumCover, Plans[0].bCampfireCover);
		TestEqual(TEXT("Imprint Grant 接收者来自当前 StableNetId"), ImprintGrant.RecipientStableNetId, FString(TEXT("PlayerA")));
		TestTrue(TEXT("Grant 尚未 durable ACK"), DeliveryRecords[0].Stage != ECatGrantDeliveryStage::Acknowledged);
	}

	const FCatDomainCommandResult ReplayResult = Service->ReportCaptureResult(
		Controller, Plans[0].CapturePlanId, true, ImprintId);
	TestEqual(TEXT("重复成像结果返回 AlreadyResolved"), ReplayResult.Error,
		ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("重复成像结果不重复生成 Grant"), Service->GetPendingGrantAckCount(), 1);
	Service->CopyGrantDeliveryRecordsForAutomation(DeliveryRecords);
	TestEqual(TEXT("重复成像结果不重复生成 GrantDeliveryRecord"), DeliveryRecords.Num(), 1);

	FCatImprintCandidate FailedCandidate = CatRunImprintServiceTest::MakeCandidate();
	TestTrue(TEXT("失败候选提交通过"), Service->SubmitImprintCandidate(FailedCandidate));
	TArray<FCatCapturePlan> FailedPlans;
	TestTrue(TEXT("为失败路径创建待成像计划"),
		Service->CreateCapturePlansForParticipants(FailedCandidate.CandidateId, {TEXT("PlayerA")}, false, FailedPlans));
	TestEqual(TEXT("失败路径只创建一份计划"), FailedPlans.Num(), 1);
	if (FailedPlans.Num() == 1)
	{
		const FCatDomainCommandResult FailedResult = Service->ReportCaptureResult(
			Controller, FailedPlans[0].CapturePlanId, false, FGuid());
		TestEqual(TEXT("失败成像结果返回 Cancelled"), FailedResult.Error, ECatDomainCommandError::Cancelled);
		TestTrue(TEXT("失败成像同样提交终态"), FailedResult.bCommitted);
		TestEqual(TEXT("失败成像不会生成新 Grant"), Service->GetPendingGrantAckCount(), 1);
		Service->CopyGrantDeliveryRecordsForAutomation(DeliveryRecords);
		TestEqual(TEXT("失败成像不增加 GrantDeliveryRecord"), DeliveryRecords.Num(), 1);
	}
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
