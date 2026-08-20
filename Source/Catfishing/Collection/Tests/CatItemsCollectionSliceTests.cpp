#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Camp/CatCampSettings.h"
#include "Collection/CatRunImprintService.h"
#include "Collection/Tests/CatImprintSettingsTestOverride.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Items/CatContainerReplicationComponent.h"
#include "Items/CatItemsService.h"
#include "Net/OnlineEngineInterface.h"
#include "OnlineSubsystemTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatItemsCollectionCommitCaptureSliceTest,
	"Catfishing.Slice.ItemsCollection.CommitCaptureRecordsSingleFishGrant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatRunImprintCaptureResultPayloadDriftTest,
	"Catfishing.Unit.Collection.RunImprint.CaptureResultPayloadDriftRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatRunImprintCaptureRecordTrackSplitTest,
	"Catfishing.Unit.Collection.RunImprint.CaptureRecordSplitsFisherAndScooperTracks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatRunImprintSettlementWithoutCampfireCoverTest,
	"Catfishing.Unit.Collection.RunImprint.SettlementCompletesWhenCampfireCoverEventUnset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatItemsCollectionSliceTest
{
	/** 已注册个人鱼护的切片上下文；它把真实 Actor 宿主、复制组件、容器 ID 和服务器身份绑成一次测试输入。 */
	struct FPersonalGuardFixture
	{
		/** authority Actor 宿主；复制组件依附它取得真实 Owner 生命周期。 */
		TObjectPtr<AActor> Owner = nullptr;

		/** Items 服务发布公开快照的正式组件；切片测试不直接写它的数组。 */
		TObjectPtr<UCatContainerReplicationComponent> Component = nullptr;

		/** 本次测试容器的稳定 ID；Items 命令通过它定位个人鱼护聚合。 */
		FGuid ContainerId;

		/** 服务器私有玩家身份；Items owner 校验与 Collection grant 接收者使用同一个值。 */
		FString StableNetId;
	};

	// 注册流程：创建真实 Actor 与正式复制组件，再通过 Items public API 注册个人鱼护；失败时返回空组件并让调用方断言。
	static FPersonalGuardFixture RegisterPersonalGuard(UWorld* World, UCatItemsService* ItemsService)
	{
		FPersonalGuardFixture Fixture;
		Fixture.Owner = World ? World->SpawnActor<AActor>() : nullptr;
		Fixture.Component = Fixture.Owner ? NewObject<UCatContainerReplicationComponent>(Fixture.Owner) : nullptr;
		Fixture.ContainerId = FGuid::NewGuid();
		Fixture.StableNetId = TEXT("SlicePlayerA");
		if (Fixture.Owner && Fixture.Component && ItemsService)
		{
			Fixture.Owner->AddInstanceComponent(Fixture.Component);
			Fixture.Component->RegisterComponent();
			ItemsService->RegisterContainer(Fixture.Component, Fixture.ContainerId,
				ECatContainerKind::PersonalGuard, Fixture.StableNetId, 2);
		}
		return Fixture;
	}

	// 捕获命令流程：构造一条合法实物鱼提交；ExpectedRevision 固定为注册后的初始 Revision=1，保证切片从真实 Items 写口进入。
	static FCatCaptureCommitCommand MakeCaptureCommand(const FPersonalGuardFixture& Guard)
	{
		FCatCaptureCommitCommand Command;
		Command.Context.RequestId = FGuid::NewGuid();
		Command.Context.ExpectedRevision = 1;
		Command.Context.StableNetId = Guard.StableNetId;
		Command.FishingSessionId = FGuid::NewGuid();
		Command.FishInstanceId = FGuid::NewGuid();
		Command.FishDefinitionId = TEXT("SliceFish");
		Command.TargetContainerId = Guard.ContainerId;
		Command.WeightKilograms = 3.25;
		Command.SacrificeContribution = 2;
		return Command;
	}

	// committed DTO 构造流程：直接拼一份字段完整的捕获终态，让记录归约用例不必再跑一遍 Items 事务；
	// 两轨归约只读 CaptureRequestId、FishInstanceId、FishDefinitionId 和重量这四项，因此其余字段留默认值不影响断言。
	static FCatCaptureCommittedResult MakeCommittedCapture(const FName FishDefinitionId)
	{
		FCatCaptureCommittedResult Capture;
		Capture.CaptureRequestId = FGuid::NewGuid();
		Capture.FishingSessionId = FGuid::NewGuid();
		Capture.FishInstance.FishInstanceId = FGuid::NewGuid();
		Capture.FishInstance.FishDefinitionId = FishDefinitionId;
		Capture.FishInstance.WeightKilograms = 3.25;
		return Capture;
	}

	/** 临时改写营地篝火封面事件配置的守卫；结算归档评估读取 GetDefault，因此用例必须在结束时还原项目默认值。 */
	struct FCampfireCoverEventOverride
	{
		/** 被结算归档评估读取的默认营地配置对象；只在本用例生命周期内改写。 */
		UCatCampSettings* Settings = GetMutableDefault<UCatCampSettings>();

		/** 进入用例前的项目默认封面事件 ID；析构时原样写回，不落配置文件。 */
		FName OldCampfireCoverEventId = NAME_None;

		/** 构造流程：只记录项目当前默认值，不预设任何配置，由用例显式声明每一段要测的配置状态。 */
		FCampfireCoverEventOverride()
		{
			if (Settings)
			{
				OldCampfireCoverEventId = Settings->CampfireCoverEventId;
			}
		}

		/** 设置流程：把默认对象改成用例这一段需要的封面事件配置；空 ID 表示产品尚未裁定封面事件。 */
		void SetCampfireCoverEventId(const FName EventId) const
		{
			if (Settings)
			{
				Settings->CampfireCoverEventId = EventId;
			}
		}

		/** 析构流程：恢复项目默认封面事件配置，避免影响营地用例和项目默认值用例。 */
		~FCampfireCoverEventOverride()
		{
			if (Settings)
			{
				Settings->CampfireCoverEventId = OldCampfireCoverEventId;
			}
		}
	};

	/** 测试身份装配流程：创建 PlayerState 并写入稳定 UniqueId；RunImprint 结果回报只从 Controller 当前身份重建接收者。 */
	static bool AttachStablePlayerState(UWorld* World, APlayerController* Controller, const FString& StableNetId)
	{
		if (!World || !Controller || StableNetId.IsEmpty())
		{
			return false;
		}
		APlayerState* PlayerState = World->SpawnActor<APlayerState>();
		if (!PlayerState)
		{
			return false;
		}
		const FUniqueNetIdRef UniqueId = FUniqueNetIdString::Create(StableNetId,
			UOnlineEngineInterface::Get()->GetDefaultOnlineSubsystemName());
		PlayerState->SetUniqueId(FUniqueNetIdRepl(UniqueId));
		Controller->SetPlayerState(PlayerState);
		return true;
	}
}

