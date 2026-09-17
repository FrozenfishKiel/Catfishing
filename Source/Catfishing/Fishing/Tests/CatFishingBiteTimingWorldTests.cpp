#include "Inventory/CatInventorySettings.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "OnlineSubsystemTypes.h"
#include "UI/CatFishingViewTypes.h"
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "TimerManager.h"
#include "EngineUtils.h"
#include "Environment/CatChumFieldSubsystem.h"
#include "Environment/Tests/CatWaterTestFixtures.h"
#include "Equipment/CatEquipmentItemDefinition.h"
#include "Equipment/Fragments/CatEquipmentFragment_Chum.h"
#include "Fishing/Actors/CatFishingHookActor.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/CatFishingService.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingGameState.h"
#include "Fishing/CatFishingGameplayTags.h"
#include "Components/StateTreeComponent.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/Presentation/CatFishingPresentationSettings.h"
#include "Fishing/Simulation/CatFishingBiteTimingModel.h"
#include "Character/CatCharacter.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Inventory/CatInventoryComponent.h"
#include "Equipment/Fragments/CatEquipmentFragment_Rod.h"
#include "Fishing/Actors/CatFishEncounterActor.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingConcentrationTimingTest,
    "Catfishing.Unit.Fishing.BiteTiming.ConcentrationAndProgress",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingConcentrationTimingTest::RunTest(const FString& Parameters)
{
    FCatFishingBiteTimingParameters Timing;
    double Seconds = 0.0;
    TestTrue(TEXT("空窝参数有效"), FCatFishingBiteTimingModel::TryComputeInterval(Timing, 0.0, 1.0, Seconds));
    TestEqual(TEXT("空窝120秒"), Seconds, 120.0);
    FCatFishingBiteTimingModel::TryComputeInterval(Timing, 1.7, 1.0, Seconds);
    TestEqual(TEXT("单份中心约11.54秒"), Seconds, 150.0 / 13.0, 1.e-9);
    FCatFishingBiteTimingModel::TryComputeInterval(Timing, 8.5, 1.0, Seconds);
    TestEqual(TEXT("五份中心6秒"), Seconds, 6.0, 1.e-9);
    FCatFishingBiteTimingModel::TryComputeInterval(Timing, 850.0, 1.0, Seconds);
    TestTrue(TEXT("高浓度不被旧慢浮或预警时长托底"), Seconds > 0.0 && Seconds < 1.5);
    TestFalse(TEXT("负浓度拒绝"), FCatFishingBiteTimingModel::TryComputeInterval(Timing, -1.0, 1.0, Seconds));
    TestFalse(TEXT("非正成长乘数拒绝"), FCatFishingBiteTimingModel::TryComputeInterval(Timing, 1.7, 0.0, Seconds));
    FCatFishingBiteWaitProgress Progress;
    Progress.IntervalSeconds = 120.0;
    Progress.LastServerTime = 2.0;
    Progress.Advance(1.0, 120.0);
    TestEqual(TEXT("飞行不消费等待进度"), Progress.RemainingSeconds(1.0), 121.0);
    Progress.Advance(32.0, 6.0);
    TestEqual(TEXT("已等四分之一后补窝剩4.5秒"), Progress.RemainingSeconds(32.0), 4.5);
    Progress.Advance(33.5, 120.0);
    TestEqual(TEXT("已等一半后窝消失剩60秒"), Progress.RemainingSeconds(33.5), 60.0);
    Progress.Advance(93.5, 120.0);
    TestEqual(TEXT("到点只耗尽本漂进度"), Progress.RemainingFraction, 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingBiteTimingWorldTest,
	"Catfishing.Unit.Fishing.BiteTiming.WorldFieldsDriveFormalStateTreeAndBobber",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingBiteTimingWorldTest::RunTest(const FString& Parameters)
{
	const UCatEquipmentItemDefinition* ChumDefinition = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentItemDefinition>(5);
	if (!TestNotNull(TEXT("正式窝料资产可加载"), ChumDefinition)) return false;
	FCatFishingBiteTimingParameters Timing;
	if (!TestTrue(TEXT("正式等待配置可读取"), GetDefault<UCatFishingSettings>()->TryGetBiteTimingParameters(Timing))) return false;
	const auto* Catalog = GetDefault<UCatFishCatalogSettings>();
    TestEqual(TEXT("正式窗口覆盖16鱼资产"), Catalog->Definitions.Num(), 16);
    for (const auto& Ref : Catalog->Definitions)
    {
        const auto* Fish=Ref.LoadSynchronous();
        if (!TestNotNull(TEXT("正式鱼资产"), Fish)) return false;
        const auto Resolved=Catalog->ResolveBiteTiming(*Fish);
        TestTrue(TEXT("正式窗口有有效逐鱼秒数"), Fish->ProbeDurationSeconds > 0.0 && Fish->TrueBiteWindowSeconds >= 8.0);
        TestEqual(TEXT("正式资产解析试探秒数"), Resolved.ProbeDurationSeconds, Fish->ProbeDurationSeconds);
        TestEqual(TEXT("正式资产解析响应秒数"), Resolved.TrueBiteWindowSeconds, Fish->TrueBiteWindowSeconds);
    }
	// 只准备抛竿事务的已冻结输入；后续采样、计时器、正式 StateTree 和正式 Hook BP 都走生产代码。
	for (const int32 Portions : {0, 1, 5, 2, 3, 4})
	{
		FTestWorldWrapper Wrapper;
		if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
		Wrapper.ForwardErrorMessages(this);
		UWorld* World = Wrapper.GetTestWorld();
		const auto CountFishActors = [&]()
		{
			int32 Count = 0;
			for (TActorIterator<ACatFishEncounterActor> It(World); It; ++It)
				if (IsValid(*It)) ++Count;
			return Count;
		};
		FURL URL;
		URL.AddOption(TEXT("game=/Script/Catfishing.CatfishingGameModeBase"));
		if (!TestTrue(TEXT("使用正式昼夜准入宿主"), World->SetGameMode(URL))) return false;
		FCatWaterGeometryBuildInput WaterInput;
		WaterInput.RegionId = Catalog->FindRuntimeDefinition(30)->RegionIds[0];
		WaterInput.WaterPointVerticalToleranceCm = 100.0;
		WaterInput.BankHeightToleranceCm = 50.0;
		WaterInput.BoundaryToleranceCm = 1.0;
		WaterInput.MaxLandingCorrectionCm = 100.0;
		WaterInput.MinimumWaterInsetCm = 1.0;
		FCatWaterPolygonBuildInput Polygon;
		Polygon.BoundaryId = TEXT("BiteTimingBoundary");
		Polygon.Vertices = {{-5000, -5000}, {5000, -5000}, {5000, 5000}, {-5000, 5000}};
		WaterInput.Boundaries.Add(Polygon);
		const auto Built = FCatWaterGeometry::Build(WaterInput);
		if (!TestTrue(TEXT("水域烘焙成功"), Built.bSucceeded)) return false;
		ACatWaterRegion* Region = World->SpawnActor<ACatWaterRegion>();
		if (!Region) return false;
		FCatWaterRegionTestAccess::InjectBakedGeometry(*Region, Built.Cache);
		if (!TestTrue(TEXT("水域注册并启动 World 生命周期"), Wrapper.BeginPlayInTestWorld())) return false;
		auto* Mode = World->GetAuthGameMode<ACatfishingGameModeBase>();
		Mode->bRunCommandsOpen = true;
		Mode->RunPublicState.Phase.Phase = ECatRunPhase::DayActive;
		Mode->RunPublicState.Phase.bNewFishingBitesAllowed = true;
		auto* Fishing = World->GetSubsystem<UCatFishingService>();
		UCatChumFieldSubsystem* Chum = World->GetSubsystem<UCatChumFieldSubsystem>();
		if (!TestNotNull(TEXT("真实窝料子系统存在"), Chum)) return false;
		const double StartTime = World->GetTimeSeconds();
		for (int32 Portion = 0; Portion < Portions; ++Portion)
		{
			FCatPrepareChumFieldRequest Request;
			Request.StableNetId = TEXT("BiteTimingAutomation");
			Request.Command.RequestId = FGuid::NewGuid();
			Request.Command.ExpectedWaterRegionHandle = Built.Cache.Handle;
			Request.Command.ChumItemId = ChumDefinition->ItemId;
			Request.Command.Quantity = 1;
			Request.Influence = ChumDefinition->FindFragment<UCatEquipmentFragment_Chum>()->ChumInfluence;
			Request.ServerTime = StartTime;
			const auto Prepared = Chum->PrepareField(Request);
			if (!TestTrue(TEXT("正式窝料准备成功"), Prepared.bPrepared)) return false;
			TestEqual(TEXT("正式资产保留三分钟持续时间"), Prepared.ExpireServerTime - Prepared.StartServerTime, 180.0);
			const auto Activated = Chum->ActivatePreparedFieldDeferred(Prepared.CommitToken);
			if (!TestTrue(TEXT("真实窝料场激活成功"), Activated.bCommitted)) return false;
            FCatPlaceChumResult Terminal;
            Terminal.RequestId = Request.Command.RequestId;
            Terminal.bCommitted = true;
            Terminal.FieldId = Activated.FieldId;
            Chum->StoreTerminalResult(Request.StableNetId, Terminal);
			Chum->PublishActivatedField(Activated.FieldId);
		}
		const auto Sample = Chum->SampleChumAtPoint(FVector::ZeroVector, Built.Cache.Handle, StartTime);
		if (!TestTrue(TEXT("中心采样成功"), Sample.bSucceeded)) return false;
		const auto Total = [](const FCatChumSample& Value)
		{
			return Value.EffectiveChumVector.Fishy + Value.EffectiveChumVector.Fragrant + Value.EffectiveChumVector.Fermented;
		};
		TestEqual(TEXT("每份正式窝料贡献为1.7"), Total(Sample), Portions * 1.7, 1.e-7);
		TestEqual(TEXT("独立投放真实重叠"), Sample.ContributingFieldCount, Portions);
		if (Portions > 0)
		{
			const auto Aged = Chum->SampleChumAtPoint(FVector::ZeroVector, Built.Cache.Handle, StartTime + 90.0);
			const auto Edge = Chum->SampleChumAtPoint(FVector(800, 0, 0), Built.Cache.Handle, StartTime);
			const auto Expired = Chum->SampleChumAtPoint(FVector::ZeroVector, Built.Cache.Handle, StartTime + 180.0);
			TestTrue(TEXT("时间衰减降低有效贡献"), Aged.bSucceeded && Total(Aged) > 0.0 && Total(Aged) < Total(Sample));
			TestTrue(TEXT("扩大后的八米处仍在范围内且更淡"), Edge.bSucceeded && Total(Edge) > 0.0 && Total(Edge) < Total(Sample));
			TestTrue(TEXT("到期按零贡献返回"), Expired.bSucceeded && Total(Expired) == 0.0);
			double FreshInterval = 0.0, AgedInterval = 0.0;
			FCatFishingBiteTimingModel::TryComputeInterval(Timing, Total(Sample), 1.0, FreshInterval);
			FCatFishingBiteTimingModel::TryComputeInterval(Timing, Total(Aged), 1.0, AgedInterval);
			TestTrue(TEXT("真实衰减采样使间隔回升"), AgedInterval > FreshInterval);
		}
		UClass* HookClass = GetDefault<UCatFishingPresentationSettings>()->HookActorClass.LoadSynchronous();
		if (!HookClass) return false;
		ACatFishingHookActor* Hook = World->SpawnActor<ACatFishingHookActor>(HookClass);
		ACatFishingSession* Session = World->SpawnActor<ACatFishingSession>();
		if (!Hook || !Session) return false;
		Session->Snapshot.FishingSessionId = FGuid::NewGuid();
		auto* BaitCharacter = World->SpawnActor<ACatCharacter>();
		auto* BaitPlayer = World->SpawnActor<ACatfishingPlayerState>();
		auto* BaitRod = World->SpawnActor<ACatFishingRodActor>();
		if (!BaitCharacter || !BaitPlayer || !BaitRod) return false;
		auto* Controller = World->SpawnActor<ACatfishingPlayerController>();
		if (!Controller) return false;
		Controller->PlayerState = BaitPlayer;
		BaitCharacter->SetPlayerState(BaitPlayer);
		Controller->Possess(BaitCharacter);
		const FUniqueNetIdRef UniqueId = FUniqueNetIdString::Create(TEXT("WindowTimingFisher"), FName(TEXT("CAT_TEST")));
		BaitPlayer->SetUniqueId(FUniqueNetIdRepl(UniqueId));
		ACatfishingGameModeBase::FAdmissionRecord Admission;
		Admission.Phase = ACatfishingGameModeBase::EAdmissionPhase::Active;
		Admission.Controller = Controller;
		Mode->AdmissionRecords.Add(ACatfishingGameModeBase::MakeStableNetIdKey(BaitPlayer->GetUniqueId()), Admission);
		// 本夹具只验证计时和库存；没有角色地板，禁止无控制器角色自由落出世界。
		BaitCharacter->GetCharacterMovement()->SetComponentTickEnabled(false);
		auto* BaitEquipment = BaitCharacter->GetEquipmentComponent();
		for (const int32 Definition : {37, 9})
			if (!BaitEquipment->GrantEquipmentFromAuthority(FGuid::NewGuid(), BaitEquipment->GetSnapshot().Revision, Definition).bCommitted) return false;
		if (!BaitEquipment->GrantInventoryQuantityFromAuthority(FGuid::NewGuid(), BaitEquipment->GetSnapshot().Revision, 4, 1).bCommitted) return false;
		if (!BaitEquipment->Use(FGuid::NewGuid(), BaitEquipment->GetSnapshot().Revision, BaitEquipment->GetSnapshot().RodItemInstanceId).bCommitted) return false;
		const auto BaitLoadout = BaitEquipment->GetSnapshot();
		if (!BaitEquipment->BeginFishingUse(Session->Snapshot.FishingSessionId, BaitLoadout.RodItemInstanceId,
			BaitLoadout.BaitItemInstanceId, BaitLoadout.FloatItemInstanceId, BaitLoadout.RodItemId,
			BaitLoadout.BaitItemId, BaitLoadout.FloatItemId, BaitLoadout.Revision).bUseAccepted) return false;
		if (!BaitRod->InitializeAuthoritativeIdentity(FGuid::NewGuid(), BaitLoadout.RodItemInstanceId,
			BaitLoadout.RodItemId, NAME_None, BaitPlayer, nullptr, true, false)) return false;
		Session->Snapshot.RodActor = BaitRod;
		Session->FisherCharacter = BaitCharacter;
		Session->AttemptSnapshot.RodActor = BaitRod;
		Session->AttemptSnapshot.RodItemId = BaitLoadout.RodItemId;
		BaitCharacter->SetActorLocation(FVector(-500, 0, 0));
		Session->CastEquipment = BaitEquipment;
		Session->Snapshot.CastAttemptId = FGuid::NewGuid();
		Session->Snapshot.HookActor = Hook;
		Session->AttemptSnapshot.ServerRandomSeed = 78629;
		Session->AttemptSnapshot.BaitItemId = 4;
		Session->AttemptSnapshot.WaterRegion = Built.Cache.Handle;
		Session->AttemptSnapshot.ServerCorrectedLandingWorldPoint = FVector::ZeroVector;
		Session->bPrepared = true;
		Fishing->Sessions.Add(Session->Snapshot.FishingSessionId, Session);
		Hook->InitializeAuthoritativeIdentity(Session->Snapshot.FishingSessionId, Session->Snapshot.CastAttemptId);
		Hook->SetActorLocation(FVector(-2000, 0, 100)); // 飞行起点在窝外，必须读冻结落点。
		if (!TestTrue(TEXT("真实抛竿飞行启动"), Hook->BeginAuthoritativeFlight(FVector::ZeroVector))) return false;
		bool bNightDuringProbePublish = false;
		if (Portions == 2) Session->OnSnapshotChanged.AddLambda([&]()
		{
			if (bNightDuringProbePublish || Session->GetSnapshot().Phase != ECatFishingPhase::Probe
				|| (Session->GetSnapshot().ItemId == 0)) return;
			bNightDuringProbePublish = true;
			TestEqual(TEXT("入夜回调前抽鱼不生成实体"), CountFishActors(), 0);
			Mode->RunPublicState.Phase.RunId = FGuid::NewGuid();
			Mode->bRunStartupInProgress = true;
			TestTrue(TEXT("Probe 发布回调内正式入夜"), Mode->EnterRunPhaseFromStateTree(ECatRunPhase::NormalNight, ECatRunTransitionReason::DayEnded).bApplied);
			Mode->bRunStartupInProgress = false;
		});
		if (!TestTrue(TEXT("正式会话 StateTree 进入等待"), Session->StartPreparedSessionLogicFromAuthority())) return false;
		TestEqual(TEXT("运行阶段为等待"), Session->GetSnapshot().Phase, ECatFishingPhase::Waiting);
		if (Portions == 4)
		{
			Session->SuspendOperatorFromAuthority();
			TestFalse(TEXT("unattended wait has no operator character"), Session->FisherCharacter.IsValid());
		}
		double ExpectedWait = 0.0;
        FCatFishingBiteTimingModel::TryComputeInterval(Timing, Total(Sample), 1.0, ExpectedWait);
		const double FlightSeconds = Hook->GetPresentationState().CastTrajectory.DurationSeconds;
		const double BiteRemaining = World->GetTimerManager().GetTimerRemaining(Session->ProbeTimerHandle);
		const double WarningRemaining = World->GetTimerManager().GetTimerRemaining(Session->BiteWarningTimerHandle);
		TestEqual(TEXT("生产计时器消费冻结落点的新模型并加飞行时间"), BiteRemaining, ExpectedWait + FlightSeconds, 1.e-4);
		TestEqual(TEXT("计时器保证完整1.5秒预警"), BiteRemaining - WarningRemaining, 1.5, 1.e-4);
        if (Portions == 0)
        {
            // 真正落水后等待30秒，再通过正式投放/清场通知改排，不能重新开始一轮。
            for (int32 Frame = 0; Frame < FMath::CeilToInt((FlightSeconds + 30.0) / 0.01); ++Frame)
                Wrapper.TickTestWorld(0.01f);
            const uint32 Opportunity = Session->BiteOpportunitySequence;
            const uint64 Seed = Session->CurrentBiteRandomSeed;
            const double BeforeReentry = World->GetTimerManager().GetTimerRemaining(Session->ProbeTimerHandle);
            TestTrue(TEXT("等待重入成功"), Session->ScheduleWaitingProbeFromStateTree());
            TestEqual(TEXT("等待重入不重置进度"), double(World->GetTimerManager().GetTimerRemaining(Session->ProbeTimerHandle)), BeforeReentry, 0.002);
            const double Before = World->GetTimerManager().GetTimerRemaining(Session->ProbeTimerHandle);
            FCatPrepareChumFieldRequest Refill;
            Refill.StableNetId = TEXT("BiteTimingAutomation");
            Refill.Command.RequestId = FGuid::NewGuid();
            Refill.Command.ExpectedWaterRegionHandle = Built.Cache.Handle;
            Refill.Command.ChumItemId = ChumDefinition->ItemId;
            Refill.Command.Quantity = 1;
            Refill.Influence = ChumDefinition->FindFragment<UCatEquipmentFragment_Chum>()->ChumInfluence;
            Refill.ServerTime = World->GetTimeSeconds();
            const auto Prepared = Chum->PrepareField(Refill);
            if (!TestTrue(TEXT("等待中补窝准备成功"), Prepared.bPrepared)) return false;
            const auto Activated = Chum->ActivatePreparedFieldDeferred(Prepared.CommitToken);
            if (!TestTrue(TEXT("等待中补窝激活成功"), Activated.bCommitted)) return false;
            FCatPlaceChumResult Terminal;
            Terminal.RequestId = Refill.Command.RequestId;
            Terminal.bCommitted = true;
            Terminal.FieldId = Activated.FieldId;
            Chum->StoreTerminalResult(Refill.StableNetId, Terminal);
            Chum->PublishActivatedField(Activated.FieldId);
            double RefilledInterval = 0.0;
            FCatFishingBiteTimingModel::TryComputeInterval(Timing, 1.7, 1.0, RefilledInterval);
            TestEqual(TEXT("补窝即时按已完成比例折算"), double(World->GetTimerManager().GetTimerRemaining(Session->ProbeTimerHandle)),
                Before * RefilledInterval / Timing.UnchummedIntervalSeconds, 0.002);
            const double Fraction = Session->BiteWaitProgress.RemainingFraction;
            for (int32 Frame = 0; Frame < 100; ++Frame) Wrapper.TickTestWorld(0.01f);
            TestTrue(TEXT("持续衰减抬高当前间隔且等待继续前进"), Session->BiteWaitProgress.IntervalSeconds > RefilledInterval
                && Session->BiteWaitProgress.RemainingFraction < Fraction);
            const double BeforeClear = World->GetTimerManager().GetTimerRemaining(Session->ProbeTimerHandle);
            const double BeforeClearInterval = Session->BiteWaitProgress.IntervalSeconds;
            Chum->ClearFieldsForRunTransition();
            TestEqual(TEXT("清场即时延长剩余但不重置120秒"), double(World->GetTimerManager().GetTimerRemaining(Session->ProbeTimerHandle)),
                BeforeClear * Timing.UnchummedIntervalSeconds / BeforeClearInterval, 0.002);
            TestEqual(TEXT("改排保持同一选鱼机会"), Session->BiteOpportunitySequence, Opportunity);
            TestEqual(TEXT("改排不重抽随机种子"), Session->CurrentBiteRandomSeed, Seed);
            TestEqual(TEXT("改排不扣饵"), BaitCharacter->GetInventoryComponent()->CountVisibleInventoryQuantityByItemId(4), 1);
        }
		double ObservedWarningTime = -1.0;
		double ObservedProbeTime = -1.0;
		for (int32 Frame = 0; Frame < 20000 && !Session->IsTerminal() && Session->GetSnapshot().Phase != ECatFishingPhase::TrueBiteWindow; ++Frame)
		{
			Wrapper.TickTestWorld(0.01f);
			if (Portions == 2 && bNightDuringProbePublish)
			{
				Wrapper.TickTestWorld(0.02f);
				TestTrue(TEXT("重入入夜按空竿收回并结束会话"), Session->IsTerminal());
				TestEqual(TEXT("重入入夜为空竿终局"), Session->GetSnapshot().Outcome, ECatFishingOutcome::EmptyHook);
				TestFalse(TEXT("重入入夜不重启 Probe 计时"), World->GetTimerManager().IsTimerActive(Session->ProbeStayTimerHandle));
				Session->OnSnapshotChanged.Clear();
				double TerminalWindow = 0.0;
				if (!TestTrue(TEXT("终局复制窗口有效"), GetDefault<UCatFishingSettings>()->TryGetTerminalReplicationWindow(TerminalWindow))) return false;
				for (int32 CleanupFrame = 0; CleanupFrame < FMath::CeilToInt((TerminalWindow + 0.1) / 0.01); ++CleanupFrame)
					Wrapper.TickTestWorld(0.01f);
				TestEqual(TEXT("试探期入夜结束后没有鱼实体残留"), CountFishActors(), 0);
				break;
			}
			if (ObservedProbeTime < 0.0 && Session->GetSnapshot().Phase == ECatFishingPhase::Probe)
			{
				ObservedProbeTime = World->GetTimeSeconds();
				TestNull(TEXT("Probe 只有抽鱼数据，没有实体引用"), Session->GetSnapshot().FishEncounterActor.Get());
				TestEqual(TEXT("Probe 世界中未生成鱼实体"), CountFishActors(), 0);
				TestFalse(TEXT("Probe 尚未启动搏斗"), Session->IsFightRunnerRunning());
				TestEqual(TEXT("Probe 不扣饵"), BaitCharacter->GetInventoryComponent()->CountVisibleInventoryQuantityByItemId(4), 1);
				TestTrue(TEXT("Probe 已冻结实际鱼重和体型比例"), Session->FishWeightKilograms > 0.0 && Session->FishVisualScale > 0.0);
				TestEqual(TEXT("选鱼幂等"), Session->ResolveHookSelectionFromAuthority().ItemId, Session->Snapshot.ItemId);
				if (Portions == 3)
				{
					AddExpectedErrorPlain(TEXT("Outcome=ECatFishingOutcome::EmptyHook"), EAutomationExpectedErrorFlags::Contains, 1);
					TestTrue(TEXT("试探提前提竿"), Session->RequestHookFromAuthority(FGuid::NewGuid()).bCommitted);
					TestEqual(TEXT("试探提竿为空钩"), Session->GetSnapshot().Outcome, ECatFishingOutcome::EmptyHook);
					TestEqual(TEXT("提前提竿不生成实体"), CountFishActors(), 0);
					TestFalse(TEXT("提前提竿清除 Probe 计时"), World->GetTimerManager().IsTimerActive(Session->ProbeStayTimerHandle));
					break;
				}
			}
			if (ObservedWarningTime < 0.0 && Hook->GetPresentationState().BobberMode == ECatFishingBobberPresentationMode::BiteWarning)
			{
				ObservedWarningTime = World->GetTimeSeconds();
				TestEqual(TEXT("fast warning preserves the real bait quantity"),
					BaitCharacter->GetInventoryComponent()->CountVisibleInventoryQuantityByItemId(4), 1);
			}
		}
		if (Portions == 2 || Portions == 3)
		{
			TestTrue(TEXT("经过真实 Probe 阶段"), ObservedProbeTime >= 0.0 || bNightDuringProbePublish);
			if (IsValid(Session)) Session->OnSnapshotChanged.Clear();
			TestEqual(TEXT("真咬前退出保持饵数量"), BaitCharacter->GetInventoryComponent()->CountVisibleInventoryQuantityByItemId(4), 1);
			continue;
		}
		if (!TestEqual(TEXT("真咬阶段成功"), Session->GetSnapshot().Phase, ECatFishingPhase::TrueBiteWindow)) return false;
		TestFalse(TEXT("进入试探后持续采样计时器已清理"), World->GetTimerManager().IsTimerActive(Session->BiteRefreshTimerHandle));
		const auto ResolvedTiming = Catalog->ResolveBiteTiming(*Session->FishDefinition);
		TestEqual(TEXT("实际逐鱼试探时长"), World->GetTimeSeconds() - ObservedProbeTime, ResolvedTiming.ProbeDurationSeconds, 0.04);
		TestEqual(TEXT("逐鱼响应截止时间"), Session->Snapshot.WindowEndsServerTime - Session->Snapshot.PhaseStartedServerTime, ResolvedTiming.TrueBiteWindowSeconds, 0.001);
		TestEqual(TEXT("基础完美窗独立为一秒"), Session->Snapshot.PerfectWindowEndsServerTime - Session->Snapshot.PhaseStartedServerTime, 1.0, 0.001);
		const auto View = FCatFishingViewState::FromSnapshot(Session->GetSnapshot());
		TestEqual(TEXT("HUD 完美截止来自权威快照"), View.PerfectWindowEndsServerTime, Session->Snapshot.PerfectWindowEndsServerTime);
		TestEqual(TEXT("HUD 响应截止来自权威快照"), View.WindowEndsServerTime, Session->Snapshot.WindowEndsServerTime);
		TestTrue(TEXT("普通提竿区间独立存在"), Session->Snapshot.WindowEndsServerTime > Session->Snapshot.PerfectWindowEndsServerTime);
		TestTrue(TEXT("运行时观察到预警模式"), ObservedWarningTime >= 0.0);
		TestEqual(TEXT("正式 StateTree 真正打开咬钩窗口"), Session->GetSnapshot().Phase, ECatFishingPhase::TrueBiteWindow);
		TestEqual(TEXT("真咬才确认鱼饵消耗"), BaitEquipment->CommitFishingBaitDeferred(Session->Snapshot.FishingSessionId).Error,
			ECatDomainCommandError::AlreadyResolved);
		TestEqual(TEXT("浮漂真咬时下沉"), Hook->GetPresentationState().BobberMode, ECatFishingBobberPresentationMode::Sunk);
		TestEqual(TEXT("实际预警持续完整时段（帧量化容差）"), World->GetTimeSeconds() - ObservedWarningTime, 1.5 + ResolvedTiming.ProbeDurationSeconds, 0.06);
		TestEqual(TEXT("true bite consumes exactly one actual bait"), BaitCharacter->GetInventoryComponent()->CountVisibleInventoryQuantityByItemId(4), 0);
		const double D0 = Session->TrueBiteDistanceCentimeters;
		TestEqual(TEXT("D0 uses frozen fish spawn point at true bite"), D0, FVector::Distance(Portions == 4 ? BaitRod->GetGripWorldTransform().GetLocation() : BaitCharacter->GetActorLocation(), Session->AttemptSnapshot.ServerCorrectedLandingWorldPoint), 0.01);
		BaitCharacter->SetActorLocation(FVector(-1200, 0, 0));
		TestEqual(TEXT("response-window movement does not recalculate D0"), Session->TrueBiteDistanceCentimeters, D0);
		TestNull(TEXT("真咬开窗仍未生成实体"), Session->GetSnapshot().FishEncounterActor.Get());
		TestEqual(TEXT("真咬开窗世界中无鱼实体"), CountFishActors(), 0);
		// 正式 Morning/Day/Dusk timer 在等待期间会刷新鱼情，必须捕获实际真咬时的环境。
		const FCatEnvironmentSnapshot BiteEnvironment = World->GetGameState<ACatfishingGameState>()->GetRunPublicState().Environment;
		TestTrue(TEXT("真咬发生于正式可用的白天鱼情"), BiteEnvironment.TimeOfDay != ECatEnvironmentTimeOfDay::Unknown);
		Mode->RunPublicState.Phase.RunId = FGuid::NewGuid();
		Mode->bRunStartupInProgress = true;
		TestTrue(TEXT("正式 Run 入夜入口成功"), Mode->EnterRunPhaseFromStateTree(
			ECatRunPhase::NormalNight, ECatRunTransitionReason::DayEnded).bApplied);
		Mode->bRunStartupInProgress = false;
		TestEqual(TEXT("入夜不打断已经成立的真咬窗口"), Session->GetSnapshot().Phase, ECatFishingPhase::TrueBiteWindow);
		TestTrue(TEXT("入夜保留既有提竿响应计时"), World->GetTimerManager().IsTimerActive(Session->TrueBiteTimerHandle));
		TestEqual(TEXT("入夜后世界鱼情已没有白天时段"), World->GetGameState<ACatfishingGameState>()->GetRunPublicState().Environment.TimeOfDay,
			ECatEnvironmentTimeOfDay::Unknown);
		TestEqual(TEXT("跨夜真咬保留发生时的时段"), Session->BiteTimeOfDay, BiteEnvironment.TimeOfDay);
		TestEqual(TEXT("跨夜真咬保留发生时的天气"), Session->BiteWeather, BiteEnvironment.Weather);
		AddExpectedErrorPlain(TEXT("Outcome=ECatFishingOutcome::HookWindowExpired"), EAutomationExpectedErrorFlags::Contains, 1);
		for (int32 Frame = 0; Frame < 1600 && !Session->IsTerminal(); ++Frame) Wrapper.TickTestWorld(0.01f);
		TestEqual(TEXT("true bite timeout ends this cast"), Session->GetSnapshot().Outcome, ECatFishingOutcome::HookWindowExpired);
		TestEqual(TEXT("真咬超时未生成实体"), CountFishActors(), 0);
		TestFalse(TEXT("timeout clears future bite scheduling"), World->GetTimerManager().IsTimerActive(Session->ProbeTimerHandle));
		TestFalse(TEXT("timeout closes equipment use"), BaitEquipment->HasActiveFishingUse());
		TestEqual(TEXT("timeout never refunds bait"), BaitCharacter->GetInventoryComponent()->CountVisibleInventoryQuantityByItemId(4), 0);
		TestFalse(TEXT("exit clears warning timer"), World->GetTimerManager().IsTimerActive(Session->BiteWarningTimerHandle));
		TestFalse(TEXT("exit clears response timer"), World->GetTimerManager().IsTimerActive(Session->TrueBiteTimerHandle));
		auto* NightSession = World->SpawnActor<ACatFishingSession>();
		auto* NightHook = World->SpawnActor<ACatFishingHookActor>(HookClass);
		if (!NightSession || !NightHook) return false;
		NightSession->Snapshot.FishingSessionId = FGuid::NewGuid();
		NightSession->Snapshot.CastAttemptId = FGuid::NewGuid();
		NightSession->Snapshot.HookActor = NightHook;
		NightSession->AttemptSnapshot = Session->AttemptSnapshot;
		NightSession->bPrepared = true;
		NightHook->InitializeAuthoritativeIdentity(NightSession->Snapshot.FishingSessionId, NightSession->Snapshot.CastAttemptId);
		TestTrue(TEXT("夜间新抛竿的正式 StateTree 正常启动"), NightSession->StartPreparedSessionLogicFromAuthority());
		TestEqual(TEXT("夜间新会话保持等待"), NightSession->GetSnapshot().Phase, ECatFishingPhase::Waiting);
		TestFalse(TEXT("夜间新抛竿没有咬钩计时"), World->GetTimerManager().IsTimerActive(NightSession->ProbeTimerHandle));
		TestTrue(TEXT("夜间取消正常结束新会话"), NightSession->CancelFromAuthority(FGuid::NewGuid()).bCommitted);
		Mode->RunPublicState.Phase.Phase = ECatRunPhase::DayActive;
		Mode->RunPublicState.Phase.bNewFishingBitesAllowed = true;
		const auto* RodDefinition = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentItemDefinition>(BaitLoadout.RodItemId);
		const double Lmax = RodDefinition->FindFragment<UCatEquipmentFragment_Rod>()->MaximumLineLengthCentimeters;
		for (const double Distance : {Lmax, Lmax + 1.0})
		{
			auto* BoundarySession = World->SpawnActor<ACatFishingSession>();
			auto* BoundaryHook = World->SpawnActor<ACatFishingHookActor>(HookClass);
			BoundarySession->Snapshot.FishingSessionId = FGuid::NewGuid();
			BoundarySession->Snapshot.CastAttemptId = FGuid::NewGuid();
			BoundarySession->Snapshot.HookActor = BoundaryHook;
			BoundarySession->Snapshot.RodActor = BaitRod;
			BoundarySession->Snapshot.Phase = ECatFishingPhase::Probe;
			BoundarySession->AttemptSnapshot = Session->AttemptSnapshot;
			BoundarySession->CurrentBiteRandomSeed = 321;
			BoundarySession->CastEquipment = BaitEquipment;
			BoundarySession->FisherCharacter = BaitCharacter;
			BoundarySession->bStartupInProgress = true;
			BaitCharacter->SetActorLocation(FVector(-Distance, 0, 0));
			BoundaryHook->InitializeAuthoritativeIdentity(BoundarySession->Snapshot.FishingSessionId, BoundarySession->Snapshot.CastAttemptId);
			BoundaryHook->SetActorLocation(FVector::ZeroVector);
			if (!BaitEquipment->GrantInventoryQuantityFromAuthority(FGuid::NewGuid(), BaitEquipment->GetSnapshot().Revision, 4, 1).bCommitted) return false;
			const auto Loadout = BaitEquipment->GetSnapshot();
			if (!BaitEquipment->BeginFishingUse(BoundarySession->Snapshot.FishingSessionId, Loadout.RodItemInstanceId,
				Loadout.BaitItemInstanceId, Loadout.FloatItemInstanceId, Loadout.RodItemId, Loadout.BaitItemId,
				Loadout.FloatItemId, Loadout.Revision).bUseAccepted) return false;
			if (!TestTrue(TEXT("距离边界先开始 Probe"), BoundarySession->BeginProbeFromStateTree())) return false;
			World->GetTimerManager().ClearTimer(BoundarySession->ProbeStayTimerHandle);
			if (Distance == Lmax)
			{
				auto* FormalFish = BoundarySession->FishDefinition.Get();
				auto* InvalidFish = DuplicateObject<UCatFishDefinition>(FormalFish, BoundarySession);
				InvalidFish->TrueBiteWindowSeconds = 7;
				InvalidFish->ProbeDurationSeconds = -1;
				BoundarySession->FishDefinition = InvalidFish;
				double InvalidSeconds = 0;
				TestFalse(TEXT("拒绝负数试探覆盖"), BoundarySession->TryResolveProbeDurationSeconds(InvalidSeconds));
				AddExpectedErrorPlain(TEXT("Event=fishing_bite_timing_rejected"), EAutomationExpectedErrorFlags::Contains, 1);
				TestFalse(TEXT("响应越界拒绝开窗"), BoundarySession->OpenTrueBiteWindowFromAuthority());
				TestEqual(TEXT("非法配置未扣饵"), BaitCharacter->GetInventoryComponent()->CountVisibleInventoryQuantityByItemId(4), 1);
				BoundarySession->FishDefinition = FormalFish;
			}
			if (Distance > Lmax) AddExpectedErrorPlain(TEXT("Outcome=ECatFishingOutcome::Escaped"), EAutomationExpectedErrorFlags::Contains, 1);
			TestEqual(TEXT("true bite admits exact Lmax and rejects over-limit distance"), BoundarySession->OpenTrueBiteWindowFromAuthority(), Distance <= Lmax);
			TestEqual(TEXT("both boundary outcomes pay one bait"), BaitCharacter->GetInventoryComponent()->CountVisibleInventoryQuantityByItemId(4), 0);
			TestEqual(TEXT("the frozen distance is not clamped"), BoundarySession->TrueBiteDistanceCentimeters, Distance);
			TestTrue(TEXT("float inventory survives the distance decision"), BaitEquipment->GetSnapshot().FloatItemInstanceId.IsValid());
			if (Distance > Lmax)
			{
				TestEqual(TEXT("over-limit true bite ends as escaped before hook"), BoundarySession->GetSnapshot().Outcome, ECatFishingOutcome::Escaped);
				TestFalse(TEXT("over-limit session cannot start a fight"), BoundarySession->RequestHookFromAuthority(FGuid::NewGuid()).bCommitted);
			}
			else
			{
				AddExpectedErrorPlain(TEXT("Outcome=ECatFishingOutcome::HookWindowExpired"), EAutomationExpectedErrorFlags::Contains, 1);
				World->GetTimerManager().ClearTimer(BoundarySession->TrueBiteTimerHandle);
				for (int32 Frame = 0; Frame < 1600 && World->GetTimeSeconds() <= BoundarySession->Snapshot.WindowEndsServerTime; ++Frame)
					Wrapper.TickTestWorld(0.01f);
				TestFalse(TEXT("计时器未回调也不能接受过期提竿"), BoundarySession->RequestHookFromAuthority(FGuid::NewGuid()).bCommitted);
				TestEqual(TEXT("过期请求裁决为响应超时"), BoundarySession->Snapshot.Outcome, ECatFishingOutcome::HookWindowExpired);
			}
		}

	}
	return !HasAnyErrors();
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishBiteTimingConfigurationTest,
	"Catfishing.Unit.Fishing.BiteTiming.ConfigurationPriorityAndInvalidValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishBiteTimingConfigurationTest::RunTest(const FString& Parameters)
{
	auto* Catalog = NewObject<UCatFishCatalogSettings>();
	Catalog->BiteTimingOverridesByItemId.Reset();
	Catalog->BiteTimingDefaultsByRarityTier.Reset();
	auto* Fish = NewObject<UCatFishDefinition>();
	Fish->ItemId = 1295214;
	Fish->RarityTierId = TEXT("WindowTestTier");
	FCatFishBiteTimingDefaults Rarity;
	Rarity.ProbeDurationSeconds = 3; Rarity.TrueBiteWindowSeconds = 15;
	Catalog->BiteTimingDefaultsByRarityTier.Add(Fish->RarityTierId, Rarity);
	TestEqual(TEXT("旧鱼回退档位响应"), Catalog->ResolveBiteTiming(*Fish).TrueBiteWindowSeconds, 15.0);
	FCatFishBiteTimingDefaults Override;
	Override.ProbeDurationSeconds = 2; Override.TrueBiteWindowSeconds = 11;
	Catalog->BiteTimingOverridesByItemId.Add(Fish->ItemId, Override);
	TestEqual(TEXT("逐鱼优先于旧档位"), Catalog->ResolveBiteTiming(*Fish).TrueBiteWindowSeconds, 11.0);
	Fish->ProbeDurationSeconds = 1.5;
	TestEqual(TEXT("资产优先且按字段独立回退"), Catalog->ResolveBiteTiming(*Fish).ProbeDurationSeconds, 1.5);
	TestEqual(TEXT("资产另一空字段继续读逐鱼"), Catalog->ResolveBiteTiming(*Fish).TrueBiteWindowSeconds, 11.0);
	Fish->TrueBiteWindowSeconds = -1;
	TestEqual(TEXT("非法资产不能被默认值掩盖"), Catalog->ResolveBiteTiming(*Fish).TrueBiteWindowSeconds, -1.0);
	Fish->TrueBiteWindowSeconds = 0;
	Catalog->BiteTimingOverridesByItemId[Fish->ItemId].TrueBiteWindowSeconds = -2;
	TestEqual(TEXT("非法逐鱼值不能被档位掩盖"), Catalog->ResolveBiteTiming(*Fish).TrueBiteWindowSeconds, -2.0);
	return !HasAnyErrors();
}

#endif
