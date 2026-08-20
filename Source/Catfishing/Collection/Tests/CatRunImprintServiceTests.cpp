#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Collection/CatImprintSettings.h"
#include "Collection/CatRunImprintService.h"
#include "Collection/Tests/CatImprintSettingsTestOverride.h"

namespace CatRunImprintServiceTest
{
	/** 本组用例统一使用的触发事件名；它必须同时出现在候选和准入名单里，否则候选会被总清单挡下。 */
	static const FName TestEventType = TEXT("CatchMoment");

	/** 本组用例共用 Collection/Tests 的共享准入守卫；Camp 与 Collection 切片用例也要用它，实现只留一份。 */
	using FImprintSettingsOverride = CatImprintSettingsTest::FImprintSettingsOverride;

	// 候选构造流程：创建服务端正式印记候选的最小不可变事实；参与者列表保留在服务端私有数组中，不进入公共 CapturePlan DTO。
	static FCatImprintCandidate MakeCandidate()
	{
		FCatImprintCandidate Candidate;
		Candidate.CandidateId = FGuid::NewGuid();
		Candidate.RunId = FGuid::NewGuid();
		Candidate.EventType = TestEventType;
		Candidate.SubjectId = FGuid::NewGuid();
		Candidate.FishDefinitionId = TEXT("FishA");
		Candidate.ParticipantCount = 2;
		Candidate.ParticipantStableNetIds = {TEXT("PlayerA"), TEXT("PlayerB")};
		return Candidate;
	}

	// 同局候选构造流程：复用同一 RunId 但换 CandidateId 与 SubjectId，用于把单局上限一条条撑到边界。
	static FCatImprintCandidate MakeCandidateInRun(const FGuid RunId)
	{
		FCatImprintCandidate Candidate = MakeCandidate();
		Candidate.RunId = RunId;
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
	FCatRunImprintServiceEventAllowlistTest,
	"Catfishing.Unit.Collection.RunImprintService.CandidateAdmissionRequiresAllowlistedEvent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatRunImprintServiceRunCapTest,
	"Catfishing.Unit.Collection.RunImprintService.RunImprintCapRejectsOverflowAndFailsClosedWhenUnset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：在 Game World 取得真实 RunImprintService，提交一个候选并用同 CandidateId 重放；一致事实可重放，不一致事实必须 fail-closed。
bool FCatRunImprintServiceCandidateReplayTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatRunImprintServiceTest::FImprintSettingsOverride SettingsOverride({CatRunImprintServiceTest::TestEventType}, 8);

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

	CatRunImprintServiceTest::FImprintSettingsOverride SettingsOverride({CatRunImprintServiceTest::TestEventType}, 8);

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

	CatRunImprintServiceTest::FImprintSettingsOverride SettingsOverride({CatRunImprintServiceTest::TestEventType}, 8);

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

	// 篝火改成每晚都点、不再要求全员在场后，封面计划只看"收件人是不是登记在册的围坐者"。
	FCatImprintCandidate SeatedCandidate = CatRunImprintServiceTest::MakeCandidate();
	TestTrue(TEXT("围坐候选可以提交计划事实"), Service->SubmitImprintCandidate(SeatedCandidate));
	TArray<FCatCapturePlan> CampfirePlans;
	TestTrue(TEXT("围坐者可以创建篝火封面计划"),
		Service->CreateCapturePlansForParticipants(SeatedCandidate.CandidateId,
			{TEXT("PlayerA"), TEXT("PlayerB")}, true, CampfirePlans));
	TArray<FCatCapturePlan> OutsiderPlans;
	TestFalse(TEXT("没围坐的人不能被写进篝火封面计划"),
		Service->CreateCapturePlansForParticipants(SeatedCandidate.CandidateId,
			{TEXT("PlayerC")}, true, OutsiderPlans));

	TestTrue(TEXT("没有待 ACK Grant 时 teardown 可立即完成"), Service->PrepareForRunTeardown());
	TestFalse(TEXT("teardown 后不再接受捕获记录"), Service->CanRecordCommittedCapture());
	TestFalse(TEXT("teardown 后不再接受新候选"), Service->CanAcceptImprintCandidate(CatRunImprintServiceTest::MakeCandidate()));
	return !HasAnyErrors();
}

// 测试流程：先在空清单下提交一个字段完整的候选，再把该事件加进清单重试，最后用一个不在清单里的事件名。
// 锁住的不变量：触发点必须先被印记册收编才能产出印记；空清单是全拒而不是全放；被拒的候选不会留下任何服务端记录，
// 因此清单补齐后同一 CandidateId 仍能作为首次事实进来。
bool FCatRunImprintServiceEventAllowlistTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建准入名单测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建准入名单测试 World"), World);
	if (!World)
	{
		return false;
	}

	UCatRunImprintService* Service = World->GetSubsystem<UCatRunImprintService>();
	TestNotNull(TEXT("可取得准入名单测试服务"), Service);
	if (!Service)
	{
		return false;
	}