// 测试流程：用 Items 提交一条真实实物鱼，再把同一 committed 捕获事实交给 Collection 归档；重放必须返回同一 GrantId，pending ACK 数量不能增加。
bool FCatItemsCollectionCommitCaptureSliceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	WorldWrapper.ForwardErrorMessages(this);
	if (!TestTrue(TEXT("创建 Items→Collection 切片测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}

	UWorld* World = WorldWrapper.GetTestWorld();
	UCatItemsService* ItemsService = World ? World->GetSubsystem<UCatItemsService>() : nullptr;
	UCatRunImprintService* ImprintService = World ? World->GetSubsystem<UCatRunImprintService>() : nullptr;
	TestNotNull(TEXT("切片 World 可创建"), World);
	TestNotNull(TEXT("可取得 ItemsService"), ItemsService);
	TestNotNull(TEXT("可取得 RunImprintService"), ImprintService);
	if (!World || !ItemsService || !ImprintService)
	{
		return false;
	}

	const CatItemsCollectionSliceTest::FPersonalGuardFixture Guard =
		CatItemsCollectionSliceTest::RegisterPersonalGuard(World, ItemsService);
	TestNotNull(TEXT("个人鱼护组件已创建"), Guard.Component.Get());

	const FCatCaptureCommitCommand CaptureCommand = CatItemsCollectionSliceTest::MakeCaptureCommand(Guard);
	const FCatCaptureCommitResult CaptureResult = ItemsService->CommitCapture(CaptureCommand);
	TestTrue(TEXT("Items 捕获提交成功"), CaptureResult.Command.bCommitted);
	TestEqual(TEXT("Items 捕获返回 None"), CaptureResult.Command.Error, ECatDomainCommandError::None);
	TestEqual(TEXT("Committed 保留 Capture RequestId"), CaptureResult.Committed.CaptureRequestId,
		CaptureCommand.Context.RequestId);
	TestEqual(TEXT("Committed 保留 FishDefinition"), CaptureResult.Committed.FishInstance.FishDefinitionId,
		CaptureCommand.FishDefinitionId);

	FCatCaptureConditionSnapshot Condition;
	Condition.RegionId = TEXT("SliceRegion");
	Condition.TimeOfDayId = TEXT("Day");
	Condition.WeatherId = TEXT("Clear");

	TestTrue(TEXT("Collection 当前允许记录 committed capture"), ImprintService->CanRecordCommittedCapture());
	const FGuid FirstGrantId = ImprintService->RecordCommittedCapture(
		CaptureResult.Committed, Guard.StableNetId, Condition);
	TestTrue(TEXT("Collection 生成 FishRecorded GrantId"), FirstGrantId.IsValid());
	TestEqual(TEXT("首个 Grant 等待 durable ACK"), ImprintService->GetPendingGrantAckCount(), 1);
	TestFalse(TEXT("仍有 Grant 未 ACK"), ImprintService->AreAllGrantAcksComplete());

	FCatCaptureCommittedResult DriftCapture = CaptureResult.Committed;
	DriftCapture.FishInstance.FishDefinitionId = TEXT("SliceFishDrift");
	const FGuid DriftGrantId = ImprintService->RecordCommittedCapture(
		DriftCapture, Guard.StableNetId, Condition);
	TestFalse(TEXT("同一 CaptureRequestId 更换鱼定义不返回旧 Grant"), DriftGrantId.IsValid());
	TestEqual(TEXT("捕获归档载荷漂移不增加 pending Grant 数量"), ImprintService->GetPendingGrantAckCount(), 1);

	const FGuid ReplayGrantId = ImprintService->RecordCommittedCapture(
		CaptureResult.Committed, Guard.StableNetId, Condition);
	TestEqual(TEXT("同一 CaptureRequestId 重放返回同一 GrantId"), ReplayGrantId, FirstGrantId);
	TestEqual(TEXT("重放不增加 pending Grant 数量"), ImprintService->GetPendingGrantAckCount(), 1);
	TestFalse(TEXT("teardown 前仍需等待 durable ACK"), ImprintService->PrepareForRunTeardown());
	TestEqual(TEXT("teardown 重投不复制 Grant"), ImprintService->GetPendingGrantAckCount(), 1);

	ItemsService->UnregisterContainer(Guard.Component.Get());
	FCatContainerSnapshot RemovedSnapshot;
	TestFalse(TEXT("切片结束后容器已注销"), ItemsService->TryGetContainerSnapshot(Guard.ContainerId, RemovedSnapshot));
	return !HasAnyErrors();
}

// 测试流程：用真实 Imprint 候选创建两个 CapturePlan；成功计划只能用同一 ImprintId 重放，失败计划只能以失败重放，终态
// 后结果漂移必须暴露 InvalidPayload。
bool FCatRunImprintCaptureResultPayloadDriftTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	// 本用例锁的是成像结果的载荷漂移判定，前提是候选先能被接受。
	// 项目默认的印记触发清单是 fail-closed 的空清单，不显式放行这两个事件名，
	// 候选在 SubmitImprintCandidate 就被判 PolicyUndecided，后面的漂移断言一条也走不到。
	const CatImprintSettingsTest::FImprintSettingsOverride ImprintGuard(
		{TEXT("CaptureSuccess"), TEXT("CaptureFailed")}, 8);
	FTestWorldWrapper WorldWrapper;
	WorldWrapper.ForwardErrorMessages(this);
	if (!TestTrue(TEXT("创建 RunImprint 成像结果测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}

	UWorld* World = WorldWrapper.GetTestWorld();
	UCatRunImprintService* ImprintService = World ? World->GetSubsystem<UCatRunImprintService>() : nullptr;
	APlayerController* Controller = World ? World->SpawnActor<APlayerController>() : nullptr;
	TestNotNull(TEXT("成像结果测试 World 可创建"), World);
	TestNotNull(TEXT("可取得 RunImprintService"), ImprintService);
	TestNotNull(TEXT("可生成报告 Controller"), Controller);
	if (!World || !ImprintService || !Controller)
	{
		return false;
	}

	const FString StableNetId(TEXT("SlicePlayerA"));
	TestTrue(TEXT("报告 Controller 绑定稳定身份"), CatItemsCollectionSliceTest::AttachStablePlayerState(
		World, Controller, StableNetId));

	FCatImprintCandidate SuccessCandidate;
	SuccessCandidate.CandidateId = FGuid::NewGuid();
	SuccessCandidate.RunId = FGuid::NewGuid();
	SuccessCandidate.EventType = TEXT("CaptureSuccess");
	SuccessCandidate.SubjectId = FGuid::NewGuid();
	SuccessCandidate.FishDefinitionId = TEXT("SliceFish");
	SuccessCandidate.ParticipantCount = 1;
	SuccessCandidate.ParticipantStableNetIds = {StableNetId};
	TestTrue(TEXT("成功成像候选可提交"), ImprintService->SubmitImprintCandidate(SuccessCandidate));
	TArray<FCatCapturePlan> SuccessPlans;
	TestTrue(TEXT("成功成像候选可生成 CapturePlan"), ImprintService->CreateCapturePlansForParticipants(
		SuccessCandidate.CandidateId, {StableNetId}, false, SuccessPlans));
	TestEqual(TEXT("单一收件人只得到一份计划"), SuccessPlans.Num(), 1);
	const FCatCapturePlan SuccessPlan = SuccessPlans.IsEmpty() ? FCatCapturePlan() : SuccessPlans[0];
	TestTrue(TEXT("成功成像计划带有有效 PlanId"), SuccessPlan.CapturePlanId.IsValid());

	const FGuid ImprintId = FGuid::NewGuid();
	const FCatDomainCommandResult SuccessResult = ImprintService->ReportCaptureResult(
		Controller, SuccessPlan.CapturePlanId, true, ImprintId);
	TestTrue(TEXT("首次成功成像提交 Grant"), SuccessResult.bCommitted);
	TestEqual(TEXT("首次成功成像返回 None"), SuccessResult.Error, ECatDomainCommandError::None);

	const FCatDomainCommandResult SuccessReplay = ImprintService->ReportCaptureResult(
		Controller, SuccessPlan.CapturePlanId, true, ImprintId);
	TestFalse(TEXT("成功结果原样重放不再次提交"), SuccessReplay.bCommitted);
	TestEqual(TEXT("成功结果原样重放返回 AlreadyResolved"), SuccessReplay.Error,
		ECatDomainCommandError::AlreadyResolved);

	const FCatDomainCommandResult ImprintDrift = ImprintService->ReportCaptureResult(
		Controller, SuccessPlan.CapturePlanId, true, FGuid::NewGuid());
	TestFalse(TEXT("成功计划同 PlanId 更换 ImprintId 不提交"), ImprintDrift.bCommitted);
	TestEqual(TEXT("成功计划同 PlanId 更换 ImprintId 返回 InvalidPayload"), ImprintDrift.Error,
		ECatDomainCommandError::InvalidPayload);

	const FCatDomainCommandResult FailureAfterSuccess = ImprintService->ReportCaptureResult(
		Controller, SuccessPlan.CapturePlanId, false, FGuid());
	TestFalse(TEXT("成功计划不能再漂移成失败"), FailureAfterSuccess.bCommitted);
	TestEqual(TEXT("成功计划漂移成失败返回 InvalidPayload"), FailureAfterSuccess.Error,
		ECatDomainCommandError::InvalidPayload);

	FCatImprintCandidate FailedCandidate = SuccessCandidate;
	FailedCandidate.CandidateId = FGuid::NewGuid();
	FailedCandidate.RunId = FGuid::NewGuid();
	FailedCandidate.EventType = TEXT("CaptureFailed");
	FailedCandidate.SubjectId = FGuid::NewGuid();
	TestTrue(TEXT("失败成像候选可提交"), ImprintService->SubmitImprintCandidate(FailedCandidate));
	TArray<FCatCapturePlan> FailedPlans;
	TestTrue(TEXT("失败成像候选可生成 CapturePlan"), ImprintService->CreateCapturePlansForParticipants(
		FailedCandidate.CandidateId, {StableNetId}, false, FailedPlans));
	const FCatCapturePlan FailedPlan = FailedPlans.IsEmpty() ? FCatCapturePlan() : FailedPlans[0];
	TestTrue(TEXT("失败成像计划带有有效 PlanId"), FailedPlan.CapturePlanId.IsValid());
	const FCatDomainCommandResult FailedResult = ImprintService->ReportCaptureResult(
		Controller, FailedPlan.CapturePlanId, false, FGuid());
	TestTrue(TEXT("首次失败成像提交计划终态"), FailedResult.bCommitted);
	TestEqual(TEXT("首次失败成像返回 Cancelled"), FailedResult.Error, ECatDomainCommandError::Cancelled);
	const FCatDomainCommandResult FailedReplay = ImprintService->ReportCaptureResult(
		Controller, FailedPlan.CapturePlanId, false, FGuid());
	TestFalse(TEXT("失败结果原样重放不再次提交"), FailedReplay.bCommitted);
	TestEqual(TEXT("失败结果原样重放返回 AlreadyResolved"), FailedReplay.Error,
		ECatDomainCommandError::AlreadyResolved);
	const FCatDomainCommandResult SuccessAfterFailure = ImprintService->ReportCaptureResult(
		Controller, FailedPlan.CapturePlanId, true, FGuid::NewGuid());
	TestFalse(TEXT("失败计划不能再漂移成成功"), SuccessAfterFailure.bCommitted);
	TestEqual(TEXT("失败计划漂移成成功返回 InvalidPayload"), SuccessAfterFailure.Error,
		ECatDomainCommandError::InvalidPayload);
	return !HasAnyErrors();
}

// 测试流程：用同一条 committed 捕获事实同时归约钓起轨与抄获轨，钓手和抄手先用不同身份、再用同一身份。
// 这里锁住的不变量是「记录两轨制」：钓起轨只认钓手、抄获轨只认抄手，两轨各占一条独立 Grant，
// 并且自钓自抄不会被去重成一条。旧实现把两轨都发给抄手，且只产生一条 Grant，会在这三条断言上暴露。
bool FCatRunImprintCaptureRecordTrackSplitTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	WorldWrapper.ForwardErrorMessages(this);
	if (!TestTrue(TEXT("创建记录两轨制测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}

	UWorld* World = WorldWrapper.GetTestWorld();
	UCatRunImprintService* ImprintService = World ? World->GetSubsystem<UCatRunImprintService>() : nullptr;
	TestNotNull(TEXT("可取得 RunImprintService"), ImprintService);
	if (!ImprintService)
	{
		return false;
	}

	FCatCaptureConditionSnapshot Condition;
	Condition.RegionId = TEXT("SliceRegion");

	const FString FisherStableNetId(TEXT("SliceFisherA"));
	const FString ScooperStableNetId(TEXT("SliceScooperB"));
	const FCatCaptureCommittedResult SharedCapture =
		CatItemsCollectionSliceTest::MakeCommittedCapture(TEXT("SliceFish"));

	const FGuid RecordedGrantId = ImprintService->RecordCommittedCapture(SharedCapture, FisherStableNetId, Condition);
	const FGuid ScoopedGrantId = ImprintService->RecordCommittedScoop(SharedCapture, ScooperStableNetId);
	TestTrue(TEXT("钓起轨为钓手生成 Grant"), RecordedGrantId.IsValid());
	TestTrue(TEXT("抄获轨为抄手生成 Grant"), ScoopedGrantId.IsValid());
	TestTrue(TEXT("两轨是两条独立 Grant"), RecordedGrantId != ScoopedGrantId);
	TestEqual(TEXT("一次捕获产生两条待 ACK Grant"), ImprintService->GetPendingGrantAckCount(), 2);

	// 收件人漂移即被拒绝，这是「Grant 确实发给了哪一方」在服务外部唯一可观测的证据：
	// 同一 CaptureRequestId 换一个收件人时载荷不再等价，因此返回无效 GrantId 而不是复用旧 Grant。
	TestFalse(TEXT("钓起轨不接受把收件人换成抄手"),
		ImprintService->RecordCommittedCapture(SharedCapture, ScooperStableNetId, Condition).IsValid());
	TestFalse(TEXT("抄获轨不接受把收件人换成钓手"),
		ImprintService->RecordCommittedScoop(SharedCapture, FisherStableNetId).IsValid());
	TestEqual(TEXT("收件人漂移不增加 pending Grant 数量"), ImprintService->GetPendingGrantAckCount(), 2);

	TestEqual(TEXT("钓起轨原样重放返回同一 GrantId"),
		ImprintService->RecordCommittedCapture(SharedCapture, FisherStableNetId, Condition), RecordedGrantId);
	TestEqual(TEXT("抄获轨原样重放返回同一 GrantId"),
		ImprintService->RecordCommittedScoop(SharedCapture, ScooperStableNetId), ScoopedGrantId);
	TestEqual(TEXT("两轨重放都不增加 pending Grant 数量"), ImprintService->GetPendingGrantAckCount(), 2);

	const FString SoloStableNetId(TEXT("SliceSoloC"));
	const FCatCaptureCommittedResult SoloCapture =
		CatItemsCollectionSliceTest::MakeCommittedCapture(TEXT("SliceFish"));
	const FGuid SoloRecordedGrantId = ImprintService->RecordCommittedCapture(SoloCapture, SoloStableNetId, Condition);
	const FGuid SoloScoopedGrantId = ImprintService->RecordCommittedScoop(SoloCapture, SoloStableNetId);
	TestTrue(TEXT("自钓自抄仍生成钓起轨 Grant"), SoloRecordedGrantId.IsValid());
	TestTrue(TEXT("自钓自抄仍生成抄获轨 Grant"), SoloScoopedGrantId.IsValid());
	TestTrue(TEXT("自钓自抄的两轨不被去重成一条"), SoloRecordedGrantId != SoloScoopedGrantId);
	TestEqual(TEXT("自钓自抄同样恰好新增两条待 ACK Grant"), ImprintService->GetPendingGrantAckCount(), 4);
	return !HasAnyErrors();
}

// 测试流程：在营地未配置篝火封面事件和已配置两种情形下评估结算归档就绪度。
// 锁住的不变量是「未配置封面事件不得把结算夜卡死」，同时「已配置时仍必须等到封面计划落地」——
// 旧实现对两种情形一律返回 false，生产配置 CampfireCoverEventId=None 因此让结算永远完不成。
bool FCatRunImprintSettlementWithoutCampfireCoverTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	WorldWrapper.ForwardErrorMessages(this);
	if (!TestTrue(TEXT("创建结算归档测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}

	UWorld* World = WorldWrapper.GetTestWorld();
	UCatRunImprintService* ImprintService = World ? World->GetSubsystem<UCatRunImprintService>() : nullptr;
	TestNotNull(TEXT("可取得 RunImprintService"), ImprintService);
	if (!ImprintService)
	{
		return false;
	}

	CatItemsCollectionSliceTest::FCampfireCoverEventOverride CoverOverride;
	const FGuid RunId = FGuid::NewGuid();

	TestEqual(TEXT("无效 RunId 返回可区分的 InvalidRun"),
		ImprintService->EvaluateSettlementArchiveReadiness(FGuid()),
		ECatSettlementArchiveBlocker::InvalidRun);

	CoverOverride.SetCampfireCoverEventId(NAME_None);
	TestEqual(TEXT("未配置封面事件时归档没有阻塞项"),
		ImprintService->EvaluateSettlementArchiveReadiness(RunId), ECatSettlementArchiveBlocker::None);
	TestTrue(TEXT("未配置封面事件时结算可以推进"), ImprintService->IsSettlementArchiveReady(RunId));

	CoverOverride.SetCampfireCoverEventId(TEXT("AutomationCampfireCover"));
	TestEqual(TEXT("已配置封面事件但没有封面计划时阻塞项可区分"),
		ImprintService->EvaluateSettlementArchiveReadiness(RunId),
		ECatSettlementArchiveBlocker::CampfireCoverPlanMissing);
	TestFalse(TEXT("已配置封面事件时仍要求封面计划就绪"), ImprintService->IsSettlementArchiveReady(RunId));

	// 未 ACK 的永久 Grant 必须仍然挡住结算，且原因与封面缺失区分得开。
	CoverOverride.SetCampfireCoverEventId(NAME_None);
	const FCatCaptureCommittedResult Capture =
		CatItemsCollectionSliceTest::MakeCommittedCapture(TEXT("SliceFish"));
	TestTrue(TEXT("可为结算测试建立一条待 ACK Grant"),
		ImprintService->RecordCommittedScoop(Capture, TEXT("SliceScooperB")).IsValid());
	TestEqual(TEXT("未 ACK Grant 的阻塞原因是 GrantAckPending"),
		ImprintService->EvaluateSettlementArchiveReadiness(RunId), ECatSettlementArchiveBlocker::GrantAckPending);
	TestFalse(TEXT("未 ACK Grant 仍挡住结算"), ImprintService->IsSettlementArchiveReady(RunId));
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