	const FCatImprintCandidate Candidate = CatRunImprintServiceTest::MakeCandidate();
	{
		// 空清单：这是项目默认态，代表触发总清单还没被正式登记。
		CatRunImprintServiceTest::FImprintSettingsOverride EmptyAllowlist({}, 8);
		TestEqual(TEXT("空清单下候选被判为策略未裁"),
			Service->EvaluateCandidateAdmission(Candidate), ECatDomainCommandError::PolicyUndecided);
		TestFalse(TEXT("空清单下候选预检不通过"), Service->CanAcceptImprintCandidate(Candidate));
		TestFalse(TEXT("空清单下候选提交被拒"), Service->SubmitImprintCandidate(Candidate));
	}
	{
		CatRunImprintServiceTest::FImprintSettingsOverride Allowlist({CatRunImprintServiceTest::TestEventType}, 8);
		TestEqual(TEXT("清单内事件的候选可以受理"),
			Service->EvaluateCandidateAdmission(Candidate), ECatDomainCommandError::None);
		TestTrue(TEXT("被空清单拒过的同一候选在补齐清单后仍能首次提交"), Service->SubmitImprintCandidate(Candidate));

		FCatImprintCandidate UnlistedCandidate = CatRunImprintServiceTest::MakeCandidate();
		UnlistedCandidate.EventType = TEXT("UnlistedMoment");
		TestEqual(TEXT("清单外事件被判为策略未裁"),
			Service->EvaluateCandidateAdmission(UnlistedCandidate), ECatDomainCommandError::PolicyUndecided);
		TestFalse(TEXT("清单外事件提交被拒"), Service->SubmitImprintCandidate(UnlistedCandidate));

		TArray<FCatCapturePlan> UnlistedPlans;
		TestFalse(TEXT("被清单拒掉的候选不能创建成像计划"),
			Service->CreateCapturePlansForParticipants(UnlistedCandidate.CandidateId,
				UnlistedCandidate.ParticipantStableNetIds, false, UnlistedPlans));
	}
	return !HasAnyErrors();
}

// 测试流程：把单局上限设为 2，在同一 RunId 下连提三条候选；第三条必须被拒且不留记录，换一局则照常可提。
// 再把上限恢复成未配置的 0，验证这时第一条就进不来。
// 锁住的不变量：上限按“本局成像瞬间条数”计，跨局互不干扰；同一候选重放不重复占名额；未配置上限与已拍满走同一条拒绝路径。
bool FCatRunImprintServiceRunCapTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建单局上限测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建单局上限测试 World"), World);
	if (!World)
	{
		return false;
	}

	UCatRunImprintService* Service = World->GetSubsystem<UCatRunImprintService>();
	TestNotNull(TEXT("可取得单局上限测试服务"), Service);
	if (!Service)
	{
		return false;
	}

	const FGuid RunId = FGuid::NewGuid();
	const FCatImprintCandidate First = CatRunImprintServiceTest::MakeCandidateInRun(RunId);
	const FCatImprintCandidate Second = CatRunImprintServiceTest::MakeCandidateInRun(RunId);
	const FCatImprintCandidate Third = CatRunImprintServiceTest::MakeCandidateInRun(RunId);
	{
		CatRunImprintServiceTest::FImprintSettingsOverride CapOverride({CatRunImprintServiceTest::TestEventType}, 2);
		TestTrue(TEXT("上限内第一条候选可提交"), Service->SubmitImprintCandidate(First));
		TestTrue(TEXT("上限内第二条候选可提交"), Service->SubmitImprintCandidate(Second));
		TestEqual(TEXT("已拍满时第三条候选被判为超限"),
			Service->EvaluateCandidateAdmission(Third), ECatDomainCommandError::CapacityExceeded);
		TestFalse(TEXT("已拍满时第三条候选提交被拒"), Service->SubmitImprintCandidate(Third));

		TArray<FCatCapturePlan> OverflowPlans;
		TestFalse(TEXT("超限候选不产生成像计划"),
			Service->CreateCapturePlansForParticipants(Third.CandidateId, Third.ParticipantStableNetIds, false, OverflowPlans));
		TestEqual(TEXT("超限拒绝不产生任何计划"), OverflowPlans.Num(), 0);

		TestTrue(TEXT("已受理候选重放不重复占名额"), Service->SubmitImprintCandidate(First));
		TestFalse(TEXT("重放不会给超限候选放行"), Service->SubmitImprintCandidate(Third));

		const FCatImprintCandidate OtherRunCandidate = CatRunImprintServiceTest::MakeCandidate();
		TestTrue(TEXT("上限按局计，换一局仍可提交候选"), Service->SubmitImprintCandidate(OtherRunCandidate));
	}
	{
		CatRunImprintServiceTest::FImprintSettingsOverride UnsetCapOverride({CatRunImprintServiceTest::TestEventType}, 0);
		const FCatImprintCandidate FreshRunCandidate = CatRunImprintServiceTest::MakeCandidate();
		TestEqual(TEXT("上限未配置时空局的第一条候选也被判为超限"),
			Service->EvaluateCandidateAdmission(FreshRunCandidate), ECatDomainCommandError::CapacityExceeded);
		TestFalse(TEXT("上限未配置时空局的第一条候选提交被拒"), Service->SubmitImprintCandidate(FreshRunCandidate));
	}
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
